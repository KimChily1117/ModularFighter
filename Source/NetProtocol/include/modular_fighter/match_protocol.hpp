// 매치 서버와 주고받는 메시지 정의.
#pragma once

#include "modular_fighter/combat.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#ifndef MF_NET_API
#define MF_NET_API
#endif

namespace modular_fighter::net {

inline constexpr std::uint16_t ProtocolVersion = 6;

inline constexpr std::uint8_t StageCount = 2;
inline constexpr std::size_t MaxBatchInputs = 16;
inline constexpr std::size_t MaxFramePayload = 1024;

enum class Side : std::uint8_t { Left = 0, Right = 1, None = 0xFF };

enum class Phase : std::uint8_t {
    Waiting = 0,
    SideSelect,
    CharacterSelect,
    InMatch,
    Result,
    Closed,
    Loading
};
enum class ResultStatus : std::uint8_t { Agreed = 0, Desync = 1, Disconnect = 2 };
enum class ErrorCode : std::uint8_t {
    BadMessage = 1,
    VersionMismatch,
    WrongPhase,
    SideTaken,
    InvalidCharacter,
    NotReady,
    NotInRoom,
};

struct Hello {
    std::uint16_t protocol = ProtocolVersion;
    std::uint64_t rules_fingerprint = 0;
};

struct QueueMatch {};

struct SelectSide {
    Side side = Side::None;
};

struct SelectCharacter {
    CharacterId character = CharacterId::Training;
};

struct Ready {};

struct ResultReport {
    std::uint32_t frame = 0;
    std::uint8_t winner = 0;
    std::uint64_t state_hash = 0;
};

struct Rematch {
    bool accept = false;
};

struct Leave {};

struct SelectStage {
    std::uint8_t stage = 0;
};

struct StageLoaded {
    std::uint32_t match_id = 0;
};

struct InputBatch {
    std::uint32_t first_frame = 0;
    std::uint32_t ack_frame = 0;
    std::vector<Input> inputs;
};

struct Welcome {
    std::uint32_t client_id = 0;
};

struct SlotView {
    bool connected = false;
    Side side = Side::None;
    CharacterId character = CharacterId::Training;
    bool ready = false;
    bool rematch = false;
    std::uint8_t stage = 0;
};

struct RoomState {
    std::uint32_t room_id = 0;
    std::uint8_t your_slot = 0;
    Phase phase = Phase::Waiting;
    std::array<SlotView, 2> slots{};
};

struct MatchStart {
    std::uint32_t match_id = 0;
    std::uint8_t your_slot = 0;
    Side your_side = Side::None;
    CharacterId left = CharacterId::Training, right = CharacterId::Training;
    std::uint8_t input_delay = 0;
    std::uint8_t round = 1;
    std::uint8_t left_wins = 0, right_wins = 0;
    std::uint8_t rounds_to_win = 2;
    std::uint8_t stage = 0;
};

struct LoadStage {
    std::uint32_t match_id = 0;
    std::uint8_t stage = 0;
    CharacterId left = CharacterId::Training, right = CharacterId::Training;
};

struct MatchResult {
    std::uint32_t match_id = 0;
    ResultStatus status = ResultStatus::Agreed;
    std::uint8_t winner = 0;
};

struct Error {
    ErrorCode code = ErrorCode::BadMessage;
};

using Message = std::variant<Hello, QueueMatch, SelectSide, SelectCharacter, Ready, ResultReport, Rematch,
                             Leave, InputBatch, Welcome, RoomState, MatchStart, MatchResult, Error,
                             SelectStage, StageLoaded, LoadStage>;

constexpr std::uint32_t VsScreenFrames = 240, RoundCallDelay = 12, ReadyOffset = 84, FightOffset = 108,
                        FightLandOffset = 118;

inline std::uint32_t RoundCallFrame(std::uint8_t round) {
    return (round <= 1 ? VsScreenFrames : 0u) + RoundCallDelay;
}

inline std::uint32_t RoundIntroFrames(std::uint8_t round) {
    return RoundCallFrame(round) + FightLandOffset;
}

inline Input IntroInput(const Input& input, std::uint32_t frame, std::uint8_t round) {
    if (frame >= RoundIntroFrames(round))
        return input;
    Input neutral{};
    neutral.view_flipped = input.view_flipped;

    return neutral;
}

inline constexpr std::uint32_t DefaultMatchFrames = 60 * 60;

inline std::uint32_t RoundFrameLimit(std::uint8_t round, std::uint32_t fight_frames = DefaultMatchFrames) {
    return RoundIntroFrames(round) + fight_frames;
}

inline constexpr std::uint32_t RoundEndHoldFrames = 150;

inline bool MatchFinished(const State& confirmed, std::uint32_t limit = DefaultMatchFrames) {
    return confirmed.fighters[0].hp == 0 || confirmed.fighters[1].hp == 0 || confirmed.frame >= limit;
}

inline std::uint8_t MatchWinner(const State& s) {
    return s.fighters[0].hp == s.fighters[1].hp ? 2 : (s.fighters[0].hp > s.fighters[1].hp ? 0 : 1);
}

template <class Session>
const State* FirstFinishedFrame(const Session& session, std::uint32_t& next_check,
                                std::uint32_t limit = DefaultMatchFrames) {
    while (next_check <= session.ConfirmedFrame()) {
        const State* state = session.ConfirmedStateAt(next_check);

        if (state && MatchFinished(*state, limit))
            return state;
        ++next_check;
    }

    return nullptr;
}

MF_NET_API std::vector<std::uint8_t> Encode(const Message& message);

MF_NET_API std::optional<Message> Decode(const std::uint8_t* data, std::size_t size);

class MF_NET_API FrameReader {
public:
    void Append(const std::uint8_t* data, std::size_t size);

    std::optional<Message> Next();

    bool Failed() const {
        return failed_;
    }

private:
    std::vector<std::uint8_t> buffer_;
    bool failed_ = false;
};

}
