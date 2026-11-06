#include "BoardPreview.h"
#include <chrono>

namespace padcheaper {
namespace {

bool CoreColor(PreviewColor color, uint8_t &value) noexcept {
    switch (color) {
        case PreviewColor::Fire: value = 0; return true;
        case PreviewColor::Water: value = 1; return true;
        case PreviewColor::Wood: value = 2; return true;
        case PreviewColor::Light: value = 3; return true;
        case PreviewColor::Dark: value = 4; return true;
        case PreviewColor::Heart: value = 5; return true;
        default: return false;
    }
}

PreviewColor DisplayColor(uint8_t color) noexcept {
    switch (color) {
        case 0: return PreviewColor::Fire;
        case 1: return PreviewColor::Water;
        case 2: return PreviewColor::Wood;
        case 3: return PreviewColor::Light;
        case 4: return PreviewColor::Dark;
        case 5: return PreviewColor::Heart;
        default: return PreviewColor::Unknown;
    }
}

bool SameSnapshot(const BoardPreviewSnapshot &a, const BoardPreviewSnapshot &b) noexcept {
    if (a.width != b.width || a.height != b.height || a.geometry != b.geometry ||
        a.matching != b.matching || a.starting != b.starting || a.movement != b.movement ||
        a.boardEffects != b.boardEffects) return false;
    for (unsigned i = 0; i < PHOrbMaxCells; ++i)
        if (a.cells[i].color != b.cells[i].color || a.cells[i].effects != b.cells[i].effects) return false;
    return true;
}

bool SameRequest(const BoardPreviewRequest &a, const BoardPreviewRequest &b) noexcept {
    if (a.quality != b.quality || a.diagonal != b.diagonal || a.goals.count != b.goals.count ||
        a.goals.comboFirstWave != b.goals.comboFirstWave || a.goals.bonusFirstWave != b.goals.bonusFirstWave) return false;
    for (unsigned i = 0; i < PHOrbMaxGoals; ++i) {
        const auto &x = a.goals.items[i], &y = b.goals.items[i];
        if (x.kind != y.kind || x.colors != y.colors || x.count != y.count || x.length != y.length) return false;
    }
    return true;
}

bool SameEvaluation(const PHOrbRoute &route, const PHOrbEvaluation &checked) noexcept {
    if (route.combos != checked.combos || route.erased != checked.erased ||
        route.matchedGoals != checked.matchedGoals || route.goalsMet != checked.goalsMet ||
        route.hazardsSafe != checked.hazardsSafe || route.poisonErased != checked.poisonErased ||
        route.mortalPoisonErased != checked.mortalPoisonErased || route.unmatchedBombs != checked.unmatchedBombs ||
        route.heartFours != checked.heartFours) return false;
    for (unsigned i = 0; i < PHOrbMaxGoals; ++i)
        if (route.goalHits[i] != checked.goalHits[i]) return false;
    return true;
}

} // namespace

BoardPreviewStatus NormalizeBoardPreview(const BoardPreviewSnapshot &snapshot,
    const BoardPreviewRequest &request, PHOrbBoard &board, PHOrbSolveOptions &options) noexcept {
    board = {};
    options = {};
    // Failed preparation must not leave default, usable search limits behind.
    options.steps = options.beam = options.milliseconds = 0;
    if (!PHOrbDimensionsSupported(snapshot.width, snapshot.height)) return BoardPreviewStatus::InvalidDimensions;
    for (const auto rule : {snapshot.geometry, snapshot.matching, snapshot.starting,
                           snapshot.movement, snapshot.boardEffects}) {
        if (rule == PreviewKnowledge::Unknown) return BoardPreviewStatus::UnknownRules;
        if (rule != PreviewKnowledge::Ordinary) return BoardPreviewStatus::UnsupportedRules;
    }
    PHOrbBoard prepared{};
    prepared.width = snapshot.width;
    prepared.height = snapshot.height;
    prepared.minErase = prepared.minMatch = 3;
    const unsigned count = unsigned(snapshot.width) * snapshot.height;
    for (unsigned i = 0; i < count; ++i) {
        const auto &cell = snapshot.cells[i];
        if (cell.color == PreviewColor::Unknown || cell.effects == PreviewKnowledge::Unknown)
            return BoardPreviewStatus::UnknownCell;
        if (cell.effects != PreviewKnowledge::Ordinary || !CoreColor(cell.color, prepared.types[i]))
            return BoardPreviewStatus::UnsupportedCell;
        prepared.tokens[i] = i + 1; // Replay-only identity; never a native address.
    }
    for (unsigned i = count; i < PHOrbMaxCells; ++i)
        if (snapshot.cells[i].color != PreviewColor::Unknown || snapshot.cells[i].effects != PreviewKnowledge::Unknown)
            return BoardPreviewStatus::NonCanonicalCells;
    if ((request.quality != PHOrbSearchQuality::Fast && request.quality != PHOrbSearchQuality::Balanced &&
         request.quality != PHOrbSearchQuality::Thorough) || !PHOrbGoalsValid(&request.goals) ||
        request.goals.comboFirstWave || request.goals.bonusFirstWave) return BoardPreviewStatus::InvalidRequest;
    for (unsigned i = request.goals.count; i < PHOrbMaxGoals; ++i) {
        const auto &goal = request.goals.items[i];
        if (goal.kind || goal.colors || goal.count || goal.length) return BoardPreviewStatus::InvalidRequest;
    }
    if (!PHOrbBoardValid(&prepared)) return BoardPreviewStatus::InvalidDimensions;
    board = prepared;
    options = PHOrbOptionsForQuality(request.quality);
    options.goals = request.goals;
    options.diagonal = request.diagonal;
    return BoardPreviewStatus::Ready;
}

BoardPreviewResult CheckBoardPreview(const BoardPreviewSnapshot &snapshot,
    const BoardPreviewRequest &request, PHOrbSolveStatus status, const PHOrbRoute &route) noexcept {
    BoardPreviewResult result;
    result.source = snapshot;
    result.request = request;
    result.solverStatus = status;
    PHOrbBoard board{};
    PHOrbSolveOptions options;
    result.status = NormalizeBoardPreview(snapshot, request, board, options);
    if (result.status != BoardPreviewStatus::Ready) return result;
    result.status = BoardPreviewStatus::InvalidSolverResult;
    if (status != PHOrbSolveStatus::Solved && status != PHOrbSolveStatus::GoalsUnmet) {
        if (route.count) return result;
        switch (status) {
            case PHOrbSolveStatus::Cancelled: result.status = BoardPreviewStatus::Cancelled; break;
            case PHOrbSolveStatus::InsufficientOrbs: result.status = BoardPreviewStatus::InsufficientOrbs; break;
            case PHOrbSolveStatus::NoRoute: result.status = BoardPreviewStatus::NoRoute; break;
            case PHOrbSolveStatus::Failed: result.status = BoardPreviewStatus::Failed; break;
            default: break; // Unsafe/Invalid/Incompatible are unexpected for this prepared subset.
        }
        return result;
    }
    if (route.count < 2 || route.count > options.steps + 1 || route.diagonal != options.diagonal || route.fishing ||
        route.goalCount != options.goals.count || route.exploredDepth < unsigned(route.count - 1) ||
        route.exploredDepth > options.steps || route.backtracks > options.backtrackLimit ||
        !route.searchPasses || route.searchPasses > 1 + options.recoveryPasses + route.backtracks ||
        (status == PHOrbSolveStatus::Solved) != route.goalsMet || !route.hazardsSafe) return result;
    PHOrbBoard dragged{};
    PHOrbEvaluation checked{};
    uint32_t damage = 0;
    uint8_t contacts = 0;
    if (!PHOrbReplay(&board, &route, route.count - 1, &dragged) ||
        !PHOrbEvaluateRoute(board, route, options, checked, damage, contacts) ||
        !SameEvaluation(route, checked) || !checked.hazardsSafe || damage || contacts ||
        route.thornDamage || route.thornContacts) return result;
    result.route = route;
    result.evaluation = checked;
    for (unsigned i = 0; i < unsigned(snapshot.width) * snapshot.height; ++i)
        result.draggedColors[i] = DisplayColor(dragged.types[i]);
    result.status = BoardPreviewStatus::Ready;
    return result;
}

BoardPreviewResult SolveBoardPreview(BoardPreviewSnapshot snapshot,
    BoardPreviewRequest request, const std::atomic_bool &cancel) noexcept {
    BoardPreviewResult result;
    result.source = snapshot;
    result.request = request;
    PHOrbBoard board{};
    PHOrbSolveOptions options;
    result.status = NormalizeBoardPreview(snapshot, request, board, options);
    if (result.status != BoardPreviewStatus::Ready) return result;
    if (cancel.load(std::memory_order_relaxed)) {
        result.status = BoardPreviewStatus::Cancelled;
        result.solverStatus = PHOrbSolveStatus::Cancelled;
        return result;
    }
    PHOrbRoute route{};
    const auto start = std::chrono::steady_clock::now();
    const auto status = PHOrbSolve(board, options, cancel, route);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    // A cancellation arriving after PHOrbSolve's last check still suppresses
    // this result. No callback or UI publication occurs inside this adapter.
    if (cancel.load(std::memory_order_relaxed)) {
        result.status = BoardPreviewStatus::Cancelled;
        result.solverStatus = PHOrbSolveStatus::Cancelled;
    } else {
        result = CheckBoardPreview(snapshot, request, status, route);
    }
    result.solverInvoked = true;
    result.wallMicroseconds = elapsed > 0 ? uint64_t(elapsed) : 0;
    return result;
}

bool BoardPreviewMatches(const BoardPreviewResult &result,
    const BoardPreviewSnapshot &snapshot, const BoardPreviewRequest &request) noexcept {
    return result.status == BoardPreviewStatus::Ready && SameSnapshot(result.source, snapshot) &&
        SameRequest(result.request, request);
}

const char *BoardPreviewStatusName(BoardPreviewStatus status) noexcept {
    switch (status) {
        case BoardPreviewStatus::Ready: return "ready";
        case BoardPreviewStatus::InvalidDimensions: return "invalid-dimensions";
        case BoardPreviewStatus::UnknownRules: return "unknown-rules";
        case BoardPreviewStatus::UnsupportedRules: return "unsupported-rules";
        case BoardPreviewStatus::UnknownCell: return "unknown-cell";
        case BoardPreviewStatus::UnsupportedCell: return "unsupported-cell";
        case BoardPreviewStatus::NonCanonicalCells: return "noncanonical-cells";
        case BoardPreviewStatus::InvalidRequest: return "invalid-request";
        case BoardPreviewStatus::Cancelled: return "cancelled";
        case BoardPreviewStatus::InsufficientOrbs: return "insufficient-orbs";
        case BoardPreviewStatus::NoRoute: return "no-route";
        case BoardPreviewStatus::InvalidSolverResult: return "invalid-solver-result";
        case BoardPreviewStatus::Failed: return "failed";
    }
    return "invalid-status";
}

} // namespace padcheaper
