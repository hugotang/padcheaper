#pragma once
#include "PHOrbModel.h"
#include <atomic>

struct PHOrbSolveOptions {
    unsigned steps = 32;
    unsigned beam = 256;
    unsigned milliseconds = 350;
};

enum class PHOrbSearchQuality { Fast, Balanced, Thorough };
PHOrbSolveOptions PHOrbOptionsForQuality(PHOrbSearchQuality quality) noexcept;

enum class PHOrbSolveStatus { Solved, Cancelled, Invalid, NoRoute, Failed };
// One caller-owned worker, immutable input, no native/game calls or process exit.
PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept;
