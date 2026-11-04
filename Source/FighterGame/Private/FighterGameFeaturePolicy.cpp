// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Game Feature 로딩 정책.
#include "FighterGameFeaturePolicy.h"
#include "GameFeatureData.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatureAction_AddGameplayCuePath.h"
#include "AbilitySystemGlobals.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterGameFeaturePolicy)

DEFINE_LOG_CATEGORY_STATIC(LogFighterFeature, Log, All);

UFighterGameFeaturePolicy::UFighterGameFeaturePolicy(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

void UFighterGameFeaturePolicy::InitGameFeatureManager()
{
    Observers.Add(NewObject<UFighterGameFeature_AddGameplayCuePaths>());
    Observers.Add(NewObject<UFighterGameFeature_LogStateChanges>());

    UGameFeaturesSubsystem& Subsystem = UGameFeaturesSubsystem::Get();

    for (UObject* Observer : Observers)
    {
        Subsystem.AddObserver(Observer, UGameFeaturesSubsystem::EObserverPluginStateUpdateMode::FutureOnly);
    }

    Super::InitGameFeatureManager();
}

void UFighterGameFeaturePolicy::ShutdownGameFeatureManager()
{
    Super::ShutdownGameFeatureManager();

    UGameFeaturesSubsystem& Subsystem = UGameFeaturesSubsystem::Get();

    for (UObject* Observer : Observers)
    {
        Subsystem.RemoveObserver(Observer);
    }

    Observers.Empty();
}

void UFighterGameFeature_AddGameplayCuePaths::OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData,
                                                                       const FString& PluginName,
                                                                       const FString& PluginURL)
{
    const FString PluginRootPath = TEXT("/") + PluginName;

    for (const UGameFeatureAction* Action : GameFeatureData->GetActions())
    {
        if (const UGameFeatureAction_AddGameplayCuePath* AddGameplayCueGFA =
                Cast<UGameFeatureAction_AddGameplayCuePath>(Action))
        {
            const TArray<FDirectoryPath>& DirsToAdd = AddGameplayCueGFA->GetDirectoryPathsToAdd();

            if (UGameplayCueManager* GCM = UAbilitySystemGlobals::Get().GetGameplayCueManager())
            {
                for (const FDirectoryPath& Directory : DirsToAdd)
                {
                    FString MutablePath = Directory.Path;
                    UGameFeaturesSubsystem::FixPluginPackagePath(MutablePath, PluginRootPath, false);
                    GCM->AddGameplayCueNotifyPath(MutablePath, false);
                    UE_LOG(LogFighterFeature, Display, TEXT("FIGHTER_FEATURE cue path added %s -> %s"), *PluginName,
                           *MutablePath);
                }

                if (!DirsToAdd.IsEmpty())
                {
                    GCM->InitializeRuntimeObjectLibrary();
                }
            }
        }
    }
}

void UFighterGameFeature_AddGameplayCuePaths::OnGameFeatureUnregistering(const UGameFeatureData* GameFeatureData,
                                                                         const FString& PluginName,
                                                                         const FString& PluginURL)
{
    const FString PluginRootPath = TEXT("/") + PluginName;

    for (const UGameFeatureAction* Action : GameFeatureData->GetActions())
    {
        if (const UGameFeatureAction_AddGameplayCuePath* AddGameplayCueGFA =
                Cast<UGameFeatureAction_AddGameplayCuePath>(Action))
        {
            const TArray<FDirectoryPath>& DirsToAdd = AddGameplayCueGFA->GetDirectoryPathsToAdd();

            if (UGameplayCueManager* GCM = UAbilitySystemGlobals::Get().GetGameplayCueManager())
            {
                int32 NumRemoved = 0;

                for (const FDirectoryPath& Directory : DirsToAdd)
                {
                    FString MutablePath = Directory.Path;
                    UGameFeaturesSubsystem::FixPluginPackagePath(MutablePath, PluginRootPath, false);
                    NumRemoved += GCM->RemoveGameplayCueNotifyPath(MutablePath, false);
                }

                ensure(NumRemoved == DirsToAdd.Num());

                if (NumRemoved > 0)
                {
                    GCM->InitializeRuntimeObjectLibrary();
                }
            }
        }
    }
}

void UFighterGameFeature_LogStateChanges::OnGameFeatureRegistering(const UGameFeatureData* GameFeatureData,
                                                                   const FString& PluginName, const FString& PluginURL)
{
    UE_LOG(LogFighterFeature, Display, TEXT("FIGHTER_FEATURE registered %s actions=%d"), *PluginName,
           GameFeatureData ? GameFeatureData->GetActions().Num() : 0);
}

void UFighterGameFeature_LogStateChanges::OnGameFeatureActivating(const UGameFeatureData* GameFeatureData,
                                                                  const FString& PluginURL)
{
    UE_LOG(LogFighterFeature, Display, TEXT("FIGHTER_FEATURE activating %s"), *GetNameSafe(GameFeatureData));
}

void UFighterGameFeature_LogStateChanges::OnGameFeatureDeactivating(const UGameFeatureData* GameFeatureData,
                                                                    FGameFeatureDeactivatingContext& Context,
                                                                    const FString& PluginURL)
{
    UE_LOG(LogFighterFeature, Display, TEXT("FIGHTER_FEATURE deactivating %s"), *GetNameSafe(GameFeatureData));
}
