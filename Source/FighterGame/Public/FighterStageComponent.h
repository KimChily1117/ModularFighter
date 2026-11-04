// 스테이지 표시 컴포넌트.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterStageComponent.generated.h"

UCLASS(ClassGroup = (Fighter))

class FIGHTERGAME_API UFighterStageComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFighterStageComponent();

    void SetStage(const FString& Key);

    const FString& GetStageKey() const
    {
        return StageKey;
    }

    bool Build(USceneComponent* Parent);

    bool Rebuild(USceneComponent* Parent, const FString& Key);

    int32 PreloadOtherStages();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

    bool IsLoaded() const
    {
        return InstanceCount > 0;
    }

    int32 GetInstanceCount() const
    {
        return InstanceCount;
    }

    bool HasSourceLighting() const
    {
        return bSourceLighting;
    }

    struct FSourceCharacterLight
    {
        FString Name;
        float Intensity = 0;
        FLinearColor Color = FLinearColor::White;
        float Radius = 3000.f;
        FVector Location = FVector::ZeroVector;
        FRotator Rotation = FRotator::ZeroRotator;
        bool bHasLocation = false;
    };

    const TArray<FSourceCharacterLight>& GetSourceCharacterLights() const
    {
        return SourceCharacterLights;
    }

    void ApplyPostProcess(struct FPostProcessSettings& Settings) const;

    void RecaptureSourceSky();

    void NoteFighterBody(int32 Index, const FVector& FootL, const FVector& FootR, const FVector& Hip,
                         const FVector& Head, float DeltaSeconds);

    void UpdateCameraOcclusion(const FVector& Eye);

    void SetCinematicWaterHidden(bool Hidden);

private:
    FString StageKey = TEXT("yakushima");
    bool bStageFromCommandLine = false;
    UPROPERTY()
    TArray<TObjectPtr<class USceneComponent>> CreatedComponents;
    UPROPERTY()
    TArray<TObjectPtr<UObject>> PreloadedAssets;
    UPROPERTY()
    TArray<TObjectPtr<class UMediaPlayer>> MoviePlayers;

    struct FOccluder
    {
        TWeakObjectPtr<class UInstancedStaticMeshComponent> Component;
        int32 Index;
        FBox Box;
        FTransform Transform;
        bool bHidden;
    };

    TArray<FOccluder> Occluders;
    FVector CameraTargets[4] = {FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector};
    FString StageFolder = TEXT("Yakushima");
    bool bWaterStage = true;
    bool bCinematicWaterHidden = false;
    TArray<TWeakObjectPtr<class USceneComponent>> CinematicWaterSurfaces;
    TArray<TWeakObjectPtr<class UNiagaraComponent>> ActiveWaterParticles;
    double StageYaw = 0;
    int32 InstanceCount = 0;
    bool bSourceLighting = false;
    bool bSourceSky = false;
    float SourceWhiteTemp = 6500.f;
    float SourceBloomThreshold = -1.f;
    float GradeBloomIntensity = -1.f;
    FLinearColor GradeBloomTint = FLinearColor::White;
    UPROPERTY()
    TObjectPtr<class UTexture> SourceLut;
    TArray<FSourceCharacterLight> SourceCharacterLights;
    bool ApplySourceLighting(class ASkyLight* Ambient, class AExponentialHeightFog* Fog);
    UPROPERTY()
    TArray<TObjectPtr<AActor>> EnvironmentActors;
    UPROPERTY()
    TArray<TObjectPtr<class UStaticMeshComponent>> Ripples;
    UPROPERTY()
    TArray<TObjectPtr<class UMaterialInstanceDynamic>> RippleMaterials;
    TArray<float> RippleAges;
    TArray<float> RippleScales;
    UPROPERTY()
    TObjectPtr<class UNiagaraSystem> WaterStep;
    UPROPERTY()
    TObjectPtr<class UNiagaraSystem> WaterSplash[3];
    bool bFootLifted[2][2] = {};
    float FootCooldown[2][2] = {};
    FVector LastFoot[2][2] = {};
    float LastBody[2] = {};
    float BodyCooldown[2] = {};
    bool bBodyTracked[2] = {};
    void Ripple(const FVector2D& Position, float Scale);
    FVector2D LastPosition[2] = {};
    float RippleCooldown[2] = {};
    uint32 LastFrame = 0;
    int32 NextRipple = 0;
    bool bProfilePresentation = false;
    double LastProfileTime = 0;
    TArray<double> FrameIntervalsMs, GameMs, RenderMs, GpuMs, DrawCalls, Primitives;
};
