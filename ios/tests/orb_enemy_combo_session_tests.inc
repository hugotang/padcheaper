static void EnemyComboSessionTests() {
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
    session = AssistSession(engine, AssistSettings(NO, @[], @[@{@"slot":@1,@"every":@1,@"enemy":@"combo_absorb"}]));
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
