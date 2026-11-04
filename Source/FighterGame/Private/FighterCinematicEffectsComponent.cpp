// 필살기 조명과 바닥 이펙트 표시.
#include "FighterCinematicEffectsComponent.h"
#include "FighterArena.h"
#include "FighterCharacterMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "FighterRageLightingSource.h"
#include "FighterCinematicSource.h"
#include "FighterRageGroundSource.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterCinematicEffectsComponent)

namespace
{
struct FCinematicEvent
{
    uint16 Command;
    int32 First, End, System;
    const TCHAR* Bone;
    bool Follow;
    FVector Offset;
    FRotator Rotation = FRotator::ZeroRotator;
};

const FCinematicEvent Events[] = {{3101, 58, 88, 2, TEXT("Spine2"), false, FVector::ZeroVector},
                                  {3101, 222, 283, 0, TEXT("R_Hand"), true, FVector::ZeroVector},
                                  {3101, 222, 283, 11, nullptr, true, FVector::ZeroVector},
                                  {3101, 283, 359, 1, TEXT("R_Hand"), true, FVector::ZeroVector},
                                  {3101, 314, 359, 3, nullptr, true, FVector::ZeroVector},
                                  {3101, 322, 358, 12, nullptr, false, FVector(0, -500, 0)},
                                  {3101, 359, 398, 4, nullptr, false, FVector::ZeroVector},
                                  {3001, 110, 140, 5, nullptr, false, FVector::ZeroVector},
                                  {3001, 130, 151, 6, nullptr, false, FVector(-150, -50, 330)},
                                  {3001, 140, 161, 6, nullptr, false, FVector(50, -135, 420), FRotator(0, -25, 0)},
                                  {3001, 150, 171, 6, nullptr, false, FVector(-50, -50, 600), FRotator(0, -15, -30)},
                                  {3001, 164, 209, 7, nullptr, false, FVector::ZeroVector},
                                  {3001, 360, 412, 8, TEXT("R_Foot"), true, FVector::ZeroVector},
                                  {3001, 412, 454, 9, nullptr, false, FVector(0, 70, 0)},
                                  {3001, 412, 454, 13, nullptr, false, FVector(0, 70, 0)},
                                  {3001, 414, 561, 10, nullptr, false, FVector(0, 70, 0)}};
const TCHAR* Names[] = {TEXT("PaulArmWind"),      TEXT("PaulPunchWind"),     TEXT("PaulInk"),
                        TEXT("PaulShock"),        TEXT("PaulShockTail"),     TEXT("KazuyaExplosion0"),
                        TEXT("KazuyaExplosion1"), TEXT("KazuyaExplosion2"),  TEXT("KazuyaLeg"),
                        TEXT("KazuyaThunder1"),   TEXT("KazuyaThunder2"),    TEXT("PaulBodyWind"),
                        TEXT("PaulForceWave"),    TEXT("KazuyaThunder1Ring")};

FTransform Placement(const AFighterArena& Arena, int32 Owner, const FCinematicEvent& Event)
{
    const auto& A = Arena.GetCombatState().fighters[Owner];
    const FVector Forward(A.face_cos / 65536.f, A.face_sin / 65536.f, 0), Left(Forward.Y, -Forward.X, 0);
    FVector Position(A.position_x * .1f, A.position_z * .1f, 0);

    if (Event.Bone)
        if (const auto* Model = Arena.GetCharacterMesh(Owner))
            Model->GetBoneWorldLocation(FName(Event.Bone), Position);
    Position += Left * Event.Offset.X + Forward * Event.Offset.Y + FVector(0, 0, Event.Offset.Z);

    return FTransform(FRotator(0, Forward.Rotation().Yaw - 90.f, 0).Quaternion() * Event.Rotation.Quaternion(),
                      Position);
}
}

