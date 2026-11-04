// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Gameplay Cue 경로 추가 액션.
#pragma once

#include "GameFeatureAction.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/EngineTypes.h"
#include "GameFeatureAction_AddGameplayCuePath.generated.h"

UCLASS(BlueprintType, meta = (DisplayName = "Add Gameplay Cue Path"))

class FIGHTERGAME_API UGameFeatureAction_AddGameplayCuePath final : public UGameFeatureAction
{
    GENERATED_BODY()
public:
    UGameFeatureAction_AddGameplayCuePath();

    const TArray<FDirectoryPath>& GetDirectoryPathsToAdd() const
    {
        return DirectoryPathsToAdd;
    }

private:
    UPROPERTY(EditAnywhere, Category = "Game Feature | Gameplay Cues",
              meta = (RelativeToGameContentDir, LongPackageName))
    TArray<FDirectoryPath> DirectoryPathsToAdd;
};
