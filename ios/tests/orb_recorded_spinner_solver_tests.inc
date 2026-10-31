// Sanitized 0.5.13 device board. Written regression; no benchmark result is
// implied until the authorized test run. Independent oracles live alongside
// the existing exact-T and four tests.
static void RecordedSpinnerSolverTests() {
    const uint8_t types[] = {0,3,1,1,0,5, 5,4,5,3,4,3, 5,1,1,2,3,2,
                             5,0,0,2,3,3, 5,1,2,5,3,4};
    const uint8_t previousPath[] = {7,8,2,3,4,10,16,22,23,29,28,27,26,20,21,27,26,25,19,20,26,27,21};
    auto board = ShapeBoard(6,5,{});
    std::copy(std::begin(types), std::end(types), board.types);
    board.spinnerMask = UINT64_C(1) << 15; board.spinnerColors = 63;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = LightTHeartFourGoals(); options.goals.count = 2;
    PHOrbRoute route{}; route.count = sizeof(previousPath);
    std::copy(std::begin(previousPath), std::end(previousPath), route.cells);
    PHOrbEvaluation evaluation{}; uint32_t damage = 0; uint8_t contacts = 0;
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && !evaluation.goalsMet &&
        evaluation.goalHits[0] == 0 && evaluation.goalHits[1] == 1 && evaluation.hazardsSafe,
        "recorded preview near the spinner remains non-executable under the all-phase exact-T policy");
    const auto original = board;
    std::atomic_bool cancel{false};
    const auto status = PHOrbSolve(board,options,cancel,route);
    Check(status == PHOrbSolveStatus::Solved && route.goalsMet && route.hazardsSafe && route.count <= 49 &&
        std::memcmp(&board,&original,sizeof(board)) == 0,
        "spinner-only hard-shape search should recover the recorded 0.5.13 miss within the original budget");
    Check(route.evaluationCalls + route.evaluationCacheHits == route.evaluated && route.evaluationCacheHits > 0,
        "repeated color layouts reuse a bounded score cache without losing candidate accounting");
    PHOrbBoard replay{};
    if (route.count && PHOrbReplay(&board,&route,route.count - 1,&replay)) {
        unsigned minT = 42, minFour = 42, minCombo = 42;
        for (unsigned phase = 0; phase < 6; ++phase) {
            auto variant = replay; variant.types[15] = uint8_t((types[15] + phase) % 6);
            minT = std::min(minT,ConservativeTOracle(variant,3));
            minFour = std::min(minFour,FourOracle(variant,5));
            minCombo = std::min(minCombo,Oracle(variant).first);
        }
        Check(minT == route.goalHits[0] && minFour == route.goalHits[1] && minCombo == route.combos &&
            PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) &&
            evaluation.goalsMet == route.goalsMet && evaluation.hazardsSafe == route.hazardsSafe && !damage && !contacts,
            "cached search scores agree with uncached release evaluation and independent six-phase oracles");
    } else Check(false,"recorded spinner solve returns a replayable path");
    // A second solve must not reuse another problem's goals or context.
    auto other = options; other.goals = Goals(PHOrbGoalFour,8,1); other.steps = 8;
    PHOrbSolve(board,other,cancel,route);
    if (route.count) Check(PHOrbEvaluateRoute(board,route,other,evaluation,damage,contacts) &&
        evaluation.goalsMet == route.goalsMet && evaluation.hazardsSafe == route.hazardsSafe,
        "evaluation cache is local to one solve and cannot leak shape scores into another goal set");
    options.milliseconds = 1;
    const auto start = std::chrono::steady_clock::now();
    PHOrbSolve(board,options,cancel,route);
    Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300) && route.count <= 49,
        "cache hits cannot bypass the original cancellation/deadline checks");
    if (route.count) Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) &&
        evaluation.goalsMet == route.goalsMet && evaluation.hazardsSafe == route.hazardsSafe,
        "a deadline-limited cached candidate retains the exact release gates");
    cancel.store(true);
    Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Cancelled && !route.count,
        "a cancelled spinner search cannot publish a cached route");

    // A later 6x5 record has six spinner cells and no thorns. It does not
    // establish infeasibility; any returned candidate still needs all phases.
    const uint8_t denseTypes[] = {1,3,1,0,1,5, 5,3,5,3,3,5, 5,5,5,3,2,0,
                                  4,1,1,2,4,0, 1,0,4,3,2,3};
    const uint8_t densePath[] = {19,14,9,8,13,7,1,0,6,12,19,14,15,10,5};
    auto dense = ShapeBoard(6,5,{});
    std::copy(std::begin(denseTypes),std::end(denseTypes),dense.types);
    dense.spinnerMask = UINT64_C(7340060); dense.spinnerColors = 63;
    dense.flags[17] = PHOrbFlagBlind; dense.blindTurns[17] = 1;
    options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = LightTHeartFourGoals(); options.goals.count = 2;
    options.diagonal = options.thornComboPriority = options.allowPoisonWithHeartFour = true;
    route = {}; route.diagonal = true; route.count = sizeof(densePath);
    std::copy(std::begin(densePath),std::end(densePath),route.cells);
    Check(PHOrbEvaluateRoute(dense,route,options,evaluation,damage,contacts) && !evaluation.goalsMet &&
        evaluation.goalHits[0] == 0 && evaluation.goalHits[1] == 1 && evaluation.hazardsSafe,
        "six-spinner recorded path remains a heart-only preview under unchanged exact goals");
    cancel.store(false);
    const auto denseStatus = PHOrbSolve(dense,options,cancel,route);
    Check((denseStatus == PHOrbSolveStatus::Solved || denseStatus == PHOrbSolveStatus::GoalsUnmet) &&
        route.count >= 2 && route.count <= 49 && PHOrbReplay(&dense,&route,route.count - 1,&replay),
        "dense spinner hint fallback returns a bounded route without claiming feasibility from the old timeout");
    if (route.count) {
        unsigned minT = 42, minFour = 42, minCombo = 42;
        for (unsigned phase = 0; phase < 6; ++phase) {
            auto variant = replay;
            for (unsigned i : {2u,3u,4u,20u,21u,22u}) variant.types[i] = uint8_t((denseTypes[i] + phase) % 6);
            minT = std::min(minT,ConservativeTOracle(variant,3));
            minFour = std::min(minFour,FourOracle(variant,5));
            minCombo = std::min(minCombo,Oracle(variant).first);
        }
        Check(route.goalHits[0] == minT && route.goalHits[1] == minFour && route.combos == minCombo &&
            route.goalsMet == (minT > 0 && minFour > 0) &&
            PHOrbEvaluateRoute(dense,route,options,evaluation,damage,contacts) &&
            evaluation.goalsMet == route.goalsMet && evaluation.hazardsSafe == route.hazardsSafe &&
            route.evaluationCalls + route.evaluationCacheHits == route.evaluated,
            "dense spinner fallback and cache preserve independent six-phase scores and uncached release validation");
    }
}
