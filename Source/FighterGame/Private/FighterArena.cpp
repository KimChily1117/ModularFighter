// 경기 진행의 중심. 입력 수집, 코어 진행, 표시 갱신, 경기 흐름을 묶는다.
#include "FighterArena.h"
#include "Kismet/KismetSystemLibrary.h"
#include "ContentStreaming.h"
#include "ShaderPipelineCache.h"
#include "FighterMotionFeel.h"
#include "FighterImpactComponent.h"
#include "FighterImpactProfiles.h"
#include "FighterStageComponent.h"
#include "modular_fighter/cpu.hpp"
#include "FighterAudioComponent.h"
#include "FighterCinematicEffectsComponent.h"
#include "FighterInputBridge.h"
#include "FighterGameplayTags.h"
#include "FighterInputConfig.h"
#include "FighterInputSampler.h"
#include "FighterAbilityHost.h"
#include "FighterCameraComponent.h"
#include "FighterCameraMode.h"
#include "FighterCombatCues.h"
#include "FighterReferenceMotionComponent.h"
#include "FighterCharacterMeshComponent.h"
#include "FighterSmokeComponent.h"
#include "FighterTrainingComponent.h"
#include "FighterNetSession.h"
#include "FighterHUD.h"
#include "Components/DirectionalLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/GameViewportClient.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Sound/SoundBase.h"
#include "InputCoreTypes.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "Engine/Texture.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "MaterialShared.h"
#include "Materials/Material.h"
#include "UObject/UObjectIterator.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFighterArena, Log, All);
namespace Core = modular_fighter;
namespace Physical = modular_fighter::input;

static bool CommittedDevilPresentation(Core::CommandId Command)
{
#if defined(MF_FULL_MOVESET)
    return Command == Core::CommandId::KazuyaStringKzdvfumi || Command == Core::CommandId::KazuyaHeatDevilTwister;
#else
    return false;
#endif
}

AFighterArena::AFighterArena()
{
    PrimaryActorTick.bCanEverTick = true;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("ArenaRoot"));
    InputSampler = CreateDefaultSubobject<UFighterInputSampler>(TEXT("InputSampler"));
    Training = CreateDefaultSubobject<UFighterTrainingComponent>(TEXT("Training"));
    Smoke = CreateDefaultSubobject<UFighterSmokeComponent>(TEXT("Smoke"));
    Net = CreateDefaultSubobject<UFighterNetSession>(TEXT("NetSession"));

    CameraComponent = CreateDefaultSubobject<UFighterCameraComponent>(TEXT("Camera"));
    CameraComponent->SetupAttachment(RootComponent);
    CameraComponent->SetFieldOfView(FIGHTER_CAMERA_DEFAULT_FOV);
    CameraComponent->SetConstraintAspectRatio(false);
    CameraComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
    CameraComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    CameraComponent->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    CameraComponent->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    CameraComponent->PostProcessSettings.bOverride_AutoExposureBias = true;
    CameraComponent->PostProcessSettings.AutoExposureBias = .5f;
    CameraComponent->PostProcessSettings.bOverride_BloomIntensity = true;
    CameraComponent->PostProcessSettings.BloomIntensity = .25f;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh = Cube.Object;
    SphereMesh = Sphere.Object;
    ShapeMaterial = Material.Object;
    CombatState = Core::MakeInitialState();
    Impact = CreateDefaultSubobject<UFighterImpactComponent>(TEXT("ImpactPresentation"));
    Stage = CreateDefaultSubobject<UFighterStageComponent>(TEXT("StagePresentation"));
    Audio = CreateDefaultSubobject<UFighterAudioComponent>(TEXT("AudioPresentation"));
    CinematicEffects = CreateDefaultSubobject<UFighterCinematicEffectsComponent>(TEXT("CinematicEffects"));
}

AFighterArena::~AFighterArena() = default;

void AFighterArena::SetSessionSettings(const FFighterSessionSettings& Settings, FName InExperienceName,
                                       const TArray<TObjectPtr<UFighterPawnData>>& InFighters)
{
    Session = Settings;
    ExperienceName = InExperienceName;
    FighterPawnData = InFighters;
}

Core::CharacterId AFighterArena::SlotCharacter(int32 Index) const
{
    return FighterPawnData.IsValidIndex(Index) && FighterPawnData[Index] ? FighterPawnData[Index]->GetCoreCharacter()
                                                                         : Core::CharacterId::Training;
}

const UFighterPawnData* AFighterArena::GetPawnData(int32 Index) const
{
    return FighterPawnData.IsValidIndex(Index) ? FighterPawnData[Index].Get() : nullptr;
}

bool AFighterArena::IsSyntheticTest() const
{
    return Smoke && Smoke->IsSmokeRun();
}

void AFighterArena::PreInitializeComponents()
{
    Super::PreInitializeComponents();

    UGameFrameworkComponentManager::AddGameFrameworkComponentReceiver(this);
}

// 표시 컴포넌트를 준비하고 경기 시작 흐름에 들어간다.
void AFighterArena::BeginPlay()
{
    Super::BeginPlay();

    if (FighterPawnData.Num() != 2 || !FighterPawnData[0] || !FighterPawnData[1])
        UE_LOG(LogFighterArena, Error, TEXT("Experience %s does not name two fighters; using the Training fixtures"),
               *ExperienceName.ToString());
    CombatState = Core::MakeInitialState(SlotCharacter(0), SlotCharacter(1));
    Device = std::make_unique<Physical::XInputDevice>();
    const auto InitialPad = Device->Read(0);
    PadSamples[0] = InitialPad;
    bKeyboard = !InitialPad.raw.connected;

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterInputKeyboard")))
        bKeyboard = true;
    PadConnected[0] = InitialPad.raw.connected;
    bReferencePreview = Session.Mode == EFighterSessionMode::ReferencePreview ||
                        FParse::Param(FCommandLine::Get(), TEXT("FighterReferencePreview"));

    if (!Smoke->Configure(Session))
        return;

    CombatState.hitstop_enabled = FParse::Param(FCommandLine::Get(), TEXT("FighterHitstop")) ? 1 : 0;

    if (Smoke->IsCommandScenario())
    {
        CombatState.fighters[0].position_x = Smoke->IsContactScenario() ? -450 : -1800;
        CombatState.fighters[1].position_x = Smoke->IsContactScenario() ? 450 : 1800;
    }

    if (Smoke->IsJuggleScenario())
    {
        CombatState.fighters[0].position_x = -250;
        CombatState.fighters[1].position_x = 250;
    }

    if (Smoke->IsGroundScenario())
    {
        CombatState.fighters[0].position_x = -450;
        CombatState.fighters[1].position_x = 450;
        CombatState.fighters[0].reaction = Core::ReactionKind::Knockdown;
        CombatState.fighters[0].reaction_frame = 29;
    }

    int32 AuditStringScenario = 0;

    if (FParse::Value(FCommandLine::Get(), TEXT("FighterStringScenario="), AuditStringScenario) &&
        AuditStringScenario == 20)
    {
        CombatState.fighters[0].position_x = -900;
        CombatState.fighters[1].position_x = 900;
    }

    if (AuditStringScenario == 21 || AuditStringScenario == 22)
    {
        CombatState.fighters[0].position_x = -650;
        CombatState.fighters[1].position_x = 650;
        CombatState.fighters[1].heat_used = 1;
        CombatState.fighters[1].heat_frames = Core::HeatDurationFrames;
    }

    if (AuditStringScenario == 27)
    {
        CombatState.fighters[0].position_x = -650;
        CombatState.fighters[1].position_x = 650;
        CombatState.fighters[1].heat_used = 1;

        if (FParse::Param(FCommandLine::Get(), TEXT("FighterMirrorSystem")))
        {
            for (auto& F : CombatState.fighters)
            {
                F.position_x = -F.position_x;
                F.face_cos = -F.face_cos;
                F.facing = -F.facing;
            }
        }
    }

    if (AuditStringScenario == 23)
    {
        CombatState.fighters[0].position_x = -600;
        CombatState.fighters[1].position_x = 600;
        CombatState.fighters[1].heat_used = 1;
    }

    if (AuditStringScenario == 24)
    {
        CombatState.fighters[0].position_x = -550;
        CombatState.fighters[1].position_x = 550;
        CombatState.fighters[1].reaction = Core::ReactionKind::DownFaceUp;
        CombatState.fighters[1].reaction_frame = 1;
    }

    if (AuditStringScenario == 25 || AuditStringScenario == 26)
    {
        CombatState.fighters[0].position_x = -550;
        CombatState.fighters[1].position_x = 550;
        CombatState.fighters[0].heat_used = 1;
        CombatState.fighters[0].heat_frames = Core::HeatDurationFrames;
    }

    int32 CameraSystemScenario = 0;

    if (FParse::Value(FCommandLine::Get(), TEXT("FighterSystemScenario="), CameraSystemScenario) &&
        CameraSystemScenario && FParse::Param(FCommandLine::Get(), TEXT("FighterMirrorSystem")))
        for (auto& Fighter : CombatState.fighters)
        {
            Fighter.position_x = -Fighter.position_x;
            Fighter.position_z = -Fighter.position_z;
            Fighter.facing = -Fighter.facing;
            Fighter.face_cos = -Fighter.face_cos;
            Fighter.face_sin = -Fighter.face_sin;
        }
    NameCalls[0] = LoadObject<USoundBase>(nullptr, TEXT("/Game/LocalPresentation/Audio/Narration/NA_syvo_name_GRL"));
    NameCalls[1] = LoadObject<USoundBase>(nullptr, TEXT("/Game/LocalPresentation/Audio/Narration/NA_syvo_name_GRF"));
    {
        FString Demo;

        if (FParse::Value(FCommandLine::Get(), TEXT("FighterDemoCombos="), Demo))
        {
            const bool bPaul = Demo.Equals(TEXT("paul"), ESearchCase::IgnoreCase);
            const FString Tail = TEXT("HOLDB40 W0 SWAYDF KRP +1d W2 DF34 W0 MDF1 W0 FFF2");
            DemoSpecs =
                bPaul
                    ? TArray<
                          FString>{TEXT("W10 PDF2 W4 4 W2 DF4 W2 3 +2 HOLDB40 W4 SWAY12 W2 DF34 W0 MDF1 W0 FFF2"),
                                   TEXT(
                                       "W10 UF4 W2 3 +2 W4 2 W0 DASH2 3 +2 HOLDB40 W2 SWAYDF KRP +1d W8 DF34 W0 MDF1 W0 FFF2"),
                                   TEXT("W10 FF3 W2 2 W2 DF4 W2 2 W0 DASH0 3 +2 ") + Tail,
                                   TEXT("W10 QCF1 W2 4 W2 DF4 W2 2 W0 DASH2 3 +2 ") + Tail}
                    : TArray<FString>{TEXT("WS2 W13 QWS1 W2 B2 +2 W0 DF1 DF2 W0 SSU SS4 +1 W0 DASH6 DF1 +4"),
                                      TEXT("W10 EW W10 B2 +2 DF1 DF2 W20 SSU SS4 +1 DASH6 B2 +2 +12"),
                                      TEXT("W10 EW W0 SSD SS4 +1 W10 DF1 DF2 W20 SSU SS4 +1 DASH6 B2 +2 +12"),
                                      TEXT("W10 EW W2 EW W0 DASH4 B2 +2 W0 DF1 DF2 W24 SSU SS4 +1 W4 DASH6 B2 +2 +12"),
                                      TEXT("W10 EW W2 EW W2 FFEW W2 DASH4 DF1 DF2 W20 SSU SS4 +1 W2 DASH6 B2 +2 +12")};
            DemoCharacters[0] = bPaul ? Core::CharacterId::Paul : Core::CharacterId::Kazuya;
            DemoCharacters[1] = bPaul ? Core::CharacterId::Kazuya : Core::CharacterId::Paul;
        }
    }

    if (FParse::Value(FCommandLine::Get(), TEXT("FighterReviewCombo="), ReviewCombo) &&
        (ReviewCombo == 6 || ReviewCombo == 8 || ReviewCombo == 101 || ReviewCombo == 102 || ReviewCombo == 103))
    {
        DemoSpecs = {ReviewCombo == 101 ? TEXT("W10 PDF2 W4 4 W2 DF4 W2 3 +2 HOLDB40 W4 SWAY12 W2 DF34 W0 MDF1 W0 FFF2")
                     : ReviewCombo == 103 ? TEXT("W10 PDF2 W4 3 +2 HOLDB40 W4 SWAY12 W0 FFF2")
                     : ReviewCombo == 102 ? TEXT("W10 QCF1 W0 QCF1 W0 QCF1 W0 QCF1")
                     : ReviewCombo == 6
                         ? TEXT("W10 EW W0 SSD SS4 +1 W0 DASH4 DF3 W0 DF1 DF2 W24 SSD SS4 +1 W6 FFEW")
                         : TEXT("W10 EW W8 EW W0 DASH4 B2 +2 W0 DF1 DF2 W24 SSU SS4 +1 W4 DASH6 B2 +2 +12")};
        DemoCharacters[0] = ReviewCombo >= 100 ? Core::CharacterId::Paul : Core::CharacterId::Kazuya;
        DemoCharacters[1] = ReviewCombo >= 100 ? Core::CharacterId::Kazuya : Core::CharacterId::Paul;
    }

    {
        FString Server;

        if (FParse::Param(FCommandLine::Get(), TEXT("FighterPractice")) &&
            !FParse::Value(FCommandLine::Get(), TEXT("FighterServer="), Server))
        {
            PracticePhase = EPracticePhase::Select;
            PracticePhaseStart = GetWorld()->GetTimeSeconds();

            FString Picks;

            if (FParse::Value(FCommandLine::Get(), TEXT("FighterPracticePicks="), Picks, false))
            {
                FString A, B;

                if (Picks.Split(TEXT(","), &A, &B))
                {
                    PracticePick[0] = A.Equals(TEXT("paul"), ESearchCase::IgnoreCase) ? Core::CharacterId::Paul
                                                                                      : Core::CharacterId::Kazuya;
                    PracticePick[1] = B.Equals(TEXT("paul"), ESearchCase::IgnoreCase) ? Core::CharacterId::Paul
                                                                                      : Core::CharacterId::Kazuya;
                }
            }

            Audio->PlayMenuMusic(TEXT("character_select"));
        }

        const TCHAR* Line = FCommandLine::Get();

        if (PracticePhase == EPracticePhase::Off && !GIsAutomationTesting &&
            !FParse::Value(Line, TEXT("FighterServer="), Server) &&
            (FCString::Stristr(Line, TEXT("-Fighter")) == nullptr || FParse::Param(Line, TEXT("FighterMainMenu")) ||
             FParse::Param(Line, TEXT("FighterFlowCapture")) ||
             FCString::Stristr(Line, TEXT("-FighterMenuAuto=")) != nullptr))
            EnterOpening();
    }

    BuildTrainingRoom();
    InitializeInput();
    CameraComponent->DetermineCameraModeDelegate.BindUObject(this, &ThisClass::DetermineCameraMode);
    MoveCatalog.Load(FPaths::ProjectDir() / TEXT("LocalData/CharacterData/move-catalog.json"));
    AvailablePawnData = FighterPawnData;

    for (USceneComponent* Root : FighterRoots)
    {
        TArray<USceneComponent*> Attached;
        Root->GetChildrenComponents(true, Attached);

        for (USceneComponent* Child : Attached)
            TrainingRoomShapes.Add(Child);
    }

    SpawnFighterPresentation();
    Net->Start();
#if WITH_EDITOR

    if (Net->IsOnline())
        Stage->PreloadOtherStages();
#endif
    PrepareRenderAssets();

    if (const auto* Viewport = GetWorld()->GetGameViewport())
        UE_LOG(LogFighterArena, Display,
               TEXT("FIGHTER_RENDER flags materials=%d translucency=%d separate=%d lighting=%d"),
               int32(Viewport->EngineShowFlags.Materials), int32(Viewport->EngineShowFlags.Translucency),
               int32(Viewport->EngineShowFlags.SeparateTranslucency), int32(Viewport->EngineShowFlags.Lighting));
    UpdatePresentation();
    UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(
        this, UGameFrameworkComponentManager::NAME_GameActorReady);
    UE_LOG(LogFighterArena, Display,
           TEXT("FIGHTER_HOST ready: 60 Hz; source RP rules + four-strike fixtures; XInput=%s slot0=%s; smoke=%u"),
           UTF8_TO_TCHAR(Device->LibraryName().c_str()), PadConnected[0] ? TEXT("connected") : TEXT("disconnected"),
           Smoke->IsSmokeRun() ? 1u : 0u);
}

// 캐릭터 표시 객체를 만든다.
void AFighterArena::SpawnFighterPresentation()
{
    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto* Host = GetWorld()->SpawnActor<AFighterAbilityHost>();
        const UFighterPawnData* PawnData = GetPawnData(Index);
        Host->InitializeCharacter(CombatState.fighters[Index].character, PawnData);
        Host->Synchronize(CombatState.fighters[Index], CombatState.frame);
        Host->OnCombatCue.AddUObject(this, &ThisClass::OnCombatCue);
        AbilityHosts.Add(Host);
        const auto* Reference = PawnData ? MoveCatalog.FindByCharacter(PawnData->CharacterSlug) : nullptr;
        UFighterReferenceMotionComponent* Motion = nullptr;

        if (Reference)
        {
            TArray<USceneComponent*> ExistingShapes;
            FighterRoots[Index]->GetChildrenComponents(true, ExistingShapes);
            Motion = NewObject<UFighterReferenceMotionComponent>(this);
            AddInstanceComponent(Motion);
            Motion->RegisterComponent();

            if (!Motion->Initialize(FighterRoots[Index], PawnData->CharacterSlug, PawnData->ReferenceAttackClip,
                                    CubeMesh, SphereMesh, ShapeMaterial, PawnData->ProxyColor) ||
                Motion->GetSourceMoveIndex() != Reference->SourceMoveIndex ||
                Motion->GetAnimationKey() != Reference->AnimationKey ||
                Motion->GetMoveName() != Reference->InternalName)
            {
                Motion->SetPreviewVisible(false);
                Motion = nullptr;
            }
            else
            {
                for (auto* Component : ExistingShapes)
                    if (auto* Shape = Cast<UStaticMeshComponent>(Component))
                    {
                        FallbackShapes.Add(Shape);
                        FallbackShapeOwners.Add(Index);
                    }
            }
        }

        ReferenceMotions.Add(Motion);
        UFighterCharacterMeshComponent* CharacterMesh = nullptr;

        if (Motion && !FParse::Param(FCommandLine::Get(), TEXT("FighterPlaceholderBodies")))
        {
            CharacterMesh = NewObject<UFighterCharacterMeshComponent>(this);
            AddInstanceComponent(CharacterMesh);
            CharacterMesh->RegisterComponent();

            if (!CharacterMesh->Initialize(FighterRoots[Index], PawnData->CharacterSlug, Motion->GetSourceMoveIndex(),
                                           Motion->GetAnimationKey()))
            {
                CharacterMesh->SetVisible(false);
                CharacterMesh = nullptr;
            }
        }

        CharacterMeshes.Add(CharacterMesh);
    }
}

