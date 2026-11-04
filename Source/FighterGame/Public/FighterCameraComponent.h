// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 격투 게임 카메라 컴포넌트.
#pragma once

#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "FighterCameraComponent.generated.h"

class UFighterCameraMode;
class UFighterCameraModeStack;
struct FGameplayTag;
struct FMinimalViewInfo;
template <class TClass> class TSubclassOf;

DECLARE_DELEGATE_RetVal(TSubclassOf<UFighterCameraMode>, FFighterCameraModeDelegate);

UCLASS()

class FIGHTERGAME_API UFighterCameraComponent : public UCameraComponent
{
    GENERATED_BODY()
public:
    UFighterCameraComponent(const FObjectInitializer& ObjectInitializer);

    static UFighterCameraComponent* FindCameraComponent(const AActor* Actor)
    {
        return (Actor ? Actor->FindComponentByClass<UFighterCameraComponent>() : nullptr);
    }

    virtual AActor* GetTargetActor() const
    {
        return GetOwner();
    }

    FFighterCameraModeDelegate DetermineCameraModeDelegate;

    void AddFieldOfViewOffset(float FovOffset)
    {
        FieldOfViewOffset += FovOffset;
    }

    void AddPresentationOffset(const FVector& Offset)
    {
        PresentationOffset += Offset;
    }

    void SetSourcePitchRadians(float Radians)
    {
        SourcePitchRadians = Radians;
    }

    void AddHitBlur(float Strength, const FVector2D& Center);

    void StartDeathfistFocus(uint32 CombatFrame, const FVector& Contact);

    void ResetDeathfistFocus()
    {
        bDeathfistFocus = false;
    }

    void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

protected:
    virtual void OnRegister() override;
    virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;
    virtual void UpdateCameraModes();
    void UpdatePaulSpace(bool bShowBackdrop, const FTransform& Root);

protected:
    UPROPERTY()
    TObjectPtr<UFighterCameraModeStack> CameraModeStack;

    float FieldOfViewOffset;
    FVector PresentationOffset = FVector::ZeroVector;
    float SourcePitchRadians = 0;
    bool bDeathfistFocus = false;
    bool bPaulLetteringShown = false;
    bool bPaulSpaceShown = false;
    UPROPERTY()
    TArray<TObjectPtr<class UStaticMeshComponent>> PaulSpacePlanes;
    uint32 DeathfistFrame = 0;
    FVector DeathfistContact = FVector::ZeroVector;
    UPROPERTY()
    TObjectPtr<class UMaterialInstanceDynamic> HitBlurMaterial;
    UPROPERTY()
    TObjectPtr<class UMaterialInstanceDynamic> RageBackgroundMaterial;
    UPROPERTY()
    TObjectPtr<class UMaterialInstanceDynamic> RageSpaceMaterial;
    float HitBlurStrength = 0.f;
    float HitBlurAge = 1.f;
    FVector2D HitBlurCenter = FVector2D(.5f, .5f);
};
