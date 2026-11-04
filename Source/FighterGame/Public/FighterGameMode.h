// 게임 모드.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FighterGameMode.generated.h"

class UFighterExperienceDefinition;

UCLASS()

class FIGHTERGAME_API AFighterGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AFighterGameMode();
    virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
    virtual void InitGameState() override;

private:
    void HandleMatchAssignmentIfNotExpectingOne();
    void OnMatchAssignmentGiven(FPrimaryAssetId ExperienceId);
    void OnExperienceLoaded(const UFighterExperienceDefinition* CurrentExperience);

    void CheckExperienceLoaded();
};