UStaticMeshComponent* AFighterArena::AddShape(USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh,
                                              const FVector& Location, const FVector& Scale, const FLinearColor& Color)
{
    auto* Shape =
        NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), Name));
    AddInstanceComponent(Shape);
    Shape->SetupAttachment(Parent);
    Shape->SetStaticMesh(Mesh);
    Shape->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Shape->SetMobility(EComponentMobility::Movable);
    Shape->SetRelativeLocation(Location);
    Shape->SetRelativeScale3D(Scale);

    if (ShapeMaterial)
    {
        auto* Material = UMaterialInstanceDynamic::Create(ShapeMaterial, this);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Shape->SetMaterial(0, Material);
    }

    Shape->RegisterComponent();

    return Shape;
}

void AFighterArena::BuildTrainingRoom()
{
    const FLinearColor Dark(0.035f, 0.050f, 0.075f);
    const FLinearColor Grid(0.065f, 0.105f, 0.145f);
    const bool bStageBuilt = Stage->Build(RootComponent);
    Audio->SetStageMusic(Stage->GetStageKey());

    if (!bStageBuilt)
    {
        AddShape(RootComponent, TEXT("Floor"), CubeMesh, FVector(0, 0, -14), FVector(25, 8, 0.26), Dark);

        for (int32 X = -12; X <= 12; ++X)
            AddShape(RootComponent, TEXT("GridX"), CubeMesh, FVector(X * 100, 0, 0), FVector(0.012, 8, 0.006), Grid);

        for (int32 Y = -4; Y <= 4; ++Y)
            AddShape(RootComponent, TEXT("GridY"), CubeMesh, FVector(0, Y * 100, 0), FVector(25, 0.012, 0.006), Grid);
        AddShape(RootComponent, TEXT("CenterLine"), CubeMesh, FVector(0, 0, 0.5), FVector(0.025, 8, 0.008),
                 FLinearColor(0.16f, 0.60f, 0.72f));
        AddShape(RootComponent, TEXT("BackRail"), CubeMesh, FVector(0, 390, 5), FVector(25, 0.05, 0.08),
                 FLinearColor(0.12f, 0.28f, 0.38f));
    }

    for (int32 Index = 0; Index < 2; ++Index)
    {
        auto* FighterRoot = NewObject<USceneComponent>(
            this, MakeUniqueObjectName(this, USceneComponent::StaticClass(), TEXT("FighterRoot")));
        AddInstanceComponent(FighterRoot);
        FighterRoot->SetupAttachment(RootComponent);
        FighterRoot->RegisterComponent();
        FighterRoots.Add(FighterRoot);
        const FLinearColor Color = Index == 0 ? FLinearColor(0.04f, 0.55f, 0.78f) : FLinearColor(0.9f, 0.22f, 0.12f);
        const FLinearColor Light = Index == 0 ? FLinearColor(0.28f, 0.86f, 1.f) : FLinearColor(1.f, 0.57f, 0.25f);
        AddShape(FighterRoot, TEXT("Torso"), CubeMesh, FVector(0, 0, 98), FVector(0.40, 0.46, 0.60), Color);
        AddShape(FighterRoot, TEXT("Waist"), CubeMesh, FVector(0, 0, 61), FVector(0.34, 0.39, 0.14), Dark);
        AddShape(FighterRoot, TEXT("Head"), SphereMesh, FVector(2, 0, 145), FVector(0.32), Light);
        AddShape(FighterRoot, TEXT("Visor"), CubeMesh, FVector(16, 0, 148), FVector(0.03, 0.25, 0.06), Dark);

        for (int32 Side = 0; Side < 2; ++Side)
        {
            const float Sign = Side == 0 ? -1.f : 1.f;
            AddShape(FighterRoot, TEXT("Arm"), CubeMesh, FVector(8, Sign * 30, 105), FVector(0.18, 0.16, 0.42), Color);
            Hands.Add(
                AddShape(FighterRoot, TEXT("Glove"), SphereMesh, FVector(20, Sign * 27, 120), FVector(0.22), Light));
            AddShape(FighterRoot, TEXT("Leg"), CubeMesh, FVector(0, Sign * 12, 34), FVector(0.22, 0.19, 0.55), Color);
            Feet.Add(AddShape(FighterRoot, TEXT("Foot"), CubeMesh, FVector(10, Sign * 12, 9), FVector(0.36, 0.22, 0.16),
                              Light));
        }
    }

    const bool bSourceStage = Stage->HasSourceLighting();

    if (bSourceStage)
        Stage->ApplyPostProcess(CameraComponent->PostProcessSettings);

    const bool bLegacyCharacterLights = FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyCharacterLights"));
    const auto& SourceLights = Stage->GetSourceCharacterLights();

    if (bSourceStage && !bLegacyCharacterLights && !SourceLights.IsEmpty())
    {
        constexpr float CandelaScale = 0.028f;

        float LightScale = .6f, SpecularScale = .6f;
        FParse::Value(FCommandLine::Get(), TEXT("FighterCharacterLightScale="), LightScale);
        FParse::Value(FCommandLine::Get(), TEXT("FighterCharacterSpecular="), SpecularScale);
        LightScale = FMath::Clamp(LightScale, 0.f, 2.f);
        SpecularScale = FMath::Clamp(SpecularScale, 0.f, 1.f);

        float BackScale = .3f, FrontScale = .5f, KeyLux = 4.f;
        FParse::Value(FCommandLine::Get(), TEXT("FighterCharacterBack="), BackScale);
        FParse::Value(FCommandLine::Get(), TEXT("FighterCharacterFront="), FrontScale);
        FParse::Value(FCommandLine::Get(), TEXT("FighterCharacterKey="), KeyLux);

        if (KeyLux > 0.f)
        {
            auto* TopKey = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 600), FRotator(-55, -25, 0));
            TopKey->GetLightComponent()->SetMobility(EComponentMobility::Movable);
            TopKey->GetLightComponent()->SetIntensity(KeyLux);
            TopKey->GetLightComponent()->SetLightColor(FLinearColor(0.8f, 0.88f, 1.f));
            TopKey->GetLightComponent()->SetSpecularScale(SpecularScale);
            TopKey->GetLightComponent()->LightingChannels.bChannel0 = false;
            TopKey->GetLightComponent()->LightingChannels.bChannel1 = true;
            TopKey->GetLightComponent()->MarkRenderStateDirty();
        }

        CharacterLightRig = NewObject<USceneComponent>(this, TEXT("CharacterLightRig"));
        AddInstanceComponent(CharacterLightRig);
        CharacterLightRig->SetupAttachment(RootComponent);
        CharacterLightRig->RegisterComponent();

        for (const auto& Source : SourceLights)
        {
            auto* Spot =
                NewObject<USpotLightComponent>(this, *FString::Printf(TEXT("CharacterLight_%s"), *Source.Name));
            AddInstanceComponent(Spot);
            Spot->SetupAttachment(CharacterLightRig);
            Spot->SetMobility(EComponentMobility::Movable);
            Spot->SetIntensityUnits(ELightUnits::Candelas);
            Spot->SetIntensity(Source.Intensity * CandelaScale * LightScale *
                               (Source.bHasLocation ? BackScale : FrontScale));
            Spot->SetSpecularScale(SpecularScale);
            Spot->SetLightColor(Source.Color);
            Spot->SetAttenuationRadius(Source.Radius);

            if (Source.bHasLocation)
            {
                Spot->SetRelativeLocation(Source.Location);
                Spot->SetRelativeRotation(Source.Rotation);
            }
            else
            {
                Spot->SetRelativeLocation(FVector(450, 0, 120));
                Spot->SetRelativeRotation(FRotator(-15, 180, 0));
            }

            Spot->LightingChannels.bChannel0 = false;
            Spot->LightingChannels.bChannel1 = true;
            Spot->RegisterComponent();
            CharacterLights.Add(Spot);
            UE_LOG(LogTemp, Display,
                   TEXT("FIGHTER_CHARACTER_LIGHT %s cd=%.1f color=%s radius=%.0f rel=%s rot=%s source_location=%d"),
                   *Source.Name, Spot->Intensity, *Source.Color.ToString(), Source.Radius,
                   *Spot->GetRelativeLocation().ToString(), *Spot->GetRelativeRotation().ToString(),
                   Source.bHasLocation ? 1 : 0);
        }

        UE_LOG(LogTemp, Display, TEXT("FIGHTER_CHARACTER_LIGHTS source=%d candela_scale=%.3f legacy=0"),
               CharacterLights.Num(), CandelaScale);
        UE_LOG(
            LogTemp, Display,
            TEXT(
                "FIGHTER_CHARACTER_HIGHLIGHT light_scale=%.2f specular_scale=%.2f back=%.2f front=%.2f key_lux=%.2f authored=1"),
            LightScale, SpecularScale, BackScale, FrontScale, KeyLux);
        UpdateCharacterLightRig();

        return;
    }

    UE_LOG(LogTemp, Display, TEXT("FIGHTER_CHARACTER_LIGHTS source=0 legacy=%d authored_key_fill=1"),
           bLegacyCharacterLights ? 1 : 0);
    auto* KeyLight = GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 600), FRotator(-55, -25, 0));
    KeyLight->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    KeyLight->GetLightComponent()->SetIntensity(5.f);
    KeyLight->GetLightComponent()->SetLightColor(FLinearColor(0.8f, 0.88f, 1.f));
    auto* FillLight = GetWorld()->SpawnActor<APointLight>(FVector(0, 350, 260), FRotator::ZeroRotator);
    FillLight->PointLightComponent->SetMobility(EComponentMobility::Movable);
    FillLight->PointLightComponent->SetIntensity(8000.f);
    FillLight->PointLightComponent->SetAttenuationRadius(2500.f);
    FillLight->PointLightComponent->SetLightColor(FLinearColor(0.6f, 0.8f, 1.f));

    for (ULightComponent* Light : {static_cast<ULightComponent*>(KeyLight->GetLightComponent()),
                                   static_cast<ULightComponent*>(FillLight->PointLightComponent)})
    {
        Light->LightingChannels.bChannel0 = !bSourceStage;
        Light->LightingChannels.bChannel1 = true;
        Light->MarkRenderStateDirty();
    }
}

TSubclassOf<UFighterCameraMode> AFighterArena::DetermineCameraMode() const
{
    if (bReferencePreview)
        return UFighterCameraMode_ReferencePreview::StaticClass();

    if (CombatState.fighters[0].sequence_role != 0)
        return UFighterCameraMode_Paired::StaticClass();

    return UFighterCameraMode_Battle::StaticClass();
}

bool AFighterArena::InputHasFocus() const
{
    const auto* ViewportClient = GetWorld()->GetGameViewport();

    return ViewportClient && ViewportClient->Viewport && ViewportClient->Viewport->HasFocus();
}

// 이번 프레임의 플레이어 입력을 읽는다.
void AFighterArena::ReadLiveInputs()
{
    FrameInputs = {};

    if (!bInputFocus)
        return;
    const double Now = FPlatformTime::Seconds();

    for (uint8 Index = 0; Index < 2; ++Index)
    {
        if (Now >= NextPadPollTime[Index])
        {
            PadSamples[Index] = Device->Read(Index);

            NextPadPollTime[Index] = Now + (PadSamples[Index].raw.connected ? 1.0 / 240.0 : 2.0);
        }
    }

    int32 Slots[2] = {-1, -1};
    int32 Found = 0;

    for (uint8 Index = 0; Index < 2 && Found < 2; ++Index)
        if (PadSamples[Index].raw.connected)
            Slots[Found++] = Index;
    const int32 PlayerSlot = Slots[0];
    const int32 OtherSlot = Slots[1];
    PadConnected[0] = PlayerSlot >= 0;
    PadConnected[1] = OtherSlot >= 0;

    const auto Pad = PlayerSlot >= 0 ? Physical::MapPad(PadSamples[PlayerSlot].raw) : Physical::FrameInput{};
    const auto Keys = Physical::MapDigital(InputSampler->Sample());
    FrameInputs[0].horizontal = Keys.horizontal ? Keys.horizontal : Pad.horizontal;
    FrameInputs[0].vertical = Keys.vertical ? Keys.vertical : Pad.vertical;
    FrameInputs[0].attacks = static_cast<std::uint8_t>(Keys.attacks | Pad.attacks);
    FrameInputs[0].menu = Keys.menu || Pad.menu;
    FrameInputs[1] = OtherSlot >= 0 ? Physical::MapPad(PadSamples[OtherSlot].raw) : Physical::FrameInput{};

    if (OtherSlot < 0)
        AttackLatches[1].Reset();

    if (bControlPaul)
        Swap(FrameInputs[0], FrameInputs[1]);

    for (int32 Index = 0; Index < 2; ++Index)
        AttackLatches[Index].Capture(FrameInputs[Index]);
}

// 전투 코어를 한 프레임 진행한다.
void AFighterArena::AdvanceFrame()
{
    const bool bLive = !Smoke->IsSmokeRun();
    std::array<Core::Input, 2> Inputs;
    std::array<Physical::FrameInput, 2> Players = {bLive ? AttackLatches[0].Consume(FrameInputs[0]) : FrameInputs[0],
                                                   bLive ? AttackLatches[1].Consume(FrameInputs[1]) : FrameInputs[1]};

    const int32 Controlled = GetControlledFighter();
    const int32 DummyIndex = 1 - Controlled;
    UFighterTrainingComponent::FDummyControl Dummy;

    if (bLive)
        Dummy = Training->ControlDummy(CombatState, DummyIndex, PadConnected[1], Players[DummyIndex],
                                       GetPawnData(DummyIndex));

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const bool bDummy = bLive && Index == DummyIndex;
        Inputs[Index] = FighterInputBridge::ToCombatInput(Players[Index], CombatState, static_cast<uint8>(Index),
                                                          bDummy && Dummy.bForceGuard,
                                                          SmokeNoGuard[Index] || (bDummy && Dummy.bNoGuard));
    }

    if (bLive && !PadConnected[1])
    {
        if (Training->GetCounterMode() == 1)
            Inputs[DummyIndex].buttons |= Core::TrainingCounterOn;
        else if (Training->GetCounterMode() == 2)
            Inputs[DummyIndex].buttons |= Core::TrainingCounterOff;
    }

    if (!bLive)
        Smoke->ApplySystemInput(Inputs);

    if (bLive)
        Inputs[Controlled].training_rage = Training->GetRageMode();
    Training->ObserveInput(CombatState, Inputs[Controlled], Controlled, GetPawnData(Controlled));

    if (Dummy.bCrouch)
        Inputs[DummyIndex].vertical = -1;

    if (!DemoSpecs.IsEmpty())
        TickDemoCombo(Inputs);
    const Core::State BeforeStep = CombatState;

    if (!Core::Step(CombatState, Inputs))
    {
        UE_LOG(LogFighterArena, Error, TEXT("FIGHTER_HOST Step rejected frame=%u"), CombatState.frame);
        bStepFailure = true;
        bPaused = true;

        return;
    }

    if (FighterMotionFeel::Trace())
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MOTION_STATE frame=%u hash=%016llx"), CombatState.frame,
               Core::HashState(CombatState));

    if (ReviewCombo && CombatState.fighters[0].sequence_role != BeforeStep.fighters[0].sequence_role)
        UE_LOG(LogFighterArena, Display,
               TEXT("FIGHTER_REVIEW_PAIR route=%d frame=%u record_frame=%d role=%d victim_was_air=%d hits=%d hp=%d"),
               ReviewCombo, CombatState.frame, RecordIndex, CombatState.fighters[0].sequence_role,
               Core::IsAirborne(BeforeStep.fighters[1]) ? 1 : 0, CombatState.fighters[1].combo_hits,
               CombatState.fighters[1].hp);

    if (ReviewCombo && CombatState.fighters[1].hp < BeforeStep.fighters[1].hp)
        UE_LOG(
            LogFighterArena, Display,
            TEXT(
                "FIGHTER_REVIEW_HIT route=%d frame=%u record_frame=%d hits=%d hp=%d fingerprint=%016llx air=%d tornado=%d"),
            ReviewCombo, CombatState.frame, RecordIndex, CombatState.fighters[1].combo_hits, CombatState.fighters[1].hp,
            Core::RulesFingerprint(), Core::IsAirborne(CombatState.fighters[1]) ? 1 : 0,
            (CombatState.fighters[1].combo_flags & Core::ComboScrewed) ? 1 : 0);

    if (BeforeStep.hitstop_frames)
        for (int32 Index = 0; Index < 2; ++Index)
            ++HitReactStart[Index];

    for (int32 Index = 0; Index < 2; ++Index)
        if (BeforeStep.fighters[Index].attack_frame && !CombatState.fighters[Index].attack_frame &&
            CombatState.fighters[Index].crouch_frames >= Core::FullCrouchFrames)
        {
            bCrouchShown[Index] = true;
            CrouchChangeFrame[Index] = static_cast<int32>(CombatState.frame) - 1000;
        }

    if (PracticePhase == EPracticePhase::Training)
        for (int32 Index = 0; Index < 2; ++Index)
        {
            auto& F = CombatState.fighters[Index];

            if (F.hp == 0)
                F.hp = 1;
            const bool bNeutral = F.combo_hits == 0 && F.reaction == Core::ReactionKind::None && F.attack_frame == 0 &&
                                  F.hp < Core::InitialHp;
            PracticeIdleFrames[Index] = bNeutral ? PracticeIdleFrames[Index] + 1 : 0;

            if (PracticeIdleFrames[Index] >= 60)
            {
                F.hp = Core::InitialHp;
                PracticeIdleFrames[Index] = 0;
            }
        }
    Training->ObserveResult(CombatState, Controlled, GetPawnData(Controlled));
    DetectHitReactions(BeforeStep);
    RaiseCombatCues(BeforeStep);
    Audio->Present(BeforeStep, CombatState);
    Training->ObserveStep(BeforeStep, CombatState);
    Smoke->ObserveStep(CombatState);

    for (int32 Index = 0; Index < AbilityHosts.Num(); ++Index)
        AbilityHosts[Index]->Synchronize(CombatState.fighters[Index], CombatState.frame);
}

// 상태 변화에서 피격 반응을 찾는다.
void AFighterArena::DetectHitReactions(const Core::State& Before)
{
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto& Attacker = CombatState.fighters[Index];
        const auto& Defender = CombatState.fighters[1 - Index];

        if (!Attacker.attack_connected || Before.fighters[Index].attack_connected)
            continue;

        const auto* Rules = Core::GetContactAttackDefinition(Attacker.character, Attacker.active_command,
                                                             Before.fighters[Index].heat_frames > 1);
        HitReactClip[1 - Index] = NAME_None;

        if (Rules && Rules->reactions)
        {
            const auto& DefenderBefore = Before.fighters[1 - Index];
            const auto* Move = Core::GetMoveDefinition(DefenderBefore.move);
            const bool bCrouch = Defender.previous_vertical < 0 || (Move && (Move->flags & Core::MoveCrouching));
            const bool bBlocked = Defender.hp == DefenderBefore.hp && Defender.reaction == Core::ReactionKind::None;
            const auto Situation =
                Core::HitSituation(Attacker, Defender, bCrouch, bBlocked,
                                   Core::ResolveCounterHit(DefenderBefore, Defender.previous_buttons));
            HitReactClip[1 - Index] = FName(UTF8_TO_TCHAR(Rules->reactions[Situation].clip));
            HitReactStart[1 - Index] = CombatState.frame;
        }

        return;
    }
}

