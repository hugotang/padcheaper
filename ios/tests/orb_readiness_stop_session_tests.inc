// Exercise the cold UI path without queueRead/queueOnce or a fabricated
// canStart value. Native/GUI suites cover the reader and action wiring.
static void ReadinessStopSessionTests() {
    FakeOrbEngine *engine = EightEngineFixture();
    engine->live.multiplayer.countdownReady = 0;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    [session monitorReadiness];
    Check(!engine->reads,"requesting readiness only schedules a read on the native frame");
    Tick(session,engine,Frame());
    Check(engine->reads == 1 && ![session.snapshot[@"canStart"] boolValue] &&
        ![session.snapshot[@"canOnce"] boolValue] && [session.snapshot[@"onceReadiness"][@"readinessMonitoring"] boolValue] &&
        ![session.snapshot[@"onceReadiness"][@"monitoring"] boolValue],
        "first UI observation detects 8P countdown without queuing a solve");
    for (unsigned i = 0; i < 20; ++i) { [session monitorReadiness]; Tick(session,engine,Frame(),0.01); }
    Check(engine->reads == 1,"repeated UI opens do not defeat the four-per-second idle read limit");
    engine->live.multiplayer.countdownReady = 1;
    Tick(session,engine,Frame(),0.26);
    Check([session.snapshot[@"canStart"] boolValue] && [session.snapshot[@"canOnce"] boolValue] &&
        [session.snapshot[@"eightAutoAvailable"] boolValue],"countdown completion enables first Auto without a prior read/once tap");
    Check(!engine->stages && !engine->skillAttempts && !engine->multiplayerArms && !engine->eightArmed &&
        !engine->observationReads && !engine->enemyActionReads && !engine->dungeonReads &&
        ![session.snapshot[@"searchTrace"] count] && ![session.snapshot[@"automatic"] boolValue],
        "readiness observation never solves, collects reports, casts, arms or drags");
    const PHOrbSnapshot ready = engine->live;
    for (unsigned failure = 0; failure < 7; ++failure) {
        engine->live = ready; engine->failRead = failure == 0;
        PHBattleFrame frame = Frame();
        if (failure == 1) engine->live.multiplayer.connectionState = 0;
        if (failure == 2) engine->live.multiplayer.viewTeam = 0;
        if (failure == 3) engine->live.multiplayer.remainingSeconds = 0;
        if (failure == 4) engine->live.multiplayer.clockFlags = 0x14;
        if (failure == 5) frame.ready = false;
        if (failure == 6) frame.enabled = false;
        const NSUInteger reads = engine->reads;
        Tick(session,engine,frame,0.26);
        Check(![session.snapshot[@"canStart"] boolValue] && ![session.snapshot[@"canOnce"] boolValue],
            "read, seat, connection, time, frame and feature gates remain closed during passive observation");
        if (failure >= 5) Check(engine->reads == reads,"unready/disabled frames do not perform idle readiness reads");
    }
    engine->failRead = NO; engine->live = ready; Tick(session,engine,Frame(),0.26);
    Check([session.snapshot[@"canStart"] boolValue],"readiness recovers after a transient failure");
    engine->live.multiplayer.countdownReady = 0; [session queueAutomatic];
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->eightArmed && !engine->stages,
        "Auto rereads the operation window instead of trusting the last enabled button");
    engine->live = ready; Tick(session,engine,Frame(),0.26);
    Check([session.snapshot[@"canStart"] boolValue] && ![session.snapshot[@"automatic"] boolValue],
        "a recovered window never automatically starts a rejected intent");
    [session queueAutomatic];
    Check([session.snapshot[@"eightAutomatic"] boolValue] && engine->eightArmed,
        "a fresh explicit Auto click starts the match directly after passive readiness");
    [session queueStop:@"explicit stop"];
    engine->live.identity.entryHash[0]++; Tick(session,engine,Frame(),0.26);
    [session stopForLifecycle:@"background"]; Tick(session,engine,Frame(),0.26);
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->skillAttempts,
        "stop, a new entry and foreground recovery cannot turn observation into Auto");

    for (unsigned players : {3u,4u}) {
        engine = [FakeOrbEngine new]; SetMultiplayer(engine,players,players - 1);
        session = [[PHOrbSession alloc] initWithEngine:engine];
        [session monitorReadiness]; Tick(session,engine,Frame());
        Check([session.snapshot[@"canOnce"] boolValue] && ![session.snapshot[@"canStart"] boolValue] &&
            !engine->multiplayerArms,"passive readiness leaves 3/4P as explicit once only");
    }

    engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
    Tick(session,engine,Frame()); [session queueAutomatic];
    for (uint32_t transition : {3u,6u,18u,19u,20u,22u,26u,27u,0u,2u}) {
        engine->live.phase = transition; Tick(session,engine,Frame());
        Check([session.snapshot[@"automatic"] boolValue] && !engine->stages,
            "solo floor, resolve and skill transitions keep Auto active without premature input");
    }
    [session queueStop:@"normal transitions done"];

    for (bool eight : {false,true}) for (uint32_t terminal : {28u,29u,30u,31u,67u,68u}) {
        engine = eight ? EightEngineFixture() : [FakeOrbEngine new];
        session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        [session queueAutomatic];
        Check([session.snapshot[@"automatic"] boolValue],"terminal fixture begins an explicit Auto intent");
        engine->live.phase = terminal;
        // Result handling can retire team/enemy objects before leaving the
        // battle scene, with time remaining and the global finished byte 0.
        engine->live.automation = {}; engine->live.eight = {}; engine->live.enemyCombo = {};
        engine->live.board = {}; engine->live.settled = engine->live.ordinary = false;
        Tick(session,engine,Frame());
        NSDictionary *stopped = session.snapshot;
        Check(![stopped[@"automatic"] boolValue] && ![stopped[@"busy"] boolValue] &&
            [stopped[@"battleEnded"] boolValue] && ![stopped[@"canStart"] boolValue] &&
            ![stopped[@"canOnce"] boolValue] && !engine->eightArmed && !engine->multiplayerArmed &&
            !engine->stages && !engine->skillAttempts && [stopped[@"message"] containsString:@"Auto 已停止"],
            "confirmed clear/results stop on the first frame without board/team data or a timer expiry");
        for (unsigned i = 0; i < 5; ++i) Tick(session,engine,Frame(),0.26);
        [session queueAutomatic];
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->skillAttempts,
            "staying on results or pressing Auto there never restarts input");
        FakeOrbEngine *next = eight ? EightEngineFixture() : [FakeOrbEngine new];
        engine->live = next->live;
        engine->live.identity.entryHash[0]++; Tick(session,engine,Frame(),0.26);
        Check([session.snapshot[@"canStart"] boolValue] && ![session.snapshot[@"automatic"] boolValue],
            "another match may expose Auto but requires another explicit click");
    }
    // Cancel an actual pending worker; a later completion cannot revive Auto.
    engine = EightEngineFixture(); session = [[PHOrbSession alloc] initWithEngine:engine];
    Tick(session,engine,Frame()); [session queueAutomatic];
    Check(UntilSolveStarts(session,engine),"terminal cancellation fixture starts a real solve");
    engine->live.phase = 28; engine->live.automation = {}; engine->live.eight = {};
    Tick(session,engine,Frame());
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->skillAttempts &&
        [session.snapshot[@"message"] containsString:@"Dungeon Clear"],
        "Dungeon Clear cancels a pending solve and its late callback cannot stage input");

    engine = EightEngineFixture(); session = [[PHOrbSession alloc] initWithEngine:engine];
    Tick(session,engine,Frame()); [session configureQuality:@"fast"]; [session queueAutomatic];
    for (unsigned i = 0; i < 3000 && !engine->live.dragged && [session.snapshot[@"automatic"] boolValue]; ++i)
        Tick(session,engine,Frame());
    Check(engine->live.dragged && engine->beginFrames == 1,"terminal input fixture reaches an owned native drag");
    const NSUInteger stages = engine->stages;
    engine->live.phase = 68;
    Tick(session,engine,Frame());
    Check(![session.snapshot[@"busy"] boolValue] && !engine->eightArmed && !engine->multiplayerArmed &&
        engine->stages == stages && [session.snapshot[@"message"] containsString:@"結算"],
        "terminal pre-frame handling revokes a drag without injecting holds or releases into results");
}
