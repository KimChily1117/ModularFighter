// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 카메라 모드와 블렌딩 스택.
#include "FighterCameraMode.h"
#include "FighterMotionFeel.h"
#include "FighterArena.h"
#include "FighterCameraComponent.h"
#include "FighterCharacterMeshComponent.h"
#include "FighterSequenceCameras.h"
#include "FighterParameterCameras.h"
#include "FighterCinematicFades.h"
#include "FighterCinematicSource.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterCameraMode)

FFighterCameraModeView::FFighterCameraModeView()
    : Location(ForceInit), Rotation(ForceInit), ControlRotation(ForceInit), FieldOfView(FIGHTER_CAMERA_DEFAULT_FOV)
{
}

void FFighterCameraModeView::Blend(const FFighterCameraModeView& Other, float OtherWeight)
{
    if (OtherWeight <= 0.0f)
    {
        return;
    }
    else if (OtherWeight >= 1.0f)
    {
        *this = Other;

        return;
    }

    Location = FMath::Lerp(Location, Other.Location, OtherWeight);

    const FRotator DeltaRotation = (Other.Rotation - Rotation).GetNormalized();
    Rotation = Rotation + (OtherWeight * DeltaRotation);

    const FRotator DeltaControlRotation = (Other.ControlRotation - ControlRotation).GetNormalized();
    ControlRotation = ControlRotation + (OtherWeight * DeltaControlRotation);

    FieldOfView = FMath::Lerp(FieldOfView, Other.FieldOfView, OtherWeight);
}

UFighterCameraMode::UFighterCameraMode()
{
    FieldOfView = FIGHTER_CAMERA_DEFAULT_FOV;
    ViewPitchMin = FIGHTER_CAMERA_DEFAULT_PITCH_MIN;
    ViewPitchMax = FIGHTER_CAMERA_DEFAULT_PITCH_MAX;

    BlendTime = 0.5f;
    BlendFunction = EFighterCameraModeBlendFunction::EaseOut;
    BlendExponent = 4.0f;
    BlendAlpha = 1.0f;
    BlendWeight = 1.0f;
}

UFighterCameraComponent* UFighterCameraMode::GetFighterCameraComponent() const
{
    return CastChecked<UFighterCameraComponent>(GetOuter());
}

UWorld* UFighterCameraMode::GetWorld() const
{
    return HasAnyFlags(RF_ClassDefaultObject) ? nullptr : GetOuter()->GetWorld();
}

AActor* UFighterCameraMode::GetTargetActor() const
{
    return GetFighterCameraComponent()->GetTargetActor();
}

FVector UFighterCameraMode::GetPivotLocation() const
{
    const AActor* TargetActor = GetTargetActor();
    check(TargetActor);

    return TargetActor->GetActorLocation();
}

FRotator UFighterCameraMode::GetPivotRotation() const
{
    const AActor* TargetActor = GetTargetActor();
    check(TargetActor);

    return TargetActor->GetActorRotation();
}

void UFighterCameraMode::UpdateCameraMode(float DeltaTime)
{
    UpdateView(DeltaTime);
    UpdateBlending(DeltaTime);
}

void UFighterCameraMode::UpdateView(float DeltaTime)
{
    FVector PivotLocation = GetPivotLocation();
    FRotator PivotRotation = GetPivotRotation();

    PivotRotation.Pitch = FMath::ClampAngle(PivotRotation.Pitch, ViewPitchMin, ViewPitchMax);

    View.Location = PivotLocation;
    View.Rotation = PivotRotation;
    View.ControlRotation = View.Rotation;
    View.FieldOfView = FieldOfView;
}

void UFighterCameraMode::SetBlendWeight(float Weight)
{
    BlendWeight = FMath::Clamp(Weight, 0.0f, 1.0f);

    const float InvExponent = (BlendExponent > 0.0f) ? (1.0f / BlendExponent) : 1.0f;

    switch (BlendFunction)
    {
    case EFighterCameraModeBlendFunction::Linear:
        BlendAlpha = BlendWeight;
        break;
    case EFighterCameraModeBlendFunction::EaseIn:
        BlendAlpha = FMath::InterpEaseIn(0.0f, 1.0f, BlendWeight, InvExponent);
        break;
    case EFighterCameraModeBlendFunction::EaseOut:
        BlendAlpha = FMath::InterpEaseOut(0.0f, 1.0f, BlendWeight, InvExponent);
        break;
    case EFighterCameraModeBlendFunction::EaseInOut:
        BlendAlpha = FMath::InterpEaseInOut(0.0f, 1.0f, BlendWeight, InvExponent);
        break;
    default:
        checkf(false, TEXT("SetBlendWeight: Invalid BlendFunction [%d]\n"), (uint8)BlendFunction);
        break;
    }
}