void UFighterCinematicEffectsComponent::BeginPlay()
{
    Super::BeginPlay();

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyCinematicFX")))
        return;

    for (const TCHAR* Name : Names)
    {
        const FString Path =
            FString::Printf(TEXT("/Game/LocalPresentation/Niagara/NS_FG_Cine%s.NS_FG_Cine%s"), Name, Name);
        Systems.Add(LoadObject<UNiagaraSystem>(nullptr, *Path));

        if (!Systems.Last())
            UE_LOG(LogTemp, Error, TEXT("FIGHTER_CINEMATIC_FX missing=%s"), *Path);
    }

    UE_LOG(LogTemp, Display,
           TEXT("FIGHTER_CINEMATIC_FX ready=%d systems=%d events=16 source_times=1 authored_renderer_adapter=1"),
           Systems.Num() == UE_ARRAY_COUNT(Names) && !Systems.Contains(nullptr), Systems.Num());

    for (const auto& Layer : RageGroundLayers)
    {
        const FString Path =
            FString::Printf(TEXT("/Game/LocalPresentation/SourceFX/RageGround/%s.%s"), Layer.Mesh, Layer.Mesh);
        GroundMeshes.Add(LoadObject<UStaticMesh>(nullptr, *Path));
        const FString MaterialName =
            FString(Layer.Material) +
            (FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyGroundMaterial")) ? TEXT("_Legacy") : TEXT(""));
        const FString MaterialPath =
            FString::Printf(TEXT("/Game/LocalPresentation/SourceFX/RageGround/%s.%s"), *MaterialName, *MaterialName);
        GroundMaterials.Add(LoadObject<UMaterialInterface>(nullptr, *MaterialPath));

        if (!GroundMaterials.Last())
            UE_LOG(LogTemp, Error, TEXT("FIGHTER_RAGE_GROUND missing_material=%s"), *MaterialPath);

        if (!GroundMeshes.Last())
            UE_LOG(LogTemp, Error, TEXT("FIGHTER_RAGE_GROUND missing=%s run=create_kazuya_rage_ground.py"), *Path);
    }
}

void UFighterCinematicEffectsComponent::Reset()
{
    for (auto& Item : Active)
        if (Item.FX.IsValid())
            Item.FX->DestroyComponent();

    for (UStaticMeshComponent* Piece : GroundPieces)
        if (Piece)
            Piece->DestroyComponent();
    GroundPieces.Reset();
    Active.Reset();
    Owner = INDEX_NONE;
    Command = 0;
    LastFrame = 0;
    LastSimulationFrame = 0;

    for (ULightComponent* L : PreLights)
        if (L)
            L->SetVisibility(false);

    for (ULightComponent* L : LevelLights)
        if (L)
            L->SetVisibility(false);
    bLightingActive = false;
}

void UFighterCinematicEffectsComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Reset();
    Super::EndPlay(Reason);
}

