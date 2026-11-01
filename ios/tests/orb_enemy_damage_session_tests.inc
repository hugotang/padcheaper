// Presentation/cache lifecycle cases, written but not executed.
static void EnemyDamageSessionTests() {
    NSDictionary *analysis = @{@"status":@"conditional", @"snapshotHPCurrent":@1000,
        @"normalThresholdHP":@500, @"superFloorHP":@300, @"scenarioCoverageComplete":@NO,
        @"postPlayerStateKnown":@NO, @"survivalKnown":@NO,
        @"probes":@[@{@"name":@"lethal-before-survival", @"assumedEffectiveDamage":@100,
            @"checkpoint":@{@"status":@"conditional-checkpoint", @"localFieldsKnown":@YES, @"hp":@300}}]};
    NSDictionary *ready = @{@"status":@"ready", @"definitionCaptureComplete":@YES, @"structureSupported":@YES,
        @"damageAnalysis":@{@"status":@"ready", @"diagnosticOnly":@YES, @"postPlayerStateKnown":@NO},
        @"definitions":@[@{@"actionID":@17, @"opcode":@88}],
        @"enemies":@[@{@"slot":@1, @"damageAnalysis":analysis},
            @{@"slot":@2, @"damageAnalysis":@{@"status":@"unavailable"}}]};
    FakeOrbEngine *engine = [FakeOrbEngine new]; SeedObservation(engine); engine->enemyActionFixture = ready;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session, engine), "damage diagnostic enters ordinary preview");
    for (unsigned i = 0; i < 12 && !engine->enemyActionReads; ++i) Tick(session, engine, Frame(), 0.11);
    NSDictionary *snapshot = [session snapshot];
    Check([snapshot[@"battleObservationDescription"] containsString:@"根性與變階段：1 個敵人已取樣，1 個未確認"] &&
        [snapshot[@"battleObservationDescription"] containsString:@"僅列假設傷害下的局部結果"] &&
        [snapshot[@"battleObservation"][@"enemyActions"][@"enemies"][0][@"damageAnalysis"] isEqual:analysis] &&
        ![snapshot[@"battleObservation"][@"enemyForecastKnown"] boolValue] &&
        ![snapshot[@"battleObservation"][@"survivalKnown"] boolValue] && !engine->stages && !engine->skillAttempts,
        "conditional HP probes are not forecasts, actions or permission to defer recovery");
    NSMutableDictionary *changed = [ready mutableCopy]; changed[@"damageAnalysis"] = @{@"status":@"changed"};
    changed[@"enemies"] = @[@{@"slot":@1, @"damageAnalysis":@{@"status":@"changed"}}];
    engine->enemyActionFixture = changed; NSUInteger reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    snapshot = [session snapshot];
    Check(![snapshot[@"battleObservationDescription"] containsString:@"根性與變階段："] &&
        !snapshot[@"battleObservation"][@"enemyActions"][@"enemies"][0][@"damageAnalysis"][@"probes"] &&
        [snapshot[@"battleObservation"][@"enemyActions"][@"definitions"] count] == 1 &&
        [snapshot[@"canExecute"] boolValue] && !engine->stages, "failed optional refresh removes old probes without blocking preview");
    engine->enemyActionFixture = ready; reads = engine->enemyActionReads;
    for (unsigned i = 0; i < 12 && engine->enemyActionReads == reads; ++i) Tick(session, engine, Frame(), 0.11);
    engine->live.board.types[0] = uint8_t((engine->live.board.types[0] + 1) % 6);
    Tick(session, engine, Frame(), 0.11); snapshot = [session snapshot];
    Check(!snapshot[@"battleObservation"][@"enemyActions"] && ![snapshot[@"battleObservationDescription"] containsString:@"根性與變階段："],
        "the next idle board read removes conditional HP and phase facts");
    [session queueStop:@"end damage diagnostic fixture"];
    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        engine = [FakeOrbEngine new]; SeedObservation(engine); engine->enemyActionFixture = ready;
        engine->live.multiplayer.kind = kind;
        engine->live.identity.multiplayer = kind == PHOrbMultiplayerThreeSolo ? 2 : 1;
        session = [[PHOrbSession alloc] initWithEngine:engine]; [session queueRead];
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame(), 0.11);
        Check(!engine->enemyActionReads && ![session snapshot][@"battleObservation"][@"enemyActions"] &&
            !engine->stages && !engine->skillAttempts, "damage analysis never starts multiplayer or continuation automation");
        [session queueStop:@"end multiplayer damage fixture"];
    }
}
