// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Input Config를 Enhanced Input에 연결한다.
#pragma once

#include "CoreMinimal.h"
#include "InputTriggers.h"
#include "InputActionValue.h"
#include "FighterInputConfig.h"
#include "EnhancedInputComponent.h"
#include "FighterInputComponent.generated.h"

UCLASS()

class FIGHTERGAME_API UFighterInputComponent : public UEnhancedInputComponent
{
    GENERATED_BODY()
public:
    UFighterInputComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    template <class UserClass, typename FuncType>
    uint32 BindNativeAction(const UFighterInputConfig* InputConfig, const FGameplayTag& InputTag,
                            ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, bool bLogIfNotFound);

    template <class UserClass, typename PressedFuncType, typename ReleasedFuncType>
    void BindAbilityActions(const UFighterInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc,
                            ReleasedFuncType ReleasedFunc, TArray<uint32>& BindHandles);

    template <class UserClass, typename FuncType>
    void BindNativeTagActions(const UFighterInputConfig* InputConfig, ETriggerEvent TriggerEvent, UserClass* Object,
                              FuncType Func, TArray<uint32>& BindHandles);
};

template <class UserClass, typename FuncType>
uint32 UFighterInputComponent::BindNativeAction(const UFighterInputConfig* InputConfig, const FGameplayTag& InputTag,
                                                ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func,
                                                bool bLogIfNotFound)
{
    check(InputConfig);

    if (const UInputAction* IA = InputConfig->FindNativeInputActionForTag(InputTag, bLogIfNotFound))
    {
        return BindAction(IA, TriggerEvent, Object, Func).GetHandle();
    }

    return 0;
}

template <class UserClass, typename PressedFuncType, typename ReleasedFuncType>
void UFighterInputComponent::BindAbilityActions(const UFighterInputConfig* InputConfig, UserClass* Object,
                                                PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc,
                                                TArray<uint32>& BindHandles)
{
    check(InputConfig);

    for (const FFighterInputAction& Action : InputConfig->AbilityInputActions)
    {
        if (Action.InputAction && Action.InputTag.IsValid())
        {
            if (PressedFunc)
            {
                BindHandles.Add(
                    BindAction(Action.InputAction, ETriggerEvent::Triggered, Object, PressedFunc, Action.InputTag)
                        .GetHandle());
            }

            if (ReleasedFunc)
            {
                BindHandles.Add(
                    BindAction(Action.InputAction, ETriggerEvent::Completed, Object, ReleasedFunc, Action.InputTag)
                        .GetHandle());
            }
        }
    }
}

template <class UserClass, typename FuncType>
void UFighterInputComponent::BindNativeTagActions(const UFighterInputConfig* InputConfig, ETriggerEvent TriggerEvent,
                                                  UserClass* Object, FuncType Func, TArray<uint32>& BindHandles)
{
    check(InputConfig);

    for (const FFighterInputAction& Action : InputConfig->NativeInputActions)
    {
        if (Action.InputAction && Action.InputTag.IsValid())
        {
            BindHandles.Add(BindAction(Action.InputAction, TriggerEvent, Object, Func, Action.InputTag).GetHandle());
        }
    }
}
