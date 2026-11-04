// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 월드 단위 Game Feature 액션 공통 처리.
#include "GameFeatureAction_WorldActionBase.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureAction_WorldActionBase)

void UGameFeatureAction_WorldActionBase::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
    GameInstanceStartHandles.FindOrAdd(Context) = FWorldDelegates::OnStartGameInstance.AddUObject(
        this, &UGameFeatureAction_WorldActionBase::HandleGameInstanceStart, FGameFeatureStateChangeContext(Context));

    for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
    {
        if (Context.ShouldApplyToWorldContext(WorldContext))
        {
            AddToWorld(WorldContext, Context);
        }
    }
}

void UGameFeatureAction_WorldActionBase::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
    FDelegateHandle* FoundHandle = GameInstanceStartHandles.Find(Context);

    if (ensure(FoundHandle))
    {
        FWorldDelegates::OnStartGameInstance.Remove(*FoundHandle);
    }
}

void UGameFeatureAction_WorldActionBase::HandleGameInstanceStart(UGameInstance* GameInstance,
                                                                 FGameFeatureStateChangeContext ChangeContext)
{
    if (FWorldContext* WorldContext = GameInstance->GetWorldContext())
    {
        if (ChangeContext.ShouldApplyToWorldContext(*WorldContext))
        {
            AddToWorld(*WorldContext, ChangeContext);
        }
    }
}
