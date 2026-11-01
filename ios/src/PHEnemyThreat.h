#pragma once
#include "PHEnemyActions.h"
#include <algorithm>
#include <cmath>
#include <limits>

// Ordered action events, not a next-turn selector. A caller must establish
// reachable branches, post-player state and applicable shields separately.
namespace PHEnemyThreat {
namespace EA = PHEnemyActions;
constexpr unsigned Version = 1, MaxEvents = 128;
static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24,
    "native single precision arithmetic");
enum class Kind : uint8_t { Attack, Gravity, Enrage, AwokenBind, AssistVoid, Unmatchable, SkillDelay, Unknown };
enum Issue : uint32_t { Missing = 1, Unsupported = 2, Parameters = 4, Compound = 8, EventLimit = 16, Arithmetic = 32 };
struct Event {
    Kind kind = Kind::Unknown;
    int32_t actionID = 0, value = 0, maximum = 0;
    uint16_t opcode = 0, colors = 0;
    uint8_t targets = 0;
    bool attached = false, optional = false, randomColor = false;
};
struct Sequence {
    std::array<Event, MaxEvents> events{};
    unsigned count = 0;
    uint32_t issues = 0;
    bool complete() const { return !issues; }
};
inline const char *Name(Kind kind) {
    switch (kind) {
        case Kind::Attack: return "attack";
        case Kind::Gravity: return "current-hp-gravity";
        case Kind::Enrage: return "enrage";
        case Kind::AwokenBind: return "awoken-bind";
        case Kind::AssistVoid: return "assist-void";
        case Kind::Unmatchable: return "unmatchable";
        case Kind::SkillDelay: return "skill-delay";
        case Kind::Unknown: return "unknown-effect";
    }
    return "unknown-effect";
}
inline bool Supported(uint16_t op) {
    return op == 15 || op == 17 || op == 18 || op == 19 || op == 47 || op == 50 || op == 82 ||
        op == 88 || op == 89 || op == 107 || op == 140 || op == 141;
}
inline void Append(Sequence &out, const Event &event) {
    if (out.count == MaxEvents) out.issues |= EventLimit;
    else out.events[out.count++] = event;
}
inline bool RandomRange(int32_t low, int32_t high) {
    // Restrict the signed multiply/add in the native 16-bit RNG range to
    // values that cannot overflow. Randomness is bounded, never sampled.
    return low >= 0 && high >= low && high <= INT16_MAX;
}
inline void Leaf(Sequence &out, int32_t id, const EA::Node *node, bool child) {
    Event event; event.actionID = id;
    if (!node || !node->known) { out.issues |= Missing; Append(out, event); return; }
    const auto &record = node->record;
    const uint16_t op = record.opcode(); event.opcode = op;
    if (op == 82) {
        if (!child) { event.kind = Kind::Attack; event.value = 100; Append(out, event); }
        return; // Root falls back to attack; compound child82 is skipped.
    }
    if (!Supported(op)) {
        out.issues |= op == 83 ? Compound : Unsupported; Append(out, event); return;
    }
    if (record.word(68) > 0) {
        Event attached = event; attached.kind = Kind::Attack; attached.value = record.word(68); attached.attached = true;
        Append(out, attached); // The hit happens before the corresponding status.
    }
    const int32_t p0 = record.parameter(0), p1 = record.parameter(1), p2 = record.parameter(2);
    bool valid = true;
    switch (op) {
        case 15:
            valid = RandomRange(p0,p1) && p2 >= 0;
            if (valid) for (unsigned hit = 0; hit < unsigned(std::min(p1,15)); ++hit) {
                event.kind = Kind::Attack; event.value = p2; event.optional = hit >= unsigned(std::min(p0,15));
                Append(out, event);
            }
            break;
        case 47:
            valid = p1 >= 0; event.kind = Kind::Attack; event.value = p1; break;
        case 50:
            valid = p0 >= 0; event.kind = Kind::Gravity; event.value = p0; break;
        case 17: case 18: case 19:
            event.kind = Kind::Enrage; event.value = op == 18 ? p1 : p2; event.maximum = op == 18 ? p0 : p1;
            valid = event.value >= 0 && event.maximum >= 0 && event.maximum <= INT16_MAX; break;
        case 88: case 141:
            event.kind = op == 88 ? Kind::AwokenBind : Kind::AssistVoid; event.value = p0;
            valid = p0 >= 0 && p0 <= 511; break;
        case 107:
            event.kind = Kind::Unmatchable; event.value = p0;
            event.randomColor = p1 < 0; event.colors = p1 < 0 ? 31u : uint16_t(p1);
            valid = p0 >= 0 && p0 <= 511 && (p1 < 0 || p1 <= UINT16_MAX); break;
        case 89: case 140:
            event.kind = Kind::SkillDelay; event.value = p0; event.maximum = p1;
            event.targets = op == 89 || (p2 != 1 && p2 != 2) ? 63 : p2 == 1 ? 33 : 30;
            valid = RandomRange(p0,p1); break;
        default: valid = false; break;
    }
    if (!valid) { out.issues |= Parameters; event.kind = Kind::Unknown; Append(out, event); }
    else if (op != 15) Append(out, event);
}
inline Sequence Describe(const EA::Graph &graph, int32_t id) {
    Sequence out;
    if (id < 0) { Event attack; attack.kind = Kind::Attack; attack.value = 100; attack.actionID = id; Append(out,attack); return out; }
    const auto *root = graph.find(id);
    if (!root || !root->known || root->record.opcode() != 83) { Leaf(out,id,root,false); return out; }
    // Preserve duplicates and do not turn a root's attached field into a hit.
    bool terminated = false;
    for (unsigned child = 0; child < 8; ++child) {
        const int32_t next = root->record.parameter(child);
        if (next <= 0) { terminated = true; break; }
        Leaf(out,next,graph.find(next),true);
    }
    if (!terminated) out.issues |= Compound;
    return out;
}

