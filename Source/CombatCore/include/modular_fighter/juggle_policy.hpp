// 공중 콤보 보정 규칙.
#pragma once
#include <algorithm>
#include <cstdint>

namespace modular_fighter::juggle_policy {

inline constexpr int HitSlope = 7;
inline constexpr int InitialDrift = 12;
inline constexpr int MaximumDrift = 40;

inline int Drift(int combo_hits) noexcept {
    return std::min(MaximumDrift, InitialDrift + HitSlope * std::max(0, combo_hits - 2));
}

inline int Velocity(int height, int landing, int ticks, int gravity) noexcept {
    const auto t = std::int64_t(std::max(1, ticks));

    return static_cast<int>(
        std::clamp<std::int64_t>((gravity * t * (t + 1) / 2 - height + landing) / t, -1000, 1000));
}

inline int LandingTicks(int height, int velocity, int landing, int gravity) noexcept {
    for (int tick = 1; tick <= 512; ++tick) {
        velocity = std::max(-1000, velocity - gravity);
        height = std::min(10000, height + velocity);

        if (velocity <= 0 && height <= landing)
            return tick;
    }

    return 512;
}

struct Entry {
    int frame, ticks;
};

inline Entry SelectEntry(int landing_frame, int ticks, int previous_frame, bool same_clip) noexcept {
    const int candidate = std::max(1, landing_frame - ticks + 1);
    const int difference = previous_frame - candidate;

    if (same_clip && difference > -10 && difference < 10) {
        const int rewind = std::max(1, previous_frame - 10);

        return {rewind, ticks + candidate - rewind};
    }

    return {candidate, ticks};
}
}
