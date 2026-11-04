// Winsock 초기화와 메시지 단위 TCP 연결.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "modular_fighter/match_protocol.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace modular_fighter::net {

// Winsock 수명 관리.
class WinsockScope {
public:
    WinsockScope() {
        WSADATA data;
        ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }

    ~WinsockScope() {
        if (ok_)
            WSACleanup();
    }

    WinsockScope(const WinsockScope&) = delete;
    WinsockScope& operator=(const WinsockScope&) = delete;

    bool Ok() const {
        return ok_;
    }

private:
    bool ok_ = false;
};

inline bool ConfigureStream(SOCKET socket) {
    u_long nonblocking = 1;
    BOOL nodelay = TRUE;

    return ioctlsocket(socket, FIONBIO, &nonblocking) == 0 &&
           setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay),
                      sizeof nodelay) == 0;
}

// 소켓 하나의 송수신 버퍼 관리.
class Connection {
public:
    explicit Connection(SOCKET socket = INVALID_SOCKET) : socket_(socket) {}

    Connection(Connection&& other) noexcept {
        *this = std::move(other);
    }

    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            Close();
            socket_ = other.socket_;
            other.socket_ = INVALID_SOCKET;
            outbox_ = std::move(other.outbox_);
            reader_ = std::move(other.reader_);
        }

        return *this;
    }

    ~Connection() {
        Close();
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    SOCKET Socket() const {
        return socket_;
    }

    bool Open() const {
        return socket_ != INVALID_SOCKET;
    }

    void Send(const Message& message) {
        const auto bytes = Encode(message);
        outbox_.insert(outbox_.end(), bytes.begin(), bytes.end());
    }

    bool WantsWrite() const {
        return !outbox_.empty();
    }

    bool Flush() {
        while (!outbox_.empty()) {
            const int sent = send(socket_, reinterpret_cast<const char*>(outbox_.data()),
                                  static_cast<int>(outbox_.size()), 0);

            if (sent == SOCKET_ERROR)
                return WSAGetLastError() == WSAEWOULDBLOCK;
            outbox_.erase(outbox_.begin(), outbox_.begin() + sent);
        }

        return true;
    }

    bool Read(std::vector<Message>& messages) {
        std::uint8_t buffer[4096];

        for (;;) {
            const int got = recv(socket_, reinterpret_cast<char*>(buffer), sizeof buffer, 0);

            if (got == 0)
                return false;

            if (got == SOCKET_ERROR) {
                if (WSAGetLastError() != WSAEWOULDBLOCK)
                    return false;
                break;
            }

            reader_.Append(buffer, static_cast<std::size_t>(got));
        }
        while (auto message = reader_.Next())
            messages.push_back(std::move(*message));

        return !reader_.Failed();
    }

    void Close() {
        if (socket_ != INVALID_SOCKET)
            closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

private:
    SOCKET socket_ = INVALID_SOCKET;
    std::vector<std::uint8_t> outbox_;
    FrameReader reader_;
};

}
