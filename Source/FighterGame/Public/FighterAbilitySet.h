// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 캐릭터별 어빌리티, 이펙트, 어트리뷰트 묶음.
#pragma once

#include "ActiveGameplayEffectHandle.h"
#include "Engine/DataAsset.h"
#include "AttributeSet.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "FighterAbilitySet.generated.h"

class UAbilitySystemComponent;
class UAttributeSet;
class UGameplayAbility;
class UGameplayEffect;
class UObject;

USTRUCT(BlueprintType)

struct FFighterAbilitySet_GameplayAbility
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<UGameplayAbility> Ability = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 AbilityLevel = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Meta = (Categories = "InputTag"))
    FGameplayTag InputTag;
};

USTRUCT(BlueprintType)

struct FFighterAbilitySet_GameplayEffect
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<UGameplayEffect> GameplayEffect = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float EffectLevel = 1.0f;
};

USTRUCT(BlueprintType)

struct FFighterAbilitySet_AttributeSet
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<UAttributeSet> AttributeSet;
};

USTRUCT(BlueprintType)

struct FFighterAbilitySet_GrantedHandles
{
    GENERATED_BODY()
public:
    void AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle);
    void AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle);
    void AddAttributeSet(UAttributeSet* Set);

    void TakeFromAbilitySystem(UAbilitySystemComponent* ASC);

    int32 NumAbilities() const
    {
        return AbilitySpecHandles.Num();
    }

protected:
    UPROPERTY()
    TArray<FGameplayAbilitySpecHandle> AbilitySpecHandles;

    UPROPERTY()
    TArray<FActiveGameplayEffectHandle> GameplayEffectHandles;

    UPROPERTY()
    TArray<TObjectPtr<UAttributeSet>> GrantedAttributeSets;
};

UCLASS(BlueprintType, Const)

class FIGHTERGAME_API UFighterAbilitySet : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UFighterAbilitySet(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    void GiveToAbilitySystem(UAbilitySystemComponent* ASC, FFighterAbilitySet_GrantedHandles* OutGrantedHandles,
                             UObject* SourceObject = nullptr) const;

protected:
    UPROPERTY(EditAnywhere, Category = "Gameplay Abilities", meta = (TitleProperty = Ability))
    TArray<FFighterAbilitySet_GameplayAbility> GrantedGameplayAbilities;

    UPROPERTY(EditAnywhere, Category = "Gameplay Effects", meta = (TitleProperty = GameplayEffect))
    TArray<FFighterAbilitySet_GameplayEffect> GrantedGameplayEffects;

    UPROPERTY(EditAnywhere, Category = "Attribute Sets", meta = (TitleProperty = AttributeSet))
    TArray<FFighterAbilitySet_AttributeSet> GrantedAttributes;
};
