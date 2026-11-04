// 코어 상태를 GAS 태그와 어트리뷰트로 옮긴다. 판정은 코어가 맡는다.
#include "FighterAbilityHost.h"
#include "NativeGameplayTags.h"
#include "FighterCombatCues.h"
#include "AbilitySystemGlobals.h"
#include "GameplayCueManager.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_MoveAccepted, "Fighter.Event.MoveAccepted");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Attacking, "Fighter.State.Attacking");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Stunned, "Fighter.State.Stunned");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Guarding, "Fighter.State.Guarding");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_KO, "Fighter.State.KO");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Buffered, "Fighter.State.CommandBuffered");

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Falling, "Fighter.State.Reaction.Falling");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Down, "Fighter.State.Reaction.Down");

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Kazuya, "Fighter.Character.Kazuya");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_Paul, "Fighter.Character.Paul");

namespace
{
bool IsLying(const modular_fighter::Fighter& Fighter)
{
    const auto* Reaction = modular_fighter::GetReactionDefinition(Fighter.reaction);

    return Reaction && (Reaction->flags & modular_fighter::ReactionLying) != 0;
}
}

UFighterMoveAbility::UFighterMoveAbility()
{
    InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
    NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalOnly;
    bRetriggerInstancedAbility = true;
    FAbilityTriggerData Trigger;
    Trigger.TriggerTag = TAG_MoveAccepted;
    Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
    AbilityTriggers.Add(Trigger);
}

void UFighterMoveAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                          const FGameplayAbilityActorInfo* ActorInfo,
                                          const FGameplayAbilityActivationInfo ActivationInfo,
                                          const FGameplayEventData* TriggerEventData)
{
    auto* Host = ActorInfo ? Cast<AFighterAbilityHost>(ActorInfo->AvatarActor.Get()) : nullptr;

    if (!Host || !TriggerEventData)
    {
        EndAbility(Handle, ActorInfo, ActivationInfo, false, true);

        return;
    }

    Host->OnMoveAbilityActivated(*TriggerEventData);
}

AFighterAbilityHost::AFighterAbilityHost()
{
    PrimaryActorTick.bCanEverTick = false;
    RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
    AbilitySystem->SetIsReplicated(false);
    Attributes = CreateDefaultSubobject<UFighterAttributeSet>(TEXT("CombatMirror"));
}

void AFighterAbilityHost::InitializeCharacter(modular_fighter::CharacterId Character,
                                              const UFighterPawnData* InPawnData)
{
    AbilitySystem->InitAbilityActorInfo(this, this);
    AbilitySystem->AddAttributeSetSubobject(Attributes.Get());
    AbilitySystem->CancelAllAbilities();
    GrantedHandles.TakeFromAbilitySystem(AbilitySystem);

    for (const auto Handle : FallbackHandles)
        AbilitySystem->ClearAbility(Handle);
    FallbackHandles.Reset();
    PawnData = InPawnData;

    if (!PawnData)
    {
        auto* Fallback = NewObject<UFighterPawnData>(this);
        Fallback->CoreCharacter = static_cast<EFighterCharacter>(Character);
        Fallback->DisplayName = FText::FromString(TEXT("TRAINING"));
        PawnData = Fallback;
        FallbackHandles.Add(AbilitySystem->GiveAbility(FGameplayAbilitySpec(UFighterMoveAbility::StaticClass(), 1)));
    }

    ensureMsgf(PawnData->GetCoreCharacter() == Character, TEXT("Pawn data %s does not play the core character"),
               *PawnData->GetName());

    for (const UFighterAbilitySet* AbilitySet : PawnData->AbilitySets)
        if (AbilitySet)
            AbilitySet->GiveToAbilitySystem(AbilitySystem, &GrantedHandles,
                                            const_cast<UFighterPawnData*>(PawnData.Get()));

    if (AppliedCharacterTag.IsValid())
        AbilitySystem->SetLooseGameplayTagCount(AppliedCharacterTag, 0);
    AppliedCharacterTag = PawnData->CharacterTag;

    if (AppliedCharacterTag.IsValid())
        AbilitySystem->SetLooseGameplayTagCount(AppliedCharacterTag, 1);
    bInitialized = true;
    ResetPresentation();
}

void AFighterAbilityHost::ResetPresentation()
{
    AbilitySystem->CancelAllAbilities();
    bObserved = false;
    LastFrame = 0;
    LastMoveFrame = 0;
    LastCommand = 0;
    PresentedCommand = 0;
    ActivationCount = 0;
    EmittedCueKeys.Reset();
    ReceivedCues.Reset();

    for (const FGameplayTag Tag :
         {FGameplayTag(TAG_Attacking), FGameplayTag(TAG_Stunned), FGameplayTag(TAG_Guarding), FGameplayTag(TAG_KO),
          FGameplayTag(TAG_Buffered), FGameplayTag(TAG_Falling), FGameplayTag(TAG_Down)})
        AbilitySystem->SetLooseGameplayTagCount(Tag, 0);
}

