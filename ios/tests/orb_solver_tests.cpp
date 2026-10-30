#include "PHOrbSolver.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>
#include <set>
#include <vector>

static unsigned failures;
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

// Independent oracle: scan complete runs, union marked neighbors, then rebuild
// each column from its survivors. It does not call the production evaluator.
static std::pair<unsigned, unsigned> Oracle(const PHOrbBoard &board) {
    const unsigned w = board.width, h = board.height, n = w * h;
    std::vector<int> cells(board.types, board.types + n);
    unsigned combos = 0, erased = 0;
    for (;;) {
        std::set<unsigned> marked;
        for (unsigned axis = 0; axis < 2; ++axis) {
            unsigned lines = axis ? w : h, length = axis ? h : w;
            for (unsigned line = 0; line < lines; ++line) {
                auto index = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
                for (unsigned start = 0; start < length;) {
                    unsigned end = start + 1;
                    while (end < length && cells[index(start)] == cells[index(end)]) ++end;
                    if (cells[index(start)] >= 0 && end - start >= 3)
                        for (unsigned p = start; p < end; ++p) marked.insert(index(p));
                    start = end;
                }
            }
        }
        if (marked.empty()) return {combos, erased};
        std::vector<unsigned> parent(n); std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](unsigned a) { while (parent[a] != a) a = parent[a]; return a; };
        for (unsigned a : marked) for (unsigned b : marked) {
            if (a < b && cells[a] == cells[b] &&
                ((a / w == b / w && b - a == 1) || b - a == w)) parent[root(b)] = root(a);
        }
        std::set<unsigned> groups;
        for (unsigned i : marked) { groups.insert(root(i)); cells[i] = -1; }
        combos += unsigned(groups.size()); erased += unsigned(marked.size());
        for (unsigned column = 0; column < w; ++column) {
            std::vector<int> survivors;
            for (unsigned row = 0; row < h; ++row) if (cells[row * w + column] >= 0) survivors.push_back(cells[row * w + column]);
            survivors.insert(survivors.begin(), h - survivors.size(), -1);
            for (unsigned row = 0; row < h; ++row) cells[row * w + column] = survivors[row];
        }
    }
}

static PHOrbBoard Board(unsigned width, unsigned height, unsigned seed) {
    PHOrbBoard board{}; board.width = uint8_t(width); board.height = uint8_t(height);
    std::mt19937 random(seed);
    for (unsigned i = 0; i < width * height; ++i) {
        board.types[i] = uint8_t(random() % 6); board.tokens[i] = i + 1;
        board.x[i] = int16_t(50 + i % width * 100); board.y[i] = int16_t(300 + i / width * 100);
    }
    return board;
}

