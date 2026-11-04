// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience가 요청한 플러그인의 참조 수 관리.
#include "FighterExperienceManager.h"
#include "Engine/Engine.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterExperienceManager)

#if WITH_EDITOR
void UFighterExperienceManager::OnPlayInEditorBegun()
{
    ensure(GameFeaturePluginRequestCountMap.IsEmpty());
    GameFeaturePluginRequestCountMap.Empty();
}

void UFighterExperienceManager::NotifyOfPluginActivation(const FString& PluginURL)
{
    if (GIsEditor)
    {
        UFighterExperienceManager* Manager = GEngine->GetEngineSubsystem<UFighterExperienceManager>();
        check(Manager);
        ++Manager->GameFeaturePluginRequestCountMap.FindOrAdd(PluginURL);
    }
}

bool UFighterExperienceManager::RequestToDeactivatePlugin(const FString& PluginURL)
{
    if (GIsEditor)
    {
        UFighterExperienceManager* Manager = GEngine->GetEngineSubsystem<UFighterExperienceManager>();
        check(Manager);

        int32* Count = Manager->GameFeaturePluginRequestCountMap.Find(PluginURL);

        if (!Count)
            return false;

        if (--(*Count) == 0)
        {
            Manager->GameFeaturePluginRequestCountMap.Remove(PluginURL);

            return true;
        }

        return false;
    }

    return true;
}
#endif
