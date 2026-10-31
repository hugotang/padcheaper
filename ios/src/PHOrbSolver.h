#pragma once
#include "PHOrbModel.h"
#include <atomic>

struct PHOrbSolveOptions {
    unsigned steps = 32;
    unsigned beam = 256;
    unsigned milliseconds = 350;
    PHOrbGoalSet goals{};
    PHOrbThornState thorns{};
    unsigned recoveryPasses = 1;
    unsigned backtrackLimit = 0;
    bool thornComboPriority = false; // Rank Combo ahead of damage within the same strict survival gates.
    bool allowPoisonWithHeartFour = false; // Team policy: poison and mortal poison may clear with a first-wave heart four in every phase.
    bool diagonal = false; // Eight-way gesture moves; never diagonal matching.
};

enum class PHOrbSearchQuality { Fast, Balanced, Thorough };
PHOrbSolveOptions PHOrbOptionsForQuality(PHOrbSearchQuality quality) noexcept;

enum class PHOrbSolveStatus { Solved, GoalsUnmet, Unsafe, InsufficientOrbs, Cancelled, Invalid, NoRoute, Failed, IncompatibleGoals };
// Re-evaluate a released board. Shape goals require the first removal wave;
// combos/colors/connected-match counts may include deterministic cascades.
// Four means exactly four marked, connected orbs in a line; unmarked same-
// colour neighbours are allowed. L/T/cross/square retain their conservative
// same-colour border exclusion.
// Poison/mortal-poison matches are forbidden by default. An explicit team
// policy permits both poisons when every phase has a first-wave heart four;
// this accepts the team's recovery without estimating healing or poison damage.
// Every bomb must participate in the first removal wave. Spinners share a
// native clock; enumerate every common phase of their validated color cycle
// (up to ten phases), requiring
// every result to meet the goals and hazard policy. Native unmatchable colors,
// minimum runs and minimum connected matches apply to every cascade.
// Routes respect tape/fixed start and avoid spinners. Damaging thorn swaps
// require verified HP, a strict damage budget and a first-wave heart four.
// The budget ignores healing and resistance; these may improve real outcomes.
bool PHOrbEvaluate(const PHOrbBoard &board, const PHOrbGoalSet &goals, PHOrbEvaluation &result) noexcept;
bool PHOrbEvaluate(const PHOrbBoard &board, const PHOrbGoalSet &goals, PHOrbEvaluation &result,
                   bool allowPoisonWithHeartFour) noexcept;
bool PHOrbEvaluateRoute(const PHOrbBoard &board, const PHOrbRoute &route, const PHOrbSolveOptions &options,
                        PHOrbEvaluation &result, uint32_t &thornDamage, uint8_t &thornContacts) noexcept;
// One caller-owned worker, immutable input, no native/game calls or process exit.
// Recovery passes and at most two saved, discarded frontiers share the original
// deadline; neither changes exact evaluation or the maximum gesture length.
// Intermediate shape repairs may defer their required heart four, but every
// returned candidate still applies the complete release/survival policy.
// Spinner shape searches reuse color-layout scores in a bounded per-pass
// cache; contact damage and final uncached release checks remain route-specific.
// IncompatibleGoals means a necessary T/four or same-color T/L separator
// bound fails even with arbitrary placement. Single-color exact shape goals
// also reserve distinct orb counts. Other failures never imply infeasibility.
PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept;
