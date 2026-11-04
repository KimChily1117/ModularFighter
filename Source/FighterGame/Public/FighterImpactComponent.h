// 타격 이펙트 표시 컴포넌트.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "FighterHitFeedback.h"
#include "FighterHeatPresentation.h"
#include "FighterImpactComponent.generated.h"
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USoundBase;
class UPointLightComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UFighterElectricTrailComponent;

UCLASS(ClassGroup = (Fighter))

class FIGHTERGAME_API UFighterImpactComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFighterImpactComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
    void Play(const FGameplayTag& Tag, const FVector& Location, float Damage, uint8 AttackerCharacter,
              const FVector& AttackerLocation, bool bCharacterEffect, float SourcePower, uint32 VariationSeed,
              int32 Target = INDEX_NONE);
    void PlayFeedback(const FGameplayTag& Tag, int32 Target, float SourcePower, bool bSignature, bool bAirborne,
                      const FVector& Location, const FVector& Away);
    void ResetFeedback();
    FVector2D GetHealthBarOffset(int32 Target) const;
    float GetHealthBarFlash(int32 Target) const;
    bool HasResources() const;
    void PlayHeat(const FVector& Location);
    void PlayHeatStage(EFighterHeatEffect Kind, int32 Fighter, const FVector& Location);
    void PlayTornado(int32 Target, const FVector& Location);

    void PlayTrail(int32 Fighter, const FVector& Hand);
    void EndTrail(int32 Fighter);

    void QueuePaulWind(int32 Fighter, int32 RuleIndex, uint32 Frame);

    int32 GetPaulWindCount() const
    {
        return PaulWindCount;
    }

    int32 GetTrailCount() const
    {
        return TrailCount;
    }

    int32 GetHeatCount() const
    {
        return HeatCount;
    }

    int32 GetImpactCount() const
    {
        return ImpactCount;
    }

private:
    void PresentPaulHeatSmash(const class AFighterArena& Arena);
    int32 PaulSmashLastFrame[2] = {0, 0};
    uint8 PaulSmashPlayedMask[2] = {0, 0};
    bool Paul666Slammed[2] = {false, false};
    int32 PaulSmashCaptureDelay = 0;
    UPROPERTY()
    TObjectPtr<UStaticMesh> Plane;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> SparkMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> GlowMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> KazuyaMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> PaulMaterial;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> PaulFireMaterial;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMesh>> SourceMeshes;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> SourceMaterials;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Sprites;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInstanceDynamic>> SpriteMaterials;
    TArray<float> Ages;
    TArray<float> Sizes;
    TArray<float> Rolls;
    TArray<FVector> Aspects;
    TArray<FVector> Origins;
    TArray<FVector> Flights;
    UPROPERTY()
    TObjectPtr<UPointLightComponent> FlashLight;

    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitCommonSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitVatSystem;
    TArray<TWeakObjectPtr<UNiagaraComponent>> HitVatEffects;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitSourceLayersSystem;
    TArray<TWeakObjectPtr<UNiagaraComponent>> HitSourceLayerEffects;

    UPROPERTY()
    TObjectPtr<UNiagaraSystem> CriticalSourceSystem;
    TArray<TWeakObjectPtr<UNiagaraComponent>> CriticalSourceEffects;
    int32 HitVatCaptureTick = -1;
    bool bHitVatCaptured = false;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitGuardSystem;
    TArray<TWeakObjectPtr<UNiagaraComponent>> GuardEffects;
    bool bSourceGuard = false;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> CounterLocalSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> CounterWorldSystem;

    struct FCounterEffect
    {
        TWeakObjectPtr<UNiagaraComponent> Component;
        int32 Target = INDEX_NONE;
        FVector Offset = FVector::ZeroVector;
    };

    TArray<FCounterEffect> CounterEffects;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> TornadoLocalSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> TornadoWorldSystem;
    TArray<FCounterEffect> TornadoEffects;
    int32 TornadoCaptureDelay = 0;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitStruckSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitWeldSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitPaulSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> PaulSourceSystem;
    TArray<TWeakObjectPtr<UNiagaraComponent>> PaulSourceEffects;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HitKazuyaSystem;

    UPROPERTY()
    TObjectPtr<UNiagaraSystem> KazuyaSourceSystem;
    TArray<TWeakObjectPtr<UNiagaraComponent>> KazuyaSourceEffects;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HeatBurstSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HeatEngagerSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HeatDashLocalSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HeatDashWorldSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HeatAuraSystem;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> HeatEndSystem;
    TArray<FCounterEffect> HeatStageEffects;
    TArray<TWeakObjectPtr<UNiagaraComponent>> HeatAuraEffects[2];
    bool bHeatAuraActive[2] = {false, false};
    uint32 HeatAuraLastFrame[2] = {0, 0};

    struct FHeatCapture
    {
        EFighterHeatEffect Kind;
        int32 Fighter;
        int32 Ticks = 0;
    };

    TArray<FHeatCapture> HeatCaptures;
    uint16 HeatCapturedMask = 0;

    UPROPERTY()
    TMap<int32, TObjectPtr<UNiagaraSystem>> PaulWindSystems;

    struct FPaulWindQueue
    {
        int32 Fighter;
        int32 RuleIndex;
        uint32 Frame;
    };

    TArray<FPaulWindQueue> PaulWindQueue;

    struct FPaulWindEffect
    {
        TWeakObjectPtr<UNiagaraComponent> Component;
        int32 Fighter;
        FName Bone;
        FVector Offset;
    };

    TArray<FPaulWindEffect> PaulWindEffects;
    int32 PaulWindCount = 0;
    int32 PaulWindCaptureDelay = 0;
    int32 PaulWindCaptureNext = 0;
    UPROPERTY()
    TObjectPtr<UNiagaraSystem> KazuyaTrailSystem;
    UPROPERTY()
    TObjectPtr<UFighterElectricTrailComponent> SourceElectricTrail;
    FVector TrailPrevious[2] = {FVector::ZeroVector, FVector::ZeroVector};
    bool bTrailActive[2] = {false, false};
    int32 TrailCount = 0;
    int32 TrailCaptureDelay = 0;

public:
    int32 GetNiagaraSpawnCount() const
    {
        return NiagaraSpawnCount;
    }

private:
    int32 NiagaraSpawnCount = 0;
    float FlashAge = 1.f;
    float FlashPeak = 0.f;
    FighterHitFeedback::Pulse CameraPulse;
    FighterHitFeedback::Pulse HealthPulses[2];
    uint32 FeedbackLastCombatFrame = 0;
    float FeedbackCaptureAge = -1.f;
    int32 FeedbackCaptureNext = 0;
    int32 FeedbackCaptureTarget = 0;
    bool bFeedbackCaptured = false;
    int32 T6CameraMode = 0;
    uint32 T6CameraStartFrame = 0;
    int32 ImpactCount = 0;
    int32 NextSprite = 0;
    int32 CaptureDelay = 0;
    bool bSpecialCaptured = false;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> HeatSprites;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInstanceDynamic>> HeatMaterials;
    TArray<float> HeatAges;
    int32 HeatCount = 0;
    int32 HeatCaptureDelay = 0;
};
