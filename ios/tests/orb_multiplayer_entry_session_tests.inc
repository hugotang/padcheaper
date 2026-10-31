// Native latch recovery is exercised separately with the production reader.
// These cases verify session intent and shortcut state across its results.
static void MultiplayerEntrySessionTests() {
    for (unsigned from : {3u, 4u, 8u}) for (unsigned to : {3u, 4u, 8u}) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, from, from - 1);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueRead]; [session queueStop:@"entry observation"];
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue], "previous multiplayer match has a usable local shortcut");
        engine->failRead = YES; Tick(session, engine, Frame(), 0.12);
        Check(![session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"多人資料未確認"],
            "a missed lobby read does not publish stale readiness");
        engine->failRead = NO; SetMultiplayer(engine, to, to - 1); ++engine->live.identity.entryHash[0];
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue] && ![session.snapshot[@"canStart"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"可轉一次"] &&
            !engine->stages && !engine->multiplayerArmed,
            "a fresh native match result restores 3/4/8P readiness without starting a gesture");
        [session queueOnce];
        for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check([session.snapshot[@"completed"] isEqual:@1] && engine->beginFrames == 1 && engine->releases == 1 &&
            !engine->multiplayerArmed && !engine->skillAttempts,
            "a new explicit tap completes exactly one shot in the replacement match");
    }
    for (unsigned timing = 0; timing < 2; ++timing) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 2);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"fast"]; [session queueOnce];
        if (timing == 0) Check(UntilSolveStarts(session, engine), "entry replacement observes an active worker");
        else {
            for (unsigned i = 0; i < 2000 && !engine->live.dragged && [session.snapshot[@"busy"] boolValue]; ++i)
                Tick(session, engine, Frame());
            Check(engine->live.dragged != 0, "entry replacement observes an owned gesture");
        }
        const NSUInteger stages = engine->stages;
        SetMultiplayer(engine, 3, 1); ++engine->live.identity.entryHash[0];
        for (unsigned i = 0; i < 40; ++i) Tick(session, engine, Frame());
        Check(![session.snapshot[@"busy"] boolValue] && ![session.snapshot[@"once"] boolValue] &&
            !engine->multiplayerArmed && !engine->releases && engine->stages == stages &&
            [session.snapshot[@"completed"] isEqual:@0],
            "changing match cancels the old worker or held gesture without writing its release into the new match");
        engine->live.dragged = 0; engine->live.moves = 0; engine->live.remainingFrames = 300;
        engine->live.ordinary = engine->live.settled = true;
        Tick(session, engine, Frame(), 0.12);
        Check([session.snapshot[@"canOnce"] boolValue] && engine->stages == stages,
            "the new local board recovers readiness after cancellation without reusing old intent");
    }
}
