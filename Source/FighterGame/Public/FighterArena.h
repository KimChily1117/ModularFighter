// 경기 전체를 관리하는 액터.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FighterExperienceDefinition.h"
#include "FighterInputBridge.h"
#include "FighterMoveCatalog.h"
#include "FighterNetSession.h"
#include "modular_fighter/cpu.hpp"
#include "GameplayCueInterface.h"
#include "modular_fighter/combat.hpp"
#include "modular_fighter/input.hpp"
#include "modular_fighter/xinput_device.hpp"
#include <array>
#include <memory>
#include "FighterArena.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class AFighterAbilityHost;
class UFighterReferenceMotionComponent;
class UFighterNetSession;
class UFighterCharacterMeshComponent;
class UFighterInputSampler;
class UFighterCameraComponent;
class UFighterCameraMode;
class UFighterTrainingComponent;
class UFighterSmokeComponent;

UCLASS()

class FIGHTERGAME_API AFighterArena : public AActor
{
    GENERATED_BODY()
public:
    AFighterArena();
    virtual ~AFighterArena() override;
    virtual void PreInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    void SetSessionSettings(const FFighterSessionSettings& Settings, FName ExperienceName,
                            const TArray<TObjectPtr<UFighterPawnData>>& InFighters);

    const modular_fighter::State& GetCombatState() const
    {
        return CombatState;
    }

    const std::array<modular_fighter::input::FrameInput, 2>& GetFrameInputs() const
    {
        return FrameInputs;
    }

    FName GetExperienceName() const
    {
        return ExperienceName;
    }

    const UFighterPawnData* GetPawnData(int32 Index) const;

    int32 GetControlledFighter() const
    {
        return bControlPaul ? 1 : 0;
    }

    bool UsesKeyboard() const
    {
        return bKeyboard;
    }

    bool IsPadConnected(int32 Index) const
    {
        return PadConnected[Index == 1 ? 1 : 0];
    }

    bool IsTrainingPaused() const
    {
        return bPaused;
    }

    bool HasInputFocus() const
    {
        return bInputFocus;
    }

    bool ShowDiagnostics() const
    {
        return bDiagnostics;
    }

    bool ShowTrainingPanel() const
    {
        return bTrainingPanel;
    }

    bool ShowHitGeometry() const
    {
        return bHitGeometry;
    }

    bool HasStepFailure() const
    {
        return bStepFailure;
    }

    double GetDroppedWallSeconds() const
    {
        return DroppedWallSeconds;
    }

    bool IsReferencePreview() const
    {
        return bReferencePreview;
    }

    bool IsSyntheticTest() const;

    FString GetAttackLabel(int32 Index) const;
    FString GetCharacterLabel(int32 Index) const;
    FString GetReferenceLabel(int32 Index) const;
    bool HasCharacterMeshes() const;

    bool GetHeatCutIn(FVector& OutEye, FVector& OutLook, float& OutFov, float& OutProgress) const;

    const AFighterAbilityHost* GetAbilityHost(int32 Index) const;
    const UFighterReferenceMotionComponent* GetReferenceMotion(int32 Index) const;
    const UFighterCharacterMeshComponent* GetCharacterMesh(int32 Index) const;

    const FFighterMoveCatalog& GetMoveCatalog() const
    {
        return MoveCatalog;
    }

    UFighterInputSampler* GetInputSampler() const
    {
        return InputSampler;
    }

    class UFighterImpactComponent* GetImpactPresentation() const
    {
        return Impact;
    }

    class UFighterStageComponent* GetStagePresentation() const
    {
        return Stage;
    }

    const UFighterTrainingComponent* GetTraining() const
    {
        return Training;
    }

    const UFighterNetSession* GetNet() const
    {
        return Net;
    }

    bool IsOnline() const;

    bool IsViewFlipped() const;

    float GetViewAxisSign() const
    {
        return IsViewFlipped() ? -1.f : 1.f;
    }

    int32 GetViewSide(int32 FaceCos, int32 FaceSin) const;

    int32 GetLobbyCursor() const
    {
        return LobbyCursor;
    }

    class UTexture* GetOpeningTexture() const
    {
        return OpeningTexture;
    }

    TArray<EFighterLobbyAction> GetResultMenu() const;

    enum class EPracticePhase : uint8
    {
        Off,
        Select,
        GetReady,
        Training,
        Matched,
        MainMenu,
        StageSelect,
        Searching,
        Opening,
        Title
    };

    EPracticePhase GetPracticePhase() const
    {
        return PracticePhase;
    }

    int32 GetPracticeStep() const
    {
        return PracticeStep;
    }

    int32 GetMenuCursor() const
    {
        return MenuCursor;
    }

    uint8 GetStageChoice() const
    {
        return StageChoice;
    }

    bool IsPauseMenuOpen() const
    {
        return bPauseMenu;
    }

    bool IsVsStageUrban() const;

    modular_fighter::CharacterId GetPracticePick(int32 Index) const
    {
        return PracticePick[Index];
    }

