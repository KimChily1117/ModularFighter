// 스테이지 구성, 조명, 물 효과, 카메라 가림 처리.
#include "FighterStageComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "MediaPlayer.h"
#include "MediaSource.h"
#include "FighterArena.h"
#include "NiagaraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/Scene.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "RHI.h"
#include "RHIStats.h"
#include "RenderCore.h"

UFighterStageComponent::UFighterStageComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UFighterStageComponent::SetStage(const FString& Key)
{
    StageKey = Key == TEXT("urban") ? TEXT("urban") : TEXT("yakushima");
    StageFolder = StageKey == TEXT("urban") ? TEXT("Urban") : TEXT("Yakushima");
    bWaterStage = StageKey == TEXT("yakushima");
}

// 선택한 스테이지를 구성한다.
bool UFighterStageComponent::Build(USceneComponent* Parent)
{
    if (IsLoaded())
        return true;
    FString Requested;

    if (!bStageFromCommandLine && FParse::Value(FCommandLine::Get(), TEXT("FighterStage="), Requested))
        SetStage(Requested.ToLower());
    bStageFromCommandLine = true;
    const FString Root = TEXT("/Game/LocalPresentation/") + StageFolder + TEXT("/");
    bProfilePresentation = FParse::Param(FCommandLine::Get(), TEXT("FighterProfilePresentation"));

    for (const TCHAR* Name :
         {TEXT("r.DynamicGlobalIlluminationMethod"), TEXT("r.ReflectionMethod"), TEXT("r.Shadow.Virtual.Enable"),
          TEXT("r.Lumen.TranslucencyReflections.FrontLayer.EnableForProject")})
        if (const auto* CVar = IConsoleManager::Get().FindConsoleVariable(Name))
            UE_LOG(LogTemp, Display, TEXT("FIGHTER_QUALITY %s=%d"), Name, CVar->GetInt());
    FString Text;
    TSharedPtr<FJsonObject> Document;
    const FString Path = FPaths::ProjectDir() / TEXT("LocalData/Presentation") / (StageKey + TEXT("-runtime.json"));

    if (!FFileHelper::LoadFileToString(Text, *Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Document) || !Document.IsValid())
        return false;
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;

    if (!Document->TryGetArrayField(TEXT("instances"), Rows) || Rows->IsEmpty())
        return false;

    FVector WorldOffset = FVector::ZeroVector;
    {
        const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;

        if (Document->TryGetArrayField(TEXT("world_offset"), Offset) && Offset->Num() == 3)
            WorldOffset = FVector((*Offset)[0]->AsNumber(), (*Offset)[1]->AsNumber(), (*Offset)[2]->AsNumber());
    }

    StageYaw = 0;
    Document->TryGetNumberField(TEXT("world_yaw"), StageYaw);
    const FTransform StageTurn(FRotator(0, StageYaw, 0));

    struct FStageRow
    {
        UStaticMesh* Mesh;
        TArray<UMaterialInterface*> Materials;
        FTransform Transform;
        bool bShadow;
        FString Key;
        bool bShadowOnly = false;
        bool bSky = false;
    };

    TArray<FStageRow> Parsed;
    TMap<FString, UStaticMesh*> Meshes;
    TMap<FString, UMaterialInterface*> Materials;
    auto Vector = [](const TSharedPtr<FJsonObject>& Row, const TCHAR* Name, FVector& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;

        if (!Row->TryGetArrayField(Name, A) || A->Num() != 3)
            return false;
        double V[3];

        for (int32 I = 0; I < 3; ++I)
            if (!(*A)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I]))
                return false;
        Out = FVector(V[0], V[1], V[2]);

        return true;
    };

    for (const auto& Value : *Rows)
    {
        const auto Row = Value->AsObject();
        FString MeshPath;
        FVector Position, Rotation, Scale;

        if (!Row.IsValid() || !Row->TryGetStringField(TEXT("mesh_asset"), MeshPath) ||
            !MeshPath.StartsWith(Root + TEXT("Meshes/")) || !Vector(Row, TEXT("location"), Position) ||
            !Vector(Row, TEXT("rotation"), Rotation) || !Vector(Row, TEXT("scale"), Scale))
            return false;

        if (!Meshes.Contains(MeshPath))
            Meshes.Add(MeshPath, LoadObject<UStaticMesh>(nullptr, *MeshPath));

        if (!Meshes[MeshPath])
        {
            UE_LOG(LogTemp, Warning, TEXT("STAGE %s missing mesh %s"), *StageKey, *MeshPath);

            return false;
        }

        FStageRow Item{Meshes[MeshPath],
                       {},
                       FTransform(FRotator(Rotation.X, Rotation.Y, Rotation.Z), Position, Scale),
                       true,
                       MeshPath};
        const TArray<TSharedPtr<FJsonValue>>* Parents = nullptr;

        if (Row->TryGetArrayField(TEXT("parents"), Parents))
        {
            for (const auto& ParentValue : *Parents)
            {
                auto Ancestor = ParentValue->AsObject();

                if (!Ancestor.IsValid() || !Vector(Ancestor, TEXT("location"), Position) ||
                    !Vector(Ancestor, TEXT("rotation"), Rotation) || !Vector(Ancestor, TEXT("scale"), Scale))
                    return false;
                Item.Transform =
                    Item.Transform * FTransform(FRotator(Rotation.X, Rotation.Y, Rotation.Z), Position, Scale);
            }
        }

        Row->TryGetBoolField(TEXT("cast_shadow"), Item.bShadow);
        Row->TryGetBoolField(TEXT("shadow_only"), Item.bShadowOnly);
        Row->TryGetBoolField(TEXT("sky"), Item.bSky);
        bSourceSky |= Item.bSky;
        Item.Key += Item.bShadow ? TEXT("|shadow") : TEXT("|no-shadow");

        if (Item.bShadowOnly)
            Item.Key += TEXT("|shadow-only");
        const TArray<TSharedPtr<FJsonValue>>* Overrides = nullptr;

        if (!Row->TryGetArrayField(TEXT("material_assets"), Overrides))
            return false;

        for (const auto& Ref : *Overrides)
        {
            FString MatPath;

            if (!Ref->TryGetString(MatPath))
                return false;
            Item.Key += TEXT("|") + MatPath;

            if (MatPath.IsEmpty())
            {
                Item.Materials.Add(nullptr);
                continue;
            }

            if (!MatPath.StartsWith(Root + TEXT("Materials/")))
                return false;

            if (!Materials.Contains(MatPath))
                Materials.Add(MatPath, LoadObject<UMaterialInterface>(nullptr, *MatPath));

            if (!Materials[MatPath])
            {
                UE_LOG(LogTemp, Warning, TEXT("STAGE %s missing material %s"), *StageKey, *MatPath);

                return false;
            }

            Item.Materials.Add(Materials[MatPath]);
        }

        Item.Transform.AddToTranslation(WorldOffset);
        Item.Transform = Item.Transform * StageTurn;
        Parsed.Add(MoveTemp(Item));
    }

    TMap<FString, UInstancedStaticMeshComponent*> Groups;

    for (const auto& Row : Parsed)
    {
        UInstancedStaticMeshComponent* Component = Groups.FindRef(Row.Key);

        if (!Component)
        {
            Component = NewObject<UInstancedStaticMeshComponent>(GetOwner());
            GetOwner()->AddInstanceComponent(Component);
            Component->SetupAttachment(Parent);

            Component->SetMobility(EComponentMobility::Static);
            Component->SetStaticMesh(Row.Mesh);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCanEverAffectNavigation(false);
            Component->SetCastShadow(Row.bShadow || Row.bShadowOnly);

            if (Row.bShadowOnly)
            {
                Component->SetHiddenInGame(true);
                Component->bCastHiddenShadow = true;
            }

            if (Row.bSky)
            {
                Component->SetCastShadow(false);
                Component->bAffectDistanceFieldLighting = false;
            }

            for (int32 I = 0; I < Row.Materials.Num(); ++I)
                if (Row.Materials[I])
                    Component->SetMaterial(I, Row.Materials[I]);
            Component->RegisterComponent();
            Groups.Add(Row.Key, Component);
            CreatedComponents.Add(Component);

            if (Row.Key.Contains(TEXT("SM_Water")))
                CinematicWaterSurfaces.Add(Component);

            if (Row.Key.Contains(TEXT("SM_Water_IS_B")) || Row.Key.Contains(TEXT("SM_Ground_under")))
                UE_LOG(LogTemp, Display, TEXT("YAKUSHIMA_SURFACE mesh=%s bounds=%s transform=%s material=%s blend=%d"),
                       *Row.Mesh->GetName(), *Row.Mesh->GetBoundingBox().ToString(), *Row.Transform.ToString(),
                       *GetNameSafe(Component->GetMaterial(0)),
                       Component->GetMaterial(0) ? int32(Component->GetMaterial(0)->GetBlendMode()) : -1);
        }

        Component->AddInstance(Row.Transform);
        ++InstanceCount;
    }

    UE_LOG(
        LogTemp, Display,
        TEXT("YAKUSHIMA_STAGE loaded instances=%d groups=%d meshes=%d; source centimeters; visual collision disabled"),
        InstanceCount, Groups.Num(), Meshes.Num());
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_STAGE key=%s instances=%d offset=%s yaw=%.0f"), *StageKey, InstanceCount,
           *WorldOffset.ToString(), StageYaw);

    if (!bSourceSky && bWaterStage)
    {
        auto* Sky = NewObject<UStaticMeshComponent>(GetOwner());
        GetOwner()->AddInstanceComponent(Sky);
        Sky->SetupAttachment(Parent);
        Sky->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
        Sky->SetMaterial(
            0, LoadObject<UMaterialInterface>(
                   nullptr, TEXT("/Game/LocalPresentation/Yakushima/Materials/M_YakushimaSky.M_YakushimaSky")));
        Sky->SetRelativeScale3D(FVector(800));
        Sky->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Sky->SetCastShadow(false);
        Sky->RegisterComponent();
        CreatedComponents.Add(Sky);
    }

    auto* Ambient = GetWorld()->SpawnActor<ASkyLight>();
    EnvironmentActors.Add(Ambient);
    auto* Light = Ambient->GetLightComponent();
    Light->SetMobility(EComponentMobility::Movable);
    Light->SourceType = SLS_SpecifiedCubemap;
    Light->SetCubemap(LoadObject<UTextureCube>(
        nullptr, TEXT("/Engine/MapTemplates/Sky/DaylightAmbientCubemap.DaylightAmbientCubemap")));
    Light->SetIntensity(1.2f);
    Light->SetLowerHemisphereColor(FLinearColor(.08f, .10f, .07f));
    auto* Fog = GetWorld()->SpawnActor<AExponentialHeightFog>();
    EnvironmentActors.Add(Fog);
    Fog->GetComponent()->SetFogDensity(.002f);
    Fog->GetComponent()->SetStartDistance(600.f);
    Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.15f, .25f, .23f));
    Fog->GetComponent()->SetFogMaxOpacity(.65f);
    bSourceLighting = ApplySourceLighting(Ambient, Fog);

    const TArray<TSharedPtr<FJsonValue>>* Movies = nullptr;

    if (Document->TryGetArrayField(TEXT("movies"), Movies))
        for (const auto& Value : *Movies)
        {
            const auto Row = Value->AsObject();
            FString PlayerPath, SourcePath;

            if (!Row.IsValid() || !Row->TryGetStringField(TEXT("player"), PlayerPath) ||
                !Row->TryGetStringField(TEXT("source"), SourcePath))
                continue;
            auto* Player = LoadObject<UMediaPlayer>(nullptr, *PlayerPath);
            auto* Source = LoadObject<UMediaSource>(nullptr, *SourcePath);

            if (!Player || !Source)
            {
                UE_LOG(LogTemp, Warning, TEXT("STAGE %s missing movie %s"), *StageKey, *PlayerPath);
                continue;
            }

            Player->SetLooping(true);
            Player->PlayOnOpen = true;
            const bool bOpened = Player->OpenSource(Source);
            MoviePlayers.Add(Player);
            UE_LOG(LogTemp, Display, TEXT("STAGE_MOVIE %s opened=%d"), *PlayerPath, bOpened ? 1 : 0);
        }
    if (!bWaterStage)
        return IsLoaded();
    auto* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    auto* Ring = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Game/LocalPresentation/Yakushima/Materials/M_YakushimaRipple.M_YakushimaRipple"));

    if (Plane && Ring)
        for (int32 I = 0; I < 16; ++I)
        {
            auto* C = NewObject<UStaticMeshComponent>(GetOwner());
            GetOwner()->AddInstanceComponent(C);
            C->SetupAttachment(Parent);
            C->SetStaticMesh(Plane);
            C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            C->SetCastShadow(false);
            auto* Mat = UMaterialInstanceDynamic::Create(Ring, this);
            Mat->SetScalarParameterValue(TEXT("RippleOpacity"), 0);
            C->SetMaterial(0, Mat);
            C->SetVisibility(false);
            C->RegisterComponent();
            CreatedComponents.Add(C);
            Ripples.Add(C);
            RippleMaterials.Add(Mat);
            RippleAges.Add(1.f);
            RippleScales.Add(1.f);
        }

    auto LoadSystem = [](const TCHAR* Name)
    {
        return LoadObject<UNiagaraSystem>(nullptr,
                                          *FString::Printf(TEXT("/Game/LocalPresentation/Niagara/%s.%s"), Name, Name));
    };
    WaterStep = LoadSystem(TEXT("NS_FG_WaterStep"));
    WaterSplash[0] = LoadSystem(TEXT("NS_FG_WaterSplashS"));
    WaterSplash[1] = LoadSystem(TEXT("NS_FG_WaterSplashM"));
    WaterSplash[2] = LoadSystem(TEXT("NS_FG_WaterSplashL"));
    UE_LOG(LogTemp, Display, TEXT("YAKUSHIMA_WATER_FX step=%d splash=%d%d%d"), WaterStep ? 1 : 0,
           WaterSplash[0] ? 1 : 0, WaterSplash[1] ? 1 : 0, WaterSplash[2] ? 1 : 0);

    return IsLoaded();
}