struct Number { int32_t value = 0; bool known = false; };
inline Number RoundFloat(float value) {
    volatile float rounded = value + (value < 0 ? -0.5f : 0.5f);
    if (!std::isfinite(rounded) || double(rounded) < double(INT32_MIN) || double(rounded) > double(INT32_MAX)) return {};
    return {int32_t(rounded),true};
}
inline Number Reduce(int32_t damage, float multiplier) {
    if (damage < 0 || !std::isfinite(multiplier) || multiplier < 0 || multiplier > 1) return {};
    volatile double product = double(damage) * double(multiplier);
    volatile double rounded = product + 0.5;
    if (rounded > double(INT32_MAX)) return {};
    return {int32_t(rounded),true};
}
struct AttackState {
    int64_t base = 0;
    int32_t addition = 0;
    float enrage = 1;
    int16_t enrageTurns = 0;
    bool known = false;
};
inline Number RawHit(AttackState &enemy, int32_t percent, int32_t fixed = 0) {
    if (!enemy.known || enemy.base < 0 || percent < 0 || fixed < 0) return {};
    volatile float coefficient = float(percent) / 100.0f;
    if (enemy.enrageTurns > 0) {
        if (!std::isfinite(enemy.enrage) || enemy.enrage < 0) return {};
        coefficient = coefficient * enemy.enrage;
    }
    volatile float base = float(enemy.base);
    volatile float product = base * coefficient;
    Number raw = RoundFloat(product);
    if (!raw.known) return {};
    if (fixed > 0) raw.value = fixed;
    const int64_t sum = int64_t(raw.value) + enemy.addition;
    if (sum < 0 || sum > INT32_MAX) return {};
    enemy.addition = 0; // Consumed once, not once per hit or compound child.
    return {int32_t(sum),true};
}
inline Number Gravity(int32_t hp, int32_t percent) {
    if (hp < 0 || percent < 0) return {};
    if (percent == 100) return {hp,true};
    volatile float product = float(hp) * float(percent);
    volatile float value = product / 100.0f;
    if (!std::isfinite(value) || double(value) > double(INT32_MAX)) return {};
    return {int32_t(value),true}; // Native nonnegative truncation, before shields.
}

