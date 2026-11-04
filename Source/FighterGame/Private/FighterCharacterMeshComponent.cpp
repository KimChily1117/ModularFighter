// 코어 프레임에 맞춰 캐릭터 포즈를 샘플링한다.
#include "FighterCharacterMeshComponent.h"
#include "FighterMotionFeel.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "modular_fighter/combat.hpp"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogFighterCharacterMesh, Log, All);

namespace
{
TSharedPtr<FJsonObject> ReadObject(const FString& Path, int64 MaxBytes)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);

    if (Size <= 0 || Size > MaxBytes)
        return nullptr;
    FString Text;
    TSharedPtr<FJsonObject> Json;

    if (!FFileHelper::LoadFileToString(Text, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json))
        return nullptr;

    return Json;
}
}

// 포즈 데이터를 읽는다.
bool UFighterCharacterMeshComponent::LoadPoses(const FString& Path, int32 MoveIndex, uint32 AnimationKey)
{
    auto Json = ReadObject(Path, 160 * 1024 * 1024);
    double Schema = 0;
    FString Space;
    const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;

    if (!Json || !Json->TryGetNumberField(TEXT("schema_version"), Schema) || Schema != 1 ||
        !Json->TryGetStringField(TEXT("coordinate_space"), Space) || Space != TEXT("UE_COMPONENT") ||
        !Json->TryGetArrayField(TEXT("bones"), Names) || Names->IsEmpty() || Names->Num() > 1024 ||
        !Json->TryGetNumberField(TEXT("model_yaw_degrees"), ModelYaw) || !FMath::IsFinite(ModelYaw) ||
        FMath::Abs(ModelYaw) > 360)
        return false;

    for (const auto& Name : *Names)
    {
        FString Text;

        if (!Name->TryGetString(Text) || Text.IsEmpty() || Text.Len() > 256 || BoneLookup.Contains(FName(Text)))
            return false;
        BoneLookup.Add(FName(Text), BoneNames.Add(FName(Text)));
    }

    for (const FName Required : {FName(TEXT("Hip")), FName(TEXT("R_Hand")), FName(TEXT("L_Hand")), FName(TEXT("Head"))})
        if (!BoneLookup.Contains(Required))
            return false;
    auto ReadClip = [&](const TCHAR* Field, FClip& Clip)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;

        if (!Json->TryGetObjectField(Field, Object) || !Object || !*Object ||
            !(*Object)->TryGetArrayField(TEXT("frames"), Frames) || Frames->IsEmpty() || Frames->Num() > 36000)
            return false;

        for (const auto& Value : *Frames)
        {
            const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;

            if (!Value->TryGetArray(Bones) || Bones->Num() != BoneNames.Num())
                return false;
            TArray<FTransform> Pose;
            Pose.Reserve(BoneNames.Num());

            for (const auto& Bone : *Bones)
            {
                const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;

                if (!Bone->TryGetArray(Values) || Values->Num() != 10)
                    return false;
                double V[10];

                for (int32 I = 0; I < 10; ++I)
                    if (!(*Values)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I]) || FMath::Abs(V[I]) > 1e6)
                        return false;
                FQuat Q(V[3], V[4], V[5], V[6]);

                if (Q.SizeSquared() < 0.8 || Q.SizeSquared() > 1.2 || V[0] <= 0 || V[1] <= 0 || V[2] <= 0 ||
                    V[0] > 100 || V[1] > 100 || V[2] > 100)
                    return false;
                Q.Normalize();
                Pose.Emplace(Q, FVector(V[7], V[8], V[9]), FVector(V[0], V[1], V[2]));
            }

            Clip.Frames.Add(MoveTemp(Pose));
        }

        return true;
    };
    FClip Launch, Knockdown;

    if (!ReadClip(TEXT("idle"), Idle) || !ReadClip(TEXT("attack"), Attack) || !ReadClip(TEXT("launch"), Launch) ||
        Launch.Frames.Num() != 64 || !ReadClip(TEXT("knockdown"), Knockdown) || Knockdown.Frames.Num() != 66)
        return false;
    Reactions.Add(FName(TEXT("launch")), MoveTemp(Launch));
    Reactions.Add(FName(TEXT("knockdown")), MoveTemp(Knockdown));

    TArray<FString> MovementNames = {TEXT("walk_forward"), TEXT("walk_back"), TEXT("crouch_down"), TEXT("crouch_idle"),
                                     TEXT("crouch_up")};

    for (int32 Id = 1; Id < 256; ++Id)
        if (const auto* Move = modular_fighter::GetMoveDefinition(static_cast<modular_fighter::MoveKind>(Id)))
            MovementNames.AddUnique(UTF8_TO_TCHAR(Move->clip));

    for (const FString& Name : MovementNames)
    {
        if (!Json->HasField(Name))
            continue;
        FClip Clip;

        if (!ReadClip(*Name, Clip))
            return false;
        Movement.Add(FName(*Name), MoveTemp(Clip));
    }

    for (const auto& Pair : Json->Values)
    {
        const FString Key(Pair.Key);

        if (!Key.StartsWith(TEXT("basic_")) && !Key.StartsWith(TEXT("atk_")))
            continue;
        FClip Clip;

        if (!ReadClip(*Key, Clip))
            return false;
        AttackClips.Add(FName(*Key), MoveTemp(Clip));
    }

    for (const auto& Pair : Json->Values)
    {
        const FString Key(Pair.Key);

        if (!Key.StartsWith(TEXT("react_")))
            continue;
        FClip Clip;

        if (!ReadClip(*Key, Clip))
            return false;
        Reactions.Add(FName(*Key), MoveTemp(Clip));
    }

    const TSharedPtr<FJsonObject>* PackObject = nullptr;
    FString PackFile;

    if (Json->TryGetObjectField(TEXT("clip_pack"), PackObject) &&
        (*PackObject)->TryGetStringField(TEXT("file"), PackFile))
    {
        if (PackFile.Contains(TEXT("/")) || PackFile.Contains(TEXT("\\")) || PackFile.Contains(TEXT("..")))
            return false;
        const FString PackPath = FPaths::GetPath(Path) / PackFile;
        const int64 PackSize = IFileManager::Get().FileSize(*PackPath);
        TArray<uint8> Pack;

        if (PackSize <= 16 || PackSize > int64(1024) * 1024 * 1024 || !FFileHelper::LoadFileToArray(Pack, *PackPath))
            return false;
        int64 Cursor = 0;
        auto ReadU32 = [&](uint32& Out)
        {
            if (Cursor + 4 > Pack.Num())
                return false;
            FMemory::Memcpy(&Out, Pack.GetData() + Cursor, 4);
            Cursor += 4;

            return true;
        };
        uint32 Version = 0, PackBones = 0, ClipCount = 0;

        if (FMemory::Memcmp(Pack.GetData(), "MFPC", 4) != 0)
            return false;
        Cursor = 4;

        if (!ReadU32(Version) || Version != 1 || !ReadU32(PackBones) || PackBones != uint32(BoneNames.Num()) ||
            !ReadU32(ClipCount) || ClipCount > 4096)
            return false;

        for (uint32 ClipIndex = 0; ClipIndex < ClipCount; ++ClipIndex)
        {
            if (Cursor + 2 > Pack.Num())
                return false;
            uint16 NameLength = 0;
            FMemory::Memcpy(&NameLength, Pack.GetData() + Cursor, 2);
            Cursor += 2;

            if (NameLength == 0 || NameLength > 256 || Cursor + NameLength > Pack.Num())
                return false;
            const FString ClipName =
                FString(FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(Pack.GetData() + Cursor), NameLength));
            Cursor += NameLength;
            uint32 FrameCount = 0;

            if (!ReadU32(FrameCount) || FrameCount == 0 || FrameCount > 36000)
                return false;
            const int64 Bytes = int64(FrameCount) * PackBones * 10 * sizeof(float);

            if (Cursor + Bytes > Pack.Num())
                return false;
            const float* Values = reinterpret_cast<const float*>(Pack.GetData() + Cursor);
            Cursor += Bytes;
            FClip Clip;
            Clip.Frames.Reserve(FrameCount);

            for (uint32 Frame = 0; Frame < FrameCount; ++Frame)
            {
                TArray<FTransform> Pose;
                Pose.Reserve(PackBones);

                for (uint32 Bone = 0; Bone < PackBones; ++Bone)
                {
                    const float* V = Values + (int64(Frame) * PackBones + Bone) * 10;

                    for (int32 I = 0; I < 10; ++I)
                        if (!FMath::IsFinite(V[I]) || FMath::Abs(V[I]) > 1e6f)
                            return false;
                    FQuat Q(V[3], V[4], V[5], V[6]);

                    if (Q.SizeSquared() < 0.8 || Q.SizeSquared() > 1.2 || V[0] <= 0 || V[1] <= 0 || V[2] <= 0 ||
                        V[0] > 100 || V[1] > 100 || V[2] > 100)
                        return false;
                    Q.Normalize();
                    Pose.Emplace(Q, FVector(V[7], V[8], V[9]), FVector(V[0], V[1], V[2]));
                }

                Clip.Frames.Add(MoveTemp(Pose));
            }

            if (ClipName.StartsWith(TEXT("react_")))
                Reactions.Add(FName(*ClipName), MoveTemp(Clip));
            else if (ClipName.StartsWith(TEXT("basic_")) || ClipName.StartsWith(TEXT("atk_")))
                AttackClips.Add(FName(*ClipName), MoveTemp(Clip));
            else
                return false;
        }

        if (Cursor != Pack.Num())
            return false;
    }

    const TSharedPtr<FJsonObject>* AttackObject = nullptr;
    const TSharedPtr<FJsonObject>* Source = nullptr;
    const TSharedPtr<FJsonObject>* Payload = nullptr;
    double Index = -1, Key = -1;

    if (!Json->TryGetObjectField(TEXT("attack"), AttackObject) ||
        !(*AttackObject)->TryGetObjectField(TEXT("source"), Source) ||
        !(*Source)->TryGetNumberField(TEXT("move_index"), Index) || Index != MoveIndex ||
        !(*Source)->TryGetObjectField(TEXT("payload"), Payload) ||
        !(*Payload)->TryGetNumberField(TEXT("pool_key"), Key) || Key != AnimationKey)
        return false;
    auto CheckReactionSource = [&](const TCHAR* Field, int32 ExpectedMove, uint32 ExpectedKey)
    {
        const TSharedPtr<FJsonObject>* ClipObject = nullptr;
        const TSharedPtr<FJsonObject>* ClipSource = nullptr;
        const TSharedPtr<FJsonObject>* ClipPayload = nullptr;
        double Move = -1, PoolKey = -1;

        return Json->TryGetObjectField(Field, ClipObject) &&
               (*ClipObject)->TryGetObjectField(TEXT("source"), ClipSource) &&
               (*ClipSource)->TryGetNumberField(TEXT("move_index"), Move) && Move == ExpectedMove &&
               (*ClipSource)->TryGetObjectField(TEXT("payload"), ClipPayload) &&
               (*ClipPayload)->TryGetNumberField(TEXT("pool_key"), PoolKey) && PoolKey == ExpectedKey;
    };

    if (!CheckReactionSource(TEXT("launch"), 294, 1689895343u) ||
        !CheckReactionSource(TEXT("knockdown"), 1753, 3558484019u))
        return false;

    const TArray<TSharedPtr<FJsonValue>>* Reference = nullptr;

    if (!Json->TryGetArrayField(TEXT("reference_pose"), Reference))
        return false;
    auto Wrapper = MakeShared<FJsonObject>();
    Wrapper->SetArrayField(TEXT("frames"), {MakeShared<FJsonValueArray>(*Reference)});
    Json->SetObjectField(TEXT("reference"), Wrapper);
    FClip Bind;

    if (!ReadClip(TEXT("reference"), Bind) || Bind.Frames.Num() != 1)
        return false;
    ReferencePose = MoveTemp(Bind.Frames[0]);

    return true;
}

