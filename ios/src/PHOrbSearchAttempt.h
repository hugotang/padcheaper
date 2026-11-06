#pragma once
#include "PHOrbSolver.h"

// Session search-attempt policy only. This never changes goals, authorizes input or
// infers a deadline from wall time when the solver returned no candidate.
enum class PHOrbSoloRetryDecision { Ineligible, SearchCompleted, Retry };

static inline bool PHOrbHasSearchCandidate(PHOrbSolveStatus status, const PHOrbRoute &route) {
    return route.count >= 2 && route.count <= PHOrbMaxSteps + 1 &&
        (status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet || status == PHOrbSolveStatus::Unsafe);
}

static inline PHOrbSoloRetryDecision PHOrbDecideSoloRetry(bool solo, bool assistCandidates,
    bool cancelled, unsigned initialBudgetMS, PHOrbSolveStatus status, const PHOrbRoute &route) {
    if (!solo || assistCandidates || cancelled || initialBudgetMS >= PHOrbSoloRetryMilliseconds ||
        (status != PHOrbSolveStatus::GoalsUnmet && status != PHOrbSolveStatus::Unsafe) ||
        route.count < 2 || route.count > PHOrbMaxSteps + 1) return PHOrbSoloRetryDecision::Ineligible;
    return route.deadlineReached ? PHOrbSoloRetryDecision::Retry : PHOrbSoloRetryDecision::SearchCompleted;
}

// Keep a useful failed candidate for diagnostics, without treating it as an
// executable result. The session still replays it and checks all input gates.
// On equal progress retain the later attempt so an extended search is visible.
static inline bool PHOrbPreferSearchCandidate(PHOrbSolveStatus status, const PHOrbRoute &candidate,
    PHOrbSolveStatus selectedStatus, const PHOrbRoute &selected) {
    if (!PHOrbHasSearchCandidate(status, candidate)) return false;
    if (status == PHOrbSolveStatus::Solved && candidate.goalsMet && candidate.hazardsSafe) return true;
    if (selectedStatus == PHOrbSolveStatus::Solved && selected.goalsMet && selected.hazardsSafe) return false;
    if (!PHOrbHasSearchCandidate(selectedStatus, selected)) return true;
    if (candidate.hazardsSafe != selected.hazardsSafe) return candidate.hazardsSafe;
    return candidate.matchedGoals >= selected.matchedGoals;
}

// Each fishing colour gets its own deadline decision. A failed ordinary
// inventory check says nothing about whether the later fishing search timed
// out. The callback records/selects each attempt with these exact options.
template <typename Attempt>
static inline void PHOrbSearchWithSoloRetry(PHOrbSolveOptions options, bool solo, bool assistCandidates,
    const std::atomic_bool &cancel, Attempt attempt) {
    if (cancel.load(std::memory_order_relaxed)) return;
    PHOrbRoute candidate{};
    const auto status = attempt(options, candidate, false);
    if (PHOrbDecideSoloRetry(solo, assistCandidates, cancel.load(std::memory_order_relaxed),
            options.milliseconds, status, candidate) != PHOrbSoloRetryDecision::Retry) return;
    options.milliseconds = PHOrbSoloRetryMilliseconds;
    candidate = {};
    attempt(options, candidate, true);
}
