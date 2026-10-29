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
}