// 현재 연출 프레임의 효과를 갱신한다.
void UFighterCinematicEffectsComponent::Present(const AFighterArena& Arena)
{
    const auto& State = Arena.GetCombatState();
    int32 NewOwner = INDEX_NONE;

    for (int32 I = 0; I < 2; ++I)
        if (State.fighters[I].sequence_role == 1 &&
            (State.fighters[I].sequence_command == modular_fighter::CommandId::PaulRageArt ||
             State.fighters[I].sequence_command == modular_fighter::CommandId::KazuyaRageArt))
            NewOwner = I;

    if (NewOwner == INDEX_NONE)
    {
        if (Owner != INDEX_NONE)
        {
            UE_LOG(LogTemp, Display, TEXT("FIGHTER_CINEMATIC_FX cleanup=1 remaining=0"));
            Reset();
        }

        PresentLighting(Arena);

        return;
    }

    const auto& A = State.fighters[NewOwner];
    const int32 Frame = A.sequence_frame;
    const uint16 NewCommand = static_cast<uint16>(A.sequence_command);

    if (NewOwner != Owner || NewCommand != Command || Frame < LastFrame || State.frame < LastSimulationFrame)
    {
        Reset();
        Owner = NewOwner;
        Command = NewCommand;
    }

    PresentLighting(Arena);
    PresentGround(Arena);

    for (int32 I = 0; I < UE_ARRAY_COUNT(Events); ++I)
    {
        const auto& E = Events[I];

        if (E.Command != Command || E.First <= LastFrame || E.First > Frame || Frame >= E.End ||
            !Systems.IsValidIndex(E.System) || !Systems[E.System])
            continue;

        FString Skip;

        if (FParse::Value(FCommandLine::Get(), TEXT("FighterCineSkip="), Skip, false))
        {
            TArray<FString> SkipNames;
            Skip.ParseIntoArray(SkipNames, TEXT(","));

            if (SkipNames.Contains(Names[E.System]))
                continue;
        }

        const FTransform T = Placement(Arena, Owner, E);
        auto* FX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, Systems[E.System], T.GetLocation(), T.Rotator(),
                                                                  FVector(1), false, true);

        if (!FX)
            continue;
        FX->SetCastShadow(false);
        FX->SetAgeUpdateMode(ENiagaraAgeUpdateMode::DesiredAge);
        FX->SetSeekDelta(1.f / 60.f);
        FX->SetCanRenderWhileSeeking(true);
        FX->SetDesiredAge((Frame - E.First) / 60.f);
        Active.Add({FX, I});
        UE_LOG(LogTemp, Display,
               TEXT("FIGHTER_CINEMATIC_EVENT command=%u owner=%d source_frame=%d observed=%d effect=%s"), Command,
               Owner, E.First, Frame, Names[E.System]);
    }

    for (int32 I = Active.Num() - 1; I >= 0; --I)
    {
        auto& Item = Active[I];
        const auto& E = Events[Item.Event];

        if (!Item.FX.IsValid() || Frame >= E.End)
        {
            if (Item.FX.IsValid())
                Item.FX->DestroyComponent();
            Active.RemoveAt(I);
            continue;
        }

        if (E.Follow)
            Item.FX->SetWorldTransform(Placement(Arena, Owner, E));

        if (E.System == 13 && !FParse::Param(FCommandLine::Get(), TEXT("FighterRawCinematicFraming")))
            if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
            {
                constexpr float RingNearHideCm = 300.f;
                const float Distance =
                    FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Item.FX->GetComponentLocation());
                const bool bVisible = Distance >= RingNearHideCm;

                if (bVisible != Item.FX->IsVisible() || Frame == E.First)
                    UE_LOG(
                        LogTemp, Display,
                        TEXT(
                            "FIGHTER_CINEMATIC_RING_HIDE frame=%d distance_cm=%.1f visible=%d near_cm=%.0f authored=1"),
                        Frame, Distance, bVisible ? 1 : 0, RingNearHideCm);
                Item.FX->SetVisibility(bVisible);
            }
        Item.FX->SetDesiredAge((Frame - E.First) / 60.f);
    }

    LastFrame = Frame;
    LastSimulationFrame = State.frame;
}