bool UFighterStageComponent::ApplySourceLighting(ASkyLight* Ambient, AExponentialHeightFog* Fog)
{
    FString Text;
    TSharedPtr<FJsonObject> Doc;

    if (!FFileHelper::LoadFileToString(
            Text, *(FPaths::ProjectDir() / TEXT("LocalData/Presentation") / (StageKey + TEXT("-lighting.json")))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
        return false;
    auto Obj = [&](const TCHAR* Name)
    {
        const TSharedPtr<FJsonObject>* O = nullptr;

        return Doc->TryGetObjectField(Name, O) ? *O : nullptr;
    };
    auto Vec = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Name, double Default) -> FVector
    {
        const TArray<TSharedPtr<FJsonValue>>* A = nullptr;

        if (!O.IsValid() || !O->TryGetArrayField(Name, A) || A->Num() < 3)
            return FVector(Default);

        return FVector((*A)[0]->AsNumber(), (*A)[1]->AsNumber(), (*A)[2]->AsNumber());
    };
    auto Num = [](const TSharedPtr<FJsonObject>& O, const TCHAR* Name, double Default)
    {
        double V = Default;

        if (O.IsValid())
            O->TryGetNumberField(Name, V);

        return V;
    };
    const auto Sun = Obj(TEXT("sun")), SkyLight = Obj(TEXT("sky_light")), FogData = Obj(TEXT("fog")),
               Post = Obj(TEXT("post"));

    if (!Sun.IsValid() || !FogData.IsValid() || !Post.IsValid())
        return false;
    const double EV = Num(Post, TEXT("exposure_ev100"), 10.4);
    const double Unit = FMath::Pow(2.0, -EV);
    const FVector Rot = Vec(Sun, TEXT("rotation"), 0);
    auto* SunActor =
        GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 800), FRotator(Rot.X, Rot.Y + StageYaw, Rot.Z));
    EnvironmentActors.Add(SunActor);
    auto* SunLight = CastChecked<UDirectionalLightComponent>(SunActor->GetLightComponent());
    SunLight->SetMobility(EComponentMobility::Movable);
    SunLight->SetIntensity(Num(Sun, TEXT("intensity_lux"), 15000) * Unit);
    SunLight->SetEnableLightShaftBloom(Sun->GetBoolField(TEXT("light_shaft_bloom")));
    SunLight->SetBloomScale(Num(Sun, TEXT("bloom_scale"), .2));
    SunLight->SetBloomThreshold(Num(Sun, TEXT("bloom_threshold"), 8));
    const FVector Tint = Vec(Sun, TEXT("bloom_tint"), 1);
    SunLight->SetBloomTint(FLinearColor(Tint.X, Tint.Y, Tint.Z).ToFColor(true));
    SunLight->SetShadowAmount(Num(Sun, TEXT("shadow_amount"), 1));

    const auto Grade = Obj(TEXT("grade"));

    if (Grade.IsValid())
    {
        SunLight->SetUseTemperature(true);
        SunLight->SetTemperature(Num(Grade, TEXT("sun_temperature_k"), 6500));
        bool bShafts = false;
        Grade->TryGetBoolField(TEXT("light_shaft_bloom"), bShafts);
        SunLight->SetEnableLightShaftBloom(bShafts);
        SunLight->SetBloomScale(Num(Grade, TEXT("sun_bloom_scale"), .2));
    }

    SunLight->SetAtmosphereSunLight(false);

    SunLight->ForwardShadingPriority = 1;
    const FVector Sky = Vec(SkyLight, TEXT("color"), 1);
    auto* Light = Ambient->GetLightComponent();
    Light->SetLightColor(FLinearColor(Sky.X, Sky.Y, Sky.Z));

    if (bSourceSky)
    {
        Light->SourceType = SLS_CapturedScene;
        Light->SetIntensity(Num(SkyLight, TEXT("intensity"), 1));
        Light->RecaptureSky();
    }

    auto* FogComponent = Fog->GetComponent();

    FogComponent->SetFogDensity(Num(FogData, TEXT("density"), .02));
    FogComponent->SetFogMaxOpacity(Num(FogData, TEXT("max_opacity"), 1));
    FogComponent->SetStartDistance(Num(FogData, TEXT("start_distance"), 0));
    const FVector Lum = Vec(FogData, TEXT("inscattering_luminance"), 0) * Unit;
    FogComponent->SetFogInscatteringColor(FLinearColor(Lum.X, Lum.Y, Lum.Z));
    SourceWhiteTemp = Num(Post, TEXT("white_temp"), 6500);

    if (Grade.IsValid())
        SourceWhiteTemp = Num(Grade, TEXT("white_temp"), SourceWhiteTemp);

    if (Grade.IsValid())
    {
        GradeBloomIntensity = Num(Grade, TEXT("bloom_intensity"), -1);
        const FVector T = Vec(Grade, TEXT("bloom_tint"), 1);
        GradeBloomTint = FLinearColor(T.X, T.Y, T.Z);
    }

    SourceBloomThreshold = Num(Post, TEXT("bloom_threshold"), -1);

    SourceCharacterLights.Reset();
    const TArray<TSharedPtr<FJsonValue>>* Spots = nullptr;

    if (Doc->TryGetArrayField(TEXT("character_lights"), Spots))
        for (const auto& Value : *Spots)
        {
            const auto Row = Value->AsObject();
            FString Name;

            if (!Row.IsValid() || !Row->TryGetStringField(TEXT("name"), Name) || !Name.StartsWith(TEXT("SpotLight_")))
                continue;
            const TSharedPtr<FJsonObject>* Channels = nullptr;
            bool bChannel1 = false;

            if (Row->TryGetObjectField(TEXT("channels"), Channels) && Channels->IsValid())
                (*Channels)->TryGetBoolField(TEXT("bChannel1"), bChannel1);

            if (!bChannel1)
                continue;
            FSourceCharacterLight L;
            L.Name = Name;
            L.Intensity = Num(Row, TEXT("intensity"), 0);
            L.Radius = Num(Row, TEXT("radius"), 3000);
            const FVector C = Vec(Row, TEXT("color"), 1);
            L.Color = FLinearColor(C.X, C.Y, C.Z);

            L.Location = Vec(Row, TEXT("location"), 0);
            L.bHasLocation = !L.Location.IsNearlyZero();
            const FVector R = Vec(Row, TEXT("rotation"), 0);
            L.Rotation = FRotator(R.X, R.Y, R.Z);
            SourceCharacterLights.Add(L);
        }
    FString LutRef;
    Doc->TryGetStringField(TEXT("lut"), LutRef);
    const FString LutName = TEXT("LUT_") + FPaths::GetBaseFilename(LutRef);
    SourceLut = LutRef.IsEmpty() ? nullptr
                                 : LoadObject<UTexture>(nullptr, *(TEXT("/Game/LocalPresentation/") + StageFolder +
                                                                   TEXT("/Textures/") + LutName));
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "YAKUSHIMA_LIGHTING source sun=%.2f rot=%s fog=%.4f start=%.0f white=%.0f lut=%d sky_dome=%d character_lights=%d"),
        SunLight->Intensity, *FRotator(Rot.X, Rot.Y, Rot.Z).ToString(), FogComponent->FogDensity,
        FogComponent->StartDistance, SourceWhiteTemp, SourceLut ? 1 : 0, bSourceSky ? 1 : 0,
        SourceCharacterLights.Num());

    return true;
}

