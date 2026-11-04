// 코어 결과에서 나온 표시 큐. 재시뮬레이션해도 같은 키가 나와 중복을 막는다.
#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"
#include "modular_fighter/combat.hpp"

namespace FighterGameplayTags
{
FIGHTERGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Fighter_Hit);
FIGHTERGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Fighter_CounterHit);
FIGHTERGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Fighter_Guard);
FIGHTERGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Fighter_KO);
FIGHTERGAME_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fighter_CueContext_Tornado);
}

struct FIGHTERGAME_API FFighterCombatCue
{
    int32 Target = 0;
    FGameplayTag Tag;
    int32 Damage = 0;
    uint16 Command = 0;
    uint32 Frame = 0;
    bool bAirborneContact = false;
    bool bTornadoContact = false;

    uint64 Key() const;
};

namespace FighterCombatCues
{
FIGHTERGAME_API TArray<FFighterCombatCue> Detect(const modular_fighter::State& Before,
                                                 const modular_fighter::State& After);
}
