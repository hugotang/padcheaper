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

// Multi-take fishing splits one budget across spinner cells, inventory plans
// and a geometry reserve, so a short slice can stop a plan's beam before it
// reaches full depth (2026-11-08 Himmel log: 400 ms per cell failed, the same
// board solved in ~250 ms once a plan had the time). A solo board, whose move
// timer starts only at the grab, reruns all cells once with the existing solo
// retry budget when any attempt was cut by time; the retry searches baseline
// inventories only, without the geometry reserve. Completed searches, cancelled
// or multiplayer searches and an already extended budget never retry.
static inline bool PHOrbShouldRetryMultiFishing(bool solo, bool cancelled, unsigned initialBudgetMS,
    bool solved, bool timeLimited) {
    return solo && !cancelled && !solved && timeLimited && initialBudgetMS < PHOrbSoloRetryMilliseconds;
}

// Keep a useful failed candidate for diagnostics, without treating it as an
// executable result. The session still replays it and checks all input gates.
// On equal progress retain the later attempt so an extended search is visible.
// When both attempts use identical goals, compare progress within each clause
// too: four of five colours must not be replaced by three merely because both
// candidates completed the separate heart clause.
static inline bool PHOrbComparableSearchGoals(const PHOrbGoalSet &a, const PHOrbGoalSet &b) {
    if (!a.count || a.count > PHOrbMaxGoals || a.count != b.count ||
        a.comboFirstWave != b.comboFirstWave || a.bonusFirstWave != b.bonusFirstWave) return false;
    for (unsigned g = 0; g < a.count; ++g) {
        const auto &x = a.items[g], &y = b.items[g];
        if (!x.count || x.kind != y.kind || x.colors != y.colors || x.count != y.count || x.length != y.length) return false;
    }
    return true;
}
static inline unsigned PHOrbSearchGoalProgress(const PHOrbRoute &route, const PHOrbGoalSet &goals) {
    if (goals.count > PHOrbMaxGoals) return 0;
    unsigned progress = 0;
    for (unsigned g = 0; g < goals.count; ++g) {
        const unsigned required = goals.items[g].count, hits = route.goalHits[g];
        if (!required) return 0;
        progress += 4096u * (hits < required ? hits : required) / required;
    }
    return progress;
}
static inline bool PHOrbPreferSearchCandidate(PHOrbSolveStatus status, const PHOrbRoute &candidate,
    PHOrbSolveStatus selectedStatus, const PHOrbRoute &selected,
    const PHOrbGoalSet *candidateGoals = nullptr, const PHOrbGoalSet *selectedGoals = nullptr) {
    if (!PHOrbHasSearchCandidate(status, candidate)) return false;
    if (status == PHOrbSolveStatus::Solved && candidate.goalsMet && candidate.hazardsSafe) return true;
    if (selectedStatus == PHOrbSolveStatus::Solved && selected.goalsMet && selected.hazardsSafe) return false;
    if (!PHOrbHasSearchCandidate(selectedStatus, selected)) return true;
    if (candidate.hazardsSafe != selected.hazardsSafe) return candidate.hazardsSafe;
    if (candidate.matchedGoals != selected.matchedGoals) return candidate.matchedGoals > selected.matchedGoals;
    if (candidateGoals && selectedGoals && PHOrbComparableSearchGoals(*candidateGoals,*selectedGoals) &&
        candidate.goalCount == candidateGoals->count && selected.goalCount == selectedGoals->count) {
        const unsigned a = PHOrbSearchGoalProgress(candidate,*candidateGoals);
        const unsigned b = PHOrbSearchGoalProgress(selected,*selectedGoals);
        if (a != b) return a > b;
    }
    return true;
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