bool AFighterArena::GetHeatCutIn(FVector& OutEye, FVector& OutLook, float& OutFov, float& OutProgress) const
{
    if (HeatCutStart < 0 || !GetWorld())
        return false;
    const float T = HeatCutElapsed / HeatCutSeconds;

    if (T < 0.f || T >= 1.f)
        return false;
    const auto* Model = CharacterMeshes.IsValidIndex(HeatCutFighter) ? CharacterMeshes[HeatCutFighter].Get() : nullptr;
    FVector Head;

    if (!Model || !Model->GetBoneWorldLocation(TEXT("Head"), Head))
        return false;
    const auto& F = CombatState.fighters[HeatCutFighter];
    const FVector Forward(F.face_cos / 65536.f, F.face_sin / 65536.f, 0.f);

    const FVector Toward =
        FVector(-CombatState.view_sin / 65536.f, CombatState.view_cos / 65536.f, 0.f) * GetViewAxisSign();

    const FVector Dir = (Forward * 0.45f + Toward * 0.9f).GetSafeNormal();
    const auto& O = CombatState.fighters[1 - HeatCutFighter];
    const float Gap = FVector2D((O.position_x - F.position_x) * .1f, (O.position_z - F.position_z) * .1f).Size();
    const float Reach = FMath::Max(100.f, Gap * 0.8f / (Dir | Forward));
    const float Distance = FMath::Min(Reach, FMath::Lerp(165.f, 135.f, FMath::InterpEaseOut(0.f, 1.f, T, 2.f)));
    OutEye = Head + Dir * Distance + FVector(0, 0, -45.f);
    OutLook = Head + FVector(0, 0, -12.f);
    OutFov = 42.f;
    OutProgress = T;

    return true;
}

// 코어 결과를 표시용 큐로 전달한다.
void AFighterArena::RaiseCombatCues(const Core::State& Before)
{
    for (int32 I = 0; I < 2; ++I)
    {
        const auto& Old = Before.fighters[I];
        const auto& Current = CombatState.fighters[I];
        const auto Transition = FighterHeatPresentation::Detect(Old, Current);
        auto Emit = [&](EFighterHeatEffect Kind)
        {
            const uint64 Key = FighterHeatPresentation::Key(CombatState.frame, I, Kind);

            if (HeatCueHistory.Contains(Key))
                return;

            if (HeatCueHistory.Num() >= 64)
                HeatCueHistory.RemoveAt(0);
            HeatCueHistory.Add(Key);
            const FVector Location(Current.position_x * .1f, Current.position_z * .1f, Current.air_height * .1f);
            Impact->PlayHeatStage(Kind, I, Location);

            if (Kind != EFighterHeatEffect::End)
                HeatCueTime = FPlatformTime::Seconds();
            UE_LOG(LogFighterArena, Display,
                   TEXT("FIGHTER_HEAT_CUE frame=%u player=%d kind=%s source_dispatch=adapter"), CombatState.frame, I,
                   FighterHeatPresentation::Name(Kind));
        };

        if (Transition.Start)
        {
            Emit(Transition.Burst ? EFighterHeatEffect::Burst : EFighterHeatEffect::Engager);

            if (Transition.Burst)
            {
                HeatCutStart = GetWorld()->GetTimeSeconds();
                HeatCutElapsed = 0.f;
                HeatCutFighter = I;
            }
        }

        if (Transition.Dash)
            Emit(EFighterHeatEffect::Dash);

        if (Transition.Smash)
            Emit(EFighterHeatEffect::Smash);

        if (Transition.AutoRun)
            Emit(EFighterHeatEffect::AutoRun);

        if (Transition.End)
            Emit(EFighterHeatEffect::End);
    }

    for (int32 I = 0; I < 2; ++I)
    {
        const auto& F = CombatState.fighters[I];

        if (F.character != Core::CharacterId::Paul || F.attack_frame == 0)
            continue;
        const auto* Attack = Core::GetAttackDefinition(F.character, F.active_command);

        if (!Attack || !Attack->name)
            continue;

        for (int32 R = 0; R < FighterImpactProfiles::WindRuleCount; ++R)
        {
            const auto& Rule = FighterImpactProfiles::WindRules[R];

            if (Rule.Frame != F.attack_frame || std::strcmp(Rule.SourceName, Attack->name) != 0)
                continue;
            const uint64 Key = (uint64(CombatState.frame) << 8) | uint64(I << 7) | uint64(R);

            if (PaulWindHistory.Contains(Key))
                continue;

            if (PaulWindHistory.Num() >= 64)
                PaulWindHistory.RemoveAt(0);
            PaulWindHistory.Add(Key);
            Impact->QueuePaulWind(I, R, CombatState.frame);
            UE_LOG(
                LogFighterArena, Display,
                TEXT(
                    "FIGHTER_PAUL_WIND frame=%u player=%d source=%s record=%d source_frame=%d effect=%d slot=%d evidence=motbin_0x8039"),
                CombatState.frame, I, UTF8_TO_TCHAR(Rule.SourceName), Rule.SourceIndex, Rule.Frame, Rule.EffectId,
                Rule.Slot);
        }
    }

    const auto Cues = FighterCombatCues::Detect(Before, CombatState);
    Core::MotionTrace ContactTrace;

    if (!Cues.IsEmpty() && CombatState.frame == Before.frame + 1)
    {
        Core::State Probe = Before;
        std::array<Core::Input, 2> Inputs{};

        for (int32 I = 0; I < 2; ++I)
        {
            const auto& F = CombatState.fighters[I];
            Inputs[I] = {F.previous_horizontal, F.previous_buttons, F.previous_vertical, F.training_rage};
        }

        if (!Core::Step(Probe, Inputs, &ContactTrace))
            ContactTrace = {};
    }

    for (const FFighterCombatCue& Cue : Cues)
    {
        if (!AbilityHosts.IsValidIndex(Cue.Target))
            continue;
        const auto& Defender = Before.fighters[Cue.Target];
        const auto& Attacker = CombatState.fighters[1 - Cue.Target];
        const auto* Attack = Core::GetAttackDefinition(Attacker.character, Attacker.active_command);

        float Height =
            Attack && Attack->hit_level == Core::HitLevel::High ? 145.f
            : Attack && (Attack->hit_level == Core::HitLevel::Low || Attack->hit_level == Core::HitLevel::SpecialLow)
                ? 40.f
                : 105.f;

        if (Core::IsAirborne(Defender))
            Height = FMath::Max(45.f, Defender.air_height * .1f);
        FVector Location(Defender.position_x * 0.1f, Defender.position_z * 0.1f, Height);
        const auto& Contact = ContactTrace.fighters[1 - Cue.Target];

        if (Contact.has_contact)
        {
            Location =
                FVector(Contact.contact_world[0] * .1f, Contact.contact_world[1] * .1f, Contact.contact_world[2] * .1f);
            UE_LOG(LogFighterArena, Verbose, TEXT("CONTACT_FX_ANCHOR frame=%u command=%u mm=%d,%d,%d"),
                   CombatState.frame, Cue.Command, Contact.contact_world[0], Contact.contact_world[1],
                   Contact.contact_world[2]);
        }

        const int32 SourceIndex = 1 - Cue.Target;
        const auto* ContactRules = Core::GetContactAttackDefinition(Attacker.character, Attacker.active_command,
                                                                    Before.fighters[SourceIndex].heat_frames > 1);
        const auto* InputRule = Core::GetCommandDefinition(Attacker.active_command);
        const bool bHeatPowered =
            Before.fighters[SourceIndex].heat_frames > 1 &&
            ((ContactRules && ContactRules != Attack) || (InputRule && (InputRule->gate & Core::GateHeat)) ||
             CommittedDevilPresentation(Attacker.active_command));
        const uint64 HeatKey =
            FighterHeatPresentation::Key(CombatState.frame, SourceIndex, EFighterHeatEffect::Powered);

        if (bHeatPowered && !HeatCueHistory.Contains(HeatKey))
        {
            if (HeatCueHistory.Num() >= 64)
                HeatCueHistory.RemoveAt(0);
            HeatCueHistory.Add(HeatKey);
            Impact->PlayHeatStage(EFighterHeatEffect::Powered, SourceIndex, Location);
            UE_LOG(LogFighterArena, Display,
                   TEXT("FIGHTER_HEAT_POWERED frame=%u player=%d command=%u source_notice_lut=1 mapping=authored"),
                   CombatState.frame, SourceIndex, Cue.Command);
        }

        AbilityHosts[Cue.Target]->ExecuteCombatCue(Cue, Location);
        UE_LOG(LogFighterArena, Verbose,
               TEXT("FIGHTER_CONTACT_CUE frame=%u target=%d command=%u tag=%s hp_loss=%d air=%d"), Cue.Frame,
               Cue.Target, Cue.Command, *Cue.Tag.ToString(), Cue.Damage, Cue.bAirborneContact ? 1 : 0);
    }
}

void AFighterArena::OnCombatCue(const FGameplayTag& CueTag, const FGameplayCueParameters& Parameters)
{
    uint8 AttackerCharacter = 0;
    int32 FeedbackTarget = -1;
    bool bCharacterEffect = false;
    float SourcePower = 0.f;
    uint32 VariationSeed = CombatState.frame;
    FVector AttackerLocation = Parameters.Location;

    for (int32 I = 0; I < AbilityHosts.Num(); ++I)
        if (AbilityHosts[I].Get() == Parameters.Instigator.Get())
        {
            FeedbackTarget = I;
            AttackerCharacter = static_cast<uint8>(SlotCharacter(1 - I));
            const auto& Fighter = CombatState.fighters[1 - I];

            const auto CueCommand = static_cast<Core::CommandId>(Parameters.AbilityLevel);
            const auto* Attack = FighterImpactProfiles::FindCueAttack(Fighter.character, Parameters.AbilityLevel);
            const auto* Profile = FighterImpactProfiles::Find(Fighter.character, Attack);

            if (Attack)
                SourcePower = Attack->damage;
            VariationSeed ^= (static_cast<uint32>(CueCommand) << 16) ^ (static_cast<uint32>(I) << 15);
            bCharacterEffect = Profile != nullptr;

            if (Fighter.character == Core::CharacterId::Paul && CueCommand == Core::CommandId::PaulQuarterCircleRP &&
                (CueTag == FighterGameplayTags::GameplayCue_Fighter_Hit ||
                 CueTag == FighterGameplayTags::GameplayCue_Fighter_CounterHit) &&
                Parameters.NormalizedMagnitude <= .5f)
                CameraComponent->StartDeathfistFocus(CombatState.frame, Parameters.Location);
            UE_LOG(
                LogFighterArena, Display,
                TEXT(
                    "FIGHTER_IMPACT_MAPPING character=%d command=%d special=%d active_command=%d source_resolved=%d hit_level=%d base_damage=%.0f strength_basis=t6_base_power_adapter t8_native_strength=unresolved"),
                int32(Fighter.character), int32(CueCommand), bCharacterEffect ? 1 : 0, int32(Fighter.active_command),
                Attack ? 1 : 0, Attack ? int32(Attack->hit_level) : -1, SourcePower);

            if (Profile)
                UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_IMPACT_SOURCE record=%d name=%s policy=authored"),
                       Profile->SourceIndex, UTF8_TO_TCHAR(Profile->SourceName));
            AttackerLocation =
                FVector(Fighter.position_x * .1f, Fighter.position_z * .1f, 105.f + Fighter.air_height * .1f);
        }
    Impact->Play(CueTag, Parameters.Location, Parameters.RawMagnitude, AttackerCharacter, AttackerLocation,
                 bCharacterEffect, SourcePower, VariationSeed, FeedbackTarget);

    if (Parameters.AggregatedSourceTags.HasTagExact(FighterGameplayTags::Fighter_CueContext_Tornado))
        Impact->PlayTornado(FeedbackTarget, Parameters.Location);
    Impact->PlayFeedback(CueTag, FeedbackTarget, SourcePower, bCharacterEffect, Parameters.NormalizedMagnitude > .5f,
                         Parameters.Location, Parameters.Location - AttackerLocation);

    if (CueTag == FighterGameplayTags::GameplayCue_Fighter_Hit ||
        CueTag == FighterGameplayTags::GameplayCue_Fighter_CounterHit ||
        CueTag == FighterGameplayTags::GameplayCue_Fighter_Guard)
        Audio->PlayImpact(Parameters.Location, Parameters.RawMagnitude,
                          CueTag == FighterGameplayTags::GameplayCue_Fighter_Guard,
                          CueTag == FighterGameplayTags::GameplayCue_Fighter_CounterHit);

    using namespace FighterGameplayTags;
    float Degrees = 0.f;

    if (CueTag == GameplayCue_Fighter_KO)
        Degrees = 6.f;

    if (Degrees > 0 && Degrees >= CueKickDegrees * 0.5f)
    {
        CueKickDegrees = Degrees;
        CueKickTime = FPlatformTime::Seconds();
    }
}

// 고정 60Hz로 시뮬레이션을 진행한다.
void AFighterArena::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (ReviewCombo && !bDemoApplied && FParse::Param(FCommandLine::Get(), TEXT("FighterRecordAudio")))
    {
        bDemoApplied = true;
        ApplyMatchCharacters(DemoCharacters[0], DemoCharacters[1]);
    }

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterRecordAudio")) && AudioRecordStart < 0)
    {
        AudioRecordStart = FPlatformTime::Seconds();
        UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 1800.f, nullptr);
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_RECORD audio_start utc=%s"), *FDateTime::UtcNow().ToIso8601());
    }

    {
        float Seconds = 0;

        const bool bFramesDone = RecordTotal > 0 && RecordIndex >= RecordTotal;

        if (bFramesDone && AudioRecordStart < 0 && FParse::Param(FCommandLine::Get(), TEXT("FighterCinematicRecord")))
            FPlatformMisc::RequestExit(false);
        const bool bSecondsDone = RecordTotal <= 0 &&
                                  FParse::Value(FCommandLine::Get(), TEXT("FighterRecordSeconds="), Seconds) &&
                                  FPlatformTime::Seconds() - AudioRecordStart >= Seconds;

        if (AudioRecordStart >= 0 && (bFramesDone || bSecondsDone) && !bAudioRecordDone)
        {
            bAudioRecordDone = true;
            FString Name = TEXT("demo_audio");
            FParse::Value(FCommandLine::Get(), TEXT("FighterRecordName="), Name);
            UAudioMixerBlueprintLibrary::StopRecordingOutput(
                this, EAudioRecordingExportType::WavFile, Name,
                FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Recording")), nullptr);
            UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_RECORD audio_stop utc=%s name=%s"),
                   *FDateTime::UtcNow().ToIso8601(), *Name);
            FPlatformMisc::RequestExit(false);
        }
    }

    {
        if (RecordTotal < 0)
        {
            RecordTotal = 0;
            FParse::Value(FCommandLine::Get(), TEXT("FighterRecordFrames="), RecordTotal);
        }

        FString RecordFolder;
        FParse::Value(FCommandLine::Get(), TEXT("FighterRecordFolder="), RecordFolder);

        if (RecordFolder.Contains(TEXT("..")) || RecordFolder.Contains(TEXT("/")) || RecordFolder.Contains(TEXT("\\")))
            RecordFolder.Empty();

        if (RecordIndex < RecordTotal)
            FScreenshotRequest::RequestScreenshot(
                FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Recording") / RecordFolder /
                                                  FString::Printf(TEXT("frame_%05d.png"), RecordIndex++)),
                false, false);
    }

    {
        static bool bFlagsRead = false;

        if (!bFlagsRead)
        {
            bFlagsRead = true;
            bTrainingPanel = FParse::Param(FCommandLine::Get(), TEXT("FighterShowPanel"));
            bHitGeometry = FParse::Param(FCommandLine::Get(), TEXT("FighterShowHitboxes"));
        }
    }

    if (bHitGeometry)
        DrawHitGeometry();

    if (IsOnline())
    {
        TickOnline();

        return;
    }

    if (Smoke->IsFinished() || Smoke->IsCaptureRequested())
        return;
    const bool bSmoke = Smoke->IsSmokeRun();
    bInputFocus = bSmoke || Smoke->IsPreviewCapture() || InputHasFocus();

    if (bSmoke)
        Smoke->RunInputProbe();
    else
    {
        if (bInputFocus)
            HandleHostCommands();
        ReadLiveInputs();
        const auto& ControlledInput = FrameInputs[GetControlledFighter()];

        if (ControlledInput.menu && !bPreviousMenu)
            OnPausePressed();
        bPreviousMenu = ControlledInput.menu;

        if (PracticePhase == EPracticePhase::Opening || PracticePhase == EPracticePhase::Title ||
            PracticePhase == EPracticePhase::Select || PracticePhase == EPracticePhase::GetReady ||
            PracticePhase == EPracticePhase::MainMenu || PracticePhase == EPracticePhase::StageSelect ||
            PracticePhase == EPracticePhase::Searching)
        {
            TickPractice();
            UpdatePresentation();

            return;
        }

        if (PracticePhase == EPracticePhase::Training)
        {
            TickPracticeMatching();

            if (bPauseMenu)
                TickPauseMenu();
            else if (!bAutoPauseDone && GetPracticeSeconds() > 2.f &&
                     FCString::Stristr(FCommandLine::Get(), TEXT("-FighterMenuAutoPause=")))
            {
                bAutoPauseDone = true;
                SetPauseMenu(true);
            }
        }
    }

    if (!bInputFocus || bPaused)
    {
        Accumulator = 0;

        for (auto& Latch : AttackLatches)
            Latch.Reset();

        if (!bInputFocus)
            FrameInputs = {};
        UpdatePresentation();

        return;
    }

    if (bReferencePreview)
    {
        PreviewWarmupSeconds += FMath::Max(0.0, static_cast<double>(DeltaSeconds));
        ReferencePreviewSeconds += FMath::Clamp(static_cast<double>(DeltaSeconds), 0.0, 0.25);
        const int32 CaptureFrame = Smoke->GetPreviewCaptureFrame();

        if (CaptureFrame >= 0 && ReferencePreviewSeconds * 60 >= CaptureFrame)
        {
            ReferencePreviewSeconds = CaptureFrame / 60.0;
            UpdatePresentation();

            if (PreviewWarmupSeconds >= 5.0)
                Smoke->CaptureReferencePreview();
        }
        else
            UpdatePresentation();

        return;
    }

    const double StepSeconds = 1.0 / Core::SimulationHz;

    const double SafeDelta = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.0, static_cast<double>(DeltaSeconds)) : 0.0;
    DroppedWallSeconds += FMath::Max(0.0, SafeDelta - 0.25);
    Accumulator += FMath::Min(SafeDelta, 0.25);
    int32 Steps = 0;

    while (Accumulator >= StepSeconds && Steps < 8 && !bStepFailure)
    {
        if (bSmoke)
            Smoke->ReadInputs(CombatState, FrameInputs, SmokeNoGuard);
        else
            ReadLiveInputs();
        AdvanceFrame();
        Accumulator -= StepSeconds;
        ++Steps;

        if (Smoke->ShouldFinish(CombatState))
        {
            Smoke->Finish();
            break;
        }
    }

    if (Accumulator >= StepSeconds)
    {
        const double Remainder = FMath::Fmod(Accumulator, StepSeconds);
        DroppedWallSeconds += Accumulator - Remainder;
        Accumulator = Remainder;
    }

    if (bStepFailure && bSmoke)
        Smoke->Finish();

    if (FighterMotionFeel::FrameTrace())
    {
        static double PreviousWall = 0;
        const double Now = FPlatformTime::Seconds();
        UE_LOG(
            LogFighterArena, Display,
            TEXT(
                "FIGHTER_FRAME_TICK frame=%u delta_ms=%.4f wall_ms=%.4f steps=%d remainder_ms=%.4f dropped_ms=%.4f hitstop=%u"),
            CombatState.frame, DeltaSeconds * 1000.f, PreviousWall > 0 ? (Now - PreviousWall) * 1000 : 0., Steps,
            Accumulator * 1000, DroppedWallSeconds * 1000, CombatState.hitstop_frames);
        PreviousWall = Now;
    }

    UpdatePresentation();

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterCinematicRecord")) && Steps > 0 &&
        (CombatState.frame < 4 || CombatState.frame / 30 != (CombatState.frame - Steps) / 30))
        UE_LOG(LogFighterArena, Display,
               TEXT("FIGHTER_CINEMATIC_REC core_frame=%u record_frame=%d audio_s=%.6f fingerprint=%016llx"),
               CombatState.frame, RecordIndex - 1,
               AudioRecordStart >= 0 ? FPlatformTime::Seconds() - AudioRecordStart : -1.0, Core::RulesFingerprint());
}

