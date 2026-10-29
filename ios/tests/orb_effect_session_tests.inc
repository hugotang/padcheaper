static void SetEffectBoard(FakeOrbEngine *engine, bool spinners = true) {
    auto &board = engine->live.board;
    const unsigned w = board.width, n = w * board.height;
    // Only the final adjacent pair is reachable. This makes the execution
    // assertion independent of beam quality, timing and incidental combos.
    board.fixedStart = uint8_t(n - 1); board.minErase = 3;
    board.tapeMask = (UINT64_C(1) << (n - 2)) - 1;
    board.cloudMask = UINT64_C(3) << (n - 2);
    for (unsigned i = 0; i < n; ++i) {
        board.types[i] = uint8_t((i % w + 2 * (i / w)) % 6);
        board.flags[i] = i < n - 2 ? PHOrbFlagTape | PHOrbFlagStartDim : 0;
    }
    board.flags[2] |= PHOrbFlagThorn; board.thornDamage[2] = 4;
    board.flags[n - 2] = PHOrbFlagBlindTouch | PHOrbFlagLocked | PHOrbFlagNail | PHOrbFlagCombo;
    board.flags[n - 1] = PHOrbFlagBlind | PHOrbFlagStartDim; board.blindTurns[n - 1] = 5;
    float weak = -0.5f; std::memcpy(&board.enhancement[n - 2], &weak, sizeof(weak));
    if (spinners) {
        board.spinnerMask = 1 | (UINT64_C(1) << w); board.spinnerColors = 63;
        engine->live.spinnerTurns = 4; engine->live.spinnerPeriodFrames = 30; engine->tickSpinners = YES;
    }
}

