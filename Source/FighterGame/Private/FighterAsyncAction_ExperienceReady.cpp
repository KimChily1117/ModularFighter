// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience 로딩 완료를 기다리는 비동기 액션.
#include "FighterAsyncAction_ExperienceReady.h"
#include "FighterExperienceManagerComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "TimerManager.h"

UFighterAsyncAction_ExperienceReady::UFighterAsyncAction_ExperienceReady(const FObjectInitializer& ObjectInitializer) {}

UFighterAsyncAction_ExperienceReady*
UFighterAsyncAction_ExperienceReady::WaitForExperienceReady(UObject* WorldContextObject)
{
    UFighterAsyncAction_ExperienceReady* Action = nullptr;

    if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
    {
        Action = NewObject<UFighterAsyncAction_ExperienceReady>();
        Action->WorldPtr = World;
        Action->RegisterWithGameInstance(World);
    }

    return Action;
}

void UFighterAsyncAction_ExperienceReady::Activate()
{
    if (UWorld* World = WorldPtr.Get())
    {
        if (AGameStateBase* GameState = World->GetGameState())
        {
            Step2_ListenToExperienceLoading(GameState);
        }
        else
        {
            World->GameStateSetEvent.AddUObject(this, &ThisClass::Step1_HandleGameStateSet);
        }
    }
    else
    {
        SetReadyToDestroy();
    }
}

void UFighterAsyncAction_ExperienceReady::Step1_HandleGameStateSet(AGameStateBase* GameState)
{
    if (UWorld* World = WorldPtr.Get())
    {
        World->GameStateSetEvent.RemoveAll(this);
    }

    Step2_ListenToExperienceLoading(GameState);
}

void UFighterAsyncAction_ExperienceReady::Step2_ListenToExperienceLoading(AGameStateBase* GameState)
{
    check(GameState);
    UFighterExperienceManagerComponent* FighterExperienceManagerComponent =
        GameState->FindComponentByClass<UFighterExperienceManagerComponent>();
    check(FighterExperienceManagerComponent);

    if (FighterExperienceManagerComponent->IsExperienceLoaded())
    {
        UWorld* World = GameState->GetWorld();
        check(World);

        World->GetTimerManager().SetTimerForNextTick(
            FTimerDelegate::CreateUObject(this, &ThisClass::Step4_BroadcastReady));
    }
    else
    {
        FighterExperienceManagerComponent->CallOrRegister_OnExperienceLoaded(
            FOnFighterExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::Step3_HandleExperienceLoaded));
    }
}

void UFighterAsyncAction_ExperienceReady::Step3_HandleExperienceLoaded(
    const UFighterExperienceDefinition* CurrentExperience)
{
    Step4_BroadcastReady();
}

void UFighterAsyncAction_ExperienceReady::Step4_BroadcastReady()
{
    OnReady.Broadcast();
    SetReadyToDestroy();
}
