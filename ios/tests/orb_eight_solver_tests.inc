static void EightSolverTests() {
    auto s = MultiplayerModelState(8,7), other = s;
    other.wave = 1; ++other.multiplayer.turnSerial; other.multiplayer.resolvePending = 1; other.multiplayer.networkFlags = 3;
    Check(PHOrbEightMatchReady(&other) && PHOrbSameEightMatch(&s,&other) && !PHOrbSameTurn(&s,&other) &&
        !PHOrbMultiplayerWindow(&other),"8P match identity survives local resolve while its gesture window closes");
    other = s; ++other.multiplayer.clockStart;
    Check(!PHOrbSameEightMatch(&s,&other),"a reused scene with a new match clock requires another explicit Auto click");
    other = s; other.identity.entryHash[0]++;
    Check(!PHOrbSameEightMatch(&s,&other),"new entry nonce cannot inherit 8P Auto");
    other = MultiplayerModelState(4,3);
    Check(!PHOrbEightMatchReady(&other),"8P match helper never authorizes 4P");
    PHOrbSolveOptions options;
    options.activity.firstWaveOnly = true; options.activity.colors = 17;
    options.activity.comboWeight = 400; options.activity.rowWeight = 1500; options.activity.linkWeight = 2500;
    options.goals.count = 2; options.goals.items[0] = {PHOrbGoalMatch,17,1,12};
    options.goals.items[1] = {PHOrbGoalCombo,0,4,0}; options.goals.comboFirstWave = true;
    auto board = Board(6,5,4);
    const uint8_t rows[30]{0,0,0,0,0,0, 0,0,0,0,0,0, 1,1,1,4,4,4, 2,2,2,5,5,5, 3,5,1,2,3,0};
    std::copy(std::begin(rows),std::end(rows),board.types);
    PHOrbEvaluation result{};
    Check(PHOrbEvaluateWithOptions(board,options,result) && result.goalsMet && result.firstWaveCombos == 5 &&
        result.rowGroups[0] == 1 && result.linkGroups[0] == 1 && result.largestGroups[0] == 12 && result.activityValue == 6000,
        "one connected twelve-group containing two rows counts once per shape, separate from combat bonuses");
    board.types[5] = board.types[11] = 3;
    Check(PHOrbEvaluateWithOptions(board,options,result) && !result.goalsMet && !result.linkGroups[0],
        "less than twelve removed in one group does not satisfy super-link goal");
    bool foundCascade = false;
    for (unsigned seed = 1; seed <= 100 && !foundCascade; ++seed) {
        board = Board(6,5,seed); PHOrbEvaluation ordinary{}, first{};
        PHOrbGoalSet noGoals{};
        if (!PHOrbEvaluate(board,noGoals,ordinary) || ordinary.combos == ordinary.firstWaveCombos) continue;
        options.goals = noGoals;
        Check(PHOrbEvaluateWithOptions(board,options,first) && first.combos == ordinary.firstWaveCombos &&
            first.combos < ordinary.combos,"8P objective never credits deterministic cascades as guaranteed native no-skyfall combos");
        foundCascade = true;
    }
    Check(foundCascade,"test exercised a board with deterministic cascade ambiguity");
    board = Board(6,5,72); options.goals = {}; options.activity.stepCost = 35;
    options.steps = 8; options.beam = 32; options.milliseconds = 1000;
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    const auto status = PHOrbSolve(board,options,cancel,route);
    uint32_t damage = 0; uint8_t contacts = 0;
    Check(status == PHOrbSolveStatus::Solved && PHOrbEvaluateRoute(board,route,options,result,damage,contacts) &&
        result.combos == route.combos && result.goalsMet == route.goalsMet,
        "activity beam search and release replay use the same evaluation and goal policy");
}
