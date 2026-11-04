// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// 플러그인이 활성화될 때 입력 컨텍스트를 등록한다.
#include "GameFeatureAction_AddInputContextMapping.h"
#include "Components/GameFrameworkComponentManager.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeatureAction_AddInputContextMapping)

DEFINE_LOG_CATEGORY_STATIC(LogFighterInputMapping, Log, All);

void UGameFeatureAction_AddInputContextMapping::OnGameFeatureRegistering()
{
    Super::OnGameFeatureRegistering();

    RegisterInputMappingContexts();
}

void UGameFeatureAction_AddInputContextMapping::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
    FPerContextData& ActiveData = ContextData.FindOrAdd(Context);

    if (!ensure(ActiveData.ExtensionRequestHandles.IsEmpty()) || !ensure(ActiveData.ControllersAddedTo.IsEmpty()))
    {
        Reset(ActiveData);
    }

    Super::OnGameFeatureActivating(Context);
}

void UGameFeatureAction_AddInputContextMapping::OnGameFeatureDeactivating(FGameFeatureDeactivatingContext& Context)
{
    Super::OnGameFeatureDeactivating(Context);
    FPerContextData* ActiveData = ContextData.Find(Context);

    if (ensure(ActiveData))
    {
        Reset(*ActiveData);
    }
}

void UGameFeatureAction_AddInputContextMapping::OnGameFeatureUnregistering()
{
    Super::OnGameFeatureUnregistering();

    UnregisterInputMappingContexts();
}

void UGameFeatureAction_AddInputContextMapping::RegisterInputMappingContexts()
{
    RegisterInputContextMappingsForGameInstanceHandle = FWorldDelegates::OnStartGameInstance.AddUObject(
        this, &UGameFeatureAction_AddInputContextMapping::RegisterInputContextMappingsForGameInstance);

    const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();

    for (TIndirectArray<FWorldContext>::TConstIterator WorldContextIterator = WorldContexts.CreateConstIterator();
         WorldContextIterator; ++WorldContextIterator)
    {
        RegisterInputContextMappingsForGameInstance(WorldContextIterator->OwningGameInstance);
    }
}

void UGameFeatureAction_AddInputContextMapping::RegisterInputContextMappingsForGameInstance(UGameInstance* GameInstance)
{
    if (GameInstance != nullptr && !GameInstance->OnLocalPlayerAddedEvent.IsBoundToObject(this))
    {
        GameInstance->OnLocalPlayerAddedEvent.AddUObject(
            this, &UGameFeatureAction_AddInputContextMapping::RegisterInputMappingContextsForLocalPlayer);
        GameInstance->OnLocalPlayerRemovedEvent.AddUObject(
            this, &UGameFeatureAction_AddInputContextMapping::UnregisterInputMappingContextsForLocalPlayer);

        for (TArray<ULocalPlayer*>::TConstIterator LocalPlayerIterator = GameInstance->GetLocalPlayerIterator();
             LocalPlayerIterator; ++LocalPlayerIterator)
        {
            RegisterInputMappingContextsForLocalPlayer(*LocalPlayerIterator);
        }
    }
}

void UGameFeatureAction_AddInputContextMapping::RegisterInputMappingContextsForLocalPlayer(ULocalPlayer* LocalPlayer)
{
    if (ensure(LocalPlayer))
    {
        if (UEnhancedInputLocalPlayerSubsystem* EISubsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
        {
            if (UEnhancedInputUserSettings* Settings = EISubsystem->GetUserSettings())
            {
                for (const FFighterInputMappingContextAndPriority& Entry : InputMappings)
                {
                    if (!Entry.bRegisterWithSettings)
                    {
                        continue;
                    }

                    if (UInputMappingContext* IMC = Entry.InputMapping.LoadSynchronous())
                    {
                        Settings->RegisterInputMappingContext(IMC);
                    }
                }
            }
        }
    }
}

void UGameFeatureAction_AddInputContextMapping::UnregisterInputMappingContexts()
{
    FWorldDelegates::OnStartGameInstance.Remove(RegisterInputContextMappingsForGameInstanceHandle);
    RegisterInputContextMappingsForGameInstanceHandle.Reset();

    const TIndirectArray<FWorldContext>& WorldContexts = GEngine->GetWorldContexts();

    for (TIndirectArray<FWorldContext>::TConstIterator WorldContextIterator = WorldContexts.CreateConstIterator();
         WorldContextIterator; ++WorldContextIterator)
    {
        UnregisterInputContextMappingsForGameInstance(WorldContextIterator->OwningGameInstance);
    }
}

void UGameFeatureAction_AddInputContextMapping::UnregisterInputContextMappingsForGameInstance(
    UGameInstance* GameInstance)
{
    if (GameInstance != nullptr)
    {
        GameInstance->OnLocalPlayerAddedEvent.RemoveAll(this);
        GameInstance->OnLocalPlayerRemovedEvent.RemoveAll(this);

        for (TArray<ULocalPlayer*>::TConstIterator LocalPlayerIterator = GameInstance->GetLocalPlayerIterator();
             LocalPlayerIterator; ++LocalPlayerIterator)
        {
            UnregisterInputMappingContextsForLocalPlayer(*LocalPlayerIterator);
        }
    }
}

void UGameFeatureAction_AddInputContextMapping::UnregisterInputMappingContextsForLocalPlayer(ULocalPlayer* LocalPlayer)
{
    if (ensure(LocalPlayer))
    {
        if (UEnhancedInputLocalPlayerSubsystem* EISubsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LocalPlayer))
        {
            if (UEnhancedInputUserSettings* Settings = EISubsystem->GetUserSettings())
            {
                for (const FFighterInputMappingContextAndPriority& Entry : InputMappings)
                {
                    if (!Entry.bRegisterWithSettings)
                    {
                        continue;
                    }

                    if (UInputMappingContext* IMC = Entry.InputMapping.Get())
                    {
                        Settings->UnregisterInputMappingContext(IMC);
                    }
                }
            }
        }
    }
}

