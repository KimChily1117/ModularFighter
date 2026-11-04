// 전투 규칙 구현. 정수 연산만 써서 모든 환경에서 같은 결과를 낸다.
#include "modular_fighter/combat.hpp"
#include "modular_fighter/juggle_policy.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace modular_fighter {
namespace {
#include "cinematic_rate_data.inc"
constexpr std::uint8_t SupportedButtons =
    AllAttackButtons | Guard | NoGuard | TrainingCounterOn | TrainingCounterOff;

bool ValidButtons(std::uint8_t buttons) noexcept {
    return (buttons & static_cast<std::uint8_t>(~SupportedButtons)) == 0 &&
           (buttons & (TrainingCounterOn | TrainingCounterOff)) != (TrainingCounterOn | TrainingCounterOff);
}
#if defined(MF_FULL_MOVESET)
#include <mf_full/pushbacks.inc>
#else
#include "modules/pushbacks.inc"
#endif
struct HitSegment {
    std::array<std::int32_t, 3> a, b;
};

struct HitFrame {
    CommandId command;
    CharacterId owner;
    std::uint16_t frame;
    std::uint16_t first;
    std::uint8_t count;
};

struct HurtSphere {
    std::array<std::int32_t, 3> centre;
    std::int32_t radius;
};

struct AirPose {
    CharacterId character;
    ReactionKind reaction;
    std::uint16_t frame;
    std::int32_t height;
    std::array<HurtSphere, 14> spheres;
};

#include "air_geometry_data.inc"
#if defined(MF_FULL_MOVESET)
#include <mf_full/ground_geometry_data.inc>
#else
constexpr std::uint64_t GroundGeometryFingerprint = 0;
constexpr std::size_t GroundPoseCount = 0;
constexpr std::array<const char*, 0> GroundPoseChunks{};
#endif
std::uint32_t GroundKey(const char* p) noexcept {
    std::uint32_t value = 0;

    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(p[i])) << (i * 8);

    return value;
}

const char* FindGroundPose(CharacterId character, ReactionKind reaction, std::uint16_t frame) noexcept {
    const auto wanted =
        (static_cast<std::uint32_t>(character) << 24) | (static_cast<std::uint32_t>(reaction) << 16) | frame;
    std::size_t first = 0, last = GroundPoseCount;

    while (first < last) {
        const auto mid = first + (last - first) / 2;
        const auto* data = GroundPoseChunks[mid / 64] + (mid % 64) * 120;

        if (GroundKey(data) < wanted)
            first = mid + 1;
        else
            last = mid;
    }

    if (first == GroundPoseCount)
        return nullptr;
    const auto* data = GroundPoseChunks[first / 64] + (first % 64) * 120;

    return GroundKey(data) == wanted ? data : nullptr;
}

void DecodeGroundPose(const char* data, AirPose& pose) noexcept {
    const auto signed16 = [](const char* p) {
        const auto value = static_cast<unsigned char>(p[0]) | (static_cast<unsigned char>(p[1]) << 8);

        return value >= 32768 ? value - 65536 : value;
    };
    pose.height = signed16(data + 4);

    for (std::size_t i = 0; i < pose.spheres.size(); ++i) {
        for (unsigned axis = 0; axis < 3; ++axis)
            pose.spheres[i].centre[axis] = signed16(data + 8 + i * 8 + axis * 2);
        pose.spheres[i].radius = signed16(data + 8 + i * 8 + 6);
    }
}

const AirPose* FindAirPose(CharacterId character, ReactionKind reaction, std::uint16_t frame) noexcept {
    for (const auto& pose : AirPoses)
        if (pose.character == character && pose.reaction == reaction && pose.frame == frame)
            return &pose;
#if defined(MF_FULL_MOVESET)
    for (const auto& pose : GroundFallPoses)
        if (pose.character == character && pose.reaction == reaction && pose.frame == frame)
            return &pose;
#endif
    return nullptr;
}

struct HurtTable {
    CharacterId character;
    std::uint8_t pose;
    std::array<HurtSphere, 14> spheres;
};
#if defined(MF_FULL_MOVESET)
#include <mf_full/hit_geometry_data.inc>
#else
#include "hit_geometry_data.inc"
#endif
#include "movement_data.inc"
#if defined(MF_FULL_MOVESET)
#include <mf_full/modules.inc>
#else
#include "modules/modules.inc"
#endif

const CharacterModule* ModuleOf(CharacterId character) noexcept {
    for (const auto* module : Modules)
        if (module->character == character)
            return module;

    return nullptr;
}

struct StandingRootSample {
    std::int32_t forward, left;
};

struct StandingRootTable {
    const char* clip;
    std::uint32_t first;
    std::uint16_t count;
};
#if defined(MF_FULL_MOVESET)
#include <mf_full/standing_motion_data.inc>
#else
#include "standing_motion_data.inc"
#endif
std::uint16_t StandingMotionFor(const HitReactionDefinition* outcome) noexcept {
    if (!outcome || !outcome->clip || outcome->reaction != ReactionKind::None)
        return 0;

    for (std::uint16_t f = 1; f <= PushbackFrames(outcome->pushback); ++f)
        if (PushbackDisplacement(outcome->pushback, f) != 0)
            return 0;

    for (std::size_t i = 0; i < StandingRootTables.size(); ++i)
        if (std::strcmp(StandingRootTables[i].clip, outcome->clip) == 0)
            return static_cast<std::uint16_t>(i + 1);

    return 0;
}

const MoveDefinition* FindMove(MoveKind kind) noexcept {
    for (const auto* module : Modules)
        for (const auto& move : module->moves)
            if (move.move == kind)
                return &move;

    return nullptr;
}

const AttackDefinition* OwnAttack(CharacterId character, CommandId command) noexcept {
    const auto* module = ModuleOf(character);

    if (!module || character == CharacterId::Training)
        return nullptr;

    for (const auto& attack : module->attacks)
        if (attack.command == command)
            return &attack.rules;

    return nullptr;
}

bool CancelPressed(std::uint8_t buttons, std::uint8_t edges, std::uint8_t held) noexcept {
    const bool chord = (buttons & (buttons - 1u)) != 0;

    return chord ? (held & buttons) == buttons && (edges & buttons) != 0 : (edges & buttons) != 0;
}

const StringCancel* FindString(CharacterId character, CommandId from, CommandId to) noexcept {
    const auto* module = ModuleOf(character);

    if (!module)
        return nullptr;

    for (const auto& cancel : module->strings)
        if (cancel.from == from && cancel.to == to)
            return &cancel;

    return nullptr;
}

bool IsStringFollowUp(CharacterId character, CommandId command) noexcept {
    const auto* module = ModuleOf(character);

    if (!module || command == CommandId::None)
        return false;

    for (const auto& cancel : module->strings)
        if (cancel.to == command)
            return true;

    return false;
}

std::uint16_t StringFirst(CharacterId character, const StringCancel& cancel) noexcept;

bool GateOpen(const Fighter& fighter, std::uint8_t gate) noexcept {
    if (gate == 0)
        return true;
    const bool heat = fighter.heat_frames != 0, rage = RageActive(fighter);

    return !((gate & GateHeat) && !heat) && !((gate & GateNoHeat) && heat) && !((gate & GateRage) && !rage) &&
           !((gate & GateNoRage) && rage);
}

const StringCancel* MatchString(const Fighter& fighter, CommandId from, std::uint8_t edges, std::uint8_t held,
                                std::uint8_t direction, std::uint16_t frame, bool opponent_down) noexcept {
    const auto character = fighter.character;
    const auto* module = ModuleOf(character);

    if (!module || edges == 0)
        return nullptr;

    for (const bool chord : {true, false})
        for (const auto& cancel : module->strings) {
            const auto* destination = GetCommandDefinition(cancel.to);

            if (destination && destination->opponent_down && !opponent_down)
                continue;
            const bool is_chord = (cancel.buttons & (cancel.buttons - 1u)) != 0;

            if (is_chord == chord && cancel.from == from && GateOpen(fighter, cancel.gate) &&
                CancelPressed(cancel.buttons, edges, held) && frame >= StringFirst(character, cancel) &&
                frame <= cancel.last && (cancel.directions == 0 || (cancel.directions & Dir(direction)) != 0))
                return &cancel;
        }
    return nullptr;
}

struct AuthoredTransition {
    CharacterId character;
    CommandId from, to;
    std::uint16_t point_frame;
    bool preserve_frame;
};

struct AuthoredCounterStun {
    CharacterId character;
    CommandId command;
    std::uint16_t frames;
};

struct AuthoredAirLift {
    CharacterId character;
    CommandId command;
    std::uint16_t solve_frames;
};

struct AuthoredComboLaunch {
    CharacterId character;
    CommandId command;
    ReactionKind reaction;
    std::uint8_t minimum_hits;
    std::uint16_t solve_frames;
    std::int32_t initial_height;
};

struct AuthoredHitShape {
    CharacterId character;
    CommandId command;
    std::int32_t radius_mm;
    std::int32_t air_down_mm;
    std::int32_t ground_down_mm;
};

struct AuthoredRecovery {
    CharacterId character;
    CommandId command;
    std::uint16_t last_attack_frame, recovery_frames;
    const char* clip;
};

struct AuthoredStringWindow {
    CharacterId character;
    CommandId from, to;
    std::uint16_t first;
};

struct AuthoredStanceTransition {
    CharacterId character;
    CommandId from;
    MoveKind to;
    std::uint16_t first, last, directions;
};

struct AuthoredAirMotion {
    ReactionKind reaction;
    std::uint16_t solve_frames, landing_frame;
    std::int32_t velocity_loss, max_contact_mm;
};

struct AuthoredCleanHit {
    CharacterId character;
    CommandId command;
    std::int32_t distance_mm;
};

struct AuthoredAirWindow {
    ReactionKind reaction;
    std::uint16_t last;
};

struct AuthoredSequenceFollowup {
    CharacterId character;
    CommandId from, to;
    std::uint16_t first, last, release;
};

struct AuthoredSequenceExit {
    CharacterId character;
    CommandId from;
    MoveKind to;
    std::uint16_t frame;
    std::uint8_t direction;
};

struct AuthoredThrowVariant {
    CharacterId character;
    CommandId from, to;
    std::uint8_t direction;
};

struct AuthoredGroundPickup {
    CharacterId character;
    CommandId command;
    std::int32_t reach_mm, half_width_mm;
};

struct AuthoredSequenceImpact {
    CharacterId character;
    CommandId command;
    std::array<std::uint16_t, 4> frames;
    std::uint8_t count;
};

#include "authored_rules.inc"

int SequenceDamageThrough(CharacterId owner, CommandId command, int total, std::uint32_t frame) {
    for (const auto& rule : AuthoredSequenceImpacts) {
        if (rule.character != owner || rule.command != command)
            continue;
        int reached = 0;

        for (std::uint8_t i = 0; i < rule.count; ++i)
            reached += rule.frames[i] <= frame ? 1 : 0;

        return total * reached / rule.count;
    }

    return 0;
}

struct SequenceRootSample {
    std::int32_t attacker_forward, attacker_left, victim_forward, victim_left;
};

struct SequenceRootTable {
    CharacterId owner;
    CommandId command;
    std::uint32_t first;
    std::uint16_t frames;
    ReactionKind attacker_end;
    std::int32_t attacker_cos, attacker_left;
    ReactionKind victim_end;
    std::int32_t victim_cos, victim_left;
};
#if defined(MF_FULL_MOVESET)
#include <mf_full/sequence_motion_data.inc>
#else
constexpr std::uint64_t SequenceMotionHash = 0;
constexpr std::array<SequenceRootSample, 0> SequenceRootSamples{};
constexpr std::array<SequenceRootTable, 0> SequenceRootTables{};
#endif
const SequenceRootSample* SequenceRoot(CharacterId owner, CommandId command, std::uint16_t frame) noexcept {
    for (const auto& table : SequenceRootTables)
        if (table.owner == owner && table.command == command && frame >= 1 && frame <= table.frames)
            return &SequenceRootSamples[table.first + frame - 1];

    return nullptr;
}

constexpr std::uint16_t AirLandingFrame(ReactionKind reaction) noexcept {
    if (reaction == ReactionKind::AirPng)
        return GenericAirLandingFrame;

    for (const auto& rule : AuthoredAirMotions)
        if (reaction == rule.reaction)
            return rule.landing_frame;

    return 0;
}

std::uint16_t ClipAirLandingFrame(ReactionKind kind) noexcept {
    const auto* reaction = GetReactionDefinition(kind);

    if (!reaction || (reaction->flags & ReactionLying))
        return 0;
    auto last = reaction->air_last;

    for (const auto& rule : AuthoredAirWindows)
        if (kind == rule.reaction)
            last = rule.last;

    return last > 0 && last < reaction->frames ? static_cast<std::uint16_t>(last + 1) : 0;
}

bool HasReactionPose(CharacterId character, ReactionKind reaction, std::uint16_t frame) noexcept {
    return FindAirPose(character, reaction, frame) || FindGroundPose(character, reaction, frame);
}

std::int32_t SolveAirVelocity(std::int32_t height, std::int32_t landing, std::int64_t frames) noexcept {
    return juggle_policy::Velocity(height, landing, static_cast<int>(frames), AirGravity);
}

std::uint16_t StringFirst(CharacterId character, const StringCancel& cancel) noexcept {
    for (const auto& r : AuthoredStringWindows)
        if (r.character == character && r.from == cancel.from && r.to == cancel.to)
            return r.first;

    return cancel.first;
}

const AuthoredRecovery* RecoveryOf(CharacterId character, CommandId command) noexcept {
    for (const auto& r : AuthoredRecoveries)
        if (r.character == character && r.command == command)
            return &r;

    return nullptr;
}

std::uint16_t AuthoredDestinationFrame(CharacterId character, CommandId from, CommandId to,
                                       std::uint16_t point_frame, std::uint16_t legacy_frame,
                                       std::uint16_t next_frame) noexcept {
    for (const auto& rule : AuthoredTransitions)
        if (rule.character == character && rule.from == from && rule.to == to &&
            rule.point_frame == point_frame)
            return rule.preserve_frame ? next_frame : 1;

    return legacy_frame;
}

#if defined(MF_FULL_MOVESET)

struct SharedClockLink {
    CharacterId character;
    CommandId from, to;
    std::uint16_t frame;
};

constexpr SharedClockLink SharedClockLinks[] = {
    {CharacterId::Kazuya, CommandId::KazuyaAutoUFRKKzjprk00F, CommandId::KazuyaStringKzsjk01, 0},
    {CharacterId::Kazuya, CommandId::KazuyaFCUFRKKzjprk00F, CommandId::KazuyaStringKzsjk01, 0},
    {CharacterId::Kazuya, CommandId::KazuyaWSUFRKKzjprk00F, CommandId::KazuyaStringKzsjk01, 0},
    {CharacterId::Kazuya, CommandId::KazuyaStringKzsjk01, CommandId::KazuyaStringKzsjk01G, 0},
    {CharacterId::Kazuya, CommandId::KazuyaStringKzsjk01G, CommandId::KazuyaStringKzsjk02h, 47},
    {CharacterId::Kazuya, CommandId::KazuyaStringKzsjk02h, CommandId::KazuyaStringKzsjk02hG, 0},
    {CharacterId::Kazuya, CommandId::KazuyaStringKzsjk02hG, CommandId::KazuyaStringKzsjk03h, 24},
    {CharacterId::Paul, CommandId::PaulMotionddbbp3PlSwlk, CommandId::PaulStringPlSwlkY, 0},
    {CharacterId::Paul, CommandId::PaulMotionddbbp3PlSwlk, CommandId::PaulStringPlSwlkrp00, 0},
    {CharacterId::Paul, CommandId::PaulStringPlSwlkY, CommandId::PaulStringPlSwlkrp00, 19},
    {CharacterId::Paul, CommandId::PaulAutoDRKpolsk00, CommandId::PaulStringrakuyo2JST0, 0},
    {CharacterId::Paul, CommandId::PaulWSDRKpolsk00, CommandId::PaulStringrakuyo2JST0, 0},
    {CharacterId::Paul, CommandId::PaulAutoDRKpolsk00, CommandId::PaulStringrakuyo2JST, 0},
    {CharacterId::Paul, CommandId::PaulWSDRKpolsk00, CommandId::PaulStringrakuyo2JST, 0},
    {CharacterId::Paul, CommandId::PaulStringrakuyo2JST0, CommandId::PaulStringrakuyo2JST, 16},
    {CharacterId::Paul, CommandId::PaulStringkawarak, CommandId::PaulStringkawarakG, 0},
    {CharacterId::Paul, CommandId::PaulStringkawarakG, CommandId::PaulAutoVariantkrakuyo, 19},
    {CharacterId::Kazuya, CommandId::KazuyaMotionfnddfp4Kzmk00, CommandId::KazuyaStringKzmk00Gup, 0},
    {CharacterId::Kazuya, CommandId::KazuyaMotionfnddfp4Kzmk00, CommandId::KazuyaStringKzmk00G, 0},
    {CharacterId::Kazuya, CommandId::KazuyaStringKzmk00Gup, CommandId::KazuyaAutoVariantKznarakuup, 16},
    {CharacterId::Kazuya, CommandId::KazuyaStringKzmk00G, CommandId::KazuyaAutoVariantKzmk01, 16},
    {CharacterId::Kazuya, CommandId::KazuyaAutoDFLKCotslkF, CommandId::KazuyaStringKz3LKLP, 0},
    {CharacterId::Kazuya, CommandId::KazuyaAutoDFLKCotslkF, CommandId::KazuyaStringKzhizabody, 0},
    {CharacterId::Kazuya, CommandId::KazuyaStringKzhizabody, CommandId::KazuyaStringKz3lkrplpH, 0},
    {CharacterId::Kazuya, CommandId::KazuyaAutoBRPKzhizicom1, CommandId::KazuyaStringKz4rprp, 0},
    {CharacterId::Kazuya, CommandId::KazuyaAutoBRPKzhizicom1, CommandId::KazuyaStringKzhizicom2, 0},
    {CharacterId::Kazuya, CommandId::KazuyaAutoDFLKRKKz3wk, CommandId::KazuyaStringKz3wk2y, 0},
    {CharacterId::Kazuya, CommandId::KazuyaAutoDFLKRKKz3wk, CommandId::KazuyaAutoVariantKz3wk2, 20},
    {CharacterId::Kazuya, CommandId::KazuyaStringKz3wk2y, CommandId::KazuyaStringKz3wklp, 37},
    {CharacterId::Paul, CommandId::PaulFCSet456LKRKCostlk00, CommandId::PaulStringPlstlkrp, 0},
    {CharacterId::Paul, CommandId::PaulFCSet456LKCostlk00, CommandId::PaulStringPlstlkrp, 0},
    {CharacterId::Paul, CommandId::PaulWSAnyLKRKCostlk00, CommandId::PaulStringPlstlkrp, 0},
    {CharacterId::Paul, CommandId::PaulWSAnyLKCostlk00, CommandId::PaulStringPlstlkrp, 0},
};

