// 타격, 히트, 토네이도 이펙트 재생.
#include "FighterImpactComponent.h"
#include "FighterImpactPolicy.h"
#include "FighterImpactProfiles.h"
#include "FighterElectricTrailComponent.h"
#include "FighterCharacterMeshComponent.h"
#include "FighterT6Feedback.h"
#include "FighterArena.h"
#include "Math/RandomStream.h"
#include "FighterImpactCurves.h"
#include "FighterHeatSource.h"
#include "FighterCombatCues.h"
#include "FighterCameraComponent.h"
#include "Components/PointLightComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
constexpr int32 BurstLayers = 20;
}

UFighterImpactComponent::UFighterImpactComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UFighterImpactComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyHandTrail")))
    {
        SourceElectricTrail = NewObject<UFighterElectricTrailComponent>(GetOwner());
        SourceElectricTrail->RegisterComponent();
    }

    Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));

    if (auto* HeatMaterial = LoadObject<UMaterialInterface>(
            nullptr, TEXT("/Game/LocalPresentation/SourceFX/M_HeatSourceSmoke.M_HeatSourceSmoke")))
        for (int32 I = 0; I < 4; ++I)
        {
            auto* Sprite = NewObject<UStaticMeshComponent>(GetOwner());
            GetOwner()->AddInstanceComponent(Sprite);
            Sprite->SetStaticMesh(Plane);
            Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Sprite->SetCastShadow(false);
            auto* Dynamic = UMaterialInstanceDynamic::Create(HeatMaterial, this);
            Dynamic->SetScalarParameterValue(TEXT("HeatOpacity"), 0);
            Sprite->SetMaterial(0, Dynamic);
            Sprite->RegisterComponent();
            Sprite->SetVisibility(false);
            HeatSprites.Add(Sprite);
            HeatMaterials.Add(Dynamic);
            HeatAges.Add(FighterHeatSource::Seconds);
        }
    auto LoadSystem = [](const TCHAR* Name)
    {
        return LoadObject<UNiagaraSystem>(nullptr,
                                          *FString::Printf(TEXT("/Game/LocalPresentation/Niagara/%s.%s"), Name, Name));
    };
    HitCommonSystem = LoadSystem(TEXT("NS_FG_HitCommon"));
    HitGuardSystem = LoadSystem(TEXT("NS_FG_HitGuard"));
    HitStruckSystem = LoadSystem(TEXT("NS_FG_HitStruck"));
    HitWeldSystem = LoadSystem(TEXT("NS_FG_HitWeld"));

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyHitVAT")))
        HitVatSystem = LoadSystem(TEXT("NS_FG_HitVAT"));
    UE_LOG(LogTemp, Display,
           TEXT("FIGHTER_HIT_VAT_SOURCE enabled=%d frames=59 source_mesh=1 source_curves=3 clock_and_shader=adapter"),
           HitVatSystem ? 1 : 0);

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyHitLayers")))
        HitSourceLayersSystem = LoadSystem(TEXT("NS_FG_HitSourceLayers"));
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_HIT_LAYERS_SOURCE enabled=%d layers=4 source_curves=10 total_with_vat=5 omitted=3 shader_and_spawn=adapter"),
        HitSourceLayersSystem ? 1 : 0);

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyGuardFX")))
        if (auto* SourceGuard = LoadSystem(TEXT("NS_FG_GuardSource")))
        {
            HitGuardSystem = SourceGuard;
            bSourceGuard = true;
        }
    UE_LOG(LogTemp, Display,
           TEXT("FIGHTER_GUARD_SOURCE enabled=%d composition=8sprite_1mesh timing=authored shader=adapter"),
           bSourceGuard ? 1 : 0);

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyCounterFX")))
    {
        CounterLocalSystem = LoadSystem(TEXT("NS_FG_CounterSourceLocal"));
        CounterWorldSystem = LoadSystem(TEXT("NS_FG_CounterSourceWorld"));
    }

    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_COUNTER_SOURCE enabled=%d local_layers=21 world_layers=5 trigger=project_core_counter timing=authored shader=adapter"),
        CounterLocalSystem && CounterWorldSystem ? 1 : 0);

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyTornadoFX")))
    {
        TornadoLocalSystem = LoadSystem(TEXT("NS_FG_TornadoSourceLocal"));
        TornadoWorldSystem = LoadSystem(TEXT("NS_FG_TornadoSourceWorld"));
    }

    UE_LOG(
        LogTemp, Display,
        TEXT("FIGHTER_TORNADO_SOURCE enabled=%d mesh_layers=4 omitted_source_emitters=2 trigger=core_screw_transition"),
        TornadoLocalSystem && TornadoWorldSystem ? 1 : 0);
    HitPaulSystem = LoadSystem(TEXT("NS_FG_HitPaul"));
    HitKazuyaSystem = LoadSystem(TEXT("NS_FG_HitKazuya"));
    HeatBurstSystem = LoadSystem(TEXT("NS_FG_HeatBurst"));
    KazuyaTrailSystem = LoadSystem(TEXT("NS_FG_KazuyaTrail"));

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyPaulFX")))
        PaulSourceSystem = LoadSystem(TEXT("NS_FG_PaulSourceHit"));

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterNoCriticalSource")))
        CriticalSourceSystem = LoadSystem(TEXT("NS_FG_CriticalSource"));

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyKazuyaFX")))
        KazuyaSourceSystem = LoadSystem(TEXT("NS_FG_KazuyaSourceHit"));
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_KAZUYA_SOURCE enabled=%d source=ns_ef_GRL_01 adapted_emitters=5 omitted=1 vm_constants=1 trigger=signature_non_guard"),
        KazuyaSourceSystem ? 1 : 0);
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_CRITICAL_SOURCE enabled=%d source=ns_ef_cmncritical01 adapted_emitters=8 omitted=1 trigger=heavy_or_signature_non_counter"),
        CriticalSourceSystem ? 1 : 0);

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterNoPaulWind")))
        for (const uint16 Id : FighterImpactProfiles::WindEffectIds)
            if (auto* System = LoadSystem(*FString::Printf(TEXT("NS_FG_PaulWind%d"), Id)))
                PaulWindSystems.Add(Id, System);
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_PAUL_WIND_SOURCE enabled=%d systems=%d rules=%d trigger=motbin_0x8039_rows slot_bones=inferred spawn_and_shader=adapter"),
        PaulWindSystems.IsEmpty() ? 0 : 1, PaulWindSystems.Num(), FighterImpactProfiles::WindRuleCount);

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterLegacyHeatFX")))
    {
        HeatEngagerSystem = LoadSystem(TEXT("NS_FG_HeatEngagerSource"));
        HeatDashLocalSystem = LoadSystem(TEXT("NS_FG_HeatDashSourceLocal"));
        HeatDashWorldSystem = LoadSystem(TEXT("NS_FG_HeatDashSourceWorld"));
        HeatAuraSystem = LoadSystem(TEXT("NS_FG_HeatAuraSource"));
        HeatEndSystem = LoadSystem(TEXT("NS_FG_HeatEndSource"));
    }

    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_HEAT_SOURCE engager=%d dash_local=%d dash_world=%d aura=%d end=%d paul=%d shader_and_spawn=adapter"),
        HeatEngagerSystem ? 1 : 0, HeatDashLocalSystem ? 1 : 0, HeatDashWorldSystem ? 1 : 0, HeatAuraSystem ? 1 : 0,
        HeatEndSystem ? 1 : 0, PaulSourceSystem ? 1 : 0);
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_NIAGARA common=%d guard=%d paul=%d kazuya=%d heat=%d"),
           HitCommonSystem ? 1 : 0, HitGuardSystem ? 1 : 0, HitPaulSystem ? 1 : 0, HitKazuyaSystem ? 1 : 0,
           HeatBurstSystem ? 1 : 0);
    SparkMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LocalPresentation/Materials/M_HitSpark.M_HitSpark"));
    GlowMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LocalPresentation/Materials/M_HitGlow.M_HitGlow"));
    KazuyaMaterial = LoadObject<UMaterialInterface>(
        nullptr, TEXT("/Game/LocalPresentation/Materials/M_KazuyaElectric.M_KazuyaElectric"));
    PaulMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LocalPresentation/Materials/M_PaulEnergy.M_PaulEnergy"));
    PaulFireMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/LocalPresentation/Materials/M_PaulFire.M_PaulFire"));

    for (const TCHAR* Name : {TEXT("SM_ef_GRL_Lightning_01"), TEXT("SM_EF_Grf_Sphere"), TEXT("SM_EF_Grf_Disk"),
                              TEXT("SM_ef_GRL_Lightning_02"), TEXT("SM_ef_GRL_Lightning_04"), TEXT("SM_EF_Grf_Stream")})
        SourceMeshes.Add(
            LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/SourceFX/%s"), Name)));

    for (const TCHAR* Name : {TEXT("M_KazuyaArc"), TEXT("M_PaulSphere"), TEXT("M_PaulDisk"), TEXT("M_KazuyaArc"),
                              TEXT("M_KazuyaArc"), TEXT("M_PaulStream")})
        SourceMaterials.Add(LoadObject<UMaterialInterface>(
            nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/SourceFX/%s"), Name)));
    UE_LOG(LogTemp, Display,
           TEXT("FIGHTER_SOURCE_VFX ready=%d source_mesh_layers=6 authored_envelopes=1 bold_source_meshes=1"),
           HasResources() ? 1 : 0);

    if (HasResources())
        for (int32 I = 0; I < 6 * BurstLayers; ++I)
        {
            auto* Sprite = NewObject<UStaticMeshComponent>(GetOwner());
            GetOwner()->AddInstanceComponent(Sprite);
            const int32 Layer = I % BurstLayers;
            Sprite->SetStaticMesh(Layer >= 2 && Layer <= 7 ? SourceMeshes[Layer - 2].Get() : Plane.Get());
            Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Sprite->SetCastShadow(false);
            UMaterialInterface* Base = Layer >= 2 && Layer <= 7 ? SourceMaterials[Layer - 2].Get() : GlowMaterial.Get();
            auto* Material = UMaterialInstanceDynamic::Create(Base, this);
            Material->SetScalarParameterValue(TEXT("ImpactOpacity"), 0);
            Sprite->SetMaterial(0, Material);
            Sprite->RegisterComponent();
            Sprite->SetWorldLocation(FVector(0, 0, 110));
            Sprite->SetTranslucentSortPriority(10 + Layer);
            Sprites.Add(Sprite);
            SpriteMaterials.Add(Material);
            Ages.Add(1);
            Sizes.Add(1);
            Rolls.Add(0);
            Aspects.Add(FVector(1));
            Origins.Add(FVector::ZeroVector);
            Flights.Add(FVector::ZeroVector);
        }

    FlashLight = NewObject<UPointLightComponent>(GetOwner());
    GetOwner()->AddInstanceComponent(FlashLight);
    FlashLight->SetCastShadows(false);
    FlashLight->SetAttenuationRadius(420.f);
    FlashLight->SetIntensityUnits(ELightUnits::Unitless);
    FlashLight->SetIntensity(0.f);
    FlashLight->RegisterComponent();
}

bool UFighterImpactComponent::HasResources() const
{
    if (!Plane || !GlowMaterial || SourceMeshes.Num() != 6 || SourceMaterials.Num() != 6)
        return false;

    for (int32 I = 0; I < 6; ++I)
        if (!SourceMeshes[I] || !SourceMaterials[I])
            return false;

    return true;
}

// 타격 큐에 맞는 이펙트를 재생한다.
void UFighterImpactComponent::Play(const FGameplayTag& Tag, const FVector& Location, float Damage,
                                   uint8 AttackerCharacter, const FVector& AttackerLocation, bool bCharacterEffect,
                                   float SourcePower, uint32 VariationSeed, int32 Target)
{
    using namespace FighterGameplayTags;

    if (Tag == GameplayCue_Fighter_KO || !HasResources() || Sprites.IsEmpty())
        return;
    const bool Guard = Tag == GameplayCue_Fighter_Guard;
    const bool bCounterLayersOnly = Tag == GameplayCue_Fighter_CounterHit && CounterLocalSystem && CounterWorldSystem &&
                                    FParse::Param(FCommandLine::Get(), TEXT("FighterCounterLayersOnly"));
    const auto Plan =
        FighterImpactPolicy::Select(SourcePower, Guard, Tag == GameplayCue_Fighter_CounterHit, bCharacterEffect);
    const bool Heavy =
        Plan.Level == FighterImpactPolicy::Tier::Heavy || Plan.Level == FighterImpactPolicy::Tier::Signature;
    const bool bSourcePaul = PaulSourceSystem && AttackerCharacter == 2 && Plan.CharacterMesh && !Guard;
    const bool bSourceKazuya = KazuyaSourceSystem && AttackerCharacter == 1 && Plan.CharacterMesh && !Guard;
    FRandomStream Variation(static_cast<int32>(VariationSeed));
    const FLinearColor Color = Guard ? FLinearColor(.3f, 1.4f, 3.2f) : FLinearColor(3.f, .025f, .008f);
    ++ImpactCount;

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterTornadoLayersOnly")))
        return;
    const bool bHitVatOnly = FParse::Param(FCommandLine::Get(), TEXT("FighterHitVATLayersOnly"));
    const bool bHitSourceOnly = FParse::Param(FCommandLine::Get(), TEXT("FighterHitSourceLayersOnly"));
    const bool bUseSourceHit = HitSourceLayersSystem && !Guard && !bCounterLayersOnly && !bHitVatOnly;

    if ((HitVatSystem || bUseSourceHit) && !Guard && !bCounterLayersOnly)
    {
        const auto* VatPC = GetWorld()->GetFirstPlayerController();
        const FVector VatView = VatPC && VatPC->PlayerCameraManager
                                    ? (VatPC->PlayerCameraManager->GetCameraLocation() - Location).GetSafeNormal()
                                    : FVector(0, 1, 0);
        const FVector VatAway = (Location - AttackerLocation).GetSafeNormal2D();
        const FRotator VatFacing =
            FRotationMatrix::MakeFromZ((VatAway * .75f + VatView * .55f + FVector(0, 0, .15f)).GetSafeNormal())
                .Rotator();

        if (HitVatSystem && !bHitSourceOnly)
        {
            auto* Vat = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                this, HitVatSystem, Location, VatFacing, FVector(Plan.ContactScale * (bSourcePaul ? .85f : 1.f)), true,
                true);

            if (Vat)
                HitVatEffects.Add(Vat);
            UE_LOG(LogTemp, Display, TEXT("FIGHTER_HIT_VAT_PLAY target=%d active=%d tag=%s seed=%u"), Target,
                   Vat ? 1 : 0, *Tag.ToString(), VariationSeed);
        }

        if (bUseSourceHit)
        {
            auto* Layers = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                this, HitSourceLayersSystem, Location, VatFacing, FVector(Plan.ContactScale), true, true);

            if (Layers)
                HitSourceLayerEffects.Add(Layers);
            UE_LOG(LogTemp, Display,
                   TEXT("FIGHTER_HIT_LAYERS_PLAY target=%d active=%d tag=%s seed=%u replaces_common_struck_weld=1"),
                   Target, Layers ? 1 : 0, *Tag.ToString(), VariationSeed);
        }

        if (!bHitVatCaptured && FParse::Param(FCommandLine::Get(), TEXT("FighterHitVATCapture")))
        {
            bHitVatCaptured = true;
            HitVatCaptureTick = 0;
        }
    }

    if (bHitVatOnly || bHitSourceOnly)
        return;
    const int32 Start = NextSprite;
    NextSprite = (NextSprite + BurstLayers) % Sprites.Num();

    const float FanOffset = Variation.FRandRange(0.f, 30.f);
    const float Strength = Plan.Strength;
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_IMPACT_POLICY tier=%s source_power=%.0f hp_loss=%.0f strength=%.2f contact_scale=%.2f struck_scale=%.2f weld=%d character_mesh=%d seed=%u t6_strength_pair=%d selector=t6_20_22_23 mapping=t8_resource_adapter"),
        UTF8_TO_TCHAR(FighterImpactPolicy::Name(Plan.Level)), SourcePower, Damage, Strength, Plan.ContactScale,
        Plan.StruckScale, Plan.Weld ? 1 : 0, Plan.CharacterMesh ? 1 : 0, VariationSeed, int32(Plan.T6StrengthPair));

    if (FlashLight)
    {
        FlashLight->SetWorldLocation(Location);
        FlashLight->SetLightColor(Guard                    ? FLinearColor(.9f, .94f, 1.f)
                                  : AttackerCharacter == 2 ? FLinearColor(1.f, .42f, .08f)
                                  : AttackerCharacter == 1 ? FLinearColor(.55f, .45f, 1.f)
                                                           : FLinearColor(1.f, .55f, .3f));

        FlashPeak = (Guard && bSourceGuard) || bCounterLayersOnly ? 0.f : (Heavy ? 70.f : 30.f) * Strength;
        FlashAge = 0.f;
    }

    const bool bNiagara = HitCommonSystem && HitGuardSystem;

    if (bNiagara)
    {
        const auto* ViewPC = GetWorld()->GetFirstPlayerController();
        const FVector ToView = ViewPC && ViewPC->PlayerCameraManager
                                   ? (ViewPC->PlayerCameraManager->GetCameraLocation() - Location).GetSafeNormal()
                                   : FVector(0, 1, 0);
        const FVector Away = (Location - AttackerLocation).GetSafeNormal2D();
        const FRotator Facing =
            FRotationMatrix::MakeFromZ((Away * .75f + ToView * .55f + FVector(0, 0, .15f)).GetSafeNormal()).Rotator();

        UNiagaraSystem* System = Guard ? HitGuardSystem.Get() : HitCommonSystem.Get();
        auto* ContactEffect =
            bCounterLayersOnly || bUseSourceHit
                ? nullptr
                : UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, Location + ToView * 12.f, Facing,
                                                                 FVector(Plan.ContactScale), true, true);

        if (Guard && ContactEffect)
        {
            GuardEffects.RemoveAll([](const auto& Effect) { return !Effect.IsValid(); });
            GuardEffects.Add(ContactEffect);
        }

        if (!Guard)
        {
            const auto* Arena = Cast<AFighterArena>(GetOwner());
            const bool bSourceCounter = Tag == GameplayCue_Fighter_CounterHit && CounterLocalSystem &&
                                        CounterWorldSystem && Arena && Target >= 0 && Target < 2;

            UNiagaraSystem* CharacterSystem = AttackerCharacter == 2   ? HitPaulSystem.Get()
                                              : AttackerCharacter == 1 ? HitKazuyaSystem.Get()
                                                                       : nullptr;

            if (bSourcePaul && !bCounterLayersOnly)
            {
                auto* Paul = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, PaulSourceSystem, Location, Away.Rotation(), FVector(Plan.ContactScale), true, true);

                if (Paul)
                    PaulSourceEffects.Add(Paul);
                UE_LOG(
                    LogTemp, Display,
                    TEXT(
                        "FIGHTER_PAUL_SOURCE_PLAY active=%d target=%d layers=6 source_layers=6 video_billow=1 vm_constants=1 camera_offset=50 source=NS_EF_Grf_01"),
                    Paul ? 1 : 0, Target);
            }

            if (Plan.T6StrengthPair && CriticalSourceSystem && !bCounterLayersOnly && !bSourceCounter)
            {
                auto* Critical = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, CriticalSourceSystem, Location + ToView * 10.f, FRotationMatrix::MakeFromZ(ToView).Rotator(),
                    FVector(Plan.ContactScale * (Plan.Level == FighterImpactPolicy::Tier::Signature ? 1.15f : 1.f)),
                    true, true);

                if (Critical)
                    CriticalSourceEffects.Add(Critical);
                UE_LOG(
                    LogTemp, Display,
                    TEXT(
                        "FIGHTER_CRITICAL_SOURCE_PLAY active=%d target=%d tier=%s attacker=%d t6_pair=%d t8_resource_mapping=shared_critical"),
                    Critical ? 1 : 0, Target, UTF8_TO_TCHAR(FighterImpactPolicy::Name(Plan.Level)), AttackerCharacter,
                    int32(Plan.T6StrengthPair));
            }

            if (bSourceKazuya && !bCounterLayersOnly)
            {
                auto* Kz = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, KazuyaSourceSystem, Location, FRotator::ZeroRotator, FVector(Plan.ContactScale), true, true);

                if (Kz)
                    KazuyaSourceEffects.Add(Kz);
                UE_LOG(
                    LogTemp, Display,
                    TEXT(
                        "FIGHTER_KAZUYA_SOURCE_PLAY active=%d target=%d layers=8 source_emitters=5 vm_constants=1 source=ns_ef_GRL_01"),
                    Kz ? 1 : 0, Target);
            }

            if (CharacterSystem && !bCounterLayersOnly && !bSourcePaul && !bSourceKazuya)

                UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, CharacterSystem, Location + ToView * 13.f, Facing,
                    FVector(Plan.ContactScale * (bUseSourceHit ? .72f : 1.2f)), true, true);

            if (!bSourceCounter && !bUseSourceHit && HitStruckSystem)
                UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitStruckSystem, Location + ToView * 16.f, Facing,
                                                               FVector(Plan.StruckScale), true, true);

            if (!bSourceCounter && !bUseSourceHit && Plan.Weld && HitWeldSystem)
                UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, HitWeldSystem, Location - Away * 10.f + ToView * 8.f,
                    FRotationMatrix::MakeFromZ((Away + FVector(0, 0, .25f)).GetSafeNormal()).Rotator(),
                    FVector(FMath::Clamp(.8f + .2f * Strength, .8f, 1.2f)), true, true);

            if (bSourceCounter)
            {
                const auto& F = Arena->GetCombatState().fighters[Target];
                const FVector Root(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f);
                const FVector Contact = Location + ToView * 16.f;
                auto* Local = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CounterLocalSystem, Contact, Facing,
                                                                             FVector(1), true, true);
                auto* World = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, CounterWorldSystem, Contact, Facing,
                                                                             FVector(1), true, true);

                if (Local)
                    CounterEffects.Add({Local, Target, Contact - Root});

                if (World)
                    CounterEffects.Add({World, INDEX_NONE, FVector::ZeroVector});
                UE_LOG(LogTemp, Display, TEXT("FIGHTER_COUNTER_PLAY target=%d local=%d world=%d tag=%s"), Target,
                       Local ? 1 : 0, World ? 1 : 0, *Tag.ToString());
            }
        }

        ++NiagaraSpawnCount;
    }

    for (int32 Layer = 0; Layer < BurstLayers; ++Layer)
    {
        const bool Electric = Layer == 2 || Layer == 5 || Layer == 6;
        const bool Fire = Layer == 3 || Layer == 4 || Layer == 7;
        const int32 Slot = (Start + Layer) % Sprites.Num();
        auto* Sprite = Sprites[Slot].Get();
        auto* Material = SpriteMaterials[Slot].Get();

        Ages[Slot] = 1;
        Sprite->SetVisibility(false);

        if (bCounterLayersOnly)
            continue;

        if (Fire && bSourcePaul)
            continue;

        if (Electric && bSourceKazuya)
            continue;

        if ((Electric && (AttackerCharacter != 1 || !Plan.CharacterMesh)) ||
            (Fire && (AttackerCharacter != 2 || !Plan.CharacterMesh)))
            continue;

        if (bNiagara && !Electric && !Fire)
            continue;
        const bool Streak = Layer >= 8;
        const FLinearColor FireTint = bUseSourceHit ? FLinearColor(3.f, .75f, .035f) : FLinearColor(8.f, 2.f, .08f);
        const FLinearColor Tint = Electric                           ? FLinearColor(2.4f, 3.4f, 12.f)
                                  : Fire                             ? FireTint
                                  : Layer == 1                       ? FLinearColor(6.f, 3.f, 2.f)
                                  : Streak && AttackerCharacter == 2 ? FLinearColor(8.f, 2.3f, .18f)
                                                                     : Color;
        Material->SetVectorParameterValue(TEXT("ImpactColor"), Tint);
        Material->SetScalarParameterValue(TEXT("ImpactFrame"), 0);
        Material->SetScalarParameterValue(TEXT("ImpactOpacity"), 1);
        Material->SetScalarParameterValue(TEXT("ImpactAge"), 0);
        Sprite->SetVisibility(true);
        const auto* PC = GetWorld()->GetFirstPlayerController();
        const FVector ToCamera = PC && PC->PlayerCameraManager
                                     ? (PC->PlayerCameraManager->GetCameraLocation() - Location).GetSafeNormal()
                                     : FVector(0, 1, 0);

        Origins[Slot] = Location + ToCamera * (Electric ? 18.f : 35.f);
        Sprite->SetWorldLocation(Origins[Slot]);

        Rolls[Slot] = Streak       ? (Layer - 8) * 30.f + FanOffset + Variation.FRandRange(-9.f, 9.f)
                      : Layer == 5 ? Variation.FRandRange(40.f, 90.f)
                      : Electric   ? Variation.FRandRange(-25.f, 25.f)
                                   : 0.f;

        const FVector Side = FVector::CrossProduct(ToCamera, FVector::UpVector).GetSafeNormal();
        const FVector Up = FVector::CrossProduct(Side, ToCamera).GetSafeNormal();
        const float Radians = FMath::DegreesToRadians(Rolls[Slot]);
        Flights[Slot] = Streak ? (Side * FMath::Cos(Radians) + Up * FMath::Sin(Radians)) *
                                     Variation.FRandRange(35.f, 70.f) * Strength
                               : FVector::ZeroVector;
        const FVector Extent = Sprite->GetStaticMesh()->GetBounds().BoxExtent;
        FVector Normal = FVector::UpVector;

        if (Layer >= 2 && Layer <= 7)
            Normal = Extent.X < Extent.Y && Extent.X < Extent.Z ? FVector::ForwardVector
                     : Extent.Y < Extent.Z                      ? FVector::RightVector
                                                                : FVector::UpVector;
        Sprite->SetWorldRotation(FQuat(ToCamera, FMath::DegreesToRadians(Rolls[Slot])) *
                                 FQuat::FindBetweenNormals(Normal, ToCamera));
        const FVector Aspect = Streak ? FVector(Variation.FRandRange(.9f, 1.6f), .025f, 1) : FVector(1);
        Aspects[Slot] = Aspect;

        const float Diameter = Electric     ? (Layer == 6 ? 100.f : 155.f)
                               : Layer == 3 ? (bUseSourceHit ? 34.f : 50.f)
                               : Layer == 4 ? (bUseSourceHit ? 72.f : 110.f)
                               : Layer == 7 ? (bUseSourceHit ? 90.f : 135.f)
                               : Streak     ? 125.f
                               : Layer == 1 ? 28.f
                                            : 65.f;
        Sizes[Slot] = Diameter / (2 * Extent.GetMax()) * (Heavy ? 1.f : .78f);
        Sprite->SetWorldScale3D(Aspect * (Sizes[Slot] * .85f));
        Ages[Slot] = 0;

        if (ImpactCount == 1)
            UE_LOG(LogTemp, Display, TEXT("FIGHTER_IMPACT sprite=%s location=%s rotation=%s material=%s blend=%d"),
                   *Sprite->GetName(), *Sprite->GetComponentLocation().ToString(),
                   *Sprite->GetComponentRotation().ToString(), *GetNameSafe(Material), int32(Material->GetBlendMode()));
    }

    if (ImpactCount == 1 && FParse::Param(FCommandLine::Get(), TEXT("FighterImpactCapture")))
        CaptureDelay = 2;

    if (bCharacterEffect && !Guard && !bSpecialCaptured &&
        FParse::Param(FCommandLine::Get(), TEXT("FighterSpecialImpactCapture")))
    {
        bSpecialCaptured = true;
        CaptureDelay = 2;
    }
}

