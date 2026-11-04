// 연습 모드의 더미 조작과 프레임 정보.
#include "FighterTrainingComponent.h"
#include "FighterArena.h"
#include "FighterPawnData.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FighterTrainingComponent)

namespace Core = modular_fighter;
namespace Physical = modular_fighter::input;

namespace
{
constexpr int32 DummyIntervals[] = {40, 60, 90};

void ApplyDirection(int32 Direction, int8 Forward, Physical::FrameInput& Out)
{
    const int32 Column = (Direction - 1) % 3;
    Out.horizontal = static_cast<int8>(Column == 2 ? Forward : (Column == 0 ? -Forward : 0));
    Out.vertical = static_cast<int8>(Direction >= 7 ? 1 : (Direction <= 3 ? -1 : 0));
}

int32 RelativeDirection(const Core::Input& Input, int8 Facing)
{
    const int32 Horizontal = Input.horizontal * Facing;

    return 5 + Horizontal + 3 * Input.vertical;
}

int32 RelativeDirection(int8 Horizontal, int8 Vertical, int8 Facing)
{
    return 5 + Horizontal * Facing + 3 * Vertical;
}

bool CanActOnStep(const Core::Fighter& Fighter)
{
    if (Fighter.hp == 0 || Fighter.stun_frames != 0 || Fighter.reaction != Core::ReactionKind::None)
        return false;

    if (Fighter.attack_frame == 0)
        return true;
    const auto* Attack = Core::GetAttackDefinition(Fighter.character, Fighter.active_command);

    return Attack && Attack->source_data &&
           Fighter.attack_frame == Core::AttackEndFrame(Fighter.character, Fighter.active_command);
}
}

UFighterTrainingComponent::UFighterTrainingComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryComponentTick.bCanEverTick = false;
}

const AFighterArena* UFighterTrainingComponent::GetArena() const
{
    return Cast<AFighterArena>(GetOwner());
}

void UFighterTrainingComponent::CycleDummyAttack()
{
    DummyAttackMode = (DummyAttackMode + 1) % 6;
    DummyWait = 0;
    DummyScriptFrame = -1;
}

void UFighterTrainingComponent::ResetTrackers()
{
    AdvAttacker = -1;
    Combos[0] = {};
    Combos[1] = {};
}

// 설정에 따라 더미 입력을 만든다.
UFighterTrainingComponent::FDummyControl UFighterTrainingComponent::ControlDummy(const Core::State& State,
                                                                                 int32 DummyIndex, bool bPadDriven,
                                                                                 Physical::FrameInput& InOutInput,
                                                                                 const UFighterPawnData* DummyData)
{
    FDummyControl Control;

    if (bPadDriven)
        return Control;
    bool bScripted = false;

    if (DummyAttackMode != 0)
    {
        const auto& Dummy = State.fighters[DummyIndex];
        const int8 Forward = Dummy.facing;
        Physical::FrameInput Out;
        const bool bActionable = Dummy.attack_frame == 0 && Dummy.stun_frames == 0 &&
                                 Dummy.reaction == Core::ReactionKind::None && Dummy.move == Core::MoveKind::None &&
                                 Dummy.hp > 0;
        bool bPlay = DummyScriptFrame >= 0;

        if (!bPlay && bActionable && ++DummyWait >= DummyIntervals[DummyIntervalIndex])
        {
            DummyWait = 0;
            DummyScriptFrame = 0;
            bPlay = true;
        }

        if (bPlay)
        {
            const uint8 Buttons[] = {0, Physical::LP, Physical::RP, Physical::LK, Physical::RK};
            const int32 Step = DummyScriptFrame++;
            bool bLast = true;

            if (DummyAttackMode >= 1 && DummyAttackMode <= 4)
                Out.attacks = Buttons[DummyAttackMode];
            else if (DummyData && DummyData->DummySourceMove.IsValidIndex(Step))
            {
                const FFighterScriptedInput& Frame = DummyData->DummySourceMove[Step];
                ApplyDirection(Frame.Direction, Forward, Out);
                Out.attacks = static_cast<uint8>(Frame.Buttons & 0x0F);
                bLast = Step + 1 >= DummyData->DummySourceMove.Num();
            }

            if (bLast)
                DummyScriptFrame = -1;
            bScripted = true;
        }

        InOutInput = Out;
    }

    Control.bForceGuard = bDummyGuard && !bScripted;
    Control.bNoGuard = !bDummyGuard;
    Control.bCrouch = bDummyCrouch && !bScripted;

    return Control;
}

void UFighterTrainingComponent::ObserveInput(const Core::State& Before, const Core::Input& Input, int32 Index,
                                             const UFighterPawnData* Data)
{
    if (!Data || !Data->JustFrame.IsEnabled())
        return;
    const auto& Fighter = Before.fighters[Index];
    const int32 Frame = static_cast<int32>(Before.frame);
    const int32 Direction = Data->JustFrame.Direction;
    const bool bDirectionNow = RelativeDirection(Input, Fighter.facing) == Direction;
    const bool bDirectionBefore =
        RelativeDirection(Fighter.previous_horizontal, Fighter.previous_vertical, Fighter.facing) == Direction;

    if (bDirectionNow && !bDirectionBefore)
        JustDirectionFrame = Frame;
    const uint8 Button = static_cast<uint8>(Data->JustFrame.Button);

    if ((Input.buttons & Button) != 0 && (Fighter.previous_buttons & Button) == 0)
        JustButtonFrame = Frame;
}

