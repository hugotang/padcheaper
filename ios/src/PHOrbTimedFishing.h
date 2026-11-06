#pragma once
#include "PHOrbSolver.h"

// Offline model only. These types are deliberately not PHOrbRoute: a result
// is conditional on the spinner color at native match capture, not permission
// to submit a gesture. No Session/native caller consumes these results.
constexpr unsigned PHOrbTimedFishingMaxExchanges = 2, PHOrbTimedFishingMaxPlans = 8;

struct PHOrbTimedFishingExchange {
    uint8_t take = 0, leave = 0;
};
struct PHOrbTimedFishingInventoryPlan {
    uint8_t count = 0, releaseColor = 0;
    PHOrbTimedFishingExchange exchanges[PHOrbTimedFishingMaxExchanges]{};
    unsigned comboUpper = 0;
};
struct PHOrbTimedFishingInventory {
    bool valid = false;
    // MaxExchanges + 1 means no inventory plan within the requested limit.
    unsigned minimumExchanges = PHOrbTimedFishingMaxExchanges + 1;
    unsigned feasiblePlans = 0, count = 0;
    PHOrbTimedFishingInventoryPlan plans[PHOrbTimedFishingMaxPlans]{};
};
struct PHOrbTimedFishingStep {
    uint8_t entry = 0, exit = 0, take = 0, leave = 0;
};
struct PHOrbTimedFishingRoute {
    uint8_t count = 0, cells[PHOrbMaxSteps + 1]{};
    uint8_t fishCell = 0, exchangeCount = 0, releaseColors = 0;
    bool diagonal = false;
    PHOrbTimedFishingStep exchanges[PHOrbTimedFishingMaxExchanges]{};
    PHOrbEvaluation evaluation{}; // Minimum across releaseColors only.
    uint32_t evaluated = 0, elapsedMS = 0;
    uint8_t plansSearched = 0, exploredDepth = 0, planSlicesExpired = 0;
    uint8_t evaluatedReleaseColors = 0; // Solver validation coverage; may be partial.
    bool searchBudgetExhausted = false, deadlineReached = false;
};

// A single unlocked basic-color spinner; no locked or thorn tokens anywhere.
// Tape/fixed-start/diagonal and existing erase/unmatchable rules still apply.
// options.fishCell is required; fishColor is selected by this planner. Zero
// exchanges permits phase-only analysis. Activity options must be defaults.
// Recovery/thorn ranking options are unused. No options are read from a device.
PHOrbTimedFishingInventory PHOrbInspectTimedFishing(const PHOrbBoard &board,
    const PHOrbSolveOptions &options, unsigned maxExchanges = PHOrbTimedFishingMaxExchanges) noexcept;

// Validates the whole route, returning the requested prefix with all token
// fields swapped. A spinner visit needs a following exit; release from the
// held spinner is not modeled. No color changes merely by calling through==last:
// releaseColors is a later, explicitly conditional evaluation requirement.
bool PHOrbReplayTimedFishing(const PHOrbBoard &board, const PHOrbTimedFishingRoute &route,
    unsigned through, PHOrbBoard &result) noexcept;
bool PHOrbEvaluateTimedFishing(const PHOrbBoard &board, const PHOrbTimedFishingRoute &route,
    const PHOrbSolveOptions &options, PHOrbEvaluation &result) noexcept;

// Offline arithmetic only: these caller-supplied assumptions are not native
// timing evidence. Reserve a full advancing cycle per take, and another if
// capture depends on a subset of colors. Settle time is charged before each
// wait; inputAndCaptureFrames also covers grab/release and capture uncertainty.
struct PHOrbTimedFishingTiming {
    uint32_t remainingFrames = 0, framesPerMove = 0;
    uint32_t settleFramesPerWait = 0, inputAndCaptureFrames = 0;
    uint16_t spinnerPeriodFrames = 0;
};
struct PHOrbTimedFishingReservation {
    bool valid = false, fits = false; // fits is never a live execution gate.
    uint32_t moveFrames = 0, takeWaitFrames = 0, releaseWaitFrames = 0;
    uint32_t settleFrames = 0, requiredFrames = 0;
};
PHOrbTimedFishingReservation PHOrbReserveTimedFishing(const PHOrbBoard &board,
    const PHOrbTimedFishingRoute &route, const PHOrbTimedFishingTiming &timing) noexcept;

// Separate bounded beam search over the retained inventory plans. Uses the
// supplied step/beam/total-time limits and cancellation, not ordinary recovery
// or solo retries; a small part of the same budget is reserved for validation.
// Clocks are checked between bounded operations, so a single operation can
// cross the deadline. Solved means a replayed conditional layout, never live
// timing validation. releaseColors can be a subset when validation runs out of
// time. The two-exchange/eight-plan caps can miss otherwise legal routes.
PHOrbSolveStatus PHOrbSolveTimedFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    unsigned maxExchanges, const std::atomic_bool &cancel, PHOrbTimedFishingRoute &route) noexcept;