void AFighterArena::DrawHitGeometry() const
{
    if (bReferencePreview)
        return;
    UWorld* World = GetWorld();
    const FTransform Root = GetActorTransform();
    auto Point = [&Root](std::int32_t X, std::int32_t Z, std::int32_t Up)
    {
        return Root.TransformPosition(FVector(X * 0.1f, Z * 0.1f, Up * 0.1f));
    };

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto Geometry = Core::DescribeFighterGeometry(CombatState, Index);
        const FColor Hurt = CombatState.fighters[Index].reaction != Core::ReactionKind::None ? FColor(255, 210, 40)
                                                                                             : FColor(60, 230, 120);

        for (std::uint8_t I = 0; I < Geometry.hurt_count; ++I)
        {
            const auto& S = Geometry.hurt[I];
            DrawDebugSphere(World, Point(S.x, S.z, S.up), S.radius * 0.1f, 12, Hurt, false, -1.f, SDPG_Foreground,
                            0.6f);
        }

        for (std::uint8_t I = 0; I < Geometry.hit_count; ++I)
        {
            const auto& H = Geometry.hit[I];
            const FVector A = Point(H.ax, H.az, H.aup), B = Point(H.bx, H.bz, H.bup);
            DrawDebugLine(World, A, B, FColor(255, 40, 40), false, -1.f, SDPG_Foreground, 2.5f);

            if (H.radius > 0)
                DrawDebugCapsule(World, (A + B) * 0.5f, FVector::Dist(A, B) * 0.5f + H.radius * 0.1f, H.radius * 0.1f,
                                 FRotationMatrix::MakeFromZ(B - A).ToQuat(), FColor(255, 80, 80), false, -1.f,
                                 SDPG_Foreground, 1.f);
        }
    }
}

void AFighterArena::UpdateCharacterLightRig()
{
    if (!CharacterLightRig || FighterRoots.Num() != 2)
        return;
    FVector Mid = (FighterRoots[0]->GetComponentLocation() + FighterRoots[1]->GetComponentLocation()) * 0.5f;
    Mid.Z = GetActorLocation().Z;
    float Yaw = CharacterLightRig->GetComponentRotation().Yaw;

    if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
    {
        const FVector ToCamera = PC->PlayerCameraManager->GetCameraLocation() - Mid;

        if (ToCamera.SizeSquared2D() > 1.f)
            Yaw = FMath::RadiansToDegrees(FMath::Atan2(ToCamera.Y, ToCamera.X));
    }

    CharacterLightRig->SetWorldLocationAndRotation(Mid, FRotator(0, Yaw, 0));
}

// 코어 상태를 캐릭터, 카메라, 이펙트에 반영한다.
void AFighterArena::UpdatePresentation()
{
    if (const APlayerController* PC = GetWorld()->GetFirstPlayerController(); PC && PC->PlayerCameraManager)
        Stage->UpdateCameraOcclusion(PC->PlayerCameraManager->GetCameraLocation());

    if (FighterRoots.Num() != 2)
        return;
    UpdateCharacterLightRig();

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto& Fighter = CombatState.fighters[Index];

        const float X = bReferencePreview ? (Index == 0 ? -140.f : 140.f) : Fighter.position_x * 0.1f;
        const float Y = bReferencePreview ? 0.f : Fighter.position_z * 0.1f;
        const float FaceYaw =
            bReferencePreview
                ? (Index == 0 ? 0.f : 180.f)
                : FMath::RadiansToDegrees(FMath::Atan2(Fighter.face_sin / 65536.f, Fighter.face_cos / 65536.f));

        const float AirOffset = bReferencePreview ? 0.f : Core::AirPresentationOffset(Fighter) * 0.1f;

        const float Pelvis =
            Fighter.reaction != Core::ReactionKind::None
                ? Core::ReactionBodyHeight(Fighter.character, Fighter.reaction, Fighter.reaction_frame) * 0.1f
                : 0.f;
        const FVector BodyPoint(X, Y, AirOffset + Pelvis);
        const uint8 ReactionNow = static_cast<uint8>(Fighter.reaction);

        if (bReferencePreview || !bBodyTracked[Index] || CombatState.frame < LastBodyFrame[Index])
            BodySmoothOffset[Index] = FVector::ZeroVector;
        else if (CombatState.frame > LastBodyFrame[Index])
        {
            BodySmoothOffset[Index] *= FMath::Pow(0.7f, static_cast<float>(CombatState.frame - LastBodyFrame[Index]));
            const FVector Gap = LastBodyPoint[Index] - BodyPoint;

            if (ReactionNow != LastBodyReaction[Index] && ReactionNow != 0 && LastBodyReaction[Index] != 0 &&
                Gap.Size() > 15.f)
                BodySmoothOffset[Index] += Gap;

            if (BodySmoothOffset[Index].Size() < 0.5f)
                BodySmoothOffset[Index] = FVector::ZeroVector;
        }

        if (CombatState.frame != LastBodyFrame[Index] || !bBodyTracked[Index])
        {
            LastBodyPoint[Index] = BodyPoint;
            LastBodyReaction[Index] = ReactionNow;
            LastBodyFrame[Index] = CombatState.frame;
            bBodyTracked[Index] = true;
        }

        FighterRoots[Index]->SetRelativeLocation(FVector(X, Y, AirOffset) + BodySmoothOffset[Index]);
        FighterRoots[Index]->SetRelativeRotation(FRotator(0, FaceYaw, 0));
        float Extension = 0;

        if (const auto* Attack = Core::GetAttackDefinition(Fighter.character, Fighter.active_command))
            Extension = FMath::Sin(PI * static_cast<float>(Fighter.attack_frame) / Attack->total_frames);

        for (int32 Side = 0; Side < 2; ++Side)
        {
            const float Sign = Side == 0 ? -1.f : 1.f;
            const bool bPunch = Fighter.active_attack == (Side == 0 ? Core::LP : Core::RP);
            const bool bKick = Fighter.active_attack == (Side == 0 ? Core::LK : Core::RK);
            Hands[Index * 2 + Side]->SetRelativeLocation(FVector(20 + (bPunch ? Extension * 54 : 0), Sign * 27, 120));
            Feet[Index * 2 + Side]->SetRelativeLocation(
                FVector(10 + (bKick ? Extension * 58 : 0), Sign * 12, 9 + (bKick ? Extension * 70 : 0)));
        }

        if (ReferenceMotions.IsValidIndex(Index) && ReferenceMotions[Index])
        {
            auto* Motion = ReferenceMotions[Index].Get();
            const auto PresentedCommand = AbilityHosts.IsValidIndex(Index)
                                              ? static_cast<Core::CommandId>(AbilityHosts[Index]->GetPresentedCommand())
                                              : Core::CommandId::None;

            const UFighterPawnData* PawnData = GetPawnData(Index);
            const bool bReferenceMove =
                MoveCatalog.Find(PresentedCommand) != nullptr ||
                (PresentedCommand != Core::CommandId::None && PawnData &&
                 PawnData->ReferenceClipCommands.Contains(static_cast<int32>(PresentedCommand)));
            const bool bShowReference = bReferencePreview || Fighter.active_attack == 0 || bReferenceMove;
            auto* Model = CharacterMeshes.IsValidIndex(Index) ? CharacterMeshes[Index].Get() : nullptr;
            Motion->SetPreviewVisible(bShowReference && !Model);

            for (int32 ShapeIndex = 0; ShapeIndex < FallbackShapes.Num(); ++ShapeIndex)
                if (FallbackShapeOwners[ShapeIndex] == Index)
                    FallbackShapes[ShapeIndex]->SetVisibility(!bShowReference && !Model);
            int32 SourceFrame = static_cast<int32>(CombatState.frame % 36000);

            if (bReferencePreview)
                SourceFrame = static_cast<int32>(ReferencePreviewSeconds * 60) % Motion->GetAttackFrameCount();
            else if (bReferenceMove)
            {
                SourceFrame = FMath::Max(0, int32(Fighter.attack_frame) - 1);
            }

            Motion->UpdatePose(SourceFrame, bReferencePreview || bReferenceMove);

            if (Model)
            {
                const uint8 Reaction = bReferencePreview ? 0 : static_cast<uint8>(Fighter.reaction);
                FName Locomotion = NAME_None;
                bool bLoopClip = false;
                int32 PoseFrame = Reaction ? int32(Fighter.reaction_frame) - 1 : SourceFrame;

                FName AttackClip = NAME_None;

                if (!bReferencePreview && !bReferenceMove && Reaction == 0 && Fighter.attack_frame != 0)
                {
                    uint16 Sample = 0;

                    if (const char* Clip = Core::AttackPoseClip(Fighter, Sample))
                    {
                        AttackClip = FName(UTF8_TO_TCHAR(Clip));
                        PoseFrame = Sample;
                    }
                }

                if (!bReferencePreview && !bReferenceMove && Reaction == 0 && Fighter.attack_frame == 0)
                {
                    const auto* Move = Core::GetMoveDefinition(Fighter.move);

                    if (Move)
                    {
                        Locomotion = FName(UTF8_TO_TCHAR(Move->clip));
                        bLoopClip = (Move->flags & Core::MoveLoop) != 0;
                    }

                    if (Move && (Move->flags & Core::MoveEndsCrouched))
                    {
                        bCrouchShown[Index] = true;
                        CrouchChangeFrame[Index] = static_cast<int32>(CombatState.frame) - 1000;
                    }
                    else if (Move)
                        bCrouchShown[Index] = false;

                    const int32 Now = static_cast<int32>(CombatState.frame);
                    const bool bCrouchNow =
                        Fighter.move == Core::MoveKind::None && Fighter.stun_frames == 0 &&
                        (Fighter.previous_vertical < 0 || Fighter.crouch_frames >= Core::FullCrouchFrames);

                    if (Fighter.move == Core::MoveKind::None && Fighter.stun_frames == 0 &&
                        bCrouchNow != bCrouchShown[Index])
                    {
                        bCrouchShown[Index] = bCrouchNow;
                        CrouchChangeFrame[Index] = Now;
                    }

                    const int32 Since = Now - CrouchChangeFrame[Index];

                    if (Move)
                        PoseFrame = bLoopClip ? int32(Fighter.move_frame) : int32(Fighter.move_frame) - 1;
                    else if (bCrouchNow)
                    {
                        Locomotion = Since < 9 ? FName(TEXT("crouch_down")) : FName(TEXT("crouch_idle"));
                        bLoopClip = Since >= 9;
                        PoseFrame = Since < 9 ? Since : Since - 9;
                    }
                    else if (Fighter.stun_frames == 0 && Since >= 0 && Since < 9 && Fighter.previous_horizontal == 0)
                    {
                        Locomotion = FName(TEXT("crouch_up"));
                        PoseFrame = Since;
                    }
                    else if (Fighter.stun_frames == 0 && Fighter.previous_horizontal != 0 &&
                             Fighter.previous_vertical == 0 && (Fighter.previous_buttons & Core::Guard) == 0)
                    {
                        Locomotion = Fighter.previous_horizontal * Fighter.facing > 0 ? FName(TEXT("walk_forward"))
                                                                                      : FName(TEXT("walk_back"));
                        bLoopClip = true;
                        PoseFrame = static_cast<int32>(CombatState.frame % 36000);
                    }
                }

                if (!bReferencePreview && Fighter.sequence_role != 0)
                {
                    const auto* Sequence = Core::GetAttackDefinition(Fighter.sequence_owner, Fighter.sequence_command);
                    const char* Clip =
                        Sequence ? (Fighter.sequence_role == 1 ? Sequence->finish_clip : Sequence->victim_clip)
                                 : nullptr;

                    if (Clip)
                    {
                        AttackClip = FName(UTF8_TO_TCHAR(Clip));
                        Locomotion = NAME_None;
                        bLoopClip = false;
                        PoseFrame = Fighter.sequence_frame - 1;
                    }
                }

                FName ReactionClip = NAME_None;
                bool bReactionLoop = false;

                if (Fighter.stun_frames == 0 || Reaction != 0 || Fighter.attack_frame != 0)
                    HitReactClip[Index] = NAME_None;

                if (const auto* Fall = Reaction ? Core::GetReactionDefinition(Fighter.reaction) : nullptr)
                {
                    ReactionClip = FName(UTF8_TO_TCHAR(Fall->clip));
                    bReactionLoop = (Fall->flags & Core::ReactionLying) != 0;
                    PoseFrame = bReactionLoop ? static_cast<int32>(CombatState.frame % 36000)
                                              : int32(Fighter.reaction_frame) - 1;
                }
                else if (!bReferencePreview && Core::StandingReactionClip(Fighter))
                {
                    ReactionClip = FName(UTF8_TO_TCHAR(Core::StandingReactionClip(Fighter)));
                    PoseFrame = Fighter.standing_frame;
                }
                else if (!bReferencePreview && !HitReactClip[Index].IsNone())
                {
                    ReactionClip = HitReactClip[Index];
                    PoseFrame = static_cast<int32>(CombatState.frame - HitReactStart[Index]);
                }

                const auto* BlendAttack = Core::GetAttackDefinition(Fighter.character, Fighter.active_command);

                const auto* Sequence = Core::GetAttackDefinition(Fighter.sequence_owner, Fighter.sequence_command);
                const bool bDevilSequence = Fighter.sequence_role == 1 && Sequence &&
                                            (Sequence->system_action == Core::SystemAction::RageArt ||
                                             Sequence->system_action == Core::SystemAction::HeatSmash);
                const bool bDevilAction = BlendAttack && Fighter.attack_frame &&
                                          (BlendAttack->system_action == Core::SystemAction::HeatSmash ||
                                           BlendAttack->system_action == Core::SystemAction::RageArt ||
                                           CommittedDevilPresentation(Fighter.active_command));
                Model->SetDevilForm(Fighter.character == Core::CharacterId::Kazuya && Fighter.hp > 0 &&
                                    (Fighter.heat_frames != 0 || bDevilAction || bDevilSequence));
                const int32 AttackBlendFrames =
                    BlendAttack && Fighter.attack_frame
                        ? FMath::Clamp(int32(BlendAttack->active_frame) - int32(Fighter.attack_frame), 0, 2)
                        : 0;
                Model->UpdatePose(PoseFrame, bReferencePreview || bReferenceMove, ReactionClip, bReactionLoop,
                                  Locomotion, bLoopClip, AttackClip, CombatState.frame,
                                  !bReferencePreview && (!Smoke->IsSmokeRun() ||
                                                         FParse::Param(FCommandLine::Get(), TEXT("FighterBlendSmoke"))),
                                  CombatState.hitstop_frames != 0, AttackBlendFrames, Core::IsAirborne(Fighter),
                                  Fighter.sequence_subframe / 100.f);

                if (Model->IsPoseFinite() && ReactionClip.IsNone() && !AttackClip.IsNone() &&
                    Model->HasAttackClip(AttackClip))
                    Smoke->NoteAttackPose(Index, Fighter, AttackClip);

                if (Model->IsPoseFinite() && ReactionClip.IsNone() && AttackClip.IsNone() && bReferenceMove &&
                    SourceFrame > 0)
                    Smoke->NoteAttackPose(Index, Fighter, FName(TEXT("attack")));

                if (Reaction && Model->IsPoseFinite())
                    Smoke->NoteReactionPose(Index, Fighter);

                bool bTrail = false;
#if defined(MF_FULL_MOVESET)
                if (!bReferencePreview && Reaction == 0 && Fighter.character == Core::CharacterId::Kazuya &&
                    Fighter.attack_frame)
                {
                    const auto* Fist = Core::GetAttackDefinition(Fighter.character, Fighter.active_command);
                    const bool Legacy = FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyHandTrail"));

                    const bool Electric = Fist && Fist->name && FCStringAnsi::Strcmp(Fist->name, "Kz_shoryu_S") == 0;
                    const bool OldMove = Fighter.active_command == Core::CommandId::KazuyaCrouchDashRP ||
                                         Fighter.active_command == Core::CommandId::KazuyaWindGodFist;
                    bTrail = Fist && (Legacy ? OldMove : Electric) &&
                             int32(Fighter.attack_frame) >= (Legacy ? int32(Fist->active_frame) - 6 : 1) &&
                             int32(Fighter.attack_frame) <= int32(Fist->active_last_frame) + 3;
                }
#endif
                FVector Hand;

                if (bTrail && Model->GetBoneWorldLocation(TEXT("R_Hand"), Hand))
                    Impact->PlayTrail(Index, Hand);
                else
                    Impact->EndTrail(Index);

                {
                    bool bBeam = false;
                    int32 BeamAge = -100;
                    const auto* Attack = Core::GetAttackDefinition(Fighter.character, Fighter.active_command);

                    if (Fighter.character == Core::CharacterId::Kazuya && Attack && Reaction == 0 &&
                        (false
#if defined(MF_FULL_MOVESET)
                         || Fighter.active_command == Core::CommandId::KazuyaStringKz4rprplrp
#endif
                         ))
                    {
                        bBeam = int32(Fighter.attack_frame) >= int32(Attack->active_frame) - 2 &&
                                int32(Fighter.attack_frame) <= int32(Attack->active_last_frame) + 4;
                        BeamAge = int32(Fighter.attack_frame) - int32(Attack->active_frame);
                    }

                    if (Fighter.character == Core::CharacterId::Kazuya && Fighter.sequence_role == 1 &&
                        Fighter.sequence_command == Core::CommandId::KazuyaHeatSmash)
                    {
                        bBeam = Fighter.sequence_frame >= 41 && Fighter.sequence_frame <= 70;
                        BeamAge = int32(Fighter.sequence_frame) - 41;
                    }

                    if (Fighter.character == Core::CharacterId::Kazuya && Fighter.sequence_role == 1 &&
                        Fighter.sequence_command == Core::CommandId::KazuyaRageArt)
                    {
                        bBeam = Fighter.sequence_frame >= 90 && Fighter.sequence_frame < 150;
                        BeamAge = int32(Fighter.sequence_frame) - 90;
                    }

                    FVector Head;

                    if (bBeam && Model->GetBoneWorldLocation(TEXT("Head"), Head))
                    {
                        const FVector Forward(Fighter.face_cos / 65536.f, Fighter.face_sin / 65536.f, 0.f);
                        const FVector Start = Head + Forward * 18.f;
                        FVector TargetHip;
                        const auto* OtherModel =
                            CharacterMeshes.IsValidIndex(1 - Index) ? CharacterMeshes[1 - Index].Get() : nullptr;
                        FVector Aim = Forward;
                        float Reach = 300.f;

                        if (OtherModel && OtherModel->GetBoneWorldLocation(TEXT("Hip"), TargetHip) &&
                            (TargetHip - Start).Size() > 30.f)
                        {
                            Aim = (TargetHip - Start).GetSafeNormal();
                            Reach = FMath::Min((TargetHip - Start).Size() + 80.f, 900.f);
                        }

                        const FVector End = Start + Aim * Reach;
                        ShowBeam(Index, Start, End, BeamAge);
                    }
                    else
                        ShowBeam(Index, FVector::ZeroVector, FVector::ZeroVector, -1);
                }

                FVector FootL, FootR, Hip, Head;

                if (Model->IsPoseFinite() && Model->GetBoneWorldLocation(TEXT("L_Foot"), FootL) &&
                    Model->GetBoneWorldLocation(TEXT("R_Foot"), FootR) &&
                    Model->GetBoneWorldLocation(TEXT("Hip"), Hip) && Model->GetBoneWorldLocation(TEXT("Head"), Head))
                    Stage->NoteFighterBody(Index, FootL, FootR, Hip, Head, GetWorld()->GetDeltaSeconds());

                {
                    constexpr float FresnelScale = 0.1f;
                    const double Now = GetWorld()->GetTimeSeconds();
                    const bool bRage = Fighter.hp != 0 && Core::RageActive(Fighter);

                    if (!bRage)
                        RageSince[Index] = -1.0;
                    else if (RageSince[Index] < 0)
                        RageSince[Index] = Now;

                    if (Fighter.heat_frames != 0)
                        Model->SetFresnel(FLinearColor(0.275f, 0.497f, 1.f), 1.85f, 1.f * 5.f * FresnelScale);
                    else if (bRage)
                    {
                        const float T = float(Now - RageSince[Index]);
                        const float C = FMath::Clamp(T / 0.3f, 0.f, 1.f);
                        const float A = T < 0.031f ? 0.f
                                        : T < 0.125f
                                            ? FMath::Lerp(0.f, 2.325f, (T - 0.031f) / 0.094f)
                                            : FMath::Lerp(2.325f, 3.f, FMath::Clamp((T - 0.125f) / 0.175f, 0.f, 1.f));
                        Model->SetFresnel(FLinearColor(FMath::Lerp(5.f, 8.f, C), FMath::Lerp(0.255f, 0.375f, C),
                                                       FMath::Lerp(0.178f, 0.375f, C)),
                                          2.5f, A * 1.5f * FresnelScale);
                    }
                    else
                        Model->SetFresnel(FLinearColor::Black, 1.f, 0.f);
                }
            }

            if (bReferenceMove && SourceFrame > 0 && Motion->IsPoseFinite())
                Smoke->NoteReferencePose(Index);
        }
    }

    if (CueKickTime >= 0)
    {
        const double Age = FPlatformTime::Seconds() - CueKickTime;

        if (Age < 0.18)
        {
            const float Fade = static_cast<float>(1.0 - Age / 0.18);
            CameraComponent->AddFieldOfViewOffset(-CueKickDegrees * Fade);

            CameraComponent->AddPresentationOffset(
                FVector(0.f, 0.f, CueKickDegrees * 0.6f * Fade * FMath::Sin(static_cast<float>(Age) * 65.f)));
        }
        else
        {
            CueKickTime = -1.0;
            CueKickDegrees = 0.f;
        }
    }

    if (HeatCueTime >= 0)
    {
        const double Age = FPlatformTime::Seconds() - HeatCueTime;

        if (HeatCutStart >= 0)
            HeatCutElapsed += FMath::Min(GetWorld()->GetDeltaSeconds(), 1.f / 30.f);

        if (Age < .35 || HeatCutStart >= 0)
        {
            float Dummy = 0;
            FVector A, B;
            float CutProgress = 0;
            const bool bCut = GetHeatCutIn(A, B, Dummy, CutProgress);

            static bool bCutCaptured = false;

            if (bCut && CutProgress >= .5f && !bCutCaptured &&
                FParse::Param(FCommandLine::Get(), TEXT("FighterHeatCapture")))
            {
                bCutCaptured = true;
                FScreenshotRequest::RequestScreenshot(
                    FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/HeatCutIn.png")),
                    false, false);
            }

            const double Since = bCut ? 0.0 : HeatCutElapsed - HeatCutSeconds;
            const double Use = HeatCutStart >= 0 ? Since : Age;

            if (!bCut && Use >= .35)
            {
                HeatCueTime = -1.0;
                HeatCutStart = -1.0;
            }

            const float Pulse = bCut || Use >= .35 ? 0.f : FMath::Square(1.f - static_cast<float>(Use / .35));

            if (Pulse > 0)
            {
                CueKickDegrees = FMath::Max(CueKickDegrees, 2.5f * Pulse);
                CueKickTime = FPlatformTime::Seconds();
            }

            CameraComponent->AddFieldOfViewOffset(-4.f * Pulse);
            CameraComponent->AddPresentationOffset(FVector(0, 0, 8.f * Pulse));
        }
        else
            HeatCueTime = -1.0;
    }

    CinematicEffects->Present(*this);

    FVector CutEye, CutLook;
    float CutFov = 0, CutProgress = 0;
    const int32 Hide = GetHeatCutIn(CutEye, CutLook, CutFov, CutProgress)
                           ? 1 - HeatCutFighter
                           : UFighterCameraMode_Paired::GetIntroHiddenOpponent(this);

    if (Hide != HeatCutHiddenFighter)
    {
        if (CharacterMeshes.IsValidIndex(HeatCutHiddenFighter) && CharacterMeshes[HeatCutHiddenFighter])
            CharacterMeshes[HeatCutHiddenFighter]->SetVisible(true);

        if (CharacterMeshes.IsValidIndex(Hide) && CharacterMeshes[Hide])
            CharacterMeshes[Hide]->SetVisible(false);
        HeatCutHiddenFighter = Hide;
    }

    if (auto* Controller = GetWorld()->GetFirstPlayerController())
        if (Controller->GetViewTarget() != this)
            Controller->SetViewTarget(this);
}

