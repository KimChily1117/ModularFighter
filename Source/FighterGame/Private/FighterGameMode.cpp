// 세션 설정을 읽어 Experience와 경기를 시작한다.
#include "FighterGameMode.h"
#include "FighterCharacterRegistry.h"
#include "FighterPawnData.h"
#include "FighterArena.h"
#include "FighterExperienceDefinition.h"
#include "FighterExperienceManagerComponent.h"
#include "FighterGameState.h"
#include "FighterHUD.h"
#include "FighterPlayerController.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "TimerManager.h"

AFighterGameMode::AFighterGameMode()
{
    DefaultPawnClass = nullptr;
    GameStateClass = AFighterGameState::StaticClass();
    PlayerControllerClass = AFighterPlayerController::StaticClass();
    HUDClass = AFighterHUD::StaticClass();
}

void AFighterGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
    Super::InitGame(MapName, Options, ErrorMessage);

    GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::HandleMatchAssignmentIfNotExpectingOne);
}

void AFighterGameMode::InitGameState()
{
    Super::InitGameState();
    auto* ExperienceManager = GameState->FindComponentByClass<UFighterExperienceManagerComponent>();
    check(ExperienceManager);
    ExperienceManager->CallOrRegister_OnExperienceLoaded(
        FOnFighterExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::OnExperienceLoaded));
}

void AFighterGameMode::HandleMatchAssignmentIfNotExpectingOne()
{
    const FPrimaryAssetType ExperienceType(UFighterExperienceDefinition::StaticClass()->GetFName());
    FPrimaryAssetId ExperienceId;

    if (UGameplayStatics::HasOption(OptionsString, TEXT("Experience")))
        ExperienceId =
            FPrimaryAssetId(ExperienceType, FName(*UGameplayStatics::ParseOption(OptionsString, TEXT("Experience"))));

    if (!ExperienceId.IsValid())
        ExperienceId = FPrimaryAssetId(ExperienceType, FName(TEXT("EXP_Training")));
    OnMatchAssignmentGiven(ExperienceId);
}

void AFighterGameMode::OnMatchAssignmentGiven(FPrimaryAssetId ExperienceId)
{
    check(ExperienceId.IsValid());
    auto* ExperienceManager = GameState->FindComponentByClass<UFighterExperienceManagerComponent>();
    check(ExperienceManager);
    ExperienceManager->ServerSetCurrentExperience(ExperienceId);
    FTimerHandle Watchdog;
    GetWorldTimerManager().SetTimer(Watchdog, this, &ThisClass::CheckExperienceLoaded, 30.f, false);
}

void AFighterGameMode::CheckExperienceLoaded()
{
    const auto* ExperienceManager =
        GameState ? GameState->FindComponentByClass<UFighterExperienceManagerComponent>() : nullptr;

    if (ExperienceManager && ExperienceManager->IsExperienceLoaded())
        return;
    UE_LOG(LogTemp, Error, TEXT("FIGHTER_EXPERIENCE FAIL: %s did not load within 30 s"),
           ExperienceManager ? *ExperienceManager->GetCurrentExperienceId().ToString() : TEXT("(no manager)"));

    if (FApp::IsUnattended())
        FPlatformMisc::RequestExitWithStatus(false, 3);
}

void AFighterGameMode::OnExperienceLoaded(const UFighterExperienceDefinition* CurrentExperience)
{
    const FName ExperienceName = CurrentExperience->GetPrimaryAssetId().PrimaryAssetName;
    TArray<TObjectPtr<UFighterPawnData>> Fighters;
    bool bAllAvailable = true;
    const UFighterCharacterRegistry* Registry = UFighterCharacterRegistry::Get();

    for (const TSoftObjectPtr<UFighterPawnData>& Slot : CurrentExperience->Fighters)
    {
        UFighterPawnData* PawnData = Registry ? Registry->FindAvailable(Slot.ToSoftObjectPath()) : nullptr;

        if (!PawnData)
        {
            bAllAvailable = false;
            UE_LOG(LogTemp, Error,
                   TEXT("FIGHTER_CHARACTER unavailable: %s needs %s, which no active character feature registered"),
                   *ExperienceName.ToString(), *Slot.ToString());
        }

        Fighters.Add(PawnData);
    }

    if (!bAllAvailable && FApp::IsUnattended())
    {
        FPlatformMisc::RequestExitWithStatus(false, 4);

        return;
    }

    const FTransform Origin;
    auto* Arena = GetWorld()->SpawnActorDeferred<AFighterArena>(AFighterArena::StaticClass(), Origin);
    Arena->SetSessionSettings(CurrentExperience->Session, ExperienceName, Fighters);
    Arena->FinishSpawning(Origin);
}
