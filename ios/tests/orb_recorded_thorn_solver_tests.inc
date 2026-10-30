static void RecordedThornSolverTests() {
    std::atomic_bool cancel{false}; PHOrbRoute route{}; PHOrbBoard replay{};
    PHOrbEvaluation evaluation{}; uint32_t damage = 0; uint8_t contacts = 0;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = LightTHeartFourGoals(); options.goals.count = 2;
    options.thornComboPriority = true; options.thorns = {465209,697814,0,true};
    auto board = ShapeBoard(6,5,{});
    const uint8_t types[] = {1,0,5,1,3,1, 3,5,3,3,1,1, 3,3,2,1,5,5,
                             5,3,5,1,4,4, 3,3,4,0,5,3};
    std::copy(std::begin(types),std::end(types),board.types);
    board.spinnerMask = UINT64_C(1) << 14; board.spinnerColors = 63;
    for (unsigned cell : {0u,4u,5u,8u,11u,12u,14u,18u,20u,23u}) {
        board.flags[cell] = PHOrbFlagThorn; board.thornDamage[cell] = 4;
    }
    const uint8_t witness[] = {19,20,21,22,16,10,4,5,11,10,9,8,2,1,7,13,19,18,12,6,7,8,2,
        3,4,5,11,17,23,29,28,22,16,17,23,22,21,15,16,17,11,10,16,22,23,17};
    route.count = sizeof(witness); std::copy(std::begin(witness),std::end(witness),route.cells);
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && evaluation.goalsMet &&
        evaluation.hazardsSafe && evaluation.combos == 5 && contacts == 15 && damage == 418695,
        "second 0.5.9 stopped board has a 45-step witness within its exact current HP");
    const auto original = board;
    const auto status = PHOrbSolve(board,options,cancel,route);
    Check(status == PHOrbSolveStatus::Solved && route.count <= 49 && route.goalsMet && route.hazardsSafe &&
        route.goalHits[0] >= 1 && route.goalHits[1] >= 1 && uint64_t(route.thornDamage) < options.thorns.hp &&
        !std::memcmp(&board,&original,sizeof(board)),
        "spinner/thorn shape search solves the second recorded miss within the original 800 ms budget");
    unsigned minT = 42, minFour = 42, minCombo = 42;
    Check(PHOrbReplay(&board,&route,route.count - 1,&replay), "new recorded route replays without touching the spinner");
    for (unsigned phase = 0; phase < 6; ++phase) {
        auto variant = replay; variant.types[14] = uint8_t((board.types[14] + phase) % 6);
        minT = std::min(minT,ConservativeTOracle(variant,3));
        minFour = std::min(minFour,FourOracle(variant,5));
        minCombo = std::min(minCombo,Oracle(variant).first);
    }
    Check(minT == route.goalHits[0] && minFour == route.goalHits[1] && minCombo == route.combos &&
        PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && evaluation.goalsMet &&
        evaluation.hazardsSafe && contacts == route.thornContacts && damage == route.thornDamage,
        "all six phases and moving thorn damage independently agree on the recovered route");
    std::printf("Recorded spinner/thorn fixture: goals %u/2, %u steps, %u combos, %u ms, %u contacts, damage %u\n",
        route.matchedGoals,route.count ? route.count - 1 : 0,route.combos,route.elapsedMS,route.thornContacts,route.thornDamage);
    // The heart four is deliberately absent early; it must be present when
    // released, not a condition that eliminates all such repair branches.
    route.count = 9; std::copy(std::begin(witness),std::begin(witness) + route.count,route.cells);
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && contacts > 0 &&
        !evaluation.heartFours && !evaluation.hazardsSafe,
        "unfinished thorn repair remains non-executable until a real first-wave heart four exists");
    route.count = sizeof(witness); std::copy(std::begin(witness),std::end(witness),route.cells);
    options.thorns.hp = 418695;
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && !evaluation.hazardsSafe,
        "a recovered route still refuses HP equal to accumulated thorn damage");
    options.thorns.hp = 465209; options.thorns.pendingDamage = 465209 - 418695;
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && !evaluation.hazardsSafe,
        "pending damage cannot be paid by future healing on the recovered route");
    options.thorns = {465209,697814,0,true}; options.milliseconds = 1;
    const auto began = std::chrono::steady_clock::now();
    PHOrbSolve(board,options,cancel,route);
    Check(std::chrono::steady_clock::now() - began < std::chrono::milliseconds(300) &&
        route.count <= 49 && route.searchPasses <= 5,
        "focused thorn repairs share the one-millisecond deadline and existing pass bound");
    if (route.count) Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) &&
        evaluation.hazardsSafe == route.hazardsSafe && evaluation.goalsMet == route.goalsMet,
        "a partial short-deadline thorn candidate retains the final release gates");
    // Fourth log has only three stable hearts. A color-dependent spinner is
    // not credited as an unconditional fourth orb by the current policy.
    const uint8_t shortage[] = {3,0,5,1,3,0, 3,5,3,0,0,3, 1,4,3,3,3,3,
                               3,3,3,0,3,3, 3,5,3,0,3,0};
    board = ShapeBoard(6,5,{}); std::copy(std::begin(shortage),std::end(shortage),board.types);
    board.spinnerMask = UINT64_C(1) << 13; board.spinnerColors = 63;
    options.milliseconds = 800; options.thorns = {};
    Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::InsufficientOrbs &&
        !route.count && !route.evaluated && !route.searchPasses,
        "fourth recorded log is an immediate heart shortage, not a search timeout");
}