FString AFighterArena::GetAttackLabel(int32 Index) const
{
    const auto& Fighter = CombatState.fighters[Index == 1 ? 1 : 0];

    if (Fighter.hp == 0)
        return TEXT("KO");

    if (Fighter.reaction != Core::ReactionKind::None)
    {
        const auto* Fall = Core::GetReactionDefinition(Fighter.reaction);

        if (Fall && (Fall->flags & Core::ReactionLying))
            return (Fall->flags & Core::ReactionFaceDown)
                       ? TEXT("DOWN (face down) / 8 stand up")
                       : TEXT("DOWN / 8 stand 6/4 roll  LP side roll  3 low 4 mid 3+4 spring  2+3/4 sweep");

        if (Fall && Fall->ukemi_first)
            return FString::Printf(TEXT("LAUNCH / tech roll: punch or kick on fall frames %d-%d"), Fall->ukemi_first,
                                   Fall->ukemi_last);

        return TEXT("KNOCKDOWN");
    }

    if (Fighter.stun_frames != 0)
        return TEXT("STUN");

    if (const UFighterPawnData* PawnData = GetPawnData(Index == 1 ? 1 : 0))
        if (const FString* Label = PawnData->CommandLabels.Find(static_cast<int32>(Fighter.active_command)))
            return *Label;

    if (const auto* Reference = MoveCatalog.Find(Fighter.active_command))
        return Reference->InternalName;

    if (const auto* Command = Core::GetCommandDefinition(Fighter.active_command))
        return UTF8_TO_TCHAR(Command->notation);

    if ((Fighter.previous_buttons & Core::Guard) != 0)
        return TEXT("GUARD");

    return TEXT("READY");
}

FString AFighterArena::GetCharacterLabel(int32 Index) const
{
    const auto* Host = GetAbilityHost(Index);

    return Host && Host->GetPawnData() ? Host->GetPawnData()->DisplayName.ToString() : TEXT("TRAINING");
}

const AFighterAbilityHost* AFighterArena::GetAbilityHost(int32 Index) const
{
    return AbilityHosts.IsValidIndex(Index) ? AbilityHosts[Index].Get() : nullptr;
}

const UFighterReferenceMotionComponent* AFighterArena::GetReferenceMotion(int32 Index) const
{
    return ReferenceMotions.IsValidIndex(Index) ? ReferenceMotions[Index].Get() : nullptr;
}

const UFighterCharacterMeshComponent* AFighterArena::GetCharacterMesh(int32 Index) const
{
    return CharacterMeshes.IsValidIndex(Index) ? CharacterMeshes[Index].Get() : nullptr;
}

FString AFighterArena::GetReferenceLabel(int32 Index) const
{
    const auto* Motion = GetReferenceMotion(Index);

    if (!Motion)
        return TEXT("REFERENCE CLIP UNAVAILABLE");

    return FString::Printf(TEXT("%s  /  sample %d"), *Motion->GetMoveName(), Motion->GetSampledFrame());
}

bool AFighterArena::HasCharacterMeshes() const
{
    return CharacterMeshes.Num() == 2 && CharacterMeshes[0] && CharacterMeshes[1] && CharacterMeshes[0]->IsReady() &&
           CharacterMeshes[1]->IsReady() && CharacterMeshes[0]->IsPoseFinite() && CharacterMeshes[1]->IsPoseFinite();
}

void AFighterArena::PrepareRenderAssets()
{
#if WITH_EDITOR
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_RENDER preparing assets=%d"),
           FAssetCompilingManager::Get().GetNumRemainingAssets());
    FAssetCompilingManager::Get().FinishAllCompilation();
    int32 PreparedMaterials = 0;

    if (FApp::CanEverRender())
        for (TObjectIterator<UMaterial> It; It; ++It)
        {
            if (!It->GetPathName().StartsWith(TEXT("/Game/Local")))
                continue;

            if (FMaterialResource* Resource = It->GetMaterialResource(GMaxRHIShaderPlatform))
            {
                Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
                ++PreparedMaterials;
            }
        }
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_RENDER submitted materials=%d"), PreparedMaterials);

    if (GShaderCompilingManager)
    {
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_RENDER preparing shader_jobs=%d"),
               GShaderCompilingManager->GetNumRemainingJobs());
        GShaderCompilingManager->FinishAllCompilation();
        int32 IncompleteMaterials = 0;

        if (FApp::CanEverRender())
            for (TObjectIterator<UMaterial> It; It; ++It)
            {
                if (!It->GetPathName().StartsWith(TEXT("/Game/Local")))
                    continue;
                const FMaterialResource* Resource = It->GetMaterialResource(GMaxRHIShaderPlatform);

                if (!Resource || !Resource->IsGameThreadShaderMapComplete())
                {
                    ++IncompleteMaterials;
                    UE_LOG(LogFighterArena, Warning, TEXT("FIGHTER_RENDER incomplete material=%s"), *It->GetPathName());
                }
            }
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_RENDER ready shader_jobs=%d materials_incomplete=%d"),
               GShaderCompilingManager->GetNumRemainingJobs(), IncompleteMaterials);
    }
#endif
    Stage->RecaptureSourceSky();
}

bool AFighterArena::IsOnline() const
{
    if (!Net || !Net->IsOnline())
        return false;

    return !Net->IsPracticeMatching() || PracticePhase == EPracticePhase::Matched || Net->InMatch() ||
           Net->GetResult().has_value();
}

bool AFighterArena::IsViewFlipped() const
{
    static const bool bForced = FParse::Param(FCommandLine::Get(), TEXT("FighterViewFlip"));
    static const bool bDisabled = FParse::Param(FCommandLine::Get(), TEXT("FighterNoAlwaysLeft"));

    if (bForced)
        return true;

    return !bDisabled && IsOnline() && Net->InMatch() && Net->WantsViewFlip(Net->GetLocalPlayer() == 1);
}

int32 AFighterArena::GetViewSide(int32 FaceCos, int32 FaceSin) const
{
    const int64 Dot = int64(FaceCos) * CombatState.view_cos + int64(FaceSin) * CombatState.view_sin;

    return (IsViewFlipped() ? -Dot : Dot) >= 0 ? 1 : 2;
}

void AFighterArena::PlayNameCall(Core::CharacterId Character)
{
    USoundBase* Sound = Character == Core::CharacterId::Paul     ? NameCalls[1].Get()
                        : Character == Core::CharacterId::Kazuya ? NameCalls[0].Get()
                                                                 : nullptr;

    if (Sound)
        UGameplayStatics::PlaySound2D(this, Sound);
}

void AFighterArena::TickDemoCombo(std::array<Core::Input, 2>& Inputs)
{
    Inputs[1] = Core::Input{0, Core::NoGuard, 0};

    if (!bDemoApplied)
    {
        bDemoApplied = true;
        ApplyMatchCharacters(DemoCharacters[0], DemoCharacters[1]);
    }

    if (DemoIndex >= DemoSpecs.Num())
    {
        Inputs[0] = {};

        return;
    }

    if (!DemoSteps.empty() && CombatState.hitstop_frames != 0)
    {
        const auto& Fighter = CombatState.fighters[0];
        Inputs[0] =
            Core::Input{Fighter.previous_horizontal, static_cast<uint8>(Fighter.previous_buttons | Core::NoGuard),
                        Fighter.previous_vertical};

        return;
    }

    if (DemoSteps.empty())
    {
        if (++DemoIdle < 60)
        {
            Inputs[0] = {};

            return;
        }

        ResetTraining();
        CombatState.fighters[0].position_x = -350;
        CombatState.fighters[1].position_x = 350;
        std::string Error;
        DemoSteps = Core::ParseRoute(TCHAR_TO_UTF8(*DemoSpecs[DemoIndex]), 0, Core::NoGuard, Error);
        DemoStep = DemoCursor = 0;
        bDemoPlaying = false;
        DemoIdle = 0;
        DemoBest = 0;
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_DEMO combo %d/%d %s"), DemoIndex + 1, DemoSpecs.Num(),
               *DemoSpecs[DemoIndex]);

        if (ReviewCombo)
            UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_REVIEW_START route=%d record_frame=%d"), ReviewCombo,
                   RecordIndex);

        if (DemoSteps.empty())
        {
            ++DemoIndex;

            return;
        }
    }

    Core::Input In{0, Core::NoGuard, 0};

    if (DemoStep < DemoSteps.size())
    {
        if (!bDemoPlaying && DemoSteps[DemoStep].wait(CombatState))
        {
            bDemoPlaying = true;
            DemoCursor = 0;
        }

        if (bDemoPlaying)
        {
            In = DemoSteps[DemoStep].inputs[DemoCursor++];

            if (DemoCursor == DemoSteps[DemoStep].inputs.size())
            {
                bDemoPlaying = false;
                ++DemoStep;
            }
        }
    }

    Inputs[0] = In;
    const auto& Target = CombatState.fighters[1];
    DemoBest = FMath::Max<int32>(DemoBest, Target.combo_hits);
    const bool bOver = DemoBest > 0 && Target.combo_hits == 0 && Target.reaction == Core::ReactionKind::None;

    if (bOver || ++DemoIdle > 900)
    {
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_DEMO combo %d hits=%d"), DemoIndex + 1, DemoBest);

        if (ReviewCombo && FParse::Param(FCommandLine::Get(), TEXT("FighterReviewValidation")))
        {
            UE_LOG(
                LogFighterArena, Display,
                TEXT(
                    "FIGHTER_REVIEW_END route=%d hp=%d hits=%d fingerprint=%016llx airborne=%d reaction=%d route_done=%d"),
                ReviewCombo, Target.hp, DemoBest, Core::RulesFingerprint(), Core::IsAirborne(Target) ? 1 : 0,
                int32(Target.reaction), DemoStep >= DemoSteps.size() ? 1 : 0);
            FPlatformMisc::RequestExit(false);
        }

        DemoSteps.clear();
        DemoIdle = 0;
        ++DemoIndex;
    }
}

