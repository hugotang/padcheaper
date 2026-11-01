static void EnemyComboSolverTests() {
    auto cascade = ShapeBoard(6,5,{26,27,28});
    cascade.types[24] = cascade.types[25] = cascade.types[20] = 5;
    const auto expected = EffectOracle(cascade);
    Check(expected.combos > expected.firstCombos && expected.firstCombos == 1,
        "fixture has a heart group that forms only after the first removal");
    auto goals = Goals(PHOrbGoalCombo,0,expected.combos);
    PHOrbEvaluation result{};
    Check(PHOrbEvaluate(cascade,goals,result) && result.goalsMet && result.combos == expected.combos,
        "ordinary saved Combo goals retain deterministic cascade semantics");
    goals.comboFirstWave = true;
    Check(PHOrbEvaluate(cascade,goals,result) && !result.goalsMet && result.goalHits[0] == expected.firstCombos &&
        result.firstWaveCombos == expected.firstCombos && result.combos == expected.combos,
        "enemy countermeasure cannot use a later cascade as a guaranteed lower bound");
    goals.items[0].count = uint8_t(expected.firstCombos);
    Check(PHOrbEvaluate(cascade,goals,result) && result.goalsMet,
        "a proved first-removal threshold passes without changing the total score");
    goals.items[0].count = 1;
    for (unsigned seed = 1; seed <= 40; ++seed) {
        auto board = Board(6,5,seed);
        board.spinnerMask = (UINT64_C(1) << 0) | (UINT64_C(1) << 29); board.spinnerColors = 63;
        unsigned minimum = PHOrbMaxCells;
        for (unsigned phase = 0; phase < 6; ++phase) {
            auto sample = board; sample.spinnerMask = 0; sample.spinnerColors = 0;
            for (unsigned cell : {0u,29u}) sample.types[cell] = uint8_t((board.types[cell] + phase) % 6);
            minimum = std::min(minimum,EffectOracle(sample).firstCombos);
        }
        Check(PHOrbEvaluate(board,goals,result) && result.firstWaveCombos == minimum && result.goalHits[0] == minimum &&
            result.goalsMet == (minimum >= 1), "first-removal credit uses the independent minimum across all shared spinner phases");
    }
    auto near = ShapeBoard(6,5,{24,25,27});
    for (unsigned cell : {12u,13u,14u}) near.types[cell] = 5;
    TapeExcept(near,{26,27}); near.fixedStart = 27;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.steps = 1; options.milliseconds = 1000; options.goals = Goals(PHOrbGoalCombo,0,2);
    options.goals.comboFirstWave = true;
    std::atomic_bool cancel{false}; PHOrbRoute route{}; PHOrbBoard replay{};
    Check(PHOrbSolve(near,options,cancel,route) == PHOrbSolveStatus::Solved && route.goalsMet &&
        PHOrbReplay(&near,&route,route.count - 1,&replay) && EffectOracle(replay).firstCombos >= 2,
        "the solver can complete a first-removal countermeasure through a real adjacent swap");
}
