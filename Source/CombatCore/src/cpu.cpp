// 거리와 상대 상태로 행동을 고르는 CPU.
#include "modular_fighter/cpu.hpp"

#include <cstdlib>
#include <memory>

namespace modular_fighter {
namespace {

Input In(std::int8_t h, std::uint8_t b, std::int8_t v, std::uint8_t extra) {
    return Input{h, static_cast<std::uint8_t>(b | extra), v};
}

std::vector<Input> Repeat(Input in, int n) {
    return std::vector<Input>(static_cast<std::size_t>(n), in);
}

std::vector<Input> operator+(std::vector<Input> a, const std::vector<Input>& b) {
    a.insert(a.end(), b.begin(), b.end());

    return a;
}

bool FreeOf(const Fighter& a) {
    return a.attack_frame == 0 && a.stun_frames == 0 && a.move == MoveKind::None;
}

std::int64_t DistanceSq(const State& s) {
    const std::int64_t dx = std::int64_t(s.fighters[1].position_x) - s.fighters[0].position_x;
    const std::int64_t dz = std::int64_t(s.fighters[1].position_z) - s.fighters[0].position_z;

    return dx * dx + dz * dz;
}

constexpr std::int64_t Sq(std::int64_t v) {
    return v * v;
}

const std::vector<std::string>& CombosOf(CharacterId character) {
    static const std::vector<std::string> kazuya = {
        "EW W10 B2 +2 DF1 DF2 W20 SSU SS4 +1 DASH6 B2 +2 +12",
        "EW W0 SSD SS4 +1 W10 DF1 DF2 W20 SSU SS4 +1 DASH6 B2 +2 +12",
        "EW W8 EW W0 DASH4 B2 +2 W0 DF1 DF2 W24 SSU SS4 +1 W4 DASH6 B2 +2 +12",
    };
    static const std::vector<std::string> paul = {
        "UF4 W2 3 +2 W4 2 W0 DASH2 3 +2 HOLDB40 W2 SWAYDF KRP +1d W8 DF34 W0 MDF1 W0 FFF2",
        "FF3 W2 2 W2 DF4 W2 2 W0 DASH0 3 +2 HOLDB40 W0 SWAYDF KRP +1d W2 DF34 W0 MDF1 W0 FFF2",
        "QCF1 W2 4 W2 DF4 W2 2 W0 DASH2 3 +2 HOLDB40 W0 SWAYDF KRP +1d W2 DF34 W0 MDF1 W0 FFF2",
    };
    static const std::vector<std::string> none;

    return character == CharacterId::Kazuya ? kazuya : character == CharacterId::Paul ? paul : none;
}

}

std::vector<RouteStep> ParseRoute(const std::string& spec, std::uint8_t player, std::uint8_t extra,
                                  std::string& error) {
#if defined(MF_FULL_MOVESET)
    const std::uint8_t p = player & 1u;
    const Input n = In(0, 0, 0, extra);
    auto I = [extra](std::int8_t h, std::uint8_t b, std::int8_t v) {
        return In(h, b, v, extra);
    };
    auto Free = [p](const State& s) {
        return FreeOf(s.fighters[p]);
    };
    auto At = [p](CommandId c, int frame) {
        return std::function<bool(const State&)>([p, c, frame](const State& s) {
            return s.fighters[p].active_command == c && s.fighters[p].attack_frame >= frame;
        });
    };
    std::vector<RouteStep> steps;
    std::vector<std::string> tokens;

    for (std::size_t i = 0; i < spec.size();) {
        while (i < spec.size() && spec[i] == ' ')
            ++i;
        std::size_t j = i;

        while (j < spec.size() && spec[j] != ' ')
            ++j;

        if (j > i)
            tokens.push_back(spec.substr(i, j - i));
        i = j;
    }

    auto ready = [p](const State& s) {
        const auto& a = s.fighters[p];

        return FreeOf(a) || (a.attack_frame == 0 && a.stun_frames == 0 && a.move != MoveKind::None) ||
               (a.active_command != CommandId::None &&
                a.attack_frame + 3 >= AttackEndFrame(a.character, a.active_command));
    };

    auto last_pressed = std::make_shared<CommandId>(CommandId::None);
    int wait = 0;

    for (const auto& t : tokens) {
        auto lead = Repeat(n, wait);
        wait = 0;

        auto fresh = [last_pressed](std::function<bool(const State&)> when) {
            return std::function<bool(const State&)>([last_pressed, when](const State& s) {
                if (!when(s))
                    return false;
                *last_pressed = CommandId::None;

                return true;
            });
        };
        auto attack = [&](std::vector<Input> in) {
            steps.push_back({fresh(ready), lead + in});
        };

        if (t[0] == 'W' && t.size() > 1 && t[1] >= '0' && t[1] <= '9') {
            wait = std::atoi(t.c_str() + 1);
            continue;
        }

        if (t[0] == '+') {
            const bool held = t.back() == 'd';
            const std::string key = held ? t.substr(0, t.size() - 1) : t;
            const std::uint8_t b = key == "+1"    ? LP
                                   : key == "+2"  ? RP
                                   : key == "+3"  ? LK
                                   : key == "+4"  ? RK
                                   : key == "+12" ? LP | RP
                                   : key == "+34" ? LK | RK
                                                  : 0;

            if (!b) {
                error = t;

                return {};
            }

            const Input press = held ? I(1, b, -1) : I(0, b, 0), rest = held ? I(1, 0, -1) : n;
            steps.push_back({[p, last_pressed](const State& s) {
                                 const auto& a = s.fighters[p];

                                 if (a.active_command == CommandId::None ||
                                     a.active_command == *last_pressed || a.attack_frame < 4)
                                     return false;
                                 *last_pressed = a.active_command;

                                 return true;
                             },
                             lead + std::vector<Input>{press, rest, press, rest}});
            continue;
        }

        if (t.rfind("HOLDB", 0) == 0 || t.rfind("HOLDDF", 0) == 0) {
            const bool back = t[4] == 'B';
            steps.push_back(
                {[](const State&) { return true; },
                 lead + Repeat(back ? I(-1, 0, 0) : I(1, 0, -1), std::atoi(t.c_str() + (back ? 5 : 6)))});
            continue;
        }

        auto in_move = [p](MoveKind kind) {
            return std::function<bool(const State&)>([p, kind](const State& s) {
                return s.fighters[p].move == kind && s.fighters[p].move_frame >= 2;
            });
        };

        if (t == "MDF1" || t == "MDF2") {
            steps.push_back({fresh([p](const State& s) {
                                 return s.fighters[p].move != MoveKind::None &&
                                        s.fighters[p].move_frame >= 15;
                             }),
                             lead + std::vector<Input>{I(1, t == "MDF1" ? LP : RP, -1), I(1, 0, -1)}});
            continue;
        }

        if (t == "SWAY12") {
            steps.push_back({fresh(in_move(MoveKind::Sway)), lead + std::vector<Input>{I(0, LP | RP, 0), n}});
            continue;
        }

        if (t == "SWAYDF") {
            steps.push_back({fresh(in_move(MoveKind::Sway)), lead + Repeat(I(1, 0, -1), 6)});
            continue;
        }

        if (t == "KRP" || t == "KLP" || t == "K34") {
            const std::uint8_t b = t == "KRP" ? RP : t == "KLP" ? LP : LK | RK;
            steps.push_back(
                {fresh(in_move(MoveKind::Kakugo)), lead + std::vector<Input>{I(1, b, -1), I(1, 0, -1)}});
            continue;
        }

        if (t == "EW")
            attack({I(1, 0, 0), n, I(0, 0, -1), I(1, RP, -1), n});
        else if (t == "1" || t == "2" || t == "3" || t == "4")
            attack({I(0, t == "1" ? LP : t == "2" ? RP : t == "3" ? LK : RK, 0), n});
        else if (t == "PDF2")
            attack({I(1, RP, -1), n});
        else if (t == "PDF1")
            attack({I(1, LP, -1), n});
        else if (t == "QCF1")
            attack({I(0, 0, -1), I(1, 0, -1), I(1, LP, 0), n});
        else if (t == "QCB12")
            attack({I(0, 0, -1), I(-1, 0, -1), I(-1, LP | RP, 0), n});
        else if (t == "UF3")
            attack({I(1, LK, 1), n});
        else if (t == "B3")
            attack({I(-1, LK, 0), n});
        else if (t == "DF4")
            attack({I(1, RK, -1), n});
        else if (t == "KAK")
            attack({I(0, 0, -1), I(1, 0, -1), I(1, 0, 0), I(1, 0, -1), I(1, 0, -1)});
        else if (t == "FFEW")
            attack(std::vector<Input>{I(1, 0, 0), n} + Repeat(I(1, 0, 0), 6) +
                   std::vector<Input>{n, I(0, 0, -1), I(1, RP, -1), n});
        else if (t == "FFF2")
            steps.push_back({fresh(Free), lead + std::vector<Input>{I(1, 0, 0), n, I(1, 0, 0), n} +
                                              Repeat(I(1, 0, 0), 3) +
                                              std::vector<Input>{I(1, RP, 0), I(1, 0, 0), n}});
        else if (t == "B2")
            attack({I(-1, RP, 0), n});
        else if (t == "DF1")
            attack({I(1, LP, -1), I(1, 0, -1)});
        else if (t == "DF3")
            attack({I(1, LK, -1), n});
        else if (t == "DF34")
            attack({I(1, 0, -1), I(1, LK | RK, -1), I(1, 0, -1)});
        else if (t == "F3")
            attack({I(1, LK, 0), n});
        else if (t == "FF3" || t == "FF2" || t == "FF4")
            attack({I(1, 0, 0), n, I(1, 0, 0), I(1, t == "FF3" ? LK : t == "FF2" ? RP : RK, 0), n});
        else if (t == "D4")
            attack({I(0, RK, -1), n});
        else if (t == "UF4")
            attack({I(1, RK, 1), n});
        else if (t == "WS3" || t == "WS4")
            attack(Repeat(I(0, 0, -1), 24) + std::vector<Input>{n, I(0, t == "WS3" ? LK : RK, 0), n});
        else if (t == "QWS3")
            attack(Repeat(I(0, 0, -1), 3) + std::vector<Input>{n, I(0, LK, 0), n});
        else if (t == "QWS1" || t == "QWS2" || t == "QWS12")
            attack(Repeat(I(0, 0, -1), 3) + std::vector<Input>{n,
                                                               I(0,
                                                                 t == "QWS1"   ? LP
                                                                 : t == "QWS2" ? RP
                                                                               : LP | RP,
                                                                 0),
                                                               n});
        else if (t == "WS1" || t == "WS2" || t == "WS12")
            attack(Repeat(I(0, 0, -1), 24) + std::vector<Input>{n,
                                                                I(0,
                                                                  t == "WS1"   ? LP
                                                                  : t == "WS2" ? RP
                                                                               : LP | RP,
                                                                  0),
                                                                n});
        else if (t == "DF2")
            steps.push_back({fresh(At(CommandId::KazuyaAutoDFLPKzbodylp00, 19)),
                             lead + std::vector<Input>{I(1, RP, -1), I(1, 0, -1), n}});
        else if (t == "SSU" || t == "SSD")
            steps.push_back({Free, lead + std::vector<Input>{I(0, 0, t == "SSU" ? 1 : -1), n}});
        else if (t == "SS4" || t == "SS3" || t == "SS2") {
            const std::uint8_t b = t == "SS4" ? RK : t == "SS3" ? LK : RP;
            steps.push_back({fresh([p](const State& s) {
                                 return s.fighters[p].move != MoveKind::None && s.fighters[p].move_frame >= 4;
                             }),
                             lead + std::vector<Input>{I(0, b, 0), n}});
        } else if (t.rfind("DASH", 0) == 0) {
            steps.push_back({ready, lead + std::vector<Input>{I(1, 0, 0), n, I(1, 0, 0)} +
                                        Repeat(I(1, 0, 0), std::atoi(t.c_str() + 4))});
        } else {
            error = t;

            return {};
        }
    }

    return steps;
#else
    (void)spec;
    (void)player;
    (void)extra;
    error = "routes need the full moveset";

    return {};
#endif
}

CpuController::CpuController(std::uint8_t player, std::uint32_t seed)
    : player_(player & 1u), rng_(seed ? seed : 0x9E3779B9u) {}

std::uint32_t CpuController::Random() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;

