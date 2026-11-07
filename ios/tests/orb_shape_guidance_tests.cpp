// Private score-contract tests. This translation unit owns the solver source;
// link only PHInputModel/PHBattleModel/PHOrbModel, never another Solver object.
// These tests inspect guidance and cache invariants, not an independent match
// oracle or a proof that beam search will find an arbitrary valid route.
#include "../src/PHOrbSolver.cpp"
#include <cstdio>

static unsigned failures = 0;
static void Check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

template<typename T, size_t N> static bool SameArray(const T (&a)[N], const T (&b)[N]) {
    return std::equal(std::begin(a), std::end(a), std::begin(b));
}

// Compare fields, not object representations: neither padding nor compiler
// layout is part of the score contract.
static bool SameEvaluation(const PHOrbEvaluation &a, const PHOrbEvaluation &b) {
    return a.combos == b.combos && a.erased == b.erased && SameArray(a.goalHits, b.goalHits) &&
        a.matchedGoals == b.matchedGoals && a.firstWaveCombos == b.firstWaveCombos &&
        SameArray(a.rowGroups, b.rowGroups) && SameArray(a.linkGroups, b.linkGroups) &&
        SameArray(a.largestGroups, b.largestGroups) && a.activityValue == b.activityValue &&
        SameArray(a.retainedAfterConversion, b.retainedAfterConversion) && a.preparationReady == b.preparationReady &&
        a.poisonErased == b.poisonErased && a.unmatchedBombs == b.unmatchedBombs && a.heartFours == b.heartFours &&
        a.goalsMet == b.goalsMet && a.hazardsSafe == b.hazardsSafe && a.mortalPoisonErased == b.mortalPoisonErased;
}

static bool SameScore(const Score &a, const Score &b) {
    return SameEvaluation(a.result, b.result) && a.progress == b.progress && a.potential == b.potential &&
        a.jointPenalty == b.jointPenalty && a.goalProgress == b.goalProgress && a.layouts == b.layouts &&
        a.colorProgress == b.colorProgress && a.colorGuidedProgress == b.colorGuidedProgress &&
        a.matchedColors == b.matchedColors && a.missingColors == b.missingColors && a.weakestColor == b.weakestColor &&
        a.missing == b.missing && a.focus == b.focus;
}

static PHOrbGoalSet Goal(unsigned kind, unsigned colors = 1, unsigned count = 1) {
    PHOrbGoalSet goals{}; goals.count = 1;
    goals.items[0] = {uint8_t(kind), uint8_t(colors), uint8_t(count), 0}; return goals;
}

static Cells BoardCells(const PHOrbBoard &board) {
    Cells cells{};
    std::copy(board.types, board.types + board.width * board.height, cells.begin()); return cells;
}

static PHOrbBoard ShapeFixture(unsigned kind) {
    PHOrbBoard board{}; board.width = 6; board.height = 5;
    for (unsigned i = 0; i < 30; ++i) {
        board.types[i] = uint8_t(1 + (i % 6 + 2 * (i / 6)) % 4); board.tokens[i] = i + 1;
    }
    const std::vector<unsigned> cells = kind == PHOrbGoalL ? std::vector<unsigned>{0,6,12,13,14} :
        kind == PHOrbGoalT ? std::vector<unsigned>{0,1,2,7,13} : std::vector<unsigned>{0,1,2,6,7,8,12,13,14};
    for (unsigned cell : cells) board.types[cell] = 0;
    for (unsigned cell : {24u,25u,26u,27u}) board.types[cell] = 5;
    return board;
}

static Context Alternate(const Context &primary, unsigned variant) {
    auto alternate = primary;
    alternate.alternateShapeHints = true; alternate.stableHints = false;
    alternate.shapeHoleWeight = variant == 0 ? 2 : 4;
    alternate.shapeDistanceWeight = variant == 0 ? 2 : 1;
    return alternate;
}