bool UFighterCharacterMeshComponent::Initialize(USceneComponent* Parent, const FString& CharacterSlug, int32 MoveIndex,
                                                uint32 AnimationKey)
{
    if (bReady || !Parent || !GetOwner() || CharacterSlug.IsEmpty() || CharacterSlug.Contains(TEXT("/")) ||
        CharacterSlug.Contains(TEXT("..")))
        return false;
    const FString Root = FPaths::ProjectDir() / TEXT("LocalData/Characters");
    auto Manifest = ReadObject(Root / TEXT("models.json"), 1024 * 1024);
    double Schema = 0;
    const TArray<TSharedPtr<FJsonValue>>* Characters = nullptr;

    if (!Manifest || !Manifest->TryGetNumberField(TEXT("schema_version"), Schema) || Schema != 1 ||
        !Manifest->TryGetArrayField(TEXT("characters"), Characters) || Characters->Num() != 2)
        return false;
    TArray<USkeletalMesh*> Assets;

    for (const auto& Character : *Characters)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        FString Slug;
        const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;

        if (!Character->TryGetObject(Object) || !Object || !*Object ||
            !(*Object)->TryGetStringField(TEXT("slug"), Slug))
            return false;

        if (Slug != CharacterSlug)
            continue;

        if (!Assets.IsEmpty() || !(*Object)->TryGetArrayField(TEXT("parts"), Entries) || Entries->IsEmpty() ||
            Entries->Num() > 16)
            return false;

        for (const auto& Entry : *Entries)
        {
            const TSharedPtr<FJsonObject>* Part = nullptr;
            FString AssetPath;

            if (!Entry->TryGetObject(Part) || !Part || !*Part ||
                !(*Part)->TryGetStringField(TEXT("asset"), AssetPath) ||
                !AssetPath.StartsWith(TEXT("/Game/LocalCharacters/")))
                return false;
            auto* Asset = LoadObject<USkeletalMesh>(nullptr, *AssetPath, nullptr, LOAD_NoWarn);

            if (!Asset || !Asset->GetSkeleton() || Asset->GetRefSkeleton().GetNum() < 20)
            {
                UE_LOG(LogFighterCharacterMesh, Warning, TEXT("CHARACTER_MESH invalid mesh/skeleton: %s"), *AssetPath);

                return false;
            }

            if (Asset->GetMaterials().IsEmpty())
                return false;

            for (const auto& Slot : Asset->GetMaterials())
                if (!Slot.MaterialInterface)
                {
                    UE_LOG(LogFighterCharacterMesh, Warning, TEXT("CHARACTER_MESH unsaved material slot: %s / %s"),
                           *AssetPath, *Slot.MaterialSlotName.ToString());

                    return false;
                }
            Assets.Add(Asset);
        }
    }

    if (Assets.IsEmpty() || !LoadPoses(Root / (CharacterSlug + TEXT("-poses.json")), MoveIndex, AnimationKey))
        return false;

    if (CharacterSlug == TEXT("kazuya"))
    {
        auto* Wings = LoadObject<USkeletalMesh>(
            nullptr, TEXT("/Game/LocalCharacters/Kazuya/SK_Kazuya_DevilWings.SK_Kazuya_DevilWings"));
        auto* Horns = LoadObject<USkeletalMesh>(
            nullptr, TEXT("/Game/LocalCharacters/Kazuya/SK_Kazuya_DevilHorns.SK_Kazuya_DevilHorns"));

        if (Wings && Horns && Wings->GetSkeleton() && Horns->GetSkeleton())
        {
            DevilPartFirst = Assets.Num();
            Assets.Add(Wings);
            Assets.Add(Horns);
            DevilFaceMaterial =
                LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LocalCharacters/Devil/M_DevilFace.M_DevilFace"));
        }
    }

    for (auto* Asset : Assets)
    {
        auto* Part = NewObject<UPoseableMeshComponent>(GetOwner());
        GetOwner()->AddInstanceComponent(Part);
        Part->SetupAttachment(Parent);
        Part->SetRelativeRotation(FRotator(0, ModelYaw, 0));
        Part->SetSkinnedAssetAndUpdate(Asset);

        Part->SetForcedLOD(1);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        Part->SetRenderCustomDepth(true);
        Part->SetCustomDepthStencilValue(1);
        Part->bCastDynamicShadow = true;

        Part->LightingChannels.bChannel0 = false;
        Part->LightingChannels.bChannel1 = true;
        Part->SetBoundsScale(2.0f);
        Part->RegisterComponent();
        Part->PrestreamTextures(3600.f, true);
        UE_LOG(LogFighterCharacterMesh, Display,
               TEXT("FIGHTER_MODEL_QUALITY mesh=%s forced_lod=0 imported_lods=%d resident_seconds=3600"),
               *Asset->GetName(), Asset->GetLODNum());
        Parts.Add(Part);

        if (DevilPartFirst != INDEX_NONE && Parts.Num() - 1 >= DevilPartFirst)
            Part->SetVisibility(false);

        if (DevilPartFirst != INDEX_NONE && Asset->GetName() == TEXT("SK_Kazuya_Face"))
            for (int32 Slot = 0; Slot < Asset->GetMaterials().Num(); ++Slot)
                if (Asset->GetMaterials()[Slot].MaterialSlotName.ToString().Contains(TEXT("face_skin")))
                {
                    DevilFacePart = Parts.Num() - 1;
                    DevilFaceSlot = Slot;
                    NormalFaceMaterial = Part->GetMaterial(Slot);
                }
        const auto& Skeleton = Asset->GetRefSkeleton();
        const auto& Bind = Skeleton.GetRefBonePose();
        TArray<FTransform> World;
        TArray<FTransform> Offsets;
        World.SetNum(Bind.Num());
        Offsets.Init(FTransform::Identity, Bind.Num());

        for (int32 I = 0; I < Bind.Num(); ++I)
        {
            const int32 ParentIndex = Skeleton.GetParentIndex(I);
            World[I] = ParentIndex < 0 ? Bind[I] : Bind[I] * World[ParentIndex];

            if (const int32* Track = BoneLookup.Find(Skeleton.GetBoneName(I)))
                Offsets[I] = World[I].GetRelativeTransform(ReferencePose[*Track]);
        }

        BindOffsets.Add(MoveTemp(Offsets));
        TArray<TSharedPtr<FJsonValue>> BoneEvidence;

        for (int32 I = 0; I < Bind.Num(); ++I)
        {
            auto Bone = MakeShared<FJsonObject>();
            Bone->SetStringField(TEXT("name"), Skeleton.GetBoneName(I).ToString());
            const int32 ParentIndex = Skeleton.GetParentIndex(I);
            Bone->SetStringField(TEXT("parent"),
                                 ParentIndex < 0 ? TEXT("") : Skeleton.GetBoneName(ParentIndex).ToString());
            const auto Q = World[I].GetRotation();
            const auto P = World[I].GetTranslation();
            const auto S = World[I].GetScale3D();
            auto Numbers = [](std::initializer_list<double> Values)
            {
                TArray<TSharedPtr<FJsonValue>> Result;

                for (const auto V : Values)
                    Result.Add(MakeShared<FJsonValueNumber>(V));

                return Result;
            };
            Bone->SetArrayField(TEXT("rotation"), Numbers({Q.X, Q.Y, Q.Z, Q.W}));
            Bone->SetArrayField(TEXT("translation"), Numbers({P.X, P.Y, P.Z}));
            Bone->SetArrayField(TEXT("scale"), Numbers({S.X, S.Y, S.Z}));
            BoneEvidence.Add(MakeShared<FJsonValueObject>(Bone));
        }

        FString Text;
        FJsonSerializer::Serialize(BoneEvidence, TJsonWriterFactory<>::Create(&Text));
        const FString EvidenceDir = FPaths::ProjectSavedDir() / TEXT("Automation");
        IFileManager::Get().MakeDirectory(*EvidenceDir, true);
        FFileHelper::SaveStringToFile(Text, *(EvidenceDir / (TEXT("character-bind-") + CharacterSlug + TEXT("-") +
                                                             Asset->GetName() + TEXT(".json"))));
    }

    bReady = true;
    bPaul = CharacterSlug.Equals(TEXT("paul"), ESearchCase::IgnoreCase);
    UpdatePose(0, false);
    UE_LOG(
        LogFighterCharacterMesh, Display,
        TEXT(
            "CHARACTER_MESH ready %s parts=%d pose_bones=%d idle=%d attack=%d; original skin, prepared retarget poses"),
        *CharacterSlug, Parts.Num(), BoneNames.Num(), Idle.Frames.Num(), Attack.Frames.Num());

    return bPoseFinite;
}

