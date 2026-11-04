// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience에 함께 적용할 Game Feature 액션 묶음.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FighterExperienceActionSet.generated.h"

class UGameFeatureAction;

UCLASS(BlueprintType)

class FIGHTERGAME_API UFighterExperienceActionSet : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UFighterExperienceActionSet();

#if WITH_EDITORONLY_DATA
    virtual void UpdateAssetBundleData() override;
#endif

    UPROPERTY(EditAnywhere, Category = "Actions to Perform")
    TArray<TObjectPtr<UGameFeatureAction>> Actions;
};