void AFighterArena::ShowBeam(int32 Index, const FVector& Start, const FVector& End, int32 Age)
{
    if (Index < 0 || Index > 1)
        return;
    const bool bShow = Age >= -2 && !(Start - End).IsNearlyZero();
    const TCHAR* MeshNames[] = {TEXT("SM_EF_Grl_BeamPlane"), TEXT("SM_EF_Grl_BeamLightning01"),
                                TEXT("SM_EF_Grl_BeamHead01")};
    const TCHAR* MaterialNames[] = {TEXT("M_SourceBeamPlane"), TEXT("M_SourceBeamLightning"), TEXT("M_SourceBeamHead")};

    for (int32 Layer = 0; Layer < 3; ++Layer)
    {
        TObjectPtr<UStaticMeshComponent>& Beam = BeamParts[Index * 3 + Layer];

        if (!Beam && bShow)
        {
            const FString MeshPath =
                FString::Printf(TEXT("/Game/LocalPresentation/SourceFX/%s.%s"), MeshNames[Layer], MeshNames[Layer]);
            auto* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);

            if (!Mesh)
            {
                UE_LOG(LogFighterArena, Warning, TEXT("SOURCE_BEAM_MISSING %s"), *MeshPath);
                continue;
            }

            Beam = NewObject<UStaticMeshComponent>(this);
            AddInstanceComponent(Beam);
            Beam->SetStaticMesh(Mesh);
            const FString MatPath = FString::Printf(TEXT("/Game/LocalPresentation/SourceFX/%s.%s"),
                                                    MaterialNames[Layer], MaterialNames[Layer]);

            if (auto* Material = LoadObject<UMaterialInterface>(nullptr, *MatPath))
                Beam->SetMaterial(0, Material);
            Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Beam->SetCastShadow(false);
            Beam->SetVisibility(false);
            Beam->RegisterComponent();
        }

        if (!Beam)
            continue;

        if (Layer == 0 && bShow && !Beam->IsVisible())
            UE_LOG(
                LogFighterArena, Display,
                TEXT(
                    "FIGHTER_BEAM fighter=%d frame=%u length=%.0f source_meshes=3 authored_shader=1 sequence=%u sequence_frame=%u"),
                Index, CombatState.frame, (End - Start).Size(),
                static_cast<uint32>(CombatState.fighters[Index].sequence_command),
                CombatState.fighters[Index].sequence_frame);
        Beam->SetVisibility(bShow);

        if (!bShow)
            continue;
        const auto Bounds = Beam->GetStaticMesh()->GetBounds();
        const FVector Size = Bounds.BoxExtent * 2.f;

        const bool AlongY = Layer == 1;
        const FVector Dir = (End - Start).GetSafeNormal();
        const FRotator Rotation =
            (AlongY ? FRotationMatrix::MakeFromY(Dir) : FRotationMatrix::MakeFromX(Dir)).Rotator();
        const float Length = Layer == 2 ? 18.f : (End - Start).Size();
        const float Width = Layer == 0 ? 16.f : Layer == 1 ? 12.f : 20.f;
        FVector Scale(Width / FMath::Max(Size.X, 1.f), Width / FMath::Max(Size.Y, 1.f),
                      Width / FMath::Max(Size.Z, 1.f));

        if (AlongY)
            Scale.Y = Length / FMath::Max(Size.Y, 1.f);
        else
            Scale.X = Length / FMath::Max(Size.X, 1.f);
        FVector Origin = Bounds.Origin;

        if (AlongY)
            Origin.Y -= Bounds.BoxExtent.Y;
        else
            Origin.X -= Bounds.BoxExtent.X;
        Beam->SetWorldLocationAndRotation(Start - Rotation.RotateVector(Origin * Scale), Rotation);
        Beam->SetWorldScale3D(Scale);
    }

    if (bShow && Age >= 0 && !bSourceBeamCaptured && BeamParts[Index * 3 + 2] &&
        FParse::Param(FCommandLine::Get(), TEXT("FighterBeamCapture")))
    {
        bSourceBeamCaptured = true;
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/SourceBeam.png")), false,
            false);
    }
}

bool AFighterArena::IsPracticeMatching() const
{
    return Net && Net->IsPracticeMatching();
}

float AFighterArena::GetPracticeSeconds() const
{
    return GetWorld() ? static_cast<float>(GetWorld()->GetTimeSeconds() - PracticePhaseStart) : 0.f;
}

void AFighterArena::BeginGetReady(EPracticePhase Next)
{
    PracticePhase = Next == EPracticePhase::Matched ? EPracticePhase::Matched : EPracticePhase::GetReady;
    PracticeAfterReady = Next;
    PracticePhaseStart = GetWorld()->GetTimeSeconds();

    PracticeStage =
        Next == EPracticePhase::Matched || !bFromMainMenu ? static_cast<uint8>(FMath::RandRange(0, 1)) : StageChoice;

    if (Next == EPracticePhase::Matched && bPauseMenu)
        SetPauseMenu(false);
    VsLoad = EVsLoad::Art;
    VsLoadStartedAt = FPlatformTime::Seconds();
    VsWarmupFrames = 0;
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_PRACTICE get_ready next=%s stage_request=%u"),
           Next == EPracticePhase::Matched ? TEXT("match") : TEXT("training"), PracticeStage);
}

static void ApplyMenuArrowKeys(const APlayerController* Controller, modular_fighter::input::FrameInput& In);

// 매칭 대기 중 연습 모드를 진행한다.
void AFighterArena::TickPractice()
{
    auto In = FrameInputs[GetControlledFighter()];

    if (bInputFocus)
        ApplyMenuArrowKeys(GetWorld()->GetFirstPlayerController(), In);

    if (PracticePhase == EPracticePhase::Opening || PracticePhase == EPracticePhase::Title)
    {
        const auto& Old = PreviousPracticeInput;
        bool bAny = (In.attacks & ~Old.attacks) != 0 || (In.menu && !Old.menu) ||
                    (In.horizontal != 0 && Old.horizontal == 0) || (In.vertical != 0 && Old.vertical == 0);
        PreviousPracticeInput = In;

        if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
            !bAny && Controller && bInputFocus)
        {
            TArray<FKey> Keys;
            EKeys::GetAllKeys(Keys);

            for (const FKey& Key : Keys)
                if (Key.IsDigital() && Controller->WasInputKeyJustPressed(Key))
                {
                    bAny = true;
                    break;
                }
        }

        TickStartFlow(bAny);

        return;
    }

    const bool bAutoFlow = FParse::Param(FCommandLine::Get(), TEXT("FighterPracticeAuto")) ||
                           FCString::Stristr(FCommandLine::Get(), TEXT("-FighterMenuAuto=")) != nullptr;

    if ((PracticePhase == EPracticePhase::Select || PracticePhase == EPracticePhase::StageSelect) && bAutoFlow &&
        GetPracticeSeconds() > 0.5f && !(PreviousPracticeInput.attacks & Core::LP))
    {
        In = {};
        In.attacks = Core::LP;
        PracticePhaseStart = GetWorld()->GetTimeSeconds();
    }

    const bool Left = In.horizontal < 0 && PreviousPracticeInput.horizontal >= 0;
    const bool Right = In.horizontal > 0 && PreviousPracticeInput.horizontal <= 0;
    const bool Confirm = (In.attacks & Core::LP) && !(PreviousPracticeInput.attacks & Core::LP);
    const bool Back = (In.attacks & Core::LK) && !(PreviousPracticeInput.attacks & Core::LK);
    PreviousPracticeInput = In;

    const APlayerController* MenuController = GetWorld()->GetFirstPlayerController();
    const bool bEnter = bInputFocus && MenuController &&
                        (MenuController->WasInputKeyJustPressed(EKeys::Enter) ||
                         MenuController->WasInputKeyJustPressed(EKeys::SpaceBar));
    const bool bMenuConfirm = Confirm || bEnter;
    const bool bMenuBack = Back || bMenuBackPressed;
    bMenuBackPressed = false;

    if (PracticePhase != EPracticePhase::GetReady)
        UpdateMenuMouse();

    if (PracticePhase == EPracticePhase::MainMenu || PracticePhase == EPracticePhase::Searching ||
        PracticePhase == EPracticePhase::StageSelect)
    {
        TickMainMenu(In, bMenuConfirm, bMenuBack);

        return;
    }

    if (PracticePhase == EPracticePhase::Select)
    {
        if (Left)
            PracticePick[PracticeStep] = Core::CharacterId::Kazuya;

        if (Right)
            PracticePick[PracticeStep] = Core::CharacterId::Paul;

        if (bMenuBack)
        {
            if (PracticeStep == 1)
                PracticeStep = 0;
            else if (bFromMainMenu)
                EnterMainMenu();
        }
        else if (bMenuConfirm)
        {
            PlayNameCall(PracticePick[PracticeStep]);

            if (PracticeStep == 0)
                PracticeStep = 1;
            else
                GoToStageSelectOrReady();
        }

        return;
    }

    if (PracticePhase == EPracticePhase::GetReady)
        TickVsLoading();
}

bool AFighterArena::IsVsStageUrban() const
{
    if (PracticePhase == EPracticePhase::GetReady && bFromMainMenu)
        return StageChoice == 1;

    return Stage && Stage->GetStageKey() == TEXT("urban");
}

bool AFighterArena::IsPresentationSettled(Core::CharacterId Left, Core::CharacterId Right, bool bUrban) const
{
    if (!FApp::CanEverRender())
        return true;
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    AFighterHUD* Hud = Controller ? Cast<AFighterHUD>(Controller->GetHUD()) : nullptr;
    const bool bArt = !Hud || Hud->PrepareVsTextures(Left, Right, bUrban);

    return bArt && IStreamingManager::Get().GetNumWantingResources() == 0 &&
           FShaderPipelineCache::NumPrecompilesRemaining() == 0;
}

void AFighterArena::TickVsLoading()
{
    constexpr float LoadAtFrame = 96.f, HoldFrame = 200.f;
    constexpr int32 WarmupMinFrames = 30;
    constexpr double ArtTimeoutSeconds = 3.0, WarmupTimeoutSeconds = 20.0;
    const bool bUrban = IsVsStageUrban();
    const double Now = GetWorld()->GetTimeSeconds();
    const double Real = FPlatformTime::Seconds();

    if (VsLoad == EVsLoad::Art)
    {
        const bool bArtReady = IsPresentationSettled(PracticePick[0], PracticePick[1], bUrban) ||
                               Real - VsLoadStartedAt > ArtTimeoutSeconds;
        PracticePhaseStart = Now;

        if (!bArtReady)
            return;
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_VS_LOAD art_ready seconds=%.2f"), Real - VsLoadStartedAt);
        VsLoad = EVsLoad::Intro;

        return;
    }

    float Frames = GetPracticeSeconds() * 60.f;

    if (VsLoad == EVsLoad::Intro && Frames >= LoadAtFrame)
    {
        ApplyMatchCharacters(PracticePick[0], PracticePick[1]);

        if (bFromMainMenu)
        {
            const TCHAR* Key = bUrban ? TEXT("urban") : TEXT("yakushima");

            if (Stage->GetStageKey() != Key && Stage->Rebuild(RootComponent, Key) && Stage->HasSourceLighting())
                Stage->ApplyPostProcess(CameraComponent->PostProcessSettings);
            Audio->SetStageMusic(Stage->GetStageKey());
        }

        ResetTraining();
        PrepareRenderAssets();
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_VS_LOAD built stage=%s seconds=%.2f"), *Stage->GetStageKey(),
               FPlatformTime::Seconds() - Real);
        VsLoad = EVsLoad::Warmup;
        VsLoadStartedAt = FPlatformTime::Seconds();
        VsWarmupFrames = 0;

        return;
    }

    if (VsLoad == EVsLoad::Warmup)
    {
        ++VsWarmupFrames;
        const bool bTimedOut = Real - VsLoadStartedAt > WarmupTimeoutSeconds;

        if ((VsWarmupFrames >= WarmupMinFrames && IsPresentationSettled(PracticePick[0], PracticePick[1], bUrban)) ||
            bTimedOut)
        {
            UE_LOG(LogFighterArena, Display,
                   TEXT("FIGHTER_VS_LOAD ready warmup_frames=%d seconds=%.2f wanting=%d timed_out=%d"), VsWarmupFrames,
                   Real - VsLoadStartedAt, IStreamingManager::Get().GetNumWantingResources(), bTimedOut ? 1 : 0);
            VsLoad = EVsLoad::Done;
        }
    }

    if (VsLoad != EVsLoad::Done && Frames > HoldFrame)
    {
        PracticePhaseStart = Now - HoldFrame / 60.f;
        Frames = HoldFrame;
    }

    static bool bVsShotDone = false;

    if (!bVsShotDone && Frames >= 150.f && FParse::Param(FCommandLine::Get(), TEXT("FighterVsShot")))
    {
        bVsShotDone = true;
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("VsScreen.png")), true, false);
    }

    if (VsLoad == EVsLoad::Done && Frames >= float(modular_fighter::net::VsScreenFrames + 12))
    {
        ResetTraining();
        PracticePhase = EPracticePhase::Training;
        PracticePhaseStart = Now;
        Audio->PlayBattleMusic();

        if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
            Controller->bShowMouseCursor = false;
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_PRACTICE training player=%d opponent=%d stage=%s"),
               int32(PracticePick[0]), int32(PracticePick[1]), *Stage->GetStageKey());
    }
}

static void ApplyMenuArrowKeys(const APlayerController* Controller, modular_fighter::input::FrameInput& In)
{
    if (!Controller)
        return;

    if (!In.horizontal)
        In.horizontal = Controller->IsInputKeyDown(EKeys::Right) ? 1 : Controller->IsInputKeyDown(EKeys::Left) ? -1 : 0;

    if (!In.vertical)
        In.vertical = Controller->IsInputKeyDown(EKeys::Up) ? 1 : Controller->IsInputKeyDown(EKeys::Down) ? -1 : 0;
}

void AFighterArena::EnterOpening()
{
    PracticePhase = EPracticePhase::Opening;
    PracticePhaseStart = GetWorld()->GetTimeSeconds();
    bFromMainMenu = true;
    bOpeningPlaying = false;
    OpeningPlayer = LoadObject<UMediaPlayer>(nullptr, TEXT("/Game/LocalPresentation/Movies/MP_Opening.MP_Opening"));
    OpeningTexture = LoadObject<UTexture>(nullptr, TEXT("/Game/LocalPresentation/Movies/MT_Opening.MT_Opening"));
    UMediaSource* Source =
        LoadObject<UMediaSource>(nullptr, TEXT("/Game/LocalPresentation/Movies/FMS_Opening.FMS_Opening"));
    const bool bOpened = OpeningPlayer && Source && OpeningTexture && OpeningPlayer->OpenSource(Source);
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_FLOW opening opened=%d"), bOpened ? 1 : 0);

    if (!bOpened)
        EnterTitle();
}

void AFighterArena::EnterTitle()
{
    const bool bNatural = bOpeningPlaying && OpeningPlayer && !OpeningPlayer->IsPlaying();

    if (OpeningPlayer)
        OpeningPlayer->Close();
    bOpeningPlaying = false;
    PracticePhase = EPracticePhase::Title;
    PracticePhaseStart = GetWorld()->GetTimeSeconds();
    bPauseMenu = false;
    bPaused = false;

    if (Audio && !(bNatural && Audio->IsMusicPlaying()))
        Audio->PlayMenuMusic(TEXT("main_menu"));
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_FLOW title natural=%d"), bNatural ? 1 : 0);
}

// 타이틀부터 경기까지 시작 흐름을 진행한다.
void AFighterArena::TickStartFlow(bool bAnyButton)
{
    const float Seconds = GetPracticeSeconds();
    const bool bAuto = FCString::Stristr(FCommandLine::Get(), TEXT("-FighterMenuAuto=")) != nullptr && Seconds > 0.3f;
    const bool bCapture = FParse::Param(FCommandLine::Get(), TEXT("FighterFlowCapture"));
    auto Shot = [Seconds](float At, bool& bDone, const TCHAR* Name)
    {
        if (bDone || Seconds < At)
            return;
        bDone = true;
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / Name), false, false);
    };

    if (PracticePhase == EPracticePhase::Opening)
    {
        if (!bOpeningPlaying && OpeningPlayer && OpeningPlayer->IsPlaying())
        {
            bOpeningPlaying = true;

            if (Audio)
                Audio->PlayMenuMusic(TEXT("opening"));
            UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_FLOW opening playing duration=%.2f"),
                   OpeningPlayer->GetDuration().GetTotalSeconds());
        }

        static bool bOpeningShot = false;

        if (bCapture)
            Shot(3.f, bOpeningShot, TEXT("Flow-opening.png"));
        const bool bEnded = OpeningPlayer && bOpeningPlaying && !OpeningPlayer->IsPlaying();
        const bool bFailed = !bOpeningPlaying && Seconds > 15.f;
        const bool bCaptureSkip =
            bCapture && Seconds > 6.f && !FParse::Param(FCommandLine::Get(), TEXT("FighterFlowWaitOpening"));

        if (bAnyButton || bAuto || bEnded || bFailed || bCaptureSkip || Seconds > 140.f)
            EnterTitle();

        return;
    }

    if (Audio && !Audio->IsMusicPlaying())
        Audio->PlayMenuMusic(TEXT("main_menu"));
    static bool bTitleShot = false;

    if (bCapture)
        Shot(1.5f, bTitleShot, TEXT("Flow-title.png"));

    if (bAnyButton || bAuto || (bCapture && Seconds > 2.5f))
        EnterMainMenu();
}

void AFighterArena::EnterMainMenu()
{
    PracticePhase = EPracticePhase::MainMenu;
    PracticePhaseStart = GetWorld()->GetTimeSeconds();
    PracticeStep = 0;
    MenuCursor = 0;
    bFromMainMenu = true;
    bPauseMenu = false;
    bPaused = false;
    bMenuBackPressed = false;

    if (Audio)
        Audio->PlayMenuMusic(TEXT("main_menu"));
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MENU main"));
}

void AFighterArena::GoToStageSelectOrReady()
{
    if (!bFromMainMenu)
    {
        BeginGetReady(EPracticePhase::Training);

        return;
    }

    StageChoice = Stage->GetStageKey() == TEXT("urban") ? 1 : 0;
    PracticePhase = EPracticePhase::StageSelect;
    PracticePhaseStart = GetWorld()->GetTimeSeconds();
}

void AFighterArena::UpdateMenuMouse()
{
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();
    const AFighterHUD* Hud = Controller ? Cast<AFighterHUD>(Controller->GetHUD()) : nullptr;

    if (!Hud || !bInputFocus)
        return;
    Controller->bShowMouseCursor = true;
    Controller->bEnableClickEvents = true;
    float X = 0, Y = 0;

    if (!Controller->GetMousePosition(X, Y))
        return;
    const EFighterLobbyAction Under = Hud->FindLobbyButton(FVector2D(X, Y));

    if (FVector2D(X, Y) != LastMenuMouse)
    {
        LastMenuMouse = FVector2D(X, Y);

        switch (Under)
        {
        case EFighterLobbyAction::MenuPractice:
        case EFighterLobbyAction::PauseResume:
            MenuCursor = 0;
            break;
        case EFighterLobbyAction::MenuMultiplayer:
        case EFighterLobbyAction::PauseMatching:
            MenuCursor = 1;
            break;
        case EFighterLobbyAction::MenuQuit:
        case EFighterLobbyAction::PauseMainMenu:
            MenuCursor = 2;
            break;
        case EFighterLobbyAction::PauseQuit:
            MenuCursor = 3;
            break;
        case EFighterLobbyAction::MenuStageUrban:
            StageChoice = 1;
            break;
        case EFighterLobbyAction::MenuStageYakushima:
            StageChoice = 0;
            break;
        case EFighterLobbyAction::PickKazuya:
            PracticePick[PracticeStep] = Core::CharacterId::Kazuya;
            break;
        case EFighterLobbyAction::PickPaul:
            PracticePick[PracticeStep] = Core::CharacterId::Paul;
            break;
        default:
            break;
        }
    }

    if (Controller->WasInputKeyJustPressed(EKeys::LeftMouseButton))
        DoMenuAction(Under);
}

void AFighterArena::ConfirmStageChoice()
{
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MENU stage=%s"),
           StageChoice == 1 ? TEXT("urban") : TEXT("yakushima"));
    BeginGetReady(EPracticePhase::Training);
}