    return rng_;
}

// 현재 상황을 판단해 다음 행동을 고른다.
Input CpuController::Screen(const State& state, Input in) const {
    const auto facing = state.fighters[player_].facing ? state.fighters[player_].facing
                                                       : static_cast<std::int8_t>(player_ == 0 ? 1 : -1);
    in.horizontal = static_cast<std::int8_t>(in.horizontal * facing);

    return in;
}

// 콤보 루트를 시작한다.
void CpuController::StartRoute(const State& state, const std::string& spec) {
    std::string error;
    steps_ = ParseRoute(spec, player_, 0, error);

    if (steps_.empty())
        return;
    mode_ = Mode::Route;
    step_ = cursor_ = 0;
    playing_ = false;
    combo_landed_ = false;
    mode_frame_ = step_wait_frame_ = state.frame;
    activity_ = "combo";
    ++combos_started_;
}

void CpuController::StartHold(Input input, int frames, const char* label) {
    mode_ = Mode::Hold;
    hold_ = input;
    hold_frames_ = frames;
    activity_ = label;
}

// 이번 프레임의 입력을 만든다.
Input CpuController::Next(const State& state) {
    const auto& me = state.fighters[player_];
    const auto& op = state.fighters[1u - player_];
    const Input neutral{};

    if (me.hp == 0 || op.hp == 0) {
        mode_ = Mode::Neutral;
        activity_ = "neutral";

        return neutral;
    }

    if (mode_ == Mode::Route) {
        combo_landed_ = combo_landed_ || op.combo_hits != 0;
        const bool dropped = combo_landed_ && op.combo_hits == 0 && !playing_;
        const bool stalled = !playing_ && state.frame - step_wait_frame_ > 75;

        const bool missed = (step_ >= 1 && !combo_landed_ && state.frame - mode_frame_ > 50) ||
                            (combo_landed_ && op.reaction == ReactionKind::None && op.stun_frames == 0 &&
                             state.frame - step_wait_frame_ > 12);

        if (dropped || stalled || missed || step_ >= steps_.size() || me.reaction != ReactionKind::None) {
            mode_ = Mode::Neutral;
            activity_ = "neutral";
            next_decision_ = state.frame + 8;
        } else {
            if (!playing_ && steps_[step_].wait(state)) {
                playing_ = true;
                cursor_ = 0;
            }

            if (!playing_)
                return neutral;
            const Input in = steps_[step_].inputs[cursor_++];

            if (cursor_ == steps_[step_].inputs.size()) {
                playing_ = false;
                ++step_;
                step_wait_frame_ = state.frame;
            }

            return Screen(state, in);
        }
    }

    if (me.reaction != ReactionKind::None || me.stun_frames != 0) {
        mode_ = Mode::Neutral;
        activity_ = "down";

        return state.frame % 12 < 2 ? Input{0, 0, 1} : neutral;
    }

    const bool op_attacking =
        op.attack_frame != 0 && op.active_command != CommandId::None && DistanceSq(state) < Sq(2400);

    if (op_attacking) {
        const auto command = static_cast<std::uint16_t>(op.active_command);

        if (command != guarded_attack_) {
            guarded_attack_ = command;
            const auto* attack = GetAttackDefinition(op.character, op.active_command);
            const bool low =
                attack && (attack->hit_level == HitLevel::Low || attack->hit_level == HitLevel::SpecialLow);
            guard_low_ = (Random() % 4 != 0) ? low : !low;
            guarding_ = Random() % 5 < 3;
            punish_ready_ = false;
        }

        if (guarding_ && FreeOf(me)) {
            if (op.attack_connected)
                punish_ready_ = true;
            mode_ = Mode::Neutral;
            activity_ = "guard";

            return Screen(state, Input{-1, 0, static_cast<std::int8_t>(guard_low_ ? -1 : 0)});
        }
    } else {
        guarded_attack_ = 0;

        if (punish_ready_ && FreeOf(me)) {
            punish_ready_ = false;

            if (DistanceSq(state) < Sq(1300)) {
                StartHold(Input{0, LP, 0}, 2, "punish");
                next_decision_ = state.frame + 14;
            }
        }
    }

    if (mode_ == Mode::Hold) {
        if (hold_frames_-- > 0)
            return Screen(state, hold_);
        mode_ = Mode::Neutral;
        activity_ = "neutral";
    }

    if (me.attack_frame != 0)
        return neutral;

    if (me.move != MoveKind::None && me.move != MoveKind::DashForward && me.move != MoveKind::Run) {
        activity_ = "recover";

        return Screen(state, Input{-1, 0, 0});
    }

    const auto distance = DistanceSq(state);

    if (distance > Sq(1500)) {
        activity_ = "approach";

        return Screen(state, Input{1, 0, 0});
    }

    if (state.frame < next_decision_)
        return neutral;

    const auto roll = Random() % 100;
    const auto& combos = CombosOf(me.character);

    if (roll < 55 && !combos.empty() && distance < Sq(1250)) {
        StartRoute(state, combos[Random() % combos.size()]);

        if (mode_ == Mode::Route)
            return neutral;
    }

    if (roll < 78) {
        static const Input pokes[] = {{0, LP, 0}, {1, LP, -1}, {0, RK, -1}, {0, RP, 0}};
        StartHold(pokes[Random() % 4], 2, "poke");
    } else if (roll < 86)
        StartHold(Input{0, 0, static_cast<std::int8_t>(Random() % 2 ? 1 : -1)}, 2, "sidestep");
    else if (roll < 93)
        StartHold(Input{-1, 0, 0}, 14, "back");
    else
        StartHold(Input{0, 0, 0}, 10, "wait");
    next_decision_ = state.frame + 12 + Random() % 16;

    return neutral;
}

}
