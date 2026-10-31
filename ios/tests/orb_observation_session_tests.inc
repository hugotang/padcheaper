static void SeedObservation(FakeOrbEngine *engine) {
    auto &out = engine->live.observation;
    out = {}; out.status = PHOrbObservationReady; out.hpKnown = true; out.hp = 100; out.maximumHP = 1000;
    out.rulesKnown = out.cooldownAdjustmentKnown = out.assistVoidKnown = out.enemiesKnown = true;
    out.assistVoidTurns = 3; out.enemyCount = out.activeEnemies = 1;
    for (unsigned slot = 0; slot < 6; ++slot) {
        out.members[slot].active = true; out.members[slot].number = engine->live.automation.members[slot].number;
        out.members[slot].charge = engine->live.automation.members[slot].charge;
        out.members[slot].lEligibilityKnown = true; out.members[slot].lBlockers = PHOrbLNoLatent;
    }
    auto &member = out.members[2];
    member.active = true; member.number = 14060; member.sourceAssist = member.activeAssist = 13893;
    member.charge = 20; member.bodyCooldown = 5; member.totalCooldown = 21; member.chargeToAssist = 1;
    member.thresholdsKnown = member.assistRetained = true; member.chargeWindow = PHOrbChargeBody;
    member.lEligibilityKnown = member.latentKnown = member.lAwakeningsKnown = member.lAttributesKnown = true;
    member.assistRecoveryLatents = 1; member.lAwakenings = 2; member.lColors = 1u << 3; member.lBlockers = 0;
    engine->live.automation.hp = out.hp; engine->live.automation.maximumHP = out.maximumHP;
    engine->live.automation.members[2].number = member.number;
    engine->live.automation.members[2].charge = member.charge;
    engine->live.automation.members[2].assistCatalogID = member.activeAssist;
    engine->live.automation.members[2].sourceCatalogIDs[1] = member.sourceAssist;
}
static void ObservationSessionTests() {
    FakeOrbEngine *engine = [FakeOrbEngine new]; SeedObservation(engine);
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
    Check(!engine->reads && !engine->observationReads, "unused orb monitoring does not start diagnostic polling");
    Check(Preview(session, engine), "diagnostic presentation fixture can preview normally");
    NSDictionary *snapshot = [session snapshot], *trace = snapshot[@"battleObservation"];
    Check([trace[@"current"] boolValue] && [trace[@"diagnosticOnly"] boolValue] && ![trace[@"survivalKnown"] boolValue] &&
        ![trace[@"enemyForecastKnown"] boolValue] && ![trace[@"shieldReductionKnown"] boolValue] &&
        [trace[@"members"][2][@"chargeToAssist"] unsignedIntValue] == 1 &&
        [snapshot[@"battleObservationDescription"] containsString:@"位置 3：光 L"] &&
        [snapshot[@"battleObservationDescription"] containsString:@"尚未啟用自動 L＋心四連耐久"] &&
        !engine->skillAttempts && !engine->stages,
        "current diagnostic facts explicitly distinguish charge/L eligibility from survival, use authorization and future turns");
    const NSUInteger reads = engine->observationReads;
    for (unsigned i = 0; i < 30; ++i) Tick(session, engine, Frame());
    Check(engine->observationReads <= reads + 1, "stable diagnostics do not poll at the native frame rate");
    engine->failRead = YES;
    for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
    trace = [session snapshot][@"battleObservation"];
    Check(![trace[@"current"] boolValue] && !trace[@"members"] && !trace[@"hp"] &&
        ![[session snapshot][@"battleObservationDescription"] containsString:@"位置 3：光 L"],
        "read failure removes old numeric and L-provider evidence from UI and exported trace");
    engine->failRead = NO; engine->failObservation = YES;
    Check(Preview(session, engine) && [[session snapshot][@"canExecute"] boolValue] &&
        ![[session snapshot][@"battleObservation"][@"current"] boolValue],
        "optional observation failure cannot disable a valid ordinary preview");
    engine->failObservation = NO;
    ++engine->live.wave; engine->live.observation.assistVoidTurns = 1;
    Tick(session, engine, Frame(), 0.11);
    trace = [session snapshot][@"battleObservation"];
    Check(![trace[@"current"] boolValue] || [trace[@"wave"] unsignedIntValue] == 1,
        "wave changes hide old evidence even when the next diagnostic read is throttled");
    for (unsigned i = 0; i < 50 && ![[session snapshot][@"battleObservation"][@"current"] boolValue]; ++i)
        Tick(session, engine, Frame());
    trace = [session snapshot][@"battleObservation"];
    Check([trace[@"current"] boolValue] && [trace[@"wave"] unsignedIntValue] == 1 &&
        [trace[@"assistVoidTurns"] intValue] == 1, "new floor observations do not reuse the previous wave's facts");
    engine->live.phase = 22;
    for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
    Check(![[session snapshot][@"battleObservation"][@"current"] boolValue] &&
        ![session snapshot][@"battleObservation"][@"members"], "skill animation removes previously stable charge windows");
    engine->live.phase = 2; engine->live.dragged = engine->live.board.tokens[0];
    const NSUInteger beforeDrag = engine->observationReads;
    for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
    Check(engine->observationReads == beforeDrag && ![[session snapshot][@"battleObservation"][@"current"] boolValue],
        "diagnostics never sample during a manually held orb");
    engine->live.dragged = 0;
    for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
    PHBattleFrame background = Frame(); background.ready = false;
    Tick(session, engine, background);
    Check(![[session snapshot][@"battleObservation"][@"current"] boolValue], "background instantly invalidates public diagnostic evidence");
    [session queueStop:@"end observation presentation fixture"];

    engine = [FakeOrbEngine new]; SeedObservation(engine);
    session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session, engine), "changing diagnostic context fixture can preview");
    const NSUInteger beforeFresh = engine->observationReads;
    for (unsigned i = 0; i < 10 && engine->observationReads == beforeFresh; ++i) Tick(session, engine, Frame(), 0.11);
    const NSUInteger beforeChanges = engine->observationReads;
    Check(beforeChanges == beforeFresh + 1 && [[session snapshot][@"battleObservation"][@"current"] boolValue],
        "fixture starts immediately after a fresh diagnostic collection");
    for (unsigned i = 0; i < 6; ++i) {
        engine->live.board.types[0] = uint8_t((engine->live.board.types[0] + 1) % 6);
        ++engine->live.automation.members[2].charge;
        engine->live.observation.members[2].charge = engine->live.automation.members[2].charge;
        Tick(session, engine, Frame(), 0.11);
        trace = [session snapshot][@"battleObservation"];
        Check(engine->observationReads == beforeChanges && ![trace[@"current"] boolValue] && !trace[@"members"],
            "board and charge changes invalidate evidence without bypassing the diagnostic interval");
    }
    Tick(session, engine, Frame(), 0.11);
    trace = [session snapshot][@"battleObservation"];
    Check(engine->observationReads == beforeChanges + 1 && [trace[@"current"] boolValue] &&
        [trace[@"members"][2][@"charge"] unsignedIntValue] == engine->live.automation.members[2].charge,
        "the next scheduled collection publishes the new charge instead of reviving stale evidence");
    engine->live.observation.members[2].lEligibilityKnown = false;
    engine->live.observation.members[2].lBlockers = PHOrbLUnknownAwakenings;
    engine->live.observation.assistVoidKnown = false;
    for (unsigned i = 0; i < 8; ++i) Tick(session, engine, Frame(), 0.11);
    trace = [session snapshot][@"battleObservation"];
    Check([trace[@"current"] boolValue] && trace[@"members"][2][@"lEligible"] == NSNull.null &&
        trace[@"members"][2][@"lEligibleColors"] == NSNull.null && trace[@"assistVoidFresh"] == NSNull.null,
        "unknown diagnostic flags remain null rather than appearing as confirmed false");
    const NSUInteger beforeFailures = engine->observationReads;
    for (unsigned i = 0; i < 10; ++i) {
        engine->failRead = (i % 2) == 0;
        Tick(session, engine, Frame(), 0.11);
        if (engine->failRead) Check(![[session snapshot][@"battleObservation"][@"current"] boolValue] &&
            ![session snapshot][@"battleObservation"][@"members"], "intermittent failures immediately remove evidence");
    }
    Check(engine->observationReads <= beforeFailures + 2,
        "intermittent read recovery cannot reset the diagnostic rate limit");
    engine->live.automation.valid = false;
    const NSUInteger beforeOrdinary = engine->observationReads;
    for (unsigned i = 0; i < 10 && engine->observationReads == beforeOrdinary; ++i) Tick(session, engine, Frame(), 0.11);
    Check(engine->observationReads == beforeOrdinary + 1 && [[session snapshot][@"battleObservation"][@"current"] boolValue],
        "ordinary board reads can publish optional observations without changing automation validity");
    const uint8_t previousColor = engine->live.board.types[0];
    engine->live.board.types[0] = uint8_t((previousColor + 1) % 6);
    Tick(session, engine, Frame(), 0.11);
    trace = [session snapshot][@"battleObservation"];
    Check(engine->observationReads == beforeOrdinary + 1 && ![trace[@"current"] boolValue] && !trace[@"members"],
        "board changes invalidate diagnostics even without the full automation read");
    engine->live.board.types[0] = previousColor;
    Tick(session, engine, Frame(), 0.11);
    Check(engine->observationReads == beforeOrdinary + 1 && ![[session snapshot][@"battleObservation"][@"current"] boolValue],
        "returning to a previous board cannot revive discarded observations before a fresh collection");
    [session queueStop:@"end changing diagnostic context fixture"];

    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        engine = [FakeOrbEngine new]; SeedObservation(engine);
        engine->live.multiplayer.kind = kind; engine->live.identity.multiplayer = kind == PHOrbMultiplayerThreeSolo ? 2 : 1;
        session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueRead];
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        Check(!engine->observationReads && ![[session snapshot][@"battleObservation"][@"current"] boolValue] &&
            ![[session snapshot][@"battleObservationDescription"] length] && !engine->skillAttempts && !engine->stages,
            "3/4/8-player and co-op solo continuation never enable solo diagnostic sampling or automatic action");
        [session queueStop:@"end multiplayer observation fixture"];
    }
    engine = [FakeOrbEngine new]; SeedObservation(engine); engine->failObservation = YES;
    session = [[PHOrbSession alloc] initWithEngine:engine];
    Tick(session, engine, Frame()); [session queueAutomatic];
    for (unsigned i = 0; i < 2000 && ![[session snapshot][@"autoMoves"] unsignedIntegerValue] &&
        [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session, engine, Frame());
    Check([[session snapshot][@"autoMoves"] unsignedIntegerValue] == 1 && engine->observationReads &&
        !engine->skillAttempts, "unknown optional diagnostics leave the existing single-player automatic turn intact");
    [session queueStop:@"end optional observation failure fixture"];
}