void UFighterImpactComponent::ResetFeedback()
{
    for (int32 I = 0; I < 2; ++I)
    {
        PaulSmashLastFrame[I] = 0;
        PaulSmashPlayedMask[I] = 0;
        Paul666Slammed[I] = false;
    }

    PaulSmashCaptureDelay = 0;

    if (SourceElectricTrail)
        SourceElectricTrail->ResetTrails();

    for (int32 I = 0; I < 2; ++I)
        bTrailActive[I] = false;

    if (auto* Camera = GetOwner()->FindComponentByClass<UFighterCameraComponent>())
        Camera->ResetDeathfistFocus();

    for (auto& Effect : PaulSourceEffects)
        if (Effect.IsValid())
            Effect->DestroyComponent();
    PaulSourceEffects.Reset();

    for (auto& Effect : KazuyaSourceEffects)
        if (Effect.IsValid())
            Effect->DestroyComponent();
    KazuyaSourceEffects.Reset();

    for (auto& Effect : PaulWindEffects)
        if (Effect.Component.IsValid())
            Effect.Component->DestroyComponent();
    PaulWindEffects.Reset();
    PaulWindQueue.Reset();
    PaulWindCaptureDelay = 0;

    for (auto& Effect : HeatStageEffects)
        if (Effect.Component.IsValid())
            Effect.Component->DestroyComponent();
    HeatStageEffects.Reset();
    HeatCaptures.Reset();
    HeatCapturedMask = 0;
    HeatCaptureDelay = 0;

    for (int32 I = 0; I < 2; ++I)
    {
        for (auto& Effect : HeatAuraEffects[I])
            if (Effect.IsValid())
                Effect->DestroyComponent();
        HeatAuraEffects[I].Reset();
        bHeatAuraActive[I] = false;
        HeatAuraLastFrame[I] = 0;
    }

    for (int32 I = 0; I < HeatSprites.Num(); ++I)
    {
        HeatAges[I] = FighterHeatSource::Seconds;
        HeatSprites[I]->SetVisibility(false);
    }

    for (auto& Effect : HitSourceLayerEffects)
        if (Effect.IsValid())
            Effect->DestroyComponent();
    HitSourceLayerEffects.Reset();

    for (auto& Effect : CriticalSourceEffects)
        if (Effect.IsValid())
            Effect->DestroyComponent();
    CriticalSourceEffects.Reset();

    for (auto& Effect : HitVatEffects)
        if (Effect.IsValid())
            Effect->DestroyComponent();
    HitVatEffects.Reset();
    HitVatCaptureTick = -1;
    bHitVatCaptured = false;

    for (auto& Effect : GuardEffects)
        if (Effect.IsValid())
            Effect->DestroyComponent();
    GuardEffects.Reset();

    for (auto& Effect : CounterEffects)
        if (Effect.Component.IsValid())
            Effect.Component->DestroyComponent();
    CounterEffects.Reset();

    for (auto& Effect : TornadoEffects)
        if (Effect.Component.IsValid())
            Effect.Component->DestroyComponent();
    TornadoEffects.Reset();
    TornadoCaptureDelay = 0;
    CameraPulse.Reset();

    for (auto& Pulse : HealthPulses)
        Pulse.Reset();
    FeedbackLastCombatFrame = 0;
    T6CameraMode = 0;
}

