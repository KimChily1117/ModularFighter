// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 어빌리티 묶음 부여와 회수.
#include "FighterAbilitySet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterAbilitySet)

DEFINE_LOG_CATEGORY_STATIC(LogFighterAbilitySet, Log, All);

void FFighterAbilitySet_GrantedHandles::AddAbilitySpecHandle(const FGameplayAbilitySpecHandle& Handle)
{
    if (Handle.IsValid())
    {
        AbilitySpecHandles.Add(Handle);
    }
}

void FFighterAbilitySet_GrantedHandles::AddGameplayEffectHandle(const FActiveGameplayEffectHandle& Handle)
{
    if (Handle.IsValid())
    {
        GameplayEffectHandles.Add(Handle);
    }
}

void FFighterAbilitySet_GrantedHandles::AddAttributeSet(UAttributeSet* Set)
{
    GrantedAttributeSets.Add(Set);
}

void FFighterAbilitySet_GrantedHandles::TakeFromAbilitySystem(UAbilitySystemComponent* ASC)
{
    check(ASC);

    if (!ASC->IsOwnerActorAuthoritative())
    {
        return;
    }

    for (const FGameplayAbilitySpecHandle& Handle : AbilitySpecHandles)
    {
        if (Handle.IsValid())
        {
            ASC->ClearAbility(Handle);
        }
    }

    for (const FActiveGameplayEffectHandle& Handle : GameplayEffectHandles)
    {
        if (Handle.IsValid())
        {
            ASC->RemoveActiveGameplayEffect(Handle);
        }
    }

    for (UAttributeSet* Set : GrantedAttributeSets)
    {
        ASC->RemoveSpawnedAttribute(Set);
    }

    AbilitySpecHandles.Reset();
    GameplayEffectHandles.Reset();
    GrantedAttributeSets.Reset();
}

UFighterAbilitySet::UFighterAbilitySet(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

void UFighterAbilitySet::GiveToAbilitySystem(UAbilitySystemComponent* ASC,
                                             FFighterAbilitySet_GrantedHandles* OutGrantedHandles,
                                             UObject* SourceObject) const
{
    check(ASC);

    if (!ASC->IsOwnerActorAuthoritative())
    {
        return;
    }

    for (int32 AbilityIndex = 0; AbilityIndex < GrantedGameplayAbilities.Num(); ++AbilityIndex)
    {
        const FFighterAbilitySet_GameplayAbility& AbilityToGrant = GrantedGameplayAbilities[AbilityIndex];

        if (!IsValid(AbilityToGrant.Ability))
        {
            UE_LOG(LogFighterAbilitySet, Error, TEXT("GrantedGameplayAbilities[%d] on ability set [%s] is not valid."),
                   AbilityIndex, *GetNameSafe(this));
            continue;
        }

        UGameplayAbility* AbilityCDO = AbilityToGrant.Ability->GetDefaultObject<UGameplayAbility>();

        FGameplayAbilitySpec AbilitySpec(AbilityCDO, AbilityToGrant.AbilityLevel);
        AbilitySpec.SourceObject = SourceObject;

        if (AbilityToGrant.InputTag.IsValid())
        {
            AbilitySpec.GetDynamicSpecSourceTags().AddTag(AbilityToGrant.InputTag);
        }

        const FGameplayAbilitySpecHandle AbilitySpecHandle = ASC->GiveAbility(AbilitySpec);

        if (OutGrantedHandles)
        {
            OutGrantedHandles->AddAbilitySpecHandle(AbilitySpecHandle);
        }
    }

    for (int32 EffectIndex = 0; EffectIndex < GrantedGameplayEffects.Num(); ++EffectIndex)
    {
        const FFighterAbilitySet_GameplayEffect& EffectToGrant = GrantedGameplayEffects[EffectIndex];

        if (!IsValid(EffectToGrant.GameplayEffect))
        {
            UE_LOG(LogFighterAbilitySet, Error, TEXT("GrantedGameplayEffects[%d] on ability set [%s] is not valid"),
                   EffectIndex, *GetNameSafe(this));
            continue;
        }

        const UGameplayEffect* GameplayEffect = EffectToGrant.GameplayEffect->GetDefaultObject<UGameplayEffect>();
        const FActiveGameplayEffectHandle GameplayEffectHandle =
            ASC->ApplyGameplayEffectToSelf(GameplayEffect, EffectToGrant.EffectLevel, ASC->MakeEffectContext());

        if (OutGrantedHandles)
        {
            OutGrantedHandles->AddGameplayEffectHandle(GameplayEffectHandle);
        }
    }

    for (int32 SetIndex = 0; SetIndex < GrantedAttributes.Num(); ++SetIndex)
    {
        const FFighterAbilitySet_AttributeSet& SetToGrant = GrantedAttributes[SetIndex];

        if (!IsValid(SetToGrant.AttributeSet))
        {
            UE_LOG(LogFighterAbilitySet, Error, TEXT("GrantedAttributes[%d] on ability set [%s] is not valid"),
                   SetIndex, *GetNameSafe(this));
            continue;
        }

        UAttributeSet* NewSet = NewObject<UAttributeSet>(ASC->GetOwner(), SetToGrant.AttributeSet);
        ASC->AddAttributeSetSubobject(NewSet);

        if (OutGrantedHandles)
        {
            OutGrantedHandles->AddAttributeSet(NewSet);
        }
    }
}