void UFighterCameraMode::UpdateBlending(float DeltaTime)
{
    if (!FighterMotionFeel::HardCut() && BlendTime > 0.0f)
    {
        BlendAlpha += (DeltaTime / BlendTime);
        BlendAlpha = FMath::Min(BlendAlpha, 1.0f);
    }
    else
    {
        BlendAlpha = 1.0f;
    }

    const float Exponent = (BlendExponent > 0.0f) ? BlendExponent : 1.0f;

    switch (BlendFunction)
    {
    case EFighterCameraModeBlendFunction::Linear:
        BlendWeight = BlendAlpha;
        break;
    case EFighterCameraModeBlendFunction::EaseIn:
        BlendWeight = FMath::InterpEaseIn(0.0f, 1.0f, BlendAlpha, Exponent);
        break;
    case EFighterCameraModeBlendFunction::EaseOut:
        BlendWeight = FMath::InterpEaseOut(0.0f, 1.0f, BlendAlpha, Exponent);
        break;
    case EFighterCameraModeBlendFunction::EaseInOut:
        BlendWeight = FMath::InterpEaseInOut(0.0f, 1.0f, BlendAlpha, Exponent);
        break;
    default:
        checkf(false, TEXT("UpdateBlending: Invalid BlendFunction [%d]\n"), (uint8)BlendFunction);
        break;
    }
}

UFighterCameraModeStack::UFighterCameraModeStack()
{
    bIsActive = true;
}

void UFighterCameraModeStack::ActivateStack()
{
    if (!bIsActive)
    {
        bIsActive = true;

        for (UFighterCameraMode* CameraMode : CameraModeStack)
        {
            check(CameraMode);
            CameraMode->OnActivation();
        }
    }
}

void UFighterCameraModeStack::DeactivateStack()
{
    if (bIsActive)
    {
        bIsActive = false;

        for (UFighterCameraMode* CameraMode : CameraModeStack)
        {
            check(CameraMode);
            CameraMode->OnDeactivation();
        }
    }
}

// 모드를 스택에 넣고 블렌딩을 시작한다.
void UFighterCameraModeStack::PushCameraMode(TSubclassOf<UFighterCameraMode> CameraModeClass)
{
    if (!CameraModeClass)
    {
        return;
    }

    UFighterCameraMode* CameraMode = GetCameraModeInstance(CameraModeClass);
    check(CameraMode);

    int32 StackSize = CameraModeStack.Num();

    if ((StackSize > 0) && (CameraModeStack[0] == CameraMode))
    {
        return;
    }

    int32 ExistingStackIndex = INDEX_NONE;
    float ExistingStackContribution = 1.0f;

    for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
    {
        if (CameraModeStack[StackIndex] == CameraMode)
        {
            ExistingStackIndex = StackIndex;
            ExistingStackContribution *= CameraMode->GetBlendWeight();
            break;
        }
        else
        {
            ExistingStackContribution *= (1.0f - CameraModeStack[StackIndex]->GetBlendWeight());
        }
    }

    if (ExistingStackIndex != INDEX_NONE)
    {
        CameraModeStack.RemoveAt(ExistingStackIndex);
        StackSize--;
    }
    else
    {
        ExistingStackContribution = 0.0f;
    }

    const bool bShouldBlend = (!FighterMotionFeel::HardCut() && (CameraMode->GetBlendTime() > 0.0f) && (StackSize > 0));
    const float BlendWeight = (bShouldBlend ? ExistingStackContribution : 1.0f);

    CameraMode->SetBlendWeight(BlendWeight);

    CameraModeStack.Insert(CameraMode, 0);

    CameraModeStack.Last()->SetBlendWeight(1.0f);

    if (ExistingStackIndex == INDEX_NONE)
    {
        CameraMode->OnActivation();
    }
}

bool UFighterCameraModeStack::EvaluateStack(float DeltaTime, FFighterCameraModeView& OutCameraModeView)
{
    if (!bIsActive)
    {
        return false;
    }

    UpdateStack(DeltaTime);
    BlendStack(OutCameraModeView);

    return true;
}

UFighterCameraMode* UFighterCameraModeStack::GetCameraModeInstance(TSubclassOf<UFighterCameraMode> CameraModeClass)
{
    check(CameraModeClass);

    for (UFighterCameraMode* CameraMode : CameraModeInstances)
    {
        if ((CameraMode != nullptr) && (CameraMode->GetClass() == CameraModeClass))
        {
            return CameraMode;
        }
    }

    UFighterCameraMode* NewCameraMode =
        NewObject<UFighterCameraMode>(GetOuter(), CameraModeClass, NAME_None, RF_NoFlags);
    check(NewCameraMode);

    CameraModeInstances.Add(NewCameraMode);

    return NewCameraMode;
}

// 각 모드를 갱신한다.
void UFighterCameraModeStack::UpdateStack(float DeltaTime)
{
    const int32 StackSize = CameraModeStack.Num();

    if (StackSize <= 0)
    {
        return;
    }

    int32 RemoveCount = 0;
    int32 RemoveIndex = INDEX_NONE;

    for (int32 StackIndex = 0; StackIndex < StackSize; ++StackIndex)
    {
        UFighterCameraMode* CameraMode = CameraModeStack[StackIndex];
        check(CameraMode);

        CameraMode->UpdateCameraMode(DeltaTime);

        if (CameraMode->GetBlendWeight() >= 1.0f)
        {
            RemoveIndex = (StackIndex + 1);
            RemoveCount = (StackSize - RemoveIndex);
            break;
        }
    }

    if (RemoveCount > 0)
    {
        for (int32 StackIndex = RemoveIndex; StackIndex < StackSize; ++StackIndex)
        {
            UFighterCameraMode* CameraMode = CameraModeStack[StackIndex];
            check(CameraMode);

            CameraMode->OnDeactivation();
        }

        CameraModeStack.RemoveAt(RemoveIndex, RemoveCount);
    }
}