FVector2D UFighterImpactComponent::GetHealthBarOffset(int32 Target) const
{
    if (Target < 0 || Target > 1)
        return FVector2D::ZeroVector;
    const auto& P = HealthPulses[Target];

    return FVector2D(P.Direction * P.Settings.BarPixels * P.Kick(), -.35f * P.Settings.BarPixels * P.Kick());
}

float UFighterImpactComponent::GetHealthBarFlash(int32 Target) const
{
    if (Target < 0 || Target > 1)
        return 0;
    const auto& P = HealthPulses[Target];

    return P.Active() ? FMath::Max(0.f, 1.f - P.Age / 3.f) : 0.f;
}

// 히트스톱과 화면 흔들림을 적용한다.
void UFighterImpactComponent::PlayFeedback(const FGameplayTag& Tag, int32 Target, float SourcePower, bool bSignature,
                                           bool bAirborne, const FVector& Location, const FVector& Away)
{
    using namespace FighterGameplayTags;

    int32 ForcedMode = 0;
    const bool Forced = FParse::Value(FCommandLine::Get(), TEXT("FighterT6CameraMode="), ForcedMode) &&
                        FighterT6Feedback::CameraSampleCount(ForcedMode) > 0;
    const bool CameraDisabled = FParse::Param(FCommandLine::Get(), TEXT("FighterDisableCameraShake")) ||
                                FParse::Param(FCommandLine::Get(), TEXT("FighterDisableHitFeedback"));

    if (Target >= 0 && Target < 2 && Tag != GameplayCue_Fighter_Guard && Tag != GameplayCue_Fighter_KO &&
        !CameraDisabled)
        if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
        {
            T6CameraMode = Forced ? ForcedMode : 3;
            T6CameraStartFrame = Arena->GetCombatState().frame;
            UE_LOG(
                LogTemp, Display,
                TEXT(
                    "FIGHTER_T6_CAMERA mode=%d frame=%u forced_reference_test=%d native_waveform=1 project_trigger=%d"),
                T6CameraMode, T6CameraStartFrame, Forced ? 1 : 0, Forced ? 0 : 1);

            if (!bFeedbackCaptured && FParse::Param(FCommandLine::Get(), TEXT("FighterFeedbackCapture")) &&
                (!FParse::Param(FCommandLine::Get(), TEXT("FighterSpecialImpactCapture")) || bSignature))
            {
                bFeedbackCaptured = true;
                FeedbackCaptureAge = 0;
                FeedbackCaptureTarget = Target;
            }
        }

    if (!FParse::Param(FCommandLine::Get(), TEXT("FighterExperimentalHitFeedback")) || Target < 0 || Target > 1 ||
        Tag == GameplayCue_Fighter_KO || FParse::Param(FCommandLine::Get(), TEXT("FighterDisableHitFeedback")))
        return;
    const bool Guard = Tag == GameplayCue_Fighter_Guard;
    using namespace FighterHitFeedback;
    const auto KindValue = Guard                                                        ? Kind::Guard
                           : bSignature                                                 ? Kind::Signature
                           : Tag == GameplayCue_Fighter_CounterHit || SourcePower >= 20 ? Kind::Heavy
                                                                                        : Kind::Light;
    const auto Profile = MakeProfile(KindValue, bAirborne);
    auto* PC = GetWorld()->GetFirstPlayerController();
    auto* Camera = PC ? UFighterCameraComponent::FindCameraComponent(PC->GetViewTarget()) : nullptr;
    const float Direction = Camera ? FVector::DotProduct(Away, Camera->GetRightVector()) : Target == 0 ? -1.f : 1.f;

    if (Profile.CameraCm >= CameraPulse.Settings.CameraCm * CameraPulse.Fade())
        CameraPulse.Start(Profile, Direction);

    if (!Guard)
        HealthPulses[Target].Start(Profile, Target == 0 ? -1.f : 1.f);

    if (Camera && Profile.Blur > 0)
    {
        FVector2D Screen = FVector2D::ZeroVector;
        int32 W = 0, H = 0;
        PC->GetViewportSize(W, H);
        const bool Projected = W > 0 && H > 0 && PC->ProjectWorldLocationToScreen(Location, Screen);
        Camera->AddHitBlur(Profile.Blur, Projected ? FVector2D(Screen.X / W, Screen.Y / H) : FVector2D(.5f, .5f));
    }

    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_HIT_FEEDBACK target=%d kind=%d air=%d camera_cm=%.3f frames=%.0f fov=%.3f blur=%.4f bar_px=%.1f authored=1"),
        Target, int32(KindValue), bAirborne ? 1 : 0, Profile.CameraCm, Profile.Frames, Profile.FovDegrees, Profile.Blur,
        Profile.BarPixels);

    if (!Guard && !bFeedbackCaptured && FParse::Param(FCommandLine::Get(), TEXT("FighterFeedbackCapture")) &&
        (!FParse::Param(FCommandLine::Get(), TEXT("FighterSpecialImpactCapture")) || bSignature))
    {
        bFeedbackCaptured = true;
        FeedbackCaptureAge = 0;
        FeedbackCaptureTarget = Target;
    }
}