void UFighterCharacterMeshComponent::SetVisible(bool bVisible)
{
    bModelVisible = bVisible;

    for (int32 I = 0; I < Parts.Num(); ++I)
        Parts[I]->SetVisibility(bVisible && (DevilPartFirst == INDEX_NONE || I < DevilPartFirst || bDevilActive));
}

void UFighterCharacterMeshComponent::SetDevilForm(bool bActive)
{
    bActive = bActive && !bPaul && DevilPartFirst != INDEX_NONE;

    if (bActive == bDevilActive)
        return;
    bDevilActive = bActive;

    if (Parts.IsValidIndex(DevilFacePart) && DevilFaceSlot != INDEX_NONE && NormalFaceMaterial && DevilFaceMaterial)
        Parts[DevilFacePart]->SetMaterial(DevilFaceSlot, bActive ? DevilFaceMaterial : NormalFaceMaterial);
    SetVisible(bModelVisible);
    UE_LOG(LogFighterCharacterMesh, Display,
           TEXT("FIGHTER_DEVIL active=%d source_parts=2 face=%d wing_motion=source_bind"), bDevilActive ? 1 : 0,
           DevilFaceMaterial && DevilFacePart != INDEX_NONE ? 1 : 0);
}

void UFighterCharacterMeshComponent::SetFresnel(const FLinearColor& Color, float Contrast, float Weight)
{
    if (Weight <= 0.f)
    {
        if (bFresnelApplied)
            for (const auto& Part : Parts)
                Part->SetOverlayMaterial(nullptr);
        bFresnelApplied = false;

        return;
    }

    if (!FresnelMaterial)
    {
        UMaterialInterface* Base = LoadObject<UMaterialInterface>(
            nullptr, TEXT("/Game/LocalPresentation/Materials/M_FighterFresnelOverlay.M_FighterFresnelOverlay"));

        if (!Base)
            return;
        FresnelMaterial = UMaterialInstanceDynamic::Create(Base, this);
    }

    FresnelMaterial->SetVectorParameterValue(TEXT("FresnelColor"), Color);
    FresnelMaterial->SetScalarParameterValue(TEXT("FresnelContrast"), Contrast);
    FresnelMaterial->SetScalarParameterValue(TEXT("FresnelWeight"), Weight);

    if (!bFresnelApplied)
        for (const auto& Part : Parts)
            Part->SetOverlayMaterial(FresnelMaterial);
    bFresnelApplied = true;
}

