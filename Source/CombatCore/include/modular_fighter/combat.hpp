// 엔진에 의존하지 않는 결정적 전투 코어의 공개 인터페이스.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#ifndef MF_CORE_API
#define MF_CORE_API
#endif

namespace modular_fighter {

inline constexpr std::uint32_t SimulationHz = 60;
inline constexpr std::int32_t StageLimit = 10000;
inline constexpr std::int32_t InitialHp = 190;

inline constexpr std::int32_t RageHpThreshold = InitialHp / 4;

inline constexpr std::int32_t WalkForwardPerFrame = 33;
inline constexpr std::int32_t WalkBackPerFrame = 14;
inline constexpr std::int32_t MovePerFrame = WalkForwardPerFrame;
inline constexpr std::int32_t AttackRange = 1000;
inline constexpr std::int32_t AttackDamage = 100;
inline constexpr std::uint16_t AttackActiveFrame = 3;
inline constexpr std::uint16_t AttackTotalFrames = 10;
inline constexpr std::uint16_t HitStunFrames = 5;
inline constexpr std::uint16_t GuardStunFrames = 2;

inline constexpr std::uint16_t MaximumStunFrames = 120;
inline constexpr std::size_t DirectionHistoryFrames = 20;

inline constexpr std::uint8_t CommandBufferFrames = 6;

inline constexpr std::uint8_t DashBufferFrames = CommandBufferFrames;

inline constexpr std::int32_t BodyCylinderRadius = 150;

inline constexpr std::int32_t BodySeparation = 900;

inline constexpr std::size_t PushbackExtraFrames = 8;
inline constexpr std::size_t PushbackExtraCapacity = 26;
inline constexpr std::size_t SnapshotBytes = 244;
inline constexpr std::uint16_t SnapshotVersion = 20;
inline constexpr std::uint16_t RulesVersion = 80;

inline constexpr std::uint8_t MaximumComboHits = 8;

inline constexpr std::uint8_t MaximumPaulQcfAirHits = 2;

inline constexpr std::int32_t AirGravity = 6;

inline constexpr std::uint16_t GenericAirSolveFrames = 38;
inline constexpr std::uint16_t GenericAirLandingFrame = 37;

inline constexpr std::int32_t UnitQ16 = 1 << 16;

inline constexpr std::int32_t SyntheticHitHalfWidth = 300;

inline constexpr std::uint8_t SidestepTapFrames = 7;

inline constexpr std::uint8_t FullCrouchFrames = 9;
inline constexpr std::uint8_t RiseFrames = 9;
enum class Posture : std::uint8_t { Standing = 0, Crouching = 1, Rising = 2 };

enum Button : std::uint8_t {
    LP = 1,
    RP = 2,
    LK = 4,
    RK = 8,
    Guard = 16,
    NoGuard = 32,
    TrainingCounterOn = 64,
    TrainingCounterOff = 128,
    Attack = LP
};

inline constexpr std::uint8_t AllAttackButtons = LP | RP | LK | RK;

enum class CharacterId : std::uint8_t {
#if defined(MF_FULL_MOVESET)
#include <mf_full/character_ids.inc>
#else
#include "modular_fighter/generated/character_ids.inc"
#endif
};
enum class CommandId : std::uint16_t {
#if defined(MF_FULL_MOVESET)
#include <mf_full/command_ids.inc>
#else
#include "modular_fighter/generated/command_ids.inc"
#endif
};

inline constexpr std::size_t CommandPatternLength = 8;

struct CommandDefinition {
    CommandId id;
    CharacterId character;
    const char* name;
    const char* notation;
    std::uint8_t fallback_attack;
    std::array<std::uint8_t, CommandPatternLength> directions;
    std::uint8_t direction_count;

    CommandId held_variant = CommandId::None;

    bool context_only = false;

    std::uint8_t chord = 0;

    std::uint8_t window = 0;

    Posture posture = Posture::Standing;

    std::uint16_t direction_set = 0;

    std::uint8_t gate = 0;
    bool opponent_down = false;
};

enum CommandGate : std::uint8_t { GateHeat = 1, GateNoHeat = 2, GateRage = 4, GateNoRage = 8 };

MF_CORE_API const CommandDefinition* GetCommandDefinition(CommandId command) noexcept;

enum class HitLevel : std::uint8_t { Any = 0, High = 1, Mid = 2, Low = 3, SpecialLow = 4, Unblockable = 5 };

enum class ReactionKind : std::uint8_t {
#if defined(MF_FULL_MOVESET)
#include <mf_full/reaction_ids.inc>
#else
#include "modular_fighter/generated/reaction_ids.inc"
#endif
};

enum HitSituationIndex : std::uint8_t {
    HitFront = 0,
    HitFrontCrouch = 1,
    HitCounter = 2,
    HitCounterCrouch = 3,
    HitLeft = 4,
    HitLeftCrouch = 5,
    HitRight = 6,
    HitRightCrouch = 7,
    HitBack = 8,
    HitBackCrouch = 9,
    HitGuard = 10,
    HitGuardCrouch = 11,

