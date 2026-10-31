// These session fixtures exercise the actual model/worker/input lifecycle,
// not peer networking. Written cases remain pending an authorized test run.
static void SetSessionPeerExit(FakeOrbEngine *engine, uint32_t sync = 0) {
    auto &m = engine->live.multiplayer;
    m.networkFlags |= 0x10000u; m.syncState = sync; m.syncStep = 0;
    m.disconnectKnown = true; m.disconnectSeat = 0; m.disconnectStatus = 5;
}
static void MultiplayerPeerSessionTests() {
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 2);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueRead]; [session queueStop:@"peer observation"];
        engine->live.multiplayer.syncState = 5; engine->live.multiplayer.syncStep = 0;
        Tick(session, engine, Frame(), 0.12);
        Check(![session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"等待多人同步"],
            "recovery entry that clears native selection still disables the shortcut");
        engine->live.multiplayer.syncStep = 2;
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"可轉一次"] &&
            [session.snapshot[@"onceReadiness"][@"multiplayer"][@"syncStep"] isEqual:@2] &&
            !engine->multiplayerArmed && !engine->stages,
            "initialized 4P peer recovery restores readiness without submitting input");
        SetSessionPeerExit(engine, 15);
        Tick(session, engine, Frame(), 0.12);
        NSDictionary *trace = session.snapshot[@"onceReadiness"][@"multiplayer"];
        Check([session.snapshot[@"canOnce"] boolValue] && [trace[@"localReady"] boolValue] &&
            [trace[@"disconnectKnown"] boolValue] && [trace[@"disconnectSeat"] isEqual:@0] &&
            [trace[@"disconnectStatus"] isEqual:@5],
            "the shortcut and diagnostic explain the same validated remote departure");
        engine->live.multiplayer.syncState = 0;
        Tick(session, engine, Frame(), 0.12);
        const NSUInteger firstCount = [session.snapshot[@"onceReadinessHistory"] count];
        engine->live.multiplayer.disconnectSeat = 1;
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"onceReadinessHistory"] count] == firstCount + 1 && !engine->stages,
            "a second departed peer is retained even when the sync state and flags do not change");
        engine->live.multiplayer.connectionState = 0;
        Tick(session, engine, Frame(), 0.12);
        Check(![session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"等待多人同步"],
            "a peer departure does not hide actual local connection loss");
        engine->live.multiplayer.connectionState = 2;
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue] && !engine->stages && !engine->multiplayerArmed,
            "restoring a valid local window never resumes a gesture automatically");
        Check([session configureSettings:@{@"quality":@"fast", @"mode":@"leaders", @"goals":@[], @"rules":@[]}],
            "peer-exit fixture can select leader planning");
        Check(![session.snapshot[@"canOnce"] boolValue] &&
            ![session.snapshot[@"liveLeaderPlan"][@"available"] boolValue] && !engine->stages,
            "remote departure support cannot bypass missing local leader data");
        NSArray *history = session.snapshot[@"onceReadinessHistory"];
        engine->live = {}; Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"onceReadinessHistory"] isEqual:history],
            "peer-exit observations remain available when diagnostics are copied after leaving");
    }
    for (unsigned timing = 0; timing < 3; ++timing) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 2);
        engine->multiplayerReleaseMetadata = YES;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"fast"];
        if (timing == 0) SetSessionPeerExit(engine);
        [session queueOnce];
        if (timing == 1) {
            Check(UntilSolveStarts(session, engine), "peer departure while solving captures the actual worker phase");
            SetSessionPeerExit(engine, 15);
        }
        if (timing == 2) {
            for (unsigned i = 0; i < 2000 && !engine->live.dragged && [session.snapshot[@"busy"] boolValue]; ++i)
                Tick(session, engine, Frame());
            Check(engine->live.dragged && engine->beginFrames == 1, "peer departure during dragging has an owned token");
            SetSessionPeerExit(engine, 15);
        }
        Tick(session, engine, Frame()); engine->live.multiplayer.syncState = 0;
        for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check([session.snapshot[@"completed"] isEqual:@1] && engine->beginFrames == 1 && engine->releases == 1 &&
            !engine->multiplayerArmed && !engine->skillAttempts &&
            [session.snapshot[@"executionHistory"][0][@"observedMultiplayer"][@"disconnectSeat"] isEqual:@0],
            "an unaffected local board completes its explicit shot when a peer leaves before solving, during solving or dragging");
        engine->live.multiplayer.resolvePending = 0; engine->live.settled = engine->live.ordinary = true;
        engine->live.remainingFrames = 300; engine->live.moves = 0;
        for (unsigned cell = 0; cell < 30; ++cell)
            engine->live.board.types[cell] = uint8_t((engine->live.board.types[cell] + 1) % 6);
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue] && ![session.snapshot[@"canStart"] boolValue] &&
            !engine->multiplayerArmed && engine->beginFrames == 1,
            "the next 4P board is available with retained peer/submission flags and no automatic begin");
        [session queueOnce];
        for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check([session.snapshot[@"completed"] isEqual:@2] && engine->beginFrames == 2 && engine->releases == 2 &&
            engine->multiplayerArms == 2 && !engine->multiplayerArmed && !engine->skillAttempts,
            "another explicit tap is required for the second shot after peer departure");
    }
    for (unsigned interruption = 0; interruption < 4; ++interruption) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 2);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        StartMultiplayerOnce(session, engine);
        const NSUInteger stages = engine->stages;
        SetSessionPeerExit(engine);
        if (interruption == 0) {
            engine->live.multiplayer.syncState = 5; engine->live.multiplayer.syncStep = 2;
            engine->live.dragged = 0; // Actual native recovery entry, not a preserved peer-exit token.
        }
        if (interruption == 1) engine->live.multiplayer.connectionState = 0;
        if (interruption == 2) {
            engine->live.multiplayer.disconnectSeat = 2; engine->live.multiplayer.localStatus = 5;
        }
        if (interruption == 3) engine->live.multiplayer.finished = 1;
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages && !engine->releases && !engine->multiplayerArmed &&
            ![session.snapshot[@"busy"] boolValue] && [session.snapshot[@"completed"] isEqual:@0],
            "lost selection, local connection, local departure or match end cancels without unowned cleanup");
    }
    for (unsigned interruption = 0; interruption < 3; ++interruption) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 2);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"fast"]; [session queueOnce];
        Check(UntilSolveStarts(session, engine), "4P recovery cancellation observes the worker before any began");
        if (interruption == 0) { engine->live.multiplayer.syncState = 5; engine->live.multiplayer.syncStep = 0; }
        if (interruption == 1) engine->live.multiplayer.connectionState = 0;
        if (interruption == 2) engine->live.multiplayer.finished = 1;
        for (unsigned i = 0; i < 35; ++i) Tick(session, engine, Frame());
        Check(!engine->stages && !engine->multiplayerArmed && ![session.snapshot[@"busy"] boolValue],
            "recovery initialization, local connection loss or match end cancels a 4P worker before input");
        engine->live.multiplayer.syncState = 0; engine->live.multiplayer.connectionState = 2;
        engine->live.multiplayer.finished = 0;
        for (unsigned i = 0; i < 25; ++i) Tick(session, engine, Frame());
        Check(!engine->stages && !engine->multiplayerArmed && ![session.snapshot[@"busy"] boolValue],
            "late worker completion and restored local readiness cannot revive a cancelled shot");
    }
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 2);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        StartMultiplayerOnce(session, engine); SetSessionPeerExit(engine, 15);
        [session queueStop:@"stop after peer exit"];
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame());
        Check(engine->releases == 1 && engine->beginFrames == 1 && !engine->live.dragged &&
            !engine->multiplayerArmed && [session.snapshot[@"completed"] isEqual:@0],
            "explicit stop can still release an unaffected local token during peer departure completion");
    }
}
