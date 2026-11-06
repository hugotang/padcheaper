#pragma once
#include "PHOrbSolver.h"

// Session retry policy only. This never changes goals, authorizes input or
// infers a deadline from wall time when the solver returned no candidate.
enum class PHOrbSoloRetryDecision { Ineligible, SearchCompleted, Retry };

static inline PHOrbSoloRetryDecision PHOrbDecideSoloRetry(bool solo, bool assistCandidates,
    bool cancelled, unsigned initialBudgetMS, PHOrbSolveStatus status, const PHOrbRoute &route) {
    if (!solo || assistCandidates || cancelled || initialBudgetMS >= PHOrbSoloRetryMilliseconds ||
        (status != PHOrbSolveStatus::GoalsUnmet && status != PHOrbSolveStatus::Unsafe) ||
        route.count < 2 || route.count > PHOrbMaxSteps + 1) return PHOrbSoloRetryDecision::Ineligible;
    return route.deadlineReached ? PHOrbSoloRetryDecision::Retry : PHOrbSoloRetryDecision::SearchCompleted;
}
