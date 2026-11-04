// 입력 예측이 틀리면 스냅샷으로 되돌려 다시 계산한다.
#include "modular_fighter/rollback.hpp"
#include <limits>
#include <stdexcept>

namespace modular_fighter {
namespace {
std::array<Input, 2> Both(std::uint8_t local_player, Input local, Input remote) {
    std::array<Input, 2> inputs{};
    inputs[local_player] = local;
    inputs[1u - local_player] = remote;

    return inputs;
}
}

RollbackSession::RollbackSession(std::uint8_t local_player, std::size_t history_limit, CharacterId first,
                                 CharacterId second, bool enable_hitstop)
    : state_(MakeInitialState(first, second)), local_player_(local_player), history_limit_(history_limit) {
    if (local_player > 1 || history_limit < 1 || history_limit > 600)
        throw std::invalid_argument("player must be 0/1 and history limit 1..600");
    state_.hitstop_enabled = enable_hitstop ? 1 : 0;

    if (!IsValid(state_))
        throw std::invalid_argument("unsupported character profile");
}

AdvanceResult RollbackSession::Advance(Input local) {
    if (!IsValid(local))
        return AdvanceResult::InvalidInput;

    if (state_.frame == std::numeric_limits<std::uint32_t>::max())
        return AdvanceResult::FrameOverflow;

    if (history_.size() == history_limit_ && !history_.front().remote)
        return AdvanceResult::AwaitingRemote;
    Input predicted = history_.empty() ? baseline_remote_ : history_.back().used_remote;
    auto received = pending_.find(state_.frame);
    std::optional<Input> actual;

    if (received != pending_.end())
        actual = received->second;
    const Input remote = actual.value_or(predicted);
    State next = state_;

    if (!Step(next, Both(local_player_, local, remote)))
        throw std::logic_error("validated rollback state could not step");

    if (history_.size() == history_limit_) {
        baseline_remote_ = history_.front().used_remote;
        history_.pop_front();
    }

    history_.push_back(Frame{state_, local, actual, remote});

    if (received != pending_.end())
        pending_.erase(received);
    state_ = next;

    return AdvanceResult::Advanced;
}

ReceiveResult RollbackSession::ReceiveRemote(std::uint32_t frame, Input input) {
    if (!IsValid(input))
        return ReceiveResult::InvalidInput;

    if (frame >= state_.frame) {
        if (frame - state_.frame >= history_limit_)
            return ReceiveResult::TooFarAhead;
        const auto inserted = pending_.emplace(frame, input);

        if (inserted.second)
            return ReceiveResult::Accepted;

        return inserted.first->second == input ? ReceiveResult::Duplicate : ReceiveResult::Conflict;
    }

    if (history_.empty() || frame < history_.front().before.frame)
        return ReceiveResult::TooOld;
    const auto index = static_cast<std::size_t>(frame - history_.front().before.frame);
    auto& record = history_.at(index);

    if (record.remote)
        return *record.remote == input ? ReceiveResult::Duplicate : ReceiveResult::Conflict;
    record.remote = input;

    if (!(record.used_remote == input))
        ReplayFrom(index);

    return ReceiveResult::Accepted;
}

const State& RollbackSession::ConfirmedState() const {
    for (const auto& record : history_)
        if (!record.remote)
            return record.before;

    return state_;
}

const State* RollbackSession::ConfirmedStateAt(std::uint32_t frame) const {
    if (frame > ConfirmedFrame())
        return nullptr;

    if (frame == state_.frame)
        return &state_;

    if (history_.empty() || frame < history_.front().before.frame)
        return nullptr;

    return &history_[frame - history_.front().before.frame].before;
}

void RollbackSession::ReplayFrom(std::size_t index) {
    State replay = history_[index].before;
    Input predicted = index == 0 ? baseline_remote_ : history_[index - 1].used_remote;

    for (std::size_t i = index; i < history_.size(); ++i) {
        auto& record = history_[i];
        record.before = replay;
        record.used_remote = record.remote.value_or(predicted);

        if (!Step(replay, Both(local_player_, record.local, record.used_remote)))
            throw std::logic_error("rollback replay could not step");
        predicted = record.used_remote;
        ++resimulated_frames_;
    }

    state_ = replay;
    ++rollback_count_;
}
}