bool AFighterArena::DoMenuAction(EFighterLobbyAction Action)
{
    switch (Action)
    {
    case EFighterLobbyAction::MenuPractice:
        PracticePhase = EPracticePhase::Select;
        PracticePhaseStart = GetWorld()->GetTimeSeconds();
        PracticeStep = 0;

        if (Audio)
            Audio->PlayMenuMusic(TEXT("character_select"));
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MENU practice"));

        return true;
    case EFighterLobbyAction::MenuMultiplayer:
        PracticePhase = EPracticePhase::Searching;
        PracticePhaseStart = GetWorld()->GetTimeSeconds();
        Net->StartMatching();
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MENU multiplayer"));

        return true;
    case EFighterLobbyAction::MenuCancelSearch:
        Net->LeaveOnline();
        EnterMainMenu();

        return true;
    case EFighterLobbyAction::MenuStageUrban:
    case EFighterLobbyAction::MenuStageYakushima:
        StageChoice = Action == EFighterLobbyAction::MenuStageUrban ? 1 : 0;
        ConfirmStageChoice();

        return true;
    case EFighterLobbyAction::PickKazuya:
    case EFighterLobbyAction::PickPaul:
        if (PracticePhase != EPracticePhase::Select)
            return false;

        PracticePick[PracticeStep] =
            Action == EFighterLobbyAction::PickPaul ? Core::CharacterId::Paul : Core::CharacterId::Kazuya;
        PlayNameCall(PracticePick[PracticeStep]);

        if (PracticeStep == 0)
            PracticeStep = 1;
        else
            GoToStageSelectOrReady();

        return true;
    case EFighterLobbyAction::PauseResume:
        SetPauseMenu(false);

        return true;
    case EFighterLobbyAction::PauseReselect:
    case EFighterLobbyAction::ResultPractice:

        if (Action == EFighterLobbyAction::PauseReselect)
        {
            SetPauseMenu(false);

            if (Net->IsPracticeMatching())
                Net->StopMatching();
        }
        else
        {
            Net->LeaveOnline();
            LastLobbyPhase = 255;
        }

        bFromMainMenu = true;
        ResetTraining();

        return DoMenuAction(EFighterLobbyAction::MenuPractice);
    case EFighterLobbyAction::PauseMatching:
        if (Net->IsPracticeMatching())
            Net->StopMatching();
        else
            Net->StartMatching();
        SetPauseMenu(false);
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_PRACTICE matching=%d connected=%d"),
               Net->IsPracticeMatching() ? 1 : 0, Net->IsConnected() ? 1 : 0);

        return true;
    case EFighterLobbyAction::PauseMainMenu:
        Net->LeaveOnline();
        LastLobbyPhase = 255;
        ResetTraining();
        EnterMainMenu();

        return true;
    case EFighterLobbyAction::MenuQuit:
    case EFighterLobbyAction::PauseQuit:
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MENU quit"));
        UKismetSystemLibrary::QuitGame(this, GetWorld()->GetFirstPlayerController(), EQuitPreference::Quit, false);

        return true;
    default:
        return false;
    }
}

void AFighterArena::TickMainMenu(const Core::input::FrameInput& In, bool bConfirm, bool bBack)
{
    const bool bLeftKey = In.horizontal < 0 && PreviousMenuHorizontal >= 0;
    const bool bRightKey = In.horizontal > 0 && PreviousMenuHorizontal <= 0;
    const bool bUpKey = In.vertical > 0 && PreviousMenuVertical <= 0;
    const bool bDownKey = In.vertical < 0 && PreviousMenuVertical >= 0;
    PreviousMenuHorizontal = In.horizontal;
    PreviousMenuVertical = In.vertical;

    if (PracticePhase == EPracticePhase::MainMenu)
    {
        FString Auto;
        const bool bAuto =
            FParse::Value(FCommandLine::Get(), TEXT("FighterMenuAuto="), Auto) && GetPracticeSeconds() > 0.5f;

        if (bLeftKey && MenuCursor == 1)
            MenuCursor = 0;

        if (bRightKey && MenuCursor == 0)
            MenuCursor = 1;

        if (bDownKey && MenuCursor < 2)
        {
            LastMenuTile = MenuCursor;
            MenuCursor = 2;
        }

        if (bUpKey && MenuCursor == 2)
            MenuCursor = LastMenuTile;

        if (bBack)
        {
            EnterTitle();

            return;
        }

        static bool bMenuShots[2] = {false, false};

        if (FParse::Param(FCommandLine::Get(), TEXT("FighterFlowCapture")))
            for (int32 Shot = 0; Shot < 2; ++Shot)
                if (!bMenuShots[Shot] && GetPracticeSeconds() > (Shot == 0 ? 0.2f : 1.5f))
                {
                    bMenuShots[Shot] = true;
                    FScreenshotRequest::RequestScreenshot(
                        FPaths::ConvertRelativePathToFull(
                            FPaths::ProjectSavedDir() /
                            (Shot == 0 ? TEXT("Automation/Flow-menu-enter.png") : TEXT("Automation/Flow-menu.png"))),
                        false, false);
                }
        if (bAuto)
        {
            MenuCursor = Auto.Equals(TEXT("multi"), ESearchCase::IgnoreCase) ? 1 : 0;
            bConfirm = true;
        }

        if (bConfirm)
            DoMenuAction(MenuCursor == 0   ? EFighterLobbyAction::MenuPractice
                         : MenuCursor == 1 ? EFighterLobbyAction::MenuMultiplayer
                                           : EFighterLobbyAction::MenuQuit);

        return;
    }

    if (PracticePhase == EPracticePhase::StageSelect)
    {
        if (bLeftKey)
            StageChoice = 1;

        if (bRightKey)
            StageChoice = 0;

        if (bBack)
        {
            PracticePhase = EPracticePhase::Select;
            PracticeStep = 1;
            PracticePhaseStart = GetWorld()->GetTimeSeconds();

            return;
        }

        static bool bStageShotDone = false;

        if (!bStageShotDone && GetPracticeSeconds() > 0.3f && FParse::Param(FCommandLine::Get(), TEXT("FighterVsShot")))
        {
            bStageShotDone = true;
            FScreenshotRequest::RequestScreenshot(
                FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StageSelect.png")), true, false);
        }

        FString AutoStage;

        if (FParse::Value(FCommandLine::Get(), TEXT("FighterMenuAutoStage="), AutoStage))
            StageChoice = AutoStage.Equals(TEXT("urban"), ESearchCase::IgnoreCase) ? 1 : 0;

        if (bConfirm)
            ConfirmStageChoice();

        return;
    }

    Net->Pump();

    if (Net->HasRoom())
    {
        Net->PromoteMatchingToLobby();
        PracticePhase = EPracticePhase::Off;
        LobbyCursor = 0;

        if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
            Controller->bShowMouseCursor = true;
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MENU room_found"));

        return;
    }

    if (bBack)
        DoMenuAction(EFighterLobbyAction::MenuCancelSearch);
}

void AFighterArena::SetPauseMenu(bool bOpen)
{
    bPauseMenu = bOpen;
    bPaused = bOpen;
    MenuCursor = 0;

    if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
        Controller->bShowMouseCursor = bOpen;
    PreviousPauseInput = FrameInputs[GetControlledFighter()];
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_MENU pause=%d"), bOpen ? 1 : 0);
}

void AFighterArena::TickPauseMenu()
{
    auto In = FrameInputs[GetControlledFighter()];

    if (bInputFocus)
        ApplyMenuArrowKeys(GetWorld()->GetFirstPlayerController(), In);
    const bool bUp = In.vertical > 0 && PreviousPauseInput.vertical <= 0;
    const bool bDown = In.vertical < 0 && PreviousPauseInput.vertical >= 0;
    bool bConfirm = (In.attacks & Core::LP) && !(PreviousPauseInput.attacks & Core::LP);
    const bool bBack = (In.attacks & Core::LK) && !(PreviousPauseInput.attacks & Core::LK);
    PreviousPauseInput = In;
    const APlayerController* Controller = GetWorld()->GetFirstPlayerController();

    if (bInputFocus && Controller &&
        (Controller->WasInputKeyJustPressed(EKeys::Enter) || Controller->WasInputKeyJustPressed(EKeys::SpaceBar)))
        bConfirm = true;

    if (bUp)
        MenuCursor = (MenuCursor + 4) % 5;

    if (bDown)
        MenuCursor = (MenuCursor + 1) % 5;
    UpdateMenuMouse();

    if (!bPauseMenu)
        return;

    FString Auto;

    if (FParse::Value(FCommandLine::Get(), TEXT("FighterMenuAutoPause="), Auto) && GetPracticeSeconds() > 3.5f)
    {
        MenuCursor = Auto == TEXT("reselect") ? 1 : Auto == TEXT("matching") ? 2 : Auto == TEXT("menu") ? 3 : 0;
        bConfirm = true;
    }

    if (bBack)
    {
        SetPauseMenu(false);

        return;
    }

    if (bConfirm)
    {
        static const EFighterLobbyAction Entries[5] = {
            EFighterLobbyAction::PauseResume, EFighterLobbyAction::PauseReselect, EFighterLobbyAction::PauseMatching,
            EFighterLobbyAction::PauseMainMenu, EFighterLobbyAction::PauseQuit};
        DoMenuAction(Entries[MenuCursor]);
    }
}

void AFighterArena::OnPausePressed()
{
    switch (PracticePhase)
    {
    case EPracticePhase::Select:
    case EPracticePhase::StageSelect:
    case EPracticePhase::Searching:
    case EPracticePhase::MainMenu:
        bMenuBackPressed = true;
        break;
    case EPracticePhase::Training:
        SetPauseMenu(!bPauseMenu);
        break;
    case EPracticePhase::Off:
        bPaused = !bPaused;
        break;
    default:
        break;
    }
}

void AFighterArena::TickPracticeMatching()
{
    if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
        Controller && bInputFocus &&
        (Controller->WasInputKeyJustPressed(EKeys::M) ||
         Controller->WasInputKeyJustPressed(EKeys::Gamepad_Special_Left) ||
         (FParse::Param(FCommandLine::Get(), TEXT("FighterPracticeAuto")) && !Net->IsPracticeMatching() &&
          GetPracticeSeconds() > 2.f)))
    {
        if (Net->IsPracticeMatching())
            Net->StopMatching();
        else
            Net->StartMatching();
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_PRACTICE matching=%d connected=%d"),
               Net->IsPracticeMatching() ? 1 : 0, Net->IsConnected() ? 1 : 0);
    }

    if (!Net->IsPracticeMatching())
        return;
    Net->Pump();

    if (Net->HasRoom())
        BeginGetReady(EPracticePhase::Matched);
}

UFighterPawnData* AFighterArena::PawnDataFor(Core::CharacterId Character) const
{
    for (const auto& Data : AvailablePawnData)
        if (Data && Data->GetCoreCharacter() == Character)
            return Data;

    return nullptr;
}

void AFighterArena::ApplyMatchCharacters(Core::CharacterId Left, Core::CharacterId Right)
{
    UFighterPawnData* Wanted[2] = {PawnDataFor(Left), PawnDataFor(Right)};

    if (!Wanted[0] || !Wanted[1])
    {
        UE_LOG(LogFighterArena, Error, TEXT("FIGHTER_NET no pawn data for the match characters"));

        return;
    }

    if (FighterPawnData.Num() == 2 && FighterPawnData[0] == Wanted[0] && FighterPawnData[1] == Wanted[1])
        return;

    for (AFighterAbilityHost* Host : AbilityHosts)
        if (Host)
            Host->Destroy();
    AbilityHosts.Reset();

    for (USceneComponent* Root : FighterRoots)
    {
        TArray<USceneComponent*> Attached;
        Root->GetChildrenComponents(true, Attached);

        for (USceneComponent* Child : Attached)
            if (!TrainingRoomShapes.Contains(Child))
                Child->DestroyComponent();
    }

    for (UFighterReferenceMotionComponent* Motion : ReferenceMotions)
        if (Motion)
            Motion->DestroyComponent();

    for (UFighterCharacterMeshComponent* Mesh : CharacterMeshes)
        if (Mesh)
            Mesh->DestroyComponent();
    ReferenceMotions.Reset();
    CharacterMeshes.Reset();
    FallbackShapes.Reset();
    FallbackShapeOwners.Reset();
    FighterPawnData = {Wanted[0], Wanted[1]};
    CombatState = Core::MakeInitialState(Left, Right);
    SpawnFighterPresentation();
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_NET fighters rebuilt %s / %s"), *Wanted[0]->CharacterSlug,
           *Wanted[1]->CharacterSlug);
}

void AFighterArena::TickMatchLoading()
{
    namespace NetMsg = modular_fighter::net;

    const bool bVsSwap = Net->WantsViewFlip(Net->GetLocalSide() == NetMsg::Side::Right) &&
                         !FParse::Param(FCommandLine::Get(), TEXT("FighterNoAlwaysLeft"));

    if (const auto Load = Net->TakeLoadStage())
    {
        LoadRequest = *Load;
        MatchLoad = EMatchLoad::Requested;

        return;
    }

    if (MatchLoad == EMatchLoad::Requested)
    {
        const double Started = FPlatformTime::Seconds();
        ApplyMatchCharacters(LoadRequest.left, LoadRequest.right);

        if (Stage->Rebuild(RootComponent, LoadRequest.stage == 1 ? TEXT("urban") : TEXT("yakushima")) &&
            Stage->HasSourceLighting())
            Stage->ApplyPostProcess(CameraComponent->PostProcessSettings);
        PrepareRenderAssets();
        MatchLoad = EMatchLoad::Warmup;
        LoadWarmupFrames = 0;
        MatchWarmupStartedAt = FPlatformTime::Seconds();
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_LOAD match %u stage=%u built in %.2f s"), LoadRequest.match_id,
               LoadRequest.stage, FPlatformTime::Seconds() - Started);
    }
    else if (MatchLoad == EMatchLoad::Warmup && ++LoadWarmupFrames >= 30 &&
             (IsPresentationSettled(bVsSwap ? LoadRequest.right : LoadRequest.left,
                                    bVsSwap ? LoadRequest.left : LoadRequest.right, LoadRequest.stage == 1) ||
              FPlatformTime::Seconds() - MatchWarmupStartedAt > 20.0))
    {
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_LOAD settled frames=%d seconds=%.2f wanting=%d"),
               LoadWarmupFrames, FPlatformTime::Seconds() - MatchWarmupStartedAt,
               IStreamingManager::Get().GetNumWantingResources());
        Net->ReportStageLoaded(LoadRequest.match_id);
        MatchLoad = EMatchLoad::WaitingPeer;
    }
    else if (MatchLoad == EMatchLoad::WaitingPeer && Net->InMatch())
        MatchLoad = EMatchLoad::None;
}

void AFighterArena::ApplySideViewProbe(Physical::FrameInput& Pressed)
{
    static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("FighterSideViewProbe"));

    if (!bProbe || !Net->GetRound())
        return;
    static bool bVsShot = false;

    if (!bVsShot && Net->GetRound()->round == 1 && CombatState.frame >= 150)
    {
        bVsShot = true;
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(
                FPaths::ProjectSavedDir() /
                FString::Printf(TEXT("Automation/SideView-core%u-vs.png"), Net->GetLocalPlayer())),
            false, false);
    }

    const uint32 Start = modular_fighter::net::RoundIntroFrames(Net->GetRound()->round) + 30;

    if (CombatState.frame < Start || CombatState.frame > Start + 70)
        return;
    const uint32 Age = CombatState.frame - Start;

    if (Age < 20)
        Pressed.horizontal = 1;

    static FVector StepFrom = FVector::ZeroVector, StepAway = FVector::ZeroVector;

    if (Age >= 40 && Age < 43)
        Pressed.vertical = 1;

    if (Age == 40 || Age == 70)
    {
        const auto& Me = CombatState.fighters[Net->GetLocalPlayer()];
        const FVector Now(Me.position_x * .1f, Me.position_z * .1f, 0.f);

        if (Age == 40)
        {
            StepFrom = Now;
            const APlayerController* Viewer = GetWorld()->GetFirstPlayerController();
            StepAway = Viewer && Viewer->PlayerCameraManager
                           ? Viewer->PlayerCameraManager->GetCameraRotation().Vector().GetSafeNormal2D()
                           : FVector::ZeroVector;
        }
        else
            UE_LOG(LogFighterArena, Display,
                   TEXT("FIGHTER_SIDE_VIEW_STEP core_player=%u flipped=%d away_from_camera_cm=%.1f move=%d"),
                   Net->GetLocalPlayer(), IsViewFlipped() ? 1 : 0, FVector::DotProduct(Now - StepFrom, StepAway),
                   int32(Me.move));
    }

    static TSet<uint64> Logged;

    if (Age != 0 && Age != 20)
        return;
    const uint64 Key = (uint64(Net->GetRound()->match_id) << 16) | (uint64(Net->GetRound()->round) << 8) | Age;

    if (Logged.Contains(Key))
        return;
    Logged.Add(Key);
    const APlayerController* PC = GetWorld()->GetFirstPlayerController();
    const uint8 Local = Net->GetLocalPlayer();
    FVector2D Mine = FVector2D::ZeroVector, Theirs = FVector2D::ZeroVector;
    const auto At = [&](uint8 Index)
    {
        const auto& F = CombatState.fighters[Index];

        return FVector(F.position_x * .1f, F.position_z * .1f, 90.f);
    };
    const bool bProjected = PC && PC->ProjectWorldLocationToScreen(At(Local), Mine) &&
                            PC->ProjectWorldLocationToScreen(At(1 - Local), Theirs);
    UE_LOG(
        LogFighterArena, Display,
        TEXT(
            "FIGHTER_SIDE_VIEW_PROBE round=%u frame=%u age=%u core_player=%u flipped=%d projected=%d local_screen_x=%.1f opponent_screen_x=%.1f core_x=%d"),
        Net->GetRound()->round, CombatState.frame, Age, Local, IsViewFlipped() ? 1 : 0, bProjected ? 1 : 0, Mine.X,
        Theirs.X, CombatState.fighters[Local].position_x);

    if (Age == 0)
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(
                FPaths::ProjectSavedDir() /
                FString::Printf(TEXT("Automation/SideView-core%u-round%u.png"), Local, Net->GetRound()->round)),
            false, false);
}