void UFighterImpactComponent::PlayTornado(int32 Target, const FVector& Location)
{
    if (FParse::Param(FCommandLine::Get(), TEXT("FighterTornadoCapture")))
        TornadoCaptureDelay = 6;
    const auto* Arena = Cast<AFighterArena>(GetOwner());

    if (!Arena || Target < 0 || Target > 1 || !TornadoLocalSystem || !TornadoWorldSystem)
        return;
    const auto& F = Arena->GetCombatState().fighters[Target];
    const auto& A = Arena->GetCombatState().fighters[1 - Target];
    const FVector Root(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f);
    const FVector Away(F.position_x - A.position_x, F.position_z - A.position_z, 0);

    const FRotator Rotation = FRotationMatrix::MakeFromZ(Away.GetSafeNormal()).Rotator();
    auto* Local = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TornadoLocalSystem, Location, Rotation,
                                                                 FVector(1), true, true);
    auto* World = UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TornadoWorldSystem, Location, Rotation,
                                                                 FVector(1), true, true);

    if (Local)
        TornadoEffects.Add({Local, Target, Location - Root});

    if (World)
        TornadoEffects.Add({World, INDEX_NONE, FVector::ZeroVector});
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_TORNADO_PLAY frame=%u target=%d local=%d world=%d flags=%d origin=%s"),
           Arena->GetCombatState().frame, Target, Local ? 1 : 0, World ? 1 : 0, int32(F.combo_flags),
           *Location.ToString());
}

