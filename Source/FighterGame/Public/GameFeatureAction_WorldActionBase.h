// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 월드 단위 Game Feature 액션의 기반 클래스.
#pragma once

#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureAction_WorldActionBase.generated.h"

class FDelegateHandle;
class UGameInstance;
struct FGameFeatureActivatingContext;
struct FGameFeatureDeactivatingContext;
struct FGameFeatureStateChangeContext;
struct FWorldContext;

UCLASS(Abstract)

class FIGHTERGAME_API UGameFeatureAction_WorldActionBase : public UGameFeatureAction
{
    GENERATED_BODY()
public:
    virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
    virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

private:
    void HandleGameInstanceStart(UGameInstance* GameInstance, FGameFeatureStateChangeContext ChangeContext);

    virtual void AddToWorld(const FWorldContext& WorldContext, const FGameFeatureStateChangeContext& ChangeContext)
        PURE_VIRTUAL(UGameFeatureAction_WorldActionBase::AddToWorld, );

private:
    TMap<FGameFeatureStateChangeContext, FDelegateHandle> GameInstanceStartHandles;
};