// 스택의 모드들을 하나의 뷰로 합친다.
void UFighterCameraModeStack::BlendStack(FFighterCameraModeView& OutCameraModeView) const
{
    const int32 StackSize = CameraModeStack.Num();

    if (StackSize <= 0)
    {
        return;
    }

    const UFighterCameraMode* CameraMode = CameraModeStack[StackSize - 1];
    check(CameraMode);

    OutCameraModeView = CameraMode->GetCameraModeView();

    for (int32 StackIndex = (StackSize - 2); StackIndex >= 0; --StackIndex)
    {
        CameraMode = CameraModeStack[StackIndex];
        check(CameraMode);

        OutCameraModeView.Blend(CameraMode->GetCameraModeView(), CameraMode->GetBlendWeight());
    }
}

void UFighterCameraModeStack::GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const
{
    if (CameraModeStack.Num() == 0)
    {
        OutWeightOfTopLayer = 1.0f;
        OutTagOfTopLayer = FGameplayTag();

        return;
    }
    else
    {
        UFighterCameraMode* TopEntry = CameraModeStack.Last();
        check(TopEntry);
        OutWeightOfTopLayer = TopEntry->GetBlendWeight();
        OutTagOfTopLayer = TopEntry->GetCameraTypeTag();
    }
}

namespace
{
bool GSourceFadeActive = false;

void ApplySourceFade(const AFighterArena* Arena, modular_fighter::CommandId Command, bool Finish, int32 Side,
                     int32 Frame)
{
    auto* PC = Arena->GetWorld()->GetFirstPlayerController();

    if (!PC || !PC->PlayerCameraManager)
        return;
    const FFighterCinematicFade* Fade = nullptr;

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyCinematicFade")))
        for (const auto& F : FighterCinematicFades)
            if (F.Command == Command && F.Finish == Finish && F.Side == Side && !F.bKnockout)
            {
                Fade = &F;
                break;
            }
    float Alpha = 0.f;

    if (Fade && Fade->KeyCount > 0 && Frame >= Fade->Keys[0].Frame && Frame <= Fade->Keys[Fade->KeyCount - 1].Frame)
    {
        Alpha = Fade->Keys[Fade->KeyCount - 1].Alpha;

        for (int32 K = 0; K + 1 < Fade->KeyCount; ++K)
            if (Frame <= Fade->Keys[K + 1].Frame)
            {
                const float Span = FMath::Max(1.f, Fade->Keys[K + 1].Frame - Fade->Keys[K].Frame);
                Alpha = FMath::Lerp(Fade->Keys[K].Alpha, Fade->Keys[K + 1].Alpha, (Frame - Fade->Keys[K].Frame) / Span);
                break;
            }
    }

    if (Alpha > 0.f)
    {
        PC->PlayerCameraManager->SetManualCameraFade(Alpha, Fade->Color, false);

        if (!GSourceFadeActive)
            UE_LOG(
                LogTemp, Display,
                TEXT(
                    "FIGHTER_CINEMATIC_FADE command=%d side=%d frame=%d keys=%d color=%s source_keys=1 manager_adapter=1"),
                int(Command), Side, Frame, Fade->KeyCount, *Fade->Color.ToString());
        GSourceFadeActive = true;
    }
    else if (GSourceFadeActive)
    {
        PC->PlayerCameraManager->StopCameraFade();
        GSourceFadeActive = false;
    }
}

bool ApplySourceCinematic(const AFighterArena* Arena, FFighterCameraModeView& View)
{
    if (!Arena)
        return false;
    const auto& State = Arena->GetCombatState();

    for (int32 Player = 0; Player < 2; ++Player)
    {
        const auto& A = State.fighters[Player];
        const bool Finish = A.sequence_role == 1;

        if (A.sequence_role == 2)
            continue;
        const auto Command = Finish ? A.sequence_command : A.active_command;
        const int32 Frame = Finish ? A.sequence_frame : A.attack_frame;
        const FVector Forward(A.face_cos / 65536.f, A.face_sin / 65536.f, 0.f);
        const FVector Left(Forward.Y, -Forward.X, 0.f);

        const FVector BattleRight =
            FVector(State.view_cos / 65536.f, State.view_sin / 65536.f, 0.f) * Arena->GetViewAxisSign();
        const int32 Side = Arena->GetViewSide(A.face_cos, A.face_sin);
        const FSourceCinematicTrack* Track = nullptr;

        for (const auto& T : SourceCinematicTracks)
            if (T.Command == Command && T.Finish == Finish && T.Side == Side && Frame > 0 && Frame <= int32(T.Count))
            {
                Track = &T;
                break;
            }
        if (!Track)
            continue;
        ApplySourceFade(Arena, Command, Finish, Side, Frame);
        const auto& S = SourceCinematicSamples[Track->First + Frame - 1];

        const FVector Origin =
            FVector(A.position_x * .1f, A.position_z * .1f, 0.f) - Left * S.RootLeft - Forward * S.RootForward;
        FFighterCameraModeView SourceView;
        SourceView.Location = Origin + Left * S.X + Forward * S.Y + FVector(0, 0, S.Z);
        SourceView.Rotation = FRotator(S.Pitch, Forward.Rotation().Yaw - 90.f + S.Yaw, S.Roll);
        SourceView.ControlRotation = SourceView.Rotation;
        SourceView.FieldOfView = S.Fov;

        if (Finish && Command == modular_fighter::CommandId::PaulRageArt && S.Shot >= 2 && S.Shot <= 7 &&
            !FParse::Param(FCommandLine::Get(), TEXT("FighterRawCinematicFraming")))
        {
            if (const auto* Model = Arena->GetCharacterMesh(Player))
            {
                FVector Head, Chest, Hand;

                if (Model->GetBoneWorldLocation(TEXT("Head"), Head) &&
                    Model->GetBoneWorldLocation(TEXT("Spine2"), Chest) &&
                    Model->GetBoneWorldLocation(TEXT("R_Hand"), Hand))
                {
                    const FVector Target = S.Shot == 3   ? FMath::Lerp(Chest, Hand, .45f)
                                           : S.Shot == 4 ? FMath::Lerp(Chest, Head, .55f)
                                                         : FMath::Lerp(Chest, Head, .3f);
                    const float Minimum = S.Shot == 3 ? 145.f : S.Shot == 4 ? 170.f : S.Shot == 5 ? 280.f : 220.f;
                    FVector Away = SourceView.Location - Target;

                    if (Away.IsNearlyZero())
                        Away = -SourceView.Rotation.Vector();
                    SourceView.Location = Target + Away.GetSafeNormal() * FMath::Max(Away.Size(), Minimum);
                    SourceView.Rotation = (Target - SourceView.Location).Rotation();
                    SourceView.ControlRotation = SourceView.Rotation;
                    SourceView.FieldOfView = FMath::Max(SourceView.FieldOfView, 38.f);
                }
            }
        }

        const auto& B = State.fighters[1 - Player];
        const FVector Mid((A.position_x + B.position_x) * .05f, (A.position_z + B.position_z) * .05f, 0.f);
        const float Gap = FVector2D((A.position_x - B.position_x) * .1f, (A.position_z - B.position_z) * .1f).Size();
        const FVector Toward(-BattleRight.Y, BattleRight.X, 0.f);
        View.Location = Mid + Toward * FMath::Max(435.f, Gap * 1.35f + 210.f) + FVector(0, 0, 120.f + Gap * .08f);
        View.Rotation = (Mid + FVector(0, 0, 114.f) - View.Location).Rotation();
        View.ControlRotation = View.Rotation;
        View.FieldOfView = FIGHTER_CAMERA_DEFAULT_FOV;
        View.Blend(SourceView, 1.f - S.GameWeight);
        static uint64 LastLog = 0;
        const uint64 Key = (uint64(Command) << 32) | (uint64(Finish) << 24) | (uint64(Side) << 16) | S.Shot;

        if (Key != LastLog)
        {
            LastLog = Key;

            if (auto* PC = Arena->GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
                PC->PlayerCameraManager->SetGameCameraCutThisFrame();
            UE_LOG(
                LogTemp, Display,
                TEXT(
                    "FIGHTER_SOURCE_CAMERA command=%d finish=%d side=%d frame=%d shot=%d fov=%.3f game_weight=%.3f original_keys=1 root_adapter=1"),
                int(Command), Finish ? 1 : 0, Side, Frame, S.Shot, S.Fov, S.GameWeight);
        }

        if (FParse::Param(FCommandLine::Get(), TEXT("FighterCinematicCapture")))
        {
            static TSet<uint64> Captured;

            if (Finish && Command == modular_fighter::CommandId::KazuyaRageArt &&
                (Frame == 426 || Frame == 449 || Frame == 494 || Frame == 545 || Frame == 560 || Frame == 562 ||
                 Frame == 575))
            {
                const uint64 GroundKey =
                    (uint64(Command) << 32) | (uint64(Finish) << 24) | (uint64(Side) << 16) | (0x200 + Frame);

                if (!Captured.Contains(GroundKey))
                {
                    Captured.Add(GroundKey);
                    FScreenshotRequest::RequestScreenshot(
                        FPaths::ConvertRelativePathToFull(
                            FPaths::ProjectSavedDir() /
                            FString::Printf(TEXT("Automation/Cinematic-%d-ground-%dp-f%d.png"), int(Command), Side,
                                            Frame)),
                        false, false);
                }
            }

            const uint64 LetterKey = (uint64(Command) << 32) | (uint64(Finish) << 24) | (uint64(Side) << 16) | 0x100;

            if (Finish && Command == modular_fighter::CommandId::PaulRageArt && Frame >= 75 && Frame < 90 &&
                !Captured.Contains(LetterKey))
            {
                Captured.Add(LetterKey);
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ConvertRelativePathToFull(
                        FPaths::ProjectSavedDir() /
                        FString::Printf(TEXT("Automation/Cinematic-%d-letter-%dp.png"), int(Command), Side)),
                    false, false);
            }

            int32 CutStart = Frame, CutEnd = Frame;

            while (CutStart > 1 && SourceCinematicSamples[Track->First + CutStart - 2].Shot == S.Shot)
                --CutStart;

            while (CutEnd < int32(Track->Count) && SourceCinematicSamples[Track->First + CutEnd].Shot == S.Shot)
                ++CutEnd;

            if (Frame >= (CutStart + CutEnd) / 2 && !Captured.Contains(Key))
            {
                Captured.Add(Key);
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ConvertRelativePathToFull(
                        FPaths::ProjectSavedDir() / FString::Printf(TEXT("Automation/Cinematic-%d-%s-%dp-shot%d.png"),
                                                                    int(Command), Finish ? TEXT("finish") : TEXT("pre"),
                                                                    Side, S.Shot)),
                    false, false);
            }
        }

        return true;
    }

