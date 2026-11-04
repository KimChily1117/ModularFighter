// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Gameplay Cue 경로 추가 액션 구현.
#include "GameFeatureAction_AddGameplayCuePath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureAction_AddGameplayCuePath)

UGameFeatureAction_AddGameplayCuePath::UGameFeatureAction_AddGameplayCuePath()
{
    DirectoryPathsToAdd.Add(FDirectoryPath{TEXT("/GameplayCues")});
}
