// GAS 표시 계층. 코어 상태를 보여주기만 한다.
#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayCueInterface.h"
#include "FighterAbilitySet.h"
#include "GameFramework/Actor.h"
#include "FighterPawnData.h"
#include "modular_fighter/combat.hpp"
#include "FighterAbilityHost.generated.h"

struct FFighterCombatCue;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnFighterCombatCue, const FGameplayTag&, const FGameplayCueParameters&);

UCLASS()

class FIGHTERGAME_API UFighterAttributeSet : public UAttributeSet
{
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadOnly, Category = "Fighter")
    FGameplayAttributeData Health;
    UPROPERTY(BlueprintReadOnly, Category = "Fighter")
    FGameplayAttributeData MaximumHealth;
    UPROPERTY(BlueprintReadOnly, Category = "Fighter")
    FGameplayAttributeData MoveFrame;
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UFighterAttributeSet, Health)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UFighterAttributeSet, MaximumHealth)
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UFighterAttributeSet, MoveFrame)
};

UCLASS()

class FIGHTERGAME_API UFighterMoveAbility : public UGameplayAbility
{
    GENERATED_BODY()
public:
    UFighterMoveAbility();
    virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData) override;
};

UCLASS()

class FIGHTERGAME_API AFighterAbilityHost : public AActor, public IAbilitySystemInterface, public IGameplayCueInterface
{
    GENERATED_BODY()
public:
    AFighterAbilityHost();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
    {
        return AbilitySystem;
    }

    void InitializeCharacter(modular_fighter::CharacterId Character, const UFighterPawnData* InPawnData = nullptr);
    void Synchronize(const modular_fighter::Fighter& Fighter, uint32 Frame, bool bEmitPresentationEvents = true);
    void ResetPresentation();
    void OnMoveAbilityActivated(const FGameplayEventData& Event);

    int32 GetActivationCount() const
    {
        return ActivationCount;
    }

    uint16 GetPresentedCommand() const
    {
        return PresentedCommand;
    }

    const UFighterAttributeSet* GetAttributes() const
    {
        return Attributes;
    }

    const UFighterPawnData* GetPawnData() const
    {
        return PawnData;
    }

    bool IsMirrorOf(const modular_fighter::Fighter& Fighter) const;

    bool ExecuteCombatCue(const FFighterCombatCue& Cue, const FVector& Location, bool bEmitPresentationEvents = true);

    int32 GetReceivedCueCount(const FGameplayTag& CueTag) const
    {
        return ReceivedCues.FindRef(CueTag);
    }

    int32 GetReceivedCueTotal() const;
    FOnFighterCombatCue OnCombatCue;

    using IGameplayCueInterface::HandleGameplayCue;
    virtual void HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag, EGameplayCueEvent::Type EventType,
                                   const FGameplayCueParameters& Parameters) override;

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;
    UPROPERTY()
    TObjectPtr<UFighterAttributeSet> Attributes;
    UPROPERTY()
    TObjectPtr<const UFighterPawnData> PawnData;
    FGameplayTag AppliedCharacterTag;
    FFighterAbilitySet_GrantedHandles GrantedHandles;
    TArray<FGameplayAbilitySpecHandle> FallbackHandles;
    TSet<uint64> EmittedCueKeys;
    TMap<FGameplayTag, int32> ReceivedCues;
    bool bInitialized = false;
    bool bObserved = false;
    uint32 LastFrame = 0;
    uint16 LastMoveFrame = 0;
    uint16 PresentedCommand = 0;
    uint16 LastCommand = 0;
    int32 ActivationCount = 0;
};
