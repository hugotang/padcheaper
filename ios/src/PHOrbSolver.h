#pragma once
#include "PHOrbModel.h"
#include <atomic>

struct PHOrbActivityObjective {
    bool firstWaveOnly = false;
    bool comboPriority = false; // Maximize actual first-removal Combo before style/step cost.
    uint8_t colors = 0, linkLength = 12;
    uint16_t comboWeight = 0, rowWeight = 0, linkWeight = 0, stepCost = 0;
    uint8_t linkRewardLimit = 0; // Zero retains the legacy per-group heuristic.
    uint16_t attackWeights[6]{}; // Relative orb contribution, not native damage.
    uint16_t attackRowPercent[6]{};
    uint8_t attackLinkMultiplier = 1, attackLinkComboBonus = 0;
    bool preparation = false; // Exactly one three-orb removal, with no existing-orb cascades.
    uint8_t conversion[6]{0,1,2,3,4,5};
    uint8_t primaryColor = 0, primaryMinimum = 3;
};

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
    PHOrbActivityObjective activity{}; // Heuristic weights, separate from native scoring.
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
bool PHOrbEvaluateWithOptions(const PHOrbBoard &board, const PHOrbSolveOptions &options, PHOrbEvaluation &result) noexcept;
bool PHOrbEvaluateRoute(const PHOrbBoard &board, const PHOrbRoute &route, const PHOrbSolveOptions &options,
                        PHOrbEvaluation &result, uint32_t &thornDamage, uint8_t &thornContacts) noexcept;
// One caller-owned worker, immutable input, no native/game calls or process exit.
// Recovery passes and at most two saved, discarded frontiers share the original
// deadline; neither changes exact evaluation or the maximum gesture length.
// Intermediate shape repairs may defer their required heart four, but every
// returned candidate still applies the complete release/survival policy.
// Spinner shape searches reuse color-layout scores in a bounded per-pass
// cache; contact damage and final uncached release checks remain route-specific.
// IncompatibleGoals is retained for API compatibility; exact shapes follow the
// native removed-group rule, so no border-separator bound produces it now.
// Single-color exact shape goals still reserve distinct orb counts
// (InsufficientOrbs). Other failures never imply infeasibility.
// Ordinary quality budgets stay at or below 800 ms. Solo boards may retry a
// failed search with a longer budget: the solo move timer starts at the grab.
constexpr unsigned PHOrbMaxSearchMilliseconds = 2500, PHOrbSoloRetryMilliseconds = 2000;
PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept;
