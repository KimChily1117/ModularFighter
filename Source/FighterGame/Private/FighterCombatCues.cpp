// 두 상태를 비교해 표시용 큐를 만든다.
#include "FighterCombatCues.h"

namespace FighterGameplayTags
{
UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Fighter_Hit, "GameplayCue.Fighter.Hit",
                               "A core-resolved hit on this fighter.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Fighter_CounterHit, "GameplayCue.Fighter.CounterHit",
                               "A core-resolved counter hit on this fighter.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Fighter_Guard, "GameplayCue.Fighter.Guard",
                               "This fighter guarded a core-resolved contact.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Fighter_KO, "GameplayCue.Fighter.KO", "This fighter's health reached zero.");
UE_DEFINE_GAMEPLAY_TAG_COMMENT(Fighter_CueContext_Tornado, "Fighter.CueContext.Tornado",
                               "This contact newly resolved the core Tornado branch.");
}

namespace Core = modular_fighter;

uint64 FFighterCombatCue::Key() const
{
    return (static_cast<uint64>(Frame) << 32) ^ (static_cast<uint64>(Command) << 16) ^
           (static_cast<uint64>(Target & 1) << 15) ^ (static_cast<uint64>(bTornadoContact) << 14) ^
           (GetTypeHash(Tag) & 0x3FFF);
}

TArray<FFighterCombatCue> FighterCombatCues::Detect(const Core::State& Before, const Core::State& After)
{
    using namespace FighterGameplayTags;
    TArray<FFighterCombatCue> Cues;

    for (int32 Index = 0; Index < 2; ++Index)
    {
        const auto& Attacker = After.fighters[Index];
        const auto& Defender = After.fighters[1 - Index];
        const auto& DefenderBefore = Before.fighters[1 - Index];

        if (Attacker.attack_connected && !Before.fighters[Index].attack_connected)
        {
            FFighterCombatCue Cue;
            Cue.Target = 1 - Index;
            Cue.Command = static_cast<uint16>(Attacker.active_command);
            Cue.Frame = After.frame;
            Cue.Damage = DefenderBefore.hp - Defender.hp;
            Cue.bAirborneContact = Core::IsAirborne(DefenderBefore);

            const bool bBlocked =
                Defender.combo_hits == 0 && Cue.Damage == 0 && Defender.reaction == Core::ReactionKind::None;

            const auto* Move = Core::GetMoveDefinition(DefenderBefore.move);
            const bool bCrouch = Defender.previous_vertical < 0 || (Move && (Move->flags & Core::MoveCrouching));
            const uint8 Situation =
                Core::HitSituation(Attacker, Defender, bCrouch, bBlocked,
                                   Core::ResolveCounterHit(DefenderBefore, Defender.previous_buttons));
            const bool bCounter = Situation == Core::HitCounter || Situation == Core::HitCounterCrouch;
            Cue.Tag = bBlocked ? GameplayCue_Fighter_Guard
                               : (bCounter ? GameplayCue_Fighter_CounterHit : GameplayCue_Fighter_Hit);
            Cue.bTornadoContact = !bBlocked && !(DefenderBefore.combo_flags & Core::ComboScrewed) &&
                                  (Defender.combo_flags & Core::ComboScrewed);
            Cues.Add(Cue);
        }

        if (DefenderBefore.hp > 0 && Defender.hp == 0)
        {
            FFighterCombatCue Cue;
            Cue.Target = 1 - Index;
            Cue.Command = static_cast<uint16>(Attacker.active_command);
            Cue.Frame = After.frame;
            Cue.Tag = GameplayCue_Fighter_KO;
            Cues.Add(Cue);
        }
    }

    return Cues;
}
