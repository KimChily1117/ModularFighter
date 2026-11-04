// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Game Feature 정책과 상태 관찰자.
#pragma once

#include "Containers/Array.h"
#include "GameFeatureStateChangeObserver.h"
#include "GameFeaturesProjectPolicies.h"
#include "UObject/Object.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "FighterGameFeaturePolicy.generated.h"

UCLASS()

class FIGHTERGAME_API UFighterGameFeaturePolicy : public UDefaultGameFeaturesProjectPolicies
{
    GENERATED_BODY()
public:
    UFighterGameFeaturePolicy(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

    virtual void InitGameFeatureManager() override;
    virtual void ShutdownGameFeatureManager() override;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UObject>> Observers;
};

UCLASS()

class FIGHTERGAME_API UFighterGameFeature_AddGameplayCuePaths : public UObject, public IGameFeatureStateChangeObserver
{
    GENERATED_BODY()
public:
    virtual void OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName,
                                          const FString& PluginURL) override;
    virtual void OnGameFeatureUnregistering(const UGameFeatureData* GameFeatureData, const FString& PluginName,
                                            const FString& PluginURL) override;
};

UCLASS()

class FIGHTERGAME_API UFighterGameFeature_LogStateChanges : public UObject, public IGameFeatureStateChangeObserver
{
    GENERATED_BODY()
public:
    virtual void OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData, const FString& PluginName,
                                          const FString& PluginURL) override;
    virtual void OnGameFeatureActivating(const UGameFeatureData* GameFeatureData, const FString& PluginURL) override;
    virtual void OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData,
                                           FGameFeatureDeactivatingContext& Context, const FString& PluginURL) override;
};
