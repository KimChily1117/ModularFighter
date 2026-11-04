// Windows XInput 장치 조회.
#pragma once

#include "modular_fighter/input.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace modular_fighter::input {

enum class DeviceStatus { Connected, Disconnected, Unavailable, InvalidIndex, Error };

struct DeviceSample {
    DeviceStatus status = DeviceStatus::Unavailable;
    RawPadState raw{};
    std::uint32_t packet_number = 0;
    std::uint32_t error_code = 0;
};

class MF_INPUT_API XInputDevice {
public:
    XInputDevice();
    ~XInputDevice();
    XInputDevice(const XInputDevice&) = delete;
    XInputDevice& operator=(const XInputDevice&) = delete;
    DeviceSample Read(std::uint32_t user_index) const;
    bool Available() const;
    const std::string& LibraryName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

MF_INPUT_API const char* StatusName(DeviceStatus status);

}