    if (GSourceFadeActive)
        if (auto* PC = Arena->GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        {
            PC->PlayerCameraManager->StopCameraFade();
            GSourceFadeActive = false;
        }
    return false;
}
}

UFighterCameraMode_Framing::UFighterCameraMode_Framing()
{
    BlendTime = 0.35f;
    BlendFunction = EFighterCameraModeBlendFunction::EaseOut;
}

const AFighterArena* UFighterCameraMode_Framing::GetArena() const
{
    return Cast<AFighterArena>(GetTargetActor());
}

// 두 캐릭터가 화면에 들어오도록 맞춘다.
void UFighterCameraMode_Framing::UpdateView(float DeltaTime)
{
    FVector2D Mid = FVector2D::ZeroVector;
    float Separation = 0.f;
    FVector2D Right(1, 0);
    GetFraming(Mid, Separation, Right);

    if (FighterMotionFeel::HardCut() || SmoothedRight.IsNearlyZero() ||
        FVector2D::DotProduct(SmoothedRight, Right) < 0.f || AxisLagSeconds <= 0.f)
        SmoothedRight = Right;
    else
    {
        const float Follow = 1.f - FMath::Exp(-FMath::Max(DeltaTime, 0.f) / AxisLagSeconds);
        SmoothedRight = (SmoothedRight + (Right - SmoothedRight) * Follow).GetSafeNormal();
    }

    if (FighterMotionFeel::Trace())
        if (const auto* Arena = GetArena())
            UE_LOG(
                LogTemp, Display,
                TEXT("FIGHTER_MOTION_CAMERA frame=%u lag_seconds=%.3f axis_error_deg=%.5f metric=atan2"),
                Arena->GetCombatState().frame, FighterMotionFeel::HardCut() ? 0.f : AxisLagSeconds,

                FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(Right.X * SmoothedRight.Y - Right.Y * SmoothedRight.X),
                                                     FVector2D::DotProduct(Right, SmoothedRight))));
    Right = SmoothedRight;

    const FVector2D Toward(-Right.Y, Right.X);

    const FVector Target(Mid.X, Mid.Y, TargetHeight);
    const float Back = FMath::Max(MinDistance, Separation * DistancePerSeparation + DistanceOffset);
    const FVector Location(Mid.X + Toward.X * Back, Mid.Y + Toward.Y * Back,
                           BaseHeight + Separation * HeightPerSeparation);
    View.Location = Location;
    View.Rotation = (Target - Location).Rotation();
    View.Rotation.Pitch = FMath::ClampAngle(View.Rotation.Pitch, ViewPitchMin, ViewPitchMax);
    View.ControlRotation = View.Rotation;
    View.FieldOfView = FieldOfView;

    FVector Eye, Look;
    float Fov = 0.f, Progress = 0.f;

    if (const AFighterArena* Arena = GetArena(); Arena && Arena->GetHeatCutIn(Eye, Look, Fov, Progress))
    {
        View.Location = Eye;
        View.Rotation = (Look - Eye).Rotation();
        View.ControlRotation = View.Rotation;
        View.FieldOfView = Fov;
    }

    ApplySourceCinematic(GetArena(), View);
}

