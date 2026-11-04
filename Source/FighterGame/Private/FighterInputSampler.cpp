// Enhanced Input 이벤트를 프레임 입력으로 모은다.
#include "FighterInputSampler.h"
#include "FighterGameplayTags.h"
#include "FighterInputComponent.h"
#include "FighterInputConfig.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputMappingContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterInputSampler)

DEFINE_LOG_CATEGORY_STATIC(LogFighterInput, Log, All);
namespace Physical = modular_fighter::input;

UFighterInputSampler::UFighterInputSampler(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
}

uint8 UFighterInputSampler::FightBit(const FGameplayTag& InputTag)
{
    using namespace FighterGameplayTags;
    const FGameplayTag Order[] = {InputTag_Move_Up,   InputTag_Move_Down, InputTag_Move_Back, InputTag_Move_Forward,
                                  InputTag_Button_LP, InputTag_Button_RP, InputTag_Button_LK, InputTag_Button_RK};

    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Order); ++Index)
        if (Order[Index] == InputTag)
            return static_cast<uint8>(1u << Index);

    return 0;
}

bool UFighterInputSampler::InitializePlayerInput(APlayerController* PlayerController,
                                                 UInputComponent* PlayerInputComponent,
                                                 const UFighterInputConfig* InputConfig)
{
    const TCHAR* BlockedBy = nullptr;
    ULocalPlayer* LocalPlayer =
        PlayerController && PlayerController->IsLocalController() ? PlayerController->GetLocalPlayer() : nullptr;
    UEnhancedInputLocalPlayerSubsystem* Subsystem =
        LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    UFighterInputComponent* FighterIC = Cast<UFighterInputComponent>(PlayerInputComponent);

    if (!LocalPlayer)
        BlockedBy = TEXT("LocalPlayerController");
    else if (!Subsystem)
        BlockedBy = TEXT("EnhancedInputSubsystem");
    else if (!InputConfig)
        BlockedBy = TEXT("InputConfig");
    else if (!FighterIC)
        BlockedBy = TEXT("FighterInputComponent");

    if (BlockedBy)
    {
        UE_LOG(LogFighterInput, Warning, TEXT("FIGHTER_INPUT deferred: blocked by %s (component %s)"), BlockedBy,
               *GetNameSafe(PlayerInputComponent));

        return false;
    }

    if (bReadyToBindInputs && BoundInputComponent.Get() == FighterIC)
        return true;
    RemovePlayerInput();

    FModifyContextOptions Options = {};
    Options.bIgnoreAllPressedKeysUntilRelease = false;

    for (const FFighterMappableConfigPair& Pair : DefaultInputMappings)
    {
        if (!Pair.bShouldActivateAutomatically)
            continue;

        if (const UInputMappingContext* Context = Pair.Config.LoadSynchronous())
        {
            Subsystem->AddMappingContext(Context, Pair.Priority, Options);
            AddedContexts.Add(Context);
            ++MappingCount;
        }
        else
            UE_LOG(LogFighterInput, Error, TEXT("FIGHTER_INPUT mapping context missing: %s"), *Pair.Config.ToString());
    }

    TArray<uint32> FightHandles;
    FighterIC->BindAbilityActions(InputConfig, this, &ThisClass::Input_FightTagPressed,
                                  &ThisClass::Input_FightTagReleased, FightHandles);
    TArray<uint32> CommandHandles;
    FighterIC->BindNativeTagActions(InputConfig, ETriggerEvent::Started, this, &ThisClass::Input_CommandTagStarted,
                                    CommandHandles);
    InputBindingHandles.Append(FightHandles);
    InputBindingHandles.Append(CommandHandles);

    BoundInputComponent = FighterIC;
    BoundController = PlayerController;
    BoundConfig = InputConfig;
    ResetState();
    SeenFightMask = 0;
    SeenCommands.Reset();

    bReadyToBindInputs = FightHandles.Num() > 0;
    UE_LOG(LogFighterInput, Display,
           TEXT("FIGHTER_INPUT %s config=%s mappings=%d fight_bindings=%d command_bindings=%d"),
           bReadyToBindInputs ? TEXT("ready") : TEXT("incomplete"), *GetNameSafe(InputConfig), MappingCount,
           FightHandles.Num(), CommandHandles.Num());

    if (!bReadyToBindInputs)
        RemovePlayerInput();

    return bReadyToBindInputs;
}

void UFighterInputSampler::RemovePlayerInput()
{
    if (UEnhancedInputComponent* IC = Cast<UEnhancedInputComponent>(BoundInputComponent.Get()))
        for (const uint32 Handle : InputBindingHandles)
            IC->RemoveBindingByHandle(Handle);
    InputBindingHandles.Reset();
    const APlayerController* Controller = BoundController.Get();
    const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;

    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
        for (const UInputMappingContext* Context : AddedContexts)
            Subsystem->RemoveMappingContext(Context);
    AddedContexts.Reset();
    MappingCount = 0;
    BoundInputComponent.Reset();
    BoundController.Reset();
    BoundConfig = nullptr;
    bReadyToBindInputs = false;
    ResetState();
}

void UFighterInputSampler::Input_FightTagPressed(FGameplayTag InputTag)
{
    const uint8 Bit = FightBit(InputTag);

    if ((HeldFight & Bit) == 0)
        PendingFight |= Bit;
    HeldFight |= Bit;
    SeenFightMask |= Bit;
}

void UFighterInputSampler::Input_FightTagReleased(FGameplayTag InputTag)
{
    HeldFight &= ~FightBit(InputTag);
}

void UFighterInputSampler::Input_CommandTagStarted(FGameplayTag InputTag)
{
    PendingCommands.AddTag(InputTag);
    SeenCommands.AddTag(InputTag);
}

// 이번 프레임의 입력 상태를 만든다.
Physical::RawDigitalState UFighterInputSampler::Sample()
{
    const uint8 Fight = HeldFight | (PendingFight & 0xF0);
    PendingFight = 0;
    Physical::RawDigitalState Raw;
    Raw.up = (Fight & 1) != 0;
    Raw.down = (Fight & 2) != 0;
    Raw.left = (Fight & 4) != 0;
    Raw.right = (Fight & 8) != 0;

    if (Fight & 16)
        Raw.attacks |= Physical::LP;

    if (Fight & 32)
        Raw.attacks |= Physical::RP;

    if (Fight & 64)
        Raw.attacks |= Physical::LK;

    if (Fight & 128)
        Raw.attacks |= Physical::RK;

    return Raw;
}

bool UFighterInputSampler::ConsumeCommand(const FGameplayTag& CommandTag)
{
    return PendingCommands.RemoveTag(CommandTag);
}

void UFighterInputSampler::ResetState()
{
    HeldFight = 0;
    PendingFight = 0;
    PendingCommands.Reset();
}