int32 UFighterStageComponent::PreloadOtherStages()
{
    int32 Loaded = 0;

    for (const TCHAR* Key : {TEXT("yakushima"), TEXT("urban")})
    {
        if (StageKey == Key)
            continue;
        FString Text;
        TSharedPtr<FJsonObject> Doc;

        if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectDir() / TEXT("LocalData/Presentation") /
                                                   (FString(Key) + TEXT("-runtime.json")))) ||
            !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Doc) || !Doc.IsValid())
            continue;
        const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;

        if (!Doc->TryGetArrayField(TEXT("instances"), Rows))
            continue;
        TSet<FString> Paths;

        for (const auto& Value : *Rows)
        {
            const auto Row = Value->AsObject();

            if (!Row.IsValid())
                continue;
            FString Mesh;

            if (Row->TryGetStringField(TEXT("mesh_asset"), Mesh))
                Paths.Add(Mesh);
            const TArray<TSharedPtr<FJsonValue>>* Mats = nullptr;

            if (Row->TryGetArrayField(TEXT("material_assets"), Mats))
                for (const auto& M : *Mats)
                {
                    FString P;

                    if (M->TryGetString(P) && !P.IsEmpty())
                        Paths.Add(P);
                }
        }

        for (const FString& P : Paths)
            if (UObject* Asset = LoadObject<UObject>(nullptr, *P))
            {
                PreloadedAssets.Add(Asset);
                ++Loaded;
            }
    }

    UE_LOG(LogTemp, Display, TEXT("FIGHTER_STAGE preloaded other stages assets=%d"), Loaded);

    return Loaded;
}

