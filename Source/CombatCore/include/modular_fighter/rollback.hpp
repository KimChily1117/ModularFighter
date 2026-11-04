// 입력 예측과 재시뮬레이션을 담당하는 롤백 세션.
#pragma once

#include "modular_fighter/combat.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>

namespace modular_fighter {

enum class AdvanceResult { Advanced, InvalidInput, AwaitingRemote, FrameOverflow };
enum class ReceiveResult { Accepted, Duplicate, Conflict, TooOld, TooFarAhead, InvalidInput };

// 확정 프레임의 스냅샷을 보관하고 입력이 늦게 오면 되돌려 다시 계산한다.
class MF_CORE_API RollbackSession {
public:
    explicit RollbackSession(std::uint8_t local_player = 0, std::size_t history_limit = 120,
                             CharacterId first = CharacterId::Training,
                             CharacterId second = CharacterId::Training, bool enable_hitstop = false);
    AdvanceResult Advance(Input local);
    ReceiveResult ReceiveRemote(std::uint32_t frame, Input input);

    const State& GetState() const {
        return state_;
    }

    std::size_t HistorySize() const {
        return history_.size();
    }

    std::size_t PendingSize() const {
        return pending_.size();
    }

    std::uint64_t RollbackCount() const {
        return rollback_count_;
    }

    std::uint64_t ResimulatedFrames() const {
        return resimulated_frames_;
    }

    std::uint32_t ConfirmedFrame() const {
        return ConfirmedState().frame;
    }

    const State& ConfirmedState() const;

    const State* ConfirmedStateAt(std::uint32_t frame) const;

private:
    struct Frame {
        State before;
        Input local;
        std::optional<Input> remote;
        Input used_remote;
    };

    void ReplayFrom(std::size_t index);
    State state_;
    std::uint8_t local_player_;
    std::size_t history_limit_;
    Input baseline_remote_{};
    std::deque<Frame> history_;
    std::map<std::uint32_t, Input> pending_;
    std::uint64_t rollback_count_ = 0;
    std::uint64_t resimulated_frames_ = 0;
};
}
