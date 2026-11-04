// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 카메라 모드 정의.
#pragma once

#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "FighterCameraMode.generated.h"

class AActor;
class AFighterArena;
class UFighterCameraComponent;

#define FIGHTER_CAMERA_DEFAULT_FOV (52.0f)
#define FIGHTER_CAMERA_DEFAULT_PITCH_MIN (-89.0f)
#define FIGHTER_CAMERA_DEFAULT_PITCH_MAX (89.0f)

UENUM(BlueprintType)
enum class EFighterCameraModeBlendFunction : uint8
{
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    COUNT UMETA(Hidden)
};

struct FFighterCameraModeView
{
public:
    FFighterCameraModeView();
    void Blend(const FFighterCameraModeView& Other, float OtherWeight);

public:
    FVector Location;
    FRotator Rotation;
    FRotator ControlRotation;
    float FieldOfView;
};

UCLASS(Abstract, NotBlueprintable)

class FIGHTERGAME_API UFighterCameraMode : public UObject
{
    GENERATED_BODY()
public:
    UFighterCameraMode();

    UFighterCameraComponent* GetFighterCameraComponent() const;
    virtual UWorld* GetWorld() const override;
    AActor* GetTargetActor() const;

    const FFighterCameraModeView& GetCameraModeView() const
    {
        return View;
    }

    virtual void OnActivation() {}

    virtual void OnDeactivation() {}

    void UpdateCameraMode(float DeltaTime);

    float GetBlendTime() const
    {
        return BlendTime;
    }

    float GetBlendWeight() const
    {
        return BlendWeight;
    }

    void SetBlendWeight(float Weight);

    FGameplayTag GetCameraTypeTag() const
    {
        return CameraTypeTag;
    }

protected:
    virtual FVector GetPivotLocation() const;
    virtual FRotator GetPivotRotation() const;
    virtual void UpdateView(float DeltaTime);
    virtual void UpdateBlending(float DeltaTime);

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Blending")
    FGameplayTag CameraTypeTag;

    FFighterCameraModeView View;

    UPROPERTY(EditDefaultsOnly, Category = "View",
              Meta = (UIMin = "5.0", UIMax = "170", ClampMin = "5.0", ClampMax = "170.0"))
    float FieldOfView;

    UPROPERTY(EditDefaultsOnly, Category = "View",
              Meta = (UIMin = "-89.9", UIMax = "89.9", ClampMin = "-89.9", ClampMax = "89.9"))
    float ViewPitchMin;

    UPROPERTY(EditDefaultsOnly, Category = "View",
              Meta = (UIMin = "-89.9", UIMax = "89.9", ClampMin = "-89.9", ClampMax = "89.9"))
    float ViewPitchMax;

    UPROPERTY(EditDefaultsOnly, Category = "Blending")
    float BlendTime;

    UPROPERTY(EditDefaultsOnly, Category = "Blending")
    EFighterCameraModeBlendFunction BlendFunction;

    UPROPERTY(EditDefaultsOnly, Category = "Blending")
    float BlendExponent;

    float BlendAlpha;
    float BlendWeight;
};

UCLASS()

class FIGHTERGAME_API UFighterCameraModeStack : public UObject
{
    GENERATED_BODY()
public:
    UFighterCameraModeStack();

    void ActivateStack();
    void DeactivateStack();

    bool IsStackActivate() const
    {
        return bIsActive;
    }

    void PushCameraMode(TSubclassOf<UFighterCameraMode> CameraModeClass);
    bool EvaluateStack(float DeltaTime, FFighterCameraModeView& OutCameraModeView);
    void GetBlendInfo(float& OutWeightOfTopLayer, FGameplayTag& OutTagOfTopLayer) const;

    int32 GetStackSize() const
    {
        return CameraModeStack.Num();
    }

protected:
    UFighterCameraMode* GetCameraModeInstance(TSubclassOf<UFighterCameraMode> CameraModeClass);
    void UpdateStack(float DeltaTime);
    void BlendStack(FFighterCameraModeView& OutCameraModeView) const;

protected:
    bool bIsActive;

    UPROPERTY()
    TArray<TObjectPtr<UFighterCameraMode>> CameraModeInstances;

    UPROPERTY()
    TArray<TObjectPtr<UFighterCameraMode>> CameraModeStack;
};

UCLASS(Abstract)

class FIGHTERGAME_API UFighterCameraMode_Framing : public UFighterCameraMode
{
    GENERATED_BODY()
public:
    UFighterCameraMode_Framing();

protected:
    virtual void UpdateView(float DeltaTime) override;
    virtual void GetFraming(FVector2D& OutMid, float& OutSeparation, FVector2D& OutRight) const
        PURE_VIRTUAL(UFighterCameraMode_Framing::GetFraming, );
    const AFighterArena* GetArena() const;

    UPROPERTY(EditDefaultsOnly, Category = "Framing")
    float TargetHeight = 100.f;
    UPROPERTY(EditDefaultsOnly, Category = "Framing")
    float MinDistance = 820.f;
    UPROPERTY(EditDefaultsOnly, Category = "Framing")
    float DistancePerSeparation = 1.35f;
    UPROPERTY(EditDefaultsOnly, Category = "Framing")
    float DistanceOffset = 380.f;
    UPROPERTY(EditDefaultsOnly, Category = "Framing")
    float BaseHeight = 300.f;
    UPROPERTY(EditDefaultsOnly, Category = "Framing")
    float HeightPerSeparation = 0.08f;

    UPROPERTY(EditDefaultsOnly, Category = "Framing")
    float AxisLagSeconds = 0.3f;
    FVector2D SmoothedRight = FVector2D::ZeroVector;
};

UCLASS()

class FIGHTERGAME_API UFighterCameraMode_Battle : public UFighterCameraMode_Framing
{
    GENERATED_BODY()
public:
    UFighterCameraMode_Battle();

protected:
    virtual void GetFraming(FVector2D& OutMid, float& OutSeparation, FVector2D& OutRight) const override;
};

UCLASS()

class FIGHTERGAME_API UFighterCameraMode_Paired : public UFighterCameraMode_Battle
{
    GENERATED_BODY()
public:
    UFighterCameraMode_Paired();
    static int32 GetIntroHiddenOpponent(const AFighterArena* Arena);

protected:
    virtual void UpdateView(float DeltaTime) override;
};

UCLASS()

class FIGHTERGAME_API UFighterCameraMode_ReferencePreview : public UFighterCameraMode_Framing
{
    GENERATED_BODY()
protected:
    virtual void GetFraming(FVector2D& OutMid, float& OutSeparation, FVector2D& OutRight) const override;
};