    float GetPracticeSeconds() const;
    bool IsPracticeMatching() const;

    enum class EMatchLoad : uint8
    {
        None,
        Requested,
        Warmup,
        WaitingPeer
    };

    EMatchLoad GetMatchLoad() const
    {
        return MatchLoad;
    }

    void RunLobbyAction(EFighterLobbyAction Action)
    {
        DoLobbyAction(Action);
    }

private:
    void BuildTrainingRoom();
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UFighterImpactComponent> Impact;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UFighterStageComponent> Stage;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UFighterAudioComponent> Audio;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<class UFighterCinematicEffectsComponent> CinematicEffects;
    UStaticMeshComponent* AddShape(USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh,
                                   const FVector& Location, const FVector& Scale, const FLinearColor& Color);
    void SpawnFighterPresentation();
    void InitializeInput();
    TSubclassOf<UFighterCameraMode> DetermineCameraMode() const;
    bool InputHasFocus() const;
    void HandleHostCommands();
    void ReadLiveInputs();
    void AdvanceFrame();
    void DetectHitReactions(const modular_fighter::State& Before);
    void RaiseCombatCues(const modular_fighter::State& Before);
    void OnCombatCue(const FGameplayTag& CueTag, const FGameplayCueParameters& Parameters);
    void UpdatePresentation();
    void ResetTraining();
    void TickOnline();
    void ApplySideViewProbe(modular_fighter::input::FrameInput& Pressed);
    void HandleLobbyInput();
    void HandleLobbyClick();
    void UpdateLobbyCursor();
    void TickPractice();
    void TickPracticeMatching();
    void BeginGetReady(EPracticePhase Next);
    void PlayNameCall(modular_fighter::CharacterId Character);
    void TickDemoCombo(std::array<modular_fighter::Input, 2>& Inputs);
    void ShowBeam(int32 Index, const FVector& Start, const FVector& End, int32 Age);
    bool bSourceBeamCaptured = false;
    void PrepareRenderAssets();
    void TickMatchLoading();
    void DoLobbyAction(EFighterLobbyAction Action);
    bool DoMenuAction(EFighterLobbyAction Action);
    void OnPausePressed();
    void SetPauseMenu(bool bOpen);
    void EnterOpening();
    void EnterTitle();
    void TickStartFlow(bool bAnyButton);
    void EnterMainMenu();
    void ConfirmStageChoice();
    void GoToStageSelectOrReady();
    void TickVsLoading();
    bool IsPresentationSettled(modular_fighter::CharacterId Left, modular_fighter::CharacterId Right,
                               bool bUrban) const;
    void TickMainMenu(const modular_fighter::input::FrameInput& In, bool bConfirm, bool bBack);
    void TickPauseMenu();
    void UpdateMenuMouse();
    void ApplyMatchCharacters(modular_fighter::CharacterId Left, modular_fighter::CharacterId Right);
    UFighterPawnData* PawnDataFor(modular_fighter::CharacterId Character) const;
    modular_fighter::CharacterId SlotCharacter(int32 Index) const;

    modular_fighter::State CombatState;
    std::array<modular_fighter::input::FrameInput, 2> FrameInputs{};
    std::array<FighterInputBridge::FAttackEdgeLatch, 2> AttackLatches{};
    std::array<bool, 2> SmokeNoGuard{};
    std::unique_ptr<modular_fighter::input::XInputDevice> Device;
    std::array<modular_fighter::input::DeviceSample, 2> PadSamples{};
    std::array<double, 2> NextPadPollTime{};
    bool PadConnected[2] = {false, false};
    bool bKeyboard = true;
    bool bControlPaul = false;
    bool bPaused = false;
    bool bInputFocus = false;
    bool bPreviousMenu = false;
    bool bDiagnostics = false;
    bool bTrainingPanel = false;
    bool bHitGeometry = false;
    void DrawHitGeometry() const;
    bool bStepFailure = false;
    double Accumulator = 0;
    double DroppedWallSeconds = 0;
    bool bReferencePreview = false;
    double ReferencePreviewSeconds = 0;
    double PreviewWarmupSeconds = 0;

    FFighterSessionSettings Session;
    FName ExperienceName = NAME_None;
    UPROPERTY()
    TArray<TObjectPtr<UFighterPawnData>> FighterPawnData;
    FFighterMoveCatalog MoveCatalog;

    FName HitReactClip[2] = {NAME_None, NAME_None};
    double RageSince[2] = {-1.0, -1.0};
    uint32 HitReactStart[2] = {0, 0};
    bool bCrouchShown[2] = {false, false};
    int32 CrouchChangeFrame[2] = {-1000, -1000};

    float CueKickDegrees = 0.f;
    double CueKickTime = -1.0;
    double HeatCueTime = -1.0;
    static constexpr float HeatCutSeconds = 0.47f;
    double HeatCutStart = -1.0;

