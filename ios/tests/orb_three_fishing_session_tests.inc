// Source cases only until the next authorized build/test. Synthetic peers and
// clocks check local admission/withdrawal, not multiplayer synchronization.
static void FishingIntent(PHOrbSession *session, FakeOrbEngine *engine, bool automatic = false) {
    Tick(session,engine,Frame());
    if (automatic) [session queueAutomatic]; else [session queueOnce];
    for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue] &&
        ![session.snapshot[@"phase"] isEqual:@"arming"] && !engine->live.dragged; ++i)
        Tick(session,engine,Frame(),1.0 / 60);
}
static void ThreeFishingSessionTests() {
    // The same public multi-take request works for solo once, solo continuous,
    // and one expressly armed local 3P turn; it never turns 3P into Auto.
    for (unsigned mode : {0u,1u,3u}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(true);
        if (mode == 3) SetMultiplayer(engine,3,1);
        PHOrbSession *session = MultiFishingSession(engine);
        FishingIntent(session,engine,mode == 1);
        NSDictionary *trace = session.snapshot[@"searchTrace"];
        Check([trace[@"fishingEligibilityReason"] isEqual:@"eligible"] &&
            [trace[@"multiFishingSelected"] boolValue] && [trace[@"fishingExchanges"] count] == 2,
            "solo once/continuous and armed 3P once plan the same two heart takes");
        if (mode == 3) Check(![session.snapshot[@"automatic"] boolValue] &&
            [trace[@"soloRetrySkipped"] isEqual:@"not-single-player"] &&
            [trace[@"multiplayerSearchBudgetShared"] boolValue] && [trace[@"fishingReservedBudgetMS"] unsignedIntValue] > 0 &&
            [trace[@"ordinaryStageBudgetMS"] unsignedIntValue] < [trace[@"initialBudgetMS"] unsignedIntValue],
            "3P fishing keeps manual once and one shared search budget without a solo retry");
        for (unsigned i = 0; i < 1000 && !engine->releases && [session.snapshot[@"busy"] boolValue]; ++i)
            Tick(session,engine,Frame(),1.0 / 60);
        Check(engine->releases == 1 && !engine->live.dragged &&
            [session.snapshot[@"lastFishingSuccess"][@"releaseConfirmed"] boolValue],
            "each allowed intent confirms both takes and one owned release");
        const NSUInteger stages = engine->stages;
        if (mode != 1) {
            for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
            Check(engine->stages == stages && !engine->multiplayerArmed,
                "one-shot fishing cannot re-arm itself after success");
        }
        [session queueStop:@"fishing intent fixture complete"]; Tick(session,engine,Frame());
    }
    // MultiFishingTiming must reserve against the shorter co-op clock even
    // when move time would otherwise allow several full spinner cycles.
    for (bool shortMove : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        if (shortMove) engine->live.remainingFrames = 48;
        else engine->live.multiplayer.remainingSeconds = 0.8;
        PHOrbSession *session = MultiFishingSession(engine);
        FishingIntent(session,engine);
        Check(!engine->stages && !engine->releases,
            "either short native timer prevents beginning a multi-take gesture");
        [session queueStop:@"short clock fixture complete"]; Tick(session,engine,Frame());
    }
    for (unsigned players : {4u,8u}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,players);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        Check([session.snapshot[@"searchTrace"][@"fishingEligibilityReason"] isEqual:@"mode-not-supported"] &&
            ![session.snapshot[@"searchTrace"][@"multiFishingSelected"] boolValue] && !engine->stages,
            "4P/8P manual once retains an explicit no-fishing reason");
        [session queueStop:@"excluded mode fixture complete"]; Tick(session,engine,Frame());
    }
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetSessionContinuation(engine,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        Check([session.snapshot[@"searchTrace"][@"fishingEligibilityReason"] isEqual:@"mode-not-supported"] && !engine->stages,
            "selector-2 continuation has no valid co-op clock and remains outside fishing");
        [session queueStop:@"continuation fixture complete"]; Tick(session,engine,Frame());
    }
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        PHOrbSession *session = MultiFishingSession(engine);
        Preview(session,engine);
        Check([session.snapshot[@"searchTrace"][@"fishingEligibilityReason"] isEqual:@"three-player-manual-once-required"] &&
            !engine->stages && !engine->multiplayerArmed,
            "3P read/preview alone does not authorize fishing");
        [session queueAutomatic]; Tick(session,engine,Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages,
            "3P Auto remains excluded after the fishing extension");
        [session queueStop:@"unarmed fixture complete"]; Tick(session,engine,Frame());
    }
    // Clock reduction while held immediately withdraws further takes but
    // allows releasing exactly our held token while the local window remains.
    for (bool shortMove : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        if (!MultiFishingSessionWait(session,engine)) continue;
        const auto moves = engine->live.moves; const uintptr_t held = engine->live.dragged;
        if (shortMove) engine->live.remainingFrames = 18;
        else engine->live.multiplayer.remainingSeconds = 0.3;
        Tick(session,engine,Frame(),1.0 / 60);
        Check(held && engine->live.moves == moves && engine->releases == 1 && !engine->live.dragged &&
            !engine->multiplayerArmed && !engine->allowedSpinnerCell,
            "shortening either clock during a wait cancels the take and releases only the owned token");
    }
    for (unsigned change = 0; change < 5; ++change) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        if (!MultiFishingSessionWait(session,engine)) continue;
        const NSUInteger stages = engine->stages;
        if (change == 0) engine->live.multiplayer.turnSerial += 3;
        if (change == 1) engine->live.multiplayer.viewTeam = 2;
        if (change == 2) engine->live.identity.entryHash[0] ^= 1;
        if (change == 3) engine->live.inputManager++;
        if (change == 4) engine->live.dragged = engine->live.board.tokens[29];
        Tick(session,engine,Frame(),1.0 / 60);
        Check(engine->stages == stages && !engine->releases && !engine->multiplayerArmed,
            "turn/team/entry/input identity change or foreign held token receives no stale swap or release");
    }
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        const NSUInteger count = [session.snapshot[@"path"] count];
        for (unsigned i = 0; i < 1000 && [session.snapshot[@"busy"] boolValue] && engine->live.moves + 1 < count; ++i)
            Tick(session,engine,Frame(),1.0 / 60);
        if (count > 1 && engine->live.dragged && engine->live.moves + 1 == count) {
            for (unsigned i = 0; i < 30; ++i) if (engine->live.board.tokens[i] != engine->live.dragged) engine->live.orbMotion[i] = 0;
            engine->live.multiplayer.remainingSeconds = 0.5;
            for (unsigned i = 0; i < 10 && !engine->releases; ++i) Tick(session,engine,Frame(),1.0 / 60);
            Check([session.snapshot[@"lastFishingSuccess"][@"releaseConfirmed"] boolValue] && engine->releases == 1,
                "already stable final board uses the release margin without charging another settling wait");
        } else Check(false,"stable final boundary fixture reaches the last confirmed move");
        [session queueStop:@"stable final boundary complete"]; Tick(session,engine,Frame());
    }
    // A single take uses the same motion/timing evidence as multi-take. The
    // target color alone is insufficient while its actual source is moving.
    {
        FakeOrbEngine *engine = FishEngine(); engine->live.orbMotionKnown = false;
        PHOrbSession *session = FishSession(engine); Preview(session,engine);
        Check([session.snapshot[@"searchTrace"][@"fishingEligibilityReason"] isEqual:@"motion-unavailable"] && !engine->stages,
            "unknown source motion explains why even a single fishing take was not attempted");
        [session queueStop:@"missing motion fixture complete"]; Tick(session,engine,Frame());
    }
    for (bool changePeriod : {false,true}) {
        FakeOrbEngine *engine = FishEngine(); PHOrbSession *session = FishSession(engine);
        if (!Preview(session,engine)) continue;
        NSArray<NSNumber *> *path = session.snapshot[@"path"];
        const unsigned fish = [session.snapshot[@"searchTrace"][@"fishCell"] unsignedIntValue];
        const NSUInteger entry = [path indexOfObject:@(fish)];
        Execute(session); UntilDragging(session,engine);
        if (entry == NSNotFound || !entry) { [session queueStop:@"missing single entry"]; continue; }
        for (unsigned i = 0; i < 300 && engine->live.moves + 1 < entry; ++i) Tick(session,engine,Frame(),1.0 / 60);
        engine->live.board.types[fish] = 0; engine->live.orbMotion[fish] = 3;
        const auto moves = engine->live.moves;
        if (changePeriod) ++engine->live.spinnerPeriodFrames;
        for (unsigned i = 0; i < 8 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
        if (changePeriod) Check(engine->releases == 1 && !engine->live.dragged,
            "single-take period change stops and releases the captured token");
        else Check(engine->live.moves == moves && engine->live.board.tokens[fish] != engine->live.dragged && !engine->releases,
            "single take never enters a moving source even when the needed color is visible");
        [session queueStop:@"single fishing motion fixture complete"]; Tick(session,engine,Frame());
    }
}
