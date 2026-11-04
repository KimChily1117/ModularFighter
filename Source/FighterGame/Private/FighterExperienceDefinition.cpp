// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience 정의 구현.
#include "FighterExperienceDefinition.h"
#include "FighterExperienceActionSet.h"
#include "GameFeatureAction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterExperienceDefinition)

UFighterExperienceDefinition::UFighterExperienceDefinition(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

#if WITH_EDITORONLY_DATA
void UFighterExperienceDefinition::UpdateAssetBundleData()
{
    Super::UpdateAssetBundleData();

    for (UGameFeatureAction* Action : Actions)
    {
        if (Action)
        {
            Action->AddAdditionalAssetBundleData(AssetBundleData);
        }
    }
}
#endif
