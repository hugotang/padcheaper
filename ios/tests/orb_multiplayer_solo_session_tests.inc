static void SetSessionContinuation(FakeOrbEngine *engine, unsigned seat = 1) {
    SetMultiplayer(engine, 3, seat);
    engine->live.identity.multiplayer = 2;
    auto &m = engine->live.multiplayer; m.kind = PHOrbMultiplayerThreeSolo;
    m.turnSerial = (seat + 1) % 3; m.networkFlags = m.connectionState = 0;
    m.continuationOwnerKnown = true; m.clockKnown = false; m.remainingSeconds = 0;
}
static void MultiplayerSoloSessionTests() {
    for (unsigned seat = 0; seat < 3; ++seat) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetSessionContinuation(engine, seat);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame());
        Check([session.snapshot[@"canOnce"] boolValue] && ![session.snapshot[@"canStart"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] containsString:@"單人續玩"] &&
            [session.snapshot[@"onceReadiness"][@"multiplayer"][@"timeSource"] isEqual:@"move-timer"],
            "solo continuation shortcut uses native move time and keeps continuous input disabled");
        [session queueAutomatic];
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->skillAttempts,
            "a co-op continuation cannot start single-player automation");
        [session configureQuality:@"fast"]; [session queueOnce];
        for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check([session.snapshot[@"completed"] isEqual:@1] && engine->beginFrames == 1 && engine->releases == 1 &&
            !engine->multiplayerArmed && !engine->skillAttempts,
            "continuation completes one explicitly requested gesture without a network countdown");
    }
    for (unsigned timing = 0; timing < 2; ++timing) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 3, 1);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"fast"]; [session queueOnce];
        if (!timing) Check(UntilSolveStarts(session, engine), "continuation transition captures an active worker");
        else {
            for (unsigned i = 0; i < 2000 && !engine->live.dragged && [session.snapshot[@"busy"] boolValue]; ++i)
                Tick(session, engine, Frame());
            Check(engine->live.dragged != 0, "continuation transition captures an owned token");
        }
        const NSUInteger stages = engine->stages;
        SetSessionContinuation(engine);
        for (unsigned i = 0; i < 40; ++i) Tick(session, engine, Frame());
        Check(![session.snapshot[@"busy"] boolValue] && !engine->multiplayerArmed &&
            engine->stages == stages && !engine->releases && [session.snapshot[@"completed"] isEqual:@0],
            "online-to-continuation cancels old worker/held gesture without stale input or completion");
        engine->live.dragged = 0; engine->live.moves = 0; engine->live.remainingFrames = 300;
        engine->live.ordinary = engine->live.settled = true;
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue] && engine->stages == stages,
            "continuation readiness recovers after native cleanup without reusing old intent");
        engine->live.multiplayer.recoveryFlags = 0x40;
        Tick(session, engine, Frame(), 0.12);
        Check(![session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"等待原生單人續玩"],
            "native recovery gate still explains a disabled continuation shortcut");
    }
}
