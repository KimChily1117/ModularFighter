// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience 로딩 완료 대기 노드.
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "FighterAsyncAction_ExperienceReady.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FExperienceReadyAsyncDelegate);

class AGameStateBase;
class UFighterExperienceDefinition;

UCLASS()

class FIGHTERGAME_API UFighterAsyncAction_ExperienceReady : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()
public:
    UFighterAsyncAction_ExperienceReady(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"))
    static UFighterAsyncAction_ExperienceReady* WaitForExperienceReady(UObject* WorldContextObject);

    virtual void Activate() override;

    void Step1_HandleGameStateSet(AGameStateBase* GameState);
    void Step2_ListenToExperienceLoading(AGameStateBase* GameState);
    void Step3_HandleExperienceLoaded(const UFighterExperienceDefinition* CurrentExperience);
    void Step4_BroadcastReady();

    UPROPERTY(BlueprintAssignable)
    FExperienceReadyAsyncDelegate OnReady;

    TWeakObjectPtr<UWorld> WorldPtr;
};
