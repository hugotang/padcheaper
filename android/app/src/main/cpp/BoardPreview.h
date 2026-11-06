#pragma once
#include "PHOrbSolver.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace padcheaper {

// Owned, normalized data only. These values are NOT Android native color IDs,
// flags, addresses, read permissions or evidence that a live board was read.
enum class PreviewColor : uint8_t { Unknown, Fire, Water, Wood, Light, Dark, Heart, Unsupported };
enum class PreviewKnowledge : uint8_t { Unknown, Ordinary, Unsupported };

struct PreviewCell {
    PreviewColor color = PreviewColor::Unknown;
    // Ordinary confirms no lock, enhancement/weakening, blindness, combo orb,
    // nail, thorn or any other special orb effect. Unknown is never absent.
    PreviewKnowledge effects = PreviewKnowledge::Unknown;
};

struct BoardPreviewSnapshot {
    uint8_t width = 0, height = 0;
    // Top row first, left to right. Entries outside width*height stay Unknown.
    std::array<PreviewCell, PHOrbMaxCells> cells{};
    // Ordinary: complete, settled grid; no resize/transition or held orb.
    PreviewKnowledge geometry = PreviewKnowledge::Unknown;
    // Ordinary: runs/groups of at least three; all six colors matchable;
    // no conversion, special minimum or other match-rule changes.
    PreviewKnowledge matching = PreviewKnowledge::Unknown;
    // Ordinary: any starting cell; no fixed/forced start or start restriction.
    PreviewKnowledge starting = PreviewKnowledge::Unknown;
    // Ordinary: adjacent swaps only, no free-movement or movement restriction.
    PreviewKnowledge movement = PreviewKnowledge::Unknown;
    // Ordinary: no spinner, tape, cloud or other cell-anchored board effect.
    PreviewKnowledge boardEffects = PreviewKnowledge::Unknown;
};

struct BoardPreviewRequest {
    PHOrbGoalSet goals{};
    PHOrbSearchQuality quality = PHOrbSearchQuality::Balanced;
    bool diagonal = false;
};

enum class BoardPreviewStatus {
    Ready, InvalidDimensions, UnknownRules, UnsupportedRules,
    UnknownCell, UnsupportedCell, NonCanonicalCells, InvalidRequest,
    Cancelled, InsufficientOrbs, NoRoute, InvalidSolverResult, Failed
};

struct BoardPreviewResult {
    BoardPreviewStatus status = BoardPreviewStatus::Failed;
    // Full owned copies permit exact, padding-independent content comparison.
    // They do not identify a native match, turn, frame or operation window.
    BoardPreviewSnapshot source{};
    BoardPreviewRequest request{};
    PHOrbSolveStatus solverStatus = PHOrbSolveStatus::Invalid;
    bool solverInvoked = false;
    uint64_t wallMicroseconds = 0;
    // Populated only for Ready. No-route returns have no usable solver stats.
    // A Ready candidate may miss goals: evaluation.goalsMet says whether it
    // meets all requested goals, independently checked after replay.
    PHOrbRoute route{};
    PHOrbEvaluation evaluation{};
    // After all swaps, before removal/gravity. This is a display grid, not an
    // observed game board or prediction of random skyfall/native scoring.
    std::array<PreviewColor, PHOrbMaxCells> draggedColors{};
};

// Validate and convert plain data to the shared model; outputs are cleared on
// every rejection. Local tokens are inert 1-based indices, never game pointers.
BoardPreviewStatus NormalizeBoardPreview(const BoardPreviewSnapshot &snapshot,
    const BoardPreviewRequest &request, PHOrbBoard &board, PHOrbSolveOptions &options) noexcept;

// Pure validation/replay of a candidate, also used by SolveBoardPreview. It
// never invokes a solver, reads memory, calls a native game function or injects
// input. Invalid candidates return no path, dragged grid or evaluation.
BoardPreviewResult CheckBoardPreview(const BoardPreviewSnapshot &snapshot,
    const BoardPreviewRequest &request, PHOrbSolveStatus status, const PHOrbRoute &route) noexcept;

// Called on a caller-owned worker with value copies. No JNI/UI/live-reader
// entry point is connected in this milestone; cancellation is caller-owned.
BoardPreviewResult SolveBoardPreview(BoardPreviewSnapshot snapshot,
    BoardPreviewRequest request, const std::atomic_bool &cancel) noexcept;

// Ready result + exact full snapshot and request match, without a lossy hash.
// Content equality does not establish freshness or authorize a game action.
bool BoardPreviewMatches(const BoardPreviewResult &result,
    const BoardPreviewSnapshot &snapshot, const BoardPreviewRequest &request) noexcept;
const char *BoardPreviewStatusName(BoardPreviewStatus status) noexcept;

} // namespace padcheaper
