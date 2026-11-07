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

// Fishing: reds at 0 and 1 need a third red that only the spinner at 8 can give.
// The gesture holds next to the cell until red shows with frames left.
static FakeOrbEngine *FishEngine(uint16_t colors = 63) {
    FakeOrbEngine *engine = [FakeOrbEngine new];
    auto &board = engine->live.board; const unsigned w = board.width, n = w * board.height;
    for (unsigned i = 0; i < n; ++i) { board.types[i] = uint8_t(1 + (i % w + 2 * (i / w)) % 4); board.flags[i] = 0; }
    board.types[0] = board.types[1] = 0; board.types[w + 2] = 1;
    board.spinnerMask = UINT64_C(1) << (w + 2); board.spinnerColors = colors;
    engine->live.spinnerTurns = 4; engine->live.spinnerPeriodFrames = 30; engine->live.fps = 60;
    engine->live.remainingFrames = 1200; engine->live.spinnerCountdownKnown = true; engine->live.spinnerCountdown = 30;
    engine->live.orbMotionKnown = true; // This fixture explicitly supplies stable source motion.
    return engine;
}
static PHOrbSession *FishSession(FakeOrbEngine *engine) {
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"rules":@[],
        @"goals":@[@{@"kind":@"match",@"colors":@1,@"count":@1,@"length":@3}]}], "fishing goal fixture");
    return session;
}
static void FishSessionTests() {
    {
        FakeOrbEngine *engine = FishEngine(); auto &board = engine->live.board;
        const uint8_t types[] = {1,3,3,5,5,3, 3,3,5,0,3,5, 4,5,0,3,3,3,
                                 5,3,0,5,3,5, 4,1,5,2,5,5};
        std::copy(std::begin(types), std::end(types), board.types);
        board.spinnerMask = UINT64_C(1) << 6;
        engine->live.fps = 30; engine->live.spinnerPeriodFrames = engine->live.spinnerCountdown = 15;
        engine->live.remainingFrames = 570;
        engine->live.enemyCombo.turns[0] = 3; engine->live.enemyCombo.thresholds[0] = 14;
        engine->live.automation.enemyEffects = PHOrbEnemyComboAbsorb;
        auto &bonus = engine->live.enemyCombo.bonus;
        bonus.valid = true; bonus.count = 2; bonus.attackColors = bonus.awakeningColors[4] = 8;
        bonus.effects[0] = PHOrbComboEffect{16,2,PHOrbComboAwakenings,0,1,{}};
        bonus.effects[1] = PHOrbComboEffect{1,4,PHOrbComboGroups,1,2,{3,5,255,255,255}};
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:@{@"quality":@"thorough",@"mode":@"manual",@"diagonal":@YES,@"rules":@[],
            @"goals":@[@{@"kind":@"t",@"colors":@8,@"count":@1},
                @{@"kind":@"four",@"colors":@32,@"count":@1}]}], "recorded final-board goals accepted");
        Tick(session,engine,Frame()); [session queueAutomatic];
        for (unsigned i = 0; i < 2000 && ![session.snapshot[@"phase"] isEqual:@"stopped"] &&
            ![session.snapshot[@"phase"] isEqual:@"arming"] && ![session.snapshot[@"phase"] isEqual:@"fault"]; ++i)
            Tick(session,engine,Frame());
        NSDictionary *state = session.snapshot, *trace = state[@"searchTrace"], *plan = trace[@"fishingPlan"];
        NSArray<NSDictionary *> *choices = plan[@"colors"];
        NSArray<NSDictionary *> *attempts = trace[@"candidateAttempts"];
        CheckSearchAttemptTrace(trace);
        Check(attempts.count && [attempts.firstObject[@"status"] isEqual:@"insufficient-orbs"] &&
            [attempts.firstObject[@"evaluated"] isEqual:@0] && [attempts.firstObject[@"searchPasses"] isEqual:@0] &&
            choices.count == 6 && [plan[@"selectedCount"] isEqual:@0] &&
            [plan[@"scope"] isEqual:@"one-exchange-all-phases"] &&
            [state[@"goalPlan"][@"enemyComboBeforeBonus"] isEqual:@15] &&
            [state[@"goalPlan"][@"enemyComboRequired"] isEqual:@9],
            "all six single-take colors retain their preflight bound without weakening the first-wave nine-Combo goal");
        const unsigned upper[] = {7,8,7,7,8,8}; unsigned seen = 0;
        for (NSDictionary *choice in choices) {
            const unsigned color = [choice[@"color"] unsignedIntValue];
            Check(color < 6, "preflight color stays in the basic spinner table");
            if (color >= 6) continue;
            seen |= 1u << color;
            Check([choice[@"reason"] isEqual:@"inventory"] && ![choice[@"selected"] boolValue] &&
                [choice[@"comboUpper"] unsignedIntValue] == upper[color] && [choice[@"required"] isEqual:@9],
                "each rejected fishing color retains its own inventory explanation");
        }
        Check(seen == 63 && [state[@"goalPlan"][@"enemyComboRequired"] isEqual:@9],
            "a later multi-take attempt cannot reinterpret the single-exchange bound as permission to lower nine Combo");
        if ([state[@"phase"] isEqual:@"arming"]) Check(
            [trace[@"multiFishingSelected"] boolValue] && [trace[@"evaluation"][@"goalsMet"] boolValue] &&
            !engine->stages && !engine->releases,
            "only a checked complete multi-take route may pass the old bound; this inventory case stops before gesture timing");
        else Check([state[@"phase"] isEqual:@"stopped"] && !engine->stages && !engine->releases && !engine->allowedSpinnerCell,
            "unsolved expanded fishing search submits no partial gesture");
        [session queueStop:@"single inventory preflight fixture complete"]; Tick(session,engine,Frame());
    }
    // A Combo-only goal has no color mask. With only 28 -> 29 reachable,
    // taking fire at 29 completes 26,27,28 in every release phase.
    {
        FakeOrbEngine *engine = FishEngine(); auto &board = engine->live.board;
        for (unsigned i = 0; i < 30; ++i) {
            board.types[i] = uint8_t(1 + (i % 6 + 2 * (i / 6)) % 4);
            board.flags[i] = i < 28 ? PHOrbFlagTape : 0;
        }
        board.types[26] = board.types[27] = 0; board.types[29] = 1;
        board.fixedStart = 29; board.tapeMask = (UINT64_C(1) << 28) - 1;
        board.spinnerMask = UINT64_C(1) << 29;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"rules":@[],
            @"goals":@[@{@"kind":@"combo",@"colors":@0,@"count":@1}]}], "pure Combo fishing goal accepted");
        Check(Preview(session,engine), "Combo-only fallback reaches a completed preview");
        NSDictionary *state = session.snapshot, *trace = state[@"searchTrace"];
        CheckSearchAttemptTrace(trace);
        Check([trace[@"fishing"] boolValue] && [trace[@"fishColor"] isEqual:@0] &&
            [trace[@"path"] isEqual:@[@28,@29]] && [state[@"canExecute"] boolValue] &&
            [trace[@"fishingPlan"][@"colors"] count] == 6 && !engine->stages,
            "pure Combo considers a third fire orb without needing a shape color mask or executing the preview");
        [session queueStop:@"end Combo fishing fixture"]; Tick(session,engine,Frame());
    }
    // Only 28 -> 29 is reachable. The spinner can supply a third red, but it
    // stays in column 4, away from the two reds in columns 0 and 1. Water has
    // an existing three. This guarantees a partial fishing result without
    // relying on a machine-specific timeout or a difficult search puzzle.
    for (unsigned mode = 0; mode < 3; ++mode) {
        FakeOrbEngine *engine = FishEngine(); auto &board = engine->live.board;
        for (unsigned i = 0; i < 30; ++i) {
            board.types[i] = uint8_t(2 + (i % 6 + 2 * (i / 6)) % 4);
            board.flags[i] = i < 28 ? PHOrbFlagTape : 0;
        }
        board.types[0] = board.types[1] = 0;
        board.types[12] = board.types[13] = board.types[14] = 1;
        board.fixedStart = 29; board.tapeMask = (UINT64_C(1) << 28) - 1;
        board.spinnerMask = UINT64_C(1) << 29;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"rules":@[],
            @"goals":@[@{@"kind":@"match",@"colors":@1,@"count":@1,@"length":@3},
                @{@"kind":@"match",@"colors":@2,@"count":@1,@"length":@3}]}],
            "constrained partial fishing fixture accepts its two goals");
        Tick(session,engine,Frame());
        if (mode == 0) [session queueRead];
        else if (mode == 1) [session queueOnce];
        else [session queueAutomatic];
        NSString *expectedPhase = mode == 0 ? @"preview" : @"stopped";
        for (unsigned i = 0; i < 2000 && ![session.snapshot[@"phase"] isEqual:expectedPhase] &&
            ![session.snapshot[@"phase"] isEqual:@"fault"] && ![session.snapshot[@"phase"] isEqual:@"blocked"]; ++i)
            Tick(session,engine,Frame());
        NSDictionary *state = session.snapshot, *trace = state[@"searchTrace"];
        NSArray<NSDictionary *> *attempts = trace[@"candidateAttempts"];
        const NSUInteger selected = [trace[@"selectedAttemptIndex"] unsignedIntegerValue];
        CheckSearchAttemptTrace(trace);
        NSArray<NSDictionary *> *colors = trace[@"fishingPlan"][@"colors"];
        Check([state[@"phase"] isEqual:expectedPhase] && attempts.count >= 2 && selected == 1 &&
            [trace[@"selectedStatus"] isEqual:@"goals-unmet"] && [trace[@"status"] isEqual:@"goals-unmet"] &&
            [attempts.firstObject[@"status"] isEqual:@"insufficient-orbs"] &&
            colors.count == 2 && [colors.lastObject[@"color"] isEqual:@1] &&
            [colors.lastObject[@"reason"] isEqual:@"inventory"] && ![colors.lastObject[@"selected"] boolValue],
            "an evaluated fishing failure replaces the original shortage while an impossible later color is preflight-only");
        if (selected < attempts.count) {
            NSDictionary *fish = attempts[selected];
            Check([fish[@"fishingRequested"] boolValue] && [fish[@"fishing"] boolValue] &&
                [fish[@"fishCell"] isEqual:@29] && [fish[@"fishColor"] isEqual:@0] &&
                [fish[@"path"] isEqual:@[@28,@29]] && [fish[@"goalHits"] isEqual:@[@0,@1]] &&
                [fish[@"matchedGoals"] isEqual:@1] && [fish[@"goalCount"] isEqual:@2] &&
                ![fish[@"goalsMet"] boolValue] && [fish[@"hazardsSafe"] boolValue] &&
                ![fish[@"deadline"] boolValue] && ![fish[@"soloRetry"] boolValue],
                "completed fishing failure preserves the unmet red goal and met water goal without an unnecessary retry");
        }
        Check([trace[@"reason"] containsString:@"未達成"] && [trace[@"reason"] containsString:@"1／2"] &&
            [trace[@"reason"] containsString:@"撈火珠搜尋"] &&
            ![trace[@"reason"] containsString:@"未開始轉珠；請調整目標"] &&
            ![state[@"canExecute"] boolValue] && !engine->stages && !engine->allowedSpinnerCell && !engine->releases,
            "partial fishing diagnostics name the real goal failure and never arm or execute the route");
        Execute(session); Tick(session,engine,Frame());
        Check(!engine->stages && !engine->allowedSpinnerCell && !engine->releases,
            "an explicit execute request cannot turn a failed fishing preview into input");
        [session queueStop:@"end partial fishing fixture"]; Tick(session,engine,Frame());
    }
    // Same cause as uryu_t_lottery_test: optional shortage straddles the
    // spinner cycle. Auto must reach the existing fishing path without casting.
    for (bool required : {false,true}) {
        FakeOrbEngine *engine = FishEngine(); PHOrbSession *session = FishSession(engine);
        NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy];
        rule[@"color"] = @0; rule[@"below"] = @3; rule[@"required"] = @(required);
        Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"rules":@[rule],
            @"goals":@[@{@"kind":@"match",@"colors":@1,@"count":@1,@"length":@3}]}],"Auto fishing shortage fixture");
        Tick(session,engine,Frame()); [session queueAutomatic];
        if (required) {
            Check(UntilPhase(session,engine,@"stopped") && !engine->stages && engine->skillOrder.empty(),
                "required spinner ambiguity does not cast or start an orb gesture");
        } else {
            Check(UntilPhase(session,engine,@"arming") && [session.snapshot[@"searchTrace"][@"fishing"] boolValue] &&
                engine->skillOrder.empty(),"optional spinner ambiguity reaches Auto fishing without casting");
            engine->live.board.types[engine->live.board.width + 2] = 0;
            Check(UntilPhase(session,engine,@"turnWaiting") && engine->releases == 1 && engine->skillOrder.empty(),
                "Auto takes the missing orb and completes a verified release");
        }
        [session queueStop:@"Auto fishing fixture complete"]; Tick(session,engine,Frame());
    }
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        FakeOrbEngine *engine = FishEngine(); PHOrbSession *session = FishSession(engine);
        const unsigned fish = engine->live.board.width + 2;
        Check(Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue] &&
            [session.snapshot[@"searchTrace"][@"fishing"] boolValue] && [session.snapshot[@"searchTrace"][@"fishColor"] isEqual:@0],
            "a colour only the spinner can supply yields a fishing route");
        NSDictionary *fishTrace = session.snapshot[@"searchTrace"];
        CheckSearchAttemptTrace(fishTrace);
        NSArray<NSDictionary *> *fishAttempts = fishTrace[@"candidateAttempts"];
        const NSUInteger selectedFish = [fishTrace[@"selectedAttemptIndex"] unsignedIntegerValue];
        Check(selectedFish < fishAttempts.count && [fishAttempts[selectedFish][@"fishing"] boolValue] &&
            [fishAttempts[selectedFish][@"fishColor"] isEqual:@0],
            "selected-attempt diagnostics follow the fishing fallback without changing its eligibility");
        NSArray<NSNumber *> *path = session.snapshot[@"path"];
        const NSUInteger entry = [path indexOfObject:@(fish)], steps = path.count ? path.count - 1 : 0;
        NSUInteger entries = 0;
        for (NSNumber *step in path) entries += step.unsignedIntegerValue == fish;
        Check(entry != NSNotFound && entry >= 1 && entries == 1, "the fishing route enters the spinner cell once, from a neighbour");
        if (entry == NSNotFound || !entry) continue;
        Execute(session);
        Check(engine->allowedSpinnerCell == (UINT64_C(1) << fish), "execution opens only the route's spinner cell to input");
        Check([session.snapshot[@"executionLimitMS"] isEqual:@7000], "fishing diagnostics include the reserved colour cycle in the execution limit");
        // Reach the neighbour, then hold. The limit is 4 s plus one 3 s cycle
        // (6 colours x 30 frames at 60 fps); a 24-step route needs about 2 s.
        for (unsigned i = 0; i < 300 && !(engine->live.dragged && engine->live.moves + 1 >= entry); ++i)
            Tick(session,engine,Frame());
        for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
        Check(engine->live.dragged && engine->live.moves + 1 == entry && [[session snapshot][@"busy"] boolValue],
            "the gesture holds beside the cell while another colour shows");
        if (scenario == 1) {
            for (unsigned i = 0; i < 400 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),0.05);
            Check(![[session snapshot][@"busy"] boolValue] && engine->live.moves + 1 == entry && !engine->live.dragged &&
                ([[session snapshot][@"message"] containsString:@"剩餘時間不足"] ||
                 [[session snapshot][@"message"] containsString:@"7.0 秒上限"]) && !engine->allowedSpinnerCell,
                "a color that never shows stops before its required suffix exceeds the extended limit without entering");
            continue;
        }
        // The session needs at least 6 frames left in the colour (FishMinCountdown).
        engine->live.board.types[fish] = 0; engine->live.spinnerCountdown = 5;
        for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
        Check(engine->live.moves + 1 == entry, "too few frames left in the colour keeps holding");
        engine->live.spinnerCountdown = 6;
        for (unsigned i = 0; i < 400 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
        Check([[session snapshot][@"phase"] isEqual:@"complete"] && engine->live.moves == steps && engine->releases == 1 &&
            !engine->allowedSpinnerCell, "the route takes the colour out, finishes, passes the release check and closes the cell");
    }
    // A table without the needed colour cannot be fished.
    FakeOrbEngine *engine = FishEngine(6); PHOrbSession *session = FishSession(engine);
    Tick(session,engine,Frame()); [session queueRead];
    for (unsigned i = 0; i < 2000 && ![session.snapshot[@"phase"] isEqual:@"stopped"]; ++i) Tick(session,engine,Frame());
    NSArray *attempts = session.snapshot[@"searchTrace"][@"candidateAttempts"];
    CheckSearchAttemptTrace(session.snapshot[@"searchTrace"]);
    Check([session.snapshot[@"phase"] isEqual:@"stopped"] && ![session.snapshot[@"canExecute"] boolValue] && !engine->stages &&
        attempts.count && ![[attempts valueForKey:@"fishColor"] containsObject:@0],
        "a spinner table without the wanted colour never attempts to fish it");
}
static void OrbEffectSessionTests() {
    FishSessionTests();
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