// Conditional upper bound before every player reduction. It cannot itself
// authorize survival: gravity needs current per-hit HP and status events can
// invalidate HP, healing, shields or the next rescue. Unknown stays unknown.
struct UpperBound {
    int64_t damage = 0;
    unsigned hits = 0;
    uint32_t issues = 0, effects = 0;
    bool known = false;
};
inline UpperBound BeforeShields(const Sequence &sequence, AttackState enemy, int32_t playerHPUpper) {
    UpperBound out; out.issues = sequence.issues;
    if (!enemy.known) { out.issues |= Missing; return out; }
    if (enemy.enrageTurns > 0 && (!std::isfinite(enemy.enrage) || enemy.enrage < 0)) {
        out.issues |= Arithmetic; return out;
    }
    float enrageUpper = std::max(1.0f, enemy.enrageTurns > 0 ? enemy.enrage : 1.0f);
    // Optional compound children and optional extra hits can leave the old
    // enrage/addition untouched. Overapproximate, do not subtract their credit.
    const int32_t additionUpper = std::max(enemy.addition,0);
    for (unsigned i = 0; i < sequence.count; ++i) {
        const auto &event = sequence.events[i];
        if (event.kind == Kind::Enrage) {
            if (event.maximum > 0) enrageUpper = std::max(enrageUpper,float(event.value) / 100.0f);
            continue;
        }
        if (event.kind != Kind::Attack && event.kind != Kind::Gravity) {
            out.effects |= 1u << unsigned(event.kind);
            if (event.kind == Kind::Unknown) out.issues |= Unsupported;
            continue;
        }
        AttackState hit = enemy; hit.enrageTurns = 1; hit.enrage = enrageUpper; hit.addition = 0;
        Number raw;
        if (event.kind == Kind::Gravity) {
            if (playerHPUpper < 1) { out.issues |= Missing; continue; }
            Number fixed = Gravity(playerHPUpper,event.value);
            raw = fixed.known ? RawHit(hit,0,fixed.value) : Number{};
        } else raw = RawHit(hit,event.value);
        if (!raw.known) { out.issues |= Arithmetic; continue; }
        out.damage += raw.value; ++out.hits;
    }
    if (out.hits) out.damage += additionUpper;
    // A branch exceeding the native signed damage range is not modeled as
    // a wrapped value or an invented saturation.
    if (out.damage > INT32_MAX) out.issues |= Arithmetic;
    out.known = !out.issues;
    return out;
}

// One already-established event branch, with a separately proven reduction
// chain for EACH hit. Used by defense-candidate checks; no live capture can
// manufacture these proof flags from an optional observation.
struct HitContext {
    std::array<float,8> reductions{};
    uint8_t count = 0;
    bool known = false;
};
struct SurvivalInput {
    int32_t hpAfterPlayer = 0, reserve = 0;
    AttackState enemy;
    std::array<HitContext,MaxEvents> hitContexts{};
    bool branchKnown = false, postPlayerKnown = false, nextFloorCovered = false, rescueReady = false;
};
struct Survival {
    int32_t hpLower = 0;
    unsigned failingEvent = 0;
    uint32_t issues = 0;
    bool known = false, safe = false;
};
inline Survival Evaluate(const Sequence &sequence, SurvivalInput input) {
    Survival out; out.hpLower = input.hpAfterPlayer; out.issues = sequence.issues;
    if (!input.branchKnown || !input.postPlayerKnown || !input.nextFloorCovered || !input.rescueReady ||
        !input.enemy.known || input.hpAfterPlayer <= 0 || input.reserve < 0 || out.issues) {
        out.issues |= Missing; return out;
    }
    for (unsigned i = 0; i < sequence.count; ++i) {
        out.failingEvent = i + 1;
        const auto &event = sequence.events[i];
        if (event.optional) { out.issues |= Unsupported; return out; } // Branch expansion is the caller's job.
        if (event.kind == Kind::Enrage) {
            input.enemy.enrageTurns = int16_t(event.maximum); input.enemy.enrage = float(event.value) / 100.0f; continue;
        }
        if (event.kind != Kind::Attack && event.kind != Kind::Gravity) { out.issues |= Unsupported; return out; }
        const auto &context = input.hitContexts[i];
        if (!context.known || context.count > context.reductions.size()) { out.issues |= Missing; return out; }
        Number damage;
        if (event.kind == Kind::Gravity) {
            const Number fixed = Gravity(out.hpLower,event.value);
            damage = fixed.known ? RawHit(input.enemy,0,fixed.value) : Number{};
        } else damage = RawHit(input.enemy,event.value);
        for (unsigned layer = 0; damage.known && layer < context.count; ++layer) damage = Reduce(damage.value,context.reductions[layer]);
        if (!damage.known) { out.issues |= Arithmetic; return out; }
        if (damage.value >= out.hpLower) { out.hpLower = 0; out.known = true; return out; }
        out.hpLower -= damage.value;
    }
    out.known = true; out.safe = out.hpLower > input.reserve; out.failingEvent = 0;
    return out;
}
} // namespace PHEnemyThreat