static PHOrbBoard ShapeBoard(unsigned width, unsigned height, const std::vector<unsigned> &positions) {
    auto board = Board(width, height, 1);
    // Background never contains the red/heart fixture colors or an initial run.
    for (unsigned i = 0; i < width * height; ++i) board.types[i] = uint8_t(1 + (i % width + 2 * (i / width)) % 4);
    for (unsigned i : positions) board.types[i] = 0;
    return board;
}
static PHOrbGoalSet Goals(PHOrbGoalKind kind, unsigned colors = 1, unsigned count = 1, unsigned length = 0) {
    PHOrbGoalSet goals{}; goals.count = 1;
    goals.items[0] = {uint8_t(kind), uint8_t(colors), uint8_t(count), uint8_t(length)}; return goals;
}
static void ShapeTests() {
    PHOrbEvaluation result{};
    for (const auto &positions : std::vector<std::vector<unsigned>>{
        {0,6,12,13,14}, {2,8,12,13,14}, {0,1,2,6,12}, {0,1,2,8,14}}) {
        auto board = ShapeBoard(6,5,positions);
        Check(PHOrbEvaluate(board, Goals(PHOrbGoalL), result) && result.goalsMet && result.goalHits[0] == 1,
              "each geometrically distinct L rotation is recognized");
        Check(PHOrbEvaluate(board, Goals(PHOrbGoalT), result) && !result.goalsMet, "an L is not a T");
    }
    for (const auto &positions : std::vector<std::vector<unsigned>>{
        {0,1,2,7,13}, {1,7,12,13,14}, {0,6,7,8,12}, {2,6,7,8,14}}) {
        Check(PHOrbEvaluate(ShapeBoard(6,5,positions), Goals(PHOrbGoalT), result) && result.goalsMet,
              "all T rotations, including sideways shapes, are recognized");
    }
    auto cross = ShapeBoard(6,5,{1,6,7,8,13});
    Check(PHOrbEvaluate(cross, Goals(PHOrbGoalCross), result) && result.goalsMet, "isolated cross is recognized");
    Check(PHOrbEvaluate(cross, Goals(PHOrbGoalT), result) && !result.goalsMet, "cross and T are different requirements");
    cross.types[19] = 0;
    Check(PHOrbEvaluate(cross, Goals(PHOrbGoalCross), result) && !result.goalsMet, "a connected sixth orb invalidates a five-orb cross");
    auto innerNeighbor = ShapeBoard(6,5,{0,6,12,13,14,7});
    Check(PHOrbEvaluate(innerNeighbor, Goals(PHOrbGoalL), result) && !result.goalsMet,
          "exact shape conservatively excludes even an unmatched same-color neighbor inside the L");
    const std::vector<unsigned> squareCells{0,1,2,6,7,8,12,13,14};
    auto square = ShapeBoard(6,5,squareCells);
    Check(PHOrbEvaluate(square, Goals(PHOrbGoalSquare), result) && result.goalsMet, "isolated 3x3 square is recognized");
    square.types[3] = square.types[9] = square.types[15] = 0;
    Check(PHOrbEvaluate(square, Goals(PHOrbGoalSquare), result) && !result.goalsMet,
          "3x4 rectangle cannot masquerade as a square by containing a 3x3 subregion");
    auto largeSquare = ShapeBoard(7,6,{25,26,27,32,33,34,39,40,41});
    Check(PHOrbEvaluate(largeSquare, Goals(PHOrbGoalSquare), result) && result.goalsMet,
          "shape masks include the 42nd cell and do not truncate at 32 bits");
    for (auto dimensions : {std::pair<unsigned, unsigned>{6,5}, {7,6}, {5,4}}) {
        unsigned w = dimensions.first, h = dimensions.second;
        std::vector<unsigned> row, column;
        for (unsigned x = 0; x < w; ++x) row.push_back((h - 1) * w + x);
        for (unsigned y = 0; y < h; ++y) column.push_back(y * w + w - 1);
        Check(PHOrbEvaluate(ShapeBoard(w,h,row), Goals(PHOrbGoalRow), result) && result.goalsMet,
              "row means actual board width on every supported geometry");
        row.pop_back();
        Check(PHOrbEvaluate(ShapeBoard(w,h,row), Goals(PHOrbGoalRow), result) && !result.goalsMet,
              "an almost-full row does not meet the goal");
        Check(PHOrbEvaluate(ShapeBoard(w,h,column), Goals(PHOrbGoalColumn), result) && result.goalsMet,
              "column means actual board height, including the smaller board");
        column.pop_back();
        Check(PHOrbEvaluate(ShapeBoard(w,h,column), Goals(PHOrbGoalColumn), result) && !result.goalsMet,
              "an almost-full column does not meet the goal");
    }
    Check(PHOrbEvaluate(ShapeBoard(6,5,{0,1,2,3}), Goals(PHOrbGoalFour), result) && result.goalsMet,
          "four consecutive isolated orbs meet the four-match goal");
    Check(PHOrbEvaluate(ShapeBoard(6,5,{0,1,6,7}), Goals(PHOrbGoalFour), result) && !result.goalsMet,
          "2x2 of the right color is not a match");
    Check(PHOrbEvaluate(ShapeBoard(6,5,{0,1,2,3,4}), Goals(PHOrbGoalFour), result) && !result.goalsMet,
          "a five-orb line is not an exact four-match");
    auto columns = ShapeBoard(6,5,{0,6,12,18,24,4,10,16,22,28});
    Check(PHOrbEvaluate(columns, Goals(PHOrbGoalColumn,1,2), result) && result.goalsMet, "two separate columns count as two groups");
    columns.types[1] = columns.types[2] = columns.types[3] = 0;
    Check(PHOrbEvaluate(columns, Goals(PHOrbGoalColumn,1,2), result) && !result.goalsMet && result.goalHits[0] == 1,
          "two columns joined into one combo cannot satisfy two separate groups");

    auto together = ShapeBoard(7,6,{0,7,14,15,16});
    for (unsigned i : {5u,11u,12u,13u,19u}) together.types[i] = 5;
    auto combined = Goals(PHOrbGoalL);
    combined.items[combined.count++] = {PHOrbGoalCross, 32, 1, 0};
    combined.items[combined.count++] = {PHOrbGoalColors, 33, 2, 0};
    combined.items[combined.count++] = {PHOrbGoalCombo, 0, 2, 0};
    Check(PHOrbEvaluate(together, combined, result) && result.goalsMet && result.matchedGoals == 4,
          "different shapes, colors and combo threshold form a conjunction");
    auto oracle = Oracle(together);
    Check(result.combos == oracle.first && result.erased == oracle.second, "adding goals preserves independent cascade scoring");
    combined.items[1].colors = 2;
    Check(PHOrbEvaluate(together, combined, result) && !result.goalsMet, "the same geometry in the wrong color is not accepted");

    std::atomic_bool cancel{false}; PHOrbRoute route{};
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Balanced);
    options.steps = 1; options.milliseconds = 1000; options.goals = Goals(PHOrbGoalSquare);
    auto nearSquare = ShapeBoard(6,5,squareCells); std::swap(nearSquare.types[8], nearSquare.types[9]);
    Check(PHOrbSolve(nearSquare, options, cancel, route) == PHOrbSolveStatus::Solved && route.goalsMet && route.count == 2,
          "one legal adjacent swap completes a near-square and is favored over unmet routes");
    auto insufficient = ShapeBoard(6,5,{0,1,2,6,7,8,12,13});
    Check(PHOrbSolve(insufficient, options, cancel, route) == PHOrbSolveStatus::InsufficientOrbs && route.count == 0,
          "inventory failure yields no executable route, without relying on skyfall");
    auto conflict = ShapeBoard(6,5,squareCells);
    options.goals.items[options.goals.count++] = {PHOrbGoalL, 1, 1, 0};
    Check(PHOrbSolve(conflict, options, cancel, route) == PHOrbSolveStatus::GoalsUnmet && !route.goalsMet &&
          route.count == 2 && route.matchedGoals < route.goalCount,
          "nine red orbs cannot form disjoint red square and L; a legal partial route is explicitly unmet");
    PHOrbBoard replay{};
    Check(PHOrbReplay(&conflict, &route, route.count - 1, &replay), "an unmet route remains usable for explanatory preview only");
}

