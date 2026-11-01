static void EnemyActionSessionTests() {
    FakeOrbEngine *engine = [FakeOrbEngine new]; SeedObservation(engine);
    engine->enemyActionFixture = @{@"status":@"ready", @"diagnosticOnly":@YES, @"definitionCaptureComplete":@YES,
        @"structureSupported":@NO, @"enemyForecastKnown":@NO, @"damageBoundKnown":@NO, @"survivalKnown":@NO,
        @"definitions":@[@{@"actionID":@17, @"opcode":@88, @"kind":@"awoken-bind", @"parameters":@[@4]}],
        @"enemies":@[@{@"slot":@1, @"catalogID":@400, @"baseAttack":@20000, @"countdownRaw":@1}],
        @"issues":@[@"unknown-opcode"]};
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session, engine), "enemy definition diagnostics can accompany an ordinary preview");
    for (unsigned i = 0; i < 12 && !engine->enemyActionReads; ++i) Tick(session, engine, Frame(), 0.11);
    NSDictionary *snapshot = [session snapshot], *trace = snapshot[@"battleObservation"];
    Check([trace[@"current"] boolValue] && [trace[@"enemyActions"][@"definitions"] count] == 1 &&
        ![trace[@"enemyForecastKnown"] boolValue] && ![trace[@"survivalKnown"] boolValue] &&
        [snapshot[@"battleObservationDescription"] containsString:@"下一回合可達招式尚未判定"] &&
        [snapshot[@"battleObservationDescription"] containsString:@"未支援或不完整資料"] && !engine->stages && !engine->skillAttempts,
        "current enemy definitions are visible but never promote unknown damage or authorize a gesture");
    NSUInteger reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 6; ++i) Tick(session, engine, Frame(), 0.01);
    Check(engine->enemyActionReads <= reads + 1, "enemy action capture shares the existing diagnostic rate limit");
    reads = engine->enemyActionReads;
    NSDictionary *ready = engine->enemyActionFixture;
    engine->enemyActionFixture = @{@"status":@"changed", @"enemyForecastKnown":@NO};
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    trace = [session snapshot][@"battleObservation"];
    Check([trace[@"current"] boolValue] && [trace[@"enemyActions"][@"status"] isEqual:@"changed"] &&
        !trace[@"enemyActions"][@"definitions"] && [trace[@"members"] count] == 6 &&
        [[session snapshot][@"canExecute"] boolValue],
        "enemy-only read failure drops its stale table while keeping current earlier diagnostics and preview authorization");
    engine->enemyActionFixture = ready; reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    engine->live.board.types[0] = uint8_t((engine->live.board.types[0] + 1) % 6);
    Tick(session, engine, Frame(), 0.01);
    trace = [session snapshot][@"battleObservation"];
    Check(![trace[@"current"] boolValue] && !trace[@"enemyActions"],
        "board changes remove enemy definitions immediately even before a scheduled capture");
    reads = engine->enemyActionReads;
    engine->failObservation = YES;
    for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame(), 0.11);
    Check(engine->enemyActionReads == reads && ![session snapshot][@"battleObservation"][@"enemyActions"],
        "missing stable observation context never retains or resamples an old enemy definition table");
    [session queueStop:@"end enemy action presentation fixture"];

    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        engine = [FakeOrbEngine new]; SeedObservation(engine); engine->enemyActionFixture = ready;
        engine->live.multiplayer.kind = kind;
        engine->live.identity.multiplayer = kind == PHOrbMultiplayerThreeSolo ? 2 : 1;
        session = [[PHOrbSession alloc] initWithEngine:engine]; [session queueRead];
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame(), 0.11);
        Check(!engine->enemyActionReads && ![session snapshot][@"battleObservation"][@"enemyActions"] &&
            !engine->stages && !engine->skillAttempts, "no enemy action polling or automatic work is introduced in any multiplayer mode");
        [session queueStop:@"end multiplayer enemy action fixture"];
    }
}
