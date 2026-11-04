// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 카메라 모드 스택과 시네마틱 후처리를 묶는다.
#include "FighterCameraComponent.h"
#include "FighterCameraMode.h"
#include "FighterMotionFeel.h"
#include "FighterArena.h"
#include "FighterStageComponent.h"
#include "FighterCinematicPostSource.h"
#include "FighterPaulLetterSource.h"
#include "FighterPaulSpaceSource.h"
#include "FighterCinematicSource.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterCameraComponent)

UFighterCameraComponent::UFighterCameraComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    CameraModeStack = nullptr;
    FieldOfViewOffset = 0.0f;
}

void UFighterCameraComponent::OnRegister()
{
    Super::OnRegister();
    FighterMotionFeel::ApplyFrameOptions();

    const bool Disabled =
        FighterMotionFeel::HardCut() || FParse::Param(FCommandLine::Get(), TEXT("FighterDisableMotionBlur"));
    PostProcessSettings.bOverride_MotionBlurAmount = true;
    PostProcessSettings.MotionBlurAmount = Disabled ? 0.f : FighterMotionFeel::BlurAmount();
    PostProcessSettings.bOverride_MotionBlurMax = true;
    PostProcessSettings.MotionBlurMax = FighterMotionFeel::BlurMax();
    PostProcessSettings.bOverride_MotionBlurTargetFPS = true;
    PostProcessSettings.MotionBlurTargetFPS = 60;
    PostProcessBlendWeight = 1.f;
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_MOTION_BLUR enabled=%d amount=%.2f max=%g target_fps=60 authored=1"),
           Disabled ? 0 : 1, PostProcessSettings.MotionBlurAmount, PostProcessSettings.MotionBlurMax);
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_MOTION_FEEL mask=%d attack_blend=%d air_rehit_blend=%d combat_exit_blend=%d battle_axis_lag=%.2f authored=1 core_hz=60"),
        FighterMotionFeel::Mask(),
        FighterMotionFeel::HardCut() ? 0
        : FighterMotionFeel::Pose()  ? 1
                                     : 2,
        FighterMotionFeel::HardCut() ? 0
        : FighterMotionFeel::Pose()  ? 2
                                     : 3,
        FighterMotionFeel::HardCut() ? 0
        : FighterMotionFeel::Pose()  ? 2
                                     : 4,
        FighterMotionFeel::AxisLag());

    if (!CameraModeStack)
    {
        CameraModeStack = NewObject<UFighterCameraModeStack>(this);
        check(CameraModeStack);
    }

    if (!HitBlurMaterial)
        if (auto* Base = LoadObject<UMaterialInterface>(
                nullptr, TEXT("/Game/LocalPresentation/PostProcess/M_PP_HitBlur.M_PP_HitBlur")))
        {
            HitBlurMaterial = UMaterialInstanceDynamic::Create(Base, this);
            HitBlurMaterial->SetScalarParameterValue(TEXT("HitBlurStrength"), 0.f);
            PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, HitBlurMaterial));
            PostProcessBlendWeight = 1.f;
        }
    if (!RageBackgroundMaterial)
        if (auto* Base = LoadObject<UMaterialInterface>(
                nullptr, TEXT("/Game/LocalPresentation/PostProcess/M_PP_RageBackground.M_PP_RageBackground")))
        {
            RageBackgroundMaterial = UMaterialInstanceDynamic::Create(Base, this);
            PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, RageBackgroundMaterial));
        }
    if (!RageSpaceMaterial)
        if (auto* Base = LoadObject<UMaterialInterface>(
                nullptr, TEXT("/Game/LocalPresentation/PostProcess/M_PP_RageSpace.M_PP_RageSpace")))
        {
            RageSpaceMaterial = UMaterialInstanceDynamic::Create(Base, this);
            PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, RageSpaceMaterial));
        }
}

void UFighterCameraComponent::AddHitBlur(float Strength, const FVector2D& Center)
{
    HitBlurStrength = FMath::Max(HitBlurStrength * FMath::Max(0.f, 1.f - HitBlurAge), Strength);
    HitBlurCenter = Center;
    HitBlurAge = 0.f;
}