#include "orb_effect_solver_tests.inc"
#include "orb_shape_solver_tests.inc"
#include "orb_thorn_solver_tests.inc"
#include "orb_t_boundary_solver_tests.inc"
#include "orb_search_recovery_tests.inc"
#include "orb_recorded_thorn_solver_tests.inc"
#include "orb_poison_solver_tests.inc"

int main() {
    ShapeTests();
    TBoundarySolverTests();
    OrbEffectSolverTests();
    PoisonHeartFourSolverTests();
    ThornSolverTests();
    OrbShapeSolverTests();
    SearchRecoveryTests();
    RecordedThornSolverTests();
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    for (auto dimensions : {std::pair<unsigned, unsigned>{6,5}, {7,6}, {5,4}}) {
        PHOrbSnapshot snapshot{}; snapshot.board = Board(dimensions.first, dimensions.second, 7);
        snapshot.present = snapshot.valid = snapshot.settled = snapshot.ordinary = snapshot.input.valid = true;
        snapshot.phase = 2;
        Check(PHOrbCanPreview(&snapshot), "all supported dimensions can preview an ordinary single-player board");
        for (unsigned seed = 1; seed <= 8; ++seed) {
            auto board = Board(dimensions.first, dimensions.second, seed);
            const auto original = board;
            Check(PHOrbSolve(board, {12, 64, 1000}, cancel, route) == PHOrbSolveStatus::Solved, "fixed board solves");
            Check(std::memcmp(&board, &original, sizeof(board)) == 0, "solver leaves snapshot immutable");
            PHOrbBoard replay{};
            Check(PHOrbReplay(&board, &route, route.count - 1, &replay), "path contains only adjacent legal cells");
            auto score = Oracle(replay);
            Check(score.first == route.combos && score.second == route.erased, "independent run/union/gravity oracle matches score");
            Check(route.count <= 13 && route.combos <= dimensions.first * dimensions.second / 3, "route and combo bounds");
        }
    }
    for (auto quality : {PHOrbSearchQuality::Fast, PHOrbSearchQuality::Balanced, PHOrbSearchQuality::Thorough}) {
        const auto options = PHOrbOptionsForQuality(quality);
        auto profileBoard = Board(7,6,83);
        PHOrbBoard profileReplay{};
        Check(PHOrbSolve(profileBoard, options, cancel, route) == PHOrbSolveStatus::Solved &&
            route.count <= options.steps + 1 && PHOrbReplay(&profileBoard, &route, route.count - 1, &profileReplay),
            "every shipped quality produces a bounded legal route for the largest board");
        const auto score = Oracle(profileReplay);
        Check(score.first == route.combos && score.second == route.erased, "quality profiles preserve independent combo scoring");
    }
    Check(!PHOrbDimensionsSupported(8,6) && !PHOrbDimensionsSupported(6,6), "unreviewed board geometries remain rejected");
    auto board = Board(6,5,42);
    Check(!PHOrbAdjacent(5,6,6,5) && !PHOrbAdjacent(0,7,6,5), "row wrap and diagonals rejected");
    PHOrbRoute invalid{}; invalid.count = 2; invalid.cells[0] = 5; invalid.cells[1] = 6;
    PHOrbBoard replay{};
    Check(!PHOrbReplay(&board, &invalid, 1, &replay), "replay rejects a malformed solver route");
    auto same = board;
    std::swap(same.tokens[0], same.tokens[1]);
    Check(!PHOrbSameBoard(&board, &same), "same-color token swaps invalidate stale previews");
    same = board; same.flags[0] = 0x40000000;
    Check(!PHOrbBoardValid(&same), "unmodeled flags fail closed");
    same = board; same.flags[0] = PHOrbFlagLocked;
    Check(PHOrbBoardValid(&same) && !PHOrbSameBoard(&same,&board), "locked basic orbs are matchable but not interchangeable with unlocked snapshots");
    same = board; same.types[0] = 10;
    Check(PHOrbSolve(same, {}, cancel, route) == PHOrbSolveStatus::Invalid, "unsupported orb rejected without process exit");
    Check(PHOrbSolve(board, {49, 64, 100}, cancel, route) == PHOrbSolveStatus::Invalid, "depth bounded");
    Check(PHOrbSolve(board, {20, 513, 100}, cancel, route) == PHOrbSolveStatus::Invalid, "beam memory bounded");
    cancel = true;
    Check(PHOrbSolve(board, {}, cancel, route) == PHOrbSolveStatus::Cancelled && route.count == 0, "cancellation returns no executable route");
    cancel = false;
    auto before = std::chrono::steady_clock::now();
    auto status = PHOrbSolve(board, {48, 512, 1}, cancel, route);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count();
    Check((status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::NoRoute) && elapsed < 500, "deadline returns promptly");
    if (status == PHOrbSolveStatus::Solved) Check(PHOrbReplay(&board, &route, route.count - 1, &replay), "partial deadline result remains replayable");
    PHOrbRoute first{}, second{};
    PHOrbSolve(board, {4,32,1000}, cancel, first); PHOrbSolve(board, {4,32,1000}, cancel, second);
    Check(first.count == second.count && !std::memcmp(first.cells, second.cells, first.count) && first.combos == second.combos,
          "sequential solves have no retained board/counter state");
    std::printf("Orb solver/model tests: %s (24 seeded boards; independent oracle; bounds, stale tokens, cancellation, deadline)\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
