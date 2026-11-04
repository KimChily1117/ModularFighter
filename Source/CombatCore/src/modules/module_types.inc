// 캐릭터 모듈 데이터 형식.
template <typename T> struct Span {
    const T* items = nullptr;
    std::size_t count = 0;

    const T* begin() const noexcept {
        return items;
    }

    const T* end() const noexcept {
        return items + count;
    }
};

template <typename T, std::size_t N> constexpr Span<T> SpanOf(const std::array<T, N>& values) noexcept {
    return {values.data(), N};
}

constexpr std::uint16_t Dir(std::uint8_t numpad) noexcept {
    return static_cast<std::uint16_t>(1u << numpad);
}

struct Pattern {
    std::array<std::uint8_t, 4> directions{};
    std::uint8_t count = 0;
};

struct AttackEntry {
    CommandId command;
    AttackDefinition rules;
};

struct EntryRule {
    MoveKind move;
    std::uint8_t last;
    Pattern pattern;
    std::uint8_t run_direction;
    std::uint8_t run_max;
    bool keep_history;

    std::uint8_t buttons = 0;
};

struct MoveTransition {
    MoveKind from;
    MoveKind to;
    CommandId command;
    std::uint8_t button;
    std::uint16_t directions;
    bool fresh;
    Pattern pattern;
    std::uint16_t first, last;

    std::uint16_t start = 0;
};

struct WakeOption {
    ReactionKind from;
    std::uint16_t directions;
    std::uint8_t buttons;
    bool all_buttons;
    MoveKind move;
    CommandId command;
};

struct StringCancel {
    CommandId from;
    CommandId to;
    std::uint8_t buttons;
    std::uint16_t directions;
    std::uint16_t first, last;
    std::uint16_t start;
    std::uint8_t gate = 0;
};

struct CharacterModule {
    CharacterId character;
    Span<CommandDefinition> commands;
    Span<AttackEntry> attacks;
    Span<MoveDefinition> moves;
    Span<EntryRule> entries;
    Span<MoveTransition> transitions;
    Span<ReactionDefinition> reactions;
    Span<WakeOption> wake;
    Span<StringCancel> strings;
    std::uint64_t data_hash;
};