const SharedClockLink* FindSharedClock(CharacterId character, CommandId from, CommandId to,
                                       std::uint16_t frame) noexcept {
    for (const auto& item : SharedClockLinks)
        if (item.character == character && item.from == from && item.to == to && item.frame == frame)
            return &item;

    return nullptr;
}
#endif

std::uint16_t MoveFlags(MoveKind kind) noexcept {
    const auto* move = FindMove(kind);

    return move ? move->flags : 0;
}

constexpr std::int32_t MaximumGeometryDistance = 5000;

// 나눗셈 없이 선분과 구의 충돌을 판정한다.
bool SegmentTouchesSphere(const HitSegment& segment, const std::array<std::int64_t, 3>& point,
                          std::int64_t radius) noexcept {
    std::int64_t length = 0, along = 0, from_a = 0, from_b = 0;

    for (std::size_t i = 0; i < 3; ++i) {
        const std::int64_t ab = segment.b[i] - segment.a[i];
        const std::int64_t ap = point[i] - segment.a[i];
        const std::int64_t bp = point[i] - segment.b[i];
        length += ab * ab;
        along += ab * ap;
        from_a += ap * ap;
        from_b += bp * bp;
    }

    if (length == 0 || along <= 0)
        return from_a <= radius * radius;

    if (along >= length)
        return from_b <= radius * radius;

    return from_a * length - along * along <= radius * radius * length;
}

bool ValidDirection(std::int8_t direction) noexcept {
    return direction >= -1 && direction <= 1;
}

bool ValidCharacter(CharacterId character) noexcept {
    return ModuleOf(character) != nullptr;
}

bool Available(const CommandDefinition& command, CharacterId character) noexcept {
    return command.character == CharacterId::Training || command.character == character;
}

void ClearHistory(Fighter& fighter) noexcept {
    fighter.direction_count = 0;
    fighter.direction_history.fill(0);
}

// 방향 입력 이력을 기록한다.
void CaptureDirection(Fighter& fighter, const Input& input, std::int8_t facing) noexcept {
    if (fighter.facing != facing)
        ClearHistory(fighter);
    fighter.facing = facing;

    if (fighter.direction_count == DirectionHistoryFrames) {
        std::move(fighter.direction_history.begin() + 1, fighter.direction_history.end(),
                  fighter.direction_history.begin());
        --fighter.direction_count;
    }

    fighter.direction_history[fighter.direction_count++] =
        static_cast<std::uint8_t>(5 + input.horizontal * facing + input.vertical * 3);
}

// 입력 이력이 커맨드 패턴과 맞는지 확인한다.
bool MatchesPattern(const Fighter& fighter, const std::uint8_t* directions, std::uint8_t count,
                    std::uint8_t window = 0) noexcept {
    if (count == 0)
        return true;
    int cursor = static_cast<int>(fighter.direction_count) - 1;

    for (int part = static_cast<int>(count) - 1; part >= 0; --part) {
        const auto expected = directions[static_cast<std::size_t>(part)];

        if (cursor < 0 || fighter.direction_history[static_cast<std::size_t>(cursor)] != expected)
            return false;

        if (part == 0 && window != 0 && static_cast<int>(fighter.direction_count) - cursor > window)
            return false;

        do {
            --cursor;
        } while (cursor >= 0 && fighter.direction_history[static_cast<std::size_t>(cursor)] == expected);
    }

    return true;
}

bool IsRunning(const Fighter& fighter) noexcept {
    return fighter.move == MoveKind::RunStart || fighter.move == MoveKind::Run;
}

int ForwardTaps(const CommandDefinition& command) noexcept {
    int forward = 0;

    for (std::uint8_t i = 0; i < command.direction_count; ++i)
        forward += command.directions[i] == 6;

    return forward;
}

bool Matches(const Fighter& fighter, const CommandDefinition& command, bool opponent_down = false) noexcept {
    if (command.opponent_down && !opponent_down)
        return false;

    if (!GateOpen(fighter, command.gate))
        return false;

    if (command.direction_set == 0 && IsRunning(fighter) && ForwardTaps(command) >= 3 &&
        fighter.direction_count != 0 && fighter.direction_history[fighter.direction_count - 1u] == 6)
        return true;

    if (command.direction_set != 0)
        return fighter.direction_count != 0 &&
               (command.direction_set & (1u << fighter.direction_history[fighter.direction_count - 1u])) != 0;

    return MatchesPattern(fighter, command.directions.data(), command.direction_count, command.window);
}

bool Matches(const Fighter& fighter, const Pattern& pattern) noexcept {
    return MatchesPattern(fighter, pattern.directions.data(), pattern.count);
}

const MovementTable* Movement(MoveKind kind) noexcept {
    if (kind == MoveKind::None)
        return nullptr;

    for (const auto& table : MovementTables)
        if (table.kind == kind)
            return &table;

    return nullptr;
}

bool IsLateral(MoveKind kind) noexcept {
    return (MoveFlags(kind) & MoveLateral) != 0;
}

bool IsLoop(MoveKind kind) noexcept {
    return (MoveFlags(kind) & MoveLoop) != 0;
}

struct Vec {
    std::int64_t x, z;
};

std::uint64_t ISqrt(std::uint64_t value) noexcept {
    std::uint64_t root = 0, bit = std::uint64_t{1} << 62;

    while (bit > value)
        bit >>= 2;

    while (bit != 0) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else
            root >>= 1;
        bit >>= 2;
    }

    return root;
}

Vec UnitOf(Vec d, Vec fallback) noexcept {
    if (d.x == 0 && d.z == 0)
        return fallback;
    const auto length = static_cast<std::int64_t>(ISqrt(static_cast<std::uint64_t>(d.x * d.x + d.z * d.z)));

    return {std::clamp<std::int64_t>(d.x * UnitQ16 / length, -UnitQ16, UnitQ16),
            std::clamp<std::int64_t>(d.z * UnitQ16 / length, -UnitQ16, UnitQ16)};
}

Vec Scale(std::int64_t distance, Vec unit) noexcept {
    return {distance * unit.x / UnitQ16, distance * unit.z / UnitQ16};
}

std::int64_t Dot(Vec a, Vec b) noexcept {
    return a.x * b.x + a.z * b.z;
}

Vec LeftOf(Vec face) noexcept {
    return {face.z, -face.x};
}

Vec FaceOf(const Fighter& fighter) noexcept {
    return {fighter.face_cos, fighter.face_sin};
}

Vec PositionOf(const Fighter& fighter) noexcept {
    return {fighter.position_x, fighter.position_z};
}

void Translate(Fighter& fighter, Vec delta) noexcept {
    fighter.position_x = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(fighter.position_x + delta.x, -StageLimit, StageLimit));
    fighter.position_z = static_cast<std::int32_t>(
        std::clamp<std::int64_t>(fighter.position_z + delta.z, -StageLimit, StageLimit));
}

bool IsUnit(std::int64_t c, std::int64_t sn) noexcept {
    const auto norm = c * c + sn * sn;
    const auto one = static_cast<std::int64_t>(UnitQ16) * UnitQ16;

    return c >= -UnitQ16 && c <= UnitQ16 && sn >= -UnitQ16 && sn <= UnitQ16 && norm >= one / 10 * 9 &&
           norm <= one / 10 * 11;
}

bool IsJump(MoveKind kind) noexcept {
    return (MoveFlags(kind) & MoveJump) != 0;
}

void ClearMove(Fighter& fighter) noexcept {
    fighter.move = MoveKind::None;
    fighter.move_frame = 0;
}

// 진행 중인 기술을 한 프레임 진행한다.
void AdvanceMove(Fighter& fighter) noexcept {
    const auto* table = Movement(fighter.move);
    const bool lateral = IsLateral(fighter.move);
    const std::size_t index = fighter.move_frame;
    const std::int32_t forward = lateral ? table->secondary[index] : table->primary[index];
    const std::int32_t side = lateral ? table->primary[index] : 0;
    const auto face = FaceOf(fighter);
    const auto f = Scale(forward, face), l = Scale(side, LeftOf(face));
    Translate(fighter, {f.x + l.x, f.z + l.z});
    ++fighter.move_frame;

    if (fighter.move_frame == table->frames) {
        if (IsLoop(fighter.move)) {
            fighter.move_frame = 0;

            return;
        }

        const auto* move = FindMove(fighter.move);
        const bool heat_dash = fighter.move == MoveKind::HeatDash || fighter.move == MoveKind::EngagerRun;
        const bool run_start = fighter.move == MoveKind::RunStart;
        ClearMove(fighter);

        if (run_start) {
            const auto last =
                fighter.direction_count ? fighter.direction_history[fighter.direction_count - 1u] : 5;
            fighter.move = last == 6 || last == 3 || last == 9 ? MoveKind::Run : MoveKind::RunStop;
            fighter.move_frame = 0;
            AdvanceMove(fighter);

            return;
        }

        if (heat_dash) {
            fighter.move = MoveKind::HeatDashStop;
            fighter.move_frame = 0;
            AdvanceMove(fighter);

            return;
        }

        if (move && move->after_command != CommandId::None) {
            fighter.active_command = move->after_command;
            fighter.active_attack = GetCommandDefinition(move->after_command)->fallback_attack;
            fighter.attack_frame = 1;
            fighter.attack_connected = 0;
            fighter.pending_command = CommandId::None;
            fighter.pending_frames = 0;
            fighter.queued_command = CommandId::None;
        }

        if (move && move->after != ReactionKind::None) {
            fighter.reaction = move->after;
            fighter.reaction_frame = 1;
        }
    }
}

// 기술을 시작한다.
void StartMove(Fighter& fighter, MoveKind kind) noexcept {
    fighter.move = kind;
    fighter.move_frame = 0;
}

bool DownTap(const Fighter& fighter) noexcept {
    if (fighter.direction_count < 2 || fighter.direction_history[fighter.direction_count - 1u] != 5)
        return false;
    int cursor = static_cast<int>(fighter.direction_count) - 2, run = 0;

    while (cursor >= 0 && fighter.direction_history[static_cast<std::size_t>(cursor)] == 2) {
        ++run;
        --cursor;
    }

    return run >= 1 && run <= SidestepTapFrames &&
           (cursor < 0 || fighter.direction_history[static_cast<std::size_t>(cursor)] == 5);
}

bool MoveAllowed(MoveKind kind, CharacterId character) noexcept {
    const auto* move = FindMove(kind);

    return move && (move->owner == CharacterId::Training || move->owner == character);
}

const EntryRule* StepEntry(const Fighter& fighter, std::uint8_t previous) noexcept {
    const auto count = fighter.direction_count;
    const auto* module = ModuleOf(fighter.character);

    if (count == 0 || !module)
        return nullptr;
    const auto last = fighter.direction_history[count - 1u];

    if (last == previous)
        return nullptr;

    for (const auto& rule : module->entries) {
        if (rule.buttons != 0 || rule.last != last)
            continue;

        if (rule.pattern.count != 0 && !Matches(fighter, rule.pattern))
            return nullptr;

        if (rule.run_direction != 0) {
            int cursor = static_cast<int>(count) - 2, run = 0;

            while (cursor >= 0 &&
                   fighter.direction_history[static_cast<std::size_t>(cursor)] == rule.run_direction) {
                ++run;
                --cursor;
            }

            if (run < 1 || run > rule.run_max)
                return nullptr;
        }

        return &rule;
    }

    return nullptr;
}

const EntryRule* ChordEntry(const Fighter& fighter, std::uint8_t edges, std::uint8_t held,
                            std::uint8_t current) noexcept {
    const auto* module = ModuleOf(fighter.character);

    if (!module || edges == 0)
        return nullptr;

    for (const auto& rule : module->entries)
        if (rule.buttons != 0 && rule.last == current && (held & rule.buttons) == rule.buttons &&
            (edges & rule.buttons) != 0)
            return &rule;

    return nullptr;
}

std::uint8_t FinalRun(const Fighter& fighter) noexcept {
    if (fighter.direction_count == 0)
        return 0;
    const auto last = fighter.direction_history[fighter.direction_count - 1u];
    std::uint8_t run = 0;

    for (int cursor = static_cast<int>(fighter.direction_count) - 1;
         cursor >= 0 && fighter.direction_history[static_cast<std::size_t>(cursor)] == last; --cursor)
        ++run;

    return run;
}

bool TapTap(const Fighter& fighter, std::uint8_t direction) noexcept {
    const CommandDefinition pattern{
        CommandId::None, CharacterId::Training, "", "", 0, {direction, 5, direction, 0}, 3};

    return Matches(fighter, pattern);
}

bool Pressed(std::uint8_t chord, std::uint8_t button, std::uint8_t edges, std::uint8_t held) noexcept {
    return chord != 0 ? (held & chord) == chord && (edges & chord) != 0 : (edges & button) != 0;
}

bool SystemAllowed(const Fighter& fighter, CommandId command) noexcept {
    const auto* attack = GetAttackDefinition(fighter.character, command);

    if (!attack)
        return false;

    switch (attack->system_action) {
    case SystemAction::HeatBurst:
        return !fighter.heat_used;
    case SystemAction::HeatSmash:
        return fighter.heat_frames != 0;
    case SystemAction::RageArt:
        return RageActive(fighter);
    default:
        return true;
    }
}

constexpr std::int64_t RunMinimumDistance = 1100;

bool RunCommandAllowed(const Fighter& fighter, const Fighter& opponent,
                       const CommandDefinition& command) noexcept {
    if (ForwardTaps(command) < 3)
        return true;

    if (IsRunning(fighter))
        return true;

    if (opponent.combo_hits != 0 && IsAirborne(opponent))
        return true;
    const std::int64_t dx = static_cast<std::int64_t>(opponent.position_x) - fighter.position_x;
    const std::int64_t dz = static_cast<std::int64_t>(opponent.position_z) - fighter.position_z;

    return dx * dx + dz * dz >= RunMinimumDistance * RunMinimumDistance;
}

std::uint16_t EngagerRunFrame(const AttackDefinition* attack) noexcept {
    if (!attack)
        return 0;

    struct Row {
        const char* name;
        std::uint16_t frame;
    };

    static constexpr Row rows[] = {{"Kz_keriage0", 25}, {"Kz_Luraken", 30}, {"Kz_66rp", 24},
                                   {"Pl_rp00F", 21},    {"Pl_Swpong", 23},  {"Pl_rk00F", 30},
                                   {"Pl_9rp", 30},      {"Pl_hadoh03", 21}};

    for (const auto& row : rows)
        if (std::strcmp(attack->name, row.name) == 0)
            return row.frame;

    return 0;
}

bool HeatEngager(const AttackDefinition* attack) noexcept {
    if (!attack || attack->system_action != SystemAction::None)
        return false;

    for (const char* name : {"Kz_tukirpsp", "Kz_keriage0", "Kz_Luraken", "Kz_66rp", "Kz_1lprp", "Pl_rp00F",
                             "Pl_Swpong", "Pl_rk00F", "Pl_9rp", "Pl_hadoh03"})
        if (std::strcmp(attack->name, name) == 0)
            return true;

    return false;
}

