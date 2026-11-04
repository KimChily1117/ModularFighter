// 패드와 키보드 입력을 코어용 프레임 입력으로 바꾼다.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#ifndef MF_INPUT_API
#define MF_INPUT_API
#endif

namespace modular_fighter::input {

enum AttackMask : std::uint8_t { LP = 1, RP = 2, LK = 4, RK = 8 };

inline constexpr std::uint8_t AllAttacks = LP | RP | LK | RK;

enum class Control : std::uint8_t {
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Start,
    Back,
    LeftThumb,
    RightThumb,
    LeftShoulder,
    RightShoulder,
    A,
    B,
    X,
    Y,
    LeftTrigger,
    RightTrigger,
    Count,
    None = 255,
};
inline constexpr std::size_t ControlCount = static_cast<std::size_t>(Control::Count);

namespace pad {
inline constexpr std::uint16_t DpadUp = 0x0001;
inline constexpr std::uint16_t DpadDown = 0x0002;
inline constexpr std::uint16_t DpadLeft = 0x0004;
inline constexpr std::uint16_t DpadRight = 0x0008;
inline constexpr std::uint16_t Start = 0x0010;
inline constexpr std::uint16_t Back = 0x0020;
inline constexpr std::uint16_t LeftThumb = 0x0040;
inline constexpr std::uint16_t RightThumb = 0x0080;
inline constexpr std::uint16_t LeftShoulder = 0x0100;
inline constexpr std::uint16_t RightShoulder = 0x0200;
inline constexpr std::uint16_t A = 0x1000;
inline constexpr std::uint16_t B = 0x2000;
inline constexpr std::uint16_t X = 0x4000;
inline constexpr std::uint16_t Y = 0x8000;
}

struct RawPadState {
    bool connected = false;
    std::uint16_t buttons = 0;
    std::int16_t left_x = 0;
    std::int16_t left_y = 0;
    std::uint8_t left_trigger = 0;
    std::uint8_t right_trigger = 0;
};

struct FrameInput {
    std::int8_t horizontal = 0;
    std::int8_t vertical = 0;
    std::uint8_t attacks = 0;
    bool menu = false;
};

constexpr std::array<std::uint8_t, ControlCount> DefaultAttackBindings() noexcept {
    std::array<std::uint8_t, ControlCount> bindings{};
    bindings[static_cast<std::size_t>(Control::X)] = LP;
    bindings[static_cast<std::size_t>(Control::Y)] = RP;
    bindings[static_cast<std::size_t>(Control::A)] = LK;
    bindings[static_cast<std::size_t>(Control::B)] = RK;

    return bindings;
}

struct Config {
    std::int32_t stick_deadzone = 7849;

    std::uint16_t trigger_threshold = 30;
    std::array<std::uint8_t, ControlCount> attack_bindings = DefaultAttackBindings();
    Control menu_binding = Control::Start;
};

struct RawDigitalState {
    bool active = true;
    bool left = false;
    bool right = false;
    bool up = false;
    bool down = false;
    std::uint8_t attacks = 0;
    bool menu = false;
};

MF_INPUT_API bool operator==(const FrameInput& left, const FrameInput& right) noexcept;
MF_INPUT_API bool operator!=(const FrameInput& left, const FrameInput& right) noexcept;
MF_INPUT_API bool IsValid(const FrameInput& input) noexcept;
MF_INPUT_API bool IsValid(const Config& config) noexcept;

MF_INPUT_API bool SetAttackBinding(Config& config, Control control, std::uint8_t attacks) noexcept;

MF_INPUT_API FrameInput MapPad(const RawPadState& raw, const Config& config = {}) noexcept;

MF_INPUT_API FrameInput MapDigital(const RawDigitalState& raw) noexcept;

}