bool UFighterStageComponent::Rebuild(USceneComponent* Parent, const FString& Key)
{
    const FString Previous = StageKey;
    SetStage(Key);

    if (IsLoaded() && StageKey == Previous)
        return true;

    for (const auto& Component : CreatedComponents)
        if (IsValid(Component.Get()))
            Component->DestroyComponent();

    for (const auto& Actor : EnvironmentActors)
        if (IsValid(Actor.Get()))
            Actor->Destroy();

    for (const auto& Player : MoviePlayers)
        if (IsValid(Player.Get()))
            Player->Close();
    MoviePlayers.Reset();
    Occluders.Reset();
    CreatedComponents.Reset();
    EnvironmentActors.Reset();
    Ripples.Reset();
    RippleMaterials.Reset();
    RippleAges.Reset();
    RippleScales.Reset();
    InstanceCount = 0;
    bSourceLighting = bSourceSky = false;
    SourceLut = nullptr;
    SourceCharacterLights.Reset();
    WaterStep = nullptr;

    for (auto& S : WaterSplash)
        S = nullptr;
    CinematicWaterSurfaces.Reset();
    ActiveWaterParticles.Reset();
    bCinematicWaterHidden = false;
    bStageFromCommandLine = true;
    const bool bBuilt = Build(Parent);
    RecaptureSourceSky();
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_STAGE rebuilt %s -> %s loaded=%d"), *Previous, *StageKey, bBuilt ? 1 : 0);

    return bBuilt;
}

