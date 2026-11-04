// 리본 트레일 이펙트. 전투 판정에는 쓰지 않는다.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterElectricTrailComponent.generated.h"
class UProceduralMeshComponent;
class UMaterialInterface;

UCLASS()

class FIGHTERGAME_API UFighterElectricTrailComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UFighterElectricTrailComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function) override;

    bool IsReady() const
    {
        return bReady;
    }

    void Sample(int32 Fighter, const FVector& Hand);
    void Stop(int32 Fighter);
    void ResetTrails();

private:
    struct FPoint
    {
        FVector Position;
        double Time;
        float Distance;
        float SourceAge;
    };

    struct FTrack
    {
        TArray<FPoint> Points;
        bool Emitting = false;
        uint32 LastSample = MAX_uint32;
        double StartTime = 0;
    };

    FTrack Tracks[2];
    UPROPERTY()
    TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> Materials;
    bool bReady = false;
    uint32 LastFrame = MAX_uint32;
    double Clock = 0;
    void AdvanceClock();
    void Rebuild(int32 Fighter);
};
