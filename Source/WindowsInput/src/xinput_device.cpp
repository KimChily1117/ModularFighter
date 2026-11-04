// XInput DLL을 동적으로 불러와 패드 상태를 읽는다.
#include "modular_fighter/xinput_device.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <array>

namespace modular_fighter::input {

struct XInputDevice::Impl {
    using GetState = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
    HMODULE module = nullptr;
    GetState get_state = nullptr;
    std::string library;
    DWORD load_error = ERROR_MOD_NOT_FOUND;

    ~Impl() {
        if (module)
            FreeLibrary(module);
    }
};

XInputDevice::XInputDevice() : impl_(std::make_unique<Impl>()) {
    std::array<wchar_t, MAX_PATH + 1> directory{};
    const UINT count = GetSystemDirectoryW(directory.data(), static_cast<UINT>(directory.size()));

    if (count == 0 || count >= directory.size()) {
        impl_->load_error = count == 0 ? GetLastError() : ERROR_INSUFFICIENT_BUFFER;

        return;
    }

    struct Library {
        const wchar_t* wide;
        const char* name;
    };

    for (const auto& candidate :
         {Library{L"xinput1_4.dll", "xinput1_4.dll"}, Library{L"xinput9_1_0.dll", "xinput9_1_0.dll"}}) {
        const std::wstring path = std::wstring(directory.data(), count) + L"\\" + candidate.wide;
        HMODULE module = LoadLibraryW(path.c_str());

        if (!module) {
            impl_->load_error = GetLastError();
            continue;
        }

        const auto get_state = reinterpret_cast<Impl::GetState>(GetProcAddress(module, "XInputGetState"));

        if (!get_state) {
            impl_->load_error = GetLastError();
            FreeLibrary(module);
            continue;
        }

        impl_->module = module;
        impl_->get_state = get_state;
        impl_->library = candidate.name;
        impl_->load_error = ERROR_SUCCESS;
        break;
    }
}

XInputDevice::~XInputDevice() = default;

bool XInputDevice::Available() const {
    return impl_->get_state != nullptr;
}

const std::string& XInputDevice::LibraryName() const {
    return impl_->library;
}

DeviceSample XInputDevice::Read(std::uint32_t user_index) const {
    DeviceSample sample;

    if (user_index >= XUSER_MAX_COUNT) {
        sample.status = DeviceStatus::InvalidIndex;
        sample.error_code = ERROR_BAD_ARGUMENTS;

        return sample;
    }

    if (!Available()) {
        sample.error_code = impl_->load_error;

        return sample;
    }

    XINPUT_STATE state{};
    const DWORD result = impl_->get_state(user_index, &state);
    sample.error_code = result;

    if (result != ERROR_SUCCESS) {
        sample.status =
            result == ERROR_DEVICE_NOT_CONNECTED ? DeviceStatus::Disconnected : DeviceStatus::Error;

        return sample;
    }

    sample.status = DeviceStatus::Connected;
    sample.packet_number = state.dwPacketNumber;
    sample.raw.connected = true;
    sample.raw.buttons = state.Gamepad.wButtons;
    sample.raw.left_x = state.Gamepad.sThumbLX;
    sample.raw.left_y = state.Gamepad.sThumbLY;
    sample.raw.left_trigger = state.Gamepad.bLeftTrigger;
    sample.raw.right_trigger = state.Gamepad.bRightTrigger;

    return sample;
}

const char* StatusName(DeviceStatus status) {
    switch (status) {
    case DeviceStatus::Connected:
        return "connected";
    case DeviceStatus::Disconnected:
        return "disconnected";
    case DeviceStatus::Unavailable:
        return "API unavailable";
    case DeviceStatus::InvalidIndex:
        return "invalid index";
    case DeviceStatus::Error:
        return "device error";
    }

    return "unknown";
}
}
