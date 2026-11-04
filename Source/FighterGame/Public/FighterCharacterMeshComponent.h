// 캐릭터 메시와 포즈 표시.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterCharacterMeshComponent.generated.h"

class UPoseableMeshComponent;

UCLASS()

class FIGHTERGAME_API UFighterCharacterMeshComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    bool Initialize(USceneComponent* Parent, const FString& CharacterSlug, int32 MoveIndex, uint32 AnimationKey);

    void UpdatePose(int32 Frame, bool bAttack, FName ReactionClip = NAME_None, bool bReactionLoop = false,
                    FName MovementClip = NAME_None, bool bLoopClip = false, FName AttackClip = NAME_None,
                    int64 SimulationFrame = -1, bool bBlendTransitions = false, bool bFreezeBlend = false,
                    int32 AttackBlendFrames = 2, bool bAirborneReaction = false, float SourceFrameFraction = 0.f);
    void SetVisible(bool bVisible);
    void SetDevilForm(bool bActive);

    bool IsDevilForm() const
    {
        return bDevilActive;
    }

    bool HasReferenceAttack() const
    {
        return Attack.Frames.Num() > 0;
    }

    bool HasAttackClip(FName Name) const
    {
        const auto* Clip = AttackClips.Find(Name);

        return Clip && Clip->Frames.Num() > 0;
    }

    bool IsReady() const
    {
        return bReady;
    }

    bool IsPoseFinite() const
    {
        return bPoseFinite;
    }

    int32 GetPartCount() const
    {
        return Parts.Num();
    }

    int32 GetBoneCount() const
    {
        return BoneNames.Num();
    }

    uint64 GetPoseHash() const
    {
        return PoseHash;
    }

    bool GetBoneWorldLocation(FName Bone, FVector& OutLocation) const;

    void SetFresnel(const FLinearColor& Color, float Contrast, float Weight);

private:
    UPROPERTY()
    TObjectPtr<class UMaterialInstanceDynamic> FresnelMaterial;
    bool bFresnelApplied = false;
    bool bModelVisible = true;
    bool bDevilActive = false;
    int32 DevilPartFirst = INDEX_NONE;
    int32 DevilFacePart = INDEX_NONE;
    int32 DevilFaceSlot = INDEX_NONE;
    UPROPERTY()
    TObjectPtr<class UMaterialInterface> NormalFaceMaterial;
    UPROPERTY()
    TObjectPtr<class UMaterialInterface> DevilFaceMaterial;

    struct FClip
    {
        TArray<TArray<FTransform>> Frames;
    };

    struct FLocalPose
    {
        TArray<FTransform> Bones;
    };

    bool LoadPoses(const FString& Path, int32 MoveIndex, uint32 AnimationKey);
    TArray<FName> BoneNames;
    TMap<FName, int32> BoneLookup;
    TArray<FTransform> ReferencePose;
    TArray<TArray<FTransform>> BindOffsets;
    FClip Idle;
    FClip Attack;
    TMap<FName, FClip> Reactions;
    TMap<FName, FClip> Movement;
    TMap<FName, FClip> AttackClips;
    double ModelYaw = 0;
    bool bReady = false;
    bool bPoseFinite = false;
    bool bPaul = false;
    uint64 PoseHash = 0;
    const FClip* PreviousClip = nullptr;
    int32 PreviousKind = -1;
    int64 PreviousSimulationFrame = -1;
    int32 PreviousPoseFrame = -1;
    bool bPreviousAirborneReaction = false;
    bool bAirReactionBlend = false;
    int32 BlendElapsed = 0;
    int32 BlendDuration = 0;
    TArray<FLocalPose> LastLocalPoses;
    TArray<FLocalPose> BlendFromPoses;
    UPROPERTY()
    TArray<TObjectPtr<UPoseableMeshComponent>> Parts;
};
