// 클립을 스켈레톤에 재생해 동작을 확인한다.
#include "FighterReferenceMotionComponent.h"
#include "FighterMotionCoordinates.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogFighterReferenceMotion, Log, All);

namespace
{
struct FRigBone
{
    const TCHAR* Name;
    int32 Parent;
};

const FRigBone Rig[] = {
    {TEXT("Top"), -1},       {TEXT("Trans"), 0},       {TEXT("Rot"), 1},         {TEXT("HARA_ROT1"), 2},
    {TEXT("Hip"), 3},        {TEXT("Spine1"), 4},      {TEXT("Spine2"), 5},      {TEXT("Neck"), 6},
    {TEXT("Head"), 7},       {TEXT("L_Shoulder"), 6},  {TEXT("L_UpperArm"), 9},  {TEXT("L_LowerArm"), 10},
    {TEXT("L_Hand"), 11},    {TEXT("R_Shoulder"), 6},  {TEXT("R_UpperArm"), 13}, {TEXT("R_LowerArm"), 14},
    {TEXT("R_Hand"), 15},    {TEXT("L_UpperLeg"), 4},  {TEXT("L_LowerLeg"), 17}, {TEXT("L_Foot"), 18},
    {TEXT("R_UpperLeg"), 4}, {TEXT("R_LowerLeg"), 20}, {TEXT("R_Foot"), 21}};
constexpr int32 RigCount = UE_ARRAY_COUNT(Rig);
}

bool UFighterReferenceMotionComponent::ReadClip(const FString& Path, FClip& Destination)
{
    const int64 Size = IFileManager::Get().FileSize(*Path);

    if (Size <= 0 || Size > 16 * 1024 * 1024)
        return false;
    FString Text;

    if (!FFileHelper::LoadFileToString(Text, *Path))
        return false;
    TSharedPtr<FJsonObject> Object;

    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object)
        return false;
    double Schema = 0;
    const TArray<TSharedPtr<FJsonValue>>* Tracks = nullptr;
    const TArray<TSharedPtr<FJsonValue>>* Skipped = nullptr;
    FClip Parsed;
    double MoveIndex = -1, Key = 0, HeaderFrames = 0;
    const TSharedPtr<FJsonObject>* Source = nullptr;

    if (!Object->TryGetNumberField(TEXT("schema_version"), Schema) || Schema != 1 ||
        !Object->TryGetStringField(TEXT("move_name"), Parsed.MoveName) ||
        !Object->TryGetArrayField(TEXT("tracks"), Tracks) || Tracks->Num() == 0 || Tracks->Num() > 512 ||
        !Object->TryGetArrayField(TEXT("skipped_tracks"), Skipped) || Skipped->Num() != 0 ||
        !Object->TryGetNumberField(TEXT("total_frames"), HeaderFrames) || !FMath::IsFinite(HeaderFrames) ||
        HeaderFrames < 1 || HeaderFrames > 36000 || HeaderFrames != FMath::FloorToDouble(HeaderFrames) ||
        !Object->TryGetNumberField(TEXT("move_index"), MoveIndex) || !FMath::IsFinite(MoveIndex) || MoveIndex < 0 ||
        MoveIndex > 100000 || MoveIndex != FMath::FloorToDouble(MoveIndex) ||
        !Object->TryGetObjectField(TEXT("source"), Source) || !Source || !*Source ||
        !(*Source)->TryGetNumberField(TEXT("pool_key"), Key) || !FMath::IsFinite(Key) || Key < 0 || Key > MAX_uint32 ||
        Key != FMath::FloorToDouble(Key))
        return false;
    Parsed.SourceMoveIndex = static_cast<int32>(MoveIndex);
    Parsed.AnimationKey = static_cast<uint32>(Key);

    for (const auto& Value : *Tracks)
    {
        const TSharedPtr<FJsonObject>* TrackObject = nullptr;

        if (!Value->TryGetObject(TrackObject) || !TrackObject || !*TrackObject)
            return false;
        FString Name;
        double Encoding = 0;
        const TArray<TSharedPtr<FJsonValue>>* Frames = nullptr;

        if (!(*TrackObject)->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty() || Name.Len() > 256 ||
            Parsed.Tracks.Contains(FName(Name)) || !(*TrackObject)->TryGetNumberField(TEXT("encoding"), Encoding) ||
            (Encoding != 1 && Encoding != 2 && Encoding != 3) ||
            !(*TrackObject)->TryGetArrayField(TEXT("frames"), Frames) || Frames->Num() == 0 ||
            Frames->Num() > HeaderFrames + 1)
            return false;
        double DeclaredFrames = 0;

        if (!(*TrackObject)->TryGetNumberField(TEXT("frame_count"), DeclaredFrames) || DeclaredFrames != Frames->Num())
            return false;
        FTrack Track;
        Track.Encoding = static_cast<int32>(Encoding);

        for (const auto& FrameValue : *Frames)
        {
            const TArray<TSharedPtr<FJsonValue>>* Frame = nullptr;

            if (!FrameValue->TryGetArray(Frame) || Frame->Num() != 10)
                return false;
            double V[10];

            for (int32 I = 0; I < 10; ++I)
                if (!(*Frame)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I]) || FMath::Abs(V[I]) > 1e6)
                    return false;
            FQuat Q(V[3], V[4], V[5], V[6]);

            if (Q.SizeSquared() < 0.8 || Q.SizeSquared() > 1.2)
                return false;

            if (FMath::Abs(V[0]) > 100 || FMath::Abs(V[1]) > 100 || FMath::Abs(V[2]) > 100)
                return false;
            Q.Normalize();
            Track.Samples.Add({FVector(V[0], V[1], V[2]), Q, FVector(V[7], V[8], V[9])});
        }

        Parsed.FrameCount = FMath::Max(Parsed.FrameCount, Track.Samples.Num());
        Parsed.Tracks.Add(FName(Name), MoveTemp(Track));
    }

    for (const auto& Bone : Rig)
        if (!Parsed.Tracks.Contains(FName(Bone.Name)))
            return false;
    Destination = MoveTemp(Parsed);

    return true;
}

