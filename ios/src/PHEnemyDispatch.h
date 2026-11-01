#pragma once
#include "PHEnemyActions.h"

// Value-only checkpoints, conditional on an unchanged captured context. These
// are not a player-turn simulation or an authorization to survive an attack.
namespace PHEnemyDispatch {
namespace EA = PHEnemyActions;
constexpr unsigned Version = 1, MaxReadBytes = 4096, MaxReads = 64;
constexpr uint32_t SpecialMarker = 986027508u, TickExcludedState = 525190u;
enum class Redirect : uint8_t { Unknown, Inactive, OtherAction, RowChange };
enum class Route : uint8_t {
    Unknown, Parts, Inactive, Countdown, FlagBlocked, PreDispatchHook,
    NormalAttackOnly, CompoundContinuation, CachedAction, SpecialUnresolved,
    SelectorFallback, ActionOverride, SpecialAction, RedirectedRows, OrdinaryRows
};
struct Context {
    uint32_t specialMarker = 0, loopState = 0;
    uint8_t cacheMode = 0, preDispatchHook = 0;
    bool known = false, actionsEnabled = false;
    Redirect redirect = Redirect::Unknown;
    int8_t redirectSlot = -1;
    int32_t redirectRow = 0; // Native one-based parameter, before subtracting 1.
};
struct Enemy {
    uint64_t hp = 0;
    uint32_t compound = 0, overrideState = 0;
    int32_t countdown = 0, selected = -1, special = -1, attackAddition = 0;
    uint16_t flags = 0;
    int16_t overrideAction = 0;
    uint8_t survivalFlags = 0;
    bool known = false, ordered = true, interpolationEqual = false;
};
struct Entry {
    Route route = Route::Unknown;
    int32_t selectedAfterClear = -1, directSelection = -1, redirectRow = 0;
    bool cacheCleared = false, directSelectionKnown = false;
};
inline const char *RouteName(Route r) {
    switch (r) {
        case Route::Unknown: return "unknown";
        case Route::Parts: return "parts-skipped";
        case Route::Inactive: return "inactive-skipped";
        case Route::Countdown: return "countdown-pending";
        case Route::FlagBlocked: return "native-flag-8-skipped";
        case Route::PreDispatchHook: return "pre-dispatch-hook-unresolved";
        case Route::NormalAttackOnly: return "normal-attack-without-selector";
        case Route::CompoundContinuation: return "compound-continuation";
        case Route::CachedAction: return "cached-action";
        case Route::SpecialUnresolved: return "special-mode-predicate-unresolved";
        case Route::SelectorFallback: return "selector-negative-fallback";
        case Route::ActionOverride: return "override-action";
        case Route::SpecialAction: return "ordered-special-action";
        case Route::RedirectedRows: return "redirected-row-entry";
        case Route::OrdinaryRows: return "ordinary-row-zero";
    }
    return "unknown";
}
inline Entry Classify(const Context &context, const Enemy &enemy, unsigned slot) {
    Entry out;
    if (!context.known || !enemy.known || slot >= EA::MaxEnemies) return out;
    out.selectedAfterClear = enemy.selected;
    // Scheduler tests bit 0; dispatcher below tests exact equality to 1.
    out.cacheCleared = !(context.cacheMode & 1u) && !(enemy.compound & 0x100u) && enemy.interpolationEqual;
    if (out.cacheCleared) out.selectedAfterClear = -1;
    if (enemy.flags & 0x100u) out.route = Route::Parts;
    else if (!enemy.hp && !((enemy.survivalFlags & 1u) && (enemy.flags & 2u))) out.route = Route::Inactive;
    else if (enemy.countdown > 0) out.route = Route::Countdown;
    else if (enemy.flags & 8u) out.route = Route::FlagBlocked;
    else if (context.preDispatchHook == 1) out.route = Route::PreDispatchHook;
    else if (!context.actionsEnabled) out.route = Route::NormalAttackOnly;
    else if (enemy.compound & 0x100u) out.route = Route::CompoundContinuation;
    else if (context.cacheMode == 1 && out.selectedAfterClear >= 1) out.route = Route::CachedAction;
    else if (context.specialMarker == SpecialMarker) out.route = Route::SpecialUnresolved;
    else if (enemy.attackAddition > 0) {
        out.route = Route::SelectorFallback; out.directSelectionKnown = true;
    } else if (enemy.overrideState) {
        out.route = Route::ActionOverride; out.directSelectionKnown = true;
        out.directSelection = enemy.overrideAction;
    } else if (enemy.special >= 0 && (enemy.flags & 2u)) {
        out.route = enemy.ordered ? Route::SpecialAction : Route::SelectorFallback;
        out.directSelectionKnown = !enemy.ordered;
    } else if (!enemy.ordered || context.redirect == Redirect::Inactive || context.redirect == Redirect::OtherAction) {
        out.route = Route::OrdinaryRows;
    } else if (context.redirect == Redirect::RowChange) {
        if (context.redirectSlot != int(slot)) out.route = Route::OrdinaryRows;
        else if (context.redirectRow > int(EA::Rows)) {
            out.route = Route::SelectorFallback; out.directSelectionKnown = true;
        } else if (context.redirectRow > 0) {
            out.route = Route::RedirectedRows; out.redirectRow = context.redirectRow;
        } // Nonpositive row parameters are deliberately unresolved.
    }
    return out;
}
struct Tick {
    bool known = false, decremented = false;
    int32_t countdown = 0;
    uint8_t cacheMode = 0;
};
inline Tick ConditionalTick(const Context &context, const Enemy &enemy) {
    Tick out;
    // Assumes the native routine is reached and its external early-return
    // check does not stop it. HP/survival/phase/skills are NOT projected here.
    if (!context.known || !enemy.known) return out;
    out.known = true; out.countdown = enemy.countdown; // Cache always clears first.
    if (context.loopState != TickExcludedState && !(enemy.flags & 0x100u) &&
        (enemy.hp || (enemy.survivalFlags & 1u)) && enemy.countdown >= 1) {
        --out.countdown; out.decremented = true;
    }
    return out;
}
struct SelectionInput {
    uint32_t actionCount = 0, compound = 0;
    uint16_t budget = 0, protectedCount = 0;
    int16_t maximumBudget = 0, regeneration = 0;
    bool protectedCountKnown = false;
};
struct Selection {
    uint32_t actionCount = 0, compound = 0;
    uint16_t budgetAfterRegeneration = 0, budgetAtCheckpoint = 0, protectedCount = 0, chargedCost = 0;
    int32_t selected = -1;
    bool checkpointKnown = false, protectedCountKnown = false, costApplied = false, compoundWritten = false;
    bool negativeFallback = false, compoundPreparation = false;
};
inline Selection AfterFreshSelection(const SelectionInput &input, int32_t selected, const EA::Record *record) {
    Selection out;
    out.actionCount = input.actionCount + uint32_t(1);
    out.protectedCountKnown = input.protectedCountKnown;
    out.protectedCount = input.protectedCount;
    if (out.protectedCountKnown && (unsigned(out.protectedCount) >> 5) <= 0x752u) ++out.protectedCount;
    const int32_t sum = int32_t(input.budget) + int32_t(input.regeneration);
    // Native signed CMP/CSEL order, including a negative maximum. No C++
    // clamp with a presumed valid [0,max] interval, and no signed overflow.
    const int32_t upper = sum >= int32_t(input.maximumBudget) ? int32_t(input.maximumBudget) : sum;
    out.budgetAfterRegeneration = uint16_t(sum >= 0 ? upper : 0);
    out.budgetAtCheckpoint = out.budgetAfterRegeneration;
    out.selected = selected; out.compound = input.compound;
    if (selected < 0 || (selected > 0 && record && record->opcode() == 82)) {
        if (selected > 0) out.selected = -1;
        out.checkpointKnown = true; out.negativeFallback = true;
        return out;
    }
    // An uncaptured definition is not evidence that native lookup returns
    // null. ID zero can address the native table too; do not assume a skip.
    if (!record) return out;
    out.chargedCost = uint16_t(record->word(64));
    out.budgetAtCheckpoint = uint16_t(uint32_t(out.budgetAfterRegeneration) - uint32_t(out.chargedCost));
    out.costApplied = true;
    out.compound = (uint32_t(selected) << 9) | 0xf0u;
    out.compoundWritten = true; out.checkpointKnown = true;
    out.compoundPreparation = record->opcode() == 83;
    // Stops BEFORE ordinary/compound preparation, which may mutate again.
    return out;
}
} // namespace PHEnemyDispatch
