static void EnemySelectorSessionTests() {
    NSDictionary *analysis = @{@"status":@"bounded-rows", @"conditionalWalkComplete":@YES,
        @"candidateSet":@"conditional-conservative-superset", @"nextEntryKnown":@NO,
        @"enemyForecastKnown":@NO, @"survivalKnown":@NO,
        @"candidates":@[@{@"actionID":@17, @"row":@1, @"selectionGuaranteed":@NO}]};
    NSDictionary *ready = @{@"status":@"ready", @"definitionCaptureComplete":@YES, @"structureSupported":@YES,
        @"selectorAnalysis":@{@"status":@"ready", @"diagnosticOnly":@YES},
        @"definitions":@[@{@"actionID":@17, @"opcode":@88}],
        @"enemies":@[@{@"slot":@1, @"selectorAnalysis":analysis},
                      @{@"slot":@2, @"selectorAnalysis":@{@"status":@"unknown", @"conditionalWalkComplete":@NO}}]};
    FakeOrbEngine *engine = [FakeOrbEngine new]; SeedObservation(engine); engine->enemyActionFixture = ready;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session, engine), "selector diagnostic fixture enters the ordinary preview");
    for (unsigned i = 0; i < 12 && !engine->enemyActionReads; ++i) Tick(session, engine, Frame(), 0.11);
    NSDictionary *snapshot = [session snapshot];
    Check([snapshot[@"battleObservationDescription"] containsString:@"1 個敵人的條件分支已分析，1 個未完整；保留 1 組候選"] &&
        [snapshot[@"battleObservationDescription"] containsString:@"尚不能確認下一招或判斷耐久"] &&
        [snapshot[@"battleObservation"][@"enemyActions"][@"enemies"][0][@"selectorAnalysis"] isEqual:analysis] &&
        ![snapshot[@"battleObservation"][@"enemyForecastKnown"] boolValue] &&
        ![snapshot[@"battleObservation"][@"survivalKnown"] boolValue] && !engine->stages && !engine->skillAttempts,
        "conditional complete and unknown enemy branches are presented without becoming a survival decision");

    NSMutableDictionary *unavailable = [ready mutableCopy];
    unavailable[@"selectorAnalysis"] = @{@"status":@"unavailable"};
    unavailable[@"enemies"] = @[@{@"slot":@1, @"selectorAnalysis":@{@"status":@"unavailable"}}];
    engine->enemyActionFixture = unavailable; NSUInteger reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    snapshot = [session snapshot];
    Check(![snapshot[@"battleObservationDescription"] containsString:@"選招分支："] &&
        [snapshot[@"battleObservation"][@"enemyActions"][@"definitions"] count] == 1 &&
        !snapshot[@"battleObservation"][@"enemyActions"][@"enemies"][0][@"selectorAnalysis"][@"candidates"] &&
        [snapshot[@"canExecute"] boolValue] && !engine->stages,
        "selector guard loss clears prior candidates and their description without blocking ordinary preview execution");
    engine->enemyActionFixture = ready; reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    engine->live.board.types[0] = uint8_t((engine->live.board.types[0] + 1) % 6);
    Tick(session, engine, Frame(), 0.01); snapshot = [session snapshot];
    Check(!snapshot[@"battleObservation"][@"enemyActions"] &&
        ![snapshot[@"battleObservationDescription"] containsString:@"選招分支："],
        "stale board context immediately removes selector candidates along with raw action data");
    [session queueStop:@"end selector presentation fixture"];
    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        engine = [FakeOrbEngine new]; SeedObservation(engine); engine->enemyActionFixture = ready;
        engine->live.multiplayer.kind = kind;
        engine->live.identity.multiplayer = kind == PHOrbMultiplayerThreeSolo ? 2 : 1;
        session = [[PHOrbSession alloc] initWithEngine:engine]; [session queueRead];
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame(), 0.11);
        Check(!engine->enemyActionReads && ![session snapshot][@"battleObservation"][@"enemyActions"] &&
            !engine->stages && !engine->skillAttempts, "conditional enemy analysis remains excluded from all multiplayer sessions");
        [session queueStop:@"end multiplayer selector fixture"];
    }
}