UFighterCameraMode_Battle::UFighterCameraMode_Battle()
{
    AxisLagSeconds = FighterMotionFeel::AxisLag();

    MinDistance = 435.f;
    DistanceOffset = 210.f;
    BaseHeight = 120.f;
    TargetHeight = 114.f;
}

UFighterCameraMode_Paired::UFighterCameraMode_Paired()
{
    AxisLagSeconds = 0.3f;
    MinDistance = 680.f;
    BaseHeight = 150.f;
    TargetHeight = 100.f;
    BlendTime = 0.2f;
}

int32 UFighterCameraMode_Paired::GetIntroHiddenOpponent(const AFighterArena* Arena)
{
    if (!Arena)
        return INDEX_NONE;
    const auto& State = Arena->GetCombatState();

    for (int32 Player = 0; Player < 2; ++Player)
    {
        const auto& A = State.fighters[Player];

        if (A.sequence_role == 1 && A.sequence_command == modular_fighter::CommandId::PaulRageArt &&
            !FParse::Param(FCommandLine::Get(), TEXT("FighterRawCinematicFraming")) &&
            ((A.sequence_frame >= 16 && A.sequence_frame < 315) || (A.sequence_frame >= 360 && A.sequence_frame < 415)))
            return 1 - Player;

        if (A.sequence_role || !A.attack_frame)
            continue;

        for (const auto& Track : SourceCinematicTracks)
            if (!Track.Finish && Track.Side == 1 && Track.Command == A.active_command &&
                A.attack_frame <= Track.Count &&
                SourceCinematicSamples[Track.First + A.attack_frame - 1].GameWeight < .999f)
                return 1 - Player;
    }

    return INDEX_NONE;
}