void AFighterArena::TickOnline()
{
    Net->Pump();
    TickMatchLoading();

    if (MatchLoad == EMatchLoad::Requested || MatchLoad == EMatchLoad::Warmup)
    {
        UpdatePresentation();

        return;
    }

    if (const auto Start = Net->TakeMatchStart())
    {
        ApplyMatchCharacters(Start->left, Start->right);

        if (FParse::Param(FCommandLine::Get(), TEXT("FighterCpu")) ||
            FParse::Value(FCommandLine::Get(), TEXT("FighterCpu="), CpuSeed))
        {
            Cpu.Emplace(Start->your_side == modular_fighter::net::Side::Left ? 0 : 1, CpuSeed * 7919u + Start->round);

            Net->LocalInputSource = [this](uint32 InputFrame) -> TOptional<Core::Input>
            {
                const uint32 Seen = InputFrame > CpuReactionFrames ? InputFrame - CpuReactionFrames : 0;
                const Core::State* Observed = Net->GetConfirmedStateAt(Seen);

                if (!Observed || !Cpu.IsSet())
                    return {};

                return Cpu->Next(*Observed);
            };
        }

        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_REC round_start match=%u round=%u record_frame=%d audio_s=%.3f"),
               Start->match_id, Start->round, RecordIndex,
               AudioRecordStart >= 0 ? FPlatformTime::Seconds() - AudioRecordStart : -1.0);

        if (Start->round == 1 && Stage->Rebuild(RootComponent, Start->stage == 1 ? TEXT("urban") : TEXT("yakushima")) &&
            Stage->HasSourceLighting())
            Stage->ApplyPostProcess(CameraComponent->PostProcessSettings);

        if (Start->round == 1)
        {
            Audio->SetStageMusic(Stage->GetStageKey());
            Audio->PlayBattleMusic();
        }

        ResetTraining();
        CombatState = Net->GetState();
        bControlPaul = false;
        LobbyCursor = 0;
    }

    bInputFocus = InputHasFocus();

    if (bInputFocus)
        HandleHostCommands();
    UpdateLobbyCursor();

    if (!Net->InMatch() || Net->GetResult())
    {
        ReadLiveInputs();
        Accumulator = 0;

        if (bInputFocus)
            HandleLobbyInput();

        if (bInputFocus)
            HandleLobbyClick();
        UpdatePresentation();

        return;
    }

    const double StepSeconds = 1.0 / Core::SimulationHz;
    Accumulator += FMath::Min(static_cast<double>(GetWorld()->GetDeltaSeconds()), 0.25);
    int32 Steps = 0;

    while (Accumulator >= StepSeconds && Steps < 8)
    {
        ReadLiveInputs();
        const uint8 Local = Net->GetLocalPlayer();
        Physical::FrameInput Pressed = bInputFocus ? AttackLatches[0].Consume(FrameInputs[0]) : Physical::FrameInput{};
        ApplySideViewProbe(Pressed);
        Pressed = FighterInputBridge::ToCoreScreenAxes(Pressed, IsViewFlipped());
        Core::Input Input = FighterInputBridge::ToCombatInput(Pressed, CombatState, Local, false, false);
        Input.view_flipped = IsViewFlipped() ? 1 : 0;

        const Core::State BeforeStep = CombatState;

        if (Net->AdvanceLocal(Input) == Core::AdvanceResult::Advanced)
        {
            CombatState = Net->GetState();
            DetectHitReactions(BeforeStep);
            RaiseCombatCues(BeforeStep);
            Audio->Present(BeforeStep, CombatState);

            for (int32 Index = 0; Index < AbilityHosts.Num(); ++Index)
                AbilityHosts[Index]->Synchronize(CombatState.fighters[Index], CombatState.frame);
        }

        Accumulator -= StepSeconds;
        ++Steps;
        Net->Pump();
    }

    if (Accumulator >= StepSeconds)
        Accumulator = FMath::Fmod(Accumulator, StepSeconds);
    Net->ReportIfFinished();
    UpdatePresentation();
}

void AFighterArena::UpdateLobbyCursor()
{
    namespace NetMsg = modular_fighter::net;

    if (!Net->HasRoom())
        return;
    const auto& Room = Net->GetRoom();
    const auto& Me = Room.slots[Room.your_slot];
    const uint8 Key = static_cast<uint8>(Net->GetResult() ? 9 : static_cast<uint8>(Room.phase));

    if (Key == LastLobbyPhase)
        return;
    LastLobbyPhase = Key;
    LobbyPhaseEnteredAt = GetWorld()->GetTimeSeconds();

    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_REC round_start match=0 round=%u record_frame=%d audio_s=%.3f"), Key,
           RecordIndex, AudioRecordStart >= 0 ? FPlatformTime::Seconds() - AudioRecordStart : -1.0);

    if (!Net->GetResult() && (Room.phase == NetMsg::Phase::Waiting || Room.phase == NetMsg::Phase::SideSelect ||
                              Room.phase == NetMsg::Phase::CharacterSelect))
        Audio->PlayMenuMusic(TEXT("character_select"));

    if (Net->GetResult())
        LobbyCursor = 0;
    else if (Room.phase == NetMsg::Phase::CharacterSelect)
        LobbyCursor = Me.character == Core::CharacterId::Paul ? 1 : 0;
    else
        LobbyCursor = 0;
}

void AFighterArena::DoLobbyAction(EFighterLobbyAction Action)
{
    namespace NetMsg = modular_fighter::net;

    if (Action == EFighterLobbyAction::PauseMainMenu || Action == EFighterLobbyAction::ResultPractice)
    {
        DoMenuAction(Action);

        return;
    }

    if (!Net->HasRoom() || Action == EFighterLobbyAction::None)
        return;
    const auto& Room = Net->GetRoom();
    const auto& Me = Room.slots[Room.your_slot];

    switch (Action)
    {
    case EFighterLobbyAction::SideLeft:
    case EFighterLobbyAction::SideRight:
        LobbyCursor = Action == EFighterLobbyAction::SideRight ? 1 : 0;

        if (Room.phase == NetMsg::Phase::SideSelect && Me.side == NetMsg::Side::None)
            Net->SelectSide(LobbyCursor ? NetMsg::Side::Right : NetMsg::Side::Left);
        break;
    case EFighterLobbyAction::PickKazuya:
    case EFighterLobbyAction::PickPaul:
        LobbyCursor = Action == EFighterLobbyAction::PickPaul ? 1 : 0;

        if (Room.phase == NetMsg::Phase::CharacterSelect && !Me.ready)
            Net->SelectCharacter(LobbyCursor ? Core::CharacterId::Paul : Core::CharacterId::Kazuya);
        break;
    case EFighterLobbyAction::StageYakushima:
    case EFighterLobbyAction::StageUrban:
        if (Room.phase == NetMsg::Phase::CharacterSelect && !Me.ready)
            Net->SelectStage(Action == EFighterLobbyAction::StageUrban ? 1 : 0);
        break;
    case EFighterLobbyAction::Ready:
        if (Room.phase == NetMsg::Phase::CharacterSelect && !Me.ready)
        {
            const auto Pick = LobbyCursor ? Core::CharacterId::Paul : Core::CharacterId::Kazuya;

            if (Me.character != Pick)
                Net->SelectCharacter(Pick);
            Net->Ready();
            PlayNameCall(Pick);
        }

        break;
    case EFighterLobbyAction::Unready:
        if (Room.phase == NetMsg::Phase::CharacterSelect && Me.ready)
            Net->SelectCharacter(Me.character);
        break;
    case EFighterLobbyAction::Rematch:
        if (Net->GetResult() && Room.phase == NetMsg::Phase::Result && !Me.rematch)
            Net->Rematch(true);
        break;
    case EFighterLobbyAction::Quit:
        if (Net->GetResult() && Room.phase == NetMsg::Phase::Result)
            Net->Rematch(false);
        break;
    case EFighterLobbyAction::Requeue:
        if (!Net->GetResult())
            break;

        if (Room.phase == NetMsg::Phase::Result)
            Net->Rematch(false);
        UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_LOBBY requeue"));
        Net->PromoteMatchingToLobby();

        if (PracticePhase == EPracticePhase::Matched)
            PracticePhase = EPracticePhase::Off;
        LobbyCursor = 0;
        Net->Requeue();
        break;
    default:
        break;
    }
}

TArray<EFighterLobbyAction> AFighterArena::GetResultMenu() const
{
    const bool bClosed = Net->HasRoom() && Net->GetRoom().phase == modular_fighter::net::Phase::Closed;

    if (bClosed)
        return {EFighterLobbyAction::Requeue, EFighterLobbyAction::ResultPractice, EFighterLobbyAction::PauseMainMenu};

    return {EFighterLobbyAction::Rematch, EFighterLobbyAction::Requeue, EFighterLobbyAction::ResultPractice};
}

void AFighterArena::HandleLobbyInput()
{
    namespace NetMsg = modular_fighter::net;
    auto Now = FrameInputs[0];

    if (bInputFocus)
        ApplyMenuArrowKeys(GetWorld()->GetFirstPlayerController(), Now);

    static TSet<uint8> LobbyShots;

    if (Net->HasRoom() && !LobbyShots.Contains(LastLobbyPhase) &&
        FParse::Param(FCommandLine::Get(), TEXT("FighterSideViewProbe")) &&
        GetWorld()->GetTimeSeconds() - LobbyPhaseEnteredAt > (LastLobbyPhase == 9 ? 4.f : .3f))
    {
        LobbyShots.Add(LastLobbyPhase);
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() /
                                              FString::Printf(TEXT("Automation/SideView-slot%u-lobby%u.png"),
                                                              Net->GetRoom().your_slot, LastLobbyPhase)),
            false, false);
    }

    FString Auto;

    if (FParse::Value(FCommandLine::Get(), TEXT("FighterAutoLobby="), Auto) && Net->HasRoom())
    {
        float Period = 0.75f;
        FParse::Value(FCommandLine::Get(), TEXT("FighterAutoLobbyPeriod="), Period);

        const double Elapsed = GetWorld()->GetTimeSeconds() - LobbyPhaseEnteredAt;
        const bool bPaced = Period > 0.75f;
        const bool bTick = bPaced ? Elapsed >= Period : FMath::Fmod(GetWorld()->GetTimeSeconds(), (double)Period) < 0.1;
        const auto& AutoRoom = Net->GetRoom();
        int32 Want = LobbyCursor;

        if (!Net->GetResult())
            Want = AutoRoom.phase == NetMsg::Phase::SideSelect ? (Auto.StartsWith(TEXT("right")) ? 1 : 0)
                                                               : (Auto.EndsWith(TEXT("paul")) ? 1 : 0);
        Now = {};

        if (LobbyCursor != Want)
        {
            if (!bPaced || Elapsed >= Period * 0.5)
                Now.horizontal = Want ? 1 : -1;
        }
        else if (!Net->GetResult() && bTick)
            Now.attacks = Core::LP;
    }

    const bool Left = Now.horizontal < 0 && PreviousLobbyInput.horizontal >= 0;
    const bool Right = Now.horizontal > 0 && PreviousLobbyInput.horizontal <= 0;
    const bool Confirm = (Now.attacks & Core::LP) && !(PreviousLobbyInput.attacks & Core::LP);
    const bool Back = (Now.attacks & Core::LK) && !(PreviousLobbyInput.attacks & Core::LK);
    const int8 PreviousVertical = PreviousLobbyVertical;
    PreviousLobbyVertical = Now.vertical;
    PreviousLobbyInput = Now;

    if (!Net->HasRoom())
        return;
    const auto& Room = Net->GetRoom();
    const auto& Me = Room.slots[Room.your_slot];

    if (PracticePhase == EPracticePhase::Matched && !Net->GetResult() && !Net->InMatch())
    {
        const double Now2 = GetWorld()->GetTimeSeconds();

        if (Now2 - PracticeLobbyActionTime < 0.4)
            return;
        PracticeLobbyActionTime = Now2;

        if (Room.phase == NetMsg::Phase::SideSelect && Me.side == NetMsg::Side::None)
        {
            Net->SelectSide(NetMsg::Side::Left);
        }
        else if (Room.phase == NetMsg::Phase::CharacterSelect && !Me.ready)
        {
            if (Me.character != PracticePick[0])
                Net->SelectCharacter(PracticePick[0]);
            else if (Me.stage != PracticeStage)
                Net->SelectStage(PracticeStage);
            else
            {
                Net->Ready();
                PlayNameCall(PracticePick[0]);
            }
        }

        return;
    }

    if (Net->GetResult())
    {
        const TArray<EFighterLobbyAction> Menu = GetResultMenu();

        FString AutoResult;
        static uint32 AutoResultMatch = 0;

        if (FParse::Value(FCommandLine::Get(), TEXT("FighterAutoResult="), AutoResult) &&
            GetWorld()->GetTimeSeconds() - LobbyPhaseEnteredAt > 4.5 && AutoResultMatch != Net->GetResult()->match_id)
        {
            AutoResultMatch = Net->GetResult()->match_id;
            UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_LOBBY auto_result=%s"), *AutoResult);
            DoLobbyAction(AutoResult == TEXT("requeue")    ? EFighterLobbyAction::Requeue
                          : AutoResult == TEXT("practice") ? EFighterLobbyAction::ResultPractice
                                                           : EFighterLobbyAction::Rematch);

            return;
        }

        const bool bPrev = Left || (Now.vertical > 0 && PreviousVertical <= 0),
                   bNext = Right || (Now.vertical < 0 && PreviousVertical >= 0);

        if (bPrev)
            LobbyCursor = (LobbyCursor + Menu.Num() - 1) % Menu.Num();

        if (bNext)
            LobbyCursor = (LobbyCursor + 1) % Menu.Num();

        if (Confirm && Menu.IsValidIndex(LobbyCursor))
            DoLobbyAction(Menu[LobbyCursor]);

        return;
    }

    if (Room.phase == NetMsg::Phase::SideSelect && Me.side == NetMsg::Side::None)
    {
        if (Left)
            LobbyCursor = 0;

        if (Right)
            LobbyCursor = 1;

        if (Confirm)
            DoLobbyAction(LobbyCursor ? EFighterLobbyAction::SideRight : EFighterLobbyAction::SideLeft);
    }
    else if (Room.phase == NetMsg::Phase::CharacterSelect)
    {
        if (Left && !Me.ready)
            DoLobbyAction(EFighterLobbyAction::PickKazuya);

        if (Right && !Me.ready)
            DoLobbyAction(EFighterLobbyAction::PickPaul);

        if (Confirm)
            DoLobbyAction(EFighterLobbyAction::Ready);

        if (Back)
            DoLobbyAction(EFighterLobbyAction::Unready);

        const bool bStage = Now.vertical != 0 && PreviousVertical == 0;

        if (bStage && !Me.ready)
            DoLobbyAction(Me.stage == 0 ? EFighterLobbyAction::StageUrban : EFighterLobbyAction::StageYakushima);
    }
}

void AFighterArena::HandleLobbyClick()
{
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();

    if (!Controller)
        return;
    Controller->bShowMouseCursor = true;
    Controller->bEnableClickEvents = true;

    if (!Controller->WasInputKeyJustPressed(EKeys::LeftMouseButton))
        return;
    float X = 0, Y = 0;
    const AFighterHUD* Hud = Cast<AFighterHUD>(Controller->GetHUD());

    if (Hud && Controller->GetMousePosition(X, Y))
        DoLobbyAction(Hud->FindLobbyButton(FVector2D(X, Y)));
}

void AFighterArena::ResetTraining()
{
    Impact->ResetFeedback();
    CinematicEffects->Reset();
    CueKickDegrees = 0;
    CueKickTime = -1;
    HeatCueHistory.Reset();
    PaulWindHistory.Reset();
    HeatCueTime = -1.0;

    if (CharacterMeshes.IsValidIndex(HeatCutHiddenFighter) && CharacterMeshes[HeatCutHiddenFighter])
        CharacterMeshes[HeatCutHiddenFighter]->SetVisible(true);
    HeatCutHiddenFighter = -1;
    HeatCutStart = -1.0;
    Audio->ResetPresentation();
    CombatState = Core::MakeInitialState(SlotCharacter(0), SlotCharacter(1));

    CombatState.hitstop_enabled = FParse::Param(FCommandLine::Get(), TEXT("FighterHitstop")) ? 1 : 0;
    Training->ResetTrackers();

    for (int32 Index = 0; Index < AbilityHosts.Num(); ++Index)
    {
        AbilityHosts[Index]->ResetPresentation();
        AbilityHosts[Index]->Synchronize(CombatState.fighters[Index], CombatState.frame);
    }

    FrameInputs = {};

    for (auto& Latch : AttackLatches)
        Latch.Reset();
    Accumulator = 0;
    bPaused = false;
    bStepFailure = false;
    ReferencePreviewSeconds = 0;
    PreviewWarmupSeconds = 0;
    UE_LOG(LogFighterArena, Display, TEXT("FIGHTER_HOST training reset"));
}

void AFighterArena::InitializeInput()
{
    APlayerController* Controller = GetWorld()->GetFirstPlayerController();

    if (Controller)
        EnableInput(Controller);
    const UFighterInputConfig* Config = nullptr;

    for (const auto& PawnData : FighterPawnData)
        if (PawnData && PawnData->InputConfig)
        {
            Config = PawnData->InputConfig;
            break;
        }
    InputSampler->InitializePlayerInput(Controller, InputComponent, Config);
}

void AFighterArena::HandleHostCommands()
{
    using namespace FighterGameplayTags;
    auto Pressed = [this](const FGameplayTag& Tag)
    {
        return InputSampler->ConsumeCommand(Tag);
    };

    if (Pressed(InputTag_Training_UseKeyboard))
    {
        bKeyboard = true;
        AttackLatches[0].Reset();
    }

    if (Pressed(InputTag_Training_UsePad))
    {
        bKeyboard = false;
        AttackLatches[0].Reset();
    }

    if (Pressed(InputTag_Training_DummyGuard))
        Training->ToggleDummyGuard();

    if (Pressed(InputTag_Training_DummyAttack))
        Training->CycleDummyAttack();

    if (Pressed(InputTag_Training_DummyInterval))
        Training->CycleDummyInterval();

    if (Pressed(InputTag_Training_DummyCrouch))
        Training->ToggleDummyCrouch();

    if (const APlayerController* Controller = GetWorld()->GetFirstPlayerController())
    {
        if (Controller->WasInputKeyJustPressed(EKeys::Zero))
            Training->CycleCounterMode();

        if (Controller->WasInputKeyJustPressed(EKeys::F9))
            Training->CycleRageMode();

        if (Controller->WasInputKeyJustPressed(EKeys::Tab))
            bTrainingPanel = !bTrainingPanel;

        if (Controller->WasInputKeyJustPressed(EKeys::H))
            bHitGeometry = !bHitGeometry;
    }

    if (Pressed(InputTag_Training_Diagnostics))
        bDiagnostics = !bDiagnostics;

    if (Pressed(InputTag_Training_ReferencePreview))
    {
        bReferencePreview = !bReferencePreview;
        ReferencePreviewSeconds = 0;
        Accumulator = 0;

        for (auto& Latch : AttackLatches)
            Latch.Reset();
    }

    if (Pressed(InputTag_Training_SwapControl))
    {
        bControlPaul = !bControlPaul;

        for (auto& Latch : AttackLatches)
            Latch.Reset();
    }

    if (Pressed(InputTag_Training_Reset))
        ResetTraining();

    if (Pressed(InputTag_Training_Pause))
        OnPausePressed();
}

void AFighterArena::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (InputSampler)
        InputSampler->RemovePlayerInput();
    UGameFrameworkComponentManager::RemoveGameFrameworkComponentReceiver(this);
    Device.reset();
    Super::EndPlay(EndPlayReason);
}
