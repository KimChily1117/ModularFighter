// Lyra 샘플의 구조를 참고해 이 프로젝트에 맞게 이식했다.
// Experience를 비동기로 불러와 Game Feature를 활성화한다.
#include "FighterExperienceManagerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogFighterExperience, Log, All);

#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFeatureAction.h"
#include "FighterExperienceManager.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeaturesSubsystemSettings.h"
#include "GameFramework/Actor.h"
#include "FighterAssetManager.h"
#include "FighterExperienceActionSet.h"
#include "FighterExperienceDefinition.h"
#include "Net/UnrealNetwork.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterExperienceManagerComponent)

namespace FighterExperienceLog
{
const TCHAR* GetLoadStateName(EFighterExperienceLoadState State)
{
    switch (State)
    {
    case EFighterExperienceLoadState::Unloaded:
        return TEXT("Unloaded");
    case EFighterExperienceLoadState::Loading:
        return TEXT("Loading");
    case EFighterExperienceLoadState::LoadingGameFeatures:
        return TEXT("LoadingGameFeatures");
    case EFighterExperienceLoadState::ExecutingActions:
        return TEXT("ExecutingActions");
    case EFighterExperienceLoadState::Loaded:
        return TEXT("Loaded");
    case EFighterExperienceLoadState::Failed:
        return TEXT("Failed");
    case EFighterExperienceLoadState::Deactivating:
        return TEXT("Deactivating");
    default:
        return TEXT("Unknown");
    }
}

const TCHAR* GetNetModeName(const UFighterExperienceManagerComponent* Component)
{
    const UWorld* World = Component ? Component->GetWorld() : nullptr;

    if (!World)
    {
        return TEXT("NoWorld");
    }

    switch (World->GetNetMode())
    {
    case NM_Standalone:
        return TEXT("Standalone");
    case NM_DedicatedServer:
        return TEXT("DedicatedServer");
    case NM_ListenServer:
        return TEXT("ListenServer");
    case NM_Client:
        return TEXT("Client");
    default:
        return TEXT("Unknown");
    }
}

const TCHAR* GetRoleName(const UFighterExperienceManagerComponent* Component)
{
    const AActor* Owner = Component ? Component->GetOwner() : nullptr;

    if (!Owner)
    {
        return TEXT("None");
    }

    switch (Owner->GetLocalRole())
    {
    case ROLE_None:
        return TEXT("None");
    case ROLE_SimulatedProxy:
        return TEXT("SimulatedProxy");
    case ROLE_AutonomousProxy:
        return TEXT("AutonomousProxy");
    case ROLE_Authority:
        return TEXT("Authority");
    default:
        return TEXT("Unknown");
    }
}
}

UFighterExperienceManagerComponent::UFighterExperienceManagerComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    SetIsReplicatedByDefault(true);
}

void UFighterExperienceManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(UFighterExperienceManagerComponent, CurrentExperienceId);
}

void UFighterExperienceManagerComponent::CallOrRegister_OnExperienceLoaded(
    FOnFighterExperienceLoaded::FDelegate&& Delegate)
{
    if (IsExperienceLoaded())
    {
        Delegate.Execute(CurrentExperience);
    }
    else
    {
        OnExperienceLoaded.Add(MoveTemp(Delegate));
    }
}

void UFighterExperienceManagerComponent::ServerSetCurrentExperience(FPrimaryAssetId ExperienceId)
{
    AActor* Owner = GetOwner();

    if (!Owner || !Owner->HasAuthority())
    {
        UE_LOG(
            LogFighterExperience, Error,
            TEXT("[Experience] Event=ServerSetRejected NetMode=%s OwnerRole=%s PrimaryAssetId=%s Reason=NotAuthority"),
            FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
            *ExperienceId.ToString());

        return;
    }

    if (!ExperienceId.IsValid())
    {
        FailExperienceLoad(TEXT("Server selected an invalid PrimaryAssetId"));

        return;
    }

    if (CurrentExperienceId.IsValid() || LoadState != EFighterExperienceLoadState::Unloaded)
    {
        UE_LOG(
            LogFighterExperience, Error,
            TEXT(
                "[Experience] Event=ServerSetRejected NetMode=%s OwnerRole=%s PrimaryAssetId=%s ExistingPrimaryAssetId=%s LoadState=%s Reason=ExperienceAlreadySelected"),
            FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
            *ExperienceId.ToString(), *CurrentExperienceId.ToString(),
            FighterExperienceLog::GetLoadStateName(LoadState));

        return;
    }

    CurrentExperienceId = ExperienceId;
    Owner->ForceNetUpdate();

    UE_LOG(LogFighterExperience, Log,
           TEXT("[Experience] Event=ServerSelected NetMode=%s OwnerRole=%s PrimaryAssetId=%s"),
           FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
           *CurrentExperienceId.ToString());

    StartExperienceLoad();
}