// 히트 발동 연출을 재생한다.
void UFighterImpactComponent::PlayHeat(const FVector& Location)
{
    if (HeatBurstSystem)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HeatBurstSystem, Location, FRotator::ZeroRotator,
                                                       FVector(1), true, true);

    if (HeatSprites.IsEmpty())
        return;
    const int32 Slot = HeatCount++ % HeatSprites.Num();
    HeatAges[Slot] = 0.f;
    HeatSprites[Slot]->SetWorldLocation(Location);
    HeatSprites[Slot]->SetVisibility(true);
    HeatSprites[Slot]->SetWorldScale3D(FVector(2.2f));
    HeatMaterials[Slot]->SetScalarParameterValue(TEXT("HeatAge"), 0.f);
    HeatMaterials[Slot]->SetScalarParameterValue(TEXT("HeatOpacity"), 1.f);

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterHeatCapture")))
        HeatCaptureDelay = 7;
}

void UFighterImpactComponent::PlayHeatStage(EFighterHeatEffect Kind, int32 Fighter, const FVector& Location)
{
    const auto* Arena = Cast<AFighterArena>(GetOwner());

    if (!Arena || Fighter < 0 || Fighter > 1)
        return;
    const auto& F = Arena->GetCombatState().fighters[Fighter];
    const FVector Root(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f);
    const FRotator Facing = FVector(F.face_cos / 65536.f, F.face_sin / 65536.f, 0).Rotation();
    int32 Created = 0;
    auto Spawn = [&](UNiagaraSystem* System, const FVector& At, bool Follow)
    {
        if (!System)
            return;
        auto* Component =
            UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, At, Facing, FVector(1), true, true);

        if (Component)
        {
            HeatStageEffects.Add({Component, Follow ? Fighter : INDEX_NONE, At - Root});
            ++Created;
        }
    };

    switch (Kind)
    {
    case EFighterHeatEffect::Engager:
        if (HeatEngagerSystem)
            Spawn(HeatEngagerSystem, Root + FVector(0, 0, 60), false);
        else
            PlayHeat(Root + FVector(0, 0, 100));
        break;
    case EFighterHeatEffect::Dash:
    case EFighterHeatEffect::AutoRun:
        if (HeatDashLocalSystem && HeatDashWorldSystem)
        {
            Spawn(HeatDashLocalSystem, Root + FVector(0, 0, 75), true);
            Spawn(HeatDashWorldSystem, Root + FVector(0, 0, 75), false);
        }
        else if (Kind == EFighterHeatEffect::Dash)
            PlayHeat(Root + FVector(0, 0, 20));
        break;
    case EFighterHeatEffect::End:

        for (auto& Effect : HeatAuraEffects[Fighter])
            if (Effect.IsValid())
                Effect->DestroyComponent();
        HeatAuraEffects[Fighter].Reset();
        bHeatAuraActive[Fighter] = false;
        Spawn(HeatEndSystem, Root + FVector(0, 0, 85), false);
        break;
    case EFighterHeatEffect::Powered:
        PlayHeat(Location);
        break;
    default:
        PlayHeat(Root + FVector(0, 0, 100));
        break;
    }

    if (Created)
        ++HeatCount;
    UE_LOG(LogTemp, Display, TEXT("FIGHTER_HEAT_STAGE frame=%u fighter=%d kind=%s source_components=%d root=%s"),
           Arena->GetCombatState().frame, Fighter, FighterHeatPresentation::Name(Kind), Created, *Root.ToString());
    const uint16 Bit = uint16(1) << (uint8(Kind) + Fighter * 7);

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterHeatCapture")) && !(HeatCapturedMask & Bit))
    {
        HeatCapturedMask |= Bit;
        HeatCaptures.Add({Kind, Fighter, 0});
        HeatCaptureDelay = 0;
    }
}

