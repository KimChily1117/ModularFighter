// 히트 상태 변화를 표시 효과로 바꾼다.
#pragma once
#include "CoreMinimal.h"
#include "modular_fighter/combat.hpp"

enum class EFighterHeatEffect : uint8
{
    Burst,
    Engager,
    Dash,
    Smash,
    End,
    AutoRun,
    Powered,
    Count
};

namespace FighterHeatPresentation
{
inline const TCHAR* Name(EFighterHeatEffect Kind)
{
    switch (Kind)
    {
    case EFighterHeatEffect::Burst:
        return TEXT("burst");
    case EFighterHeatEffect::Engager:
        return TEXT("engager");
    case EFighterHeatEffect::Dash:
        return TEXT("dash");
    case EFighterHeatEffect::Smash:
        return TEXT("smash");
    case EFighterHeatEffect::End:
        return TEXT("end");
    case EFighterHeatEffect::AutoRun:
        return TEXT("autorun");
    default:
        return TEXT("powered");
    }
}

struct FTransition
{
    bool Start = false, Burst = false, Dash = false, Smash = false, End = false, AutoRun = false;
};

inline FTransition Detect(const modular_fighter::Fighter& Before, const modular_fighter::Fighter& After)
{
    namespace C = modular_fighter;
    const auto* Attack = C::GetAttackDefinition(After.character, After.active_command);
    FTransition R;
    R.Start = !Before.heat_used && After.heat_frames > 0;
    R.Burst = R.Start && Attack && Attack->system_action == C::SystemAction::HeatBurst;
    R.End = Before.heat_frames > 0 && After.heat_frames == 0;
    R.Dash = R.End && After.move == C::MoveKind::HeatDash;
    R.Smash = R.End && Attack && Attack->system_action == C::SystemAction::HeatSmash;
    R.AutoRun = Before.move != C::MoveKind::EngagerRun && After.move == C::MoveKind::EngagerRun;

    return R;
}

inline uint64 Key(uint32 Frame, int32 Fighter, EFighterHeatEffect Kind)
{
    return uint64(Frame) * 16 + uint64(Fighter) * 8 + uint8(Kind);
}
}