bool UFighterReferenceMotionComponent::Initialize(USceneComponent* Parent, const FString& CharacterSlug,
                                                  const FString& AttackClip, UStaticMesh* Cube, UStaticMesh* Sphere,
                                                  UMaterialInterface* Material, const FLinearColor& Color)
{
    auto IsFileKey = [](const FString& Key)
    {
        return !Key.IsEmpty() && !Key.Contains(TEXT("/")) && !Key.Contains(TEXT("\\")) && !Key.Contains(TEXT(".."));
    };

    if (bReady || !Parent || !GetOwner() || !Cube || !Sphere || !Material || !IsFileKey(CharacterSlug) ||
        !IsFileKey(AttackClip))
        return false;
    const FString Root = FPaths::ProjectDir() / TEXT("LocalData/Animations");
    const FString AttackFile = AttackClip + TEXT(".json");

    if (!ReadClip(Root / (CharacterSlug + TEXT("-idle.json")), Idle) || !ReadClip(Root / AttackFile, Attack))
    {
        UE_LOG(LogFighterReferenceMotion, Warning, TEXT("Reference motion unavailable for %s; keep fallback proxy"),
               *CharacterSlug);

        return false;
    }

    auto* Dynamic = UMaterialInstanceDynamic::Create(Material, this);
    Dynamic->SetVectorParameterValue(TEXT("Color"), Color);
    auto MakeShape = [&](UStaticMesh* Mesh)
    {
        auto* Shape = NewObject<UStaticMeshComponent>(GetOwner());
        GetOwner()->AddInstanceComponent(Shape);
        Shape->SetupAttachment(Parent);
        Shape->SetMobility(EComponentMobility::Movable);
        Shape->SetStaticMesh(Mesh);
        Shape->SetMaterial(0, Dynamic);
        Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Shape->RegisterComponent();

        return Shape;
    };

    for (int32 I = 0; I < RigCount; ++I)
    {
        Joints.Add(MakeShape(Sphere));
        Links.Add(MakeShape(Cube));
        Joints[I]->SetRelativeScale3D(FVector(I == 8 ? 0.23f : 0.10f));

        Joints[I]->SetVisibility(I >= 4);
        Links[I]->SetVisibility(I >= 5);
    }

    bReady = true;
    UpdatePose(0, false);
    UE_LOG(
        LogFighterReferenceMotion, Display,
        TEXT(
            "REFERENCE_MOTION ready %s move=%s samples=%d tracks=%d; proxy hierarchy and axis mapping, not original mesh"),
        *CharacterSlug, *Attack.MoveName, Attack.FrameCount, Attack.Tracks.Num());

    return true;
}

