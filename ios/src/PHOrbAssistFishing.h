#pragma once
#include "PHOrbTimedFishing.h"

// Legacy recovery-only entry points; every release color must pass. Shared
// planning types are also used by PHOrbMultiFishing, whose Session caller
// converts validated exchanges to PHOrbRoute and confirms native input.
// Timing values are supplied assumptions, not device authorization.
constexpr unsigned PHOrbAssistFishingMaxExchanges = 6, PHOrbAssistFishingMaxPlans = 12;
constexpr unsigned PHOrbAssistFishingMaxInventoryStates = 8192;

struct PHOrbAssistFishingPlan {
    PHOrbGoalSet goals{};
    uint8_t lCounts[5]{}, take[PHOrbBasicTypes]{}, leave[PHOrbBasicTypes]{};
    uint8_t exchangeCount = 0, providers = 0;
    uint32_t predictedTurns = 0;
    unsigned comboUpper = 0;
    uint8_t geometryExtraExchanges = 0; // Search provenance only, not native permission.
};
struct PHOrbAssistFishingInventory {
    // Complete only for the restricted net-inventory enumeration described
    // below. Goal/selection/state caps are separate from path feasibility.
    bool valid = false, enumerationComplete = false, selectionTruncated = false, goalLimit = false;
    bool stateLimitReached = false, deadlineReached = false;
    unsigned visitedStates = 0, feasiblePlans = 0, count = 0;
    // Minimum among discovered plans only; the sentinel means none retained.
    unsigned minimumExchanges = PHOrbAssistFishingMaxExchanges + 1;
    uint8_t fixed[PHOrbBasicTypes]{};
    PHOrbAssistFishingPlan plans[PHOrbAssistFishingMaxPlans]{};
    // Combined counts include geometry; enumerationComplete and the earlier
    // goal/state/deadline flags describe baseline enumeration only.
    unsigned geometryRequestedExtra = 0, geometryVisitedStates = 0, geometryFeasiblePlans = 0, geometrySelectedPlans = 0;
    bool geometrySelectionTruncated = false, geometryStateLimitReached = false;
    bool geometryDeadlineReached = false, geometrySlotLimitReached = false;
};
struct PHOrbAssistFishingRoute {
    uint8_t count = 0, cells[PHOrbMaxSteps + 1]{};
    uint8_t fishCell = 0, exchangeCount = 0;
    bool diagonal = false;
    PHOrbTimedFishingStep exchanges[PHOrbAssistFishingMaxExchanges]{};
    PHOrbAssistFishingPlan plan{};
};
struct PHOrbAssistFishingResult {
    PHOrbAssistFishingRoute route{};
    PHOrbEvaluation evaluation{};
    PHOrbTimedFishingReservation reservation{};
    PHOrbAssistFishingInventory inventory{};
    uint32_t evaluated = 0, elapsedMS = 0;
    uint8_t plansSearched = 0, plansTimeRejected = 0, exploredDepth = 0, planSlicesExpired = 0;
    bool searchBudgetExhausted = false, deadlineReached = false;
    uint8_t geometryPlansSearched = 0, geometryPlansTimeRejected = 0, geometryPlanSlicesExpired = 0;
    uint8_t plansBudgetSkipped = 0, geometryPlansBudgetSkipped = 0;
};

// Enumerates minimal L allocations and unique net color inventories, filling
// required deficits before extra transfers for Combo/multi-color conditions.
// A color is never both taken and sacrificed. Supersets of a passing inventory
// are omitted, and capped enumeration/selection can miss routes. Counts are
// necessary bounds only: no location, path or timing claim follows from them.
PHOrbAssistFishingInventory PHOrbPlanAssistFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, unsigned maxExchanges = PHOrbAssistFishingMaxExchanges) noexcept;

// Full-route validation precedes every prefix return. Token fields travel with
// swaps; the spinner's later color is not written into the replayed board.
bool PHOrbReplayAssistFishing(const PHOrbBoard &board, const PHOrbAssistFishingRoute &route,
    unsigned through, PHOrbBoard &result) noexcept;
// Rebuilds goals from the base request and current recovery evidence, checks
// the actual exchanges, then evaluates all native spinner colors together.
bool PHOrbEvaluateAssistFishing(const PHOrbBoard &board, const PHOrbAssistFishingRoute &route,
    const PHOrbSolveOptions &options, const PHOrbAssistRecoveryState &recovery, PHOrbEvaluation &result) noexcept;
PHOrbTimedFishingReservation PHOrbReserveAssistFishing(const PHOrbBoard &board,
    const PHOrbAssistFishingRoute &route, const PHOrbTimedFishingTiming &timing) noexcept;

// Joint beam search chooses the take/sacrifice order from each net inventory.
// Inventory, all retained L choices, search and full replay share one deadline.
// Zero routes / bounded misses return NoRoute, never a proof of no solution.
// Solved requires all goals in every color and the assumed frame reservation.
// Checks run between bounded operations; a single operation may cross the
// deadline, in which case final validation cannot publish a late route.
PHOrbSolveStatus PHOrbSolveAssistFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, const PHOrbTimedFishingTiming &timing, unsigned maxExchanges,
    const std::atomic_bool &cancel, PHOrbAssistFishingResult &result) noexcept;