    float HeatCutElapsed = 0.f;
    int32 HeatCutHiddenFighter = -1;
    int32 HeatCutFighter = 0;
    TArray<uint64> HeatCueHistory;
    TArray<uint64> PaulWindHistory;

    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFighterInputSampler> InputSampler;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFighterCameraComponent> CameraComponent;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFighterTrainingComponent> Training;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFighterSmokeComponent> Smoke;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UFighterNetSession> Net;
    UPROPERTY()
    TArray<TObjectPtr<UFighterPawnData>> AvailablePawnData;
    TSet<USceneComponent*> TrainingRoomShapes;
    int32 LobbyCursor = 0;
    EPracticePhase PracticePhase = EPracticePhase::Off;
    int32 MenuCursor = 0;
    int32 LastMenuTile = 0;
    uint8 StageChoice = 0;
    bool bPauseMenu = false;

    enum class EVsLoad : uint8
    {
        Art,
        Intro,
        Warmup,
        Done
    };
    EVsLoad VsLoad = EVsLoad::Done;
    double VsLoadStartedAt = 0.0;
    int32 VsWarmupFrames = 0;
    double MatchWarmupStartedAt = 0.0;
    bool bMenuBackPressed = false;
    bool bFromMainMenu = false;
    UPROPERTY()
    TObjectPtr<class UMediaPlayer> OpeningPlayer;
    UPROPERTY()
    TObjectPtr<class UTexture> OpeningTexture;
    bool bOpeningPlaying = false;
    bool bAutoPauseDone = false;
    int8 PreviousMenuHorizontal = 0;
    int8 PreviousMenuVertical = 0;
    FVector2D LastMenuMouse = FVector2D(-1, -1);
    modular_fighter::input::FrameInput PreviousPauseInput{};
    EPracticePhase PracticeAfterReady = EPracticePhase::Training;
    int32 PracticeStep = 0;
    modular_fighter::CharacterId PracticePick[2] = {modular_fighter::CharacterId::Kazuya,
                                                    modular_fighter::CharacterId::Paul};
    double PracticePhaseStart = 0;
    double PracticeLobbyActionTime = 0;
    uint8 PracticeStage = 0;
    int32 PracticeIdleFrames[2] = {0, 0};
    TOptional<modular_fighter::CpuController> Cpu;
    uint32 CpuSeed = 1;
    static constexpr uint32 CpuReactionFrames = 12;
    double AudioRecordStart = -1.0;
    int32 RecordTotal = -1, RecordIndex = 0;
    TArray<FString> DemoSpecs;
    modular_fighter::CharacterId DemoCharacters[2] = {modular_fighter::CharacterId::Kazuya,
                                                      modular_fighter::CharacterId::Paul};
    std::vector<modular_fighter::RouteStep> DemoSteps;
    int32 DemoIndex = 0, DemoIdle = 0, DemoBest = 0;
    int32 ReviewCombo = 0;
    std::size_t DemoStep = 0, DemoCursor = 0;
    bool bDemoPlaying = false, bDemoApplied = false;
    UPROPERTY()
    TObjectPtr<class UStaticMeshComponent> BeamParts[6];
    bool bAudioRecordDone = false;
    EMatchLoad MatchLoad = EMatchLoad::None;
    modular_fighter::net::LoadStage LoadRequest;
    int32 LoadWarmupFrames = 0;
    UPROPERTY()
    TObjectPtr<class USoundBase> NameCalls[2];
    modular_fighter::input::FrameInput PreviousPracticeInput{};
    uint8 LastLobbyPhase = 255;
    double LobbyPhaseEnteredAt = 0.0;
    modular_fighter::input::FrameInput PreviousLobbyInput{};
    int8 PreviousLobbyVertical = 0;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> ShapeMaterial;
    UPROPERTY()
    TObjectPtr<UStaticMesh> CubeMesh;
    UPROPERTY()
    TObjectPtr<UStaticMesh> SphereMesh;
    UPROPERTY()
    TArray<TObjectPtr<USceneComponent>> FighterRoots;

    UPROPERTY()
    TObjectPtr<USceneComponent> CharacterLightRig;
    UPROPERTY()
    TArray<TObjectPtr<class USpotLightComponent>> CharacterLights;
    void UpdateCharacterLightRig();

    FVector BodySmoothOffset[2] = {FVector::ZeroVector, FVector::ZeroVector};
    FVector LastBodyPoint[2] = {FVector::ZeroVector, FVector::ZeroVector};
    uint8 LastBodyReaction[2] = {0, 0};
    uint32 LastBodyFrame[2] = {0, 0};
    bool bBodyTracked[2] = {false, false};
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Hands;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> Feet;
    UPROPERTY()
    TArray<TObjectPtr<AFighterAbilityHost>> AbilityHosts;
    UPROPERTY()
    TArray<TObjectPtr<UFighterReferenceMotionComponent>> ReferenceMotions;
    UPROPERTY()
    TArray<TObjectPtr<UFighterCharacterMeshComponent>> CharacterMeshes;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> FallbackShapes;
    TArray<int32> FallbackShapeOwners;
};
