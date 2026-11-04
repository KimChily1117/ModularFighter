// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 게임 상태 구현.
#include "FighterGameState.h"
#include "FighterExperienceManagerComponent.h"

AFighterGameState::AFighterGameState()
{
    ExperienceManagerComponent =
        CreateDefaultSubobject<UFighterExperienceManagerComponent>(TEXT("ExperienceManagerComponent"));
}