void UFighterExperienceManagerComponent::OnRep_CurrentExperienceId()
{
    UE_LOG(LogFighterExperience, Log,
           TEXT("[Experience] Event=RepNotify NetMode=%s OwnerRole=%s PrimaryAssetId=%s LoadState=%s"),
           FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
           *CurrentExperienceId.ToString(), FighterExperienceLog::GetLoadStateName(LoadState));

    StartExperienceLoad();
}

// 에셋 로딩을 시작한다.
void UFighterExperienceManagerComponent::StartExperienceLoad()
{
    if (!CurrentExperienceId.IsValid())
    {
        FailExperienceLoad(TEXT("Cannot start loading without a valid PrimaryAssetId"));

        return;
    }

    if (LoadState != EFighterExperienceLoadState::Unloaded)
    {
        UE_LOG(
            LogFighterExperience, Warning,
            TEXT(
                "[Experience] Event=LoadIgnored NetMode=%s OwnerRole=%s PrimaryAssetId=%s LoadState=%s Reason=AlreadyLoadingOrLoaded"),
            FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
            *CurrentExperienceId.ToString(), FighterExperienceLog::GetLoadStateName(LoadState));

        return;
    }

    UFighterAssetManager& AssetManager = UFighterAssetManager::Get();
    ExperienceAssetPath = AssetManager.GetPrimaryAssetPath(CurrentExperienceId);

    if (!ExperienceAssetPath.IsValid())
    {
        FailExperienceLoad(FString::Printf(TEXT("AssetManager has no path for %s"), *CurrentExperienceId.ToString()));

        return;
    }

    SetLoadState(EFighterExperienceLoadState::Loading, TEXT("PrimaryAssetAsyncLoadStarted"));

    UE_LOG(
        LogFighterExperience, Log,
        TEXT(
            "[Experience] Event=DefinitionLoadStart NetMode=%s OwnerRole=%s PrimaryAssetId=%s ExperienceAssetPath=%s Async=true"),
        FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
        *CurrentExperienceId.ToString(), *ExperienceAssetPath.ToString());

    ExperienceDefinitionLoadHandle = AssetManager.GetStreamableManager().RequestAsyncLoad(
        ExperienceAssetPath, FStreamableDelegate::CreateUObject(this, &ThisClass::OnExperienceDefinitionLoadComplete),
        FStreamableManager::AsyncLoadHighPriority, false, false,
        FString::Printf(TEXT("ExperienceDefinition %s"), *CurrentExperienceId.ToString()));

    if (!ExperienceDefinitionLoadHandle.IsValid())
    {
        FailExperienceLoad(
            FString::Printf(TEXT("Failed to create async load handle for %s"), *ExperienceAssetPath.ToString()));
    }
    else if (ExperienceDefinitionLoadHandle->IsLoadingInProgress())
    {
        ExperienceDefinitionLoadHandle->BindCancelDelegate(
            FStreamableDelegate::CreateUObject(this, &ThisClass::OnExperienceDefinitionLoadCanceled));
    }
}

