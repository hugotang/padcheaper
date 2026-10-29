#pragma once
#include "PHOrbModel.h"
#include <atomic>

struct PHOrbSolveOptions {
    unsigned steps = 32;
    unsigned beam = 256;
    unsigned milliseconds = 350;
    PHOrbGoalSet goals{};
};

enum class PHOrbSearchQuality { Fast, Balanced, Thorough };
PHOrbSolveOptions PHOrbOptionsForQuality(PHOrbSearchQuality quality) noexcept;

enum class PHOrbSolveStatus { Solved, GoalsUnmet, Unsafe, InsufficientOrbs, Cancelled, Invalid, NoRoute, Failed };
// Re-evaluate a released board. Shape goals require the first removal wave;
// combos/colors/connected-match counts may include deterministic cascades.
// Four means exactly four marked, connected orbs in a line; unmarked same-
// colour neighbours are allowed. L/T/cross/square retain their conservative
// same-colour border exclusion.
// Poison/mortal-poison matches are forbidden, and every bomb must participate
// in the first removal wave. Spinners share a native clock; enumerate every
// common phase of their validated color cycle (up to ten phases), requiring
// every result to meet the goals and hazard policy. Native unmatchable colors,
// minimum runs and minimum connected matches apply to every cascade.
// Routes respect tape/fixed start and never traverse spinners or thorns.
bool PHOrbEvaluate(const PHOrbBoard &board, const PHOrbGoalSet &goals, PHOrbEvaluation &result) noexcept;
// One caller-owned worker, immutable input, no native/game calls or process exit.
PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept;