void UFighterCinematicEffectsComponent::PresentGround(const AFighterArena& Arena)
{
    const int32 Frame = Arena.GetCombatState().fighters[Owner].sequence_frame;
    const bool Enabled = Command == 3001 && Frame >= 414 && Frame < 561 &&
                         !FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyRageGround"));

    if (!Enabled)
    {
        for (UStaticMeshComponent* Piece : GroundPieces)
            if (Piece)
                Piece->SetVisibility(false);

        return;
    }

    if (GroundMeshes.Num() != UE_ARRAY_COUNT(RageGroundLayers) || GroundMeshes.Contains(nullptr) ||
        GroundMaterials.Num() != UE_ARRAY_COUNT(RageGroundLayers) || GroundMaterials.Contains(nullptr))
        return;

    constexpr int32 Count = 8;

    if (GroundPieces.IsEmpty())
    {
        for (int32 Layer = 0; Layer < UE_ARRAY_COUNT(RageGroundLayers); ++Layer)
            for (int32 I = 0; I < Count; ++I)
            {
                auto* Piece = NewObject<UStaticMeshComponent>(GetOwner());
                GetOwner()->AddInstanceComponent(Piece);
                Piece->SetMobility(EComponentMobility::Movable);
                Piece->SetStaticMesh(GroundMeshes[Layer]);

                for (int32 Slot = 0; Slot < Piece->GetNumMaterials(); ++Slot)
                    Piece->SetMaterial(Slot, GroundMaterials[Layer]);
                Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                Piece->SetCanEverAffectNavigation(false);
                Piece->SetCastShadow(false);
                Piece->SetVisibility(false);
                Piece->RegisterComponent();
                GroundPieces.Add(Piece);
            }
        UE_LOG(
            LogTemp, Display,
            TEXT(
                "FIGHTER_RAGE_GROUND first=414 end=561 observed=%d source_layers=8 omitted_layers=10 pieces=64 authored_count_seed=1 source_rotation_layers=4 source_delay_layers=8 inferred_curve_binding=1"),
            Frame);
    }

    const auto& State = Arena.GetCombatState();
    const auto& A = State.fighters[Owner];
    const FVector Forward(A.face_cos / 65536.f, A.face_sin / 65536.f, 0), Left(Forward.Y, -Forward.X, 0);
    FVector Anchor(A.position_x * .1f, A.position_z * .1f, 0);
    const int32 Side = Arena.GetViewSide(A.face_cos, A.face_sin);

    for (const auto& Track : SourceCinematicTracks)
        if (Track.Command == A.sequence_command && Track.Finish && Track.Side == Side && Frame <= int32(Track.Count) &&
            Track.Count >= 414)
        {
            const auto& Now = SourceCinematicSamples[Track.First + Frame - 1];
            const auto& Start = SourceCinematicSamples[Track.First + 413];
            Anchor += Left * (Start.RootLeft - Now.RootLeft) + Forward * (Start.RootForward - Now.RootForward);
            break;
        }
    Anchor += Forward * 70.f;
    const float SystemAgeFrames = float(Frame - 414);

    for (int32 Layer = 0; Layer < UE_ARRAY_COUNT(RageGroundLayers); ++Layer)
        for (int32 I = 0; I < Count; ++I)
        {
            const auto& S = RageGroundLayers[Layer];
            UStaticMeshComponent* Piece = GroundPieces[Layer * Count + I];
            const float U = (I + .5f) / Count, V = float((I * 5 + Layer * 3) % Count + .5f) / Count;
            const float AgeFrames = SystemAgeFrames - S.Delay * 60.f, Age = AgeFrames / 60.f;
            const float Life = S.Life + S.LifeRange * V;

            Piece->SetVisibility(AgeFrames > 0.f && Age < Life);

            if (AgeFrames <= 0.f || Age >= Life)
                continue;
            const float Q = 1.f / (1.f + S.Drag / 60.f);
            const int32 WholeFrames = FMath::FloorToInt(AgeFrames);
            const float PartialTime = (AgeFrames - WholeFrames) / 60.f, InitialSpeed = S.Speed + S.SpeedRange * V;
            const float WholeDecay = FMath::Pow(Q, WholeFrames);
            const float Rise = InitialSpeed / 60.f * Q * (1.f - WholeDecay) / (1.f - Q) +
                               InitialSpeed * WholeDecay / (1.f + S.Drag * PartialTime) * PartialTime;
            const float Radius = S.Radius + S.RadiusRange * V, Angle = PI * U;
            const float BaseZ = Layer < 2    ? -18.f
                                : Layer < 4  ? -10.5f
                                : Layer == 4 ? -13.f
                                : Layer == 5 ? -3.25f
                                : Layer == 6 ? -10.f
                                             : -10.5f;
            const float Z = BaseZ + (Layer == 6 ? 10.f : 3.f) * V + Rise;
            const FVector Position = Anchor + Left * (Radius * FMath::Cos(Angle) * (Layer == 6 ? 1.5f : 1.f)) +
                                     Forward * (Radius * FMath::Sin(Angle)) + FVector(0, 0, Z);
            const float T = FMath::Clamp(Age / Life, 0.f, 1.f) * 64.f;
            const int32 J = FMath::Min(63, FMath::FloorToInt(T));
            const float Curve =
                FMath::Lerp(RageGroundScaleCurves[Layer][J], RageGroundScaleCurves[Layer][J + 1], T - J);

            const float Control =
                FMath::Lerp(RageGroundDissolveCurves[Layer][J], RageGroundDissolveCurves[Layer][J + 1], T - J);
            const float Vanish = FMath::Clamp((Control - 1.f) / .5f, 0.f, 1.f);
            const float Release = FMath::Clamp((float(Frame) - 548.f) / 12.f, 0.f, 1.f);
            const float Phase = FMath::Max(Vanish, Release);

            if (Phase >= 1.f)
            {
                Piece->SetVisibility(false);
                continue;
            }

            Piece->SetCustomPrimitiveDataFloat(0, Phase);
            const FVector Scale =
                Layer < 2 ? FVector(.8f + .3f * U, .7f + .1f * V, .8f + .2f * U) * S.Scale * (.98f + .02f * V) * Curve
                : Layer < 4
                    ? FVector(1.f + .5f * U, .75f + .25f * V, .75f + .25f * U) * S.Scale * (.5f + .5f * V) * Curve
                : Layer < 6 ? FVector(1.6f + .4f * U, .8f + .2f * V, .8f + .2f * U) * S.Scale * Curve
                : Layer == 6
                    ? FVector(1.f + .2f * U, .25f + .25f * V, 1.f + .2f * U) * S.Scale * Curve
                    : FVector(1.f + .5f * U, .75f + .25f * V, .75f + .25f * U) * S.Scale * (.5f + .5f * V) * Curve;
            FQuat Rotation = FRotator(0, Forward.Rotation().Yaw - 90.f, 0).Quaternion();

            if (Layer < 2 || Layer >= 6)
            {
                const float W = float((I * 3 + Layer * 5) % Count + .5f) / Count;
                const FVector InitialVector(-U, Layer < 2 ? 2.f * V - 1.f : V, W);
                const float InitialAngle = InitialVector.Size() * 2.f * PI;
                const FVector SpinVector(-(2.f * W - 1.f), 2.f * U - 1.f, 2.f * V - 1.f);
                float Turns = 0.f;

                for (int32 Step = 1; Step <= FMath::CeilToInt(AgeFrames); ++Step)
                {
                    const float EndFrame = FMath::Min(float(Step), AgeFrames);
                    const float StepTime = (EndFrame - float(Step - 1)) / 60.f;
                    const float RT = FMath::Clamp((EndFrame / 60.f) / Life, 0.f, 1.f) * 64.f;
                    const int32 RJ = FMath::Min(63, FMath::FloorToInt(RT));
                    Turns += FMath::Lerp(RageGroundRotationCurves[Layer][RJ], RageGroundRotationCurves[Layer][RJ + 1],
                                         RT - RJ) *
                             StepTime;
                }

                const FQuat Initial(InitialVector.GetSafeNormal(), InitialAngle);
                const FQuat Spin(SpinVector.GetSafeNormal(), SpinVector.Size() * Turns * 2.f * PI);
                Rotation = (Rotation * Initial * Spin).GetNormalized();
            }

            Piece->SetWorldTransform(FTransform(Rotation, Position, Scale));
        }
}