void UFighterCameraComponent::UpdatePaulSpace(bool bShowBackdrop, const FTransform& Root)
{
    if (bShowBackdrop && PaulSpacePlanes.IsEmpty())
    {
        auto* Mesh = LoadObject<UStaticMesh>(
            nullptr, TEXT("/Game/LocalPresentation/PostProcess/PaulSpace/SM_Rage_Plane_temp_01.SM_Rage_Plane_temp_01"));

        if (!Mesh)
        {
            UE_LOG(LogTemp, Error, TEXT("FIGHTER_PAUL_SPACE missing=mesh"));

            return;
        }

        for (int32 I = 0; I < 3; ++I)
        {
            const FString Path = FString::Printf(
                TEXT("/Game/LocalPresentation/PostProcess/PaulSpace/M_PaulNebula_%02d.M_PaulNebula_%02d"), I + 1,
                I + 1);
            auto* Material = LoadObject<UMaterialInterface>(nullptr, *Path);

            if (!Material)
            {
                UE_LOG(LogTemp, Error, TEXT("FIGHTER_PAUL_SPACE missing=%s"), *Path);
                continue;
            }

            auto* Plane = NewObject<UStaticMeshComponent>(GetOwner());
            GetOwner()->AddInstanceComponent(Plane);
            Plane->SetMobility(EComponentMobility::Movable);
            Plane->SetStaticMesh(Mesh);
            Plane->SetMaterial(0, Material);
            Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Plane->SetGenerateOverlapEvents(false);
            Plane->SetCastShadow(false);
            Plane->RegisterComponent();
            PaulSpacePlanes.Add(Plane);
        }
    }

    for (int32 I = 0; I < PaulSpacePlanes.Num(); ++I)
    {
        UStaticMeshComponent* Plane = PaulSpacePlanes[I];
        Plane->SetVisibility(bShowBackdrop);

        if (bShowBackdrop)
            Plane->SetWorldTransform(PaulSpaceTransforms[I] * PaulSpaceTransforms[3] * Root);
    }

    if (bPaulSpaceShown != bShowBackdrop)
    {
        bPaulSpaceShown = bShowBackdrop;
        UE_LOG(
            LogTemp, Display,
            TEXT(
                "FIGHTER_PAUL_SPACE active=%d planes=%d source_level_transforms=1 static_placement=1 shader_adapter=1"),
            bShowBackdrop ? 1 : 0, PaulSpacePlanes.Num());
    }
}

void UFighterCameraComponent::StartDeathfistFocus(uint32 Frame, const FVector& Contact)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("FighterDisableDeathfistFocus")))
        return;
    bDeathfistFocus = true;
    DeathfistFrame = Frame;
    DeathfistContact = Contact;
    UE_LOG(
        LogTemp, Display,
        TEXT("FIGHTER_DEATHFIST_CAMERA frame=%u peak_zoom=1.35 duration_frames=34 video_reference=Paul_CS.2 adapter=1"),
        Frame);
}

