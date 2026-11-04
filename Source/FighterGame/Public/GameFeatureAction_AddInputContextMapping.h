// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 입력 컨텍스트 추가 액션.
#pragma once

#include "GameFeatureAction_WorldActionBase.h"
#include "UObject/SoftObjectPtr.h"
#include "GameFeatureAction_AddInputContextMapping.generated.h"

class AActor;
class UInputMappingContext;
class UPlayer;
class APlayerController;
class ULocalPlayer;
struct FComponentRequestHandle;

USTRUCT(BlueprintType)

struct FFighterInputMappingContextAndPriority
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Input", meta = (AssetBundles = "Client,Server"))
    TSoftObjectPtr<UInputMappingContext> InputMapping;

    UPROPERTY(EditAnywhere, Category = "Input")
    int32 Priority = 0;

    UPROPERTY(EditAnywhere, Category = "Input")
    bool bRegisterWithSettings = true;
};

UCLASS(BlueprintType, meta = (DisplayName = "Add Input Mapping"))

class FIGHTERGAME_API UGameFeatureAction_AddInputContextMapping final : public UGameFeatureAction_WorldActionBase
{
    GENERATED_BODY()
public:
    virtual void OnGameFeatureRegistering() override;
    virtual void OnGameFeatureActivating(FGameFeatureActivatingContext& Context) override;
    virtual void OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context) override;
    virtual void OnGameFeatureUnregistering() override;

    UPROPERTY(EditAnywhere, Category = "Input")
    TArray<FFighterInputMappingContextAndPriority> InputMappings;

private:
    struct FPerContextData
    {
        TArray<TSharedPtr<FComponentRequestHandle>> ExtensionRequestHandles;
        TArray<TWeakObjectPtr<APlayerController>> ControllersAddedTo;
    };

    TMap<FGameFeatureStateChangeContext, FPerContextData> ContextData;

    FDelegateHandle RegisterInputContextMappingsForGameInstanceHandle;

    void RegisterInputMappingContexts();
    void RegisterInputContextMappingsForGameInstance(UGameInstance* GameInstance);
    void RegisterInputMappingContextsForLocalPlayer(ULocalPlayer* LocalPlayer);
    void UnregisterInputMappingContexts();
    void UnregisterInputContextMappingsForGameInstance(UGameInstance* GameInstance);
    void UnregisterInputMappingContextsForLocalPlayer(ULocalPlayer* LocalPlayer);

    virtual void AddToWorld(const FWorldContext& WorldContext,
                            const FGameFeatureStateChangeContext& ChangeContext) override;

    void Reset(FPerContextData& ActiveData);
    void HandleControllerExtension(AActor* Actor, FName EventName, FGameFeatureStateChangeContext ChangeContext);
    void AddInputMappingForPlayer(APlayerController* PlayerController, FPerContextData& ActiveData);
    void RemoveInputMapping(APlayerController* PlayerController, FPerContextData& ActiveData);
};