void UFighterStageComponent::RecaptureSourceSky()
{
    if (!bSourceSky)
        return;

    for (const auto& Actor : EnvironmentActors)
        if (auto* Sky = Cast<ASkyLight>(Actor.Get()))
        {
            Sky->GetLightComponent()->RecaptureSky();
            UE_LOG(LogTemp, Display, TEXT("FIGHTER_STAGE sky recaptured"));
        }
}

// 스테이지 후처리를 적용한다.
void UFighterStageComponent::ApplyPostProcess(FPostProcessSettings& Settings) const
{
    if (!bSourceLighting)
        return;
    Settings.bOverride_WhiteTemp = true;
    Settings.WhiteTemp = SourceWhiteTemp;

    if (SourceBloomThreshold >= 0)
    {
        Settings.bOverride_BloomThreshold = true;
        Settings.BloomThreshold = SourceBloomThreshold;
    }

    if (GradeBloomIntensity >= 0)
    {
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = GradeBloomIntensity;
        Settings.bOverride_Bloom1Tint = Settings.bOverride_Bloom2Tint = Settings.bOverride_Bloom3Tint = true;
        Settings.bOverride_Bloom4Tint = Settings.bOverride_Bloom5Tint = Settings.bOverride_Bloom6Tint = true;
        Settings.Bloom1Tint = Settings.Bloom2Tint = Settings.Bloom3Tint = Settings.Bloom4Tint = Settings.Bloom5Tint =
            Settings.Bloom6Tint = GradeBloomTint;
    }

    if (SourceLut)
    {
        Settings.bOverride_ColorGradingLUT = true;
        Settings.ColorGradingLUT = SourceLut;
        Settings.bOverride_ColorGradingIntensity = true;
        Settings.ColorGradingIntensity = 1.f;
    }
}

void UFighterStageComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);

    if (IsLoaded() && bProfilePresentation && FrameIntervalsMs.Num() < 1800)
        if (const auto* Owner = Cast<AFighterArena>(GetOwner()); Owner && Owner->GetCombatState().frame > 60)
        {
            const double Now = FPlatformTime::Seconds();

            if (LastProfileTime > 0)
            {
                FrameIntervalsMs.Add((Now - LastProfileTime) * 1000.);
                GameMs.Add(FPlatformTime::ToMilliseconds(GGameThreadTime));
                RenderMs.Add(FPlatformTime::ToMilliseconds(GRenderThreadTime));
                GpuMs.Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
                DrawCalls.Add(GNumDrawCallsRHI[0]);
                Primitives.Add(GNumPrimitivesDrawnRHI[0]);
            }

            LastProfileTime = Now;
        }
    ActiveWaterParticles.RemoveAll([](const auto& P) { return !P.IsValid(); });

    if (!IsLoaded() || !bWaterStage || Ripples.IsEmpty() || bCinematicWaterHidden)
        return;

    for (int32 I = 0; I < Ripples.Num(); ++I)
    {
        RippleAges[I] += Delta;
        const float T = RippleAges[I] / .7f;
        Ripples[I]->SetVisibility(T < 1);

        if (T < 1)
        {
            Ripples[I]->SetRelativeScale3D(FVector((.25f + T * .9f) * RippleScales[I]));
            RippleMaterials[I]->SetScalarParameterValue(TEXT("RippleOpacity"), .65f * (1 - T));
        }
    }

    const auto* Arena = Cast<AFighterArena>(GetOwner());

    if (!Arena)
        return;
    const auto& State = Arena->GetCombatState();

    if (State.frame <= LastFrame)
    {
        LastFrame = State.frame;

        return;
    }

    const bool Initial = LastFrame == 0;
    LastFrame = State.frame;

    for (int32 I = 0; I < 2; ++I)
    {
        const auto& Fighter = State.fighters[I];
        const FVector2D Position(Fighter.position_x * .1f, Fighter.position_z * .1f);
        RippleCooldown[I] = FMath::Max(0.f, RippleCooldown[I] - Delta);
        const bool Moved = FVector2D::Distance(Position, LastPosition[I]) > 12.f;

        if (!Initial && RippleCooldown[I] == 0 && !modular_fighter::IsAirborne(Fighter) && Fighter.air_height <= 0 &&
            (Moved || Fighter.attack_frame == 1))
        {
            Ripple(Position, 1.f);
            RippleCooldown[I] = .18f;
            LastPosition[I] = Position;
        }

        if (Initial)
            LastPosition[I] = Position;
    }
}

