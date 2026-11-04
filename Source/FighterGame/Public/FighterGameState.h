// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience 관리자를 보유하는 게임 상태.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "FighterGameState.generated.h"

class UFighterExperienceManagerComponent;

UCLASS()

class FIGHTERGAME_API AFighterGameState : public AGameStateBase
{
    GENERATED_BODY()
public:
    AFighterGameState();

    UPROPERTY()
    TObjectPtr<UFighterExperienceManagerComponent> ExperienceManagerComponent;
};
