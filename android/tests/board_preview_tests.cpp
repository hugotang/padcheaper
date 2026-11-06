#include "BoardPreview.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

using namespace padcheaper;

namespace {

BoardPreviewSnapshot Ordinary(unsigned width = 6, unsigned height = 5) {
    BoardPreviewSnapshot result;
    result.width = uint8_t(width); result.height = uint8_t(height);
    result.geometry = result.matching = result.starting = result.movement = result.boardEffects = PreviewKnowledge::Ordinary;
    const PreviewColor colors[]{PreviewColor::Fire, PreviewColor::Water, PreviewColor::Wood,
                                PreviewColor::Light, PreviewColor::Dark, PreviewColor::Heart};
    for (unsigned i = 0; i < width * height; ++i)
        result.cells[i] = {colors[(i % width + i / width) % 6], PreviewKnowledge::Ordinary};
    return result;
}

BoardPreviewRequest Combo(unsigned count = 1) {
    BoardPreviewRequest result;
    result.goals.count = 1;
    result.goals.items[0] = {PHOrbGoalCombo, 0, uint8_t(count), 0};
    result.quality = PHOrbSearchQuality::Fast;
    return result;
}

PHOrbRoute Candidate(const BoardPreviewSnapshot &snapshot, const BoardPreviewRequest &request,
                     unsigned from, unsigned to) {
    PHOrbBoard board{};
    PHOrbSolveOptions options;
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::Ready);
    PHOrbRoute route{};
    route.count = 2; route.cells[0] = uint8_t(from); route.cells[1] = uint8_t(to);
    route.diagonal = request.diagonal;
    route.exploredDepth = 1; route.searchPasses = 1;
    route.goalCount = request.goals.count;
    PHOrbEvaluation evaluated{};
    assert(PHOrbEvaluateRoute(board, route, options, evaluated, route.thornDamage, route.thornContacts));
    route.combos = evaluated.combos; route.erased = evaluated.erased;
    route.matchedGoals = evaluated.matchedGoals; route.goalsMet = evaluated.goalsMet;
    route.hazardsSafe = evaluated.hazardsSafe;
    route.poisonErased = evaluated.poisonErased; route.mortalPoisonErased = evaluated.mortalPoisonErased;
    route.unmatchedBombs = evaluated.unmatchedBombs; route.heartFours = evaluated.heartFours;
    std::copy(std::begin(evaluated.goalHits), std::end(evaluated.goalHits), std::begin(route.goalHits));
    return route;
}

void PreparationRejectsUnknownAndUnsupported() {
    PHOrbBoard board{};
    PHOrbSolveOptions options;
    BoardPreviewSnapshot snapshot;
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::InvalidDimensions);
    snapshot.width = 6; snapshot.height = 5;
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnknownRules);
    snapshot = Ordinary();
    using Field = PreviewKnowledge BoardPreviewSnapshot::*;
    const Field fields[]{&BoardPreviewSnapshot::geometry, &BoardPreviewSnapshot::matching,
                         &BoardPreviewSnapshot::starting, &BoardPreviewSnapshot::movement,
                         &BoardPreviewSnapshot::boardEffects};
    for (const auto field : fields) {
        snapshot.*field = PreviewKnowledge::Unknown;
        assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnknownRules);
        snapshot.*field = PreviewKnowledge::Unsupported;
        assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnsupportedRules);
        snapshot.*field = static_cast<PreviewKnowledge>(255);
        assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnsupportedRules);
        snapshot.*field = PreviewKnowledge::Ordinary;
    }
    snapshot.cells[0].color = PreviewColor::Unknown;
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnknownCell);
    snapshot.cells[0].color = PreviewColor::Unsupported;
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnsupportedCell);
    snapshot.cells[0].color = static_cast<PreviewColor>(255);
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnsupportedCell);
    snapshot.cells[0].color = PreviewColor::Fire;
    snapshot.cells[0].effects = PreviewKnowledge::Unknown;
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnknownCell);
    snapshot.cells[0].effects = PreviewKnowledge::Unsupported;
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::UnsupportedCell);
    snapshot = Ordinary();
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::Ready);
    snapshot.cells[30] = {PreviewColor::Fire, PreviewKnowledge::Ordinary};
    assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::NonCanonicalCells);
    assert(!board.width && !options.steps && !options.beam && !options.milliseconds);
}