void UFighterExperienceManagerComponent::OnExperienceDefinitionLoadComplete()
{
    if (LoadState != EFighterExperienceLoadState::Loading)
    {
        return;
    }

    if (ExperienceDefinitionLoadHandle.IsValid() && ExperienceDefinitionLoadHandle->WasCanceled())
    {
        OnExperienceDefinitionLoadCanceled();

        return;
    }

    UObject* LoadedObject = ExperienceAssetPath.ResolveObject();

    if (!LoadedObject && ExperienceDefinitionLoadHandle.IsValid())
    {
        LoadedObject = ExperienceDefinitionLoadHandle->GetLoadedAsset();
    }

    if (UClass* ExperienceClass = Cast<UClass>(LoadedObject))
    {
        if (!ExperienceClass->IsChildOf(UFighterExperienceDefinition::StaticClass()))
        {
            FailExperienceLoad(FString::Printf(TEXT("Loaded class %s is not a FighterExperienceDefinition"),
                                               *ExperienceClass->GetPathName()));

            return;
        }

        CurrentExperience = Cast<UFighterExperienceDefinition>(ExperienceClass->GetDefaultObject());
    }
    else
    {
        CurrentExperience = Cast<UFighterExperienceDefinition>(LoadedObject);
    }

    if (!CurrentExperience)
    {
        FailExperienceLoad(FString::Printf(TEXT("Loaded object %s is not a FighterExperienceDefinition"),
                                           *GetPathNameSafe(LoadedObject)));

        return;
    }

    const FPrimaryAssetId ResolvedId = CurrentExperience->GetPrimaryAssetId();

    if (ResolvedId != CurrentExperienceId)
    {
        FailExperienceLoad(FString::Printf(TEXT("Resolved Experience ID mismatch. Expected=%s Actual=%s"),
                                           *CurrentExperienceId.ToString(), *ResolvedId.ToString()));

        return;
    }

    UE_LOG(
        LogFighterExperience, Log,
        TEXT(
            "[Experience] Event=DefinitionLoaded NetMode=%s OwnerRole=%s PrimaryAssetId=%s ExperienceObjectPath=%s ExperienceClassPath=%s"),
        FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
        *CurrentExperienceId.ToString(), *CurrentExperience->GetPathName(),
        *CurrentExperience->GetClass()->GetPathName());

    StartExperienceBundleLoad();
}

void UFighterExperienceManagerComponent::OnExperienceDefinitionLoadCanceled()
{
    FailExperienceLoad(
        FString::Printf(TEXT("Experience definition async load was canceled for %s"), *CurrentExperienceId.ToString()));
}

void UFighterExperienceManagerComponent::StartExperienceBundleLoad()
{
    if (!CurrentExperience || LoadState != EFighterExperienceLoadState::Loading)
    {
        FailExperienceLoad(TEXT("Cannot start bundle load without a loaded Experience definition"));

        return;
    }

    TSet<FPrimaryAssetId> BundleAssetList;
    BundleAssetList.Add(CurrentExperienceId);

    for (const TObjectPtr<UFighterExperienceActionSet>& ActionSet : CurrentExperience->ActionSets)
    {
        if (ActionSet)
        {
            const FPrimaryAssetId ActionSetId = ActionSet->GetPrimaryAssetId();

            if (!ActionSetId.IsValid())
            {
                FailExperienceLoad(
                    FString::Printf(TEXT("ActionSet has no valid PrimaryAssetId: %s"), *ActionSet->GetPathName()));

                return;
            }

            BundleAssetList.Add(ActionSetId);
        }
    }

    const ENetMode OwnerNetMode = GetOwner()->GetNetMode();
    const bool bLoadClientBundle = GIsEditor || (OwnerNetMode != NM_DedicatedServer);
    const bool bLoadServerBundle = GIsEditor || (OwnerNetMode != NM_Client);

    TArray<FName> BundlesToLoad;

    if (bLoadClientBundle)
    {
        BundlesToLoad.Add(UGameFeaturesSubsystemSettings::LoadStateClient);
    }

    if (bLoadServerBundle)
    {
        BundlesToLoad.Add(UGameFeaturesSubsystemSettings::LoadStateServer);
    }

    UE_LOG(
        LogFighterExperience, Log,
        TEXT(
            "[Experience] Event=BundleLoadStart NetMode=%s OwnerRole=%s PrimaryAssetId=%s AssetCount=%d ClientBundle=%s ServerBundle=%s"),
        FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
        *CurrentExperienceId.ToString(), BundleAssetList.Num(), bLoadClientBundle ? TEXT("true") : TEXT("false"),
        bLoadServerBundle ? TEXT("true") : TEXT("false"));

    UFighterAssetManager& AssetManager = UFighterAssetManager::Get();
    TSharedPtr<FStreamableHandle> BundleStateHandle = AssetManager.ChangeBundleStateForPrimaryAssets(
        BundleAssetList.Array(), BundlesToLoad, {}, false, FStreamableDelegate(),
        FStreamableManager::AsyncLoadHighPriority);

    if (BundleStateHandle.IsValid() && BundleStateHandle->IsLoadingInProgress())
    {
        ExperienceBundleLoadHandle = AssetManager.GetStreamableManager().CreateCombinedHandle({BundleStateHandle});

        if (!ExperienceBundleLoadHandle.IsValid())
        {
            FailExperienceLoad(TEXT("Failed to create Experience bundle completion handle"));

            return;
        }

        ExperienceBundleLoadHandle->BindCompleteDelegate(
            FStreamableDelegate::CreateUObject(this, &ThisClass::OnExperienceLoadComplete));
        ExperienceBundleLoadHandle->BindCancelDelegate(
            FStreamableDelegate::CreateUObject(this, &ThisClass::OnExperienceBundleLoadCanceled));
    }
    else
    {
        ExperienceBundleLoadHandle = BundleStateHandle;
        OnExperienceLoadComplete();
    }
}