void UGameFeatureAction_AddInputContextMapping::AddToWorld(const FWorldContext& WorldContext,
                                                           const FGameFeatureStateChangeContext& ChangeContext)
{
    UWorld* World = WorldContext.World();
    UGameInstance* GameInstance = WorldContext.OwningGameInstance;
    FPerContextData& ActiveData = ContextData.FindOrAdd(ChangeContext);

    if ((GameInstance != nullptr) && (World != nullptr) && World->IsGameWorld())
    {
        if (UGameFrameworkComponentManager* ComponentManager =
                UGameInstance::GetSubsystem<UGameFrameworkComponentManager>(GameInstance))
        {
            UGameFrameworkComponentManager::FExtensionHandlerDelegate AddMappingDelegate =
                UGameFrameworkComponentManager::FExtensionHandlerDelegate::CreateUObject(
                    this, &ThisClass::HandleControllerExtension, ChangeContext);
            TSharedPtr<FComponentRequestHandle> ExtensionRequestHandle =
                ComponentManager->AddExtensionHandler(APlayerController::StaticClass(), AddMappingDelegate);

            ActiveData.ExtensionRequestHandles.Add(ExtensionRequestHandle);
        }
    }
}

void UGameFeatureAction_AddInputContextMapping::Reset(FPerContextData& ActiveData)
{
    ActiveData.ExtensionRequestHandles.Empty();

    while (!ActiveData.ControllersAddedTo.IsEmpty())
    {
        TWeakObjectPtr<APlayerController> ControllerPtr = ActiveData.ControllersAddedTo.Top();

        if (ControllerPtr.IsValid())
        {
            RemoveInputMapping(ControllerPtr.Get(), ActiveData);
        }
        else
        {
            ActiveData.ControllersAddedTo.Pop();
        }
    }
}

void UGameFeatureAction_AddInputContextMapping::HandleControllerExtension(AActor* Actor, FName EventName,
                                                                          FGameFeatureStateChangeContext ChangeContext)
{
    APlayerController* AsController = CastChecked<APlayerController>(Actor);
    FPerContextData& ActiveData = ContextData.FindOrAdd(ChangeContext);

    if ((EventName == UGameFrameworkComponentManager::NAME_ExtensionRemoved) ||
        (EventName == UGameFrameworkComponentManager::NAME_ReceiverRemoved))
    {
        RemoveInputMapping(AsController, ActiveData);
    }
    else if (EventName == UGameFrameworkComponentManager::NAME_ExtensionAdded)
    {
        AddInputMappingForPlayer(AsController, ActiveData);
    }
}

void UGameFeatureAction_AddInputContextMapping::AddInputMappingForPlayer(APlayerController* PlayerController,
                                                                         FPerContextData& ActiveData)
{
    if (ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr)
    {
        if (UEnhancedInputLocalPlayerSubsystem* InputSystem =
                LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            int32 Added = 0;

            for (const FFighterInputMappingContextAndPriority& Entry : InputMappings)
            {
                if (const UInputMappingContext* IMC = Entry.InputMapping.LoadSynchronous())
                {
                    InputSystem->AddMappingContext(IMC, Entry.Priority);
                    ++Added;
                }
            }

            ActiveData.ControllersAddedTo.AddUnique(PlayerController);
            UE_LOG(LogFighterInputMapping, Display, TEXT("FIGHTER_INPUT mapping added contexts=%d controller=%s"),
                   Added, *GetNameSafe(PlayerController));
        }
        else
        {
            UE_LOG(
                LogFighterInputMapping, Error,
                TEXT(
                    "Failed to find `UEnhancedInputLocalPlayerSubsystem` for local player. Input mappings will not be added. Make sure you're set to use the EnhancedInput system via config file."));
        }
    }
}

void UGameFeatureAction_AddInputContextMapping::RemoveInputMapping(APlayerController* PlayerController,
                                                                   FPerContextData& ActiveData)
{
    if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
    {
        if (UEnhancedInputLocalPlayerSubsystem* InputSystem =
                LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
        {
            for (const FFighterInputMappingContextAndPriority& Entry : InputMappings)
            {
                if (const UInputMappingContext* IMC = Entry.InputMapping.Get())
                {
                    InputSystem->RemoveMappingContext(IMC);
                }
            }
        }
    }

    ActiveData.ControllersAddedTo.Remove(PlayerController);
}
