// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 입력 태그와 액션 매핑 데이터.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "FighterInputConfig.generated.h"

class UInputAction;

USTRUCT(BlueprintType)

struct FFighterInputAction
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<const UInputAction> InputAction = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Meta = (Categories = "InputTag"))
    FGameplayTag InputTag;
};

UCLASS(BlueprintType)

class FIGHTERGAME_API UFighterInputConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    UFighterInputConfig(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    const UInputAction* FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;
    const UInputAction* FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound = true) const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
    TArray<FFighterInputAction> NativeInputActions;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Meta = (TitleProperty = "InputAction"))
    TArray<FFighterInputAction> AbilityInputActions;
};
