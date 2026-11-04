// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience 로딩 상태 관리.
#pragma once

#include "CoreMinimal.h"
#include "Components/GameStateComponent.h"
#include "GameFeaturePluginOperationResult.h"
#include "UObject/PrimaryAssetId.h"
#include "FighterExperienceManagerComponent.generated.h"

struct FStreamableHandle;
class UFighterExperienceDefinition;

enum class EFighterExperienceLoadState
{
    Unloaded,
    Loading,
    LoadingGameFeatures,
    ExecutingActions,
    Loaded,
    Failed,
    Deactivating,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnFighterExperienceLoaded, const UFighterExperienceDefinition*);

UCLASS()

class FIGHTERGAME_API UFighterExperienceManagerComponent : public UGameStateComponent
{
    GENERATED_BODY()

public:
    UFighterExperienceManagerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    bool IsExperienceLoaded() const
    {
        return (LoadState == EFighterExperienceLoadState::Loaded) && (CurrentExperience != nullptr);
    }

    EFighterExperienceLoadState GetLoadState() const
    {
        return LoadState;
    }

    void CallOrRegister_OnExperienceLoaded(FOnFighterExperienceLoaded::FDelegate&& Delegate);

    void ServerSetCurrentExperience(FPrimaryAssetId ExperienceId);
    const UFighterExperienceDefinition* GetCurrentExperienceChecked() const;

    const FPrimaryAssetId& GetCurrentExperienceId() const
    {
        return CurrentExperienceId;
    }

    FOnFighterExperienceLoaded OnExperienceLoaded;

private:
    UFUNCTION()
    void OnRep_CurrentExperienceId();

    void StartExperienceLoad();
    void OnExperienceDefinitionLoadComplete();
    void OnExperienceDefinitionLoadCanceled();
    void StartExperienceBundleLoad();
    void OnExperienceLoadComplete();
    void OnExperienceBundleLoadCanceled();
    void OnGameFeaturePluginLoadComplete(const UE::GameFeatures::FResult& Result, FString PluginName,
                                         FString PluginURL);
    void OnExperienceFullLoadCompleted();

    void SetLoadState(EFighterExperienceLoadState NewLoadState, const TCHAR* Reason);
    void FailExperienceLoad(const FString& ErrorMessage);
    bool ValidateStreamableHandle(const TSharedPtr<FStreamableHandle>& Handle, FString& OutError) const;

private:
    UPROPERTY(ReplicatedUsing = OnRep_CurrentExperienceId)
    FPrimaryAssetId CurrentExperienceId;

    UPROPERTY(Transient)
    TObjectPtr<const UFighterExperienceDefinition> CurrentExperience;

    EFighterExperienceLoadState LoadState = EFighterExperienceLoadState::Unloaded;

    int32 NumGameFeaturePluginsLoading = 0;
    TArray<FString> GameFeaturePluginURLs;
    TMap<FString, FString> GameFeaturePluginNamesByURL;
    bool bGameFeaturePluginLoadFailed = false;

    FSoftObjectPath ExperienceAssetPath;
    TSharedPtr<FStreamableHandle> ExperienceDefinitionLoadHandle;
    TSharedPtr<FStreamableHandle> ExperienceBundleLoadHandle;
};
