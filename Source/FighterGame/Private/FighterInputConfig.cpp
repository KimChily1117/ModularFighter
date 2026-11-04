// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 입력 액션 조회.
#include "FighterInputConfig.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterInputConfig)

DEFINE_LOG_CATEGORY_STATIC(LogFighterInputConfig, Log, All);

UFighterInputConfig::UFighterInputConfig(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

const UInputAction* UFighterInputConfig::FindNativeInputActionForTag(const FGameplayTag& InputTag,
                                                                     bool bLogNotFound) const
{
    for (const FFighterInputAction& Action : NativeInputActions)
    {
        if (Action.InputAction && (Action.InputTag == InputTag))
        {
            return Action.InputAction;
        }
    }

    if (bLogNotFound)
    {
        UE_LOG(LogFighterInputConfig, Error,
               TEXT("Can't find NativeInputAction for InputTag [%s] on InputConfig [%s]."), *InputTag.ToString(),
               *GetNameSafe(this));
    }

    return nullptr;
}

const UInputAction* UFighterInputConfig::FindAbilityInputActionForTag(const FGameplayTag& InputTag,
                                                                      bool bLogNotFound) const
{
    for (const FFighterInputAction& Action : AbilityInputActions)
    {
        if (Action.InputAction && (Action.InputTag == InputTag))
        {
            return Action.InputAction;
        }
    }

    if (bLogNotFound)
    {
        UE_LOG(LogFighterInputConfig, Error,
               TEXT("Can't find AbilityInputAction for InputTag [%s] on InputConfig [%s]."), *InputTag.ToString(),
               *GetNameSafe(this));
    }

    return nullptr;
}