void UFighterStageComponent::SetCinematicWaterHidden(bool Hidden)
{
    if (bCinematicWaterHidden == Hidden)
        return;
    bCinematicWaterHidden = Hidden;

    for (const auto& Surface : CinematicWaterSurfaces)
        if (Surface.IsValid())
            Surface->SetVisibility(!Hidden);

    for (const auto& FX : ActiveWaterParticles)
        if (FX.IsValid())
            FX->SetVisibility(!Hidden);

    if (Hidden)
        for (int32 I = 0; I < Ripples.Num(); ++I)
        {
            Ripples[I]->SetVisibility(false);
            RippleAges[I] = 1.f;
        }
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_CINEMATIC_WATER hidden=%d surfaces=%d"), Hidden ? 1 : 0,
           CinematicWaterSurfaces.Num());
}

namespace
{
constexpr float WaterSurfaceZ = 9.8f;
}

// 캐릭터 위치에 물결을 만든다.
void UFighterStageComponent::Ripple(const FVector2D& Position, float Scale)
{
    if (Ripples.IsEmpty())
        return;
    const int32 Slot = NextRipple++ % Ripples.Num();
    Ripples[Slot]->SetRelativeLocation(FVector(Position.X, Position.Y, WaterSurfaceZ + .4f));
    Ripples[Slot]->SetRelativeScale3D(FVector(.25f * Scale));
    Ripples[Slot]->SetVisibility(true);
    RippleMaterials[Slot]->SetScalarParameterValue(TEXT("RippleOpacity"), .65f);
    RippleAges[Slot] = 0;
    RippleScales[Slot] = Scale;
}

void UFighterStageComponent::NoteFighterBody(int32 Index, const FVector& FootL, const FVector& FootR,
                                             const FVector& Hip, const FVector& Head, float Delta)
{
    if (Index >= 0 && Index < 2)
    {
        CameraTargets[Index * 2] = Hip;
        CameraTargets[Index * 2 + 1] = Head;
    }

    if (!IsLoaded() || !bWaterStage || Index < 0 || Index > 1 || Delta <= 0.f || bCinematicWaterHidden)
        return;
    const FTransform Root = GetOwner()->GetActorTransform();
    const float WaterZ = Root.TransformPosition(FVector(0, 0, WaterSurfaceZ)).Z;
    auto Surface = [&](const FVector& P)
    {
        return FVector(P.X, P.Y, WaterZ + 1.f);
    };
    auto Local2D = [&](const FVector& P)
    {
        const FVector L = Root.InverseTransformPosition(P);

        return FVector2D(L.X, L.Y);
    };
    const FVector Feet[2] = {FootL, FootR};

    for (int32 F = 0; F < 2; ++F)
    {
        FootCooldown[Index][F] = FMath::Max(0.f, FootCooldown[Index][F] - Delta);
        const float Height = Feet[F].Z - WaterZ;
        const float Slide = FVector2D::Distance(FVector2D(Feet[F]), FVector2D(LastFoot[Index][F])) / Delta;
        LastFoot[Index][F] = Feet[F];

        if (Height > 24.f)
        {
            bFootLifted[Index][F] = true;
            continue;
        }

        const bool bLanded = bFootLifted[Index][F] && Height < 16.f;
        const bool bSliding = Height < 16.f && Slide > 320.f && Slide < 3000.f;

        if ((bLanded || bSliding) && FootCooldown[Index][F] == 0.f && WaterStep)
        {
            ActiveWaterParticles.Add(UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                this, WaterStep, Surface(Feet[F]), FRotator::ZeroRotator, FVector(1), true, true));
            Ripple(Local2D(Feet[F]), .7f);
            FootCooldown[Index][F] = bLanded ? .12f : .09f;
            bFootLifted[Index][F] = false;
        }
    }

    BodyCooldown[Index] = FMath::Max(0.f, BodyCooldown[Index] - Delta);
    const FVector& Low = Hip.Z < Head.Z ? Hip : Head;
    const float Body = Low.Z - WaterZ;

    if (bBodyTracked[Index])
    {
        const float Fall = (LastBody[Index] - Body) / Delta;

        if (Body < 32.f && LastBody[Index] >= 32.f && Fall > 150.f && BodyCooldown[Index] == 0.f)
        {
            const int32 Size = Fall > 520.f ? 2 : Fall > 280.f ? 1 : 0;

            if (WaterSplash[Size])
                ActiveWaterParticles.Add(UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, WaterSplash[Size], Surface(Low), FRotator::ZeroRotator, FVector(1), true, true));
            Ripple(Local2D(Low), 1.3f + Size * .6f);
            BodyCooldown[Index] = .3f;
        }
    }

    LastBody[Index] = Body;
    bBodyTracked[Index] = true;
}

void UFighterStageComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (FrameIntervalsMs.Num() > 0)
    {
        auto Stats = [](TArray<double> V, double& Mean, double& P95)
        {
            double Sum = 0;

            for (double X : V)
                Sum += X;
            V.Sort();
            Mean = Sum / V.Num();
            P95 = V[FMath::Min(V.Num() - 1, FMath::FloorToInt(V.Num() * .95))];
        };
        double Wall, WallP, Game, GameP, Render, RenderP, Gpu, GpuP, Draws, DrawsP, Prims, PrimsP;
        Stats(FrameIntervalsMs, Wall, WallP);
        Stats(GameMs, Game, GameP);
        Stats(RenderMs, Render, RenderP);
        Stats(GpuMs, Gpu, GpuP);
        Stats(DrawCalls, Draws, DrawsP);
        Stats(Primitives, Prims, PrimsP);
        UE_LOG(
            LogTemp, Display,
            TEXT(
                "FIGHTER_PRESENTATION_PROFILE stage=%s samples=%d wall_ms=%.2f/%.2f fps=%.1f game_ms=%.2f/%.2f render_ms=%.2f/%.2f gpu_ms=%.2f/%.2f draws=%.0f/%.0f prims=%.0f/%.0f (mean/p95)"),
            *StageKey, FrameIntervalsMs.Num(), Wall, WallP, 1000. / Wall, Game, GameP, Render, RenderP, Gpu, GpuP,
            Draws, DrawsP, Prims, PrimsP);
    }

    for (const auto& Actor : EnvironmentActors)
        if (IsValid(Actor.Get()))
            Actor->Destroy();
    EnvironmentActors.Reset();
    Super::EndPlay(Reason);
}

// 카메라를 가리는 물체를 숨긴다.
void UFighterStageComponent::UpdateCameraOcclusion(const FVector& Eye)
{
    if (!IsLoaded())
        return;

    if (Occluders.IsEmpty())
        for (const auto& Created : CreatedComponents)
            if (auto* Component = Cast<UInstancedStaticMeshComponent>(Created.Get());
                Component && Component->GetStaticMesh())
            {
                const FBox Local = Component->GetStaticMesh()->GetBoundingBox();

                for (int32 I = 0; I < Component->GetInstanceCount(); ++I)
                {
                    FTransform T;
                    Component->GetInstanceTransform(I, T, true);
                    const FBox World = Local.TransformBy(T);

                    if (World.GetExtent().GetMax() > 2500.f || World.Max.Z < 30.f)
                        continue;
                    FTransform Relative;
                    Component->GetInstanceTransform(I, Relative, false);
                    Occluders.Add({Component, I, World.ExpandBy(10.f), Relative, false});
                }
            }
    int32 Changed = 0;

    for (auto& O : Occluders)
    {
        bool bBlocks = false;

        for (const FVector& Target : CameraTargets)
            if (!Target.IsZero() && !O.Box.IsInside(Target) &&
                FMath::LineBoxIntersection(O.Box, Eye, Target, Target - Eye))
            {
                bBlocks = true;
                break;
            }
        if (bBlocks == O.bHidden || !O.Component.IsValid())
            continue;
        O.bHidden = bBlocks;
        ++Changed;
        FTransform T = O.Transform;

        if (bBlocks)
            T.SetScale3D(FVector(0.001f));
        O.Component->UpdateInstanceTransform(O.Index, T, false, false, true);
    }

    if (Changed)
        for (auto& O : Occluders)
            if (O.Component.IsValid())
                O.Component->MarkRenderStateDirty();
}
