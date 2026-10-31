static void SetDiagonalBoard(FakeOrbEngine *engine, bool thorn = false) {
    auto &board = engine->live.board;
    const unsigned w = board.width, n = w * board.height, a = n - w - 2, b = n - 1;
    board.fixedStart = uint8_t(a + 1);
    for (unsigned i = 0; i < n; ++i) {
        board.types[i] = uint8_t((i % w + 2 * (i / w)) % 5);
        if (i != a && i != b) {
            board.tapeMask |= UINT64_C(1) << i;
            board.flags[i] = PHOrbFlagTape;
        }
    }
    for (unsigned i = 0; i < 4; ++i) board.types[i] = 5;
    board.flags[a] = PHOrbFlagLocked | PHOrbFlagNail | PHOrbFlagStartDim;
    if (thorn) { board.flags[b] = PHOrbFlagThorn; board.thornDamage[b] = 4; }
}
static NSDictionary *DiagonalSettings(bool enabled) {
    return @{@"quality":@"fast", @"mode":@"manual", @"rules":@[], @"diagonal":@(enabled),
        @"goals":@[@{@"kind":@"four", @"colors":@32, @"count":@1, @"length":@0}]};
}
static void DiagonalSessionTests() {
    for (auto dimensions : {std::pair<unsigned,unsigned>{6,5}, {7,6}, {5,4}}) for (unsigned fps : {30u,60u}) {
        FakeOrbEngine *engine = [[FakeOrbEngine alloc] initWithWidth:dimensions.first height:dimensions.second];
        SetDiagonalBoard(engine, true); engine->live.fps = uint8_t(fps);
        engine->live.leadersValid = true; engine->live.leaderNumbers[0] = engine->live.leaderNumbers[1] = 12832;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:DiagonalSettings(true)] && [session configureQuality:@"fast"] &&
            Preview(session,engine) && [session.snapshot[@"diagonal"] boolValue] && [session.snapshot[@"routeDiagonal"] boolValue],
            "quality changes preserve a diagonal preview on every supported geometry");
        const unsigned a = dimensions.first * dimensions.second - dimensions.first - 2, b = dimensions.first * dimensions.second - 1;
        Check([session.snapshot[@"path"] isEqual:@[@(a),@(b)]] &&
            [session.snapshot[@"searchTrace"][@"diagonal"] isEqual:@YES], "only the isolated diagonal move is advertised");
        Execute(session);
        for (unsigned i = 0; i < 150 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / fps);
        NSDictionary *record = [session.snapshot[@"executionHistory"] lastObject];
        Check([session.snapshot[@"phase"] isEqual:@"complete"] && engine->live.moves == 1 && engine->releases == 1 &&
            engine->live.thorns.pendingDamage == 40 && [record[@"confirmedSteps"] isEqual:@1] &&
            [record[@"releaseConfirmed"] boolValue] && [record[@"search"][@"diagonal"] isEqual:@YES] &&
            [record[@"leaderNumbers"] isEqual:@[@12832,@12832]] && [record[@"scope"] isEqual:@"preview-execute"] &&
            [record[@"releaseEvaluation"][@"heartFours"] isEqual:@1],
            "a directly accepted diagonal token exchange and thorn contact require acknowledgment before release");
        NSDictionary *captured = session.snapshot[@"searchTrace"];
        engine->failRead = YES;
        for (unsigned i = 0; i < 7; ++i) Tick(session,engine,Frame());
        Check([session.snapshot[@"lastReadIssue"][@"reason"] length] && ![session.snapshot[@"liveReadValid"] boolValue] &&
            [session.snapshot[@"searchTraceRelation"] isEqual:@"current-unreadable"] && [session.snapshot[@"searchTrace"] isEqual:captured],
            "idle read failure is diagnosed without replacing the captured successful search");
        engine->failRead = NO; ++engine->live.identity.epoch;
        for (unsigned i = 0; i < 7; ++i) Tick(session,engine,Frame());
        Check([session.snapshot[@"searchTraceRelation"] isEqual:@"captured-in-earlier-battle"] &&
            [session.snapshot[@"searchTrace"] isEqual:captured], "a new battle labels a retained search rather than treating it as current");
    }
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetDiagonalBoard(engine);
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    [session configureSettings:DiagonalSettings(true)];
    Check(Preview(session,engine), "diagonal permission revocation fixture has a preview");
    Check([session configureSettings:DiagonalSettings(false)] && ![session.snapshot[@"canExecute"] boolValue],
        "revoking diagonal permission invalidates the old preview");
    Execute(session); Tick(session,engine,Frame()); Check(!engine->stages, "revoked diagonal preview cannot stage a touch");

    engine = [FakeOrbEngine new]; SetDiagonalBoard(engine); engine->wrongDiagonalTarget = YES;
    session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureSettings:DiagonalSettings(true)];
    Check(Preview(session,engine), "native wrong-target diagonal fixture is prepared"); Execute(session);
    for (unsigned i = 0; i < 150 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(![session.snapshot[@"completed"] unsignedIntegerValue] && [session.snapshot[@"confirmed"] isEqual:@0] &&
        engine->releases == 1 && [session.snapshot[@"message"] containsString:@"不符"],
        "a horizontal intermediate selected instead of the diagonal target stops and releases without acknowledging the planned swap");

    engine = [FakeOrbEngine new]; SetDiagonalBoard(engine); engine->live.identity.multiplayer = 1;
    session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureSettings:DiagonalSettings(true)];
    Tick(session,engine,Frame()); [session queueOnce];
    for (unsigned i = 0; i < 35; ++i) Tick(session,engine,Frame());
    Check(!engine->stages && !engine->skillAttempts, "diagonal permission cannot enable multiplayer orb or skill input");
}
