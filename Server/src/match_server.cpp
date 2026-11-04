// 매치 흐름 구현.
#include "modular_fighter/match_server.hpp"

#include <algorithm>
#include <type_traits>

namespace modular_fighter::net {

// 새 연결을 등록하고 Welcome을 보낸다.
std::uint32_t MatchServer::Connect(std::vector<Outgoing>& out) {
    const auto id = next_client_++;
    clients_[id] = Client{};
    out.push_back({id, Welcome{id}});

    return id;
}

std::optional<Phase> MatchServer::RoomPhaseOf(std::uint32_t client_id) const {
    const auto c = clients_.find(client_id);

    if (c == clients_.end() || c->second.room == 0)
        return std::nullopt;

    return rooms_.at(c->second.room).phase;
}

// 메시지 종류와 방 상태에 따라 처리한다.
void MatchServer::Receive(std::uint32_t client_id, const Message& message, std::vector<Outgoing>& out) {
    const auto found = clients_.find(client_id);

    if (found == clients_.end())
        return;
    auto& client = found->second;

    if (const auto* hello = std::get_if<Hello>(&message)) {
        if (hello->protocol != ProtocolVersion)
            return Fail(client_id, ErrorCode::VersionMismatch, out);
        client.hello = true;
        client.fingerprint = hello->rules_fingerprint;

        return;
    }

    if (!client.hello)
        return Fail(client_id, ErrorCode::VersionMismatch, out);

    if (std::holds_alternative<QueueMatch>(message)) {
        if (client.room != 0 || std::find(queue_.begin(), queue_.end(), client_id) != queue_.end())
            return Fail(client_id, ErrorCode::WrongPhase, out);
        queue_.push_back(client_id);

        return TryPair(out);
    }

    if (std::holds_alternative<Leave>(message))
        return Disconnect(client_id, out);

    if (client.room == 0)
        return Fail(client_id, ErrorCode::NotInRoom, out);
    auto& room = rooms_.at(client.room);
    auto& self = room.slots[client.slot];
    auto& other = room.slots[1 - client.slot];

    if (const auto* pick = std::get_if<SelectSide>(&message)) {
        if (room.phase != Phase::SideSelect)
            return Fail(client_id, ErrorCode::WrongPhase, out);

        self.side =
            other.side != pick->side ? pick->side : (pick->side == Side::Left ? Side::Right : Side::Left);

        if (other.side != Side::None)
            room.phase = Phase::CharacterSelect;

        return Broadcast(room, out);
    }

    if (const auto* pick = std::get_if<SelectCharacter>(&message)) {
        if (room.phase != Phase::CharacterSelect)
            return Fail(client_id, ErrorCode::WrongPhase, out);

        if (pick->character != CharacterId::Kazuya && pick->character != CharacterId::Paul)
            return Fail(client_id, ErrorCode::InvalidCharacter, out);
        self.character = pick->character;
        self.ready = false;

        return Broadcast(room, out);
    }

    if (const auto* pick = std::get_if<SelectStage>(&message)) {
        if (room.phase != Phase::CharacterSelect)
            return Fail(client_id, ErrorCode::WrongPhase, out);

        if (self.ready)
            return Fail(client_id, ErrorCode::NotReady, out);
        self.stage = pick->stage;

        return Broadcast(room, out);
    }

    if (std::holds_alternative<Ready>(message)) {
        if (room.phase != Phase::CharacterSelect)
            return Fail(client_id, ErrorCode::WrongPhase, out);

        if (self.character == CharacterId::Training)
            return Fail(client_id, ErrorCode::NotReady, out);
        self.ready = true;

        if (other.ready)
            return StartMatch(room, out);

        return Broadcast(room, out);
    }

    if (const auto* loaded = std::get_if<StageLoaded>(&message)) {
        if (room.phase != Phase::Loading || loaded->match_id != room.match_id)
            return Fail(client_id, ErrorCode::WrongPhase, out);
        self.loaded = true;

        if (other.loaded)
            return StartRound(room, out);

        return Broadcast(room, out);
    }

    if (const auto* batch = std::get_if<InputBatch>(&message)) {
        if (room.phase != Phase::InMatch)
            return Fail(client_id, ErrorCode::WrongPhase, out);
        out.push_back({other.client, *batch});

        return;
    }

    if (const auto* report = std::get_if<ResultReport>(&message)) {
        if (room.phase != Phase::InMatch)
            return Fail(client_id, ErrorCode::WrongPhase, out);
        self.report = *report;

        if (!other.report)
            return;

        const auto& a = *room.slots[0].report;
        const auto& b = *room.slots[1].report;
        const bool agreed = a.frame == b.frame && a.winner == b.winner && a.state_hash == b.state_hash;

        if (!agreed)
            return Finish(room, ResultStatus::Desync, 2, out);

        if (a.winner == 0 || a.winner == 2)
            ++room.wins[0];

        if (a.winner == 1 || a.winner == 2)
            ++room.wins[1];
        const bool left_done = room.wins[0] >= rounds_to_win_, right_done = room.wins[1] >= rounds_to_win_;

        if (left_done || right_done)
            return Finish(room, ResultStatus::Agreed, left_done && right_done ? 2 : left_done ? 0 : 1, out);

        return StartRound(room, out);
    }

    if (const auto* rematch = std::get_if<Rematch>(&message)) {
        if (room.phase != Phase::Result)
            return Fail(client_id, ErrorCode::WrongPhase, out);

        if (!rematch->accept)
            return CloseRoom(client.room, out);
        self.rematch = true;

        if (other.rematch)
            return StartMatch(room, out);

        return Broadcast(room, out);
    }

    Fail(client_id, ErrorCode::BadMessage, out);
}

// 연결 해제를 처리하고 방을 정리한다.
void MatchServer::Disconnect(std::uint32_t client_id, std::vector<Outgoing>& out) {
    const auto found = clients_.find(client_id);

    if (found == clients_.end())
        return;
    queue_.erase(std::remove(queue_.begin(), queue_.end(), client_id), queue_.end());

    if (const auto room_id = found->second.room) {
        auto& room = rooms_.at(room_id);
        const auto slot = found->second.slot;
        room.slots[slot].client = 0;

        if (room.phase == Phase::InMatch || room.phase == Phase::Loading)
            Finish(room, ResultStatus::Disconnect, static_cast<std::uint8_t>(room.slots[1 - slot].side), out);
        clients_.erase(found);
        CloseRoom(room_id, out);

        return;
    }

    clients_.erase(found);
}

// 규칙 지문이 같은 플레이어끼리 짝을 짓는다.
void MatchServer::TryPair(std::vector<Outgoing>& out) {
    for (auto first = queue_.begin(); first != queue_.end(); ++first)
        for (auto second = std::next(first); second != queue_.end(); ++second) {
            if (clients_[*first].fingerprint != clients_[*second].fingerprint)
                continue;
            const auto a = *first, b = *second;

            queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                        [a, b](std::uint32_t id) { return id == a || id == b; }),
                         queue_.end());
            Room room;
            room.id = next_room_++;
            room.slots[0].client = a;
            room.slots[1].client = b;
            clients_[a].room = clients_[b].room = room.id;
            clients_[a].slot = 0;
            clients_[b].slot = 1;
            Broadcast(rooms_[room.id] = room, out);

