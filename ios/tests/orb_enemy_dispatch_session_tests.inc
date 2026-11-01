static void EnemyDispatchSessionTests() {
    NSDictionary *analysis = @{@"status":@"conditional", @"nextEntryKnown":@NO, @"postPlayerStateKnown":@NO,
        @"current":@{@"route":@"countdown-pending", @"conditionalRouteClassified":@YES},
        @"afterConditionalTick":@{@"countdown":@0, @"entry":@{@"route":@"ordinary-row-zero"}}};
    NSDictionary *ready = @{@"status":@"ready", @"definitionCaptureComplete":@YES, @"structureSupported":@YES,
        @"dispatchAnalysis":@{@"status":@"ready", @"diagnosticOnly":@YES},
        @"definitions":@[@{@"actionID":@17, @"opcode":@88}],
        @"enemies":@[@{@"slot":@1, @"dispatchAnalysis":analysis},
            @{@"slot":@2, @"dispatchAnalysis":@{@"status":@"unavailable"}}]};
    FakeOrbEngine *engine = [FakeOrbEngine new]; SeedObservation(engine); engine->enemyActionFixture = ready;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session, engine), "dispatch diagnostic fixture enters ordinary preview");
    for (unsigned i = 0; i < 12 && !engine->enemyActionReads; ++i) Tick(session, engine, Frame(), 0.11);
    NSDictionary *snapshot = [session snapshot];
    Check([snapshot[@"battleObservationDescription"] containsString:@"1 個敵人已按目前狀態分類，1 個未確認"] &&
        [snapshot[@"battleObservationDescription"] containsString:@"不能據此延後救援或判斷安全回合"] &&
        [snapshot[@"battleObservation"][@"enemyActions"][@"enemies"][0][@"dispatchAnalysis"] isEqual:analysis] &&
        ![snapshot[@"battleObservation"][@"enemyForecastKnown"] boolValue] &&
        ![snapshot[@"battleObservation"][@"survivalKnown"] boolValue] && !engine->stages && !engine->skillAttempts,
        "entry classification and assumed tick are visibly conditional without skill or survival authority");
    NSMutableDictionary *unavailable = [ready mutableCopy];
    unavailable[@"dispatchAnalysis"] = @{@"status":@"changed"};
    unavailable[@"enemies"] = @[@{@"slot":@1, @"dispatchAnalysis":@{@"status":@"changed"}}];
    engine->enemyActionFixture = unavailable; NSUInteger reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    snapshot = [session snapshot];
    Check(![snapshot[@"battleObservationDescription"] containsString:@"行動入口："] &&
        [snapshot[@"battleObservation"][@"enemyActions"][@"definitions"] count] == 1 &&
        !snapshot[@"battleObservation"][@"enemyActions"][@"enemies"][0][@"dispatchAnalysis"][@"current"] &&
        [snapshot[@"canExecute"] boolValue] && !engine->stages,
        "dispatch race clears previous classifications and wording without blocking preview");
    engine->enemyActionFixture = ready; reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    engine->live.board.types[0] = uint8_t((engine->live.board.types[0] + 1) % 6);
    Tick(session, engine, Frame(), 0.01); snapshot = [session snapshot];
    Check(!snapshot[@"battleObservation"][@"enemyActions"] &&
        ![snapshot[@"battleObservationDescription"] containsString:@"行動入口："],
        "board change immediately discards conditional entry and tick data");
    [session queueStop:@"end dispatch presentation fixture"];
    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        engine = [FakeOrbEngine new]; SeedObservation(engine); engine->enemyActionFixture = ready;
        engine->live.multiplayer.kind = kind;
        engine->live.identity.multiplayer = kind == PHOrbMultiplayerThreeSolo ? 2 : 1;
        session = [[PHOrbSession alloc] initWithEngine:engine]; [session queueRead];
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame(), 0.11);
        Check(!engine->enemyActionReads && ![session snapshot][@"battleObservation"][@"enemyActions"] &&
            !engine->stages && !engine->skillAttempts, "dispatch diagnostic never enables multiplayer automation");
        [session queueStop:@"end multiplayer dispatch fixture"];
    }
}