// 입력 이력에서 커맨드를 인식한다.
CommandId Recognize(const Fighter& fighter, std::uint8_t edges, std::uint8_t held,
                    bool opponent_down) noexcept {
    if (opponent_down)
        if (const auto* module = ModuleOf(fighter.character))
            for (const auto& command : module->commands)
                if (command.opponent_down && !command.context_only && SystemAllowed(fighter, command.id) &&
                    Pressed(command.chord, command.fallback_attack, edges, held) &&
                    Matches(fighter, command, true))
                    return command.id;

    const auto posture = PostureOf(fighter);

    if (IsRunning(fighter))
        for (const bool chords : {true, false})
            for (const auto* module :
                 {ModuleOf(fighter.character), static_cast<const CharacterModule*>(&CommonModule)}) {
                if (!module)
                    continue;

                for (const auto& command : module->commands)
                    if ((command.chord != 0) == chords && ForwardTaps(command) >= 3 &&
                        SystemAllowed(fighter, command.id) && !command.context_only &&
                        Available(command, fighter.character) &&
                        Pressed(command.chord, command.fallback_attack, edges, held) &&
                        Matches(fighter, command, opponent_down))
                        return command.id;
            }
    for (const auto wanted : {posture, Posture::Standing}) {
        for (const bool chords : {true, false})
            for (const auto* module :
                 {ModuleOf(fighter.character), static_cast<const CharacterModule*>(&CommonModule)}) {
                if (!module)
                    continue;

                for (const auto& command : module->commands) {
                    if (!SystemAllowed(fighter, command.id) || command.context_only ||
                        command.posture != wanted || (command.chord != 0) != chords ||
                        !Available(command, fighter.character) ||
                        !Pressed(command.chord, command.fallback_attack, edges, held) ||
                        !Matches(fighter, command, opponent_down))
                        continue;

                    if (command.held_variant != CommandId::None && command.direction_count != 0 &&
                        fighter.direction_count >= 2 &&
                        fighter.direction_history[fighter.direction_count - 2] ==
                            command.directions[command.direction_count - 1u])
                        return command.held_variant;

                    return command.id;
                }
            }
        if (posture == Posture::Standing)
            break;
    }

    return CommandId::None;
}

void Write16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    for (unsigned shift = 0; shift < 16; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void Write32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint16_t Read16(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) noexcept {
    const auto value =
        static_cast<std::uint16_t>(bytes[cursor]) | (static_cast<std::uint16_t>(bytes[cursor + 1]) << 8);
    cursor += 2;

    return static_cast<std::uint16_t>(value);
}

std::uint32_t Read32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) noexcept {
    std::uint32_t value = 0;

    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes[cursor++]) << shift;
    }

    return value;
}

std::int32_t ReadSigned32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) noexcept {
    const std::uint32_t value = Read32(bytes, cursor);

    const auto signed_value = value <= static_cast<std::uint32_t>(INT32_MAX)
                                  ? static_cast<std::int64_t>(value)
                                  : static_cast<std::int64_t>(value) - (std::int64_t{1} << 32);

    return static_cast<std::int32_t>(signed_value);
}

std::int8_t ReadSigned8(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) noexcept {
    const auto value = bytes[cursor++];

    return static_cast<std::int8_t>(value <= 127 ? static_cast<int>(value) : static_cast<int>(value) - 256);
}

void ClearPushback(Fighter& fighter) noexcept {
    fighter.pushback = NoPushback;
    fighter.pushback_frame = 0;
    fighter.push_cos = 0;
    fighter.push_sin = 0;
}

// 겹친 두 캐릭터를 밀어낸다.
void SeparateBodies(State& next, Vec view) noexcept {
    auto& a = next.fighters[0];
    auto& b = next.fighters[1];
    const Vec d{static_cast<std::int64_t>(b.position_x) - a.position_x,
                static_cast<std::int64_t>(b.position_z) - a.position_z};
    const auto squared = d.x * d.x + d.z * d.z;

    if (squared >= static_cast<std::int64_t>(BodySeparation) * BodySeparation)
        return;
    const auto distance = static_cast<std::int64_t>(ISqrt(static_cast<std::uint64_t>(squared)));
    const auto unit = UnitOf(d, view);
    const auto deficit = BodySeparation - distance;
    const bool a_first =
        a.position_x < b.position_x || (a.position_x == b.position_x && a.position_z <= b.position_z);
    const auto a_share = a_first ? deficit / 2 : deficit - deficit / 2;
    Translate(a, Scale(-a_share, unit));
    Translate(b, Scale(deficit - a_share, unit));

    for (int pass = 0; pass < 2; ++pass) {
        const Vec now{static_cast<std::int64_t>(b.position_x) - a.position_x,
                      static_cast<std::int64_t>(b.position_z) - a.position_z};
        const auto remaining =
            BodySeparation -
            static_cast<std::int64_t>(ISqrt(static_cast<std::uint64_t>(now.x * now.x + now.z * now.z)));

        if (remaining <= 0)
            return;

        const bool push_b = (pass == 0) == a_first;

        if (push_b)
            Translate(b, Scale(remaining, unit));
        else
            Translate(a, Scale(-remaining, unit));
    }
}
}

std::uint16_t MoveFrames(MoveKind kind) noexcept {
    const auto* table = Movement(kind);

    return table ? table->frames : 0;
}

std::int32_t MoveRise(MoveKind kind, std::uint16_t frame) noexcept {
    const auto* table = Movement(kind);

    return table && IsJump(kind) && frame >= 1 && frame <= table->frames ? table->secondary[frame - 1u] : 0;
}

namespace {

const std::array<HurtSphere, 14>* SelectHurtSpheres(CharacterId defender, std::uint8_t defender_pose,
                                                    ReactionKind defender_reaction,
                                                    std::uint16_t reaction_frame,
                                                    AirPose& ground_pose) noexcept {
    const HurtTable* hurt = nullptr;

    for (const auto& table : HurtTables)
        if (table.character == defender && table.pose == defender_pose)
            hurt = &table;
    const auto* air_pose = FindAirPose(defender, defender_reaction, reaction_frame);

    if ((defender_pose != 0 || !air_pose) && defender_reaction != ReactionKind::None &&
        defender_reaction != ReactionKind::DownFaceUp && defender_reaction != ReactionKind::DownFaceDown)
        if (const auto* ground = FindGroundPose(defender, defender_reaction, reaction_frame)) {
            DecodeGroundPose(ground, ground_pose);
            air_pose = &ground_pose;
        }
    if (air_pose)
        return &air_pose->spheres;

    return hurt ? &hurt->spheres : nullptr;
}
}

bool SourceHitContact(CommandId command, std::uint16_t attack_frame, CharacterId defender,
                      std::int32_t forward_mm, std::int32_t side_mm, std::int32_t defender_rise_mm,
                      std::int32_t defender_face_cos, std::int32_t defender_face_sin) noexcept {
    return SourceHitContact(CharacterId::Training, command, attack_frame, defender, forward_mm, side_mm,
                            defender_rise_mm, defender_face_cos, defender_face_sin);
}

bool SourceHitContact(CharacterId attacker, CommandId command, std::uint16_t attack_frame,
                      CharacterId defender, std::int32_t forward_mm, std::int32_t side_mm,
                      std::int32_t defender_rise_mm, std::int32_t defender_face_cos,
                      std::int32_t defender_face_sin, std::uint8_t defender_pose,
                      ReactionKind defender_reaction, std::uint16_t reaction_frame, bool defender_airborne,
                      std::array<std::int32_t, 3>* contact_point) noexcept {
    if (contact_point)
        *contact_point = {};

    if (forward_mm < -MaximumGeometryDistance || forward_mm > MaximumGeometryDistance ||
        side_mm < -MaximumGeometryDistance || side_mm > MaximumGeometryDistance ||
        !IsUnit(defender_face_cos, defender_face_sin))
        return false;
    AirPose ground_pose{};
    const auto* selected =
        SelectHurtSpheres(defender, defender_pose, defender_reaction, reaction_frame, ground_pose);

    if (!selected)
        return false;
    const auto& spheres = *selected;
    std::int32_t hit_radius = 0, air_down = 0;

    for (const auto& shape : AuthoredHitShapes)
        if (shape.command == command && (shape.character == attacker || attacker == CharacterId::Training)) {
            hit_radius = shape.radius_mm;

            if (defender_airborne)
                air_down = shape.air_down_mm;
            else if (defender_pose != 0)
                air_down = shape.ground_down_mm;
        }

    const std::int64_t fc = defender_face_cos, fs = defender_face_sin;

    for (const auto& frame : HitFrames) {
        if (frame.command != command || frame.frame != attack_frame ||
            (attacker != CharacterId::Training && frame.owner != attacker))
            continue;

        for (std::size_t s = frame.first; s < static_cast<std::size_t>(frame.first) + frame.count; ++s) {
            for (const auto& sphere : spheres) {
                const std::int64_t cf = sphere.centre[0], cs = sphere.centre[1];
                const std::array<std::int64_t, 3> point{
                    forward_mm + (cf * fc - cs * fs) / UnitQ16, side_mm + (cf * fs + cs * fc) / UnitQ16,
                    static_cast<std::int64_t>(sphere.centre[2]) + defender_rise_mm};
                const auto contact = [&](const HitSegment& line) {
                    if (!SegmentTouchesSphere(line, point, sphere.radius + hit_radius))
                        return false;

                    if (contact_point) {
                        std::int64_t length = 0, along = 0;

                        for (std::size_t i = 0; i < 3; ++i) {
                            const std::int64_t d = line.b[i] - line.a[i];
                            length += d * d;
                            along += (point[i] - line.a[i]) * d;
                        }

                        along = std::clamp<std::int64_t>(along, 0, length);

                        for (std::size_t i = 0; i < 3; ++i)
                            (*contact_point)[i] = static_cast<std::int32_t>(
                                line.a[i] + (length ? (line.b[i] - line.a[i]) * along / length : 0));
                    }

                    return true;
                };
                const auto& segment = HitSegments[s];

                if (contact(segment))
                    return true;

                if (air_down > 0)
                    for (const auto& endpoint : {segment.a, segment.b}) {
                        auto lower = endpoint;
                        lower[2] -= air_down;

                        if (contact({endpoint, lower}))
                            return true;
                    }
            }
        }
    }

    return false;
}

std::int32_t AttackTravel(CharacterId attacker, CommandId command, std::uint16_t attack_frame) noexcept {
    for (const auto& table : AttackTravels)
        if (table.command == command && (attacker == CharacterId::Training || table.owner == attacker) &&
            attack_frame >= 1 && attack_frame <= table.frames)
            return table.forward[attack_frame - 1u];

    return 0;
}

std::int32_t AttackTravel(CommandId command, std::uint16_t attack_frame) noexcept {
    return AttackTravel(CharacterId::Training, command, attack_frame);
}

const PushbackDefinition* GetPushbackDefinition(std::uint8_t id) noexcept {
    return id != NoPushback && id < Pushbacks.size() ? &Pushbacks[id] : nullptr;
}

std::uint16_t PushbackFrames(std::uint8_t id) noexcept {
    const auto* pushback = GetPushbackDefinition(id);

    return pushback
               ? static_cast<std::uint16_t>(
                     std::max<std::size_t>(PushbackExtraFrames, pushback->extra_count) + pushback->duration)
               : 0;
}

std::int32_t PushbackDisplacement(std::uint8_t id, std::uint16_t frame) noexcept {
    const auto* pushback = GetPushbackDefinition(id);

    if (!pushback || frame == 0 || frame > PushbackFrames(id))
        return 0;

    return frame <= std::max<std::size_t>(PushbackExtraFrames, pushback->extra_count)
               ? pushback->extra[frame - 1u]
               : pushback->linear;
}

const ReactionDefinition* GetReactionDefinition(ReactionKind kind) noexcept {
    if (kind == ReactionKind::None)
        return nullptr;

    for (const auto* module : Modules)
        for (const auto& reaction : module->reactions)
            if (reaction.kind == kind)
                return &reaction;

    return nullptr;
}

std::int32_t ReactionBodyHeight(CharacterId character, ReactionKind reaction, std::uint16_t frame) noexcept {
    const auto* pose = FindAirPose(character, reaction, frame);

    if (pose)
        return pose->height;

    if (const auto* imported = FindGroundPose(character, reaction, frame)) {
        AirPose decoded{};
        DecodeGroundPose(imported, decoded);

        return decoded.height;
    }

    return 0;
}

std::int32_t AirPresentationOffset(const Fighter& fighter) noexcept {
    return fighter.air_phase == 1 || fighter.air_phase == 3
               ? fighter.air_height -
                     ReactionBodyHeight(fighter.character, fighter.reaction, fighter.reaction_frame)
               : 0;
}

std::int32_t GenericAirInitialVelocity(std::int32_t height, std::int32_t landing_height) noexcept {
    return SolveAirVelocity(height, landing_height, GenericAirSolveFrames);
}

constexpr std::array<std::int32_t, 16> AirCarryPercent{
    {90, 90, 90, 103, 103, 103, 103, 103, 103, 103, 103, 103, 103, 103, 103, 103}};

constexpr std::int32_t AirCarryGrowthPercent = 150;

constexpr int AirCarryFlatHits = 5;

constexpr std::int32_t AirCarryCapPercent = 500;

constexpr std::int32_t AirDriftBase = 16;
constexpr std::int16_t MaxAirDrift = 40;

constexpr std::int64_t AirDriftEarly = juggle_policy::InitialDrift;

constexpr std::int64_t JuggleChaseRange = 1100;
constexpr std::int64_t JuggleChaseStep = 40;

std::int32_t AirCarry(const Fighter& fighter) noexcept {
    if (fighter.combo_hits < 2)
        return 100;
    const int air_hit = int(fighter.combo_hits) - 1;
    const auto index = std::clamp<int>(air_hit - 1, 0, int(AirCarryPercent.size()) - 1);
    const auto growth = 100 + AirCarryGrowthPercent * std::max(0, air_hit - AirCarryFlatHits);

    return std::min(AirCarryCapPercent, AirCarryPercent[static_cast<std::size_t>(index)] * growth / 100);
}

Vec UncoveredAirDrift(const Fighter& fighter) noexcept {
    const Vec drift{fighter.air_drift_x, fighter.air_drift_z};
    const auto length = static_cast<std::int64_t>(ISqrt(static_cast<std::uint64_t>(Dot(drift, drift))));

    if (length == 0 || fighter.pushback == NoPushback)
        return drift;
    std::int64_t displacement = PushbackDisplacement(fighter.pushback, fighter.pushback_frame + 1);

    if (fighter.combo_hits && IsAirborne(fighter))
        displacement = displacement * AirCarry(fighter) / 100;
    const auto push = Scale(displacement, {fighter.push_cos, fighter.push_sin});
    const auto covered = std::clamp<std::int64_t>(Dot(push, drift) / length, 0, length);

    return {drift.x * (length - covered) / length, drift.z * (length - covered) / length};
}

bool StandingStagger(const Fighter& fighter) noexcept {
    if (fighter.reaction == ReactionKind::None)
        return false;
    const auto* reaction = GetReactionDefinition(fighter.reaction);

    return reaction && (reaction->flags & ReactionLying) == 0 && reaction->air_last == 0;
}

bool IsAirborne(const Fighter& fighter) noexcept {
    if (fighter.air_phase != 0)
        return fighter.air_phase == 1 || fighter.air_phase == 3;

    for (const auto& rule : AuthoredAirWindows)
        if (fighter.reaction == rule.reaction && fighter.reaction_frame >= 1 &&
            fighter.reaction_frame <= rule.last)
            return true;
    const auto* reaction = GetReactionDefinition(fighter.reaction);

    return reaction && (reaction->flags & ReactionLying) == 0 && fighter.reaction_frame >= 1 &&
           fighter.reaction_frame <= reaction->air_last;
}

bool RageActive(const Fighter& fighter) noexcept {
    return fighter.hp > 0 && !fighter.rage_spent &&
           (fighter.training_rage == 1 || (fighter.training_rage == 0 && fighter.hp <= RageHpThreshold));
}

Posture PostureOf(const Fighter& fighter) noexcept {
    if (fighter.rise_frame != 0)
        return Posture::Rising;

    return fighter.crouch_frames >= FullCrouchFrames ? Posture::Crouching : Posture::Standing;
}

std::uint64_t ModuleDataHash(CharacterId character) noexcept {
    const auto* module = ModuleOf(character);

    return module ? module->data_hash : 0;
}

const char* StandingReactionClip(const Fighter& fighter) noexcept {
    return fighter.standing_motion && fighter.standing_motion <= StandingRootTables.size()
               ? StandingRootTables[fighter.standing_motion - 1].clip
               : nullptr;
}

std::uint64_t RulesFingerprint() noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    auto mix = [&hash](std::uint64_t value, int bytes) {
        for (int index = 0; index < bytes; ++index) {
            hash ^= (value >> (index * 8)) & 0xffu;
            hash *= 1099511628211ULL;
        }
    };
    mix(RulesVersion, 2);
    mix(MaximumComboHits, 1);
    mix(MaximumPaulQcfAirHits, 1);
    mix(juggle_policy::HitSlope, 1);
    mix(juggle_policy::MaximumDrift, 1);
    mix(HeatDurationFrames, 2);
    mix(HeatBurstDurationFrames, 2);
    mix(HeatBurstHoldFirst, 2);
    mix(HeatBurstHoldLast, 2);
    mix(static_cast<std::uint64_t>(RunMinimumDistance), 4);
    mix(static_cast<std::uint64_t>(AirDriftEarly), 2);
    mix(static_cast<std::uint64_t>(JuggleChaseRange), 4);
    mix(static_cast<std::uint64_t>(JuggleChaseStep), 2);
    mix(AirGeometryFingerprint, 8);
    mix(GroundGeometryFingerprint, 8);
    mix(AuthoredGameplayHash, 8);
    mix(AuthoredAirMotionHash, 8);
    mix(SequenceMotionHash, 8);
    mix(StandingMotionHash, 8);
    mix(CinematicRateHash, 8);

    for (const auto* module : Modules) {
        mix(static_cast<std::uint8_t>(module->character), 1);
        mix(module->data_hash, 8);
    }

    return hash;
}

std::uint16_t ReactionDownFrame(ReactionKind kind) noexcept {
    const auto* reaction = GetReactionDefinition(kind);

    return reaction ? reaction->frames : 0;
}

