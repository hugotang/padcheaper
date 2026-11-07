#pragma once
#include "PHOrbAssistFishing.h"

// Shared ordinary/recovery multi-take planner. One selected spinner is used
// per route; other spinner cells remain blocked. The selected cell's color is
// evaluated independently of the untouched cells' shared phase, so no stale
// pre-exchange phase correlation authorizes native input. A result alone is
// not device timing evidence: every exchange still needs live confirmation.
using PHOrbMultiFishingPlan = PHOrbAssistFishingPlan;
using PHOrbMultiFishingInventory = PHOrbAssistFishingInventory;
using PHOrbMultiFishingRoute = PHOrbAssistFishingRoute;
using PHOrbMultiFishingResult = PHOrbAssistFishingResult;
constexpr unsigned PHOrbMultiFishingMaxExchanges = PHOrbAssistFishingMaxExchanges;
constexpr unsigned PHOrbMultiFishingMaxGeometryExtra = 2, PHOrbMultiFishingMaxGeometryPlans = 4;
constexpr unsigned PHOrbMultiFishingMaxGeometryStates = 512;

// nullptr keeps the complete ordinary goal set; a non-null recovery must be
// valid and active, and adds only currently eligible recovery L allocations.
// Nonbasic stationary/moved tokens are retained and evaluated under the normal
// poison/bomb policy, never treated as donors or allowed to enter the spinner.
// Optional geometry expansion adds up to two exchanges per selected passing
// inventory, within the original total cap. Baseline order/slots stay intact;
// extras share the original state/time limits and use at most four free slots.
PHOrbMultiFishingInventory PHOrbPlanMultiFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState *recovery = nullptr,
    unsigned maxExchanges = PHOrbMultiFishingMaxExchanges, unsigned geometryExtraExchanges = 0) noexcept;
bool PHOrbReplayMultiFishing(const PHOrbBoard &board, const PHOrbMultiFishingRoute &route,
    unsigned through, PHOrbBoard &result) noexcept;
bool PHOrbEvaluateMultiFishing(const PHOrbBoard &board, const PHOrbMultiFishingRoute &route,
    const PHOrbSolveOptions &options, const PHOrbAssistRecoveryState *recovery,
    PHOrbEvaluation &result) noexcept;
// A fully replayed final board no longer preserves the selected spinner's
// original phase relation. Evaluate every selected color against every shared
// phase of untouched spinners, retaining the complete supplied goal set.
// This is route verification, not proof of live settling or input permission.
bool PHOrbEvaluateMultiFishingBoard(const PHOrbBoard &board, unsigned fishCell,
    const PHOrbSolveOptions &options, PHOrbEvaluation &result) noexcept;
PHOrbTimedFishingReservation PHOrbReserveMultiFishing(const PHOrbBoard &board,
    const PHOrbMultiFishingRoute &route, const PHOrbTimedFishingTiming &timing) noexcept;
// Time-feasible retained geometry plans reserve one third of the remaining
// search time after inventory planning. Baseline plans run first; unused time
// is lent to geometry. This reallocates the same deadline, not extra CPU time.
PHOrbSolveStatus PHOrbSolveMultiFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState *recovery, const PHOrbTimedFishingTiming &timing, unsigned maxExchanges,
    const std::atomic_bool &cancel, PHOrbMultiFishingResult &result, unsigned geometryExtraExchanges = 0) noexcept;
