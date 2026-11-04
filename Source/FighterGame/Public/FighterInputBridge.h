// 짧은 입력을 다음 시뮬레이션 프레임까지 유지한다.
#pragma once

#include "CoreMinimal.h"
#include "modular_fighter/combat.hpp"
#include "modular_fighter/input.hpp"

namespace FighterInputBridge
{
struct FIGHTERGAME_API FAttackEdgeLatch
{
    void Capture(const modular_fighter::input::FrameInput& Input);
    modular_fighter::input::FrameInput Consume(const modular_fighter::input::FrameInput& Current);
    void Reset();

private:
    uint8 PreviousHeld = 0;
    uint8 PendingPresses = 0;
};

FIGHTERGAME_API modular_fighter::input::FrameInput ToCoreScreenAxes(const modular_fighter::input::FrameInput& Input,
                                                                    bool bViewFlipped);

FIGHTERGAME_API modular_fighter::Input ToCombatInput(const modular_fighter::input::FrameInput& Input,
                                                     const modular_fighter::State& State, uint8 PlayerIndex,
                                                     bool bForceGuard = false, bool bNoGuard = false);
}