std::uint8_t GroundHitPose(const Fighter& fighter) noexcept {
    if (IsAirborne(fighter))
        return 0;
    const auto* reaction = GetReactionDefinition(fighter.reaction);

    if (!reaction)
        return 0;

    if (!(reaction->flags & ReactionLying) &&
        !FindGroundPose(fighter.character, fighter.reaction, fighter.reaction_frame) &&
        !FindAirPose(fighter.character, fighter.reaction, fighter.reaction_frame))
        return 0;

    for (unsigned depth = 0; reaction && depth < 256; ++depth) {
        if (reaction->flags & ReactionLying)
            return (reaction->flags & ReactionFaceDown) ? 2 : 1;
        reaction = GetReactionDefinition(reaction->next);
    }

    return 0;
}

bool IsCounterHit(const Fighter& defender) noexcept {
    if (defender.attack_frame == 0)
        return false;
    const auto* attack = GetAttackDefinition(defender.character, defender.active_command);

    return attack && defender.attack_frame + 1u <= attack->active_last_frame;
}

bool ResolveCounterHit(const Fighter& defender, std::uint8_t buttons) noexcept {
    if (buttons & TrainingCounterOn)
        return true;

    if (buttons & TrainingCounterOff)
        return false;

    return IsCounterHit(defender);
}

std::uint8_t HitSituation(const Fighter& attacker, const Fighter& defender, bool crouching, bool blocked,
                          bool counter) noexcept {
    if (blocked)
        return crouching ? HitGuardCrouch : HitGuard;

    const Vec to_attacker{static_cast<std::int64_t>(attacker.position_x) - defender.position_x,
                          static_cast<std::int64_t>(attacker.position_z) - defender.position_z};
    const auto face = FaceOf(defender);
    const auto forward = Dot(to_attacker, face), left = Dot(to_attacker, LeftOf(face));
    const auto side = left < 0 ? -left : left;
    std::uint8_t situation = HitFront;

    if (forward >= side)
        situation = counter ? HitCounter : HitFront;
    else if (-forward >= side)
        situation = HitBack;
    else
        situation = left > 0 ? HitLeft : HitRight;

    return static_cast<std::uint8_t>(situation + (crouching ? 1 : 0));
}

const AttackDefinition* GetAttackDefinition(std::uint8_t button) noexcept {
    for (const auto& attack : CommonModule.attacks)
        if (attack.rules.button == button)
            return &attack.rules;

    return nullptr;
}

const AttackDefinition* GetAttackDefinition(CommandId command) noexcept {
    for (const auto* module : Modules)
        for (const auto& attack : module->attacks)
            if (attack.command == command)
                return &attack.rules;

    return nullptr;
}

const AttackDefinition* GetAttackDefinition(CharacterId character, CommandId command) noexcept {
    if (const auto* module = ModuleOf(character); module && character != CharacterId::Training)
        for (const auto& attack : module->attacks)
            if (attack.command == command)
                return &attack.rules;

    return GetAttackDefinition(command);
}

const AttackDefinition* GetContactAttackDefinition(CharacterId character, CommandId command,
                                                   bool heat) noexcept {
    if (heat && command != CommandId::None)
        if (const auto* module = ModuleOf(character))
            for (const auto& attack : module->attacks)
                if (attack.rules.heat_parent == command)
                    return &attack.rules;

    return GetAttackDefinition(character, command);
}

const MoveDefinition* GetMoveDefinition(MoveKind move) noexcept {
    return FindMove(move);
}

std::uint16_t AttackEndFrame(CharacterId character, CommandId command) noexcept {
    if (const auto* r = RecoveryOf(character, command))
        return r->last_attack_frame + r->recovery_frames;
    const auto* a = GetAttackDefinition(character, command);

    return a ? a->total_frames : 0;
}

const char* AttackPoseClip(const Fighter& fighter, std::uint16_t& sample) noexcept {
    sample = fighter.attack_frame ? fighter.attack_frame - 1 : 0;

    if (const auto* r = RecoveryOf(fighter.character, fighter.active_command);
        r && fighter.attack_frame > r->last_attack_frame) {
        sample = fighter.attack_frame - r->last_attack_frame - 1;

        return r->clip;
    }

    const auto* a = GetAttackDefinition(fighter.character, fighter.active_command);

    return a ? a->clip : nullptr;
}

const CommandDefinition* GetCommandDefinition(CommandId command) noexcept {
    for (const auto* module : Modules)
        for (const auto& definition : module->commands)
            if (definition.id == command)
                return &definition;

    return nullptr;
}

bool operator==(const Input& a, const Input& b) noexcept {
    return a.horizontal == b.horizontal && a.buttons == b.buttons && a.vertical == b.vertical &&
           a.training_rage == b.training_rage && a.view_flipped == b.view_flipped;
}

bool operator!=(const Input& a, const Input& b) noexcept {
    return !(a == b);
}

bool operator==(const Fighter& a, const Fighter& b) noexcept {
    return a.position_x == b.position_x && a.position_z == b.position_z && a.hp == b.hp &&
           a.attack_frame == b.attack_frame && a.stun_frames == b.stun_frames &&
           a.previous_buttons == b.previous_buttons && a.active_attack == b.active_attack &&
           a.previous_horizontal == b.previous_horizontal && a.previous_vertical == b.previous_vertical &&
           a.character == b.character && a.facing == b.facing && a.active_command == b.active_command &&
           a.pending_command == b.pending_command && a.pending_frames == b.pending_frames &&
           a.direction_count == b.direction_count && a.direction_history == b.direction_history &&
           a.attack_connected == b.attack_connected && a.reaction == b.reaction &&
           a.reaction_frame == b.reaction_frame && a.pushback == b.pushback &&
           a.pushback_frame == b.pushback_frame && a.push_cos == b.push_cos && a.push_sin == b.push_sin &&
           a.move == b.move && a.move_frame == b.move_frame && a.face_cos == b.face_cos &&
           a.face_sin == b.face_sin && a.queued_command == b.queued_command &&
           a.crouch_frames == b.crouch_frames && a.rise_frame == b.rise_frame &&
           a.combo_hits == b.combo_hits && a.combo_flags == b.combo_flags && a.air_height == b.air_height &&
           a.air_velocity == b.air_velocity && a.air_drift_x == b.air_drift_x &&
           a.air_drift_z == b.air_drift_z && a.air_phase == b.air_phase &&
           a.training_rage == b.training_rage && a.heat_frames == b.heat_frames &&
           a.heat_used == b.heat_used && a.rage_spent == b.rage_spent &&
           a.sequence_command == b.sequence_command && a.sequence_owner == b.sequence_owner &&
           a.sequence_frame == b.sequence_frame && a.sequence_subframe == b.sequence_subframe &&
           a.sequence_role == b.sequence_role && a.standing_motion == b.standing_motion &&
           a.standing_frame == b.standing_frame;
}

bool operator!=(const Fighter& a, const Fighter& b) noexcept {
    return !(a == b);
}

bool operator==(const State& a, const State& b) noexcept {
    return a.frame == b.frame && a.fighters == b.fighters && a.view_cos == b.view_cos &&
           a.view_sin == b.view_sin && a.hitstop_enabled == b.hitstop_enabled &&
           a.hitstop_frames == b.hitstop_frames;
}

bool operator!=(const State& a, const State& b) noexcept {
    return !(a == b);
}

bool IsValid(const Input& input) noexcept {
    return ValidDirection(input.horizontal) && ValidDirection(input.vertical) &&
           ValidButtons(input.buttons) && input.training_rage <= 2 && input.view_flipped <= 1;
}

bool IsValid(const State& state) noexcept {
    const auto& a = state.fighters[0];
    const auto& b = state.fighters[1];

    if ((a.sequence_role != 0) != (b.sequence_role != 0) ||
        (a.sequence_role &&
         (a.sequence_role + b.sequence_role != 3 || a.sequence_command != b.sequence_command ||
          a.sequence_owner != b.sequence_owner || a.sequence_frame != b.sequence_frame ||
          a.sequence_subframe != b.sequence_subframe)))
        return false;

    for (const auto& fighter : state.fighters) {
        const auto* attack = GetAttackDefinition(fighter.character, fighter.active_command);
        const auto* active = GetCommandDefinition(fighter.active_command);
        const auto* pending = GetCommandDefinition(fighter.pending_command);
        const auto down_frame = ReactionDownFrame(fighter.reaction);
        const auto* sequence = GetAttackDefinition(fighter.sequence_owner, fighter.sequence_command);

        if (fighter.heat_frames > HeatDurationFrames || fighter.heat_used > 1 || fighter.rage_spent > 1 ||
            (fighter.heat_frames && !fighter.heat_used) || fighter.sequence_role > 2 ||
            (fighter.sequence_subframe >= 100 ||
             fighter.sequence_subframe % SequenceRatePercent(fighter) != 0) ||
            (fighter.sequence_role == 0
                 ? (fighter.sequence_frame != 0 || fighter.sequence_subframe != 0 ||
                    fighter.sequence_command != CommandId::None ||
                    fighter.sequence_owner != CharacterId::Training)
                 : (!sequence || !sequence->finish_frames || fighter.sequence_frame == 0 ||
                    fighter.sequence_frame > sequence->finish_frames || fighter.attack_frame ||
                    fighter.move != MoveKind::None || fighter.reaction != ReactionKind::None ||
                    fighter.stun_frames || fighter.hp == 0)) ||
            fighter.training_rage > 2 || fighter.air_phase > 3 || fighter.combo_hits > MaximumComboHits ||
            (fighter.air_phase == 0 && (fighter.air_height != 0 || fighter.air_velocity != 0)) ||
            ((fighter.air_phase == 1 || fighter.air_phase == 2) &&
             (AirLandingFrame(fighter.reaction) == 0 ||
              !FindAirPose(fighter.character, fighter.reaction, fighter.reaction_frame))) ||
            (fighter.air_phase == 3 &&
             (fighter.air_height < 0 || fighter.air_height > 10000 || fighter.air_velocity != 0 ||
              fighter.reaction_frame >= ClipAirLandingFrame(fighter.reaction) ||
              !HasReactionPose(fighter.character, fighter.reaction, fighter.reaction_frame) ||
              !HasReactionPose(fighter.character, fighter.reaction,
                               ClipAirLandingFrame(fighter.reaction)))) ||
            (fighter.air_phase == 1 && (fighter.air_height < 0 || fighter.air_height > 10000 ||
                                        fighter.air_velocity < -1000 || fighter.air_velocity > 1000)) ||
            (fighter.air_phase == 2 && (fighter.air_height != 0 || fighter.air_velocity != 0)) ||
            (fighter.air_phase != 1 && (fighter.air_drift_x != 0 || fighter.air_drift_z != 0)) ||
            fighter.air_drift_x < -MaxAirDrift || fighter.air_drift_x > MaxAirDrift ||
            fighter.air_drift_z < -MaxAirDrift || fighter.air_drift_z > MaxAirDrift ||
            (fighter.reaction != ReactionKind::None && down_frame == 0) ||
            (fighter.reaction == ReactionKind::None
                 ? fighter.reaction_frame != 0
                 : (fighter.reaction_frame == 0 || fighter.reaction_frame > down_frame ||
                    fighter.attack_frame != 0 || fighter.stun_frames != 0 || fighter.pending_frames != 0)) ||
            fighter.position_x < -StageLimit || fighter.position_x > StageLimit ||
            fighter.position_z < -StageLimit || fighter.position_z > StageLimit ||
            !IsUnit(fighter.face_cos, fighter.face_sin) || fighter.hp < 0 || fighter.hp > InitialHp ||
            (fighter.attack_frame == 0 && fighter.active_attack != 0) ||
            (fighter.attack_frame != 0 &&
             (attack == nullptr ||
              fighter.attack_frame > AttackEndFrame(fighter.character, fighter.active_command))) ||
            fighter.stun_frames > MaximumStunFrames ||
            (fighter.standing_motion == 0
                 ? fighter.standing_frame != 0
                 : (fighter.standing_motion > StandingRootTables.size() || !fighter.stun_frames ||
                    fighter.hp == 0 || fighter.reaction != ReactionKind::None || fighter.sequence_role ||
                    fighter.standing_frame >= StandingRootTables[fighter.standing_motion - 1].count)) ||
            fighter.attack_connected > 1 ||
            (fighter.attack_connected && (!attack || fighter.attack_frame < attack->active_frame)) ||
            (fighter.attack_frame != 0 && fighter.stun_frames != 0) ||
            !ValidButtons(fighter.previous_buttons) || !ValidDirection(fighter.previous_horizontal) ||
            !ValidDirection(fighter.previous_vertical) || !ValidCharacter(fighter.character) ||
            (fighter.facing != -1 && fighter.facing != 1) ||
            (fighter.attack_frame == 0 && fighter.active_command != CommandId::None) ||
            (fighter.attack_frame != 0 && (!active || !Available(*active, fighter.character) ||
                                           active->fallback_attack != fighter.active_attack)) ||
            ((fighter.pending_command == CommandId::None) != (fighter.pending_frames == 0)) ||
            fighter.pending_frames > CommandBufferFrames ||
            (fighter.pending_command != CommandId::None &&
             (!pending || !Available(*pending, fighter.character))) ||
            fighter.direction_count > DirectionHistoryFrames ||
            (fighter.move == MoveKind::None
                 ? fighter.move_frame != 0
                 : (!Movement(fighter.move) || !MoveAllowed(fighter.move, fighter.character) ||
                    (fighter.move_frame == 0 && !IsLoop(fighter.move)) ||
                    fighter.move_frame >= MoveFrames(fighter.move) || fighter.attack_frame != 0 ||
                    fighter.stun_frames != 0 || fighter.reaction != ReactionKind::None)) ||
            (fighter.pushback == NoPushback
                 ? fighter.pushback_frame != 0 || fighter.push_cos != 0 || fighter.push_sin != 0
                 : (!GetPushbackDefinition(fighter.pushback) ||
                    fighter.pushback_frame >= PushbackFrames(fighter.pushback) ||
                    !IsUnit(fighter.push_cos, fighter.push_sin))) ||
            (fighter.hp == 0 &&
             (fighter.attack_frame != 0 || fighter.stun_frames != 0 || fighter.pending_frames != 0)) ||

            fighter.crouch_frames > FullCrouchFrames || fighter.rise_frame > RiseFrames ||
            (fighter.combo_flags & static_cast<std::uint8_t>(~(ComboScrewed | ComboPaulQcfAirMask))) != 0 ||
            PaulQcfAirHits(fighter) > fighter.combo_hits ||
            ((fighter.combo_hits != 0 || fighter.combo_flags != 0) &&
             fighter.reaction == ReactionKind::None && fighter.stun_frames == 0 &&
             fighter.sequence_role == 0) ||
            (fighter.crouch_frames != 0 && fighter.rise_frame != 0) ||
            ((fighter.crouch_frames != 0 || fighter.rise_frame != 0) &&
             (fighter.attack_frame != 0 || fighter.reaction != ReactionKind::None || fighter.hp == 0)) ||
            (fighter.queued_command != CommandId::None &&
             (fighter.attack_frame == 0 ||
              !FindString(fighter.character, fighter.active_command, fighter.queued_command) ||
              fighter.attack_frame >
                  FindString(fighter.character, fighter.active_command, fighter.queued_command)->start))) {
            return false;
        }

        for (std::size_t i = 0; i < DirectionHistoryFrames; ++i) {
            const auto direction = fighter.direction_history[i];

            if (i < fighter.direction_count ? (direction < 1 || direction > 9) : direction != 0)
                return false;
        }
    }

    return IsUnit(state.view_cos, state.view_sin) && state.hitstop_enabled <= 1 &&
           state.hitstop_frames <= 2 && (state.hitstop_enabled || state.hitstop_frames == 0);
}

// 경기 시작 상태를 만든다.
State MakeInitialState(CharacterId first, CharacterId second) noexcept {
    State state;

    state.fighters[0].position_x = -600;
    state.fighters[1].position_x = 600;
    state.fighters[0].character = first;
    state.fighters[1].character = second;
    state.fighters[1].facing = -1;
    state.fighters[1].face_cos = -UnitQ16;

    return state;
}

