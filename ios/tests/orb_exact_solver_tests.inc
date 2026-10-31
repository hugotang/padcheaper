// Exact length is the size of an actually removed first-wave group. It is
// independent of the conservative L/T/cross templates and of later gravity.
static void ExactLengthSolverTests() {
    PHOrbEvaluation result{};
    PHOrbRoute route{};
    std::atomic_bool cancel{false};
    const auto exact5 = Goals(PHOrbGoalExact, 1, 1, 5);
    for (const auto &positions : std::vector<std::vector<unsigned>>{
            {0,1,2,3,4}, {0,6,12,18,24}, {0,6,12,13,14}, {1,6,7,8,13}, {0,1,2,7,13}}) {
        Check(PHOrbEvaluate(ShapeBoard(6,5,positions), exact5, result) && result.goalsMet && result.goalHits[0] == 1,
            "exact five accepts a straight line, L, cross or T as one removed five-orb group");
    }
    auto unmarked = ShapeBoard(6,5,{0,1,2,3,4,7});
    Check(PHOrbEvaluate(unmarked, exact5, result) && result.goalsMet,
        "an unmarked side neighbour does not enlarge an actually removed exact five");
    for (const auto &positions : std::vector<std::vector<unsigned>>{
            {0,1,2,3}, {0,1,2,3,4,5}, {0,1,2,3,4,6,7,8}, {0,1,6,7,12}}) {
        Check(PHOrbEvaluate(ShapeBoard(6,5,positions), exact5, result) && !result.goalsMet,
            "four, six, merged groups and five connected orbs without five marked cells fail exact five");
    }
    auto pairs = ShapeBoard(6,5,{0,1,2,3,4,24,25,26,27,28});
    Check(PHOrbEvaluate(pairs, Goals(PHOrbGoalExact,1,2,5), result) && result.goalHits[0] == 2 && result.goalsMet,
        "two separate exact groups satisfy a count of two");
    auto same = exact5;
    same.items[same.count++] = {PHOrbGoalMatch,1,1,5};
    Check(PHOrbEvaluate(pairs,same,result) && result.goalsMet,
        "one exact five can also satisfy an at-least-five clause");

    // Clear the five red cells beneath the staggered hearts; gravity then
    // creates a heart five, which the first-wave-only exact goal must reject.
    auto falling = ShapeBoard(6,5,{16,22,26,27,28});
    for (unsigned i : {24u,25u,20u,21u,10u}) falling.types[i] = 5;
    Check(PHOrbEvaluate(falling,Goals(PHOrbGoalExact,32,1,5),result) && !result.goalsMet &&
        PHOrbEvaluate(falling,Goals(PHOrbGoalMatch,32,1,5),result) && result.goalsMet,
        "later deterministic gravity cannot satisfy a first-wave exact clause");

    auto spinner = ShapeBoard(6,5,{24,25,26,27,28});
    spinner.spinnerMask = UINT64_C(1) << 29; spinner.spinnerColors = 3;
    spinner.types[29] = 1;
    Check(PHOrbEvaluate(spinner,exact5,result) && !result.goalsMet,
        "an end spinner's six-orb phase invalidates an otherwise exact five");
    spinner.spinnerMask = UINT64_C(1) << 19; spinner.types[19] = 1;
    Check(PHOrbEvaluate(spinner,exact5,result) && result.goalsMet,
        "an unmarked side spinner preserves an exact five in both phases");
    spinner.spinnerMask = 0; spinner.spinnerColors = 0; spinner.unmatchableColors = 1;
    Check(PHOrbEvaluate(spinner,exact5,result) && !result.goalsMet,
        "exact matching respects unmatchable colors");
    spinner.unmatchableColors = 0; spinner.minErase = 4;
    Check(PHOrbEvaluate(spinner,exact5,result) && result.goalsMet,
        "a straight exact five respects a native four-orb minimum run");
    spinner.minMatch = 6;
    Check(PHOrbEvaluate(spinner,exact5,result) && !result.goalsMet &&
        PHOrbSolve(spinner,{1,32,1000,exact5},cancel,route) == PHOrbSolveStatus::InsufficientOrbs,
        "native six-orb minimum cannot be relaxed by an exact-five goal");

    auto dangerous = ShapeBoard(6,5,{24,25,26,27,28});
    for (unsigned i : {0u,1u,2u}) dangerous.types[i] = PHOrbPoison;
    Check(PHOrbEvaluate(dangerous,exact5,result) && result.goalsMet && !result.hazardsSafe,
        "meeting exact length never bypasses release hazards");
    for (auto dimensions : {std::pair<unsigned,unsigned>{6,5},{7,6},{5,4}}) {
        const unsigned w = dimensions.first, h = dimensions.second;
        auto near = ShapeBoard(w,h,{0,1,2,3,4});
        std::swap(near.types[4],near.types[w+4]);
        near.fixedStart = uint8_t(w+5); TapeExcept(near,{4,w+4});
        auto before = near;
        PHOrbBoard replay{};
        Check(PHOrbSolve(near,{1,32,1000,exact5},cancel,route) == PHOrbSolveStatus::Solved &&
            route.goalsMet && route.count == 2 && route.cells[0] == w+4 && route.cells[1] == 4 &&
            PHOrbReplay(&near,&route,1,&replay) && PHOrbEvaluate(replay,exact5,result) && result.goalsMet &&
            std::memcmp(&near,&before,sizeof(near)) == 0,
            "one legal scoped swap repairs exact five with replay and immutable source on each geometry");
    }
    auto invalid = exact5; invalid.items[0].length = 0;
    Check(!PHOrbGoalsValid(&invalid),"exact goals require a length");
    invalid.items[0].length = 43;
    Check(!PHOrbGoalsValid(&invalid),"exact lengths cannot exceed the supported board");
    invalid = exact5; invalid.items[0].kind = uint8_t(PHOrbGoalExact + 1);
    Check(!PHOrbGoalsValid(&invalid),"new exact goal does not admit unknown kind values");
}
