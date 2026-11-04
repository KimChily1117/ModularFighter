// 규칙 기반 CPU 상대.
#pragma once

#include "modular_fighter/combat.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifndef MF_CORE_API
#define MF_CORE_API
#endif

namespace modular_fighter {

struct RouteStep {
    std::function<bool(const State&)> wait;
    std::vector<Input> inputs;
};

MF_CORE_API std::vector<RouteStep> ParseRoute(const std::string& spec, std::uint8_t player,
                                              std::uint8_t extra_buttons, std::string& error);

class MF_CORE_API CpuController {
public:
    explicit CpuController(std::uint8_t player = 0, std::uint32_t seed = 1);
    Input Next(const State& state);

    const char* Activity() const {
        return activity_;
    }

    int CombosStarted() const {
        return combos_started_;
    }

private:
    enum class Mode : std::uint8_t { Neutral, Hold, Route };
    std::uint32_t Random();
    Input Screen(const State& state, Input forward_relative) const;
    void StartRoute(const State& state, const std::string& spec);
    void StartHold(Input input, int frames, const char* label);

    std::uint8_t player_;
    std::uint32_t rng_;
    Mode mode_ = Mode::Neutral;
    std::vector<RouteStep> steps_;
    std::size_t step_ = 0, cursor_ = 0;
    bool playing_ = false;
    bool combo_landed_ = false;
    std::uint32_t mode_frame_ = 0, step_wait_frame_ = 0, next_decision_ = 0;
    Input hold_{};
    int hold_frames_ = 0;
    bool guard_low_ = false;
    bool guarding_ = true;
    std::uint16_t guarded_attack_ = 0;
    bool punish_ready_ = false;
    int combos_started_ = 0;
    const char* activity_ = "neutral";
};

}