static void AllMetScores() {
    for (unsigned kind : {unsigned(PHOrbGoalL), unsigned(PHOrbGoalT), unsigned(PHOrbGoalSquare)}) {
        for (unsigned conjunction = 0; conjunction < 3; ++conjunction) {
            auto goals = Goal(kind);
            if (conjunction) {
                goals.items[goals.count++] = {PHOrbGoalColors,33,2,0};
                goals.items[goals.count++] = {PHOrbGoalCombo,0,2,0};
            }
            if (conjunction == 2) goals.items[goals.count++] = {PHOrbGoalFour,32,1,0};
            for (unsigned hazard = 0; hazard < 4; ++hazard) for (bool allowPoison : {false, true}) {
                auto board = ShapeFixture(kind);
                if (hazard == 1 || hazard == 2)
                    for (unsigned cell : {18u,19u,20u}) board.types[cell] = uint8_t(hazard == 1 ? PHOrbPoison : PHOrbMortalPoison);
                if (hazard == 3) board.types[23] = PHOrbBomb;
                const auto cells = BoardCells(board);
                const auto primary = MakeContext(board, goals, true, allowPoison);
                const auto expected = Evaluate(cells, primary);
                Check(expected.result.goalsMet, "hand-constructed L/T/square and color/combo conjunctions satisfy all goals");
                Check(expected.result.hazardsSafe == (!hazard || (hazard < 3 && allowPoison)),
                    "all goals met is separate from poison, mortal poison and unmatched-bomb safety");
                for (unsigned variant = 0; variant < 2; ++variant) {
                    const auto actual = Evaluate(cells, Alternate(primary, variant));
                    Check(SameScore(expected, actual),
                        "all-met scores including focus/layouts are identical under both ordinary shape policies");
                }
            }
        }
        // A heart triple can satisfy Colors but cannot authorize poison.
        auto board = ShapeFixture(kind); board.types[27] = 1;
        for (unsigned cell : {18u,19u,20u}) board.types[cell] = PHOrbPoison;
        auto goals = Goal(kind); goals.items[goals.count++] = {PHOrbGoalColors,33,2,0};
        const auto primary = MakeContext(board, goals, true, true);
        const auto expected = Evaluate(BoardCells(board), primary);
        Check(expected.result.goalsMet && !expected.result.hazardsSafe && !expected.result.heartFours,
            "a goal-complete board without a heart four remains unsafe under poison opt-in");
        Check(SameScore(expected, Evaluate(BoardCells(board), Alternate(primary, 0))),
            "unsafe all-met scores also retain canonical progress and focus");
    }
}

static PHOrbBoard PartialSquare() {
    auto board = ShapeFixture(PHOrbGoalSquare);
    // Eight cells of the square stay in place; the ninth is distant. No
    // alternate square or deterministic cascade can contain these nine reds.
    std::swap(board.types[14], board.types[23]);
    return board;
}

static void PartialAndCacheScores() {
    const auto board = PartialSquare(); const auto cells = BoardCells(board);
    const auto primary = MakeContext(board, Goal(PHOrbGoalSquare));
    const auto expected = Evaluate(cells, primary);
    Check(!expected.result.goalsMet && expected.result.goalHits[0] == 0,
        "the separated ninth red supplies an unmet but inventory-sufficient square");
    for (unsigned variant = 0; variant < 2; ++variant) {
        const auto alternate = Alternate(primary, variant);
        const auto guided = Evaluate(cells, alternate);
        Check(SameEvaluation(expected.result, guided.result) && expected.progress != guided.progress,
            "alternate weights change partial guidance while preserving every exact evaluation field");
        Node frontier; frontier.cells = cells; frontier.score = guided;
        Node retained = frontier; retained.score = Evaluate(retained.cells, primary);
        Check(SameScore(retained.score, expected) && SameScore(frontier.score, guided) &&
            !SameScore(retained.score, frontier.score),
            "canonical scoring on a candidate copy preserves the separate guided frontier value");

        EvaluationCache cache(true);
        cache.nextPass();
        Check(SameScore(cache.score(cells, primary), expected), "cache initially scores the primary policy");
        Check(SameScore(cache.score(cells, primary), expected) && cache.hits == 1,
            "a repeated color layout reuses only the current pass's score");
        cache.nextPass();
        Check(SameScore(cache.score(cells, alternate), guided) && cache.calls == 2,
            "the first saved-frontier score in a new pass cannot inherit primary guidance");

        // An equal-color swap leaves evaluator input unchanged. Search can
        // reuse this score only after the new pass's first layer was scored.
        auto sameColor = cells; std::swap(sameColor[0], sameColor[1]);
        Check(sameColor == cells && SameScore(cache.score(sameColor, alternate), guided),
            "same-color reuse within the alternate pass retains guided rather than canonical scores");
        Check(SameScore(Evaluate(sameColor, primary), expected),
            "canonical candidate normalization does not overwrite the alternate cache entry");
        Check(SameScore(cache.score(sameColor, alternate), guided),
            "a later same-color cache hit remains in the alternate scoring scale");
        cache.nextPass();
        Check(SameScore(cache.score(cells, primary), expected),
            "returning to primary guidance starts with another fresh score");
    }
}

