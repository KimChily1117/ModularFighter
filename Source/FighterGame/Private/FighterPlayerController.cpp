// 플레이어 컨트롤러 구현.
#include "FighterPlayerController.h"
#include "Components/GameFrameworkComponentManager.h"

AFighterPlayerController::AFighterPlayerController()
{
    bAutoManageActiveCameraTarget = false;
    bShowMouseCursor = false;
}

void AFighterPlayerController::PreInitializeComponents()
{
    Super::PreInitializeComponents();
    UGameFrameworkComponentManager::AddGameFrameworkComponentReceiver(this);
}

void AFighterPlayerController::BeginPlay()
{
    Super::BeginPlay();
    FInputModeGameOnly Mode;
    Mode.SetConsumeCaptureMouseDown(false);
    SetInputMode(Mode);
}

void AFighterPlayerController::ReceivedPlayer()
{
    UGameFrameworkComponentManager::SendGameFrameworkComponentExtensionEvent(
        this, UGameFrameworkComponentManager::NAME_GameActorReady);
    Super::ReceivedPlayer();
}

void AFighterPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    UGameFrameworkComponentManager::RemoveGameFrameworkComponentReceiver(this);
    Super::EndPlay(EndPlayReason);
}
