// 연습 모드 기능.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "modular_fighter/combat.hpp"
#include "modular_fighter/input.hpp"
#include "FighterTrainingComponent.generated.h"

class AFighterArena;
class UFighterPawnData;

UCLASS(ClassGroup = (Fighter))

class FIGHTERGAME_API UFighterTrainingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFighterTrainingComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    void ToggleDummyGuard()
    {
        bDummyGuard = !bDummyGuard;
    }

    void CycleDummyAttack();

    void CycleDummyInterval()
    {
        DummyIntervalIndex = (DummyIntervalIndex + 1) % 3;
    }

    void ToggleDummyCrouch()
    {
        bDummyCrouch = !bDummyCrouch;
    }

    void CycleCounterMode()
    {
        CounterMode = (CounterMode + 1) % 3;
    }

    uint8 GetCounterMode() const
    {
        return CounterMode;
    }

    void CycleRageMode()
    {
        RageMode = (RageMode + 1) % 3;
    }

    uint8 GetRageMode() const
    {
        return RageMode;
    }

    void ResetTrackers();

    struct FDummyControl
    {
        bool bForceGuard = false;
        bool bNoGuard = false;
        bool bCrouch = false;
    };

    FDummyControl ControlDummy(const modular_fighter::State& State, int32 DummyIndex, bool bPadDriven,
                               modular_fighter::input::FrameInput& InOutInput, const UFighterPawnData* DummyData);

    void ObserveInput(const modular_fighter::State& Before, const modular_fighter::Input& Input, int32 Index,
                      const UFighterPawnData* Data);
    void ObserveResult(const modular_fighter::State& After, int32 Index, const UFighterPawnData* Data);

    void ObserveStep(const modular_fighter::State& Before, const modular_fighter::State& After);

    bool IsDummyGuarding() const
    {
        return bDummyGuard;
    }

    bool IsDummyCrouching() const
    {
        return bDummyCrouch;
    }

    struct FComboDisplay
    {
        int32 Hits = 0;
        int32 Damage = 0;
        int32 LastDamage = 0;
        uint32 LastHitFrame = 0;
    };

    const FComboDisplay& GetComboDisplay(int32 Attacker) const
    {
        return Combos[Attacker == 1 ? 1 : 0];
    }

    FString GetDummyAttackLabel() const;
    FString GetJustFrameLabel(const UFighterPawnData* Data) const;
    FString GetFrameAdvantageLabel() const;

private:
    const AFighterArena* GetArena() const;

    FComboDisplay Combos[2];
    bool bDummyGuard = false;
    bool bDummyCrouch = false;
    uint8 CounterMode = 0;
    uint8 RageMode = 0;
    uint8 DummyAttackMode = 0;
    uint8 DummyIntervalIndex = 1;
    int32 DummyWait = 0;
    int32 DummyScriptFrame = -1;

    int32 JustDirectionFrame = -1;
    int32 JustButtonFrame = -1;
    int32 JustTimingOffset = 0;
    bool bJustTimingValid = false;
    bool bJustTimingHit = false;

    int32 AdvAttacker = -1;
    int32 AdvSteps = 0;
    int32 AdvAttackerFree = -1;
    int32 AdvDefenderFree = -1;
    bool bAdvHit = false;
    bool bAdvReaction = false;
    FString AdvMoveName;
    FString AdvAttackerName;
};