void UFighterImpactComponent::QueuePaulWind(int32 Fighter, int32 RuleIndex, uint32 Frame)
{
    if (Fighter < 0 || Fighter > 1 || RuleIndex < 0 || RuleIndex >= FighterImpactProfiles::WindRuleCount)
        return;
    PaulWindQueue.Add({Fighter, RuleIndex, Frame});
}

namespace
{
FName PaulWindBone(uint8 Slot)
{
    return Slot == 2 ? FName(TEXT("R_Hand")) : Slot == 3 ? FName(TEXT("L_Hand")) : FName(TEXT("Hip"));
}
}

void UFighterImpactComponent::PlayTrail(int32 Fighter, const FVector& Hand)
{
    if (SourceElectricTrail && SourceElectricTrail->IsReady() && Fighter >= 0 && Fighter < 2)
    {
        SourceElectricTrail->Sample(Fighter, Hand);

        if (!bTrailActive[Fighter] || !Hand.Equals(TrailPrevious[Fighter], .5f))
        {
            ++TrailCount;
            TrailPrevious[Fighter] = Hand;
            bTrailActive[Fighter] = true;

            if (TrailCount == 7 && FParse::Param(FCommandLine::Get(), TEXT("FighterTrailCapture")))
                TrailCaptureDelay = 1;
        }

        return;
    }

    if (Fighter < 0 || Fighter > 1 || !KazuyaTrailSystem)
        return;

    if (!bTrailActive[Fighter])
    {
        bTrailActive[Fighter] = true;
        TrailPrevious[Fighter] = Hand;

        return;
    }

    const FVector Segment = Hand - TrailPrevious[Fighter];
    const float Length = Segment.Size();

    if (Length < 3.f)
        return;
    const FRotator Along = FRotationMatrix::MakeFromZ(Segment / Length).Rotator();
    const int32 Steps = FMath::Clamp(FMath::CeilToInt(Length / 10.f), 1, 10);

    for (int32 Step = 1; Step <= Steps; ++Step)
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, KazuyaTrailSystem,
                                                       FMath::Lerp(TrailPrevious[Fighter], Hand, float(Step) / Steps),
                                                       Along, FVector(1), true, true);
    TrailPrevious[Fighter] = Hand;
    ++TrailCount;

    if (TrailCount == 7 && FParse::Param(FCommandLine::Get(), TEXT("FighterTrailCapture")))
        TrailCaptureDelay = 1;
}

void UFighterImpactComponent::EndTrail(int32 Fighter)
{
    if (SourceElectricTrail)
        SourceElectricTrail->Stop(Fighter);

    if (Fighter >= 0 && Fighter <= 1)
        bTrailActive[Fighter] = false;
}

void UFighterImpactComponent::PresentPaulHeatSmash(const AFighterArena& Arena)
{
    namespace C = modular_fighter;
    const auto& State = Arena.GetCombatState();

    for (int32 I = 0; I < 2; ++I)
    {
        const auto& F = State.fighters[I];
        const bool Active = F.character == C::CharacterId::Paul && F.sequence_role == 1 &&
                            F.sequence_command == C::CommandId::PaulHeatSmash;

        if (F.sequence_role == 1 && F.sequence_command == modular_fighter::CommandId::PaulMotionnfnfnfp2Pl666rp &&
            !FParse::Param(FCommandLine::Get(), TEXT("FighterDisableCameraShake")) &&
            !FParse::Param(FCommandLine::Get(), TEXT("FighterDisableHitFeedback")))
        {
            const int32 SlamFrame = F.sequence_frame;

            if (SlamFrame < 84)
                Paul666Slammed[I] = false;
            else if (!Paul666Slammed[I])
            {
                Paul666Slammed[I] = true;

                if (SlamFrame <= 88)
                {
                    T6CameraMode = 3;
                    T6CameraStartFrame = State.frame;
                    UE_LOG(
                        LogTemp, Display,
                        TEXT(
                            "FIGHTER_PAUL_666_SLAM frame=%u fighter=%d source_frame=84 observed=%d t6_mode=3 native_waveform=1 project_trigger=1"),
                        State.frame, I, SlamFrame);
                }
            }
        }
        else
            Paul666Slammed[I] = false;

        if (!Active)
        {
            PaulSmashLastFrame[I] = 0;
            PaulSmashPlayedMask[I] = 0;
            continue;
        }

        const int32 Frame = F.sequence_frame;

        if (Frame < PaulSmashLastFrame[I])
            PaulSmashPlayedMask[I] = 0;

        const int32 Frames[] = {16, 31, 44};

        for (int32 Event = 0; Event < 3; ++Event)
        {
            const uint8 Bit = uint8(1u << Event);

            if (Frame < Frames[Event] || (PaulSmashPlayedMask[I] & Bit))
                continue;
            PaulSmashPlayedMask[I] |= Bit;

            if (Frame > Frames[Event] + 4)
                continue;

            if (Event < 2)
            {
                for (int32 R = 0; R < FighterImpactProfiles::WindRuleCount; ++R)
                {
                    const auto& Rule = FighterImpactProfiles::WindRules[R];

                    if (Rule.SourceIndex == 1768 && Rule.Frame == Frames[Event])
                        QueuePaulWind(I, R, State.frame);
                }

                continue;
            }

            const auto& Victim = State.fighters[1 - I];
            FVector At(Victim.position_x * .1f, Victim.position_z * .1f, 100.f + Victim.air_height * .1f);
            FVector From(F.position_x * .1f, F.position_z * .1f, 105.f + F.air_height * .1f);

            bool Bone = false;

            if (const auto* Model = Arena.GetCharacterMesh(I); Model && Model->IsPoseFinite())
                Bone = Model->GetBoneWorldLocation(FName(TEXT("R_Hand")), At);

            constexpr float Power = 25.f;

            Play(FighterGameplayTags::GameplayCue_Fighter_Hit, At, 0.f, 2, From, true, Power,
                 State.frame ^ (uint32(I) << 15) ^ 0x504853u, 1 - I);
            UE_LOG(
                LogTemp, Display,
                TEXT(
                    "FIGHTER_PAUL_HEAT_FINISH frame=%u fighter=%d source_move=1768 source_frame=44 observed=%d wind201_at=16 wind202_at=31 bone_resolved=%d signature_adapter=1 paul_source=%d base_power=25 power_source_move=1767 counterpart_offset=6 native_inheritance=unresolved t6_strength_pair=9,10 hp_mutation=0"),
                State.frame, I, Frame, Bone ? 1 : 0, PaulSourceSystem ? 1 : 0);

            if (FParse::Param(FCommandLine::Get(), TEXT("FighterPaulHeatFinishCapture")))
                PaulSmashCaptureDelay = 3;
        }

        PaulSmashLastFrame[I] = Frame;
    }
}