void UFighterExperienceManagerComponent::OnExperienceLoadComplete()
{
    if (LoadState != EFighterExperienceLoadState::Loading)
    {
        return;
    }

    FString StreamableError;

    if (!ValidateStreamableHandle(ExperienceBundleLoadHandle, StreamableError))
    {
        FailExperienceLoad(StreamableError);

        return;
    }

    GameFeaturePluginURLs.Reset();
    GameFeaturePluginNamesByURL.Reset();

    for (const FString& PluginName : CurrentExperience->GameFeaturesToEnable)
    {
        FString PluginURL;

        if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(PluginName, PluginURL))
        {
            FailExperienceLoad(FString::Printf(TEXT("No GameFeature plugin URL found for %s"), *PluginName));

            return;
        }

        GameFeaturePluginURLs.AddUnique(PluginURL);
        GameFeaturePluginNamesByURL.Add(PluginURL, PluginName);
    }

    NumGameFeaturePluginsLoading = GameFeaturePluginURLs.Num();
    bGameFeaturePluginLoadFailed = false;

    if (NumGameFeaturePluginsLoading > 0)
    {
        SetLoadState(EFighterExperienceLoadState::LoadingGameFeatures, TEXT("AssetBundlesLoaded"));

        for (const FString& PluginURL : GameFeaturePluginURLs)
        {
            const FString PluginName = GameFeaturePluginNamesByURL.FindRef(PluginURL);
            UE_LOG(
                LogFighterExperience, Log,
                TEXT(
                    "[Experience] Event=GameFeatureActivationStart NetMode=%s OwnerRole=%s PrimaryAssetId=%s Plugin=%s PluginURL=%s"),
                FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
                *CurrentExperienceId.ToString(), *PluginName, *PluginURL);

            UFighterExperienceManager::NotifyOfPluginActivation(PluginURL);
            UGameFeaturesSubsystem::Get().LoadAndActivateGameFeaturePlugin(
                PluginURL, FGameFeaturePluginLoadComplete::CreateUObject(
                               this, &ThisClass::OnGameFeaturePluginLoadComplete, PluginName, PluginURL));
        }
    }
    else
    {
        OnExperienceFullLoadCompleted();
    }
}

void UFighterExperienceManagerComponent::OnExperienceBundleLoadCanceled()
{
    FailExperienceLoad(
        FString::Printf(TEXT("Experience bundle load was canceled for %s"), *CurrentExperienceId.ToString()));
}