// 스택을 평가해 최종 카메라를 만든다.
void UFighterCameraComponent::GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView)
{
    check(CameraModeStack);

    UpdateCameraModes();

    FFighterCameraModeView CameraModeView;
    CameraModeStack->EvaluateStack(DeltaTime, CameraModeView);

    if (bDeathfistFocus)
        if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
        {
            const auto& State = Arena->GetCombatState();
            const int64 Age = int64(State.frame) - int64(DeathfistFrame);

            if (Age < 0 || Age >= 34 || State.fighters[0].sequence_role || State.fighters[1].sequence_role)
                bDeathfistFocus = false;
            else
            {
                const float T = Age < 2 ? float(Age) / 2.f : Age < 18 ? 1.f : 1.f - float(Age - 18) / 16.f;
                const float Weight = T * T * (3.f - 2.f * T);
                const float Zoom = 1.f + .35f * Weight;
                CameraModeView.FieldOfView = FMath::RadiansToDegrees(
                    2.f * FMath::Atan(FMath::Tan(FMath::DegreesToRadians(CameraModeView.FieldOfView * .5f)) / Zoom));

                const FRotator Aim = (DeathfistContact - CameraModeView.Location).Rotation();
                CameraModeView.Rotation.Pitch = FMath::Lerp(CameraModeView.Rotation.Pitch, Aim.Pitch, .35f * Weight);
                CameraModeView.ControlRotation = CameraModeView.Rotation;

                if (FParse::Param(FCommandLine::Get(), TEXT("FighterProfilePresentation")))
                    UE_LOG(LogTemp, Display,
                           TEXT("FIGHTER_DEATHFIST_VIEW frame=%u age=%lld zoom=%.4f fov=%.3f pitch=%.3f"), State.frame,
                           Age, Zoom, CameraModeView.FieldOfView, CameraModeView.Rotation.Pitch);
            }
        }

    if (APawn* TargetPawn = Cast<APawn>(GetTargetActor()))
    {
        if (APlayerController* PC = TargetPawn->GetController<APlayerController>())
        {
            PC->SetControlRotation(CameraModeView.ControlRotation);
        }
    }

    CameraModeView.FieldOfView += FieldOfViewOffset;
    FieldOfViewOffset = 0.0f;
    CameraModeView.Location += PresentationOffset;
    PresentationOffset = FVector::ZeroVector;
    CameraModeView.Rotation.Pitch += FMath::RadiansToDegrees(SourcePitchRadians);
    SourcePitchRadians = 0;

    if (HitBlurMaterial)
    {
        HitBlurAge = FMath::Min(1.f, HitBlurAge + DeltaTime / .15f);
        const float Amount = HitBlurStrength * FMath::Square(1.f - HitBlurAge);
        HitBlurMaterial->SetScalarParameterValue(TEXT("HitBlurStrength"), Amount);
        HitBlurMaterial->SetVectorParameterValue(TEXT("HitBlurCenter"),
                                                 FLinearColor(HitBlurCenter.X, HitBlurCenter.Y, 0.f, 0.f));
    }

    SetWorldLocationAndRotation(CameraModeView.Location, CameraModeView.Rotation);
    FieldOfView = CameraModeView.FieldOfView;

    DesiredView.Location = CameraModeView.Location;
    DesiredView.Rotation = CameraModeView.Rotation;
    DesiredView.FOV = CameraModeView.FieldOfView;
    DesiredView.OrthoWidth = OrthoWidth;
    DesiredView.OrthoNearClipPlane = OrthoNearClipPlane;
    DesiredView.OrthoFarClipPlane = OrthoFarClipPlane;
    DesiredView.AspectRatio = AspectRatio;
    DesiredView.bConstrainAspectRatio = bConstrainAspectRatio;
    DesiredView.bUseFieldOfViewForLOD = bUseFieldOfViewForLOD;
    DesiredView.ProjectionMode = ProjectionMode;

    DesiredView.PostProcessBlendWeight = PostProcessBlendWeight;

    if (PostProcessBlendWeight > 0.0f)
    {
        DesiredView.PostProcessSettings = PostProcessSettings;
    }

    int32 RageFrame = -1;
    float PaulPlanetWeight = 0.f;
    float PaulSpaceWeight = 0.f;
    FTransform PaulSpaceRoot = FTransform::Identity;
    FPaulLetterSample Letter{1, 0, 0, 0, 0, 0};

    if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
        for (const auto& Fighter : Arena->GetCombatState().fighters)
        {
            if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyRageEnvironment")) &&
                Fighter.sequence_role == 1 && Fighter.sequence_command == modular_fighter::CommandId::PaulRageArt &&
                ((Fighter.sequence_frame >= 16 && Fighter.sequence_frame < 315) ||
                 (Fighter.sequence_frame >= 360 && Fighter.sequence_frame < 415)))
            {
                PaulSpaceWeight = 1.f;
                const FVector Forward(Fighter.face_cos / 65536.f, Fighter.face_sin / 65536.f, 0.f);
                const FVector Left(Forward.Y, -Forward.X, 0.f);
                const auto& State = Arena->GetCombatState();
                const int32 Side = Arena->GetViewSide(Fighter.face_cos, Fighter.face_sin);

                for (const auto& Track : SourceCinematicTracks)
                    if (Track.Finish && Track.Side == Side && Track.Command == Fighter.sequence_command &&
                        Fighter.sequence_frame > 0 && Fighter.sequence_frame <= Track.Count)
                    {
                        const auto& Sample = SourceCinematicSamples[Track.First + Fighter.sequence_frame - 1];
                        const FVector Origin = FVector(Fighter.position_x * .1f, Fighter.position_z * .1f, 0.f) -
                                               Left * Sample.RootLeft - Forward * Sample.RootForward;
                        PaulSpaceRoot = FTransform(FRotator(0, Forward.Rotation().Yaw - 90.f, 0), Origin);
                        break;
                    }
            }

            if (!Fighter.sequence_role &&
                (Fighter.active_command == modular_fighter::CommandId::KazuyaRageArt ||
                 Fighter.active_command == modular_fighter::CommandId::PaulRageArt) &&
                Fighter.attack_frame > 0)
                RageFrame = int32(Fighter.attack_frame) - 1;

            if (Fighter.sequence_role == 1 && Fighter.sequence_command == modular_fighter::CommandId::PaulRageArt &&
                Fighter.sequence_frame >= 59 && Fighter.sequence_frame < 59 + UE_ARRAY_COUNT(PaulLetterSamples))
            {
                Letter = PaulLetterSamples[Fighter.sequence_frame - 59];
                PaulPlanetWeight = 1.f;
            }
        }
    const bool Enabled = !FParse::Param(FCommandLine::Get(), TEXT("FighterDisableSourceRagePost"));

    if (bPaulLetteringShown != (PaulPlanetWeight > 0.f))
    {
        bPaulLetteringShown = PaulPlanetWeight > 0.f;
        UE_LOG(LogTemp, Display,
               TEXT("FIGHTER_PAUL_LETTERING active=%d source_widget_frames=42 aligned_source_se=59 behind_character=1"),
               bPaulLetteringShown ? 1 : 0);
    }

    const FSourceRagePostSample S = Enabled && RageFrame >= 0 && RageFrame < UE_ARRAY_COUNT(SourceRagePostSamples)
                                        ? SourceRagePostSamples[RageFrame]
                                        : FSourceRagePostSample{0, 0, 0, 0};

    if (RageBackgroundMaterial)
    {
        RageBackgroundMaterial->SetScalarParameterValue(TEXT("BackgroundDesaturation"), S.BackgroundDesaturation);
        RageBackgroundMaterial->SetScalarParameterValue(TEXT("PaulPlanetWeight"), PaulPlanetWeight);
        RageBackgroundMaterial->SetScalarParameterValue(TEXT("PaulSpaceWeight"), 0.f);
        RageBackgroundMaterial->SetScalarParameterValue(TEXT("PaulLetterScale"), Letter.Scale);
        RageBackgroundMaterial->SetVectorParameterValue(
            TEXT("PaulLetterParams"), FLinearColor(Letter.Alpha, Letter.FadeL, Letter.SpeedL, Letter.FadeR));
        RageBackgroundMaterial->SetScalarParameterValue(TEXT("PaulLetterRightSpeed"), Letter.SpeedR);
    }

    if (RageSpaceMaterial)
        RageSpaceMaterial->SetScalarParameterValue(TEXT("PaulSpaceWeight"), PaulSpaceWeight);
    UpdatePaulSpace(PaulSpaceWeight > 0.f, PaulSpaceRoot);

    if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
        if (auto* Stage = Arena->GetStagePresentation())
            Stage->SetCinematicWaterHidden(PaulSpaceWeight > 0.f);

    if (S.Weight > 0.f)
    {
        auto& PP = DesiredView.PostProcessSettings;
        PP.BloomIntensity = FMath::Lerp(PP.BloomIntensity, S.Bloom, S.Weight);
        PP.VignetteIntensity = FMath::Lerp(PP.VignetteIntensity, S.Vignette, S.Weight);
        PP.bOverride_BloomIntensity = true;
        PP.bOverride_VignetteIntensity = true;
        DesiredView.PostProcessBlendWeight = 1.f;
    }

    if (IsXRHeadTrackedCamera())
    {
        Super::GetCameraView(DeltaTime, DesiredView);
    }
}

void UFighterCameraComponent::UpdateCameraModes()
{
    check(CameraModeStack);

    if (CameraModeStack->IsStackActivate())
    {
        if (DetermineCameraModeDelegate.IsBound())
        {
            if (const TSubclassOf<UFighterCameraMode> CameraMode = DetermineCameraModeDelegate.Execute())
            {
                CameraModeStack->PushCameraMode(CameraMode);
            }
        }
    }
}

void UFighterCameraComponent::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
    check(CameraModeStack);
    CameraModeStack->GetBlendInfo(OutWeightOfTopLayer, OutTagOfTopLayer);
}
