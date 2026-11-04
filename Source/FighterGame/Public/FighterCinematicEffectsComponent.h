// 필살기 연출 효과.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FighterCinematicEffectsComponent.generated.h"
class AFighterArena;
class UNiagaraSystem;
class UNiagaraComponent;
class ULightComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

UCLASS()

class FIGHTERGAME_API UFighterCinematicEffectsComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void Present(const AFighterArena& Arena);
    void Reset();

private:
    void PresentLighting(const AFighterArena& Arena);
    void PresentGround(const AFighterArena& Arena);
    UPROPERTY()
    TArray<TObjectPtr<UStaticMesh>> GroundMeshes;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> GroundMaterials;
    UPROPERTY()
    TArray<TObjectPtr<UStaticMeshComponent>> GroundPieces;
    UPROPERTY()
    TArray<TObjectPtr<ULightComponent>> PreLights;
    UPROPERTY()
    TArray<TObjectPtr<ULightComponent>> LevelLights;
    bool bLightingActive = false;
    UPROPERTY()
    TArray<TObjectPtr<UNiagaraSystem>> Systems;

    struct FActive
    {
        TWeakObjectPtr<UNiagaraComponent> FX;
        int32 Event = 0;
    };

    TArray<FActive> Active;
    int32 Owner = INDEX_NONE;
    uint16 Command = 0;
    int32 LastFrame = 0;
    uint32 LastSimulationFrame = 0;
};
