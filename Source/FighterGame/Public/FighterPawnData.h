// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 캐릭터별 설정 데이터.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "modular_fighter/combat.hpp"
#include "FighterPawnData.generated.h"

class UFighterAbilitySet;
class UFighterInputConfig;

UENUM(BlueprintType)
enum class EFighterCharacter : uint8
{
    Training = 0,
    Kazuya = 1,
    Paul = 2,
};
static_assert(static_cast<uint8>(EFighterCharacter::Kazuya) ==
                      static_cast<uint8>(modular_fighter::CharacterId::Kazuya) &&
                  static_cast<uint8>(EFighterCharacter::Paul) == static_cast<uint8>(modular_fighter::CharacterId::Paul),
              "EFighterCharacter must match the generated CharacterId values");

USTRUCT(BlueprintType)

struct FFighterScriptedInput
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Direction = 5;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Buttons = 0;
};

USTRUCT(BlueprintType)

struct FFighterJustFrameTrainer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 JustCommand = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 NearCommand = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Direction = 3;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 Button = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString JustName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString NearName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FString Hint;

    bool IsEnabled() const
    {
        return JustCommand != 0;
    }
};

UCLASS(BlueprintType)

class FIGHTERGAME_API UFighterPawnData : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UFighterPawnData(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    modular_fighter::CharacterId GetCoreCharacter() const
    {
        return static_cast<modular_fighter::CharacterId>(CoreCharacter);
    }

    FString GetCompiledModuleHash() const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    EFighterCharacter CoreCharacter = EFighterCharacter::Training;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FString ModuleDataHash;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FName CharacterCode;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FString CharacterSlug;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
    FGameplayTag CharacterTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    FString ReferenceAttackClip;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
    FLinearColor ProxyColor = FLinearColor(0.5f, 0.5f, 0.5f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
    TObjectPtr<UFighterInputConfig> InputConfig;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training")
    FString CommandHint;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training")
    FString StartupSummary;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training")
    TMap<int32, FString> CommandLabels;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training")
    TArray<int32> ReferenceClipCommands;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training")
    TArray<FFighterScriptedInput> DummySourceMove;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Training")
    FFighterJustFrameTrainer JustFrame;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abilities")
    TArray<TObjectPtr<UFighterAbilitySet>> AbilitySets;
};
