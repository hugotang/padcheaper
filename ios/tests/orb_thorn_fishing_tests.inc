// Hand witness: 7 -> 14 -> 8 -> 14 -> 15. The thorn on donor 8 is
// touched on exit, deposited into source 14, then touched again on re-entry.
// These cases are source-only until the next authorized build/test.
static void ThornFishingTests() {
    auto board = FishingGeometryBoard(false); auto options = FishingGeometryOptions();
    options.steps = 4; // Longer detours can avoid the repeated donor contact.
    auto witness = FishingGeometryWitness();
    board.flags[8] |= PHOrbFlagThorn; board.thornDamage[8] = 4;
    board.enhancement[8] = 0x3f800000; board.blindTurns[8] = 2; // float32 +1
    PHOrbBoard replay{}; PHOrbEvaluation evaluation{};
    Check(!PHOrbPlanMultiFishing(board,options).valid &&
        !PHOrbReplayMultiFishing(board,witness,4,replay), "thorn fishing defaults closed in planner and replay");
    options.allowThornFishing = witness.allowThornFishing = true;
    options.thorns = {81,1000,0,true};
    Check(PHOrbReplayMultiFishing(board,witness,4,replay) &&
        PHOrbEvaluateMultiFishing(board,witness,options,nullptr,evaluation) && evaluation.goalsMet && evaluation.hazardsSafe,
        "two contacts costing 80 leave one HP and satisfy the independent first-wave heart four");
    Check(replay.tokens[8] == board.tokens[8] && replay.thornDamage[8] == 4 &&
        (replay.flags[8] & PHOrbFlagThorn) && replay.enhancement[8] == 0x3f800000 && replay.blindTurns[8] == 2 && replay.types[8] == 5,
        "thorn donor retains its identity and metadata after leaving the spinner as a heart");
    PHOrbRoute route{}; route.count = witness.count; route.diagonal = true;
    route.fishing = true; route.fishingExchangeCount = 2; route.fishCell = 14; route.allowThornFishing = true;
    std::copy(witness.cells,witness.cells + witness.count,route.cells);
    route.fishingExchanges[0] = {1,2,5,2}; route.fishingExchanges[1] = {3,4,5,2};
    uint32_t damage = 0; uint8_t contacts = 0;
    Check(PHOrbEvaluateRoute(board,route,options,evaluation,damage,contacts) && damage == 80 && contacts == 2 && evaluation.hazardsSafe,
        "executable C route and multi planner agree on repeated contact damage");
    auto disabled = options; disabled.allowThornFishing = false;
    Check(!PHOrbEvaluateMultiFishing(board,witness,disabled,nullptr,evaluation) &&
        !PHOrbEvaluateRoute(board,route,disabled,evaluation,damage,contacts), "saved thorn route cannot outlive its option");
    // Independent swaps track all metadata, including a thorn already on the source.
    for (unsigned thornCell : {7u,8u,14u,15u,29u}) {
        auto marked = FishingGeometryBoard(false), manual = marked;
        marked.flags[thornCell] |= PHOrbFlagThorn; marked.thornDamage[thornCell] = 4;
        manual = marked;
        unsigned expectedContacts = 0;
        for (unsigned step = 1; step < route.count; ++step) {
            const unsigned a = route.cells[step - 1], b = route.cells[step];
            if (b == 14) manual.types[b] = 5;
            expectedContacts += (manual.flags[b] & PHOrbFlagThorn) != 0;
            std::swap(manual.types[a],manual.types[b]); std::swap(manual.flags[a],manual.flags[b]);
            std::swap(manual.tokens[a],manual.tokens[b]); std::swap(manual.thornDamage[a],manual.thornDamage[b]);
            std::swap(manual.enhancement[a],manual.enhancement[b]); std::swap(manual.blindTurns[a],manual.blindTurns[b]);
        }
        auto ample = options; ample.thorns.hp = 1000;
        Check(PHOrbReplay(&marked,&route,4,&replay) && PHOrbSameBoard(&manual,&replay) &&
            PHOrbEvaluateRoute(marked,route,ample,evaluation,damage,contacts) && contacts == expectedContacts &&
            damage == expectedContacts * 40 && evaluation.hazardsSafe,
            "held, source, donor and untouched thorns agree with independent identity and damage replay");
    }
    for (unsigned step = 1; step < route.count; ++step) {
        PHOrbBoard prefix{}, after{};
        Check(PHOrbReplay(&board,&route,step - 1,&prefix), "thorn fishing prefix replay");
        if (route.cells[step] == 14) prefix.types[14] = 5;
        Check(PHOrbReplayFishingStep(&prefix,&route,step,&after), "live step preserves thorn fields through take and donor exit");
    }
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        auto unsafe = options;
        if (scenario == 0) unsafe.thorns.hp = 80;
        if (scenario == 1) unsafe.thorns.pendingDamage = 1;
        if (scenario == 2) unsafe.thorns.known = false;
        if (scenario == 3) unsafe.thorns.maximumHP = 2000;
        Check(PHOrbEvaluateMultiFishing(board,witness,unsafe,nullptr,evaluation) && !evaluation.hazardsSafe,
            "exact lethal, pending damage, unknown HP and changed maximum cannot borrow future healing");
    }
    auto prefix = board;
    Check(PHOrbReplay(&board,&route,2,&prefix), "confirmed prefix after first thorn contact");
    auto budget = options.thorns; budget.pendingDamage = 40;
    for (unsigned frame = 0; frame < 10; ++frame)
        Check(PHOrbRemainingThornBudget(&prefix,&route,2,&budget,&damage,&contacts) && damage == 40 && contacts == 1,
            "stationary fishing waits neither repeat past contact damage nor remove the remaining one");
    budget.pendingDamage = 41;
    Check(!PHOrbRemainingThornBudget(&prefix,&route,2,&budget,&damage,&contacts), "changed pending damage revokes the remaining route");
    auto triple = board; triple.types[10] = 2;
    auto tripleOptions = options; tripleOptions.goals = Goals(PHOrbGoalMatch,32); tripleOptions.goals.items[0].length = 3;
    auto tripleRoute = witness; tripleRoute.plan.goals = tripleOptions.goals;
    Check(PHOrbEvaluateMultiFishing(triple,tripleRoute,tripleOptions,nullptr,evaluation) && evaluation.heartMatches &&
        !evaluation.heartFours && !evaluation.hazardsSafe, "heart triple alone cannot satisfy default thorn recovery");
    tripleOptions.allowThornsWithHeartMatch = true;
    Check(PHOrbEvaluateMultiFishing(triple,tripleRoute,tripleOptions,nullptr,evaluation) && evaluation.hazardsSafe,
        "explicit heart-match policy allows a triple in every terminal spinner phase");
    triple.unmatchableColors = 32;
    Check(PHOrbEvaluateMultiFishing(triple,tripleRoute,tripleOptions,nullptr,evaluation) && !evaluation.hazardsSafe,
        "unmatchable hearts cannot pay the thorn recovery requirement");
    auto phaseDependent = board; phaseDependent.types[2] = phaseDependent.types[7] = 5;
    phaseDependent.types[9] = phaseDependent.types[10] = 2;
    auto phaseRoute = witness; phaseRoute.count = 3; phaseRoute.exchangeCount = phaseRoute.plan.exchangeCount = 1;
    phaseRoute.plan.take[5] = phaseRoute.plan.leave[2] = 1; phaseRoute.plan.goals = {};
    auto phaseOptions = options; phaseOptions.goals = {}; phaseOptions.allowThornsWithHeartMatch = true;
    Check(PHOrbReplayMultiFishing(phaseDependent,phaseRoute,2,replay), "phase-dependent heart layout replays");
    for (unsigned color : {2u,5u}) {
        auto concrete = replay; concrete.types[14] = uint8_t(color); concrete.spinnerMask = concrete.spinnerColors = 0;
        Check(PHOrbEvaluateWithOptions(concrete,phaseOptions,evaluation) && bool(evaluation.heartMatches) == (color == 5),
            "only the heart-colored terminal source creates the vertical three-heart run");
    }
    Check(PHOrbEvaluateMultiFishing(phaseDependent,phaseRoute,phaseOptions,nullptr,evaluation) &&
        !evaluation.heartMatches && !evaluation.hazardsSafe,
        "a heart match in only the heart-colored spinner phase cannot authorize thorn release");
    auto held = FishingGeometryBoard(false); held.flags[7] |= PHOrbFlagThorn; held.thornDamage[7] = 4;
    auto unknown = options; unknown.thorns = {};
    Check(PHOrbEvaluateMultiFishing(held,witness,unknown,nullptr,evaluation) && evaluation.hazardsSafe,
        "holding a thorn throughout the route adds no target contacts and needs no unknown HP credit");
    auto locked = board; locked.flags[29] |= PHOrbFlagLocked;
    Check(!PHOrbPlanMultiFishing(locked,options).valid, "thorn opt-in does not authorize locked-token fishing");
    const PHOrbTimedFishingTiming timing{1200,2,12,12,10};
    std::atomic_bool cancel{false}; PHOrbMultiFishingResult result{};
    Check(PHOrbSolveMultiFishing(board,options,nullptr,timing,2,cancel,result) == PHOrbSolveStatus::Solved &&
        result.route.thornDamage == 80 && result.route.thornContacts == 2 && result.route.allowThornFishing &&
        PHOrbEvaluateMultiFishing(board,result.route,options,nullptr,evaluation) && evaluation.hazardsSafe,
        "bounded solver tracks displaced thorn donors and exports its independently replayable damage");
    auto lethal = options; lethal.thorns.hp = 80;
    Check(PHOrbSolveMultiFishing(board,lethal,nullptr,timing,2,cancel,result) != PHOrbSolveStatus::Solved,
        "the four-step repeated-thorn geometry cannot publish a lethal solution");
}
