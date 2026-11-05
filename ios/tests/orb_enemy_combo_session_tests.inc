static void EnemyComboSessionTests() {
    {
        FakeOrbEngine *fixture = [FakeOrbEngine new]; SetAssistBoard(fixture,0);
        for (unsigned i = 0; i < 30; ++i) if (fixture->live.board.types[i] == 3) fixture->live.board.types[i] = 2;
        for (unsigned i : {0u,1u,2u,7u,13u}) fixture->live.board.types[i] = 3;
        fixture->live.enemyCombo.turns[0] = 3; fixture->live.enemyCombo.thresholds[0] = 9;
        fixture->live.automation.enemyEffects = PHOrbEnemyComboAbsorb;
        auto &bonus = fixture->live.enemyCombo.bonus;
        bonus.valid = true; bonus.count = 3; bonus.attackColors = bonus.awakeningColors[4] = 8;
        bonus.effects[0] = PHOrbComboEffect{16,2,PHOrbComboAwakenings,0,1,{}};
        bonus.effects[1] = PHOrbComboEffect{1,4,PHOrbComboGroups,1,2,{3,5,255,255,255}};
        bonus.effects[2] = PHOrbComboEffect{16,2,PHOrbComboAwakenings,2,1,{}};
        NSArray *goals = @[@{@"kind":@"t",@"colors":@8,@"count":@1},
            @{@"kind":@"four",@"colors":@32,@"count":@1}];
        PHOrbSession *test = AssistSession(fixture,AssistSettings(NO,goals));
        Check(UntilPhase(test,fixture,@"turnWaiting") && fixture->releases == 1 &&
            [test.snapshot[@"goalPlan"][@"enemyComboBeforeBonus"] isEqual:@10] &&
            [test.snapshot[@"goalPlan"][@"enemyComboRequired"] isEqual:@2] &&
            [test.snapshot[@"searchTrace"][@"enemyCountermeasure"][@"bonusSources"] isEqual:@[@2,@4,@2,@0]] &&
            [[test.snapshot[@"executionHistory"] lastObject][@"releaseEvaluation"][@"bonusTriggersFirstWaveRequired"] boolValue],
            "Auto completes light T and heart four with two actual plus eight guaranteed bonus Combos");
        [test queueStop:@"bonus execution fixture complete"]; Tick(test,fixture,Frame());
    }
    // A changed effective bonus is action evidence, including during a drag.
    for (bool held : {false,true}) {
        FakeOrbEngine *fixture = [FakeOrbEngine new]; SetAssistBoard(fixture,0);
        fixture->live.enemyCombo.bonus.valid = true; fixture->live.enemyCombo.bonus.sourceSignature = 1;
        PHOrbSession *test = AssistSession(fixture,AssistSettings(NO));
        Check(UntilPhase(test,fixture,held ? @"running" : @"arming"),"bonus race reaches the execution boundary");
        if (held) for (unsigned i = 0; i < 4 && !fixture->live.dragged; ++i) Tick(test,fixture,Frame());
        const NSUInteger before = fixture->stages;
        ++fixture->live.enemyCombo.bonus.sourceSignature;
        Check(UntilPhase(test,fixture,@"stopped") && fixture->stages == before + (held ? 1 : 0) &&
            fixture->releases == (held ? 1u : 0u),"bonus-source changes stop stale input and allow only owned release cleanup");
    }
    // Recorded 0.5.32 failure: enough light/hearts, but an added first-clear
    // Combo goal exceeds board capacity. The stop and exported reason agree.
    for (unsigned width : {5u,6u}) {
        const unsigned height = width - 1, required = width == 6 ? 13 : 7;
        FakeOrbEngine *fixture = [[FakeOrbEngine alloc] initWithWidth:width height:height];
        const uint8_t types[] = {5,2,3,0,3,1,4,0,3,5,3,5,5,5,3,0,3,5,3,4,3,2,3,5,5,4,4,1,4,5};
        std::copy(types,types + width * height,fixture->live.board.types);
        fixture->live.enemyCombo.turns[0] = 3; fixture->live.enemyCombo.thresholds[0] = uint16_t(required - 1);
        fixture->live.automation.enemyEffects = PHOrbEnemyComboAbsorb;
        NSArray *goals = @[@{@"kind":@"t",@"colors":@8,@"count":@1}, @{@"kind":@"four",@"colors":@32,@"count":@1}];
        PHOrbSession *test = AssistSession(fixture,AssistSettings(NO,goals));
        Check(UntilPhase(test,fixture,@"stopped") && !fixture->stages && !fixture->releases &&
            [test.snapshot[@"searchTrace"][@"status"] isEqual:@"insufficient-orbs"] &&
            [test.snapshot[@"message"] containsString:[NSString stringWithFormat:@"首消至少 %u Combo",required]] &&
            [test.snapshot[@"message"] containsString:width == 6 ? @"最多 10 Combo" : @"最多 6 Combo"] &&
            [test.snapshot[@"message"] containsString:@"敵人 Combo 吸收"] &&
            [test.snapshot[@"message"] isEqual:test.snapshot[@"searchTrace"][@"reason"]],
            "enemy Combo board-capacity failure names the added constraint and never submits input");
    }
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetAssistBoard(engine, 0);
    engine->live.enemyCombo.turns[0] = 3; engine->live.enemyCombo.thresholds[0] = 1;
    engine->live.automation.enemyEffects = PHOrbEnemyComboAbsorb;
    PHOrbSession *session = AssistSession(engine, AssistSettings(NO));
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->enemyComboReads > 1 && engine->releases == 1 &&
        [session.snapshot[@"goalPlan"][@"enemyComboRequired"] isEqual:@2] &&
        [session.snapshot[@"searchTrace"][@"enemyCountermeasure"][@"firstRemovalOnly"] boolValue] &&
        [session.snapshot[@"searchTrace"][@"enemyCountermeasure"][@"minimumCombo"] isEqual:@2],
        "continuous mode executes a route satisfying the actual enemy requirement");
    engine->live.enemyCombo.turns[0] = 0; engine->live.automation.enemyEffects = 0;
    Check(NextOrbTurn(session,engine) && ![session.snapshot[@"goalPlan"][@"enemyComboActive"] boolValue] &&
        [session.snapshot[@"goalPlan"][@"goals"] count] == 0, "next turn drops expired temporary Combo goals");
    [session queueStop:@"test done"]; Tick(session,engine,Frame());

    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        engine = [FakeOrbEngine new]; SetAssistBoard(engine, 0);
        engine->live.enemyCombo.turns[0] = 3;
        engine->live.enemyCombo.thresholds[0] = scenario == 0 ? 14 : 7;
        engine->live.automation.enemyEffects = PHOrbEnemyComboAbsorb;
        engine->failEnemyCombo = scenario == 1;
        if (scenario == 3) { engine->live.enemyCombo.subtractTurns = 1; engine->live.enemyCombo.subtract = 12; }
        if (scenario == 4) engine->live.enemyCombo.upperTurns[0] = 1;
        session = AssistSession(engine, AssistSettings(NO));
        Check(UntilPhase(session,engine,@"stopped") && !engine->stages && !engine->releases,
            "excessive, unknown, debit, upper or unsatisfied enemy Combo requirements never authorize a drag");
        Check(scenario == 2 || ![session.snapshot[@"searchTrace"][@"solverStarted"] boolValue],
            "invalid enemy requirements stop before the solver");
    }
    engine = [FakeOrbEngine new]; SetAssistBoard(engine);
    engine->live.enemyCombo.turns[0] = 3; engine->live.enemyCombo.thresholds[0] = 1;
    engine->live.automation.enemyEffects = PHOrbEnemyComboAbsorb;
    session = AssistSession(engine);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->releases == 1 &&
        [session.snapshot[@"goalPlan"][@"assistRecoveryActive"] boolValue] &&
        [session.snapshot[@"goalPlan"][@"enemyComboFirstWave"] boolValue] &&
        [[session.snapshot[@"executionHistory"] lastObject][@"releaseEvaluation"][@"comboFirstWaveRequired"] boolValue],
        "L candidate parsing and final release evaluation retain the first-removal requirement");
    [session queueStop:@"test done"]; Tick(session,engine,Frame());
    engine = [FakeOrbEngine new]; SetAssistBoard(engine,0);
    session = AssistSession(engine,AssistSettings(NO));
    Check(UntilPhase(session,engine,@"arming"), "debit race fixture reaches the first input boundary");
    engine->live.enemyCombo.subtractTurns = 1; engine->live.enemyCombo.subtract = 2;
    Check(UntilPhase(session,engine,@"stopped") && !engine->stages,
        "debit changes revoke old input even when no lower-absorption flag changed");
    for (bool held : {false,true}) {
        engine = [FakeOrbEngine new]; SetAssistBoard(engine, 0);
        session = AssistSession(engine, AssistSettings(NO));
        Check(UntilPhase(session,engine,held ? @"running" : @"arming"), "enemy state change fixture reaches execution boundary");
        if (held) for (unsigned i = 0; i < 4 && !engine->live.dragged; ++i) Tick(session,engine,Frame());
        const NSUInteger before = engine->stages;
        engine->live.enemyCombo.turns[0] = 3; engine->live.enemyCombo.thresholds[0] = 7;
        Check(UntilPhase(session,engine,@"stopped") && engine->stages == before + (held ? 1 : 0) &&
            engine->releases == (held ? 1u : 0u), "new absorption cancels stale plans and only permits an owned cleanup release");
    }
    engine = [FakeOrbEngine new]; SetAssistBoard(engine, 0);
    engine->live.enemyCombo.turns[0] = 3; engine->live.enemyCombo.thresholds[0] = 14;
    engine->live.automation.enemyEffects = PHOrbEnemyComboAbsorb; engine->bypassComboWithSkill = YES;
    NSMutableDictionary *bypassRule = [PHOrbDefaultSkillRule() mutableCopy]; bypassRule[@"enemy"] = @"combo_absorb";
    session = AssistSession(engine, AssistSettings(NO, @[], @[bypassRule]));
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 &&
        ![session.snapshot[@"goalPlan"][@"enemyComboActive"] boolValue] && engine->releases == 1,
        "configured bypass skill executes before fresh countermeasure capture");
    [session queueStop:@"test done"]; Tick(session,engine,Frame());
    engine = [FakeOrbEngine new]; engine->failEnemyCombo = YES;
    session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame()); [session queueRead];
    for (unsigned i = 0; i < 2000 && ![session.snapshot[@"phase"] isEqual:@"preview"]; ++i) Tick(session,engine,Frame());
    Check([session.snapshot[@"phase"] isEqual:@"preview"] && !engine->enemyComboReads,
        "manual preview does not acquire continuous enemy policy evidence");
    [session queueStop:@"test done"]; Tick(session,engine,Frame());
}
