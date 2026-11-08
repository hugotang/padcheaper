// Memory-safety smoke test for the public PHOrbSolve entry point. Built only by
// ios/test.sh with AddressSanitizer + UndefinedBehaviorSanitizer. Sanitizers
// slow the search unpredictably, so this file asserts API validity (status,
// in-bounds adjacent routes, cancellation) and never route quality; quality
// stays in orb_solver_tests.cpp at normal optimisation.
#include "PHOrbSolver.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <random>
#include <utility>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

static PHOrbBoard RandomBoard(unsigned width, unsigned height, unsigned seed) {
    PHOrbBoard board{}; board.width = uint8_t(width); board.height = uint8_t(height);
    std::mt19937 random(seed);
    for (unsigned i = 0; i < width * height; ++i) {
        board.types[i] = uint8_t(random() % 6); board.tokens[i] = i + 1;
        board.x[i] = int16_t(50 + i % width * 100); board.y[i] = int16_t(300 + i / width * 100);
    }
    return board;
}

static bool KnownStatus(PHOrbSolveStatus status) {
    switch (status) {
        case PHOrbSolveStatus::Solved: case PHOrbSolveStatus::GoalsUnmet: case PHOrbSolveStatus::Unsafe:
        case PHOrbSolveStatus::InsufficientOrbs: case PHOrbSolveStatus::Cancelled: case PHOrbSolveStatus::Invalid:
        case PHOrbSolveStatus::NoRoute: case PHOrbSolveStatus::Failed: case PHOrbSolveStatus::IncompatibleGoals:
            return true;
    }
    return false;
}

// Every returned cell must be on the board and each step a legal neighbour.
static bool RouteShapeValid(const PHOrbBoard &board, const PHOrbRoute &route, bool diagonal) {
    const unsigned cells = unsigned(board.width) * board.height;
    if (route.count > PHOrbMaxSteps + 1u) return false;
    for (unsigned i = 0; i < route.count; ++i) if (route.cells[i] >= cells) return false;
    for (unsigned i = 1; i < route.count; ++i) {
        const int ax = route.cells[i - 1] % board.width, ay = route.cells[i - 1] / board.width;
        const int bx = route.cells[i] % board.width, by = route.cells[i] / board.width;
        const int dx = std::abs(ax - bx), dy = std::abs(ay - by);
        if (dx > 1 || dy > 1 || (dx == 0 && dy == 0) || (!diagonal && dx + dy != 1)) return false;
    }
    return true;
}

static void Run(const char *name, const PHOrbBoard &board, PHOrbSolveOptions options) {
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    const PHOrbSolveStatus status = PHOrbSolve(board, options, cancel, route);
    Check(KnownStatus(status), name);
    Check(RouteShapeValid(board, route, options.diagonal), name);
    if (status == PHOrbSolveStatus::Solved) Check(route.count >= 2, name);
    std::printf("%s: status=%d steps=%u evaluated=%u\n", name, int(status), unsigned(route.count), route.evaluated);
}

static PHOrbGoal Goal(PHOrbGoalKind kind, uint8_t colors, uint8_t count, uint8_t length = 0) {
    return PHOrbGoal{uint8_t(kind), colors, count, length};
}