    HitDownFaceUp = 12,
    HitDownFaceDown = 13,
    HitSituationCount = 14
};

struct HitReactionDefinition {
    std::uint16_t stun;
    std::uint8_t pushback;
    ReactionKind reaction;
    const char* clip;
};

struct AirHitDefinition {
    std::int32_t damage;
    std::array<ReactionKind, 4> reactions;
    std::array<std::uint8_t, 4> pushbacks;
};

enum class SystemAction : std::uint8_t { None, HeatBurst, HeatSmash, RageArt, Throw, HeatFollowup };
inline constexpr std::uint16_t HeatDurationFrames = 600;
inline constexpr std::uint16_t HeatBurstDurationFrames = 300;

inline constexpr std::uint16_t HeatBurstHoldFirst = 10, HeatBurstHoldLast = 39;

struct AttackDefinition {
    std::uint8_t button;
    const char* name;
    std::int32_t range;
    std::int32_t damage;
    std::uint16_t active_frame;
    std::uint16_t total_frames;
    std::uint16_t hit_stun_frames;
    std::uint16_t guard_stun_frames;
    std::uint16_t active_last_frame;
    HitLevel hit_level;
    bool source_data;
    std::uint8_t hit_pushback = 0;
    std::uint8_t block_pushback = 0;
    std::uint8_t hit_reaction = 0;

    const HitReactionDefinition* reactions = nullptr;
    const char* clip = nullptr;
    const AirHitDefinition* air = nullptr;

    bool ends_crouched = false;

    const AirHitDefinition* screw = nullptr;
    SystemAction system_action = SystemAction::None;
    const char* finish_clip = nullptr;
    const char* victim_clip = nullptr;
    std::uint16_t finish_frames = 0;
    std::int32_t finish_damage = 0;
    std::uint8_t throw_break = 0;
    CommandId automatic_command = CommandId::None;
    std::uint16_t automatic_frame = 0;
    bool automatic_preserve = false;
    bool catch_on_any_hit = false;
    CommandId heat_parent = CommandId::None;
    CommandId heat_start = CommandId::None;
};

enum PushbackId : std::uint8_t {
#if defined(MF_FULL_MOVESET)
#include <mf_full/pushback_ids.inc>
#else
#include "modular_fighter/generated/pushback_ids.inc"
#endif
};

struct PushbackDefinition {
    const char* source;
    std::uint16_t duration;
    std::int16_t linear;
    std::uint8_t extra_count;
    std::array<std::int16_t, PushbackExtraCapacity> extra;
};

MF_CORE_API const PushbackDefinition* GetPushbackDefinition(std::uint8_t id) noexcept;
MF_CORE_API std::uint16_t PushbackFrames(std::uint8_t id) noexcept;

MF_CORE_API std::int32_t PushbackDisplacement(std::uint8_t id, std::uint16_t frame) noexcept;

MF_CORE_API bool SourceHitContact(CharacterId attacker, CommandId command, std::uint16_t attack_frame,
                                  CharacterId defender, std::int32_t forward_mm, std::int32_t side_mm = 0,
                                  std::int32_t defender_rise_mm = 0,
                                  std::int32_t defender_face_cos = -UnitQ16,
                                  std::int32_t defender_face_sin = 0, std::uint8_t defender_pose = 0,
                                  ReactionKind defender_reaction = ReactionKind::None,
                                  std::uint16_t reaction_frame = 0, bool defender_airborne = false,
                                  std::array<std::int32_t, 3>* contact_point = nullptr) noexcept;
MF_CORE_API bool SourceHitContact(CommandId command, std::uint16_t attack_frame, CharacterId defender,
                                  std::int32_t forward_mm, std::int32_t side_mm = 0,
                                  std::int32_t defender_rise_mm = 0,
                                  std::int32_t defender_face_cos = -UnitQ16,
                                  std::int32_t defender_face_sin = 0) noexcept;

MF_CORE_API std::int32_t AttackTravel(CharacterId attacker, CommandId command,
                                      std::uint16_t attack_frame) noexcept;
MF_CORE_API std::int32_t AttackTravel(CommandId command, std::uint16_t attack_frame) noexcept;

MF_CORE_API const AttackDefinition* GetAttackDefinition(std::uint8_t button) noexcept;
MF_CORE_API const AttackDefinition* GetAttackDefinition(CommandId command) noexcept;

MF_CORE_API const AttackDefinition* GetAttackDefinition(CharacterId character, CommandId command) noexcept;
MF_CORE_API const AttackDefinition* GetContactAttackDefinition(CharacterId character, CommandId command,
                                                               bool heat) noexcept;

struct Input {
    std::int8_t horizontal = 0;
    std::uint8_t buttons = 0;
    std::int8_t vertical = 0;