void UFighterCinematicEffectsComponent::PresentLighting(const AFighterArena& Arena)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyRageEnvironment")))
        return;
    const auto& State = Arena.GetCombatState();
    int32 PreOwner = INDEX_NONE, PreFrame = 0, FinishOwner = INDEX_NONE;

    for (int32 I = 0; I < 2; ++I)
    {
        const auto& A = State.fighters[I];

        if (!A.sequence_role && A.attack_frame > 0 &&
            (A.active_command == modular_fighter::CommandId::PaulRageArt ||
             A.active_command == modular_fighter::CommandId::KazuyaRageArt))
        {
            PreOwner = I;
            PreFrame = int32(A.attack_frame) - 1;
        }

        if (A.sequence_role == 1 && A.sequence_command == modular_fighter::CommandId::PaulRageArt &&
            ((A.sequence_frame >= 16 && A.sequence_frame < 315) || (A.sequence_frame >= 360 && A.sequence_frame < 415)))
            FinishOwner = I;
    }

    const bool Enabled = PreOwner != INDEX_NONE || FinishOwner != INDEX_NONE;

    if (!Enabled)
    {
        for (ULightComponent* L : PreLights)
            L->SetVisibility(false);

        for (ULightComponent* L : LevelLights)
            L->SetVisibility(false);

        if (bLightingActive)
            UE_LOG(LogTemp, Display, TEXT("FIGHTER_RAGE_LIGHTING active=0"));
        bLightingActive = false;

        return;
    }

    auto Register = [&](ULightComponent* L)
    {
        GetOwner()->AddInstanceComponent(L);
        L->SetMobility(EComponentMobility::Movable);
        L->LightingChannels.bChannel0 = false;
        L->LightingChannels.bChannel1 = true;
        L->SetCastShadows(false);
        L->SetVisibility(false);
        L->RegisterComponent();
    };

    if (PreLights.IsEmpty())
        for (int32 I = 0; I < 3; ++I)
        {
            auto* L = NewObject<UDirectionalLightComponent>(GetOwner());
            Register(L);
            PreLights.Add(L);
        }
    if (LevelLights.IsEmpty())
        for (const auto& S : RageLevelLights)
        {
            ULightComponent* L =
                S.Type == 2   ? static_cast<ULightComponent*>(NewObject<UDirectionalLightComponent>(GetOwner()))
                : S.Type == 1 ? static_cast<ULightComponent*>(NewObject<USpotLightComponent>(GetOwner()))
                              : static_cast<ULightComponent*>(NewObject<UPointLightComponent>(GetOwner()));

            if (auto* P = Cast<UPointLightComponent>(L))
            {
                P->SetIntensityUnits(ELightUnits::Candelas);
                P->SetAttenuationRadius(S.Radius);
                P->SetSourceRadius(S.SourceRadius);
                P->SetSoftSourceRadius(S.SoftRadius);
            }

            if (auto* P = Cast<USpotLightComponent>(L))
                P->SetOuterConeAngle(S.Cone);

            L->SetIntensity(S.Intensity * .08f);
            L->SetLightColor(
                FLinearColor::FromSRGBColor(FColor(uint8(S.Color[0]), uint8(S.Color[1]), uint8(S.Color[2]))));
            Register(L);
            LevelLights.Add(L);
        }
    float CameraYaw = 0.f;

    if (const auto* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        CameraYaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;

    for (int32 I = 0; I < PreLights.Num(); ++I)
    {
        ULightComponent* L = PreLights[I];
        const bool Show = PreOwner != INDEX_NONE && PreFrame >= 1 && PreFrame < 121;
        L->SetVisibility(Show);

        if (!Show)
            continue;
        const auto& S = RagePreLights[I][PreFrame];
        L->SetIntensity(S.Intensity * .004f);
        L->SetSpecularScale(S.Specular);
        L->SetLightColor(FLinearColor(S.R, S.G, S.B));
        L->SetWorldRotation(FRotator(-S.Pitch, CameraYaw + S.Yaw, 0));
    }

    FVector Origin = FVector::ZeroVector, Forward(1, 0, 0), Left(0, -1, 0);

    if (FinishOwner != INDEX_NONE)
    {
        const auto& A = State.fighters[FinishOwner];
        Forward = FVector(A.face_cos / 65536.f, A.face_sin / 65536.f, 0);
        Left = FVector(Forward.Y, -Forward.X, 0);
        const int32 Side = Arena.GetViewSide(A.face_cos, A.face_sin);

        for (const auto& T : SourceCinematicTracks)
            if (T.Command == A.sequence_command && T.Finish && T.Side == Side && A.sequence_frame > 0 &&
                A.sequence_frame <= T.Count)
            {
                const auto& S = SourceCinematicSamples[T.First + A.sequence_frame - 1];
                Origin =
                    FVector(A.position_x * .1f, A.position_z * .1f, 0) - Left * S.RootLeft - Forward * S.RootForward;
                break;
            }
    }

    for (int32 I = 0; I < LevelLights.Num(); ++I)
    {
        ULightComponent* L = LevelLights[I];
        L->SetVisibility(FinishOwner != INDEX_NONE);

        if (FinishOwner == INDEX_NONE)
            continue;
        const auto& S = RageLevelLights[I];
        L->SetWorldLocationAndRotation(
            Origin + Left * S.Position[0] + Forward * S.Position[1] + FVector(0, 0, S.Position[2]),
            FRotator(S.Rotation[0], Forward.Rotation().Yaw - 90.f + S.Rotation[1], S.Rotation[2]));
    }

    if (!bLightingActive)
        UE_LOG(
            LogTemp, Display,
            TEXT(
                "FIGHTER_RAGE_LIGHTING active=1 pre_lights=3 source_samples=121 paul_level_lights=%d pre_lux_scale=0.004 level_intensity_scale=0.08 authored_anchor=1"),
            LevelLights.Num());
    bLightingActive = true;
}