// 입력을 받아 한 프레임을 진행한다.
bool Step(State& state, const std::array<Input, 2>& inputs, MotionTrace* trace) noexcept {
    if (trace)
        *trace = {};

    if (!IsValid(state) || !IsValid(inputs[0]) || !IsValid(inputs[1]) ||
        state.frame == std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    State next = state;

    for (std::size_t player = 0; player < 2; ++player)
        next.fighters[player].training_rage = inputs[player].training_rage;

    if (state.fighters[0].sequence_role != 0) {
        const std::size_t attacker = state.fighters[0].sequence_role == 1 ? 0 : 1;
        const auto defender = 1 - attacker;
        const auto& a = state.fighters[attacker];
        const auto* sequence = GetAttackDefinition(a.sequence_owner, a.sequence_command);
        const auto edges = static_cast<std::uint8_t>(
            inputs[defender].buttons & ~state.fighters[defender].previous_buttons & AllAttackButtons);
        const auto held = inputs[defender].buttons & AllAttackButtons;
        const bool broken = sequence->system_action == SystemAction::Throw && a.sequence_frame <= 20 &&
                            (sequence->throw_break == 5 ? (edges & (LP | RP)) != 0
                                                        : CancelPressed(sequence->throw_break, edges, held));
        auto& source = next.fighters[attacker];
        CaptureDirection(source, inputs[attacker], source.facing);
        const auto attack_edges =
            static_cast<std::uint8_t>(inputs[attacker].buttons & ~a.previous_buttons & AllAttackButtons);
        const AuthoredSequenceFollowup* followup = nullptr;

        for (const auto& rule : AuthoredSequenceFollowups) {
            if (rule.character != a.character || rule.from != a.sequence_command)
                continue;
            const auto* command = GetCommandDefinition(rule.to);

            if (source.pending_command == CommandId::None && a.sequence_frame >= rule.first &&
                a.sequence_frame <= rule.last && command &&
                Pressed(command->chord, command->fallback_attack, attack_edges,
                        inputs[attacker].buttons & AllAttackButtons) &&
                Matches(source, *command)) {
                source.pending_command = rule.to;
                source.pending_frames = CommandBufferFrames;
            }

            if (source.pending_command == rule.to && a.sequence_frame >= rule.release)
                followup = &rule;
        }

        const AuthoredSequenceExit* exit = nullptr;
        const auto direction = 5 + inputs[attacker].horizontal * a.facing + inputs[attacker].vertical * 3;

        for (const auto& rule : AuthoredSequenceExits)
            if (rule.character == a.character && rule.from == a.sequence_command &&
                a.sequence_frame == rule.frame && direction == rule.direction)
                exit = &rule;
        const bool finished = a.sequence_frame >= sequence->finish_frames || followup || exit;

        const unsigned phase = a.sequence_subframe + SequenceRatePercent(a);
        const bool advance = phase >= 100;

        for (std::size_t side = 0; side < 2; ++side) {
            auto& fighter = next.fighters[side];
            fighter.previous_buttons = inputs[side].buttons;
            fighter.previous_horizontal = inputs[side].horizontal;
            fighter.previous_vertical = inputs[side].vertical;

            if (broken || finished) {
                fighter.sequence_command = CommandId::None;
                fighter.sequence_owner = CharacterId::Training;
                fighter.sequence_frame = 0;
                fighter.sequence_subframe = 0;
                fighter.sequence_role = 0;
                fighter.combo_hits = 0;
                fighter.combo_flags = 0;
                fighter.pending_command = CommandId::None;
                fighter.pending_frames = 0;
                ClearHistory(fighter);
            } else if (advance) {
                const auto* previous = SequenceRoot(a.sequence_owner, a.sequence_command, a.sequence_frame);
                const auto* current =
                    SequenceRoot(a.sequence_owner, a.sequence_command, a.sequence_frame + 1);

                if (previous && current) {
                    const bool actor = side == attacker;
                    const auto forward = actor ? current->attacker_forward - previous->attacker_forward
                                               : current->victim_forward - previous->victim_forward;
                    const auto left = actor ? current->attacker_left - previous->attacker_left
                                            : current->victim_left - previous->victim_left;
                    const auto d = Scale(forward, FaceOf(a));
                    const auto l = Scale(left, LeftOf(FaceOf(a)));
                    Translate(fighter, {d.x + l.x, d.z + l.z});
                }

                ++fighter.sequence_frame;
                fighter.sequence_subframe = static_cast<std::uint8_t>(phase - 100);
            } else {
                fighter.sequence_subframe = static_cast<std::uint8_t>(phase);
            }
        }

        if (!broken && (advance || finished)) {
            const int total = sequence->finish_damage + sequence->damage;
            const int through = finished ? total
                                         : SequenceDamageThrough(a.sequence_owner, a.sequence_command, total,
                                                                 a.sequence_frame);
            const int before =
                SequenceDamageThrough(a.sequence_owner, a.sequence_command, total, a.sequence_frame - 1u);
            auto& victim = next.fighters[defender];
            victim.hp = std::max(0, victim.hp - (through - before));
            auto& owner = next.fighters[attacker];

            if (through > before && !owner.heat_used &&
                HeatEngager(GetAttackDefinition(a.sequence_owner, a.sequence_command))) {
                owner.heat_used = 1;
                owner.heat_frames = HeatDurationFrames;
            }
        }

        if (broken) {
            auto& victim = next.fighters[defender];
            const auto break_face =
                UnitOf({source.position_x - victim.position_x, source.position_z - victim.position_z},
                       {-a.face_cos, -a.face_sin});
            victim.face_cos = static_cast<std::int32_t>(break_face.x);
            victim.face_sin = static_cast<std::int32_t>(break_face.z);
            victim.facing = Dot(break_face, {state.view_cos, state.view_sin}) >= 0 ? 1 : -1;
        }

        if (finished && !broken) {
            auto& victim = next.fighters[defender];
            victim.combo_hits = std::max<std::uint8_t>(1, state.fighters[defender].combo_hits);
            victim.reaction = ReactionKind::DownFaceUp;
            victim.reaction_frame = 1;

            if (!followup && !exit)
                for (const auto& table : SequenceRootTables) {
                    if (table.owner != a.sequence_owner || table.command != a.sequence_command)
                        continue;

                    for (std::size_t side = 0; side < 2; ++side) {
                        auto& fighter = next.fighters[side];
                        const bool actor = side == attacker;
                        fighter.reaction = actor ? table.attacker_end : table.victim_end;
                        fighter.reaction_frame = fighter.reaction == ReactionKind::None ? 0 : 1;

                        if (fighter.reaction == ReactionKind::None)
                            fighter.combo_hits = fighter.combo_flags = 0;
                        const auto forward = Scale(actor ? table.attacker_cos : table.victim_cos, FaceOf(a));
                        const auto left =
                            Scale(actor ? table.attacker_left : table.victim_left, LeftOf(FaceOf(a)));
                        fighter.face_cos = static_cast<std::int32_t>(forward.x + left.x);
                        fighter.face_sin = static_cast<std::int32_t>(forward.z + left.z);
                        fighter.facing = Dot(FaceOf(fighter), {state.view_cos, state.view_sin}) >= 0 ? 1 : -1;
                    }

                    break;
                }
            if (followup && victim.hp > 0) {
                const auto* command = GetCommandDefinition(followup->to);
                source.active_command = command->id;
                source.active_attack = command->fallback_attack;
                source.attack_frame = 1;
                source.attack_connected = 0;
                victim.reaction = ReactionKind::None;
                victim.reaction_frame = 0;
                victim.stun_frames = 35;
                victim.face_cos = -source.face_cos;
                victim.face_sin = -source.face_sin;
                victim.facing = -source.facing;
            } else if (exit && victim.hp > 0) {
                StartMove(source, exit->to);
                AdvanceMove(source);
            }
        }

        ++next.frame;
        state = next;

        return true;
    }

    if (state.hitstop_frames != 0) {
        for (std::size_t player = 0; player < 2; ++player) {
            const auto& before = state.fighters[player];
            auto& after = next.fighters[player];
            const auto& input = inputs[player];
            const auto edges =
                static_cast<std::uint8_t>(input.buttons & ~before.previous_buttons & AllAttackButtons);
            CaptureDirection(after, input, before.facing);
            after.previous_buttons = input.buttons;
            after.previous_horizontal = input.horizontal;
            after.previous_vertical = input.vertical;

            if (before.hp == 0 || before.reaction != ReactionKind::None || (input.buttons & Guard)) {
                after.pending_command = CommandId::None;
                after.pending_frames = 0;
                ClearHistory(after);
                continue;
            }

            const auto direction =
                static_cast<std::uint8_t>(5 + input.horizontal * before.facing + input.vertical * 3);
            const auto* cancel =
                before.attack_frame && after.queued_command == CommandId::None
                    ? MatchString(before, before.active_command, edges, input.buttons & AllAttackButtons,
                                  direction, before.attack_frame,
                                  GroundHitPose(state.fighters[1 - player]) != 0)
                    : nullptr;

            if (cancel) {
                after.queued_command = cancel->to;
                after.pending_command = CommandId::None;
                after.pending_frames = 0;
                ClearHistory(after);
            } else if (after.queued_command == CommandId::None) {
                const auto command = Recognize(after, edges, input.buttons & AllAttackButtons,
                                               GroundHitPose(state.fighters[1 - player]) != 0);

                if (command != CommandId::None) {
                    after.pending_command = command;
                    after.pending_frames = CommandBufferFrames;
                    ClearHistory(after);
                }
            }
        }

        --next.hitstop_frames;
        ++next.frame;
        state = next;

        return true;
    }

    {
        const Vec d{static_cast<std::int64_t>(state.fighters[1].position_x) - state.fighters[0].position_x,
                    static_cast<std::int64_t>(state.fighters[1].position_z) - state.fighters[0].position_z};
        const Vec previous{state.view_cos, state.view_sin};
        auto unit = UnitOf(d, previous);

        if (Dot(unit, previous) < 0)
            unit = {-unit.x, -unit.z};
        next.view_cos = static_cast<std::int32_t>(unit.x);
        next.view_sin = static_cast<std::int32_t>(unit.z);
    }

    for (auto& fighter : next.fighters)
        if (fighter.heat_frames)
            --fighter.heat_frames;
    const Vec view{next.view_cos, next.view_sin};

    std::array<bool, 2> guarding{};
    std::array<bool, 2> crouching{};

    for (std::size_t player = 0; player < 2; ++player) {
        const auto& before = state.fighters[player];
        const auto& opponent = state.fighters[1 - player];
        auto& after = next.fighters[player];
        const auto& input = inputs[player];
        after.previous_buttons = input.buttons;
        after.previous_horizontal = input.horizontal;
        after.previous_vertical = input.vertical;
        const Vec to_opponent{static_cast<std::int64_t>(opponent.position_x) - before.position_x,
                              static_cast<std::int64_t>(opponent.position_z) - before.position_z};
        const auto side_dot = Dot(to_opponent, view);

        const auto facing = static_cast<std::int8_t>(side_dot == 0 ? before.facing : (side_dot > 0 ? 1 : -1));

        const int viewed_facing = input.view_flipped ? -facing : facing;
        CaptureDirection(after, input, facing);
        const bool grounded_move = before.move != MoveKind::None && !IsJump(before.move);

        const bool tracking = before.hp != 0 && before.reaction == ReactionKind::None &&
                              before.stun_frames == 0 && before.attack_frame == 0 &&
                              (grounded_move || (before.move == MoveKind::None && input.horizontal != 0));

        if (tracking) {
            const auto face = UnitOf(to_opponent, FaceOf(before));
            after.face_cos = static_cast<std::int32_t>(face.x);
            after.face_sin = static_cast<std::int32_t>(face.z);
        }

        if (before.move != MoveKind::None && before.pending_command != CommandId::None)
            for (const auto& transition : ModuleOf(before.character)->transitions)
                if (transition.from == before.move && transition.button != 0 &&
                    transition.command == before.pending_command &&
                    before.move_frame + 1 >= transition.first && before.move_frame + 1 <= transition.start &&
                    (transition.last == 0 || before.move_frame + 1 <= transition.last)) {
                    after.pending_frames = CommandBufferFrames;
                    break;
                }
        if (after.pending_frames != 0 && --after.pending_frames == 0)
            after.pending_command = CommandId::None;
        const bool guard_requested = (input.buttons & Guard) != 0;

        if (guard_requested || before.hp == 0 || before.reaction != ReactionKind::None) {
            after.pending_command = CommandId::None;
            after.pending_frames = 0;
            ClearHistory(after);
        } else {
            const auto edges =
                static_cast<std::uint8_t>(input.buttons & ~before.previous_buttons & AllAttackButtons);
            const auto command = Recognize(after, edges, input.buttons & AllAttackButtons,
                                           GroundHitPose(state.fighters[1 - player]) != 0);

            if (command != CommandId::None) {
                after.pending_command = command;
                after.pending_frames = CommandBufferFrames;
                ClearHistory(after);
            }
        }

        if (before.reaction != ReactionKind::None) {
            const auto* reaction = GetReactionDefinition(before.reaction);
            const auto next_frame = static_cast<std::uint16_t>(before.reaction_frame + 1u);
            const auto current =
                static_cast<std::uint8_t>(5 + input.horizontal * facing + input.vertical * 3);
            const auto previous = static_cast<std::uint8_t>(5 + before.previous_horizontal * facing +
                                                            before.previous_vertical * 3);
            const auto edges =
                static_cast<std::uint8_t>(input.buttons & ~before.previous_buttons & AllAttackButtons);
            const bool lying = (reaction->flags & ReactionLying) != 0;
            const bool in_ukemi = before.air_phase != 1 && reaction->ukemi_first != 0 &&
                                  next_frame >= reaction->ukemi_first && next_frame <= reaction->ukemi_last;
            const WakeOption* wake = nullptr;

            if (before.hp != 0 && (lying || in_ukemi))
                for (const auto& option : CommonModule.wake) {
                    if (option.from != before.reaction)
                        continue;
                    const bool direction_ok =
                        option.directions == 0 || (option.directions & Dir(current)) != 0;
                    const bool pressed = option.all_buttons
                                             ? ((input.buttons & option.buttons) == option.buttons &&
                                                (edges & option.buttons) != 0)
                                             : (edges & option.buttons) != 0;

                    if (option.command != CommandId::None && !OwnAttack(before.character, option.command))
                        continue;

                    if (option.buttons != 0 ? pressed && direction_ok : current != previous && direction_ok) {
                        wake = &option;
                        break;
                    }
                }
            if (before.air_phase == 1) {
                const auto* hold = GetAttackDefinition(opponent.character, opponent.active_command);

                if (hold && hold->system_action == SystemAction::HeatBurst &&
                    opponent.attack_frame >= HeatBurstHoldFirst && opponent.attack_frame <= HeatBurstHoldLast)
                    continue;

                after.air_velocity = std::max(-1000, after.air_velocity - AirGravity);
                after.air_height = std::min(10000, after.air_height + after.air_velocity);
                const auto landing_frame = AirLandingFrame(after.reaction);
                const auto landing = ReactionBodyHeight(after.character, after.reaction, landing_frame);

                if (after.air_velocity <= 0 && after.air_height <= landing) {
                    after.air_phase = 2;
                    after.air_height = after.air_velocity = 0;
                    after.reaction_frame = landing_frame;
                } else {
                    after.reaction_frame = std::min<std::uint16_t>(next_frame, landing_frame);
                }

                const auto drift_before_x = after.position_x, drift_before_z = after.position_z;
                Translate(after, UncoveredAirDrift(after));

                if (trace)
                    trace->fighters[player].drift = {after.position_x - drift_before_x,
                                                     after.position_z - drift_before_z};
                continue;
            }

            if (before.air_phase == 3) {
                const auto landing = ClipAirLandingFrame(before.reaction);

                if (wake || next_frame >= landing) {
                    after.air_phase = 0;
                    after.air_height = after.air_velocity = 0;
                } else {
                    const auto remaining = landing - before.reaction_frame;
                    const auto residual = AirPresentationOffset(before);
                    after.air_height =
                        std::clamp(ReactionBodyHeight(before.character, before.reaction, next_frame) +
                                       static_cast<std::int32_t>(static_cast<std::int64_t>(residual) *
                                                                 (remaining - 1) / remaining),
                                   0, 10000);
                }
            }

            if (wake && wake->command != CommandId::None) {
                after.reaction = ReactionKind::None;
                after.reaction_frame = 0;
                after.attack_frame = 1;
                after.attack_connected = 0;
                after.active_command = wake->command;
                after.active_attack = GetCommandDefinition(wake->command)->fallback_attack;
            } else if (wake) {
                after.reaction = ReactionKind::None;
                after.reaction_frame = 0;
                StartMove(after, wake->move);
                AdvanceMove(after);
            } else if (before.reaction_frame < reaction->frames) {
                ++after.reaction_frame;
            } else if (!lying && before.hp != 0 && reaction->next != ReactionKind::None) {
                after.reaction = reaction->next;
                after.reaction_frame = 1;
            } else if (!lying && before.hp != 0) {
                after.reaction = ReactionKind::None;
                after.reaction_frame = 0;
            }

            continue;
        }

        if (before.hp == 0) {
            continue;
        }

        if (before.stun_frames != 0) {
            if (before.standing_motion) {
                const auto& table = StandingRootTables[before.standing_motion - 1];
                const auto frame =
                    static_cast<std::uint16_t>(std::min<int>(before.standing_frame + 1, table.count - 1));
                const auto& a = StandingRootSamples[table.first + before.standing_frame];
                const auto& b = StandingRootSamples[table.first + frame];
                Translate(after, Scale(b.forward - a.forward, FaceOf(before)));
                Translate(after, Scale(b.left - a.left, {before.face_sin, -before.face_cos}));
                after.standing_frame = frame;
            }

            --after.stun_frames;
            continue;
        }

        bool recovered = false;

        if (before.attack_frame != 0) {
            const auto* attack = GetAttackDefinition(before.character, before.active_command);
            const auto next_frame = static_cast<std::uint16_t>(before.attack_frame + 1u);

            if (attack->automatic_command != CommandId::None &&
                before.attack_frame == attack->automatic_frame) {
                after.active_command = attack->automatic_command;
                after.active_attack = GetCommandDefinition(after.active_command)->fallback_attack;
                after.attack_frame = attack->automatic_preserve ? next_frame : 1;
                after.attack_connected = 0;
                after.queued_command = CommandId::None;
                after.pending_command = CommandId::None;
                after.pending_frames = 0;
                continue;
            }

#if defined(MF_FULL_MOVESET)

            bool entered_stance = false;
            const auto stance_direction =
                static_cast<std::uint8_t>(5 + input.horizontal * facing + input.vertical * 3);

            for (const auto& rule : AuthoredStanceTransitions) {
                if (rule.character != before.character || rule.from != before.active_command ||
                    before.attack_frame < rule.first || before.attack_frame > rule.last ||
                    (rule.directions & Dir(stance_direction)) == 0 || guard_requested ||
                    after.queued_command != CommandId::None)
                    continue;
                after.attack_frame = 0;
                after.active_attack = 0;
                after.active_command = CommandId::None;
                after.attack_connected = 0;
                after.queued_command = CommandId::None;
                after.pending_command = CommandId::None;
                after.pending_frames = 0;
                ClearHistory(after);
                StartMove(after, rule.to);
                AdvanceMove(after);
                entered_stance = true;
                break;
            }

            if (entered_stance)
                continue;

            if (const auto run = EngagerRunFrame(attack); run != 0 && before.attack_frame == run &&
                                                          before.heat_used &&
                                                          before.heat_frames + run > HeatDurationFrames &&
                                                          attack->system_action == SystemAction::None) {
                after.attack_frame = 0;
                after.active_attack = 0;
                after.active_command = CommandId::None;
                after.attack_connected = 0;
                after.queued_command = CommandId::None;
                after.pending_command = CommandId::None;
                after.pending_frames = 0;
                ClearHistory(after);
                StartMove(after, MoveKind::EngagerRun);
                AdvanceMove(after);
                continue;
            }

            const auto early_edges =
                static_cast<std::uint8_t>(input.buttons & ~before.previous_buttons & AllAttackButtons);
            const bool devil_preempt = before.active_command == CommandId::KazuyaSidestepKzSideRP &&
                                       (input.buttons & (LP | RP)) == (LP | RP) &&
                                       (early_edges & (LP | RP)) != 0;

            if (before.attack_frame == 1 && before.heat_frames && attack->heat_start != CommandId::None &&
                !devil_preempt) {
                after.active_command = attack->heat_start;
                after.active_attack = GetCommandDefinition(after.active_command)->fallback_attack;
                after.attack_frame = next_frame;
                after.attack_connected = 0;
                Translate(after, Scale(AttackTravel(before.character, after.active_command, next_frame),
                                       FaceOf(after)));
                continue;
            }

            if (before.character == CharacterId::Paul &&
                before.active_command == CommandId::PaulAutoDRPPltsrp00 && before.attack_frame == 1 &&
                GroundHitPose(opponent) != 0) {
                after.attack_frame = next_frame;
                after.attack_connected = 0;
                after.active_command = CommandId::PaulAutoVariantpodowp;
                after.active_attack = GetCommandDefinition(after.active_command)->fallback_attack;
                Translate(after, Scale(AttackTravel(before.character, after.active_command, next_frame),
                                       FaceOf(after)));
                continue;
            }

            if (after.queued_command == CommandId::None) {
                for (const auto& link : SharedClockLinks) {
                    if (link.character == before.character && link.from == before.active_command &&
                        link.frame != 0 && link.frame == before.attack_frame) {
                        after.attack_frame = AuthoredDestinationFrame(before.character, link.from, link.to,
                                                                      link.frame, next_frame, next_frame);
                        after.attack_connected = 0;
                        after.active_command = link.to;
                        after.active_attack = GetCommandDefinition(link.to)->fallback_attack;
                        Translate(after, Scale(AttackTravel(before.character, link.to, after.attack_frame),
                                               FaceOf(after)));
                        break;
                    }
                }

                if (after.active_command != before.active_command)
                    continue;
            }
#endif

            if (after.queued_command == CommandId::None && !guard_requested) {
                const auto edges =
                    static_cast<std::uint8_t>(input.buttons & ~before.previous_buttons & AllAttackButtons);
                const auto direction =
                    static_cast<std::uint8_t>(5 + input.horizontal * facing + input.vertical * 3);

                if (const auto* cancel =
                        MatchString(before, before.active_command, edges,
                                    static_cast<std::uint8_t>(input.buttons & AllAttackButtons), direction,
                                    before.attack_frame, GroundHitPose(opponent) != 0)) {
                    after.queued_command = cancel->to;
                    after.pending_command = CommandId::None;
                    after.pending_frames = 0;
                    ClearHistory(after);
                }
            }
#if defined(MF_FULL_MOVESET)

            if (after.queued_command == CommandId::None && !guard_requested && before.attack_connected &&
                attack->system_action == SystemAction::None && !before.heat_used &&
                (input.buttons & ~before.previous_buttons & (RP | LK)) != 0 &&
                (input.buttons & (RP | LK)) == (RP | LK)) {
                const auto burst = before.character == CharacterId::Kazuya ? CommandId::KazuyaHeatBurst
                                                                           : CommandId::PaulHeatBurst;
                after.heat_used = 1;
                after.heat_frames = HeatBurstDurationFrames;
                after.attack_frame = 1;
                after.attack_connected = 0;
                after.active_command = burst;
                after.active_attack = GetCommandDefinition(burst)->fallback_attack;
                after.pending_command = CommandId::None;
                after.pending_frames = 0;
                ClearHistory(after);
                continue;
            }
#endif
            if (after.queued_command != CommandId::None &&
                next_frame >
                    FindString(before.character, before.active_command, after.queued_command)->start) {
                after.attack_frame = 1;
#if defined(MF_FULL_MOVESET)
                if (FindSharedClock(before.character, before.active_command, after.queued_command, 0))
                    after.attack_frame = next_frame;
#endif
                after.attack_frame =
                    AuthoredDestinationFrame(before.character, before.active_command, after.queued_command, 0,
                                             after.attack_frame, next_frame);
                after.attack_connected = 0;
                after.active_command = after.queued_command;
                after.active_attack = GetCommandDefinition(after.queued_command)->fallback_attack;
                after.queued_command = CommandId::None;

                if (opponent.combo_hits != 0 && IsAirborne(opponent)) {
                    const auto face = UnitOf(to_opponent, FaceOf(before));
                    after.face_cos = static_cast<std::int32_t>(face.x);
                    after.face_sin = static_cast<std::int32_t>(face.z);
                }

                if (after.attack_frame > 1)
                    Translate(after,
                              Scale(AttackTravel(before.character, after.active_command, after.attack_frame),
                                    FaceOf(after)));
                continue;
            }

            after.attack_frame =
                before.attack_frame == AttackEndFrame(before.character, before.active_command) ? 0
                                                                                               : next_frame;

            const auto* recovery = RecoveryOf(before.character, before.active_command);

            if (after.attack_frame != 0 && (!recovery || after.attack_frame <= recovery->last_attack_frame))
                Translate(after,
                          Scale(AttackTravel(before.character, before.active_command, after.attack_frame),
                                FaceOf(after)));

            if (after.attack_frame != 0 && !before.attack_connected &&
                after.attack_frame <= attack->active_frame && opponent.combo_hits != 0 &&
                IsAirborne(opponent) && IsStringFollowUp(before.character, before.active_command)) {
                const auto gap = static_cast<std::int64_t>(
                    ISqrt(static_cast<std::uint64_t>(Dot(to_opponent, to_opponent))));
                const auto chase_before_x = after.position_x, chase_before_z = after.position_z;

                if (gap > JuggleChaseRange)
                    Translate(after, Scale(std::min<std::int64_t>(gap - JuggleChaseRange, JuggleChaseStep),
                                           UnitOf(to_opponent, FaceOf(after))));

                if (trace)
                    trace->fighters[player].chase = {after.position_x - chase_before_x,
                                                     after.position_z - chase_before_z};
            }

            if (after.attack_frame == 0) {
                after.active_attack = 0;
                after.active_command = CommandId::None;
                after.attack_connected = 0;
                after.queued_command = CommandId::None;
            }

            if (after.attack_frame != 0 || !attack->source_data)
                continue;
            recovered = true;
        }

        const CommandDefinition* command = GetCommandDefinition(after.pending_command);
        const int relative = input.horizontal * facing;

        if (before.move == MoveKind::PreJump) {
            const auto next_frame = static_cast<std::uint16_t>(before.move_frame + 1u);

            if (input.vertical == 0) {
                StartMove(after, viewed_facing > 0 ? MoveKind::StepLeft : MoveKind::StepRight);
                AdvanceMove(after);
                continue;
            }

            if (input.vertical > 0) {
                if (next_frame == MoveFrames(MoveKind::PreJump))
                    StartMove(after, relative > 0   ? MoveKind::JumpForward
                                     : relative < 0 ? MoveKind::JumpBack
                                                    : MoveKind::JumpNeutral);
                AdvanceMove(after);
                continue;
            }

            ClearMove(after);
        } else if (before.move != MoveKind::None) {
            const auto next_frame = static_cast<std::uint16_t>(before.move_frame + 1u);
            const bool attack_now = command && !guard_requested;
            const bool lateral_move = IsLateral(before.move);
            const bool left_kind = before.move == MoveKind::StepLeft || before.move == MoveKind::SideWalkLeft;
            const int screen_up = (left_kind == (viewed_facing > 0)) ? 1 : -1;
            const auto edges =
                static_cast<std::uint8_t>(input.buttons & ~before.previous_buttons & AllAttackButtons);

            const auto current = static_cast<std::uint8_t>(5 + relative + input.vertical * 3);
            const auto previous = static_cast<std::uint8_t>(5 + before.previous_horizontal * facing +
                                                            before.previous_vertical * 3);
            const auto* module = ModuleOf(before.character);
            const auto* pending_attack = command ? OwnAttack(before.character, command->id) : nullptr;

            const bool fresh_throw = pending_attack && pending_attack->system_action == SystemAction::Throw &&
                                     (edges & command->chord) == command->chord;
            const bool system_command =
                pending_attack && pending_attack->system_action != SystemAction::None &&
                (pending_attack->system_action != SystemAction::Throw || fresh_throw) &&
                !command->context_only && SystemAllowed(after, command->id);
            const MoveTransition* taken = nullptr;

            for (const auto& transition : module->transitions) {
                if ((system_command && transition.button != 0) || transition.from != before.move ||
                    (transition.first != 0 && next_frame < transition.first) ||
                    (transition.last != 0 && next_frame > transition.last) ||
                    (transition.directions != 0 && (transition.directions & Dir(current)) == 0) ||
                    (transition.fresh && previous == current) ||
                    (transition.pattern.count != 0 && !Matches(after, transition.pattern)))
                    continue;

                if (transition.button == 0
                        ? (!attack_now && (edges & AllAttackButtons) == 0)
                        : (CancelPressed(transition.button, edges,
                                         static_cast<std::uint8_t>(input.buttons & AllAttackButtons)) &&
                           !guard_requested)) {
                    taken = &transition;
                    break;
                }
            }

            if (taken && taken->button == 0) {
                StartMove(after, taken->to);
                AdvanceMove(after);
                continue;
            }

            if (taken) {
                after.pending_command = taken->command;
                after.pending_frames = CommandBufferFrames;
                command = GetCommandDefinition(after.pending_command);
            }

            bool own_attack = system_command, own_pending = false;

            if (command && !guard_requested)
                for (const auto& transition : module->transitions)
                    if (transition.from == before.move && transition.button != 0 &&
                        transition.command == after.pending_command) {
                        own_pending = true;
                        own_attack = own_attack || next_frame >= transition.start;
                    }
            const auto* rules = FindMove(before.move);
            const auto flags = rules ? rules->flags : 0;
            const bool cancel =
                rules && next_frame >= rules->cancel_first &&
                (((flags & MoveCancelOnAttack) && attack_now && (!own_pending || own_attack) &&
                  (rules->attack_cancel_last == 0 || next_frame <= rules->attack_cancel_last)) ||
                 ((flags & MoveCancelOnOwnAttack) && own_attack) ||
                 ((flags & MoveCancelOnBack) && relative < 0) ||
                 ((flags & MoveCancelOnForward) && relative > 0) ||
                 ((flags & MoveCancelOnUp) && input.vertical > 0) ||
                 ((flags & MoveHoldDownForward) && !(relative > 0 && input.vertical < 0)) ||
                 (lateral_move && (flags & MoveLoop) && input.vertical != screen_up));

            if (!cancel && (before.move == MoveKind::StepLeft || before.move == MoveKind::StepRight) &&
                input.vertical == screen_up && before.previous_vertical != screen_up) {
                StartMove(after, left_kind ? MoveKind::SideWalkLeft : MoveKind::SideWalkRight);
            }

            if (!cancel) {
                if (MoveFlags(before.move) & MoveCrouching)
                    crouching[player] = true;
                AdvanceMove(after);
                continue;
            }

            ClearMove(after);
        }

        crouching[player] = input.vertical < 0;
        const bool no_guard = (input.buttons & NoGuard) != 0;
        const auto chord_edges =
            static_cast<std::uint8_t>(input.buttons & ~before.previous_buttons & AllAttackButtons);
        const auto* chord_entry =
            guard_requested || (command && command->direction_count > 1)
                ? nullptr
                : ChordEntry(after, chord_edges, input.buttons & AllAttackButtons,
                             static_cast<std::uint8_t>(5 + input.horizontal * facing + input.vertical * 3));

        if (chord_entry) {
            StartMove(after, chord_entry->move);
            ClearHistory(after);
            after.pending_command = CommandId::None;
            after.pending_frames = 0;
            AdvanceMove(after);
        } else if (command && !guard_requested && SystemAllowed(after, command->id) &&
                   (IsRunning(before) || RunCommandAllowed(after, opponent, *command))) {
            const auto system = GetAttackDefinition(after.character, command->id)->system_action;

            if (system == SystemAction::HeatBurst) {
                after.heat_used = 1;
                after.heat_frames = HeatBurstDurationFrames;
            }

            if (system == SystemAction::HeatSmash)
                after.heat_frames = 0;

            if (system == SystemAction::RageArt)
                after.rage_spent = 1;
            after.attack_frame = 1;
            after.attack_connected = 0;
            after.active_attack = command->fallback_attack;
            after.active_command = command->id;
            after.pending_command = CommandId::None;
            after.pending_frames = 0;
        } else if (!guard_requested && input.vertical > 0) {
            StartMove(after, MoveKind::PreJump);
            ClearHistory(after);
            AdvanceMove(after);
        } else if (!guard_requested && input.vertical == 0 && input.horizontal == 0 &&
                   before.previous_vertical < 0 && DownTap(after)) {
            StartMove(after, viewed_facing > 0 ? MoveKind::StepRight : MoveKind::StepLeft);
            ClearHistory(after);
            AdvanceMove(after);
        } else if (const auto* entry = guard_requested
                                           ? nullptr
                                           : StepEntry(after, static_cast<std::uint8_t>(
                                                                  5 + before.previous_horizontal * facing +
                                                                  before.previous_vertical * 3));
                   entry != nullptr) {
            StartMove(after, entry->move);

            if (!entry->keep_history)
                ClearHistory(after);
            AdvanceMove(after);
        } else if (!guard_requested && relative != 0 &&
                   (before.previous_horizontal * facing != relative ||
                    (recovered && relative > 0 && FinalRun(after) <= DashBufferFrames)) &&
                   TapTap(after, static_cast<std::uint8_t>(relative > 0 ? 6 : 4))) {
            StartMove(after, relative > 0 ? MoveKind::DashForward : MoveKind::DashBack);

            AdvanceMove(after);
        } else if (!guard_requested && relative > 0 && input.vertical < 0) {
            StartMove(after, before.previous_vertical < 0 ? MoveKind::CrouchWalk : MoveKind::CrouchWalkStart);
            AdvanceMove(after);
        } else {
            if (!guard_requested && relative != 0 && input.vertical == 0)
                Translate(after,
                          Scale(relative > 0 ? WalkForwardPerFrame : -WalkBackPerFrame, FaceOf(after)));
            guarding[player] = guard_requested || (!no_guard && relative <= 0);
        }
    }

    if (trace) {
        trace->regular_step = true;

        for (std::size_t i = 0; i < 2; ++i) {
            auto& t = trace->fighters[i];
            t.action = {next.fighters[i].position_x - state.fighters[i].position_x - t.drift.x - t.chase.x,
                        next.fighters[i].position_z - state.fighters[i].position_z - t.drift.z - t.chase.z};
        }
    }

    for (auto& fighter : next.fighters) {
        if (fighter.pushback == NoPushback)
            continue;
        const auto push_before_x = fighter.position_x, push_before_z = fighter.position_z;
        ++fighter.pushback_frame;
        auto displacement =
            static_cast<std::int64_t>(PushbackDisplacement(fighter.pushback, fighter.pushback_frame));

        if (fighter.combo_hits != 0 && IsAirborne(fighter))
            displacement = displacement * AirCarry(fighter) / 100;
        Translate(fighter, Scale(displacement, {fighter.push_cos, fighter.push_sin}));

        if (trace)
            trace->fighters[&fighter - next.fighters.data()].pushback = {fighter.position_x - push_before_x,
                                                                         fighter.position_z - push_before_z};

        if (fighter.pushback_frame == PushbackFrames(fighter.pushback))
            ClearPushback(fighter);
    }

    const auto before_separation = next.fighters;
    SeparateBodies(next, view);

    if (trace)
        for (std::size_t i = 0; i < 2; ++i)
            trace->fighters[i].separation = {next.fighters[i].position_x - before_separation[i].position_x,
                                             next.fighters[i].position_z - before_separation[i].position_z};

    std::array<const AttackDefinition*, 2> hits{};

    for (std::size_t player = 0; player < 2; ++player) {
        const auto& self = next.fighters[player];
        const auto& other = next.fighters[1 - player];

        if (other.combo_hits >= MaximumComboHits)
            continue;
#if defined(MF_FULL_MOVESET)

        if (self.character == CharacterId::Paul &&
            self.active_command == CommandId::PaulMotionddffp1Plhizilu && IsAirborne(other) &&
            PaulQcfAirHits(other) >= MaximumPaulQcfAirHits)
            continue;
#endif

        const auto* attack =
            GetContactAttackDefinition(self.character, self.active_command, self.heat_frames != 0);

        const auto face = FaceOf(self), left = LeftOf(face), other_face = FaceOf(other);
        const Vec d{static_cast<std::int64_t>(other.position_x) - self.position_x,
                    static_cast<std::int64_t>(other.position_z) - self.position_z};
        const auto forward = Dot(d, face) / UnitQ16, side = Dot(d, left) / UnitQ16;
        const auto fc = Dot(other_face, face) / UnitQ16, fs = Dot(other_face, left) / UnitQ16;
        const std::uint8_t pose = GroundHitPose(other);
        const bool down_outcome =
            pose != 0 && attack && attack->reactions &&
            attack->reactions[pose == 1 ? HitDownFaceUp : HitDownFaceDown].reaction != ReactionKind::None;
        std::array<std::int32_t, 3> contact_local{};
        const bool reaches =
            attack != nullptr && (pose == 0 || down_outcome) &&
            (attack->source_data
                 ? SourceHitContact(
                       self.character, self.active_command, self.attack_frame, other.character,
                       static_cast<std::int32_t>(std::clamp<std::int64_t>(
                           forward, -MaximumGeometryDistance - 1, MaximumGeometryDistance + 1)),
                       static_cast<std::int32_t>(std::clamp<std::int64_t>(side, -MaximumGeometryDistance - 1,
                                                                          MaximumGeometryDistance + 1)),
                       MoveRise(other.move, other.move_frame) + AirPresentationOffset(other),
                       static_cast<std::int32_t>(std::clamp<std::int64_t>(fc, -UnitQ16, UnitQ16)),
                       static_cast<std::int32_t>(std::clamp<std::int64_t>(fs, -UnitQ16, UnitQ16)), pose,
                       other.reaction, other.reaction_frame, IsAirborne(other),
                       trace ? &contact_local : nullptr)
                 : forward >= -BodySeparation && forward <= attack->range && side >= -SyntheticHitHalfWidth &&
                       side <= SyntheticHitHalfWidth);

        bool pickup = false;

        if (!reaches && down_outcome)
            for (const auto& rule : AuthoredGroundPickups)
                if (rule.character == self.character && rule.command == self.active_command && forward >= 0 &&
                    forward <= rule.reach_mm && side >= -rule.half_width_mm && side <= rule.half_width_mm)
                    pickup = true;

        if ((reaches || pickup) && next.fighters[player].hp != 0 && next.fighters[1 - player].hp != 0 &&

            (next.fighters[1 - player].reaction == ReactionKind::None ||
             IsAirborne(next.fighters[1 - player]) || StandingStagger(next.fighters[1 - player]) ||
             down_outcome) &&
            !next.fighters[player].attack_connected &&
            next.fighters[player].attack_frame >= attack->active_frame &&
            next.fighters[player].attack_frame <= attack->active_last_frame &&
            !(attack->hit_level == HitLevel::High && crouching[1 - player]) &&
            !(attack->system_action == SystemAction::Throw &&
              (IsAirborne(other) || other.reaction != ReactionKind::None || IsJump(other.move)))) {
            hits[player] = attack;

            if (trace && reaches && attack->source_data) {
                const auto f = Scale(contact_local[0], face), l = Scale(contact_local[1], left);
                auto& out = trace->fighters[player];
                out.has_contact = true;
                out.contact_world = {static_cast<std::int32_t>(self.position_x + f.x + l.x),
                                     static_cast<std::int32_t>(self.position_z + f.z + l.z),
                                     contact_local[2]};
            }
        }
    }

    for (std::size_t player = 0; player < 2; ++player)
        if (hits[player])
            next.fighters[player].attack_connected = 1;

    for (std::size_t attacker = 0; attacker < 2; ++attacker) {
        if (!hits[attacker]) {
            continue;
        }

        const auto defender = 1 - attacker;
        auto& target = next.fighters[defender];
        target.combo_hits = static_cast<std::uint8_t>(std::min<int>(target.combo_hits + 1, 255));
        const auto hit_direction =
            5 + inputs[attacker].horizontal * next.fighters[attacker].facing + inputs[attacker].vertical * 3;

        for (const auto& rule : AuthoredThrowVariants)
            if (rule.character == next.fighters[attacker].character &&
                rule.from == next.fighters[attacker].active_command && hit_direction == rule.direction) {
                hits[attacker] = GetAttackDefinition(rule.character, rule.to);
                next.fighters[attacker].active_command = rule.to;
                break;
            }
        const auto* system = hits[attacker];
        const auto impact_command = next.fighters[attacker].active_command;
        bool heat_dashed = false;
        const bool system_blocked = guarding[defender] && system->system_action != SystemAction::Throw &&
                                    !(system->hit_level == HitLevel::Mid && crouching[defender]) &&
                                    !(system->hit_level == HitLevel::Low && !crouching[defender]);

        if (HeatEngager(system)) {
            auto& source = next.fighters[attacker];

            const bool sequence_hit =
                system->finish_frames && !system_blocked && !hits[defender] &&
                (!IsAirborne(target) && GroundHitPose(target) == 0 && !crouching[defender] &&
                 HitSituation(next.fighters[attacker], target, false, false, false) == HitFront);

            if (!source.heat_used && !system_blocked && !IsAirborne(target) && GroundHitPose(target) == 0 &&
                !sequence_hit) {
                source.heat_used = 1;
                source.heat_frames = HeatDurationFrames;
            } else if (state.fighters[attacker].heat_frames &&
                       inputs[attacker].horizontal * source.facing > 0 && !hits[defender]) {
                source.heat_frames = 0;
                source.attack_frame = 0;
                source.active_attack = 0;
                source.active_command = CommandId::None;
                source.attack_connected = 0;
                source.queued_command = CommandId::None;
                source.pending_command = CommandId::None;
                source.pending_frames = 0;
                ClearHistory(source);
                StartMove(source, MoveKind::HeatDash);

                AdvanceMove(source);
                heat_dashed = true;
            }
        }

        const bool catch_front =
            system->catch_on_any_hit || system->system_action != SystemAction::None ||
            (!IsAirborne(target) && GroundHitPose(target) == 0 && !crouching[defender] &&
             HitSituation(next.fighters[attacker], target, false, false, false) == HitFront);

        if (system->finish_frames && !system_blocked && !hits[defender] && !heat_dashed && catch_front) {
            auto& source = next.fighters[attacker];
            const auto command = impact_command;
            const auto owner = source.character;
            const auto face = FaceOf(source);

            const auto* root = SequenceRoot(owner, command, 1);
            const auto forward =
                Scale(root ? root->victim_forward - root->attacker_forward : BodySeparation, face);
            const auto left = Scale(root ? root->victim_left - root->attacker_left : 0, LeftOf(face));
            target.position_x = source.position_x;
            target.position_z = source.position_z;
            Translate(target, {forward.x + left.x, forward.z + left.z});
            target.face_cos = source.face_cos;
            target.face_sin = source.face_sin;

            for (std::size_t side = 0; side < 2; ++side) {
                auto& fighter = next.fighters[side];
                fighter.sequence_command = command;
                fighter.sequence_owner = owner;
                fighter.sequence_frame = 1;
                fighter.sequence_role = side == attacker ? 1 : 2;
                fighter.attack_frame = 0;
                fighter.active_attack = 0;
                fighter.active_command = CommandId::None;
                fighter.attack_connected = 0;
                fighter.pending_command = CommandId::None;
                fighter.pending_frames = 0;
                fighter.queued_command = CommandId::None;
                fighter.stun_frames = 0;
                fighter.reaction = ReactionKind::None;
                fighter.reaction_frame = 0;
                fighter.air_phase = 0;
                fighter.air_height = 0;
                fighter.air_velocity = 0;
                fighter.crouch_frames = 0;
                fighter.rise_frame = 0;
                ClearHistory(fighter);
                ClearMove(fighter);
                ClearPushback(fighter);
            }

            break;
        }

        if (const auto pose = GroundHitPose(target); pose != 0) {
            const auto& down = hits[attacker]->reactions[pose == 2 ? HitDownFaceDown : HitDownFaceUp];
            target.hp = std::max(0, target.hp - hits[attacker]->damage);
            target.air_phase = 0;
            target.air_height = target.air_velocity = 0;
            ClearPushback(target);
            target.pushback = down.pushback;

            if (target.pushback != NoPushback) {
                const auto& source = next.fighters[attacker];
                const Vec away{static_cast<std::int64_t>(target.position_x) - source.position_x,
                               static_cast<std::int64_t>(target.position_z) - source.position_z};
                const auto unit = UnitOf(away, FaceOf(source));
                target.push_cos = static_cast<std::int32_t>(unit.x);
                target.push_sin = static_cast<std::int32_t>(unit.z);
            }

            target.reaction = down.reaction;
            target.reaction_frame = 1;
            continue;
        }

        if (IsAirborne(target)) {
#if defined(MF_FULL_MOVESET)
            if (next.fighters[attacker].character == CharacterId::Paul &&
                impact_command == CommandId::PaulMotionddffp1Plhizilu)
                target.combo_flags = static_cast<std::uint8_t>(target.combo_flags + (1u << 1));
#endif
            const auto previous_air_reaction = target.reaction;
            const auto previous_air_frame = target.reaction_frame;
            const bool source_height_known =
                HasReactionPose(target.character, target.reaction, target.reaction_frame);
            const auto contact_height =
                ReactionBodyHeight(target.character, target.reaction, target.reaction_frame) +
                AirPresentationOffset(target);

            const bool screw = hits[attacker]->screw && (target.combo_flags & ComboScrewed) == 0;

            if (screw)
                target.combo_flags = static_cast<std::uint8_t>(target.combo_flags | ComboScrewed);
            const auto* air = screw ? hits[attacker]->screw : hits[attacker]->air;
            target.air_phase = 0;
            target.air_height = target.air_velocity = 0;
            const auto base_damage = air ? air->damage : hits[attacker]->damage;

            const int percent = target.combo_hits == 2   ? 70
                                : target.combo_hits == 3 ? 50
                                : target.combo_hits == 4 ? 40
                                : target.combo_hits >= 5 ? 30
                                                         : 100;
            const auto damage =
                hits[attacker]->source_data
                    ? static_cast<std::int32_t>((static_cast<std::int64_t>(base_damage) * percent) / 100)
                    : base_damage;
            target.hp = std::max(hits[attacker]->system_action == SystemAction::HeatBurst ? 1 : 0,
                                 target.hp - damage);

            if (air) {
                const auto situation = HitSituation(next.fighters[attacker], target, false, false, false);
                const std::size_t side = situation >= HitBack    ? 3
                                         : situation >= HitRight ? 2
                                         : situation >= HitLeft  ? 1
                                                                 : 0;
                ClearPushback(target);
                target.pushback = air->pushbacks[side];

                if (target.pushback != NoPushback) {
                    const auto& source = next.fighters[attacker];
                    const Vec away{static_cast<std::int64_t>(target.position_x) - source.position_x,
                                   static_cast<std::int64_t>(target.position_z) - source.position_z};
                    const auto unit = UnitOf(away, FaceOf(source));
                    target.push_cos = static_cast<std::int32_t>(unit.x);
                    target.push_sin = static_cast<std::int32_t>(unit.z);
                }

                if (air->reactions[side] != ReactionKind::None) {
                    target.reaction = air->reactions[side];
                    target.reaction_frame = 1;
                }
            }

            {
                const auto& source = next.fighters[attacker];
                const Vec toward{static_cast<std::int64_t>(source.position_x) - target.position_x,
                                 static_cast<std::int64_t>(source.position_z) - target.position_z};
                const auto face = UnitOf(toward, FaceOf(target));
                target.face_cos = static_cast<std::int32_t>(face.x);
                target.face_sin = static_cast<std::int32_t>(face.z);
            }

            bool capped = false;

            for (const auto& rule : AuthoredAirMotions)
                if ((air ? target.reaction : ReactionKind::AirPng) == rule.reaction &&
                    rule.max_contact_mm != 0 && contact_height > rule.max_contact_mm)
                    capped = true;

            for (const auto& rule : AuthoredAirLifts)
                if (IsAirborne(state.fighters[defender]) &&
                    rule.character == next.fighters[attacker].character &&
                    rule.command == next.fighters[attacker].active_command)
                    capped = false;

            if (hits[attacker]->source_data && source_height_known && !capped &&
                (!air || AirLandingFrame(target.reaction) != 0)) {
                if (!air)
                    target.reaction = ReactionKind::AirPng;
                target.reaction_frame = 1;
                target.air_phase = 1;
                target.air_height = contact_height;

                int solve_frames = GenericAirSolveFrames;
                const int loss = juggle_policy::HitSlope * std::max(0, int(target.combo_hits) - 2);

                for (const auto& rule : AuthoredAirMotions)
                    if (target.reaction == rule.reaction) {
                        solve_frames = rule.solve_frames;
                    }
                bool lifted = false;

                for (const auto& rule : AuthoredAirLifts)
                    if (IsAirborne(state.fighters[defender]) &&
                        rule.character == next.fighters[attacker].character &&
                        rule.command == next.fighters[attacker].active_command) {
                        solve_frames = rule.solve_frames;
                        lifted = true;
                    }
                target.air_velocity =
                    std::max(-1000, SolveAirVelocity(contact_height,
                                                     ReactionBodyHeight(target.character, target.reaction,
                                                                        AirLandingFrame(target.reaction)),
                                                     solve_frames) -
                                        loss);
                const int landing_frame = AirLandingFrame(target.reaction);
                const int landing_height =
                    ReactionBodyHeight(target.character, target.reaction, landing_frame);
                const int flight_ticks = juggle_policy::LandingTicks(contact_height, target.air_velocity,
                                                                     landing_height, AirGravity);
                const auto entry = juggle_policy::SelectEntry(landing_frame, flight_ticks, previous_air_frame,
                                                              previous_air_reaction == target.reaction);

                if (HasReactionPose(target.character, target.reaction,
                                    static_cast<std::uint16_t>(entry.frame))) {
                    target.reaction_frame = static_cast<std::uint16_t>(entry.frame);

                    if (entry.ticks != flight_ticks)
                        target.air_velocity = SolveAirVelocity(contact_height, landing_height, entry.ticks);
                }

                target.air_drift_x = target.air_drift_z = 0;

                const int air_hit = int(target.combo_hits) - 1;
                std::int64_t speed = 0;

                if (!lifted && target.combo_hits >= 2 && air_hit > AirCarryFlatHits)
                    speed = static_cast<std::int64_t>(AirDriftBase) * AirCarry(target) / 100;
                else if (target.combo_hits >= 2)
                    speed = juggle_policy::Drift(target.combo_hits);

                if (speed > 0) {
                    const auto& source = next.fighters[attacker];
                    const Vec away{static_cast<std::int64_t>(target.position_x) - source.position_x,
                                   static_cast<std::int64_t>(target.position_z) - source.position_z};
                    const auto unit = UnitOf(away, FaceOf(source));
                    target.air_drift_x = static_cast<std::int16_t>(
                        std::clamp<std::int64_t>(unit.x * speed / UnitQ16, -MaxAirDrift, MaxAirDrift));
                    target.air_drift_z = static_cast<std::int16_t>(
                        std::clamp<std::int64_t>(unit.z * speed / UnitQ16, -MaxAirDrift, MaxAirDrift));
                }
            } else if (hits[attacker]->source_data && source_height_known &&
                       target.reaction_frame < ClipAirLandingFrame(target.reaction) &&
                       HasReactionPose(target.character, target.reaction, target.reaction_frame) &&
                       HasReactionPose(target.character, target.reaction,
                                       ClipAirLandingFrame(target.reaction))) {
                target.air_phase = 3;
                target.air_height = std::clamp(contact_height, 0, 10000);
            }

            continue;
        }

        target.attack_frame = 0;
        target.active_attack = 0;
        target.active_command = CommandId::None;
        target.attack_connected = 0;
        target.queued_command = CommandId::None;
        ClearMove(target);

        const bool blocked = guarding[defender] && hits[attacker]->system_action != SystemAction::Throw &&
                             hits[attacker]->hit_level != HitLevel::Unblockable &&
                             !(hits[attacker]->hit_level == HitLevel::Mid && crouching[defender]) &&
                             !(hits[attacker]->hit_level == HitLevel::Low && !crouching[defender]);

        const auto situation =
            HitSituation(next.fighters[attacker], target, crouching[defender], blocked,
                         ResolveCounterHit(state.fighters[defender], inputs[defender].buttons));
        const HitReactionDefinition* outcome =
            hits[attacker]->reactions ? &hits[attacker]->reactions[situation] : nullptr;

        if (!blocked && outcome && situation <= HitCounterCrouch) {
            const auto& source = next.fighters[attacker];
            const std::int64_t dx = std::int64_t(target.position_x) - source.position_x;
            const std::int64_t dz = std::int64_t(target.position_z) - source.position_z;

            for (const auto& rule : AuthoredCleanHits)
                if (source.character == rule.character && source.active_command == rule.command &&
                    dx * dx + dz * dz <= std::int64_t(rule.distance_mm) * rule.distance_mm) {
                    outcome = &hits[attacker]->reactions[crouching[defender] ? HitCounterCrouch : HitCounter];
                    break;
                }
        }

        target.standing_motion = StandingMotionFor(outcome);
        target.standing_frame = 0;
        ClearPushback(target);
        target.pushback = outcome   ? outcome->pushback
                          : blocked ? hits[attacker]->block_pushback
                                    : hits[attacker]->hit_pushback;

        if (target.pushback != NoPushback) {
            const auto& source = next.fighters[attacker];
            const Vec away{static_cast<std::int64_t>(target.position_x) - source.position_x,
                           static_cast<std::int64_t>(target.position_z) - source.position_z};
            const auto face = FaceOf(source);
            const auto unit = UnitOf(away, face);
            target.push_cos = static_cast<std::int32_t>(unit.x);
            target.push_sin = static_cast<std::int32_t>(unit.z);
        }

        if (blocked) {
            target.combo_hits = 0;
            target.combo_flags = 0;
            target.stun_frames =
                std::max(target.stun_frames, outcome ? outcome->stun : hits[attacker]->guard_stun_frames);
        } else {
            target.hp = std::max(hits[attacker]->system_action == SystemAction::HeatBurst ? 1 : 0,
                                 target.hp - hits[attacker]->damage);
            auto fall = outcome ? outcome->reaction : static_cast<ReactionKind>(hits[attacker]->hit_reaction);
            target.stun_frames = target.hp == 0 || fall != ReactionKind::None ? 0
                                 : outcome                                    ? outcome->stun
                                           : hits[attacker]->hit_stun_frames;

            if (target.hp != 0 && (situation == HitCounter || situation == HitCounterCrouch)) {
                for (const auto& rule : AuthoredCounterStuns) {
                    if (rule.character == next.fighters[attacker].character &&
                        rule.command == next.fighters[attacker].active_command) {
                        fall = ReactionKind::None;
                        target.stun_frames = rule.frames;
                        break;
                    }
                }
            }

            if (target.hp != 0 && state.fighters[defender].stun_frames != 0) {
                for (const auto& rule : AuthoredComboLaunches) {
                    if (rule.character == next.fighters[attacker].character &&
                        rule.command == next.fighters[attacker].active_command &&
                        target.combo_hits >= rule.minimum_hits) {
                        fall = rule.reaction;
                        target.stun_frames = 0;
                        target.air_phase = 1;
                        target.air_height = rule.initial_height;
                        target.air_velocity = SolveAirVelocity(
                            target.air_height,
                            ReactionBodyHeight(target.character, fall, AirLandingFrame(fall)),
                            rule.solve_frames);
                        break;
                    }
                }
            }

            if (fall != ReactionKind::None) {
                target.reaction = fall;
                target.reaction_frame = 1;
                target.pending_command = CommandId::None;
                target.pending_frames = 0;
                ClearHistory(target);
            } else if (target.reaction != ReactionKind::None) {
                target.reaction = ReactionKind::None;
                target.reaction_frame = 0;
            }

            if (target.hp == 0) {
                target.pending_command = CommandId::None;
                target.pending_frames = 0;
                ClearHistory(target);
            }
        }
    }

    for (std::size_t player = 0; player < 2; ++player) {
        const auto& before = state.fighters[player];
        auto& after = next.fighters[player];
        std::uint8_t crouch = 0, rise = 0;

        if (after.hp != 0 && after.reaction == ReactionKind::None && !after.sequence_role) {
            const auto* ended_reaction = GetReactionDefinition(before.reaction);

            if (ended_reaction && (ended_reaction->flags & ReactionEndsCrouched) != 0) {
                crouch = FullCrouchFrames;
            } else if (after.attack_frame == 0 && before.attack_frame != 0) {
                const auto* ended = GetAttackDefinition(before.character, before.active_command);
                crouch = ended && ended->ends_crouched && before.attack_frame == ended->total_frames
                             ? FullCrouchFrames
                             : 0;
            } else if (after.attack_frame == 0) {
                if (inputs[player].vertical < 0 && !IsJump(after.move))
                    crouch =
                        static_cast<std::uint8_t>(std::min<int>(before.crouch_frames + 1, FullCrouchFrames));
                else if (before.crouch_frames >= FullCrouchFrames)
                    rise = 1;
                else if (before.rise_frame != 0 && before.rise_frame < RiseFrames)
                    rise = static_cast<std::uint8_t>(before.rise_frame + 1);
            }
        }

        if (after.air_phase != 3 && AirLandingFrame(after.reaction) == 0) {
            after.air_phase = 0;
            after.air_height = after.air_velocity = 0;
        }

        after.crouch_frames = crouch;
        after.rise_frame = rise;

        if (!after.stun_frames || after.reaction != ReactionKind::None || after.sequence_role ||
            after.hp == 0)
            after.standing_motion = after.standing_frame = 0;

        const auto* final_reaction = GetReactionDefinition(after.reaction);
        const bool capped_and_settled = after.combo_hits >= MaximumComboHits && final_reaction &&
                                        (final_reaction->flags & ReactionLying) != 0;

        if (!after.sequence_role &&
            ((after.reaction == ReactionKind::None && after.stun_frames == 0) || capped_and_settled)) {
            after.combo_hits = 0;
            after.combo_flags = 0;
        }
    }

    if (next.hitstop_enabled)
        for (std::size_t player = 0; player < 2; ++player)
            if (hits[player]) {
                const bool damaged = next.fighters[1 - player].hp < state.fighters[1 - player].hp;

                const std::uint8_t stop = !damaged ? 1 : hits[player]->damage >= 20 ? 2 : 1;
                next.hitstop_frames = std::max(next.hitstop_frames, stop);
            }
    for (auto& fighter : next.fighters)
        if (fighter.air_phase != 1)
            fighter.air_drift_x = fighter.air_drift_z = 0;
    ++next.frame;
    state = next;

    return true;
}

// 상태를 정규 바이트열로 저장한다.
std::vector<std::uint8_t> Serialize(const State& state) {
    if (!IsValid(state)) {
        throw std::invalid_argument("Cannot serialize invalid combat state");
    }

    std::vector<std::uint8_t> bytes{'M', 'F', 'C', 'S'};
    bytes.reserve(SnapshotBytes);
    Write16(bytes, SnapshotVersion);
    Write16(bytes, RulesVersion);
    Write32(bytes, state.frame);

    for (const auto& fighter : state.fighters) {
        Write32(bytes, static_cast<std::uint32_t>(fighter.position_x));
        Write32(bytes, static_cast<std::uint32_t>(fighter.position_z));
        Write32(bytes, static_cast<std::uint32_t>(fighter.hp));
        Write16(bytes, fighter.attack_frame);
        Write16(bytes, fighter.stun_frames);
        bytes.push_back(fighter.previous_buttons);
        bytes.push_back(fighter.active_attack);
        bytes.push_back(static_cast<std::uint8_t>(fighter.previous_horizontal));
        bytes.push_back(static_cast<std::uint8_t>(fighter.previous_vertical));
        bytes.push_back(static_cast<std::uint8_t>(fighter.character));
        bytes.push_back(static_cast<std::uint8_t>(fighter.facing));
        Write16(bytes, static_cast<std::uint16_t>(fighter.active_command));
        Write16(bytes, static_cast<std::uint16_t>(fighter.pending_command));
        bytes.push_back(fighter.pending_frames);
        bytes.push_back(fighter.direction_count);
        bytes.insert(bytes.end(), fighter.direction_history.begin(), fighter.direction_history.end());
        bytes.push_back(fighter.attack_connected);
        bytes.push_back(static_cast<std::uint8_t>(fighter.reaction));
        Write16(bytes, fighter.reaction_frame);
        bytes.push_back(fighter.pushback);
        bytes.push_back(fighter.pushback_frame);
        Write32(bytes, static_cast<std::uint32_t>(fighter.push_cos));
        Write32(bytes, static_cast<std::uint32_t>(fighter.push_sin));
        bytes.push_back(static_cast<std::uint8_t>(fighter.move));
        bytes.push_back(fighter.move_frame);
        Write32(bytes, static_cast<std::uint32_t>(fighter.face_cos));
        Write32(bytes, static_cast<std::uint32_t>(fighter.face_sin));
        Write16(bytes, static_cast<std::uint16_t>(fighter.queued_command));
        bytes.push_back(fighter.crouch_frames);
        bytes.push_back(fighter.rise_frame);
        bytes.push_back(fighter.combo_hits);
        bytes.push_back(fighter.combo_flags);
        Write32(bytes, static_cast<std::uint32_t>(fighter.air_height));
        Write32(bytes, static_cast<std::uint32_t>(fighter.air_velocity));
        Write16(bytes, static_cast<std::uint16_t>(fighter.air_drift_x));
        Write16(bytes, static_cast<std::uint16_t>(fighter.air_drift_z));
        bytes.push_back(fighter.air_phase);
        bytes.push_back(fighter.training_rage);
        Write16(bytes, fighter.heat_frames);
        bytes.push_back(fighter.heat_used);
        bytes.push_back(fighter.rage_spent);
        Write16(bytes, static_cast<std::uint16_t>(fighter.sequence_command));
        bytes.push_back(static_cast<std::uint8_t>(fighter.sequence_owner));
        Write16(bytes, fighter.sequence_frame);
        bytes.push_back(fighter.sequence_role);
        Write16(bytes, fighter.standing_motion);
        Write16(bytes, fighter.standing_frame);
        bytes.push_back(fighter.sequence_subframe);
    }

    Write32(bytes, static_cast<std::uint32_t>(state.view_cos));
    Write32(bytes, static_cast<std::uint32_t>(state.view_sin));
    bytes.push_back(state.hitstop_enabled);
    bytes.push_back(state.hitstop_frames);

    const std::uint64_t fingerprint = RulesFingerprint();
    Write32(bytes, static_cast<std::uint32_t>(fingerprint));
    Write32(bytes, static_cast<std::uint32_t>(fingerprint >> 32));

    return bytes;
}

// 바이트열을 검증하고 상태로 복원한다.
bool Deserialize(const std::vector<std::uint8_t>& bytes, State& destination) noexcept {
    if (bytes.size() != SnapshotBytes || bytes[0] != 'M' || bytes[1] != 'F' || bytes[2] != 'C' ||
        bytes[3] != 'S') {
        return false;
    }

    std::size_t cursor = 4;

    if (Read16(bytes, cursor) != SnapshotVersion || Read16(bytes, cursor) != RulesVersion) {
        return false;
    }

    State decoded;
    decoded.frame = Read32(bytes, cursor);

    for (auto& fighter : decoded.fighters) {
        fighter.position_x = ReadSigned32(bytes, cursor);
        fighter.position_z = ReadSigned32(bytes, cursor);
        fighter.hp = ReadSigned32(bytes, cursor);
        fighter.attack_frame = Read16(bytes, cursor);
        fighter.stun_frames = Read16(bytes, cursor);
        fighter.previous_buttons = bytes[cursor++];
        fighter.active_attack = bytes[cursor++];
        fighter.previous_horizontal = ReadSigned8(bytes, cursor);
        fighter.previous_vertical = ReadSigned8(bytes, cursor);
        fighter.character = static_cast<CharacterId>(bytes[cursor++]);
        fighter.facing = ReadSigned8(bytes, cursor);
        fighter.active_command = static_cast<CommandId>(Read16(bytes, cursor));
        fighter.pending_command = static_cast<CommandId>(Read16(bytes, cursor));
        fighter.pending_frames = bytes[cursor++];
        fighter.direction_count = bytes[cursor++];

        for (auto& direction : fighter.direction_history)
            direction = bytes[cursor++];
        fighter.attack_connected = bytes[cursor++];
        fighter.reaction = static_cast<ReactionKind>(bytes[cursor++]);
        fighter.reaction_frame = Read16(bytes, cursor);
        fighter.pushback = bytes[cursor++];
        fighter.pushback_frame = bytes[cursor++];
        fighter.push_cos = ReadSigned32(bytes, cursor);
        fighter.push_sin = ReadSigned32(bytes, cursor);
        fighter.move = static_cast<MoveKind>(bytes[cursor++]);
        fighter.move_frame = bytes[cursor++];
        fighter.face_cos = ReadSigned32(bytes, cursor);
        fighter.face_sin = ReadSigned32(bytes, cursor);
        fighter.queued_command = static_cast<CommandId>(Read16(bytes, cursor));
        fighter.crouch_frames = bytes[cursor++];
        fighter.rise_frame = bytes[cursor++];
        fighter.combo_hits = bytes[cursor++];
        fighter.combo_flags = bytes[cursor++];
        fighter.air_height = ReadSigned32(bytes, cursor);
        fighter.air_velocity = ReadSigned32(bytes, cursor);

        for (auto* drift : {&fighter.air_drift_x, &fighter.air_drift_z}) {
            const auto raw = Read16(bytes, cursor);
            *drift = static_cast<std::int16_t>(raw <= 32767 ? static_cast<int>(raw)
                                                            : static_cast<int>(raw) - 65536);
        }

        fighter.air_phase = bytes[cursor++];
        fighter.training_rage = bytes[cursor++];
        fighter.heat_frames = Read16(bytes, cursor);
        fighter.heat_used = bytes[cursor++];
        fighter.rage_spent = bytes[cursor++];
        fighter.sequence_command = static_cast<CommandId>(Read16(bytes, cursor));
        fighter.sequence_owner = static_cast<CharacterId>(bytes[cursor++]);
        fighter.sequence_frame = Read16(bytes, cursor);
        fighter.sequence_role = bytes[cursor++];
        fighter.standing_motion = Read16(bytes, cursor);
        fighter.standing_frame = Read16(bytes, cursor);
        fighter.sequence_subframe = bytes[cursor++];
    }

    decoded.view_cos = ReadSigned32(bytes, cursor);
    decoded.view_sin = ReadSigned32(bytes, cursor);
    decoded.hitstop_enabled = bytes[cursor++];
    decoded.hitstop_frames = bytes[cursor++];
    const std::uint64_t low = Read32(bytes, cursor);
    const std::uint64_t fingerprint = low | (static_cast<std::uint64_t>(Read32(bytes, cursor)) << 32);

    if (fingerprint != RulesFingerprint() || !IsValid(decoded)) {
        return false;
    }

    destination = decoded;

    return true;
}

// 정규 바이트열로 상태 해시를 계산한다.
std::uint64_t HashState(const State& state) {
    const auto bytes = Serialize(state);
    std::uint64_t hash = 14695981039346656037ULL;

    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }

    return hash;
}