    std::uint8_t training_rage = 0;

    std::uint8_t view_flipped = 0;
};

enum class MoveKind : std::uint8_t {
#if defined(MF_FULL_MOVESET)
#include <mf_full/move_ids.inc>
#else
#include "modular_fighter/generated/move_ids.inc"
#endif
};

enum MoveFlag : std::uint16_t {
    MoveCancelOnAttack = 1u << 0,
    MoveCancelOnOwnAttack = 1u << 1,
    MoveCancelOnBack = 1u << 2,
    MoveCancelOnForward = 1u << 3,
    MoveCancelOnUp = 1u << 4,
    MoveHoldDownForward = 1u << 5,
    MoveCrouching = 1u << 6,
    MoveEndsCrouched = 1u << 7,
    MoveLoop = 1u << 8,
    MoveLateral = 1u << 9,
    MoveJump = 1u << 10,
};

struct MoveDefinition {
    MoveKind move;
    CharacterId owner;
    const char* clip;
    std::uint16_t flags;
    std::uint16_t cancel_first;
    std::uint16_t attack_cancel_last;
    ReactionKind after = ReactionKind::None;
    CommandId after_command = CommandId::None;
};

MF_CORE_API const MoveDefinition* GetMoveDefinition(MoveKind move) noexcept;

inline constexpr std::uint8_t PonStepEntryFrames = 5;

MF_CORE_API std::uint16_t MoveFrames(MoveKind kind) noexcept;

MF_CORE_API std::int32_t MoveRise(MoveKind kind, std::uint16_t frame) noexcept;
MF_CORE_API std::uint16_t ReactionDownFrame(ReactionKind kind) noexcept;

enum ReactionFlag : std::uint8_t {
    ReactionLying = 1u << 0,
    ReactionFaceDown = 1u << 1,

    ReactionEndsCrouched = 1u << 2,
};

struct ReactionDefinition {
    ReactionKind kind;
    const char* clip;
    std::uint16_t frames;
    ReactionKind next;
    std::uint16_t ukemi_first;
    std::uint16_t ukemi_last;
    std::uint8_t flags;

    std::uint16_t air_last = 0;
};

MF_CORE_API const ReactionDefinition* GetReactionDefinition(ReactionKind kind) noexcept;

MF_CORE_API std::uint64_t ModuleDataHash(CharacterId character) noexcept;

MF_CORE_API std::uint64_t RulesFingerprint() noexcept;

struct Fighter {
    std::int32_t position_x = 0;
    std::int32_t position_z = 0;
    std::int32_t hp = InitialHp;
    std::uint16_t attack_frame = 0;
    std::uint16_t stun_frames = 0;
    std::uint8_t previous_buttons = 0;
    std::uint8_t active_attack = 0;
    std::int8_t previous_horizontal = 0;
    std::int8_t previous_vertical = 0;
    CharacterId character = CharacterId::Training;
    std::int8_t facing = 1;
    CommandId active_command = CommandId::None;
    CommandId pending_command = CommandId::None;
    std::uint8_t pending_frames = 0;
    std::uint8_t direction_count = 0;

    std::array<std::uint8_t, DirectionHistoryFrames> direction_history{};
    ReactionKind reaction = ReactionKind::None;
    std::uint16_t reaction_frame = 0;
    std::uint8_t attack_connected = 0;
    std::uint8_t pushback = NoPushback;
    std::uint8_t pushback_frame = 0;
    std::int32_t push_cos = 0;
    std::int32_t push_sin = 0;
    MoveKind move = MoveKind::None;
    std::uint8_t move_frame = 0;
    std::int32_t face_cos = UnitQ16;
    std::int32_t face_sin = 0;

    CommandId queued_command = CommandId::None;

    std::uint8_t crouch_frames = 0;
    std::uint8_t rise_frame = 0;

    std::uint8_t combo_hits = 0;
    std::uint8_t combo_flags = 0;

    std::int32_t air_height = 0;
    std::int32_t air_velocity = 0;

    std::int16_t air_drift_x = 0;
    std::int16_t air_drift_z = 0;
    std::uint8_t air_phase = 0;
    std::uint8_t training_rage = 0;
    std::uint16_t heat_frames = 0;
    std::uint8_t heat_used = 0;
    std::uint8_t rage_spent = 0;
    CommandId sequence_command = CommandId::None;
    CharacterId sequence_owner = CharacterId::Training;
    std::uint16_t sequence_frame = 0;
    std::uint8_t sequence_subframe = 0;
    std::uint8_t sequence_role = 0;

