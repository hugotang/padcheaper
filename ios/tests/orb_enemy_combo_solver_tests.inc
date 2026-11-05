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
    auto trigger = Goals(PHOrbGoalMatch,32,1,3);
    Check(PHOrbEvaluate(cascade,trigger,result) && result.goalsMet,"legacy heart-match goal permits an existing-orb cascade");
    trigger.bonusFirstWave = true;
    Check(PHOrbEvaluate(cascade,trigger,result) && !result.goalsMet,
        "heart match in a later cascade cannot authorize a leader Combo credit");
    trigger = Goals(PHOrbGoalColors,32,1); trigger.bonusFirstWave = true;
    Check(PHOrbEvaluate(cascade,trigger,result) && !result.goalsMet,
        "color-count bonus triggers also require first-removal evidence");
    auto wideColumn = ShapeBoard(6,5,{2,8,14,20,26,12,13});
    trigger = Goals(PHOrbGoalColumn,1,1);
    Check(PHOrbEvaluate(wideColumn,trigger,result) && result.goalsMet,
        "ordinary column goals allow connected horizontal branches");
    trigger.bonusFirstWave = true;
    Check(PHOrbEvaluate(wideColumn,trigger,result) && !result.goalsMet,
        "a column with a removed side branch cannot prove native column-awakening activation");
    auto exactColumn = ShapeBoard(6,5,{2,8,14,20,26});
    Check(PHOrbEvaluate(exactColumn,trigger,result) && result.goalsMet,
        "one full-height single-column removal proves the stricter bonus trigger");
    PHOrbComboBonusState bonus{}; bonus.valid = true; bonus.count = 1; bonus.attackColors = 8;
    bonus.awakeningColors[4] = 8;
    bonus.effects[0] = PHOrbComboEffect{16,2,PHOrbComboAwakenings,0,1,{}};
    auto t = Goals(PHOrbGoalT,8,1); unsigned sources[3]{};
    Check(PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources) == 2 && sources[0] == 2,
        "T awakening bonus requires a matching live attribute provider");
    bonus.awakeningColors[4] = 16;
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"dark-only T providers do not activate from a light T");
    bonus.effects[0] = PHOrbComboEffect{9,3,PHOrbComboAllLinks,0,5,{}};
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"all-color connected bonus cannot be satisfied by one selected color");
    t.items[t.count++] = PHOrbGoal{PHOrbGoalT,1,1,0};
    Check(PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources) == 3,"each selected color's length is guaranteed by its own T");
    bonus.effects[0] = PHOrbComboEffect{1,4,PHOrbComboGroups,1,2,{3,3,255,255,255}};
    t = Goals(PHOrbGoalT,8,1); t.items[t.count++] = PHOrbGoal{PHOrbGoalMatch,8,1,3};
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"one group meeting two overlapping goals is not two light combos");
    t.items[0].count = 2;
    Check(PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources) == 4,"two distinct light groups satisfy a repeated-color leader condition");
    bonus.effects[0].colors[1] = 128; bonus.effects[0].colors[2] = 3;
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"any negative native color terminates the group list");
    bonus.effects[0].colors[1] = 6;
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"unsupported group colors never supply credit");
    bonus.effects[0] = PHOrbComboEffect{63,3,PHOrbComboColors,0,3,{}};
    t = Goals(PHOrbGoalColors,63,5);
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"missing attack attributes cannot be replaced by clearing inactive colors");
    bonus.attackColors = 31;
    Check(PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources) == 3,"five cleared colors guarantee a three-color trigger with all attributes active");
    bonus.effects[0].condition = 127;
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"unsupported color masks are not narrowed into a supported condition");
    bonus.effects[0] = PHOrbComboEffect{17,3,PHOrbComboAwakenings,2,1,{}};
    bonus.awakeningColors[0] = bonus.awakeningColors[4] = 8;
    t = Goals(PHOrbGoalT,8,1);
    Check(!PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources),"a conjunction badge needs every specified awakening trigger");
    t.items[t.count++] = PHOrbGoal{PHOrbGoalFour,8,1,0};
    Check(PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources) == 3,"independently guaranteed four and T satisfy the conjunction badge");
    bonus.effects[0] = PHOrbComboEffect{126,2,PHOrbComboAwakening,2,1,{}};
    t = Goals(PHOrbGoalT,8,2);
    Check(PHOrbGuaranteedComboBonuses(&bonus,&t,6,5,sources) == 2,"repeated-awakening bonuses credit only the proved first occurrence");
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
