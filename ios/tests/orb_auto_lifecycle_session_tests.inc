static void AutoLifecycleSessionTests() {
    for (bool manual : {false, true}) {
        FakeOrbEngine *engine = EightEngineFixture();
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        NSDictionary *profile = EightProfileFixture();
        auto setting = [&](BOOL enabled) {
            return [session configureProfiles:@{@"schema":@1,@"selected":profile[@"id"],@"autoSelect":@NO,
                @"eightAutoStart":@(enabled),@"profiles":@[profile]}];
        };
        Check(setting(!manual), "countdown failure fixture saves its initial setting");
        engine->rejectEightArm = YES;
        if (manual) {
            Tick(session, engine, Frame());
            [session queueAutomatic];
        } else for (unsigned i = 0; i < 50 && !engine->eightArmAttempts; ++i) Tick(session, engine, Frame());
        Check(engine->eightArmAttempts == 1 && !engine->eightArmed && ![session.snapshot[@"automatic"] boolValue] &&
            [session.snapshot[@"phase"] isEqual:@"stopped"], "failed native arming never publishes an active Auto run");
        NSDictionary *stopped = session.snapshot;
        [session stopForLifecycle:@"background after rejection"];
        Check([session.snapshot[@"message"] isEqual:stopped[@"message"]] &&
            [session.snapshot[@"cancelled"] isEqual:stopped[@"cancelled"]],
            "lifecycle callback preserves the rejected-start reason and cancellation count");
        engine->rejectEightArm = NO;
        Check(setting(NO) && setting(YES), "settings can be reloaded after rejected arming");
        for (unsigned i = 0; i < 60; ++i) Tick(session, engine, Frame());
        Check(engine->eightArmAttempts == 1 && !engine->stages && !engine->skillAttempts &&
            ![session.snapshot[@"automatic"] boolValue],
            "a consumed manual or countdown attempt stays consumed after settings toggle and arming recovery");
        ++engine->live.multiplayer.clockStart;
        for (unsigned i = 0; i < 50 && ![session.snapshot[@"automatic"] boolValue]; ++i) Tick(session, engine, Frame());
        Check(engine->eightArmAttempts == 2 && engine->eightArmed && [session.snapshot[@"automatic"] boolValue] &&
            [session.snapshot[@"eightAutomatic"] boolValue] && [session.snapshot[@"autoTurn"] isEqual:@1],
            "only the next match gets a new automatic attempt and fresh run counters");
        [session queueStop:@"fixture finished"];
    }

    // Stop while a local gesture is owned, then receive repeated background
    // callbacks. Auto intent ends immediately; only its original release runs.
    FakeOrbEngine *engine = EightEngineFixture();
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Tick(session, engine, Frame()); [session configureQuality:@"fast"]; [session queueAutomatic];
    for (unsigned i = 0; i < 3000 && !engine->live.dragged && [session.snapshot[@"automatic"] boolValue]; ++i)
        Tick(session, engine, Frame());
    Check(engine->live.dragged != 0, "Auto lifecycle fixture owns a native gesture");
    [session queueStop:@"manual Auto stop"];
    NSDictionary *stopped = session.snapshot;
    Check(![stopped[@"automatic"] boolValue] && ![stopped[@"eightAutomatic"] boolValue] &&
        [stopped[@"phase"] isEqual:@"releasing"] && !engine->eightArmed,
        "Stop revokes both run views while preserving the release phase");
    [session stopForLifecycle:@"background"]; [session stopForLifecycle:@"foreground"];
    for (unsigned i = 0; i < 30 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
    Check(engine->releases == 1 && !engine->live.dragged && [session.snapshot[@"phase"] isEqual:@"stopped"] &&
        [session.snapshot[@"message"] isEqual:@"manual Auto stop"] &&
        [session.snapshot[@"cancelled"] isEqual:stopped[@"cancelled"]],
        "typed lifecycle keeps one release and the original Stop reason across repeated callbacks");
}