int main() {
    // Small budgets keep the sanitized run short; every search still enters
    // beam selection, recovery passes and frontier bookkeeping.
    PHOrbSolveOptions base = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    base.milliseconds = 120;
    for (auto size : {std::pair<unsigned, unsigned>{6, 5}, {7, 6}, {5, 4}})
        for (unsigned seed = 1; seed <= 3; ++seed) Run("combo", RandomBoard(size.first, size.second, seed), base);

    // Six colours / five colours + heart, plus a Combo clause left unmet:
    // completed colour clauses use the reserved no-missing beam bucket.
    for (uint8_t colors : {uint8_t(63), uint8_t(31)}) {
        PHOrbSolveOptions options = base; options.recoveryPasses = 1; options.backtrackLimit = 2;
        options.goals.count = 2;
        options.goals.items[0] = Goal(PHOrbGoalColors, colors, colors == 63 ? 6 : 5);
        options.goals.items[1] = Goal(PHOrbGoalCombo, 0, 9);
        Run("colors+combo", RandomBoard(6, 5, 11), options);
        Run("colors+combo 7x6", RandomBoard(7, 6, 12), options);
    }
    // Quality presets as shipped (beam/recovery/backtrack), shortened.
    for (auto quality : {PHOrbSearchQuality::Balanced, PHOrbSearchQuality::Thorough}) {
        PHOrbSolveOptions options = PHOrbOptionsForQuality(quality); options.milliseconds = 150;
        options.goals.count = 2;
        options.goals.items[0] = Goal(PHOrbGoalColors, 31, 5);
        options.goals.items[1] = Goal(PHOrbGoalMatch, 32, 1, 3);
        for (unsigned seed = 13; seed <= 15; ++seed) Run("preset five+heart", RandomBoard(6, 5, seed), options);
    }

    // Shape goals, exact removal and minimum-run Match clauses.
    const PHOrbGoal shapes[] = {
        Goal(PHOrbGoalL, 1, 1), Goal(PHOrbGoalCross, 32, 1), Goal(PHOrbGoalT, 8, 1), Goal(PHOrbGoalSquare, 2, 1),
        Goal(PHOrbGoalRow, 1, 1), Goal(PHOrbGoalColumn, 4, 1), Goal(PHOrbGoalFour, 32, 1),
        Goal(PHOrbGoalExact, 1, 1, 5), Goal(PHOrbGoalMatch, 63, 2, 4)};
    for (const PHOrbGoal &goal : shapes) {
        PHOrbSolveOptions options = base; options.goals.count = 1; options.goals.items[0] = goal;
        Run("shape", RandomBoard(6, 5, 21 + goal.kind), options);
    }

    // Four spinners with the completed-colour bucket, and one unlocked
    // spinner fishing exchange.
    PHOrbBoard spinners = RandomBoard(7, 6, 31);
    spinners.spinnerColors = 63;
    for (unsigned row = 0; row < 4; ++row) spinners.spinnerMask |= UINT64_C(1) << (row * spinners.width + 6);
    PHOrbSolveOptions spin = base; spin.goals.count = 2;
    spin.goals.items[0] = Goal(PHOrbGoalColors, 31 | 32, 6); spin.goals.items[1] = Goal(PHOrbGoalCombo, 0, 8);
    Run("four spinners", spinners, spin);
    PHOrbBoard single = RandomBoard(6, 5, 32);
    single.spinnerColors = 63; single.spinnerMask = UINT64_C(1) << 14;
    PHOrbSolveOptions fish = spin; fish.fishCell = 14; fish.fishColor = 2;
    Run("single spinner", single, spin);
    Run("single spinner fishing", single, fish);
    const PHOrbFishingPlan plan = PHOrbPlanFishing(single, fish);
    Check(plan.count <= PHOrbBasicTypes && plan.selectedCount <= 2, "fishing plan bounds");

    // Thorns with known and unknown HP budgets, tape, locks, blind and
    // hazards; diagonal gestures; fixed start.
    PHOrbBoard hazards = RandomBoard(6, 5, 41);
    hazards.flags[29] = PHOrbFlagThorn; hazards.thornDamage[29] = 4;
    hazards.flags[0] = PHOrbFlagTape; hazards.tapeMask = 1;
    hazards.flags[7] = PHOrbFlagLocked; hazards.flags[8] = PHOrbFlagBlind; hazards.blindTurns[8] = 2;
    hazards.types[3] = PHOrbPoison; hazards.types[4] = PHOrbMortalPoison; hazards.types[5] = PHOrbBomb;
    hazards.types[6] = PHOrbJammer;
    for (bool known : {false, true}) {
        PHOrbSolveOptions options = base; options.thorns.known = known;
        options.thorns.hp = options.thorns.maximumHP = 100;
        Run("hazards", hazards, options);
    }
    PHOrbSolveOptions diagonal = base; diagonal.diagonal = true;
    Run("diagonal", RandomBoard(6, 5, 51), diagonal);
    PHOrbBoard fixed = RandomBoard(6, 5, 52); fixed.fixedStart = 12; fixed.minErase = 4; fixed.unmatchableColors = 1;
    Run("fixed start", fixed, base);

    // 29 hearts + one spinner: the monochrome lower-bound path.
    PHOrbBoard mono = RandomBoard(6, 5, 61);
    for (unsigned i = 0; i < 30; ++i) mono.types[i] = 5;
    mono.types[0] = 3; mono.spinnerColors = 63; mono.spinnerMask = UINT64_C(1) << 29;
    PHOrbSolveOptions monoGoals = base; monoGoals.goals.count = 2;
    monoGoals.goals.items[0] = Goal(PHOrbGoalT, 8, 1); monoGoals.goals.items[1] = Goal(PHOrbGoalFour, 32, 1);
    Run("monochrome", mono, monoGoals);

    // Activity objectives and preparation.
    PHOrbSolveOptions activity = base; activity.activity.colors = 63; activity.activity.comboWeight = 10;
    activity.activity.rowWeight = 3; activity.activity.linkWeight = 2; activity.activity.stepCost = 1;
    Run("activity", RandomBoard(6, 5, 71), activity);
    PHOrbSolveOptions preparation = base; preparation.steps = 12; preparation.beam = 512;
    preparation.goals.count = 1; preparation.goals.items[0] = Goal(PHOrbGoalCombo, 0, 1);
    preparation.goals.comboFirstWave = true;
    preparation.activity.firstWaveOnly = preparation.activity.preparation = true;
    preparation.activity.colors = 17; preparation.activity.primaryMinimum = 6; preparation.activity.stepCost = 35;
    preparation.activity.attackWeights[0] = 300; preparation.activity.attackWeights[4] = 100;
    const uint8_t conversion[6]{0, 0, 4, 0, 4, 4};
    for (unsigned i = 0; i < 6; ++i) preparation.activity.conversion[i] = conversion[i];
    Run("preparation", RandomBoard(6, 5, 72), preparation);

    // Invalid input and immediate cancellation.
    PHOrbBoard invalid{}; invalid.width = 9; invalid.height = 9;
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    const PHOrbSolveStatus rejected = PHOrbSolve(invalid, base, cancel, route);
    Check(KnownStatus(rejected) && rejected != PHOrbSolveStatus::Solved, "oversized board is never solved");
    cancel = true; route = PHOrbRoute{};
    const PHOrbSolveStatus cancelled = PHOrbSolve(RandomBoard(6, 5, 81), base, cancel, route);
    Check(KnownStatus(cancelled) && RouteShapeValid(RandomBoard(6, 5, 81), route, false), "pre-cancelled search");

    std::printf("Sanitized solve tests: %u failures\n", failures);
    return failures ? 1 : 0;
}
