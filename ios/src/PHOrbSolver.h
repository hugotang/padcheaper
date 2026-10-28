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

enum class PHOrbSolveStatus { Solved, GoalsUnmet, InsufficientOrbs, Cancelled, Invalid, NoRoute, Failed };
// Re-evaluate a released board. Shape goals require the first removal wave;
// combos/colors/connected-match counts may include deterministic cascades.
bool PHOrbEvaluate(const PHOrbBoard &board, const PHOrbGoalSet &goals, PHOrbEvaluation &result) noexcept;
// One caller-owned worker, immutable input, no native/game calls or process exit.
PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept;
