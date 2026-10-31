// Session behavior only; these fixtures do not emulate network peers or prove
// server synchronization. They are source cases pending an authorized test run.
static void SetMultiplayer(FakeOrbEngine *engine, unsigned players, unsigned seat = 0) {
    auto &s = engine->live;
    s.identity.entryMode = players == 3 ? 7 : players == 4 ? 11 : 10;
    s.identity.multiplayer = 1;
    auto &m = s.multiplayer;
    m = {}; m.kind = players == 3 ? PHOrbMultiplayerThree : players == 4 ? PHOrbMultiplayerFour : PHOrbMultiplayerEight;
    m.known = m.clockKnown = true; m.teamCount = players; m.localSeat = m.viewTeam = seat;
    m.turnSerial = players == 3 ? seat : 0; // Nonzero competitive seats still own their local boards.
    m.localStatus = 3; m.networkFlags = 1; m.connectionState = 2;
    m.countdownReady = 1; m.clockFlags = players == 3 ? 0 : 4;
    m.clockStart = players == 3 ? 0 : 12345; m.remainingSeconds = 60;
    s.automation = {}; s.thorns = {}; s.leadersValid = false;
}
static void StartMultiplayerOnce(PHOrbSession *session, FakeOrbEngine *engine) {
    Tick(session, engine, Frame());
    Check([session configureQuality:@"fast"], "multiplayer fixture selects fast search");
    [session queueOnce];
    for (unsigned i = 0; i < 2000 && !engine->live.dragged && [session.snapshot[@"busy"] boolValue]; ++i)
        Tick(session, engine, Frame());
    Check(engine->live.dragged && engine->beginFrames == 1, "explicit multiplayer once reaches one acknowledged start");
}
static void MultiplayerStatusTests() {
    for (unsigned players : {3u, 4u, 8u}) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, players, players - 1);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame());
        // Observe an idle board without starting a solver or granting a gesture.
        [session queueRead]; [session queueStop:@"previous stop reason"];
        Tick(session, engine, Frame(), 0.12);
        NSDictionary *status = session.snapshot;
        Check([status[@"canOnce"] boolValue] && [status[@"multiplayerMessage"] hasPrefix:@"可轉一次"] &&
            [status[@"multiplayerMessage"] containsString:[NSString stringWithFormat:@"本機 %u／%u", players, players]],
            "idle multiplayer status leads with availability and retains the validated local seat");
        const PHOrbSnapshot baseline = engine->live;
        for (unsigned condition = 0; condition < 12; ++condition) {
            engine->live = baseline;
            PHBattleFrame frame = Frame();
            NSString *expected = nil;
            switch (condition) {
                case 0: frame.enabled = false; expected = @"轉珠功能未啟用"; break;
                case 1: frame.ready = false; expected = @"遊戲校驗或前景尚未就緒"; break;
                case 2: engine->live.multiplayer.syncState = 1; expected = @"等待多人同步"; break;
                case 3: engine->live.multiplayer.connectionState = 0; expected = @"等待多人同步"; break;
                case 4: engine->live.multiplayer.viewTeam = 0; expected = @"目前顯示其他玩家"; break;
                case 5: engine->live.multiplayer.remainingSeconds = 0.6; expected = @"剩餘時間不足"; break;
                case 6: engine->live.multiplayer.clockKnown = false; expected = @"多人操作時間尚未確認"; break;
                case 7: engine->live.multiplayer.clockFlags = players == 3 ? 3 : 0x14;
                    expected = @"多人計時暫停"; break;
                case 8: engine->live.phase = 3; expected = @"等待本盤結算"; break;
                case 9: engine->live.boardTransition = true; expected = @"等待盤面穩定"; break;
                case 10: engine->live.input.rawActiveMask = 1; expected = @"請先放開手指"; break;
                case 11: engine->live.multiplayer.known = false; expected = @"多人資料未確認"; break;
            }
            Tick(session, engine, frame, 0.12);
            status = session.snapshot;
            Check(![status[@"canOnce"] boolValue] && ![status[@"canStart"] boolValue] &&
                [status[@"multiplayerMessage"] hasPrefix:expected] &&
                [status[@"message"] isEqual:@"previous stop reason"],
                "disabled multiplayer controls show the current reason without overwriting the previous result");
        }
        engine->live = baseline;
        if (players == 3) engine->live.multiplayer.turnSerial++;
        else engine->live.multiplayer.countdownReady = 0;
        Tick(session, engine, Frame(), 0.12);
        NSString *expectedWindow = players == 3 ? @"等待自己的回合" : @"等待對戰開始倒數完成";
        Check(![session.snapshot[@"canOnce"] boolValue] && [session.snapshot[@"multiplayerMessage"] hasPrefix:
            expectedWindow],
            "co-op turn ownership and competitive countdown use distinct status messages");
        if (players != 3) {
            engine->live = baseline; engine->live.multiplayer.finished = 1;
            Tick(session, engine, Frame(), 0.12);
            Check(![session.snapshot[@"canOnce"] boolValue] &&
                [session.snapshot[@"multiplayerMessage"] hasPrefix:@"對戰已結束"],
                "a finished competitive match is not described as ready for another one-shot");
        }
        engine->live = baseline;
        Tick(session, engine, Frame(), 0.12);
        Check([session configureSettings:@{@"quality":@"fast", @"mode":@"leaders", @"goals":@[], @"rules":@[]}],
            "status fixture can select a leader goal without submitting input");
        Check(![session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"多人請選 Combo 或指定形狀"],
            "unsupported multiplayer goals explain the disabled one-shot button");
        Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[]}],
            "status fixture restores Combo before an explicit intent");
        [session queueOnce];
        Check([session.snapshot[@"busy"] boolValue] && ![session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"本次轉珠執行中"],
            "a newly queued one-shot never advertises a competing ready action");
        [session queueStop:@"new stop reason"];
        Tick(session, engine, Frame(), 0.12);
        status = session.snapshot;
        Check([status[@"canOnce"] boolValue] && [status[@"multiplayerMessage"] hasPrefix:@"可轉一次"] &&
            [status[@"message"] isEqual:@"new stop reason"] && !engine->stages && !engine->skillAttempts && !engine->multiplayerArmed,
            "idle readiness recovers after stop without any automatic gesture or skill");
    }
}
static void MultiplayerSessionTests() {
    MultiplayerStatusTests();
    for (unsigned players : {3u, 4u, 8u}) for (unsigned seat = 0; seat < players; ++seat) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, players, seat);
        engine->requireHoldForStart = YES; engine->omitResolvingBoard = YES;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"fast"]; [session queueOnce];
        const NSUInteger generation = [session.snapshot[@"generation"] unsignedIntegerValue];
        [session queueOnce]; [session queueAutomatic]; Execute(session);
        Check([session.snapshot[@"once"] boolValue] && ![session.snapshot[@"automatic"] boolValue] &&
            [session.snapshot[@"generation"] unsignedIntegerValue] == generation && engine->multiplayerArms == 1,
            "duplicate taps do not replace one-shot authorization or start multiplayer continuous mode");
        for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check([session.snapshot[@"completed"] isEqual:@1] && engine->beginFrames == 1 && engine->releases == 1 &&
            !engine->multiplayerArmed && !engine->skillAttempts && ![session.snapshot[@"once"] boolValue],
            "each local seat completes precisely one delayed-start gesture and revokes authorization");
        Check([session.snapshot[@"searchTrace"][@"multiplayer"][@"localSeat"] unsignedIntegerValue] == seat &&
            [session.snapshot[@"executionHistory"] count] == 1 &&
            [session.snapshot[@"executionHistory"][0][@"multiplayer"][@"teamCount"] unsignedIntegerValue] == players &&
            [session.snapshot[@"executionHistory"][0][@"releaseConfirmed"] boolValue],
            "search and execution traces retain local multiplayer ownership and release acknowledgement");
        engine->live.phase = 2; engine->live.moves = 0; engine->live.multiplayer.turnSerial += players;
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        Check(engine->beginFrames == 1 && ![session.snapshot[@"canStart"] boolValue],
            "a new multiplayer turn never starts automatically");
    }
    for (unsigned players : {3u, 4u, 8u}) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, players, players - 1);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        [session configureQuality:@"fast"];
        Check(Preview(session, engine) && ![session.snapshot[@"canExecute"] boolValue] &&
            ![session.snapshot[@"canStart"] boolValue] && [session.snapshot[@"canOnce"] boolValue],
            "multiplayer can preview but only the explicit once action may execute");
        Execute(session); [session queueAutomatic];
        for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
        Check(!engine->stages && !engine->skillAttempts && !engine->multiplayerArmed,
            "preview execute and continuous requests cannot arm multiplayer input or skills");
        Check([session configureSettings:@{@"quality":@"fast", @"mode":@"leaders", @"goals":@[], @"rules":@[]}],
            "leader-mode fixture configuration is valid");
        [session queueOnce];
        Check(!engine->stages && !engine->multiplayerArmed && ![session.snapshot[@"busy"] boolValue] &&
            [session.snapshot[@"message"] containsString:@"隊長"],
            "unsupported multiplayer leader goals are reported without silently falling back to Combo");
    }
    for (unsigned seat = 0; seat < 3; ++seat) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 3, seat);
        engine->live.multiplayer.turnSerial = (seat + 1) % 3;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueOnce];
        Check(![session.snapshot[@"once"] boolValue] && ![session.snapshot[@"canOnce"] boolValue] && !engine->multiplayerArmed,
            "a tap during another co-op seat is rejected immediately");
        engine->live.multiplayer.turnSerial = seat + 3;
        for (unsigned i = 0; i < 40; ++i) Tick(session, engine, Frame());
        Check(!engine->stages, "rejected co-op intent does not wait for a later local turn");
    }
    for (unsigned change = 0; change < 9; ++change) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 2);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueOnce];
        Check(UntilSolveStarts(session, engine), "multiplayer cancellation fixture reaches background search");
        if (change == 0) ++engine->live.multiplayer.turnSerial;
        if (change == 1) engine->live.multiplayer.localSeat = engine->live.multiplayer.viewTeam = 1;
        if (change == 2) engine->live.multiplayer.viewTeam = 0;
        if (change == 3) ++engine->live.multiplayer.clockStart;
        if (change == 4) engine->live.multiplayer.connectionState = 0;
        if (change == 5) engine->live.input.rawActiveMask = 1;
        if (change == 6) engine->live.boardTransition = true;
        if (change == 7) engine->failRead = YES;
        if (change == 8) std::swap(engine->live.board.tokens[0], engine->live.board.tokens[1]);
        for (unsigned i = 0; i < 35; ++i) Tick(session, engine, Frame());
        Check(!engine->stages && !engine->multiplayerArmed && ![session.snapshot[@"once"] boolValue] &&
            ![session.snapshot[@"busy"] boolValue], "stale multiplayer worker cannot publish executable input after a context change");
        if (change == 7) {
            Check([session.snapshot[@"multiplayerActive"] boolValue] &&
                ![session.snapshot[@"canStart"] boolValue] && ![session.snapshot[@"canOnce"] boolValue],
                "failed reads retain multiplayer status without offering unavailable input");
            [session queueAutomatic];
            Check(![session.snapshot[@"busy"] boolValue] && !engine->stages && !engine->multiplayerArmed,
                "known multiplayer mode rejects continuous intent even during an early read failure");
        }
    }
    for (unsigned change = 0; change < 8; ++change) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 3, 1);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        StartMultiplayerOnce(session, engine);
        const NSUInteger stages = engine->stages;
        if (change == 0) engine->live.multiplayer.turnSerial += 3; // Same seat, different turn.
        if (change == 1) engine->live.multiplayer.viewTeam = 2;
        if (change == 2) engine->live.multiplayer.syncState = 1;
        if (change == 3) engine->live.multiplayer.connectionState = 0;
        if (change == 4) engine->live.multiplayer.networkFlags = 0x11;
        if (change == 5) engine->live.multiplayer.localSeat = 2;
        if (change == 6) engine->live.identity.multiplayer = 0;
        if (change == 7) engine->live.boardTransition = true;
        Tick(session, engine, Frame());
        Check(engine->stages == stages && !engine->releases && !engine->multiplayerArmed &&
            ![session.snapshot[@"busy"] boolValue], "turn handoff, sync, disconnect, selector reset and resize get no stale cleanup");
    }
    for (unsigned stop = 0; stop < 4; ++stop) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 8, 7);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        StartMultiplayerOnce(session, engine);
        if (stop == 0) [session queueStop:@"multiplayer stop"];
        if (stop == 1) [session stopForLifecycle:@"multiplayer foreground loss"];
        if (stop == 2) engine->live.input.rawActiveMask = 1;
        if (stop == 3) engine->live.multiplayer.remainingSeconds = 0.2;
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame());
        Check(engine->releases == 1 && !engine->live.dragged && !engine->multiplayerArmed &&
            [session.snapshot[@"completed"] isEqual:@0], "stop, lifecycle, hand input or low deadline releases only the current owned token");
        if (stop == 2) Check(engine->live.input.rawActiveMask == 1, "multiplayer cancellation preserves real hand input");
    }
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 4, 3);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        StartMultiplayerOnce(session, engine); engine->ignoreRelease = YES;
        [session queueStop:@"release retry"];
        Check(![session.snapshot[@"canOnce"] boolValue] &&
            [session.snapshot[@"multiplayerMessage"] hasPrefix:@"正在放開珠子"],
            "pending release remains busy and explains why another gesture cannot start");
        Tick(session, engine, Frame()); Tick(session, engine, Frame());
        Check(engine->releases == 2 && !engine->multiplayerArmed && engine->beginFrames == 1,
            "release retries retain an owned token after authorization is revoked");
        engine->ignoreRelease = NO; Tick(session, engine, Frame());
        Check(!engine->live.dragged && ![session.snapshot[@"busy"] boolValue], "native release acknowledgement finishes cancellation");
    }
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 3);
        engine->requireHoldForStart = YES;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"fast"]; [session queueOnce];
        for (unsigned i = 0; i < 2000 && !engine->beginFrames && [session.snapshot[@"busy"] boolValue]; ++i)
            Tick(session, engine, Frame());
        Check(engine->beginFrames == 1 && !engine->live.dragged, "deferred-start cancellation has not selected an orb");
        const NSUInteger stages = engine->stages; [session queueStop:@"cancel pending start"];
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages && !engine->releases && !engine->multiplayerArmed,
            "stopping a deferred start sends neither another hold nor an unowned release");
    }
    for (unsigned players : {3u,4u,8u}) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, players);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"fast"]; [session queueOnce];
        BOOL cancelledBegin = NO;
        for (unsigned i = 0; i < 2000 && !cancelledBegin && [session.snapshot[@"busy"] boolValue]; ++i) {
            now += 0.02;
            [session beforeFrame:Frame() time:now];
            @try {
                [engine nativeFrame];
                if (engine->staged && engine->beginning) {
                    [session queueStop:@"stop during native begin"];
                    cancelledBegin = YES;
                }
            } @finally { [session finishNativeFrame]; }
            [session afterFrame:Frame() time:now];
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
        }
        Check(cancelledBegin && engine->live.dragged && !engine->multiplayerArmed &&
            [session.snapshot[@"message"] isEqual:@"stop during native begin"],
            "stop between native selection and acknowledgement preserves its reason and revokes further holds");
        for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame());
        Check(engine->beginFrames == 1 && engine->releases == 1 && !engine->live.dragged &&
            [session.snapshot[@"completed"] isEqual:@0] && ![session.snapshot[@"busy"] boolValue],
            "cancelled native begin acknowledges only its captured token for release");
        [session queueStop:@"idle stop after cancelled begin"];
        Check(![session.snapshot[@"busy"] boolValue], "old start flags cannot turn a later idle stop into a release loop");
    }
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 8, 6);
        engine->live.multiplayer.remainingSeconds = 1.5;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session configureQuality:@"thorough"]; [session queueOnce];
        Check(UntilSolveStarts(session, engine), "short competitive clock still permits a bounded search");
        NSDictionary *trace = session.snapshot[@"searchTrace"];
        const double search = [trace[@"budgetMS"] doubleValue] / 1000;
        const double route = [trace[@"stepLimit"] doubleValue] * (4.0 / 60.0);
        Check(search + 0.35 + route + 0.40 <= 1.500001 && [trace[@"stepLimit"] unsignedIntegerValue] < 48,
            "solve budget reserves arming, frame-quantized moves and release before the competitive deadline");
        engine->live.multiplayer.remainingSeconds = 0.2;
        for (unsigned i = 0; i < 30; ++i) Tick(session, engine, Frame());
        Check(!engine->stages && !engine->multiplayerArmed, "expired route budget cancels a worker before beginning input");
    }
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine, 3);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        StartMultiplayerOnce(session, engine); const NSUInteger stages = engine->stages;
        [session stopForLifecycle:@"background"];
        PHBattleFrame background = Frame(); background.ready = false;
        Tick(session, engine, background); engine->live.dragged = 0;
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages && !engine->multiplayerArmed && ![session.snapshot[@"busy"] boolValue],
            "background loss sends no cleanup into an unavailable context and never resumes on foreground return");
    }
}