void MappingAndRequestPolicy() {
    PHOrbBoard board{};
    PHOrbSolveOptions options;
    for (const auto &dimensions : {std::pair<unsigned,unsigned>{5,4}, {6,5}, {7,6}}) {
        const auto snapshot = Ordinary(dimensions.first, dimensions.second);
        assert(NormalizeBoardPreview(snapshot, {}, board, options) == BoardPreviewStatus::Ready);
        assert(board.width == dimensions.first && board.height == dimensions.second);
        assert(board.minErase == 3 && board.minMatch == 3 && !board.fixedStart);
        for (unsigned i = 0; i < dimensions.first * dimensions.second; ++i) {
            assert(board.types[i] == (i % dimensions.first + i / dimensions.first) % 6);
            assert(board.tokens[i] == i + 1 && !board.x[i] && !board.y[i]);
            assert(!board.flags[i] && !board.enhancement[i] && !board.blindTurns[i] && !board.thornDamage[i]);
        }
        assert(!board.spinnerMask && !board.tapeMask && !board.cloudMask && !board.unmatchableColors);
        assert(options.steps == 32 && options.beam == 256 && options.milliseconds == 350);
        assert(!options.diagonal && options.fishCell == -1 && !options.allowPoisonWithHeartFour && !options.thorns.known);
    }
    auto request = Combo();
    const auto snapshot = Ordinary();
    request.diagonal = true;
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::Ready);
    assert(options.steps == 24 && options.beam == 128 && options.milliseconds == 120 && options.diagonal);
    request.quality = PHOrbSearchQuality::Thorough;
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::Ready);
    assert(options.steps == 48 && options.beam == 512 && options.milliseconds == 800 && options.backtrackLimit == 2);
    request.quality = static_cast<PHOrbSearchQuality>(255);
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::InvalidRequest);
    request = Combo(); request.goals.bonusFirstWave = true;
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::InvalidRequest);
    request = Combo(); request.goals.comboFirstWave = true;
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::InvalidRequest);
    request = Combo(); request.goals.items[0].count = 0;
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::InvalidRequest);
    request = Combo(); request.goals.count = PHOrbMaxGoals + 1;
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::InvalidRequest);
    request = Combo(); request.goals.items[1] = request.goals.items[0];
    assert(NormalizeBoardPreview(snapshot, request, board, options) == BoardPreviewStatus::InvalidRequest);
}