    std::uint16_t standing_motion = 0;
    std::uint16_t standing_frame = 0;
};

MF_CORE_API const char* StandingReactionClip(const Fighter& fighter) noexcept;

MF_CORE_API bool RageActive(const Fighter& fighter) noexcept;

MF_CORE_API std::int32_t ReactionBodyHeight(CharacterId character, ReactionKind reaction,
                                            std::uint16_t frame) noexcept;
MF_CORE_API std::int32_t AirPresentationOffset(const Fighter& fighter) noexcept;

MF_CORE_API std::int32_t GenericAirInitialVelocity(std::int32_t height, std::int32_t landing_height) noexcept;

enum ComboFlag : std::uint8_t { ComboScrewed = 1u << 0, ComboPaulQcfAirMask = 3u << 1 };

inline std::uint8_t PaulQcfAirHits(const Fighter& fighter) noexcept {
    return (fighter.combo_flags & ComboPaulQcfAirMask) >> 1;
}

MF_CORE_API std::uint8_t HitSituation(const Fighter& attacker, const Fighter& defender, bool crouching,
                                      bool blocked, bool counter) noexcept;

MF_CORE_API bool IsCounterHit(const Fighter& defender_before_step) noexcept;

MF_CORE_API bool ResolveCounterHit(const Fighter& defender_before_step,
                                   std::uint8_t defender_buttons) noexcept;

MF_CORE_API bool IsAirborne(const Fighter& fighter) noexcept;

MF_CORE_API std::uint8_t GroundHitPose(const Fighter& fighter) noexcept;

MF_CORE_API std::uint16_t AttackEndFrame(CharacterId character, CommandId command) noexcept;
MF_CORE_API const char* AttackPoseClip(const Fighter& fighter, std::uint16_t& sample) noexcept;

MF_CORE_API Posture PostureOf(const Fighter& fighter) noexcept;

struct State {
    std::uint32_t frame = 0;
    std::array<Fighter, 2> fighters{};

    std::int32_t view_cos = UnitQ16;
    std::int32_t view_sin = 0;

    std::uint8_t hitstop_enabled = 0;
    std::uint8_t hitstop_frames = 0;
};

MF_CORE_API bool operator==(const Input& a, const Input& b) noexcept;
MF_CORE_API bool operator!=(const Input& a, const Input& b) noexcept;
MF_CORE_API bool operator==(const Fighter& a, const Fighter& b) noexcept;
MF_CORE_API bool operator!=(const Fighter& a, const Fighter& b) noexcept;
MF_CORE_API bool operator==(const State& a, const State& b) noexcept;
MF_CORE_API bool operator!=(const State& a, const State& b) noexcept;

struct DebugSphere {
    std::int32_t x, z, up, radius;
};

struct DebugSegment {
    std::int32_t ax, az, aup, bx, bz, bup, radius;
};

struct FighterGeometry {
    std::array<DebugSphere, 14> hurt{};
    std::uint8_t hurt_count = 0;
    std::array<DebugSegment, 32> hit{};
    std::uint8_t hit_count = 0;
};

MF_CORE_API FighterGeometry DescribeFighterGeometry(const State& state, std::size_t index) noexcept;
MF_CORE_API bool IsValid(const Input& input) noexcept;
MF_CORE_API bool IsValid(const State& state) noexcept;
// 경기 시작 상태를 만든다.
MF_CORE_API State MakeInitialState(CharacterId first = CharacterId::Training,
                                   CharacterId second = CharacterId::Training) noexcept;

struct MotionDelta {
    std::int32_t x = 0, z = 0;
};

struct FighterMotionTrace {
    MotionDelta action, chase, drift, pushback, separation;
    bool has_contact = false;
    std::array<std::int32_t, 3> contact_world{};
};

struct MotionTrace {
    bool regular_step = false;
    std::array<FighterMotionTrace, 2> fighters{};
};

// 입력 한 프레임만큼 상태를 진행한다. 같은 입력이면 항상 같은 결과가 나온다.
MF_CORE_API bool Step(State& state, const std::array<Input, 2>& inputs,
                      MotionTrace* trace = nullptr) noexcept;

// 상태를 정규 바이트열로 저장한다.
MF_CORE_API std::vector<std::uint8_t> Serialize(const State& state);

// 바이트열에서 상태를 복원한다.
MF_CORE_API bool Deserialize(const std::vector<std::uint8_t>& bytes, State& destination) noexcept;

// 클라이언트 간 동기화 비교에 쓰는 상태 해시.
MF_CORE_API std::uint64_t HashState(const State& state);

}