            return;
        }
}

void MatchServer::Broadcast(const Room& room, std::vector<Outgoing>& out) const {
    for (std::uint8_t slot = 0; slot < 2; ++slot) {
        if (!room.slots[slot].client)
            continue;
        RoomState view{room.id, slot, room.phase, {}};

        for (std::size_t i = 0; i < 2; ++i) {
            const auto& s = room.slots[i];
            view.slots[i] = SlotView{s.client != 0, s.side, s.character, s.ready, s.rematch, s.stage};
        }

        out.push_back({room.slots[slot].client, view});
    }
}

// 경기 설정을 확정하고 시작한다.
void MatchServer::StartMatch(Room& room, std::vector<Outgoing>& out) {
    room.match_id = next_match_++;

    const auto& left_slot = room.slots[0].side == Side::Left ? room.slots[0] : room.slots[1];
    const auto& right_slot = room.slots[0].side == Side::Left ? room.slots[1] : room.slots[0];
    room.stage =
        left_slot.stage == right_slot.stage || room.match_id % 2 == 1 ? left_slot.stage : right_slot.stage;
    room.round = 0;
    room.wins = {};

    room.phase = Phase::Loading;
    const auto& left = room.slots[0].side == Side::Left ? room.slots[0] : room.slots[1];
    const auto& right = room.slots[0].side == Side::Left ? room.slots[1] : room.slots[0];

    for (auto& s : room.slots) {
        s.loaded = false;
        s.ready = s.rematch = false;

        if (s.client)
            out.push_back({s.client, LoadStage{room.match_id, room.stage, left.character, right.character}});
    }

    Broadcast(room, out);
}

// 새 라운드를 시작한다.
void MatchServer::StartRound(Room& room, std::vector<Outgoing>& out) {
    room.phase = Phase::InMatch;
    ++room.round;
    const auto& left = room.slots[0].side == Side::Left ? room.slots[0] : room.slots[1];
    const auto& right = room.slots[0].side == Side::Left ? room.slots[1] : room.slots[0];

    for (std::uint8_t slot = 0; slot < 2; ++slot) {
        auto& s = room.slots[slot];
        s.ready = s.rematch = false;
        s.report.reset();
        out.push_back(
            {s.client, MatchStart{room.match_id, slot, s.side, left.character, right.character, input_delay_,
                                  room.round, room.wins[0], room.wins[1], rounds_to_win_, room.stage}});
    }

    Broadcast(room, out);
}

void MatchServer::Finish(Room& room, ResultStatus status, std::uint8_t winner, std::vector<Outgoing>& out) {
    room.phase = Phase::Result;

    for (const auto& s : room.slots)
        if (s.client)
            out.push_back({s.client, MatchResult{room.match_id, status, winner}});
    Broadcast(room, out);
}

void MatchServer::CloseRoom(std::uint32_t room_id, std::vector<Outgoing>& out) {
    auto& room = rooms_.at(room_id);
    room.phase = Phase::Closed;
    Broadcast(room, out);

    for (const auto& s : room.slots)
        if (s.client)
            clients_[s.client].room = 0;
    rooms_.erase(room_id);
}

}
