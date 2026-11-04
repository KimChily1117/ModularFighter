// 동작 확인용 표시 리그. 전투 판정에는 쓰지 않는다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterReferenceMotionComponent.generated.h"

class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;

UCLASS()

class FIGHTERGAME_API UFighterReferenceMotionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    bool Initialize(USceneComponent* Parent, const FString& CharacterSlug, const FString& AttackClip, UStaticMesh* Cube,
                    UStaticMesh* Sphere, UMaterialInterface* Material, const FLinearColor& Color);
    void UpdatePose(int32 SourceFrame, bool bAttack);
    void SetPreviewVisible(bool bVisible);

    bool IsReady() const
    {
        return bReady;
    }

    int32 GetAttackFrameCount() const
    {
        return Attack.FrameCount;
    }

    const FString& GetMoveName() const
    {
        return Attack.MoveName;
    }

    uint64 GetPoseHash() const
    {
        return PoseHash;
    }

    int32 GetSourceMoveIndex() const
    {
        return Attack.SourceMoveIndex;
    }

    uint32 GetAnimationKey() const
    {
        return Attack.AnimationKey;
    }

    int32 GetSampledFrame() const
    {
        return SampledFrame;
    }

    bool IsPoseFinite() const
    {
        return bPoseFinite;
    }

private:
    struct FSample
    {
        FVector Scale;
        FQuat Rotation;
        FVector Position;
    };

    struct FTrack
    {
        int32 Encoding = 0;
        TArray<FSample> Samples;
    };

    struct FClip
    {
        int32 FrameCount = 0;
        int32 SourceMoveIndex = -1;
        uint32 AnimationKey = 0;
        FString MoveName;
        TMap<FName, FTrack> Tracks;
    };

    bool ReadClip(const FString& Path, FClip& Destination);
    FClip Idle;
    FClip Attack;
    bool bReady = false;
    uint64 PoseHash = 0;
    int32 SampledFrame = 0;
    bool bPoseFinite = false;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Joints;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Links;
};