void UFighterExperienceManagerComponent::OnGameFeaturePluginLoadComplete(const UE::GameFeatures::FResult& Result,
                                                                         FString PluginName, FString PluginURL)
{
    const bool bSucceeded = !Result.HasError();

    if (bSucceeded)
    {
        UE_LOG(
            LogFighterExperience, Log,
            TEXT(
                "[Experience] Event=GameFeatureActivationComplete NetMode=%s OwnerRole=%s PrimaryAssetId=%s Plugin=%s PluginURL=%s Result=Success Detail=%s"),
            FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
            *CurrentExperienceId.ToString(), *PluginName, *PluginURL, *UE::GameFeatures::ToString(Result));
    }
    else
    {
        UE_LOG(
            LogFighterExperience, Error,
            TEXT(
                "[Experience] Event=GameFeatureActivationComplete NetMode=%s OwnerRole=%s PrimaryAssetId=%s Plugin=%s PluginURL=%s Result=Failure Detail=%s"),
            FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
            *CurrentExperienceId.ToString(), *PluginName, *PluginURL, *UE::GameFeatures::ToString(Result));
    }

    if (!bSucceeded)
    {
        bGameFeaturePluginLoadFailed = true;
    }

    if (!ensure(NumGameFeaturePluginsLoading > 0))
    {
        return;
    }

    --NumGameFeaturePluginsLoading;

    if (NumGameFeaturePluginsLoading == 0)
    {
        if (bGameFeaturePluginLoadFailed)
        {
            FailExperienceLoad(TEXT("One or more GameFeature plugins failed to load or activate"));
        }
        else
        {
            OnExperienceFullLoadCompleted();
        }
    }
}

// 로딩 완료 후 대기 중인 콜백을 실행한다.
void UFighterExperienceManagerComponent::OnExperienceFullLoadCompleted()
{
    if (LoadState != EFighterExperienceLoadState::Loading &&
        LoadState != EFighterExperienceLoadState::LoadingGameFeatures)
    {
        FailExperienceLoad(FString::Printf(TEXT("Invalid state before action execution: %s"),
                                           FighterExperienceLog::GetLoadStateName(LoadState)));

        return;
    }

    SetLoadState(EFighterExperienceLoadState::ExecutingActions, TEXT("GameFeaturesActive"));

    FGameFeatureActivatingContext Context;

    if (const FWorldContext* ExistingWorldContext = GEngine->GetWorldContextFromWorld(GetWorld()))
    {
        Context.SetRequiredWorldContextHandle(ExistingWorldContext->ContextHandle);
    }

    auto ActivateListOfActions =
        [this, &Context](const TArray<TObjectPtr<UGameFeatureAction>>& ActionList, const UObject* Source)
    {
        for (UGameFeatureAction* Action : ActionList)
        {
            if (Action)
            {
                UE_LOG(
                    LogFighterExperience, Log,
                    TEXT(
                        "[Experience] Event=ActionExecute NetMode=%s OwnerRole=%s PrimaryAssetId=%s Source=%s Action=%s"),
                    FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
                    *CurrentExperienceId.ToString(), *GetPathNameSafe(Source), *Action->GetPathName());

                Action->OnGameFeatureRegistering();
                Action->OnGameFeatureLoading();
                Action->OnGameFeatureActivating(Context);
            }
        }
    };

    ActivateListOfActions(CurrentExperience->Actions, CurrentExperience);

    for (const TObjectPtr<UFighterExperienceActionSet>& ActionSet : CurrentExperience->ActionSets)
    {
        if (ActionSet)
        {
            ActivateListOfActions(ActionSet->Actions, ActionSet);
        }
    }

    SetLoadState(EFighterExperienceLoadState::Loaded, TEXT("ActionsExecuted"));

    UE_LOG(
        LogFighterExperience, Log,
        TEXT(
            "[Experience] Event=OnExperienceLoadedBroadcast NetMode=%s OwnerRole=%s PrimaryAssetId=%s ExperienceObjectPath=%s ExperienceClassPath=%s LoadState=Loaded"),
        FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
        *CurrentExperienceId.ToString(), *CurrentExperience->GetPathName(),
        *CurrentExperience->GetClass()->GetPathName());

    OnExperienceLoaded.Broadcast(CurrentExperience);
    OnExperienceLoaded.Clear();
}

void UFighterExperienceManagerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    for (const FString& PluginURL : GameFeaturePluginURLs)
    {
        if (UFighterExperienceManager::RequestToDeactivatePlugin(PluginURL))
        {
            UE_LOG(
                LogFighterExperience, Log,
                TEXT(
                    "[Experience] Event=GameFeatureDeactivationStart NetMode=%s OwnerRole=%s PrimaryAssetId=%s Plugin=%s"),
                FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
                *CurrentExperienceId.ToString(), *GameFeaturePluginNamesByURL.FindRef(PluginURL));
            UGameFeaturesSubsystem::Get().DeactivateGameFeaturePlugin(PluginURL);
        }
    }

    if (LoadState == EFighterExperienceLoadState::Loaded && CurrentExperience)
    {
        SetLoadState(EFighterExperienceLoadState::Deactivating, TEXT("EndPlay"));

        FGameFeatureDeactivatingContext Context(TEXT(""), [](FStringView) {});

        if (const FWorldContext* ExistingWorldContext = GEngine->GetWorldContextFromWorld(GetWorld()))
        {
            Context.SetRequiredWorldContextHandle(ExistingWorldContext->ContextHandle);
        }

        auto DeactivateListOfActions = [&Context](const TArray<TObjectPtr<UGameFeatureAction>>& ActionList)
        {
            for (UGameFeatureAction* Action : ActionList)
            {
                if (Action)
                {
                    Action->OnGameFeatureDeactivating(Context);
                    Action->OnGameFeatureUnregistering();
                }
            }
        };
        DeactivateListOfActions(CurrentExperience->Actions);

        for (const TObjectPtr<UFighterExperienceActionSet>& ActionSet : CurrentExperience->ActionSets)
        {
            if (ActionSet)
            {
                DeactivateListOfActions(ActionSet->Actions);
            }
        }

        SetLoadState(EFighterExperienceLoadState::Unloaded, TEXT("ActionsDeactivated"));
    }
}

const UFighterExperienceDefinition* UFighterExperienceManagerComponent::GetCurrentExperienceChecked() const
{
    check(LoadState == EFighterExperienceLoadState::Loaded);
    check(CurrentExperience != nullptr);

    return CurrentExperience;
}

void UFighterExperienceManagerComponent::SetLoadState(EFighterExperienceLoadState NewLoadState, const TCHAR* Reason)
{
    const EFighterExperienceLoadState OldLoadState = LoadState;

    if (OldLoadState == NewLoadState)
    {
        return;
    }

    LoadState = NewLoadState;
    const FString ExperienceObjectPath = CurrentExperience ? CurrentExperience->GetPathName() : TEXT("None");
    const FString ExperienceClassPath = CurrentExperience ? CurrentExperience->GetClass()->GetPathName() : TEXT("None");

    UE_LOG(
        LogFighterExperience, Log,
        TEXT(
            "[Experience] Event=LoadStateChanged NetMode=%s OwnerRole=%s PrimaryAssetId=%s ExperienceObjectPath=%s ExperienceClassPath=%s OldLoadState=%s NewLoadState=%s Reason=%s"),
        FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
        *CurrentExperienceId.ToString(), *ExperienceObjectPath, *ExperienceClassPath,
        FighterExperienceLog::GetLoadStateName(OldLoadState), FighterExperienceLog::GetLoadStateName(NewLoadState),
        Reason);
}

void UFighterExperienceManagerComponent::FailExperienceLoad(const FString& ErrorMessage)
{
    if (LoadState != EFighterExperienceLoadState::Failed)
    {
        SetLoadState(EFighterExperienceLoadState::Failed, TEXT("LoadFailure"));
    }

    UE_LOG(
        LogFighterExperience, Error,
        TEXT(
            "[Experience] Event=LoadFailed NetMode=%s OwnerRole=%s PrimaryAssetId=%s ExperienceAssetPath=%s LoadState=Failed Error=%s"),
        FighterExperienceLog::GetNetModeName(this), FighterExperienceLog::GetRoleName(this),
        *CurrentExperienceId.ToString(), *ExperienceAssetPath.ToString(), *ErrorMessage);
}

bool UFighterExperienceManagerComponent::ValidateStreamableHandle(const TSharedPtr<FStreamableHandle>& Handle,
                                                                  FString& OutError) const
{
    if (!Handle.IsValid())
    {
        return true;
    }

    if (Handle->WasCanceled())
    {
        OutError = TEXT("Streamable handle was canceled");

        return false;
    }

    TArray<FSoftObjectPath> RequestedAssets;
    Handle->GetRequestedAssets(RequestedAssets, true);

    for (const FSoftObjectPath& RequestedAsset : RequestedAssets)
    {
        if (!RequestedAsset.ResolveObject())
        {
            OutError = FString::Printf(TEXT("Required bundled asset failed to load: %s"), *RequestedAsset.ToString());

            return false;
        }
    }

    return true;
}
