static void ThornSolverTests() {
    PHOrbEvaluation evaluation{}; uint32_t damage = 0; uint8_t contacts = 0;
    PHOrbRoute route{}; route.count = 2; route.cells[0] = 28; route.cells[1] = 29;
    auto board = ShapeBoard(6,5,{});
    for (unsigned i = 0; i < 4; ++i) board.types[i] = 5;
    board.flags[29] = PHOrbFlagThorn; board.thornDamage[29] = 4;
    PHOrbSolveOptions options{4,128,1000,Goals(PHOrbGoalFour,32)};
    options.thorns = {1000,1000,0,true};
    Check(PHOrbThornDamage(4,1000) == 40 && PHOrbThornDamage(1,1) == 1 &&
          PHOrbThornDamage(127,INT32_MAX) == INT32_MAX,
          "thorn upper bounds match native percentage rounding, minimum one and integer saturation");
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && evaluation.hazardsSafe &&
          evaluation.heartFours == 1 && damage == 40 && contacts == 1,
          "four first-wave hearts and sufficient current HP permit a damaging thorn contact");
    for (unsigned fault = 0; fault < 6; ++fault) {
        auto changed = board; auto limits = options;
        if (fault == 0) limits.thorns.known = false;
        if (fault == 1) limits.thorns.hp = 40;
        if (fault == 2) limits.thorns.pendingDamage = 960;
        if (fault == 3) changed.types[3] = 1;
        if (fault == 4) changed.unmatchableColors = 32;
        if (fault == 5) { changed.spinnerMask = 1; changed.spinnerColors = 33; }
        Check(PHOrbEvaluateRoute(changed,route,limits,evaluation,damage,contacts) && !evaluation.hazardsSafe,
              "unknown or lethal budgets, missing/unmatchable hearts and spinner-dependent heart fours reject contact");
    }
    options.thorns.hp = 41;
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && evaluation.hazardsSafe,
          "one HP beyond the complete upper bound remains admissible without crediting healing");
    options.thorns = {1000,1000,0,true};
    // Same target token moves to the old held cell after every swap.
    route.count = 6;
    const uint8_t revisit[] = {28,29,23,22,28,29}; std::copy(std::begin(revisit),std::end(revisit),route.cells);
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && damage == 80 && contacts == 2,
          "looping back charges the moving thorn token twice, not the original board coordinate");
    route.count = 2; route.cells[0] = 29; route.cells[1] = 28;
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && damage == 0 && !contacts,
          "holding a thorn at the starting cell adds no target-contact damage");
    for (unsigned i = 0; i < 4; ++i) board.types[i] = 1 + i % 4;
    options.goals = {};
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && evaluation.hazardsSafe,
          "a contact-free route may carry its initially held thorn without a heart requirement");
    for (auto dimensions : {std::pair<unsigned,unsigned>{6,5}, {7,6}, {5,4}}) {
        auto solve = ShapeBoard(dimensions.first,dimensions.second,{});
        unsigned n = solve.width * solve.height;
        for (unsigned i = 0; i < 4; ++i) solve.types[i] = 5;
        TapeExcept(solve,{n-2,n-1}); solve.fixedStart = uint8_t(n-1);
        solve.flags[n-1] |= PHOrbFlagThorn; solve.thornDamage[n-1] = 4;
        options.goals = Goals(PHOrbGoalFour,32); options.thorns = {1000,1000,0,true};
        std::atomic_bool cancel{false};
        Check(PHOrbSolve(solve,options,cancel,route) == PHOrbSolveStatus::Solved && route.thornContacts == 1 &&
              route.thornDamage == 40 && route.heartFours == 1,
              "all supported geometries solve a required thorn crossing with first-wave heart four");
        options.thorns.hp = 40;
        Check(PHOrbSolve(solve,options,cancel,route) == PHOrbSolveStatus::NoRoute,
              "an unavoidable lethal contact is rejected before reaching the native input stage");
    }
    // Device turn 4: the mixed beam stopped at 2 Combo / 13 steps despite a
    // zero-contact 7 Combo solution. Known HP must not remove that subspace.
    auto recorded = ShapeBoard(6,5,{});
    const uint8_t recordedTypes[] = {3,5,5,3,3,5, 2,1,3,1,5,3, 5,1,5,0,3,5,
                                    0,3,5,3,5,3, 3,3,0,3,4,4};
    std::copy(std::begin(recordedTypes),std::end(recordedTypes),recorded.types);
    for (unsigned cell : {1u,16u,24u,25u,27u}) {
        recorded.flags[cell] = PHOrbFlagThorn; recorded.thornDamage[cell] = 4;
    }
    options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.milliseconds = 1000;
    options.goals.count = 2;
    options.goals.items[0] = {PHOrbGoalT,8,1,0}; options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    options.thorns = {459648,459648,0,true};
    std::atomic_bool cancel{false};
    Check(PHOrbSolve(recorded,options,cancel,route) == PHOrbSolveStatus::Solved && route.combos >= 7 &&
        !route.thornContacts && !route.thornDamage && route.searchPasses >= 2 && route.count <= 49,
        "remaining-budget contact-free refinement recovers high combos without additional thorn damage");
    Check(PHOrbEvaluateRoute(recorded,route,options,evaluation,damage,contacts) && evaluation.goalsMet &&
        evaluation.hazardsSafe && evaluation.combos == route.combos && !damage && !contacts,
        "refined device route independently replays to both shapes with zero thorn contacts");
    recorded.tapeMask = UINT64_C(1) << 29;
    std::fill(std::begin(recorded.flags),std::end(recorded.flags),0);
    std::fill(std::begin(recorded.thornDamage),std::end(recorded.thornDamage),0);
    recorded.flags[29] = PHOrbFlagThorn | PHOrbFlagTape; recorded.thornDamage[29] = 4;
    options.goals = {}; options.steps = 4;
    Check(PHOrbSolve(recorded,options,cancel,route) == PHOrbSolveStatus::Solved && route.searchPasses == 1 && !route.thornContacts,
        "an unreachable thorn does not duplicate the already contact-free search");

    auto tradeoff = ShapeBoard(6,5,{});
    for (unsigned i = 0; i < 4; ++i) tradeoff.types[i] = 5;
    tradeoff.types[25] = 2; tradeoff.types[26] = 0; tradeoff.types[27] = 1;
    tradeoff.types[28] = tradeoff.types[29] = 0;
    TapeExcept(tradeoff,{25,26,27}); tradeoff.fixedStart = 27;
    tradeoff.flags[27] |= PHOrbFlagThorn; tradeoff.thornDamage[27] = 4;
    options = {1,32,1000,Goals(PHOrbGoalFour,32)}; options.thorns = {1000,1000,0,true};
    PHOrbRoute lowerDamage{}, higherCombo{};
    Check(PHOrbSolve(tradeoff,options,cancel,lowerDamage) == PHOrbSolveStatus::Solved && !lowerDamage.thornContacts,
        "default priority prefers the safe no-contact direction even when it loses a combo");
    options.thornComboPriority = true;
    Check(PHOrbSolve(tradeoff,options,cancel,higherCombo) == PHOrbSolveStatus::Solved && higherCombo.thornContacts == 1 &&
        higherCombo.combos > lowerDamage.combos && higherCombo.thornDamage == 40 && higherCombo.heartFours == 1,
        "Combo priority chooses the additional match through an affordable thorn with a first-wave heart four");
    PHOrbBoard replay{};
    Check(PHOrbReplay(&tradeoff,&higherCombo,higherCombo.count - 1,&replay) &&
        Oracle(replay).first == higherCombo.combos && FourOracle(replay,5) == 1,
        "the Combo-priority tradeoff matches an independent removal and heart-four oracle");
    for (unsigned fault = 0; fault < 5; ++fault) {
        auto limited = options; auto changed = tradeoff;
        if (fault == 0) limited.thorns.known = false;
        if (fault == 1) limited.thorns.hp = 40;
        if (fault == 2) limited.thorns.pendingDamage = 960;
        if (fault == 3) changed.types[3] = 2;
        if (fault == 4) changed.unmatchableColors = 32;
        const auto status = PHOrbSolve(changed,limited,cancel,route);
        Check(status != PHOrbSolveStatus::Solved || (!route.thornContacts && !route.thornDamage),
            "Combo priority cannot credit later healing, unknown HP or a missing/unmatchable heart four");
    }
    tradeoff.types[25] = tradeoff.types[27] = tradeoff.types[26];
    Check(PHOrbSolve(tradeoff,options,cancel,route) == PHOrbSolveStatus::Solved && !route.thornContacts,
        "equal-Combo routes still prefer lower thorn damage");
}
