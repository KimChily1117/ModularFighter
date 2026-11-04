// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Game Feature 플러그인 요청 추적.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "FighterExperienceManager.generated.h"

UCLASS(MinimalAPI)

class UFighterExperienceManager : public UEngineSubsystem
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    void OnPlayInEditorBegun();
    static void NotifyOfPluginActivation(const FString& PluginURL);
    static bool RequestToDeactivatePlugin(const FString& PluginURL);
#else
    static void NotifyOfPluginActivation(const FString& PluginURL) {}

    static bool RequestToDeactivatePlugin(const FString& PluginURL)
    {
        return true;
    }
#endif

private:
    TMap<FString, int32> GameFeaturePluginRequestCountMap;
};