// 코어 상태를 ASC에 반영한다.
void AFighterAbilityHost::Synchronize(const modular_fighter::Fighter& Fighter, uint32 Frame,
                                      bool bEmitPresentationEvents)
{
    if (!bInitialized)
        return;
    const uint16 Command = static_cast<uint16>(Fighter.active_command);
    const bool bRewind = bObserved && Frame < LastFrame;
    const bool bNewSample = !bObserved || Frame != LastFrame;
    const bool bMoveStart = Fighter.attack_frame != 0 && (!bObserved || LastMoveFrame == 0 || Command != LastCommand ||
                                                          Fighter.attack_frame < LastMoveFrame);
    AbilitySystem->SetNumericAttributeBase(UFighterAttributeSet::GetHealthAttribute(), Fighter.hp);
    AbilitySystem->SetNumericAttributeBase(UFighterAttributeSet::GetMaximumHealthAttribute(),
                                           modular_fighter::InitialHp);
    AbilitySystem->SetNumericAttributeBase(UFighterAttributeSet::GetMoveFrameAttribute(), Fighter.attack_frame);
    AbilitySystem->SetLooseGameplayTagCount(TAG_Attacking, Fighter.attack_frame != 0 ? 1 : 0);
    AbilitySystem->SetLooseGameplayTagCount(
        TAG_Stunned, (Fighter.stun_frames != 0 || Fighter.reaction != modular_fighter::ReactionKind::None) ? 1 : 0);
    AbilitySystem->SetLooseGameplayTagCount(TAG_KO, Fighter.hp == 0 ? 1 : 0);
    AbilitySystem->SetLooseGameplayTagCount(
        TAG_Guarding, (Fighter.previous_buttons & modular_fighter::Guard) && !Fighter.attack_frame &&
                              !Fighter.stun_frames && Fighter.reaction == modular_fighter::ReactionKind::None &&
                              Fighter.hp
                          ? 1
                          : 0);
    AbilitySystem->SetLooseGameplayTagCount(TAG_Buffered, Fighter.pending_frames != 0 ? 1 : 0);
    const auto* Reaction = modular_fighter::GetReactionDefinition(Fighter.reaction);
    const bool bLying = Reaction && (Reaction->flags & modular_fighter::ReactionLying) != 0;
    AbilitySystem->SetLooseGameplayTagCount(TAG_Falling, Reaction && !bLying ? 1 : 0);
    AbilitySystem->SetLooseGameplayTagCount(TAG_Down, bLying ? 1 : 0);

    if (!Fighter.attack_frame || bRewind || !bEmitPresentationEvents)
        AbilitySystem->CancelAllAbilities();

    if (bNewSample && bMoveStart && !bRewind && bEmitPresentationEvents)
    {
        FGameplayEventData Event;
        Event.EventTag = TAG_MoveAccepted;
        Event.Instigator = this;
        Event.Target = this;
        Event.EventMagnitude = Command;
        AbilitySystem->HandleGameplayEvent(TAG_MoveAccepted, &Event);
    }

    PresentedCommand = Command;
    LastCommand = Command;
    LastMoveFrame = Fighter.attack_frame;
    LastFrame = Frame;
    bObserved = true;
}

void AFighterAbilityHost::OnMoveAbilityActivated(const FGameplayEventData& Event)
{
    PresentedCommand = static_cast<uint16>(Event.EventMagnitude);
    ++ActivationCount;
}

bool AFighterAbilityHost::IsMirrorOf(const modular_fighter::Fighter& Fighter) const
{
    return bInitialized && Attributes->Health.GetCurrentValue() == Fighter.hp &&
           Attributes->MoveFrame.GetCurrentValue() == Fighter.attack_frame &&
           AbilitySystem->HasMatchingGameplayTag(TAG_Attacking) == (Fighter.attack_frame != 0) &&
           AbilitySystem->HasMatchingGameplayTag(TAG_Stunned) ==
               (Fighter.stun_frames != 0 || Fighter.reaction != modular_fighter::ReactionKind::None) &&
           AbilitySystem->HasMatchingGameplayTag(TAG_KO) == (Fighter.hp == 0) &&
           AbilitySystem->HasMatchingGameplayTag(TAG_Buffered) == (Fighter.pending_frames != 0) &&
           AbilitySystem->HasMatchingGameplayTag(TAG_Down) == IsLying(Fighter) &&
           AbilitySystem->HasMatchingGameplayTag(TAG_Falling) ==
               (Fighter.reaction != modular_fighter::ReactionKind::None && !IsLying(Fighter));
}

// 전투 큐를 GameplayCue로 실행한다.
bool AFighterAbilityHost::ExecuteCombatCue(const FFighterCombatCue& Cue, const FVector& Location,
                                           bool bEmitPresentationEvents)
{
    if (!bInitialized)
        return false;

    constexpr uint32 KeepFrames = 600;

    if (Cue.Frame > KeepFrames)
        for (auto It = EmittedCueKeys.CreateIterator(); It; ++It)
            if ((*It >> 32) + KeepFrames < Cue.Frame)
                It.RemoveCurrent();
    bool bAlreadyInSet = false;
    EmittedCueKeys.Add(Cue.Key(), &bAlreadyInSet);

    if (bAlreadyInSet || !bEmitPresentationEvents)
        return false;
    FGameplayCueParameters Parameters;
    Parameters.Location = Location;
    Parameters.RawMagnitude = static_cast<float>(Cue.Damage);
    Parameters.NormalizedMagnitude = Cue.bAirborneContact ? 1.f : 0.f;

    if (Cue.bTornadoContact)
        Parameters.AggregatedSourceTags.AddTag(FighterGameplayTags::Fighter_CueContext_Tornado);
    Parameters.AbilityLevel = Cue.Command;
    Parameters.Instigator = this;
    Parameters.EffectCauser = this;
    AbilitySystem->ExecuteGameplayCue(Cue.Tag, Parameters);

    return true;
}

void AFighterAbilityHost::HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag,
                                            EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters)
{
    IGameplayCueInterface::HandleGameplayCue(Self, GameplayCueTag, EventType, Parameters);

    if (EventType != EGameplayCueEvent::Executed)
        return;
    ++ReceivedCues.FindOrAdd(GameplayCueTag);
    OnCombatCue.Broadcast(GameplayCueTag, Parameters);
}

int32 AFighterAbilityHost::GetReceivedCueTotal() const
{
    int32 Total = 0;

    for (const auto& Pair : ReceivedCues)
        Total += Pair.Value;

    return Total;
}
