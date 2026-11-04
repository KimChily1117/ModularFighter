// Winsock 기반 TCP 서버 진입점.
#include "modular_fighter/match_server.hpp"
#include "tcp.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

using namespace modular_fighter::net;

namespace {
const char* Name(const Message& m) {
    static const char* names[] = {
        "Hello",       "QueueMatch", "SelectSide",  "SelectCharacter", "Ready",     "ResultReport",
        "Rematch",     "Leave",      "InputBatch",  "Welcome",         "RoomState", "MatchStart",
        "MatchResult", "Error",      "SelectStage", "StageLoaded",     "LoadStage"};
    static_assert(sizeof(names) / sizeof(names[0]) == std::variant_size_v<Message>,
                  "name every protocol message");

    return names[m.index()];
}

void Log(const std::string& text) {
    using namespace std::chrono;
    const auto ms = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    std::cout << '[' << ms << "] " << text << std::endl;
}
}

// 소켓 이벤트를 받아 MatchServer에 전달하는 메인 루프.
int main(int argc, char** argv) {
    unsigned short port = 7777;
    int input_delay = 1;
    int rounds = 3;

    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];

        if (key == "--port")
            port = static_cast<unsigned short>(std::atoi(argv[i + 1]));
        else if (key == "--input-delay")
            input_delay = std::atoi(argv[i + 1]);
        else if (key == "--rounds")
            rounds = std::atoi(argv[i + 1]);
    }

    WinsockScope winsock;

    if (!winsock.Ok()) {
        std::cerr << "WSAStartup failed\n";

        return 1;
    }

    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    BOOL reuse = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof reuse);

    if (listener == INVALID_SOCKET ||
        bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof address) == SOCKET_ERROR ||
        listen(listener, SOMAXCONN) == SOCKET_ERROR || !ConfigureStream(listener)) {
        std::cerr << "listen failed on port " << port << " (" << WSAGetLastError() << ")\n";

        return 1;
    }

    Log("listening on port " + std::to_string(port) + ", input delay " + std::to_string(input_delay) +
        " frames, protocol " + std::to_string(ProtocolVersion));

    MatchServer server(static_cast<std::uint8_t>(input_delay), static_cast<std::uint8_t>(rounds));
    std::map<std::uint32_t, Connection> connections;
    std::map<std::uint32_t, std::uint64_t> relayed;
    std::vector<Outgoing> out;

    auto deliver = [&] {
        for (auto& o : out) {
            const auto found = connections.find(o.client_id);

            if (found == connections.end())
                continue;

            if (!std::holds_alternative<InputBatch>(o.message))
                Log("-> " + std::to_string(o.client_id) + " " + Name(o.message));
            found->second.Send(o.message);
        }

        out.clear();
    };
    auto drop = [&](std::uint32_t id, const char* why) {
        Log("client " + std::to_string(id) + " closed (" + why + "), relayed batches " +
            std::to_string(relayed[id]));
        connections.erase(id);
        relayed.erase(id);
        server.Disconnect(id, out);
        deliver();
    };

    for (;;) {
        fd_set reads, writes;
        FD_ZERO(&reads);
        FD_ZERO(&writes);
        FD_SET(listener, &reads);

        for (const auto& [id, c] : connections) {
            FD_SET(c.Socket(), &reads);

            if (c.WantsWrite())
                FD_SET(c.Socket(), &writes);
        }

        timeval timeout{0, 100000};

        if (select(0, &reads, &writes, nullptr, &timeout) == SOCKET_ERROR) {
            std::cerr << "select failed\n";

            return 1;
        }

        if (FD_ISSET(listener, &reads)) {
            for (;;) {
                SOCKET accepted = accept(listener, nullptr, nullptr);

                if (accepted == INVALID_SOCKET)
                    break;

                if (!ConfigureStream(accepted) || connections.size() >= 60) {
                    closesocket(accepted);
                    continue;
                }

                const auto id = server.Connect(out);
                connections.emplace(id, Connection(accepted));
                Log("client " + std::to_string(id) + " connected");
                deliver();
            }
        }

        std::vector<std::pair<std::uint32_t, const char*>> closed;

        for (auto& [id, c] : connections) {
            if (FD_ISSET(c.Socket(), &reads)) {
                std::vector<Message> messages;
                const bool open = c.Read(messages);

                for (const auto& m : messages) {
                    if (std::holds_alternative<InputBatch>(m))
                        ++relayed[id];
                    else
                        Log("<- " + std::to_string(id) + " " + Name(m));
                    server.Receive(id, m, out);
                }

                if (!open)
                    closed.push_back({id, "read"});
            }
        }

        deliver();

        for (auto& [id, c] : connections)
            if (c.WantsWrite() && !c.Flush())
                closed.push_back({id, "write"});

        for (const auto& [id, why] : closed)
            if (connections.count(id))
                drop(id, why);
    }
}
