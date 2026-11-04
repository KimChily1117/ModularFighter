// 캐릭터 추가 액션.
#pragma once

#include "CoreMinimal.h"
#include "GameFeatureAction.h"
#include "GameFeatureAction_AddFighter.generated.h"

class UFighterPawnData;

UCLASS(meta = (DisplayName = "Add Fighter"))

class FIGHTERGAME_API UGameFeatureAction_AddFighter : public UGameFeatureAction
{
    GENERATED_BODY()
public:
    virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
    virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;

    UPROPERTY(EditAnywhere, Category = "Fighter")
    TArray<TSoftObjectPtr<UFighterPawnData>> Fighters;

private:
    FString SourceName() const;
};
