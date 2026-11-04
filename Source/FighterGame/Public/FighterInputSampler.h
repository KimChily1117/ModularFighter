// 플레이어 입력 샘플러.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "FighterMappableConfigPair.h"
#include "modular_fighter/input.hpp"
#include "FighterInputSampler.generated.h"

class APlayerController;
class UFighterInputConfig;
class UInputComponent;

UCLASS(ClassGroup = (Fighter))

class FIGHTERGAME_API UFighterInputSampler : public UActorComponent
{
    GENERATED_BODY()
public:
    UFighterInputSampler(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    bool InitializePlayerInput(APlayerController* PlayerController, UInputComponent* PlayerInputComponent,
                               const UFighterInputConfig* InputConfig);
    void RemovePlayerInput();

    bool IsReadyToBindInputs() const
    {
        return bReadyToBindInputs;
    }

    modular_fighter::input::RawDigitalState Sample();

    bool ConsumeCommand(const FGameplayTag& CommandTag);
    void ResetState();

    uint8 GetSeenFightMask() const
    {
        return SeenFightMask;
    }

    bool HasSeenCommand(const FGameplayTag& CommandTag) const
    {
        return SeenCommands.HasTagExact(CommandTag);
    }

    const UFighterInputConfig* GetInputConfig() const
    {
        return BoundConfig;
    }

    int32 GetMappingCount() const
    {
        return MappingCount;
    }

    static uint8 FightBit(const FGameplayTag& InputTag);

    UPROPERTY(EditAnywhere, Category = "Input")
    TArray<FFighterMappableConfigPair> DefaultInputMappings;

    void Input_FightTagPressed(FGameplayTag InputTag);
    void Input_FightTagReleased(FGameplayTag InputTag);
    void Input_CommandTagStarted(FGameplayTag InputTag);

private:
    TWeakObjectPtr<UInputComponent> BoundInputComponent;
    TWeakObjectPtr<APlayerController> BoundController;
    UPROPERTY()
    TObjectPtr<const UFighterInputConfig> BoundConfig;
    TArray<uint32> InputBindingHandles;
    TArray<TObjectPtr<const class UInputMappingContext>> AddedContexts;
    uint8 HeldFight = 0;
    uint8 PendingFight = 0;
    uint8 SeenFightMask = 0;
    FGameplayTagContainer PendingCommands;
    FGameplayTagContainer SeenCommands;
    int32 MappingCount = 0;
    bool bReadyToBindInputs = false;
};
