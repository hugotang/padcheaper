// Written Session regressions for the post-0.5.41 monochrome report. These
// sources have not been executed in this source-only batch. Requests use only
// public Session entry points; no route, result or private state is injected.
static FakeOrbEngine *MonochromeSessionEngine(unsigned remainingFrames) {
    FakeOrbEngine *engine = [FakeOrbEngine new];
    auto &state = engine->live; auto &board = state.board;
    std::fill(board.types,board.types + 30,uint8_t(5));
    for (unsigned i : {1u,2u,5u,6u,10u,11u,13u,14u,23u,25u,28u,29u})
        board.enhancement[i] = 1031127695;
    board.types[6] = 4; board.spinnerMask = UINT64_C(1) << 6;
    board.spinnerColors = 63; board.minErase = 3;
    state.remainingFrames = remainingFrames; state.fps = 30;
    state.spinnerTurns = 4; state.spinnerPeriodFrames = 15; state.spinnerCountdown = 8;
    state.spinnerCountdownKnown = state.orbMotionKnown = true;
    return engine;
}
static NSDictionary *MonochromeSessionSettings(bool shapes) {
    NSArray *goals = shapes ?
        @[@{@"kind":@"t",@"colors":@8,@"count":@1,@"length":@0},
          @{@"kind":@"four",@"colors":@32,@"count":@1,@"length":@0}] :
        @[@{@"kind":@"combo",@"colors":@0,@"count":@1,@"length":@0}];
    return @{@"quality":shapes ? @"thorough" : @"fast",@"mode":@"manual",@"goals":goals,@"rules":@[],
        @"diagonal":@YES,@"allowPoisonWithHeartFour":@YES,@"thornComboPriority":@YES};
}
static NSDictionary *MonochromeSessionRequest(PHOrbSession *session, FakeOrbEngine *engine, bool once) {
    Tick(session,engine,Frame());
    if (once) [session queueOnce]; else [session queueRead];
    bool workerStarted = false;
    for (unsigned i = 0; i < 100; ++i) {
        // Observe the synchronous phase before allowing any worker completion
        // to run, so a fast accidental solve cannot hide the early-exit bug.
        Tick(session,engine,Frame(),1.0 / 30,0,false);
        NSDictionary *state = session.snapshot;
        workerStarted |= [state[@"phase"] isEqual:@"solving"];
        if (workerStarted || [state[@"phase"] isEqual:@"stopped"] ||
            [state[@"phase"] isEqual:@"blocked"] || [state[@"phase"] isEqual:@"fault"]) break;
    }
    Check(!workerStarted,"monochrome geometric rejection occurs synchronously before worker launch");
    return session.snapshot;
}
static void CheckMonochromeSessionFailure(NSDictionary *state, FakeOrbEngine *engine,
    bool once, unsigned remainingFrames) {
    NSDictionary *trace = state[@"searchTrace"], *bound = trace[@"monochromeTemplate"];
    Check([state[@"phase"] isEqual:@"stopped"] && ![state[@"busy"] boolValue] &&
        ![state[@"once"] boolValue] && ![state[@"automatic"] boolValue] &&
        ![state[@"canExecute"] boolValue] && [state[@"path"] isEqual:@[]],
        "monochrome preview and once rejection clear busy intent and expose no executable route");
    Check([trace[@"status"] isEqual:@"no-route"] && [trace[@"solverStarted"] isEqual:@NO] &&
        [trace[@"statisticsAvailable"] isEqual:@NO] && [trace[@"scope"] isEqual:once ? @"once" : @"preview"] &&
        [trace[@"candidateAttempts"] isEqual:@[]] && [trace[@"multiFishingAttempts"] isEqual:@[]] &&
        [trace[@"selectedAttemptIndex"] isEqual:@(-1)] && [trace[@"selectedStatus"] isEqual:@"none"] &&
        [trace[@"path"] isEqual:@[]],
        "geometric failure records zero attempted searches rather than a deadline or selected candidate");
    Check([bound[@"eligible"] isEqual:@YES] && [bound[@"applied"] isEqual:@YES] &&
        [bound[@"reason"] isEqual:@"exact-four-separator-bound"] &&
        [bound[@"method"] isEqual:@"exact-first-wave-geometry-lower-bound"] &&
        [bound[@"minimumExchangesLowerBound"] isEqual:@7] && [bound[@"maximumExchanges"] isEqual:@6] &&
        [bound[@"lowerBoundOnly"] isEqual:@YES] && [bound[@"timeFeasibilityEvaluated"] isEqual:@NO] &&
        [bound[@"routeFound"] isEqual:@NO],
        "exact T and donor Four report a seven-take necessary bound above the six-take cap without claiming feasibility");
    Check([trace[@"ordinarySearchSkipped"] isEqual:@"exact-four-separator-bound"] &&
        [trace[@"soloRetrySkipped"] isEqual:@"exact-four-separator-bound"] &&
        [trace[@"fishingNotAttemptedReason"] isEqual:@"exact-four-separator-bound"] &&
        [trace[@"multiFishingCoverage"][@"started"] isEqual:@NO],
        "ordinary, retry and multi-fishing diagnostics all retain the same geometric skip reason");
    Check([trace[@"timing"][@"remainingFrames"] isEqual:@(remainingFrames)] &&
        [trace[@"timing"][@"fps"] isEqual:@30] && [trace[@"board"][@"spinnerMask"] isEqual:@64] &&
        [trace[@"board"][@"spinnerColors"] isEqual:@63] && [trace[@"goals"] count] == 2,
        "geometric diagnostics retain the reported board, goals and actual native clock");
    NSArray *types = trace[@"board"][@"types"], *enhancements = trace[@"board"][@"enhancementBits"];
    Check(types.count == 30 && enhancements.count == 30,"geometric failure retains all thirty reported orb entries");
    if (types.count == 30 && enhancements.count == 30) for (unsigned i = 0; i < 30; ++i)
        Check([types[i] isEqual:@(engine->live.board.types[i])] &&
            [enhancements[i] isEqual:@(engine->live.board.enhancement[i])],
            "legal enhancements and the dark source are preserved in the rejected report");
    NSString *message = state[@"message"];
    Check([message isEqual:trace[@"reason"]] && [message containsString:@"至少需取出 7"] &&
        [message containsString:@"最多連撈 6"] && [message containsString:@"必要條件"] &&
        [message containsString:@"尚未確認"] && [message containsString:@"未開始轉珠"],
        "failure text distinguishes the required separator bound from unproved route or time feasibility");
    Check([state[@"lastFailedSearch"] isEqual:trace] && [NSJSONSerialization isValidJSONObject:trace],
        "the complete early failure is retained as serializable diagnostic evidence");
    Check(!engine->stages && !engine->releases && !engine->beginFrames && !engine->live.dragged &&
        !engine->allowedSpinnerCell && !engine->multiplayerArmed && !engine->eightArmed &&
        [state[@"executionHistory"] isEqual:@[]],
        "a geometric failure neither opens native input gates nor stages or records an execution");
}
static void MonochromeSessionTests() {
    // The same rejection must occur with the reported thirteen seconds and
    // with ample time: increasing the clock cannot change this geometry bound.
    for (unsigned frames : {390u,3000u}) for (bool once : {false,true}) {
        FakeOrbEngine *engine = MonochromeSessionEngine(frames);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:MonochromeSessionSettings(true)],
            "reported monochrome light-T and heart-Four settings are accepted");
        NSDictionary *state = MonochromeSessionRequest(session,engine,once);
        CheckMonochromeSessionFailure(state,engine,once,frames);
        Execute(session);
        for (unsigned i = 0; i < 12; ++i) Tick(session,engine,Frame());
        Check(!engine->stages && !engine->releases && ![session.snapshot[@"busy"] boolValue] &&
            [session.snapshot[@"lastFailedSearch"] isEqual:state[@"lastFailedSearch"]],
            "direct execute and later frames cannot revive a rejected monochrome route");
        [session queueStop:@"end monochrome early-rejection fixture"];
    }

    FakeOrbEngine *engine = MonochromeSessionEngine(390);
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:MonochromeSessionSettings(false)] && Preview(session,engine) &&
        [session.snapshot[@"canExecute"] boolValue] &&
        !session.snapshot[@"searchTrace"][@"monochromeTemplate"],
        "the same monochrome board still permits an ordinary Combo preview outside the exact shape pair");
    const NSUInteger oldGeneration = [session.snapshot[@"generation"] unsignedIntegerValue];
    Check([session configureSettings:MonochromeSessionSettings(true)],
        "changing from a normal preview to the reported shape pair is accepted");
    [session queueExecute:oldGeneration];
    Check(![session.snapshot[@"canExecute"] boolValue] && [session.snapshot[@"path"] isEqual:@[]] && !engine->stages,
        "shape settings invalidate the prior ordinary route and its execute generation");
    NSDictionary *failed = MonochromeSessionRequest(session,engine,true);
    CheckMonochromeSessionFailure(failed,engine,true,390);
    NSDictionary *retained = failed[@"lastFailedSearch"];
    Check([session configureSettings:MonochromeSessionSettings(false)] &&
        [session.snapshot[@"phase"] isEqual:@"idle"] && [session.snapshot[@"lastFailedSearch"] isEqual:retained],
        "settings reload clears the stopped once intent while preserving the geometric failure receipt");
    Check(Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue] &&
        !session.snapshot[@"searchTrace"][@"monochromeTemplate"] &&
        [session.snapshot[@"lastFailedSearch"] isEqual:retained],
        "a later successful ordinary preview has fresh diagnostics and does not erase the last failure");

    Check([session configureSettings:MonochromeSessionSettings(true)] && [session configureQuality:@"fast"],
        "changed-board regression restores the shape goals for a fresh request");
    engine->live.board.types[0] = 3; // The fixed orbs are no longer monochrome.
    Tick(session,engine,Frame()); [session queueRead];
    const bool started = UntilSolveStarts(session,engine);
    NSDictionary *changed = session.snapshot[@"searchTrace"];
    Check(started && [changed[@"solverStarted"] isEqual:@YES] && !changed[@"monochromeTemplate"] &&
        [changed[@"board"][@"types"][0] isEqual:@3] && [session.snapshot[@"lastFailedSearch"] isEqual:retained],
        "a changed non-monochrome board enters normal search without inheriting the previous seven-take bound");
    [session queueStop:@"end changed-board monochrome fixture"];
    for (unsigned i = 0; i < 100; ++i) Tick(session,engine,Frame());
    Check(!engine->stages && !engine->releases && [session.snapshot[@"path"] isEqual:@[]],
        "cancelled changed-board search cannot publish or execute a stale route");
}