namespace
{
const FFighterSequenceCamera* FindSequenceCamera(modular_fighter::CommandId Command)
{
    for (const FFighterSequenceCamera& Row : FighterSequenceCameras)
    {
        if (Row.Command == Command && Command != modular_fighter::CommandId::None)
            return &Row;
    }

    return nullptr;
}
}

namespace
{
const FFighterParameterCamera* FindParameterCamera(int32 Id, int32 Side)
{
    const FFighterParameterCamera* Any = nullptr;

    for (const auto& Row : FighterParameterCameras)
    {
        if (Row.Id != Id)
            continue;

        if (Row.Side == Side || Row.Side == 3)
            return &Row;

        if (!Any)
            Any = &Row;
    }

    return Any;
}

bool ApplyParameterCamera(const AFighterArena* Arena, const FFighterSequenceCamera& Row, int32 Player,
                          FFighterCameraModeView& View, float& BlendTime)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyPairedCamera")) || !Row.EntryValue)
        return false;
    const auto& State = Arena->GetCombatState();
    const auto& A = State.fighters[Player];
    const int32 Frame = static_cast<int32>(A.sequence_frame);
    int32 Id = Row.EntryValue, StartFrame = 1;

    for (int32 Index = 0; Index < Row.PulseCount; ++Index)
        if (Row.Pulses[Index].Frame <= Frame)
        {
            Id = Row.Pulses[Index].Value;
            StartFrame = Row.Pulses[Index].Frame;
        }
    if (Id == 0)
    {
        BlendTime = 0.f;

        return true;
    }

    const int32 Side = Arena->GetViewSide(A.face_cos, A.face_sin);
    const FFighterParameterCamera* Preset = FindParameterCamera(Id, Side);