void UFighterReferenceMotionComponent::SetPreviewVisible(bool bVisible)
{
    for (int32 I = 0; I < Joints.Num(); ++I)
    {
        Joints[I]->SetVisibility(bVisible && I >= 4);
        Links[I]->SetVisibility(bVisible && I >= 5);
    }
}

// 클립의 현재 포즈를 적용한다.
void UFighterReferenceMotionComponent::UpdatePose(int32 SourceFrame, bool bAttack)
{
    if (!bReady)
        return;
    const FClip& Clip = bAttack ? Attack : Idle;
    const int32 Frame =
        bAttack ? FMath::Clamp(SourceFrame, 0, Clip.FrameCount - 1) : FMath::Max(SourceFrame, 0) % Clip.FrameCount;
    SampledFrame = Frame;
    bPoseFinite = true;
    FTransform World[RigCount];
    FVector Positions[RigCount];
    PoseHash = 14695981039346656037ull;

    for (int32 I = 0; I < RigCount; ++I)
    {
        const FName Name(Rig[I].Name);
        const FTrack& Track = Clip.Tracks.FindChecked(Name);
        const FSample& Sample = Track.Samples[FMath::Min(Frame, Track.Samples.Num() - 1)];
        FVector Position = Sample.Position;

        if (Track.Encoding == 3 && I >= 4 && Position.IsNearlyZero(1e-6))
            Position = Idle.Tracks.FindChecked(Name).Samples[0].Position;

        if (I == 0)
        {
            Position.X = 0;
            Position.Z = 0;
        }

        const FTransform Local(Sample.Rotation, Position, Sample.Scale);
        World[I] = Rig[I].Parent < 0 ? Local : Local * World[Rig[I].Parent];
        Positions[I] = FighterMotionCoordinates::Position(World[I].GetTranslation());

        if (Positions[I].ContainsNaN() || Positions[I].GetAbsMax() > 1e6)
        {
            bPoseFinite = false;

            return;
        }

        Joints[I]->SetRelativeLocation(Positions[I]);

        if (Rig[I].Parent >= 0)
        {
            const FVector Start = Positions[Rig[I].Parent];
            const FVector Delta = Positions[I] - Start;
            Links[I]->SetRelativeLocation((Positions[I] + Start) * 0.5);
            Links[I]->SetRelativeRotation(Delta.Rotation());
            Links[I]->SetRelativeScale3D(FVector(FMath::Max(Delta.Size(), 0.01) / 100.0, 0.075, 0.075));
        }

        for (double Component : {Positions[I].X, Positions[I].Y, Positions[I].Z})
        {
            const float Rounded = static_cast<float>(Component);
            uint32 Bits = 0;
            FMemory::Memcpy(&Bits, &Rounded, sizeof(Bits));

            for (int32 B = 0; B < 4; ++B)
            {
                PoseHash ^= (Bits >> (B * 8)) & 255;
                PoseHash *= 1099511628211ull;
            }
        }
    }
}
