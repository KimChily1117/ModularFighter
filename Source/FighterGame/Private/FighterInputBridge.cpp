// 화면 방향과 코어 방향의 변환.
#include "FighterInputBridge.h"

void FighterInputBridge::FAttackEdgeLatch::Capture(const modular_fighter::input::FrameInput& Input)
{
    if (!modular_fighter::input::IsValid(Input))
    {
        Reset();

        return;
    }

    PendingPresses |= Input.attacks & static_cast<uint8>(~PreviousHeld);
    PreviousHeld = Input.attacks;
}

modular_fighter::input::FrameInput
FighterInputBridge::FAttackEdgeLatch::Consume(const modular_fighter::input::FrameInput& Current)
{
    if (!modular_fighter::input::IsValid(Current))
    {
        Reset();

        return {};
    }

    auto Result = Current;
    Result.attacks |= PendingPresses;
    PendingPresses = 0;

    return Result;
}

void FighterInputBridge::FAttackEdgeLatch::Reset()
{
    PreviousHeld = 0;
    PendingPresses = 0;
}

modular_fighter::input::FrameInput FighterInputBridge::ToCoreScreenAxes(const modular_fighter::input::FrameInput& Input,
                                                                        bool bViewFlipped)
{
    auto Result = Input;

    if (bViewFlipped)
        Result.horizontal = static_cast<decltype(Result.horizontal)>(-Result.horizontal);

    return Result;
}

modular_fighter::Input FighterInputBridge::ToCombatInput(const modular_fighter::input::FrameInput& Input,
                                                         const modular_fighter::State& State, uint8 PlayerIndex,
                                                         bool bForceGuard, bool bNoGuard)
{
    namespace Core = modular_fighter;
    namespace Physical = modular_fighter::input;

    if (PlayerIndex > 1 || !Physical::IsValid(Input))
        return {};
    (void)State;
    Core::Input Result;
    Result.horizontal = Input.horizontal;
    Result.vertical = Input.vertical;

    if (Input.attacks & Physical::LP)
        Result.buttons |= Core::LP;

    if (Input.attacks & Physical::RP)
        Result.buttons |= Core::RP;

    if (Input.attacks & Physical::LK)
        Result.buttons |= Core::LK;

    if (Input.attacks & Physical::RK)
        Result.buttons |= Core::RK;

    if (bForceGuard)
        Result.buttons |= Core::Guard;
    else if (bNoGuard)
        Result.buttons |= Core::NoGuard;

    return Result;
}