static void RankingAndThornSafety() {
    const auto goals = Goal(PHOrbGoalSquare);
    auto complete = ShapeFixture(PHOrbGoalSquare), partial = PartialSquare();
    const auto primary = MakeContext(complete, goals);
    Node best; best.cells = BoardCells(complete); best.score = Evaluate(best.cells, primary);
    Node missing; missing.cells = BoardCells(partial);
    missing.score = Evaluate(missing.cells, Alternate(primary, 0));
    Check(best.score.result.goalsMet && best.score.result.hazardsSafe && !missing.score.result.goalsMet &&
        !Better(missing, best, false, false, {}) && Better(best, missing, false, false, {}),
        "an unmet guided candidate cannot displace a safe all-met primary candidate");
    for (unsigned cell : {18u,19u,20u}) complete.types[cell] = PHOrbPoison;
    Node unsafe; unsafe.cells = BoardCells(complete);
    unsafe.score = Evaluate(unsafe.cells, Alternate(primary, 1));
    Check(unsafe.score.result.goalsMet && !unsafe.score.result.hazardsSafe &&
        !Better(unsafe, best, false, false, {}) && !Better(unsafe, missing, false, false, {}),
        "an unsafe completed shape loses to both safe complete and safe incomplete candidates");

    for (bool hearts : {false, true}) {
        auto board = ShapeFixture(PHOrbGoalSquare);
        if (!hearts) board.types[27] = 1;
        board.flags[29] = PHOrbFlagThorn; board.thornDamage[29] = 4;
        PHOrbRoute route{}; route.count = 2; route.cells[0] = 28; route.cells[1] = 29;
        PHOrbSolveOptions options; options.goals = goals; options.thorns = {100,1000,0,true};
        PHOrbEvaluation released{}; uint32_t damage = 0; uint8_t contacts = 0;
        Check(PHOrbEvaluateRoute(board, route, options, released, damage, contacts) &&
            released.goalsMet && released.hazardsSafe == hearts && contacts == 1 && damage == 40,
            "release validation still requires a heart four after a survivable thorn contact");
        const auto context = MakeContext(board, goals);
        const auto canonical = Evaluate(BoardCells(board), context);
        Check(SameScore(canonical, Evaluate(BoardCells(board), Alternate(context, 0))),
            "shape-weight normalization leaves the exact fields used by thorn validation unchanged");
    }
}

static void ColorScoresUnaffected() {
    const uint8_t types[] = {4,3,4,1,0,1, 0,5,2,5,5,3, 2,3,3,3,2,0,
                             5,1,0,5,0,0, 1,0,0,4,2,1};
    auto board = ShapeFixture(PHOrbGoalL);
    std::copy(std::begin(types), std::end(types), board.types);
    for (bool combo : {false, true}) for (bool colorGuidance : {false, true}) {
        auto goals = Goal(PHOrbGoalColors, 31, 5);
        if (combo) goals.items[goals.count++] = {PHOrbGoalCombo,0,6,0};
        auto primary = MakeContext(board, goals); primary.colorGuidance = colorGuidance;
        primary.colorFocus = 4;
        Check(!primary.shapeGoals && primary.colorRecovery,
            "pure colors with optional combo goals do not enter the ordinary shape branch");
        const auto expected = Evaluate(BoardCells(board), primary);
        for (unsigned variant = 0; variant < 2; ++variant)
            Check(SameScore(expected, Evaluate(BoardCells(board), Alternate(primary, variant))),
                "shape-only weights cannot change primary or recovery color scores");
    }
}

static void FishingTransitionKeys() {
    PHOrbBoard board{}; board.width = 6; board.height = 5;
    board.spinnerMask = UINT64_C(1) << 7; board.spinnerColors = 3;
    for (unsigned i = 0; i < 30; ++i) board.tokens[i] = i + 1;
    PHOrbRoute unused{}; unused.count = 5; unused.diagonal = true;
    const uint8_t path[] = {0,6,1,2,8}; std::copy(std::begin(path), std::end(path), unused.cells);
    auto used = unused; used.cells[2] = 7; used.fishing = true; used.fishCell = 7; used.fishColor = 0;
    PHOrbBoard left{}, right{};
    Check(PHOrbReplay(&board, &unused, 4, &left) && PHOrbReplay(&board, &used, 4, &right),
        "two legal equal-length routes can reach the same color layout before and after taking a spinner");
    Node fresh, spent;
    fresh.cells = BoardCells(left); spent.cells = BoardCells(right);
    fresh.length = spent.length = 5; spent.fished = true;
    std::copy(unused.cells, unused.cells + unused.count, fresh.path.begin());
    std::copy(used.cells, used.cells + used.count, spent.path.begin());
    const auto context = MakeContext(board, Goal(PHOrbGoalCombo,0,1), true, false, true, {}, 7, 0);
    Check(fresh.cells == spent.cells && fresh.path[0] == spent.path[0] &&
        fresh.path[3] == spent.path[3] && fresh.path[4] == spent.path[4] &&
        Hash(fresh,30) != Hash(spent,30) && LayoutKey(fresh,context) != LayoutKey(spent,context),
        "visited and layout diversity keys preserve whether the single spinner entry is still available");
    unused.fishing = true; unused.fishCell = 7; unused.fishColor = 0;
    unused.cells[unused.count++] = 7; used.cells[used.count++] = 7;
    Check(PHOrbReplay(&board, &unused, 5, &left) && !PHOrbReplay(&board, &used, 5, &right),
        "the identical next hop is legal only for the history that has not used its spinner entry");
}

#include "orb_route_simplification_tests.inc"
#include "orb_fishing_model_tests.inc"
#include "orb_fishing_budget_tests.inc"

int main() {
    AllMetScores();
    PartialAndCacheScores();
    RankingAndThornSafety();
    ColorScoresUnaffected();
    FishingTransitionKeys();
    RouteSimplificationTests();
    FishingModelTests();
    FishingBudgetTests();
    std::printf("Private shape guidance tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
