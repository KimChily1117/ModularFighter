// 입력 정규화 구현.
#include "modular_fighter/input.hpp"

namespace modular_fighter::input {
namespace {
constexpr std::array<std::uint16_t, 14> DigitalControlBits{
    pad::DpadUp,    pad::DpadDown,   pad::DpadLeft,     pad::DpadRight,     pad::Start, pad::Back,
    pad::LeftThumb, pad::RightThumb, pad::LeftShoulder, pad::RightShoulder, pad::A,     pad::B,
    pad::X,         pad::Y,
};
constexpr std::uint16_t DpadBits = pad::DpadUp | pad::DpadDown | pad::DpadLeft | pad::DpadRight;

bool ValidAttackMask(std::uint8_t mask) noexcept {
    return (mask & static_cast<std::uint8_t>(~AllAttacks)) == 0;
}

std::int8_t Direction(bool negative, bool positive) noexcept {
    return static_cast<std::int8_t>(static_cast<int>(positive) - static_cast<int>(negative));
}

std::int8_t AxisDirection(std::int16_t raw, std::int32_t deadzone) noexcept {
    const std::int32_t axis = raw;

    return static_cast<std::int8_t>(axis > deadzone ? 1 : (axis < -deadzone ? -1 : 0));
}

bool Held(const RawPadState& raw, const Config& config, Control control) noexcept {
    const auto index = static_cast<std::size_t>(control);

    if (index < DigitalControlBits.size()) {
        return (raw.buttons & DigitalControlBits[index]) != 0;
    }

    if (control == Control::LeftTrigger)
        return raw.left_trigger > config.trigger_threshold;

    if (control == Control::RightTrigger)
        return raw.right_trigger > config.trigger_threshold;

    return false;
}
}

bool operator==(const FrameInput& left, const FrameInput& right) noexcept {
    return left.horizontal == right.horizontal && left.vertical == right.vertical &&
           left.attacks == right.attacks && left.menu == right.menu;
}

bool operator!=(const FrameInput& left, const FrameInput& right) noexcept {
    return !(left == right);
}

bool IsValid(const FrameInput& input) noexcept {
    return input.horizontal >= -1 && input.horizontal <= 1 && input.vertical >= -1 && input.vertical <= 1 &&
           ValidAttackMask(input.attacks);
}

bool IsValid(const Config& config) noexcept {
    if (config.stick_deadzone < 0 || config.stick_deadzone > 32766 || config.trigger_threshold > 255) {
        return false;
    }

    if (config.menu_binding != Control::None &&
        static_cast<std::size_t>(config.menu_binding) >= ControlCount) {
        return false;
    }

    for (const auto mask : config.attack_bindings) {
        if (!ValidAttackMask(mask))
            return false;
    }

    return true;
}

bool SetAttackBinding(Config& config, Control control, std::uint8_t attacks) noexcept {
    const auto index = static_cast<std::size_t>(control);

    if (index >= ControlCount || !ValidAttackMask(attacks))
        return false;
    config.attack_bindings[index] = attacks;

    return true;
}

FrameInput MapPad(const RawPadState& raw, const Config& config) noexcept {
    if (!raw.connected || !IsValid(config))
        return {};
    FrameInput frame;

    if ((raw.buttons & DpadBits) != 0) {
        frame.horizontal = Direction((raw.buttons & pad::DpadLeft) != 0, (raw.buttons & pad::DpadRight) != 0);
        frame.vertical = Direction((raw.buttons & pad::DpadDown) != 0, (raw.buttons & pad::DpadUp) != 0);
    } else {
        frame.horizontal = AxisDirection(raw.left_x, config.stick_deadzone);
        frame.vertical = AxisDirection(raw.left_y, config.stick_deadzone);
    }

    for (std::size_t index = 0; index < ControlCount; ++index) {
        if (Held(raw, config, static_cast<Control>(index))) {
            frame.attacks |= config.attack_bindings[index];
        }
    }

    frame.menu = Held(raw, config, config.menu_binding);

    return frame;
}

FrameInput MapDigital(const RawDigitalState& raw) noexcept {
    if (!raw.active || !ValidAttackMask(raw.attacks))
        return {};

    return FrameInput{Direction(raw.left, raw.right), Direction(raw.down, raw.up), raw.attacks, raw.menu};
}

}