    if (!Preset)
        return false;
    const FVector Forward(A.face_cos / 65536.f, A.face_sin / 65536.f, 0.f);
    const FVector Left(-Forward.Y, Forward.X, 0.f);
    const float Elapsed = static_cast<float>(FMath::Max(0, Frame - StartFrame));
    const float T = Preset->Duration > 0 ? FMath::Clamp(Elapsed / Preset->Duration, 0.f, 1.f) : 1.f;
    const FFighterParameterCameraBlock& S = Preset->Start.bPresent ? Preset->Start : Preset->End;
    const FFighterParameterCameraBlock& E = Preset->End.bPresent ? Preset->End : Preset->Start;
    auto Anchor = [&](const TCHAR* Bone)
    {
        FVector Out(A.position_x * .1f, A.position_z * .1f, 90.f);

        if (const auto* Model = Arena->GetCharacterMesh(Player))
        {
            const FName Name = FCString::Stricmp(Bone, TEXT("Head")) == 0 ? FName(TEXT("Head")) : FName(TEXT("Hip"));
            FVector Found;

            if (Model->GetBoneWorldLocation(Name, Found))
                Out = Found;
        }

        return Out;
    };
    auto Point = [&](const FFighterParameterCameraBlock& B, FVector& OutTarget, FVector& OutLocation, float& OutFov)
    {
        OutTarget = Anchor(B.Bone) + Forward * (B.Offset[0] * .1f) + Left * (B.Offset[2] * .1f) +
                    FVector(0, 0, B.Offset[1] * .1f);
        const float Yaw = FMath::DegreesToRadians(B.Rotation[1]), Pitch = FMath::DegreesToRadians(B.Rotation[2]);
        const FVector Bearing = Forward * FMath::Cos(Yaw) + Left * FMath::Sin(Yaw);
        OutLocation = OutTarget + Bearing * (B.Distance * .1f * FMath::Cos(Pitch)) +
                      FVector(0, 0, -B.Distance * .1f * FMath::Sin(Pitch));
        OutFov = B.Fov;
    };
    FVector T0, L0, T1, L1;
    float F0, F1;
    Point(S, T0, L0, F0);
    Point(E, T1, L1, F1);
    FFighterCameraModeView Preset_View;
    Preset_View.Location = FMath::Lerp(L0, L1, T);
    Preset_View.Location.Z = FMath::Max(Preset_View.Location.Z, 25.f);
    const FVector LookTarget = FMath::Lerp(T0, T1, T);
    Preset_View.Rotation = (LookTarget - Preset_View.Location).Rotation();
    Preset_View.ControlRotation = Preset_View.Rotation;
    Preset_View.FieldOfView = FMath::Lerp(F0, F1, T);

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterRawParameterCamera")))
    {
        TArray<FVector> Points;
        FBox Bounds(ForceInit);

        for (int32 P = 0; P < 2; ++P)
            if (const auto* Model = Arena->GetCharacterMesh(P))
                for (const TCHAR* Bone :
                     {TEXT("Head"), TEXT("Hip"), TEXT("R_Hand"), TEXT("L_Hand"), TEXT("R_Foot"), TEXT("L_Foot")})
                {
                    FVector Position;

                    if (Model->GetBoneWorldLocation(Bone, Position))
                    {
                        Points.Add(Position);
                        Bounds += Position;
                    }
                }
        if (Points.Num() > 0)
        {
            const FVector Center = Bounds.GetCenter();
            const FRotationMatrix Basis(Preset_View.Rotation);
            const FVector D = Basis.GetUnitAxis(EAxis::X), R = Basis.GetUnitAxis(EAxis::Y),
                          U = Basis.GetUnitAxis(EAxis::Z);
            const float TanH = FMath::Tan(FMath::DegreesToRadians(Preset_View.FieldOfView * .5f));
            const float TanV = TanH / (16.f / 9.f);
            float Distance = FVector::Distance(Preset_View.Location, LookTarget);

            for (const FVector& BodyPoint : Points)
            {
                const FVector V = BodyPoint - Center;
                Distance = FMath::Max(Distance, (FMath::Abs(FVector::DotProduct(V, R)) + 20.f) / (TanH * .9f) -
                                                    FVector::DotProduct(V, D));
                Distance = FMath::Max(Distance, (FMath::Abs(FVector::DotProduct(V, U)) + 20.f) / (TanV * .72f) -
                                                    FVector::DotProduct(V, D));
            }

            Preset_View.Location = Center - D * Distance;

            for (int32 Iteration = 0; Iteration < 4 && Preset_View.Location.Z < 25.f; ++Iteration)
            {
                Preset_View.Location.Z = 25.f;
                Preset_View.Rotation = (Center - Preset_View.Location).Rotation();
                const FRotationMatrix FloorBasis(Preset_View.Rotation);
                const FVector FD = FloorBasis.GetUnitAxis(EAxis::X), FR = FloorBasis.GetUnitAxis(EAxis::Y),
                              FU = FloorBasis.GetUnitAxis(EAxis::Z);
                float FitDistance = FVector::Distance(Center, Preset_View.Location);

                for (const FVector& BodyPoint : Points)
                {
                    const FVector V = BodyPoint - Center;
                    FitDistance =
                        FMath::Max(FitDistance, (FMath::Abs(FVector::DotProduct(V, FR)) + 20.f) / (TanH * .9f) -
                                                    FVector::DotProduct(V, FD));
                    FitDistance =
                        FMath::Max(FitDistance, (FMath::Abs(FVector::DotProduct(V, FU)) + 20.f) / (TanV * .72f) -
                                                    FVector::DotProduct(V, FD));
                }

                Preset_View.Location = Center - FD * FitDistance;
            }

            Preset_View.Location.Z = FMath::Max(Preset_View.Location.Z, 25.f);
            Preset_View.Rotation = (Center - Preset_View.Location).Rotation();
            Preset_View.ControlRotation = Preset_View.Rotation;
        }
    }

    const float In = Preset->BlendIn > 0 ? FMath::Clamp(Elapsed / Preset->BlendIn, 0.f, 1.f) : 1.f;
    const float Out =
        Preset->Duration > 0 && Elapsed > Preset->Duration
            ? (Preset->BlendOut > 0 ? FMath::Clamp(1.f - (Elapsed - Preset->Duration) / Preset->BlendOut, 0.f, 1.f)
                                    : 0.f)
            : 1.f;
    View.Blend(Preset_View, In * Out);
    BlendTime = 0.f;
    static uint64 LastLog = 0;
    const uint64 Key = (uint64(A.sequence_command) << 32) | (uint64(Id) << 8) | Side;

    if (Key != LastLog)
    {
        LastLog = Key;

        if (In >= 1.f || Preset->BlendIn == 0)
            if (auto* PC = Arena->GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
                PC->PlayerCameraManager->SetGameCameraCutThisFrame();
        UE_LOG(
            LogTemp, Display,
            TEXT(
                "FIGHTER_PARAMETER_CAMERA command=%d preset=%d side=%d frame=%d duration=%d blend_in=%d loc=%s rot=%s fov=%.1f inferred_units=1"),
            int(A.sequence_command), Id, Side, Frame, Preset->Duration, Preset->BlendIn,
            *Preset_View.Location.ToString(), *Preset_View.Rotation.ToString(), Preset_View.FieldOfView);
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterCinematicCapture")))
    {
        static TSet<uint64> Captured;

        for (int32 Age : {5, 15, 30, 60})
        {
            const uint64 CaptureKey = (Key << 8) | Age;

            if (Elapsed == Age && !Captured.Contains(CaptureKey))
            {
                Captured.Add(CaptureKey);
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
                                                      FString::Printf(TEXT("Automation/Parameter-%d-%d-%dp-age%d.png"),
                                                                      int(A.sequence_command), Id, Side, Age)),
                    false, false);
            }
        }
    }

    return true;
}
}