void ReplayAndStaleContent() {
    auto snapshot = Ordinary();
    const auto request = Combo();
    // The 2 -> 8 swap completes the explicit fire row 0,1,2.
    snapshot.cells[0].color = snapshot.cells[1].color = snapshot.cells[8].color = PreviewColor::Fire;
    snapshot.cells[2].color = PreviewColor::Wood;
    const auto route = Candidate(snapshot, request, 2, 8);
    assert(route.goalsMet && route.hazardsSafe);
    const auto result = CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, route);
    assert(result.status == BoardPreviewStatus::Ready && !result.solverInvoked);
    assert(result.evaluation.goalsMet && result.draggedColors[2] == PreviewColor::Fire);
    assert(result.draggedColors[8] == PreviewColor::Wood && result.draggedColors[30] == PreviewColor::Unknown);
    assert(snapshot.cells[2].color == PreviewColor::Wood && snapshot.cells[8].color == PreviewColor::Fire);
    assert(BoardPreviewMatches(result, snapshot, request));
    auto changed = snapshot; changed.cells[8].color = PreviewColor::Heart;
    assert(!BoardPreviewMatches(result, changed, request));
    changed = snapshot; changed.movement = PreviewKnowledge::Unknown;
    assert(!BoardPreviewMatches(result, changed, request));
    changed = snapshot; changed.cells[41].effects = PreviewKnowledge::Ordinary;
    assert(!BoardPreviewMatches(result, changed, request));
    auto otherRequest = request; otherRequest.goals.items[0].count = 2;
    assert(!BoardPreviewMatches(result, snapshot, otherRequest));
    otherRequest = request; otherRequest.diagonal = true;
    assert(!BoardPreviewMatches(result, snapshot, otherRequest));
    otherRequest = request; otherRequest.quality = PHOrbSearchQuality::Balanced;
    assert(!BoardPreviewMatches(result, snapshot, otherRequest));
    auto invalid = route; ++invalid.combos;
    auto rejected = CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, invalid);
    assert(rejected.status == BoardPreviewStatus::InvalidSolverResult && !rejected.route.count);
    assert(!rejected.evaluation.goalsMet && rejected.draggedColors[2] == PreviewColor::Unknown);
    assert(!BoardPreviewMatches(rejected, snapshot, request));
    invalid = route; invalid.cells[1] = 9;
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, invalid).status == BoardPreviewStatus::InvalidSolverResult);
    invalid = route; invalid.diagonal = true;
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, invalid).status == BoardPreviewStatus::InvalidSolverResult);
    invalid = route; invalid.count = 25 + 1;
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, invalid).status == BoardPreviewStatus::InvalidSolverResult);
    invalid = route; invalid.backtracks = 1;
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, invalid).status == BoardPreviewStatus::InvalidSolverResult);
    invalid = route; invalid.searchPasses = 3;
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, invalid).status == BoardPreviewStatus::InvalidSolverResult);
    invalid = route; invalid.exploredDepth = 25;
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::Solved, invalid).status == BoardPreviewStatus::InvalidSolverResult);
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::NoRoute, route).status == BoardPreviewStatus::InvalidSolverResult);
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::GoalsUnmet, route).status == BoardPreviewStatus::InvalidSolverResult);
}

void MissesDiagonalsAndWorker() {
    auto snapshot = Ordinary();
    auto request = Combo(14); // Impossible inventory, still useful for strict candidate review.
    auto route = Candidate(snapshot, request, 0, 1);
    assert(!route.goalsMet && route.hazardsSafe);
    auto result = CheckBoardPreview(snapshot, request, PHOrbSolveStatus::GoalsUnmet, route);
    assert(result.status == BoardPreviewStatus::Ready && !result.evaluation.goalsMet && result.route.count == 2);
    request.diagonal = true;
    route = Candidate(snapshot, request, 0, 7);
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::GoalsUnmet, route).status == BoardPreviewStatus::Ready);
    request.diagonal = false;
    assert(CheckBoardPreview(snapshot, request, PHOrbSolveStatus::GoalsUnmet, route).status == BoardPreviewStatus::InvalidSolverResult);
    result = CheckBoardPreview(snapshot, request, PHOrbSolveStatus::NoRoute, {});
    assert(result.status == BoardPreviewStatus::NoRoute && !result.route.count && !BoardPreviewMatches(result, snapshot, request));
    std::atomic_bool cancel{true};
    result = SolveBoardPreview(snapshot, request, cancel);
    assert(result.status == BoardPreviewStatus::Cancelled && !result.solverInvoked && !result.route.count);
    cancel.store(false);
    request = Combo();
    snapshot.cells[0].color = snapshot.cells[1].color = snapshot.cells[2].color = PreviewColor::Fire;
    result = SolveBoardPreview(snapshot, request, cancel);
    assert(result.status == BoardPreviewStatus::Ready && result.solverInvoked && result.route.count >= 2);
    assert(result.evaluation.goalsMet && BoardPreviewMatches(result, snapshot, request));
    snapshot.matching = PreviewKnowledge::Unknown;
    result = SolveBoardPreview(snapshot, request, cancel);
    assert(result.status == BoardPreviewStatus::UnknownRules && !result.solverInvoked && !result.route.count);
}

} // namespace

int main() {
    PreparationRejectsUnknownAndUnsupported();
    MappingAndRequestPolicy();
    ReplayAndStaleContent();
    MissesDiagonalsAndWorker();
    assert(std::strcmp(BoardPreviewStatusName(BoardPreviewStatus::UnknownRules), "unknown-rules") == 0);
}