void UFighterTrainingComponent::ObserveResult(const Core::State& After, int32 Index, const UFighterPawnData* Data)
{
    if (!Data || !Data->JustFrame.IsEnabled())
        return;
    const auto& Fighter = After.fighters[Index];
    const auto Just = static_cast<Core::CommandId>(Data->JustFrame.JustCommand);
    const auto Near = static_cast<Core::CommandId>(Data->JustFrame.NearCommand);
    const bool bAttempt = Fighter.active_command == Just || Fighter.active_command == Near;

    if (bAttempt && Fighter.attack_frame == 1 && JustDirectionFrame >= 0 && JustButtonFrame >= 0)
    {
        JustTimingOffset = JustButtonFrame - JustDirectionFrame;
        bJustTimingHit = Fighter.active_command == Just;
        bJustTimingValid = true;
    }
}

FString UFighterTrainingComponent::GetJustFrameLabel(const UFighterPawnData* Data) const
{
    if (!Data || !Data->JustFrame.IsEnabled())
        return TEXT("저스트 프레임: 이 캐릭터에는 저스트 프레임 기술이 없습니다");
    const FFighterJustFrameTrainer& Trainer = Data->JustFrame;

    if (!bJustTimingValid)
        return TEXT("저스트 프레임: ") + Trainer.Hint;

    if (bJustTimingHit)
        return FString::Printf(TEXT("저스트!  방향과 버튼이 같은 프레임  ->  %s"), *Trainer.JustName);

    if (JustTimingOffset > 0)
        return FString::Printf(TEXT("%s  버튼이 방향보다 %d프레임 늦음  (허용 0F)"), *Trainer.NearName,
                               JustTimingOffset);

    return FString::Printf(TEXT("%s  버튼이 방향보다 %d프레임 빠름  (허용 0F)"), *Trainer.NearName, -JustTimingOffset);
}

// 콤보와 프레임 이득을 집계한다.
void UFighterTrainingComponent::ObserveStep(const Core::State& Before, const Core::State& After)
{
    if (After.frame <= Before.frame)
        ResetTrackers();

    for (int32 Defender = 0; Defender < 2; ++Defender)
    {
        const auto& Old = Before.fighters[Defender];
        const auto& Now = After.fighters[Defender];
        const int32 Damage = Old.hp - Now.hp;

        if (Damage <= 0)
            continue;
        auto& Display = Combos[1 - Defender];

        if (Now.combo_hits <= 1 || Old.combo_hits == 0)
            Display = {};
        Display.Hits = Now.combo_hits;
        Display.Damage += Damage;
        Display.LastDamage = Damage;
        Display.LastHitFrame = After.frame;
    }

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto& Attacker = After.fighters[Index];
        const auto& Defender = After.fighters[1 - Index];

        if (Attacker.attack_connected && !Before.fighters[Index].attack_connected)
        {
            AdvAttacker = Index;
            AdvSteps = 0;
            AdvAttackerFree = AdvDefenderFree = -1;
            bAdvHit = Defender.hp < Before.fighters[1 - Index].hp;
            bAdvReaction = Defender.reaction != Core::ReactionKind::None;
            const auto* Attack = Core::GetAttackDefinition(Attacker.character, Attacker.active_command);
            AdvMoveName = Attack ? UTF8_TO_TCHAR(Attack->name) : TEXT("?");
            const AFighterArena* Arena = GetArena();
            AdvAttackerName = Arena ? Arena->GetCharacterLabel(Index) : TEXT("?");

            return;
        }
    }

    if (AdvAttacker < 0 || (AdvAttackerFree >= 0 && AdvDefenderFree >= 0) || AdvSteps > 180)
        return;

    ++AdvSteps;

    if (AdvAttackerFree < 0 && CanActOnStep(Before.fighters[AdvAttacker]))
        AdvAttackerFree = AdvSteps;

    if (AdvDefenderFree < 0 && CanActOnStep(Before.fighters[1 - AdvAttacker]))
        AdvDefenderFree = AdvSteps;
}

FString UFighterTrainingComponent::GetFrameAdvantageLabel() const
{
    if (AdvAttacker < 0)
        return TEXT("프레임 이득: 공격을 맞히거나 막아 보세요  (+ = 공격자가 먼저 행동)");
    const FString Who = FString::Printf(TEXT("P%d %s %s"), AdvAttacker + 1, *AdvAttackerName, *AdvMoveName);
    const TCHAR* Result = bAdvHit ? TEXT("히트") : TEXT("가드");

    if (bAdvReaction)
        return FString::Printf(TEXT("%s  %s  ->  띄우기 / 다운 (서 있는 상태 이득 없음)"), *Who, Result);

    if (AdvAttackerFree < 0 || AdvDefenderFree < 0)
        return FString::Printf(TEXT("%s  %s  ..."), *Who, Result);
    const int32 Advantage = AdvDefenderFree - AdvAttackerFree;

    return FString::Printf(TEXT("%s  %s  %+d"), *Who, Result, Advantage);
}

FString UFighterTrainingComponent::GetDummyAttackLabel() const
{
    static const TCHAR* Names[] = {TEXT("끄기"), TEXT("LP"), TEXT("RP"), TEXT("LK"), TEXT("RK"), TEXT("원본 기술")};

    return FString::Printf(TEXT("7 더미 공격 %s   8 %dF마다   9 더미 %s"), Names[DummyAttackMode],
                           DummyIntervals[DummyIntervalIndex], bDummyCrouch ? TEXT("앉기") : TEXT("서기"));
}
