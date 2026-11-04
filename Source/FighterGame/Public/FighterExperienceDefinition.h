// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 모드별 Pawn Data와 Game Feature 구성.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FighterExperienceDefinition.generated.h"

class UFighterExperienceActionSet;
class UFighterPawnData;
class UGameFeatureAction;

UENUM(BlueprintType)
enum class EFighterSessionMode : uint8
{
    Training,
    HostSmoke,
    CommandSmoke,
    ContactSmoke,
    ReferencePreview,
};

USTRUCT(BlueprintType)

struct FFighterSessionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Session")
    EFighterSessionMode Mode = EFighterSessionMode::Training;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Session", meta = (ClampMin = "0", ClampMax = "36000"))
    int32 SmokeFrames = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Session")
    bool bCaptureScreenshot = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Session")
    bool bExitWhenDone = false;
};

UCLASS(BlueprintType)

class FIGHTERGAME_API UFighterExperienceDefinition : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:
    UFighterExperienceDefinition(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

#if WITH_EDITORONLY_DATA
    virtual void UpdateAssetBundleData() override;
#endif

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fighter")
    FFighterSessionSettings Session;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fighter")
    TArray<TSoftObjectPtr<UFighterPawnData>> Fighters;

    UPROPERTY(EditAnywhere, Category = "Gameplay")
    TArray<FString> GameFeaturesToEnable;

    UPROPERTY(EditAnywhere, Category = "Gameplay")
    TArray<TObjectPtr<UFighterExperienceActionSet>> ActionSets;

    UPROPERTY(EditAnywhere, Instanced, Category = "Actions")
    TArray<TObjectPtr<UGameFeatureAction>> Actions;
};
