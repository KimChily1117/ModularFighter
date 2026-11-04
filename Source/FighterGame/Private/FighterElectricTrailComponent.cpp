// 손 궤적을 따라가는 리본 이펙트.
#include "FighterElectricTrailComponent.h"
#include "FighterArena.h"
#include "FighterCameraComponent.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "FighterSaifuColors.h"

namespace
{
constexpr double Retention = 1.;
constexpr float Widths[] = {120.f, 150.f, 150.f, 100.f};
constexpr int32 Subdivisions[] = {3, 10, 10, 10};
}

UFighterElectricTrailComponent::UFighterElectricTrailComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UFighterElectricTrailComponent::BeginPlay()
{
    Super::BeginPlay();
    AddTickPrerequisiteActor(GetOwner());
    const TCHAR* Names[] = {TEXT("M_Saifu_Ribbon03"), TEXT("M_Saifu_Ribbon04"), TEXT("M_Saifu_Lightning08"),
                            TEXT("M_Saifu_Lightning08")};

    for (const auto* Name : Names)
    {
        auto* M = LoadObject<UMaterialInterface>(
            nullptr, *FString::Printf(TEXT("/Game/LocalPresentation/SourceFX/KazuyaTrail/%s.%s"), Name, Name));

        if (!M)
        {
            Materials.Reset();

            return;
        }

        Materials.Add(M);
    }

    for (int32 I = 0; I < 2; ++I)
    {
        auto* Mesh = NewObject<UProceduralMeshComponent>(GetOwner());
        Mesh->SetupAttachment(GetOwner()->GetRootComponent());
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->bReceivesDecals = false;
        Mesh->TranslucencySortPriority = 5;
        Mesh->RegisterComponent();
        Mesh->SetWorldTransform(FTransform::Identity);

        for (int32 L = 0; L < 4; ++L)
            Mesh->SetMaterial(L, Materials[L]);
        Meshes.Add(Mesh);
    }

    bReady = true;
    UE_LOG(
        LogTemp, Display,
        TEXT(
            "FIGHTER_SAIFU_TRAIL enabled=1 source=NS_EF_Grl_Saifu02 ribbons=4 widths_cm=120,150,150,100 world_history=1 retention_s=1 adapter=1"));
}

void UFighterElectricTrailComponent::AdvanceClock()
{
    const auto* Arena = Cast<AFighterArena>(GetOwner());

    if (!Arena)
        return;
    const auto& State = Arena->GetCombatState();

    if (LastFrame != MAX_uint32 && State.frame < LastFrame)
        ResetTrails();

    if (LastFrame != MAX_uint32 && State.frame > LastFrame && !State.hitstop_frames)
        Clock += FMath::Min(State.frame - LastFrame, 8u) / 60.;
    LastFrame = State.frame;
}

void UFighterElectricTrailComponent::Sample(int32 Fighter, const FVector& Hand)
{
    if (!bReady || Fighter < 0 || Fighter > 1 || Hand.ContainsNaN())
        return;
    AdvanceClock();
    auto& T = Tracks[Fighter];

    if (!T.Emitting)
    {
        T.Points.Reset();
        T.LastSample = MAX_uint32;
        T.StartTime = Clock;
    }

    T.Emitting = true;

    if (Clock - T.StartTime > .2)
        return;

    if (T.LastSample == LastFrame)
        return;
    T.LastSample = LastFrame;

    if (!T.Points.IsEmpty() && FVector::DistSquared(T.Points.Last().Position, Hand) > FMath::Square(180.f))
        T.Points.Reset();

    if (!T.Points.IsEmpty() && FVector::DistSquared(T.Points.Last().Position, Hand) < .25f)
        return;
    const float Distance =
        T.Points.IsEmpty() ? 0.f : T.Points.Last().Distance + FVector::Distance(T.Points.Last().Position, Hand);
    T.Points.Add({Hand, Clock, Distance, float(Clock - T.StartTime)});

    if (T.Points.Num() > 96)
        T.Points.RemoveAt(0);

    if (FParse::Param(FCommandLine::Get(), TEXT("FighterTrailTrace")))
        UE_LOG(LogTemp, Display, TEXT("FIGHTER_SAIFU_SAMPLE fighter=%d frame=%u points=%d hand=%s"), Fighter, LastFrame,
               T.Points.Num(), *Hand.ToCompactString());
}

void UFighterElectricTrailComponent::Stop(int32 Fighter)
{
    if (Fighter >= 0 && Fighter < 2)
        Tracks[Fighter].Emitting = false;
}

void UFighterElectricTrailComponent::ResetTrails()
{
    for (auto& T : Tracks)
    {
        T.Points.Reset();
        T.Emitting = false;
        T.LastSample = MAX_uint32;
    }

    for (auto& Mesh : Meshes)
        if (Mesh)
            Mesh->ClearAllMeshSections();
    LastFrame = MAX_uint32;
    Clock = 0;
}

void UFighterElectricTrailComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);

    if (!bReady)
        return;
    AdvanceClock();

    for (int32 I = 0; I < 2; ++I)
    {
        auto& P = Tracks[I].Points;

        while (!P.IsEmpty() && Clock - P[0].Time > Retention)
            P.RemoveAt(0);
        Rebuild(I);
    }
}

// 샘플로 리본 메시를 다시 만든다.
void UFighterElectricTrailComponent::Rebuild(int32 Fighter)
{
    auto* Mesh = Meshes[Fighter].Get();
    const auto& Points = Tracks[Fighter].Points;

    if (Points.Num() < 2)
    {
        Mesh->ClearAllMeshSections();

        return;
    }

    const auto* Camera = GetOwner()->FindComponentByClass<UFighterCameraComponent>();
    const FVector Eye = Camera ? Camera->GetComponentLocation() : Points.Last().Position + FVector(0, -1000, 300);

    for (int32 L = 0; L < 4; ++L)
    {
        TArray<FVector> Vertices, Normals;
        TArray<int32> Indices;
        TArray<FVector2D> UV, AgeUV;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;
        const int32 Columns = Subdivisions[L] + 1;

        for (int32 P = 0; P < Points.Num(); ++P)
        {
            const auto& Point = Points[P];
            const FVector Tangent =
                (Points[FMath::Min(P + 1, Points.Num() - 1)].Position - Points[FMath::Max(P - 1, 0)].Position)
                    .GetSafeNormal();
            const FVector ToEye = (Eye - Point.Position).GetSafeNormal();
            const FVector Side =
                FVector::CrossProduct(Tangent, ToEye).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
            const float Age = FMath::Clamp(float((Clock - Point.Time) / Retention), 0.f, 1.f);

            for (int32 C = 0; C < Columns; ++C)
            {
                const float V = float(C) / Subdivisions[L];
                Vertices.Add(Point.Position + Side * ((V - .5f) * Widths[L]));
                Normals.Add(ToEye);
                UV.Add(FVector2D(Point.Distance / 300.f, V));
                AgeUV.Add(FVector2D(Age, L == 3 ? .25f : 0.f));

                Colors.Add(FighterSaifuColors::Sample(L, Point.SourceAge / (L == 0 ? .19f : .2f)));
                Tangents.Add(FProcMeshTangent(Tangent, false));

                if (P > 0 && C > 0)
                {
                    const int32 A = P * Columns + C, B = A - Columns;
                    Indices.Append({A - 1, B - 1, B, A - 1, B, A});
                }
            }
        }

        Mesh->CreateMeshSection_LinearColor(L, Vertices, Indices, Normals, UV, AgeUV, {}, {}, Colors, Tangents, false,
                                            false);
    }
}