FighterGeometry DescribeFighterGeometry(const State& state, std::size_t index) noexcept {
    FighterGeometry out;

    if (index > 1)
        return out;
    const auto& self = state.fighters[index];
    const auto face = FaceOf(self), left = LeftOf(face);
    const auto world = [&](std::int64_t forward, std::int64_t side, std::int32_t& x, std::int32_t& z) {
        x = static_cast<std::int32_t>(self.position_x + (forward * face.x + side * left.x) / UnitQ16);
        z = static_cast<std::int32_t>(self.position_z + (forward * face.z + side * left.z) / UnitQ16);
    };
    AirPose ground_pose{};

    if (const auto* spheres = SelectHurtSpheres(self.character, GroundHitPose(self), self.reaction,
                                                self.reaction_frame, ground_pose)) {
        const auto rise = MoveRise(self.move, self.move_frame) + AirPresentationOffset(self);

        for (const auto& sphere : *spheres) {
            if (sphere.radius <= 0)
                continue;
            auto& d = out.hurt[out.hurt_count++];
            world(sphere.centre[0], sphere.centre[1], d.x, d.z);
            d.up = sphere.centre[2] + rise;
            d.radius = sphere.radius;
        }
    }

    const auto* attack = GetAttackDefinition(self.character, self.active_command);

    if (!attack || !attack->source_data || self.attack_frame == 0)
        return out;
    std::int32_t hit_radius = 0, air_down = 0;

    for (const auto& shape : AuthoredHitShapes)
        if (shape.command == self.active_command && shape.character == self.character) {
            hit_radius = shape.radius_mm;

            if (IsAirborne(state.fighters[1 - index]))
                air_down = shape.air_down_mm;
            else if (GroundHitPose(state.fighters[1 - index]) != 0)
                air_down = shape.ground_down_mm;
        }
    for (const auto& frame : HitFrames) {
        if (frame.command != self.active_command || frame.frame != self.attack_frame ||
            frame.owner != self.character)
            continue;

        for (std::size_t s = frame.first;
             s < static_cast<std::size_t>(frame.first) + frame.count && out.hit_count < out.hit.size(); ++s) {
            const auto& seg = HitSegments[s];
            auto& d = out.hit[out.hit_count++];
            world(seg.a[0], seg.a[1], d.ax, d.az);
            world(seg.b[0], seg.b[1], d.bx, d.bz);
            d.aup = seg.a[2];
            d.bup = seg.b[2];
            d.radius = hit_radius;

            if (air_down > 0)
                for (const auto& endpoint : {seg.a, seg.b}) {
                    if (out.hit_count >= out.hit.size())
                        break;
                    auto& lower = out.hit[out.hit_count++];
                    world(endpoint[0], endpoint[1], lower.ax, lower.az);
                    lower.bx = lower.ax;
                    lower.bz = lower.az;
                    lower.aup = endpoint[2];
                    lower.bup = endpoint[2] - air_down;
                    lower.radius = hit_radius;
                }
        }
    }

    return out;
}
}
