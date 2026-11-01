#pragma once
#include <cstdint>
#include <cmath>
#include <limits>

// Local field checkpoints only. The caller supplies a state AFTER the native
// damage resolver and an already resolved damage value. Neither raw team
// damage nor a captured pre-move state establishes that input in a real turn.
namespace PHEnemyDamage {
constexpr unsigned Version = 1, MaxReadBytes = 4096, MaxReads = 64, MaxEnemies = 8;
constexpr uint32_t SpecialMarker = 986027508u;
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53 &&
    std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::digits == 24, "native binary floating point");
struct Number { int64_t value = 0; bool known = false; };
struct Context {
    uint32_t specialMarker = 0;
    uint8_t immediate = 0;
    bool known = false, actionsEnabled = false, callbackPresent = false;
};
struct Enemy {
    int64_t hp = 0, maximumHP = 0, snapshotHP = 0, secondarySnapshotHP = 0;
    uint64_t fixedSuperHP = 0;
    int32_t countdown = 0, selected = 0, special = -1;
    uint16_t flags = 0, normalPercent = 0, superTriggerPercent = 0, superFloorPercent = 0, hpInterval = 0;
    int16_t hpThreshold = 0, countThreshold = 0, countInterval = 0;
    uint8_t survival = 0, phase = 0, active = 0;
    int8_t separateHP = 0;
    bool known = false;
};
inline bool Valid(const Enemy &e) { return e.known && e.maximumHP > 0 && e.hp >= 0 && e.hp <= e.maximumHP; }
inline Number RoundThreshold(double value) {
    // FCVTZS after adding +0.5. Out-of-range signed conversion is deliberately
    // unknown, not C++ undefined behavior or an invented native saturation.
    volatile double rounded = value + 0.5;
    if (!std::isfinite(value) || value < 0 || !std::isfinite(rounded) || rounded >= 0x1p63) return {};
    return {int64_t(rounded), true};
}
inline Number NormalThreshold(const Enemy &e) {
    if (!e.normalPercent) return {0, true};
    if (e.maximumHP <= 0) return {};
    // Keep the native order: percentage / 100, THEN multiply maximum HP.
    volatile double fraction = double(e.normalPercent) / 100.0;
    volatile double value = fraction * double(e.maximumHP);
    return RoundThreshold(value);
}
inline Number SuperThreshold(const Enemy &e, bool floor) {
    if (!e.superTriggerPercent) return {0, true};
    if (e.fixedSuperHP) return RoundThreshold(double(e.fixedSuperHP));
    if (e.maximumHP <= 0) return {};
    // Unlike normal resolve, multiply BEFORE dividing by 100.
    volatile double product = double(floor ? e.superFloorPercent : e.superTriggerPercent) * double(e.maximumHP);
    volatile double value = product / 100.0;
    return RoundThreshold(value);
}
struct PhaseCheck { bool known = false, triggered = false; int32_t percent = 0; bool percentKnown = false; };
inline PhaseCheck CheckHPPhase(const Context &c, Enemy &e) {
    PhaseCheck out;
    if (!c.known || !e.known) return out;
    // Protected signed-byte (value & 0xffffffdf)==0 admits only 0 and 0x20.
    if (e.phase != 0 && e.phase != 0x20) { out.known = true; return out; }
    if ((e.flags & 0x100u) || e.hp <= 0 || e.hpThreshold < 1) { out.known = true; return out; }
    if (e.maximumHP <= 0) return out;
    volatile double ratio = double(e.hp) / double(e.maximumHP);
    volatile double percentage = ratio * 100.0;
    float value = float(percentage); // Native FCVT to float before its ceil-like helper.
    if (!std::isfinite(value) || value < 0 || value >= 0x1p31f) return out;
    const int32_t truncated = int32_t(value);
    if (value > 0 && float(truncated) != value) value += 1.0f;
    if (value >= 0x1p31f) return out;
    out.percent = int32_t(value); out.percentKnown = out.known = true;
    if (out.percent <= e.hpThreshold) {
        e.phase |= 1u;
        if (c.immediate == 1) e.phase |= 4u;
        out.triggered = true;
    }
    return out;
}
enum Event : uint32_t {
    HPPhase = 1u, SuperTriggered = 2u, SuperPreserved = 4u, NormalTriggered = 8u,
    NormalPreserved = 16u, SpecialCountdownZero = 32u, CleanupInvoked = 64u,
    NormalCleared = 128u, SuperCleared = 256u, SnapshotRewritten = 512u
};
enum class Status : uint8_t {
    UnknownInput, Parts, SpecialUnresolved, EffectiveDamageUnknown, SeparateHPUnresolved,
    NumericUnresolved, CallbackUnresolved, ConditionalCheckpoint
};
inline const char *StatusName(Status s) {
    switch (s) {
        case Status::UnknownInput: return "unknown-input";
        case Status::Parts: return "parts-unmodeled";
        case Status::SpecialUnresolved: return "special-predicate-unresolved";
        case Status::EffectiveDamageUnknown: return "effective-damage-unresolved";
        case Status::SeparateHPUnresolved: return "separate-hp-unmodeled";
        case Status::NumericUnresolved: return "numeric-conversion-unresolved";
        case Status::CallbackUnresolved: return "hp-callback-unresolved";
        case Status::ConditionalCheckpoint: return "conditional-checkpoint";
    }
    return "unknown-input";
}
struct Checkpoint {
    Enemy state{};
    Status status = Status::UnknownInput;
    uint32_t events = 0;
    int64_t hpBeforeSurvival = 0;
    uint8_t phaseBeforeSurvival = 0;
    bool prefixKnown = false, known = false;
};
inline bool CleanupFields(const Context &c, Enemy &e, uint32_t &events) {
    // The real routine loops over ALL enemies. This is only one enemy's field
    // effect, conditional on external animation callbacks preserving fields.
    if (e.active != 1 || ((e.survival & 1u) && (e.flags & 2u))) return true;
    if (e.survival & 1u) {
        e.survival &= uint8_t(~1u); events |= NormalCleared;
        if (!(c.immediate & 1u)) e.flags |= 0x20u;
        const auto phase = CheckHPPhase(c, e);
        if (!phase.known) return false;
        if (phase.triggered) events |= HPPhase;
    }
    if (e.survival & 8u) {
        e.survival &= uint8_t(~8u); events |= SuperCleared;
        if (!(c.immediate & 1u)) e.flags |= 0x80u;
        const auto phase = CheckHPPhase(c, e);
        if (!phase.known) return false;
        if (phase.triggered) events |= HPPhase;
    }
    return true;
}
inline Checkpoint ConditionalCleanup(const Context &c, const Enemy &input) {
    Checkpoint out; out.state = input;
    if (!c.known || !Valid(input)) return out;
    if (input.flags & 0x100u) { out.status = Status::Parts; return out; }
    out.events |= CleanupInvoked;
    out.known = CleanupFields(c, out.state, out.events);
    out.status = out.known ? Status::ConditionalCheckpoint : Status::NumericUnresolved;
    return out;
}
inline Checkpoint ConditionalBatchSnapshot(const Enemy &input) {
    Checkpoint out; out.state = input;
    if (!Valid(input)) return out;
    if (input.flags & 0x100u) { out.status = Status::Parts; return out; }
    // Local fields from 10060583c only; no claim about when it will be called.
    out.state.snapshotHP = out.state.secondarySnapshotHP = input.hp;
    out.state.survival = 0; out.state.flags &= uint16_t(~0x80u);
    out.events = SnapshotRewritten; out.known = true; out.status = Status::ConditionalCheckpoint;
    return out;
}
struct ResolvedHit {
    int64_t damage = 0;
    bool known = false, bypassSeparateHP = false;
    // known means the resolver permits HP settlement, including its absorption
    // output. A numeric return by itself does not satisfy this contract.
};
inline Checkpoint AfterResolvedHit(const Context &c, const Enemy &input, const ResolvedHit &hit) {
    Checkpoint out; out.state = input;
    if (!c.known || !Valid(input)) return out;
    if (input.flags & 0x100u) { out.status = Status::Parts; return out; }
    if (c.specialMarker == SpecialMarker) { out.status = Status::SpecialUnresolved; return out; }
    if (!hit.known) { out.status = Status::EffectiveDamageUnknown; return out; }
    if (input.separateHP > 0 && !hit.bypassSeparateHP) { out.status = Status::SeparateHPUnresolved; return out; }
    auto &e = out.state;
    // Preserve native 64-bit SUB and signed clamp without signed C++ overflow.
    const uint64_t difference = uint64_t(e.hp) - uint64_t(hit.damage);
    e.hp = difference & (uint64_t(1) << 63) ? 0 :
        difference > uint64_t(e.maximumHP) ? e.maximumHP : int64_t(difference);
    out.hpBeforeSurvival = e.hp;
    const auto phase = CheckHPPhase(c, e); // BEFORE either survival floor.
    if (!phase.known) { out.status = Status::NumericUnresolved; return out; }
    out.phaseBeforeSurvival = e.phase; out.prefixKnown = true;
    if (phase.triggered) out.events |= HPPhase;
    if (hit.damage > 0 && c.callbackPresent) { out.status = Status::CallbackUnresolved; return out; }
    auto immediateCleanup = [&]() {
        if (c.immediate != 1) return true; // Exact comparison, not bit0.
        out.events |= CleanupInvoked;
        return CleanupFields(c, e, out.events);
    };
    auto rewriteSnapshot = [&](int64_t hp) {
        e.snapshotHP = e.secondarySnapshotHP = hp; out.events |= SnapshotRewritten;
    };
    if (e.superTriggerPercent) {
        const auto floor = SuperThreshold(e, true);
        if (!floor.known) { out.status = Status::NumericUnresolved; return out; }
        if (e.survival & 8u) {
            e.hp = floor.value; out.events |= SuperPreserved;
        } else if (e.hp < floor.value) {
            const auto threshold = SuperThreshold(e, false);
            if (!threshold.known) { out.status = Status::NumericUnresolved; return out; }
            if (e.snapshotHP > threshold.value) { // Super resolve is STRICT >.
                e.survival |= 8u; e.hp = floor.value; out.events |= SuperTriggered;
                if (!immediateCleanup()) { out.status = Status::NumericUnresolved; return out; }
                if (c.immediate == 1 && floor.value >= 1) rewriteSnapshot(floor.value);
            }
        }
    }
    if (e.survival & 1u) {
        e.hp = 1; out.events |= NormalPreserved;
    } else if (e.hp <= 0) {
        if (e.special >= 0 && c.actionsEnabled) {
            e.survival |= 1u; e.flags |= 2u; e.countdown = 0; e.hp = 1;
            out.events |= SpecialCountdownZero;
            if (!immediateCleanup()) { out.status = Status::NumericUnresolved; return out; }
            if (c.immediate == 1) rewriteSnapshot(1);
        }
        // Still in the ORIGINAL lethal block, even after special survival.
        if (e.normalPercent) {
            const auto threshold = NormalThreshold(e);
            if (!threshold.known) { out.status = Status::NumericUnresolved; return out; }
            if (e.snapshotHP >= threshold.value) { // Normal resolve admits equality.
                e.survival |= 1u; e.hp = 1; out.events |= NormalTriggered;
                if (!immediateCleanup()) { out.status = Status::NumericUnresolved; return out; }
                if (c.immediate == 1) rewriteSnapshot(1);
            }
        }
    }
    // End before shadow HP copies and later damage bookkeeping/animations.
    out.known = true; out.status = Status::ConditionalCheckpoint;
    return out;
}
struct PhaseEntry {
    uint8_t phase = 0;
    bool known = false, countTriggered = false, targetRequested = false;
};
inline PhaseEntry ConditionalPhaseEntry(const Context &c, const Enemy &e, unsigned activeByteSum, bool sumKnown) {
    PhaseEntry out; out.phase = e.phase;
    // Assumes the post-damage scan reaches this slot: no earlier target/return.
    // The sum uses raw byte+1 from ALL slots, including parts; it is not HP.
    if (!c.known || !Valid(e) || !sumKnown || activeByteSum > 255u * MaxEnemies || c.specialMarker == SpecialMarker) return out;
    out.known = true;
    if ((e.flags & 0x100u) || e.hp <= 0) return out;
    if (e.countThreshold >= 1 && !(e.phase & 0x22u) && activeByteSum <= unsigned(e.countThreshold)) {
        out.phase |= 0x11u;
        if (c.immediate == 1) out.phase |= 4u;
        out.countTriggered = true;
    }
    if ((out.phase & 1u) && !(out.phase & 2u)) { out.phase |= 2u; out.targetRequested = true; }
    return out;
}
inline Checkpoint ConditionalPhaseAnimation(const Enemy &target) {
    Checkpoint out; out.state = target;
    // Assumes this is the native target and the animation wait has completed.
    if (!Valid(target)) return out;
    if (target.flags & 0x100u) { out.status = Status::Parts; return out; }
    out.state.countdown = !(target.phase & 4u) ? 0 :
        target.phase & 0x10u ? int32_t(target.countInterval) : int32_t(target.hpInterval);
    out.state.selected = 0; out.state.phase |= 8u;
    out.known = true; out.status = Status::ConditionalCheckpoint;
    return out;
}
} // namespace PHEnemyDamage
