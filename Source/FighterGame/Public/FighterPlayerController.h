// 플레이어 컨트롤러.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FighterPlayerController.generated.h"

UCLASS()

class FIGHTERGAME_API AFighterPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AFighterPlayerController();
    virtual void PreInitializeComponents() override;
    virtual void BeginPlay() override;
    virtual void ReceivedPlayer() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