static void OrbEffectSessionTests() {
    for (auto dimensions : {std::pair<unsigned,unsigned>{6,5}, {7,6}, {5,4}}) {
        FakeOrbEngine *engine = [[FakeOrbEngine alloc] initWithWidth:dimensions.first height:dimensions.second];
        SetEffectBoard(engine);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        [session configureQuality:@"fast"];
        Check(Preview(session,engine), "shared-phase spinner colors stabilize and solve on every supported geometry");
        const unsigned n = dimensions.first * dimensions.second;
        NSDictionary *state = [session snapshot], *board = state[@"board"];
        Check([state[@"path"] isEqual:@[@(n - 2), @(n - 1)]] && [state[@"spinnerVariants"] unsignedIntValue] == 6 &&
            [board[@"cloud"][n - 2] boolValue] && [board[@"tape"][0] boolValue] && [board[@"blocked"][2] boolValue] &&
            ![board[@"enhanced"][n - 2] boolValue] && [board[@"weakened"][n - 2] boolValue] &&
            [board[@"blindTurns"][n - 1] unsignedIntValue] == 5 && [board[@"thornDamage"][2] unsignedIntValue] == 4,
            "preview preserves visual effects, signed enhancement, fixed start and restricted path cells");
        for (unsigned i = 0; i < 12; ++i) Tick(session,engine,Frame());
        Check([[session snapshot][@"canExecute"] boolValue] && !engine->stages,
            "a color change after solving keeps the phase-complete preview executable without staging input");
        Execute(session);
        for (unsigned i = 0; i < 150 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
        Check([[session snapshot][@"phase"] isEqual:@"complete"] && engine->releases == 1 && engine->live.moves == 1 &&
            engine->live.board.blindTurns[n - 2] == 5 && engine->live.board.blindTurns[n - 1] == 0 &&
            (engine->live.board.flags[n - 1] & PHOrbFlagNail) && !(engine->live.board.flags[n - 1] & PHOrbFlagBlindTouch) &&
            engine->live.board.thornDamage[2] == 4 && [[session snapshot][@"confirmed"] unsignedIntValue] == 1,
            "execution tolerates every shared spinner tick, native darkness clearing and orb metadata swaps");
    }
    for (unsigned change = 0; change < 8; ++change) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetEffectBoard(engine);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureQuality:@"fast"];
        Check(Preview(session,engine), "effect-change preview fixture is ready");
        auto &board = engine->live.board;
        if (change == 0) board.spinnerMask &= ~UINT64_C(1);
        if (change == 1) board.spinnerColors = 31;
        if (change == 2) board.types[0] = uint8_t((board.types[0] + 1) % 6);
        if (change == 3) { board.tapeMask |= UINT64_C(1) << 29; board.flags[29] |= PHOrbFlagTape; }
        if (change == 4) board.fixedStart = 30;
        if (change == 5) board.cloudMask = 1;
        if (change == 6) board.unmatchableColors = 32;
        if (change == 7) board.minMatch = 4;
        for (unsigned i = 0; i < 8; ++i) Tick(session,engine,Frame());
        Execute(session);
        Check(![[session snapshot][@"canExecute"] boolValue] && !engine->stages && ![[session snapshot][@"busy"] boolValue],
            "changed masks, relative phases or match rules invalidate the old generation before any input");
    }
    for (unsigned change = 0; change < 2; ++change) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetEffectBoard(engine);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureQuality:@"fast"];
        Check(Preview(session,engine), "mid-drag effect-change fixture is ready"); Execute(session); UntilDragging(session,engine);
        if (change == 0) engine->live.board.types[0] = uint8_t((engine->live.board.types[0] + 1) % 6);
        else { engine->live.board.tapeMask |= UINT64_C(1) << 29; engine->live.board.flags[29] |= PHOrbFlagTape; }
        for (unsigned i = 0; i < 20 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
        Check(engine->releases == 1 && !engine->live.moves && !engine->live.dragged &&
            [[session snapshot][@"completed"] unsignedIntValue] == 0,
            "a new restriction or independently changing spinner releases the owned orb without the pending swap");
    }
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetEffectBoard(engine,false); engine->createSpinnerWithSkill = YES;
    PHOrbSession *session = AutomaticSession(engine,@[PHOrbDefaultSkillRule()]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && engine->releases == 1 &&
        engine->live.board.spinnerMask && [[session snapshot][@"spinnerVariants"] unsignedIntValue] == 6,
        "a Yoruichi-style skill starts spinners, then a fresh phase-complete route continues the same turn");
    Check(NextOrbTurn(session,engine) && engine->releases == 2,
        "the next continuous turn recomputes restrictions while the spinner effect is still active");
    [session queueStop:@"end effect skill creation"];

    NSMutableDictionary *remedy = [PHOrbDefaultSkillRule() mutableCopy]; remedy[@"obstacle"] = @"spinner";
    engine = [FakeOrbEngine new]; SetEffectBoard(engine); engine->removeSpinnerWithSkill = YES;
    session = AutomaticSession(engine,@[remedy]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && !engine->live.board.spinnerMask,
        "spinner remedy skills remain eligible across shared color phases and discard the old effect after acknowledgement");
    [session queueStop:@"end supported spinner remedy"];
    for (BOOL remove : {NO, YES}) {
        engine = [FakeOrbEngine new]; SetEffectBoard(engine);
        engine->live.board.spinnerColors = 0x400; engine->tickSpinners = NO;
        engine->live.ordinary = false; engine->live.obstacles = PHOrbObstacleSpinner | PHOrbObstacleBoardEffect;
        engine->removeSpinnerWithSkill = remove; session = AutomaticSession(engine,@[remedy]);
        BOOL completed = UntilPhase(session,engine,@"turnWaiting");
        Check(engine->skillOrder.size() == 1 && (remove ? completed && engine->releases == 1 : !completed && !engine->stages),
            "unsupported stable spinners permit an explicit remedy but only a freshly supported board can drag");
        [session queueStop:@"end unsupported spinner remedy"];
    }
    engine = [FakeOrbEngine new]; SetEffectBoard(engine); session = AutomaticSession(engine,@[remedy]);
    Check(UntilPhase(session,engine,@"skillArming"), "phase-aware skill preflight is prepared");
    engine->live.board.spinnerMask &= ~UINT64_C(1); Tick(session,engine,Frame());
    Check(engine->skillOrder.empty() && !engine->stages && ![[session snapshot][@"automatic"] boolValue],
        "a different spinner layout cancels the armed native skill before its call");

    for (unsigned hazard : {unsigned(PHOrbPoison), unsigned(PHOrbMortalPoison), unsigned(PHOrbBomb)}) {
        for (BOOL automatic : {NO, YES}) {
            engine = [FakeOrbEngine new]; SetEffectBoard(engine,false);
            for (unsigned i = 0; i < (hazard == PHOrbBomb ? 1u : 3u); ++i) engine->live.board.types[i] = uint8_t(hazard);
            if (automatic) {
                session = AutomaticSession(engine,@[]);
                Check(!UntilPhase(session,engine,@"turnWaiting") && !engine->stages,
                    "automatic routes stop on inevitable poison removal or a first-wave bomb explosion");
            } else {
                session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureQuality:@"fast"];
                Check(Preview(session,engine) && [[session snapshot][@"goalsMet"] boolValue] &&
                    ![[session snapshot][@"hazardsSafe"] boolValue] && ![[session snapshot][@"canExecute"] boolValue],
                    "an unsafe candidate remains reviewable but the manual execute control stays disabled");
                Execute(session); Tick(session,engine,Frame());
                Check(!engine->stages, "queueExecute independently refuses an unsafe route");
                [session queueStop:@"end unsafe preview"];
            }
        }
    }
    engine = [FakeOrbEngine new]; SetEffectBoard(engine); engine->live.identity.multiplayer = 1;
    session = AutomaticSession(engine,@[PHOrbDefaultSkillRule()]);
    Tick(session,engine,Frame());
    Check(!engine->stages && engine->skillOrder.empty() && ![[session snapshot][@"automatic"] boolValue],
        "supported effects never relax the single-player restriction");
}
