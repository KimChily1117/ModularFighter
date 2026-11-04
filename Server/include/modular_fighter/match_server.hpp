// 소켓과 분리한 매치 서버 로직. 입력 중계와 결과 합의만 맡고 시뮬레이션은 실행하지 않는다.
#pragma once

#include "modular_fighter/match_protocol.hpp"
#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <vector>

namespace modular_fighter::net {

struct Outgoing {
    std::uint32_t client_id;
    Message message;
};

// 접속, 매칭, 캐릭터 선택, 입력 중계, 재대결을 처리한다.
class MatchServer {
public:
    explicit MatchServer(std::uint8_t input_delay = 1, std::uint8_t rounds_to_win = 3)
        : input_delay_(input_delay), rounds_to_win_(rounds_to_win) {}

    std::uint32_t Connect(std::vector<Outgoing>& out);
    // 수신 메시지를 처리하고 보낼 메시지를 out에 담는다.
    void Receive(std::uint32_t client_id, const Message& message, std::vector<Outgoing>& out);
    void Disconnect(std::uint32_t client_id, std::vector<Outgoing>& out);

    std::size_t QueuedCount() const {
        return queue_.size();
    }

    std::size_t RoomCount() const {
        return rooms_.size();
    }

    std::optional<Phase> RoomPhaseOf(std::uint32_t client_id) const;

private:
    struct Client {
        bool hello = false;
        std::uint64_t fingerprint = 0;
        std::uint32_t room = 0;
        std::uint8_t slot = 0;
    };

    struct Slot {
        std::uint32_t client = 0;
        Side side = Side::None;
        CharacterId character = CharacterId::Training;
        bool ready = false;
        bool rematch = false;
        std::uint8_t stage = 0;
        bool loaded = false;
        std::optional<ResultReport> report;
    };

    struct Room {
        std::uint32_t id = 0;
        Phase phase = Phase::SideSelect;
        std::array<Slot, 2> slots{};
        std::uint32_t match_id = 0;
        std::uint8_t round = 0;
        std::array<std::uint8_t, 2> wins{};
        std::uint8_t stage = 0;
    };

    // 대기열의 두 플레이어로 방을 만든다.
    void TryPair(std::vector<Outgoing>& out);
    void Broadcast(const Room& room, std::vector<Outgoing>& out) const;
    void StartMatch(Room& room, std::vector<Outgoing>& out);
    void StartRound(Room& room, std::vector<Outgoing>& out);
    // 경기 결과를 확정해 양쪽에 알린다.
    void Finish(Room& room, ResultStatus status, std::uint8_t winner, std::vector<Outgoing>& out);
    void CloseRoom(std::uint32_t room_id, std::vector<Outgoing>& out);

    static void Fail(std::uint32_t client, ErrorCode code, std::vector<Outgoing>& out) {
        out.push_back({client, Error{code}});
    }

    std::uint8_t input_delay_;
    std::uint8_t rounds_to_win_;
    std::uint32_t next_client_ = 1, next_room_ = 1, next_match_ = 1;
    std::map<std::uint32_t, Client> clients_;
    std::deque<std::uint32_t> queue_;
    std::map<std::uint32_t, Room> rooms_;
};

}