// 프레임에 맞는 포즈를 메시에 적용한다.
void UFighterCharacterMeshComponent::UpdatePose(int32 Frame, bool bAttack, FName ReactionClip, bool bReactionLoop,
                                                FName MovementClip, bool bLoopClip, FName AttackClip,
                                                int64 SimulationFrame, bool bBlendTransitions, bool bFreezeBlend,
                                                int32 AttackBlendFrames, bool bAirborneReaction,
                                                float SourceFrameFraction)
{
    if (!bReady)
        return;
    const FClip* FoundReaction = !ReactionClip.IsNone() ? Reactions.Find(ReactionClip) : nullptr;
    const FClip* ReactionPose = FoundReaction && FoundReaction->Frames.Num() > 0 ? FoundReaction : nullptr;
    const bool bFree = !ReactionPose && !bAttack;
    const FClip* Found = bFree && !MovementClip.IsNone() ? Movement.Find(MovementClip) : nullptr;
    const FClip* Locomotion = Found && Found->Frames.Num() > 0 ? Found : nullptr;
    const FClip* FoundAttack = bFree && !AttackClip.IsNone() ? AttackClips.Find(AttackClip) : nullptr;
    const FClip* BasicClip = FoundAttack && FoundAttack->Frames.Num() > 0 ? FoundAttack : nullptr;
    const FClip& Clip = ReactionPose ? *ReactionPose
                        : bAttack    ? Attack
                        : BasicClip  ? *BasicClip
                        : Locomotion ? *Locomotion
                                     : Idle;
    const int32 Kind = ReactionPose ? 2 : (bAttack || BasicClip) ? 1 : 0;
    const bool bContinuous =
        !FighterMotionFeel::HardCut() && bBlendTransitions && SimulationFrame >= 0 && PreviousSimulationFrame >= 0 &&
        SimulationFrame >= PreviousSimulationFrame && SimulationFrame - PreviousSimulationFrame <= 8 &&
        LastLocalPoses.Num() == Parts.Num() &&
        !(SimulationFrame == PreviousSimulationFrame && (&Clip != PreviousClip || Frame != PreviousPoseFrame));
    const bool bAirRehit = Kind == 2 && PreviousKind == 2 && bAirborneReaction && bPreviousAirborneReaction &&
                           SimulationFrame > PreviousSimulationFrame &&
                           (&Clip != PreviousClip || Frame < PreviousPoseFrame);

    if (!bContinuous)
    {
        BlendDuration = 0;
        BlendFromPoses.Reset();
        bAirReactionBlend = false;
    }
    else if (&Clip != PreviousClip || bAirRehit)
    {
        bAirReactionBlend = bAirRehit;
        BlendDuration = bAirRehit                                                ? 3
                        : Kind == 2 || (bPaul && Kind == 1 && PreviousKind == 1) ? 0
                        : Kind == 1                                              ? FMath::Clamp(AttackBlendFrames, 0, 2)
                                                                                 : 4;

        if (FighterMotionFeel::Pose())
        {
            if (bAirRehit)
                BlendDuration = 2;
            else if (Kind == 1)
                BlendDuration = FMath::Min(BlendDuration, 1);
            else if (Kind == 0 && PreviousKind != 0)
                BlendDuration = 2;
        }

        BlendElapsed = 0;
        BlendFromPoses = LastLocalPoses;

        if (BlendDuration > 0)
            UE_LOG(LogFighterCharacterMesh, Verbose, TEXT("POSE_BLEND kind=%d frames=%d at=%lld"), Kind, BlendDuration,
                   SimulationFrame);

        if (bAirRehit)
            UE_LOG(LogFighterCharacterMesh, Verbose, TEXT("AIR_REHIT_BLEND same_clip=%d from=%d to=%d at=%lld"),
                   &Clip == PreviousClip, PreviousPoseFrame, Frame, SimulationFrame);
    }
    else if (BlendDuration > 0 && SimulationFrame > PreviousSimulationFrame && !bFreezeBlend)
        BlendElapsed += static_cast<int32>(SimulationFrame - PreviousSimulationFrame);

    const bool bLoop = ReactionPose ? bReactionLoop : !bAttack && !BasicClip && (!Locomotion || bLoopClip);
    const int32 PoseIndex =
        bLoop ? FMath::Max(0, Frame) % Clip.Frames.Num() : FMath::Clamp(Frame, 0, Clip.Frames.Num() - 1);
    const auto& SourcePose = Clip.Frames[PoseIndex];
    TArray<FTransform> FractionalPose;

    if (!FighterMotionFeel::HardCut() && SourceFrameFraction > 0.f && PoseIndex + 1 < Clip.Frames.Num())
    {
        const auto& NextPose = Clip.Frames[PoseIndex + 1];
        FractionalPose.SetNum(SourcePose.Num());

        for (int32 I = 0; I < SourcePose.Num(); ++I)
            FractionalPose[I].Blend(SourcePose[I], NextPose[I], FMath::Clamp(SourceFrameFraction, 0.f, 1.f));
    }

    const auto& Pose = FractionalPose.IsEmpty() ? SourcePose : FractionalPose;
    bPoseFinite = true;
    PoseHash = 14695981039346656037ull;

    for (const auto& Transform : Pose)
        for (const double Value : {Transform.GetTranslation().X, Transform.GetTranslation().Y,
                                   Transform.GetTranslation().Z, Transform.GetRotation().X, Transform.GetRotation().Y,
                                   Transform.GetRotation().Z, Transform.GetRotation().W})
        {
            const float Rounded = static_cast<float>(Value);
            uint32 Bits = 0;
            FMemory::Memcpy(&Bits, &Rounded, sizeof(Bits));

            for (int32 B = 0; B < 4; ++B)
            {
                PoseHash ^= (Bits >> (B * 8)) & 255;
                PoseHash *= 1099511628211ull;
            }
        }
    LastLocalPoses.SetNum(Parts.Num());
    float MaxRotationError = 0.f, MaxRootTranslationError = 0.f;

    for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
    {
        auto* Part = Parts[PartIndex].Get();
        const auto& Skeleton = Part->GetSkinnedAsset()->GetRefSkeleton();
        const auto& Bind = Skeleton.GetRefBonePose();
        TArray<FTransform> World;
        World.SetNum(Bind.Num());
        LastLocalPoses[PartIndex].Bones.SetNum(Bind.Num());

        if (Part->BoneSpaceTransforms.Num() != Bind.Num())
        {
            bPoseFinite = false;

            return;
        }

        for (int32 I = 0; I < Bind.Num(); ++I)
        {
            const int32 Parent = Skeleton.GetParentIndex(I);

            if (const int32* Track = BoneLookup.Find(Skeleton.GetBoneName(I)))
                World[I] = BindOffsets[PartIndex][I] * Pose[*Track];
            else
                World[I] = Parent < 0 ? Bind[I] : Bind[I] * World[Parent];
            Part->BoneSpaceTransforms[I] = Parent < 0 ? World[I] : World[I].GetRelativeTransform(World[Parent]);
            Part->BoneSpaceTransforms[I].NormalizeRotation();
            const FName BoneName = Skeleton.GetBoneName(I);
            const bool bKeepRoot =
                bAirReactionBlend && (Parent < 0 || BoneName == TEXT("Top") || BoneName == TEXT("Trans"));

            if (!bKeepRoot && BlendDuration > 0 && BlendElapsed < BlendDuration &&
                BlendFromPoses.IsValidIndex(PartIndex) && BlendFromPoses[PartIndex].Bones.IsValidIndex(I))
            {
                const float Alpha = static_cast<float>(BlendElapsed + 1) / static_cast<float>(BlendDuration);
                const auto& From = BlendFromPoses[PartIndex].Bones[I];
                const auto& To = Part->BoneSpaceTransforms[I];
                FTransform Mixed(FQuat::Slerp(From.GetRotation(), To.GetRotation(), Alpha).GetNormalized(),
                                 bAirReactionBlend ? To.GetTranslation()
                                                   : FMath::Lerp(From.GetTranslation(), To.GetTranslation(), Alpha),
                                 bAirReactionBlend ? To.GetScale3D()
                                                   : FMath::Lerp(From.GetScale3D(), To.GetScale3D(), Alpha));

                if (FighterMotionFeel::Trace())
                {
                    MaxRotationError = FMath::Max(
                        MaxRotationError,
                        float(FMath::RadiansToDegrees(To.GetRotation().AngularDistance(Mixed.GetRotation()))));

                    if (Parent < 0 || BoneName == TEXT("Top") || BoneName == TEXT("Trans"))
                        MaxRootTranslationError = FMath::Max(
                            MaxRootTranslationError, float((To.GetTranslation() - Mixed.GetTranslation()).Size()));
                }

                Part->BoneSpaceTransforms[I] = Mixed;
            }

            LastLocalPoses[PartIndex].Bones[I] = Part->BoneSpaceTransforms[I];

            if (Part->BoneSpaceTransforms[I].ContainsNaN())
            {
                bPoseFinite = false;

                return;
            }
        }

        Part->MarkRefreshTransformDirty();
        Part->RefreshBoneTransforms();
    }

    PreviousClip = &Clip;
    PreviousKind = Kind;
    PreviousSimulationFrame = SimulationFrame;
    PreviousPoseFrame = Frame;
    bPreviousAirborneReaction = bAirborneReaction;

    if (FighterMotionFeel::Trace())
        UE_LOG(
            LogFighterCharacterMesh, Display,
            TEXT(
                "FIGHTER_MOTION_POSE model=%s frame=%lld sample=%d kind=%d source=%016llx blend=%d elapsed=%d air_rehit=%d rotation_error_deg=%.5f root_error_cm=%.5f enabled=%d"),
            bPaul ? TEXT("paul") : TEXT("kazuya"), SimulationFrame, Frame, Kind, PoseHash, BlendDuration, BlendElapsed,
            bAirReactionBlend ? 1 : 0, MaxRotationError, MaxRootTranslationError, bBlendTransitions ? 1 : 0);
}

bool UFighterCharacterMeshComponent::GetBoneWorldLocation(FName Bone, FVector& OutLocation) const
{
    if (!bReady || !bPoseFinite)
        return false;

    for (const auto& Part : Parts)
    {
        if (Part && Part->IsVisible() && Part->GetBoneIndex(Bone) != INDEX_NONE)
        {
            OutLocation = Part->GetBoneLocationByName(Bone, EBoneSpaces::WorldSpace);

            return true;
        }
    }

    return false;
}