// 진행 중인 이펙트를 갱신한다.
void UFighterImpactComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    PaulSourceEffects.RemoveAll([](const auto& Effect) { return !Effect.IsValid(); });
    KazuyaSourceEffects.RemoveAll([](const auto& Effect) { return !Effect.IsValid(); });
    HeatStageEffects.RemoveAll([](const auto& Effect) { return !Effect.Component.IsValid(); });

    if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
    {
        const auto& State = Arena->GetCombatState();

        if (State.frame < FeedbackLastCombatFrame)
            ResetFeedback();
        PresentPaulHeatSmash(*Arena);

        for (auto& Effect : HeatStageEffects)
            if (Effect.Target >= 0 && Effect.Target < 2)
            {
                const auto& F = State.fighters[Effect.Target];
                Effect.Component->SetWorldLocation(FVector(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f) +
                                                   Effect.Offset);
                Effect.Component->SetWorldRotation(FVector(F.face_cos / 65536.f, F.face_sin / 65536.f, 0).Rotation());
            }

        auto WindAnchor = [&](int32 Fighter, FName Bone, FVector& Out) -> bool
        {
            const auto& F = State.fighters[Fighter];
            const FVector Root(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f);
            const FVector Forward(F.face_cos / 65536.f, F.face_sin / 65536.f, 0);
            const auto* Model = Arena->GetCharacterMesh(Fighter);

            if (Model && Model->IsPoseFinite() && Model->GetBoneWorldLocation(Bone, Out))
                return true;
            Out =
                Root + FVector(0, 0, Bone == TEXT("Hip") ? 90.f : 110.f) + Forward * (Bone == TEXT("Hip") ? 0.f : 55.f);

            return false;
        };

        for (const auto& Queued : PaulWindQueue)
        {
            const auto& Rule = FighterImpactProfiles::WindRules[Queued.RuleIndex];
            auto* System = PaulWindSystems.FindRef(Rule.EffectId).Get();
            const FName Bone = PaulWindBone(Rule.Slot);
            FVector At;
            const bool bBone = WindAnchor(Queued.Fighter, Bone, At);
            const auto& F = State.fighters[Queued.Fighter];
            const FRotator Facing = FVector(F.face_cos / 65536.f, F.face_sin / 65536.f, 0).Rotation();
            UNiagaraComponent* Component =
                System
                    ? UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, System, At, Facing, FVector(1), true, true)
                    : nullptr;

            if (Component)
            {
                PaulWindEffects.Add({Component, Queued.Fighter, Bone, FVector::ZeroVector});
                ++PaulWindCount;
            }

            UE_LOG(
                LogTemp, Display,
                TEXT(
                    "FIGHTER_PAUL_WIND_PLAY frame=%u fighter=%d source=%s source_frame=%d effect=%d slot=%d bone=%s bone_resolved=%d active=%d system=NS_FG_PaulWind%d"),
                Queued.Frame, Queued.Fighter, UTF8_TO_TCHAR(Rule.SourceName), Rule.Frame, Rule.EffectId, Rule.Slot,
                *Bone.ToString(), bBone ? 1 : 0, Component ? 1 : 0, Rule.EffectId);

            if (Component && PaulWindCaptureNext < 4 && PaulWindCaptureDelay == 0 &&
                FParse::Param(FCommandLine::Get(), TEXT("FighterPaulWindCapture")))
                PaulWindCaptureDelay = 4;
        }

        PaulWindQueue.Reset();
        PaulWindEffects.RemoveAll([](const auto& Effect) { return !Effect.Component.IsValid(); });

        for (auto& Effect : PaulWindEffects)
        {
            FVector At;
            WindAnchor(Effect.Fighter, Effect.Bone, At);
            const auto& F = State.fighters[Effect.Fighter];
            Effect.Component->SetWorldLocation(At + Effect.Offset);
            Effect.Component->SetWorldRotation(FVector(F.face_cos / 65536.f, F.face_sin / 65536.f, 0).Rotation());
        }

        if (PaulWindCaptureDelay > 0 && --PaulWindCaptureDelay == 0)
            FScreenshotRequest::RequestScreenshot(
                FPaths::ConvertRelativePathToFull(
                    FPaths::ProjectSavedDir() /
                    FString::Printf(TEXT("Automation/PaulWind_%02d.png"), ++PaulWindCaptureNext)),
                false, false);

        if (PaulSmashCaptureDelay > 0 && --PaulSmashCaptureDelay == 0)
            FScreenshotRequest::RequestScreenshot(
                FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/PaulHeatFinish.png")),
                false, false);

        for (int32 I = 0; I < 2; ++I)
        {
            auto& Effects = HeatAuraEffects[I];
            Effects.RemoveAll([](const auto& Effect) { return !Effect.IsValid(); });
            const auto& F = State.fighters[I];
            const bool Active = HeatAuraSystem && F.heat_frames > 0;

            if (Active != bHeatAuraActive[I])
                UE_LOG(LogTemp, Display, TEXT("FIGHTER_HEAT_AURA frame=%u fighter=%d active=%d"), State.frame, I,
                       Active ? 1 : 0);

            if (!Active)
            {
                for (auto& Effect : Effects)
                    if (Effect.IsValid())
                        Effect->DestroyComponent();
                Effects.Reset();
            }
            else if (!bHeatAuraActive[I] || State.frame >= HeatAuraLastFrame[I] + 12)
            {
                auto* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
                    this, HeatAuraSystem, FVector(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f + 65.f),
                    FRotator::ZeroRotator, FVector(1), true, true);

                if (Component)
                    Effects.Add(Component);
                HeatAuraLastFrame[I] = State.frame;
            }

            bHeatAuraActive[I] = Active;
        }
    }

    CounterEffects.RemoveAll([](const auto& Effect) { return !Effect.Component.IsValid(); });
    TornadoEffects.RemoveAll([](const auto& Effect) { return !Effect.Component.IsValid(); });

    if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
        for (auto& Effect : TornadoEffects)
            if (Effect.Target >= 0 && Effect.Target < 2)
            {
                const auto& F = Arena->GetCombatState().fighters[Effect.Target];
                Effect.Component->SetWorldLocation(FVector(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f) +
                                                   Effect.Offset);
            }
    if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
        for (auto& Effect : CounterEffects)
            if (Effect.Target >= 0 && Effect.Target < 2)
            {
                const auto& F = Arena->GetCombatState().fighters[Effect.Target];
                Effect.Component->SetWorldLocation(FVector(F.position_x * .1f, F.position_z * .1f, F.air_height * .1f) +
                                                   Effect.Offset);
            }
    auto* PC = GetWorld()->GetFirstPlayerController();

    for (int32 I = 0; I < HeatSprites.Num(); ++I)
    {
        HeatAges[I] += FMath::Clamp(Delta, 0.f, .05f);
        const float T = HeatAges[I] / FighterHeatSource::Seconds;
        HeatSprites[I]->SetVisibility(T < 1.f);

        if (T >= 1.f)
            continue;
        HeatMaterials[I]->SetScalarParameterValue(TEXT("HeatAge"), T);
        HeatMaterials[I]->SetScalarParameterValue(TEXT("HeatOpacity"),
                                                  FighterImpactCurves::Sample(FighterHeatSource::Notice, T));
        HeatSprites[I]->SetWorldScale3D(FVector(2.2f + T * .7f));

        if (PC && PC->PlayerCameraManager)
            HeatSprites[I]->SetWorldRotation(FQuat::FindBetweenNormals(
                FVector::UpVector,
                (PC->PlayerCameraManager->GetCameraLocation() - HeatSprites[I]->GetComponentLocation())
                    .GetSafeNormal()));
    }

    for (int32 I = Sprites.Num() - 1; I >= 0; --I)
    {
        const int32 Layer = I % BurstLayers;
        Ages[I] += FMath::Clamp(Delta, 0.f, .05f);
        const float T = Ages[I] / (Layer == 2 || Layer == 5 || Layer == 6 ? .24f : .18f);

        if (T >= 1)
        {
            Sprites[I]->SetVisibility(false);
            continue;
        }

        Sprites[I]->SetWorldScale3D(Aspects[I] * (Sizes[I] * (.85f + T * .5f)));

        if (!Flights[I].IsZero())
            Sprites[I]->SetWorldLocation(Origins[I] + Flights[I] * (1.f - FMath::Square(1.f - T)));
        SpriteMaterials[I]->SetScalarParameterValue(TEXT("ImpactAge"), Ages[I]);
        float Opacity = FMath::Square(1.f - T);

        if (Layer == 3)
            Opacity = FighterImpactCurves::Sample(FighterImpactCurves::PaulSphere, T);
        else if (Layer == 7)
            Opacity = FighterImpactCurves::Sample(FighterImpactCurves::PaulStream, T);
        else if (Layer == 2 || Layer == 5 || Layer == 6)
            Opacity = FighterImpactCurves::Sample(FighterImpactCurves::KazuyaParticle, T);
        SpriteMaterials[I]->SetScalarParameterValue(TEXT("ImpactOpacity"), FMath::Clamp(Opacity, 0.f, 1.f));

        if ((Layer < 2 || Layer >= 8) && PC && PC->PlayerCameraManager)
        {
            const FVector ToCamera =
                (PC->PlayerCameraManager->GetCameraLocation() - Sprites[I]->GetComponentLocation()).GetSafeNormal();
            Sprites[I]->SetWorldRotation(FQuat(ToCamera, FMath::DegreesToRadians(Rolls[I])) *
                                         FQuat::FindBetweenNormals(FVector::UpVector, ToCamera));
        }
    }

    if (FlashLight)
    {
        FlashAge += FMath::Clamp(Delta, 0.f, .05f);
        FlashLight->SetIntensity(FlashAge < .09f ? FlashPeak * FMath::Square(1.f - FlashAge / .09f) : 0.f);
    }

    if (const auto* Arena = Cast<AFighterArena>(GetOwner()))
    {
        const uint32 Frame = Arena->GetCombatState().frame;

        if (Frame < FeedbackLastCombatFrame)
            ResetFeedback();
        FeedbackLastCombatFrame = Frame;

        if (T6CameraMode && PC)
            if (auto* Camera = UFighterCameraComponent::FindCameraComponent(PC->GetViewTarget()))
            {
                const int32 Age = int32(Frame - T6CameraStartFrame);
                const float Radians = FighterT6Feedback::CameraRadians(T6CameraMode, Age);
                Camera->SetSourcePitchRadians(Radians);

                if (Age == 0 || Age == 3 || Age >= FighterT6Feedback::CameraSampleCount(T6CameraMode))
                    UE_LOG(LogTemp, Display, TEXT("FIGHTER_T6_CAMERA_SAMPLE mode=%d index=%d radians=%.9f"),
                           T6CameraMode, Age, Radians);

                if (Age >= FighterT6Feedback::CameraSampleCount(T6CameraMode))
                    T6CameraMode = 0;
            }
    }

    const float ElapsedFrames = FMath::Max(0.f, Delta) * 60.f;
    CameraPulse.Advance(ElapsedFrames);

    for (auto& Pulse : HealthPulses)
        Pulse.Advance(ElapsedFrames);

    if (CameraPulse.Active() && PC)
        if (auto* Camera = UFighterCameraComponent::FindCameraComponent(PC->GetViewTarget()))
        {
            const float Kick = CameraPulse.Settings.CameraCm * CameraPulse.Kick();
            Camera->AddPresentationOffset(Camera->GetRightVector() * (Kick * CameraPulse.Direction) +
                                          Camera->GetUpVector() * (Kick * .4f));
            Camera->AddFieldOfViewOffset(-CameraPulse.Settings.FovDegrees * CameraPulse.Fade());
        }
    if (FeedbackCaptureAge >= 0)
    {
        FeedbackCaptureAge += ElapsedFrames;
        constexpr int32 Frames[]{1, 3, 6, 12, 20, 44};

        if (FeedbackCaptureNext < UE_ARRAY_COUNT(Frames) && FeedbackCaptureAge + .01f >= Frames[FeedbackCaptureNext])
        {
            const auto Offset = GetHealthBarOffset(FeedbackCaptureTarget);
            UE_LOG(LogTemp, Display,
                   TEXT("FIGHTER_FEEDBACK_SAMPLE age=%.2f target=%d bar_x=%.3f bar_y=%.3f camera_kick=%.3f"),
                   FeedbackCaptureAge, FeedbackCaptureTarget, Offset.X, Offset.Y,
                   CameraPulse.Settings.CameraCm * CameraPulse.Kick());
            FScreenshotRequest::RequestScreenshot(
                FPaths::ConvertRelativePathToFull(
                    FPaths::ProjectSavedDir() /
                    FString::Printf(TEXT("Automation/Feedback_%02d.png"), Frames[FeedbackCaptureNext++])),
                false, false);
        }
    }

    if (CaptureDelay > 0 && --CaptureDelay == 0)
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/FirstImpact.png")), false,
            false);
    HitVatEffects.RemoveAll([](const auto& Effect) { return !Effect.IsValid(); });
    HitSourceLayerEffects.RemoveAll([](const auto& Effect) { return !Effect.IsValid(); });
    CriticalSourceEffects.RemoveAll([](const auto& Effect) { return !Effect.IsValid(); });

    if (HitVatCaptureTick >= 0)
    {
        ++HitVatCaptureTick;

        if (HitVatCaptureTick == 3 || HitVatCaptureTick == 9 || HitVatCaptureTick == 15)
            FScreenshotRequest::RequestScreenshot(
                FPaths::ConvertRelativePathToFull(
                    FPaths::ProjectSavedDir() / FString::Printf(TEXT("Automation/HitVAT_%02d.png"), HitVatCaptureTick)),
                false, false);

        if (HitVatCaptureTick >= 15)
            HitVatCaptureTick = -1;
    }

    if (TornadoCaptureDelay > 0 && --TornadoCaptureDelay == 0)
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TornadoImpact.png")), false,
            false);

    if (TrailCaptureDelay > 0 && --TrailCaptureDelay == 0)
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/HandTrail.png")), false,
            false);

    for (auto& Capture : HeatCaptures)
        ++Capture.Ticks;

    if (!HeatCaptures.IsEmpty() && HeatCaptures[0].Ticks >= 7)
    {
        const auto Capture = HeatCaptures[0];
        HeatCaptures.RemoveAt(0);
        const FString Name = FString::Printf(TEXT("Automation/HeatStage_%s_P%d.png"),
                                             FighterHeatPresentation::Name(Capture.Kind), Capture.Fighter + 1);
        FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / Name),
                                              false, false);
    }
    else if (HeatCaptureDelay > 0 && --HeatCaptureDelay == 0)
        FScreenshotRequest::RequestScreenshot(
            FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/HeatEffect.png")), false,
            false);
}