// 잡기와 필살기의 연출 카메라를 계산한다.
void UFighterCameraMode_Paired::UpdateView(float DeltaTime)
{
    Super::UpdateView(DeltaTime);
    BlendTime = 0.2f;
    const AFighterArena* Arena = GetArena();

    if (!Arena)
        return;

    if (ApplySourceCinematic(Arena, View))
    {
        BlendTime = 0.f;

        return;
    }

    const auto& State = Arena->GetCombatState();
    const int32 Player = State.fighters[0].sequence_role == 1 ? 0 : 1;
    const auto& A = State.fighters[Player];
    const FFighterSequenceCamera* Row = A.sequence_role ? FindSequenceCamera(A.sequence_command) : nullptr;

    if (!Row)
        return;

    if (ApplyParameterCamera(Arena, *Row, Player, View, BlendTime))
        return;
    FVector2D Mid = FVector2D::ZeroVector, Right(1, 0);
    float Separation = 0.f;
    GetFraming(Mid, Separation, Right);
    const FVector Target(Mid.X, Mid.Y, TargetHeight);
    const float Facing = (A.facing >= 0 ? 1.f : -1.f) * Arena->GetViewAxisSign();

    if (Row->ShotCount)
    {
        const uint32 Start = State.frame - A.sequence_frame;
        uint32 Total = 0;

        for (int32 Index = 0; Index < Row->ShotCount; ++Index)
            Total += FMath::Max<uint32>(1, Row->Shots[Index].Weight);
        uint32 Pick = Start % Total;
        int32 ShotIndex = 0;

        while (ShotIndex + 1 < Row->ShotCount && Pick >= FMath::Max<uint32>(1, Row->Shots[ShotIndex].Weight))
        {
            Pick -= FMath::Max<uint32>(1, Row->Shots[ShotIndex].Weight);
            ++ShotIndex;
        }

        const FFighterSequenceShot& Shot = Row->Shots[ShotIndex];

        const uint16 Value = Facing > 0 ? Shot.Left : Shot.Right;
        const int32 Z = Value & 0xF, Y = (Value >> 4) & 0xF, Group = FMath::Min(Z >> 2, 2), Side = FMath::Min(Z & 3, 2);

        static constexpr float SideYaw[3] = {-55.f, 0.f, 55.f};
        static constexpr float GroupPitch[3] = {-4.f, -12.f, -28.f};
        static constexpr float GroupDistance[3] = {430.f, 470.f, 520.f};
        BlendTime = (Y & 1) ? 0.f : 0.35f;
        const float Yaw = (SideYaw[Side] + static_cast<float>(Shot.Rotation)) * Facing;
        const FVector2D Toward = FVector2D(-Right.Y, Right.X).GetRotated(Yaw);
        const float Distance = GroupDistance[Group] * ((Value >> 8) >= 9 ? 1.25f : 1.f);
        const float Pitch = FMath::DegreesToRadians(GroupPitch[Group]);
        const FVector Location = Target + FVector(Toward.X, Toward.Y, 0.f) * (Distance * FMath::Cos(Pitch)) +
                                 FVector(0.f, 0.f, -Distance * FMath::Sin(Pitch));
        View.Location = Location;
        View.Rotation = (Target - Location).Rotation();
        View.ControlRotation = View.Rotation;
    }

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterUnverifiedSequenceShake")))
        return;
    const FFighterSequencePulse* Pulse = nullptr;

    for (int32 Index = 0; Index < Row->PulseCount; ++Index)
    {
        if (Row->Pulses[Index].Frame <= A.sequence_frame)
            Pulse = &Row->Pulses[Index];
    }

    if (Pulse && Pulse->Value)
    {
        const float Age = static_cast<float>(A.sequence_frame - Pulse->Frame);
        const float Amplitude = Pulse->Value * 0.09f * FMath::Max(0.f, 1.f - Age / 18.f);
        const float Phase = static_cast<float>(State.frame);
        View.Location +=
            FVector(FMath::Sin(Phase * 1.9f), FMath::Cos(Phase * 2.7f), FMath::Sin(Phase * 3.3f + 1.f)) * Amplitude;
    }
}

void UFighterCameraMode_Battle::GetFraming(FVector2D& OutMid, float& OutSeparation, FVector2D& OutRight) const
{
    const AFighterArena* Arena = GetArena();

    if (!Arena)
        return;

    const auto& State = Arena->GetCombatState();
    const FVector2D P0(State.fighters[0].position_x * 0.1f, State.fighters[0].position_z * 0.1f);
    const FVector2D P1(State.fighters[1].position_x * 0.1f, State.fighters[1].position_z * 0.1f);
    OutMid = (P0 + P1) * 0.5f;
    OutSeparation = FVector2D::Distance(P0, P1);
    OutRight = FVector2D(State.view_cos / 65536.f, State.view_sin / 65536.f) * Arena->GetViewAxisSign();
}

void UFighterCameraMode_ReferencePreview::GetFraming(FVector2D& OutMid, float& OutSeparation, FVector2D& OutRight) const
{
    OutMid = FVector2D::ZeroVector;
    OutSeparation = 280.f;
    OutRight = FVector2D(1, 0);
}
