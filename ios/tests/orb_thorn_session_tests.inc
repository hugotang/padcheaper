static void SetThornRoute(FakeOrbEngine *engine) {
    auto &b = engine->live.board; unsigned n = b.width * b.height;
    for (unsigned i = 0; i < n; ++i) {
        b.types[i] = uint8_t(1 + (i % b.width + 2 * (i / b.width)) % 4);
        b.flags[i] = i < n - 2 ? PHOrbFlagTape : 0;
    }
    for (unsigned i = 0; i < 4; ++i) b.types[i] = 5;
    b.tapeMask = (UINT64_C(1) << (n-2)) - 1; b.fixedStart = uint8_t(n-1);
    b.flags[n-1] = PHOrbFlagThorn; b.thornDamage[n-1] = 4;
    engine->live.thorns = {1000,1000,0,true};
}
static void ThornSessionTests() {
    for (bool heartMatch : {false, true}) for (bool priority : {false, true}) for (unsigned scenario = 0; scenario < 7; ++scenario) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetThornRoute(engine);
        if (heartMatch) engine->live.board.types[3] = 4;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[],
            @"thornComboPriority":@(priority), @"allowThornsWithHeartMatch":@(heartMatch)}] && [session configureQuality:@"fast"],
            "changing quality preserves the selected thorn preference");
        Check(Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue] &&
              [session.snapshot[@"thornContacts"] isEqual:@1] && [session.snapshot[@"heartFours"] isEqual:@(!heartMatch)] &&
              [session.snapshot[@"heartMatches"] isEqual:@1],
              "thorn route has a verified one-contact preview with first-wave recovery hearts");
        Check([session.snapshot[@"thornComboPriority"] boolValue] == priority &&
            [session.snapshot[@"searchTrace"][@"thornComboPriority"] boolValue] == priority,
            "each pending route records the actual thorn search preference");
        NSDictionary *budget = session.snapshot[@"searchTrace"][@"thorns"];
        Check([session.snapshot[@"allowThornsWithHeartMatch"] boolValue] == heartMatch &&
            [session.snapshot[@"searchTrace"][@"allowThornsWithHeartMatch"] boolValue] == heartMatch &&
            [budget[@"hp"] isEqual:@1000] && [budget[@"maximumHP"] isEqual:@1000] &&
            [budget[@"pendingDamage"] isEqual:@0] && [budget[@"maximumAdditionalDamage"] isEqual:@999] &&
            [budget[@"recoveryPolicy"] isEqual:heartMatch ? @"first-wave-heart-match" : @"first-wave-heart-four"],
            "manual previews capture their effective HP budget and recovery policy independently of automation reads");
        if (scenario == 1) { engine->live.thorns.hp = 40; Tick(session,engine,Frame()); }
        if (scenario == 2) { engine->live.thorns.pendingDamage = 960; Tick(session,engine,Frame()); }
        if (scenario == 3) { engine->live.thorns.known = false; Tick(session,engine,Frame()); }
        Execute(session);
        if (scenario == 4) engine->live.thorns.hp = 40;
        if (scenario == 5) engine->live.thorns.maximumHP = 30000;
        if (scenario == 6) { UntilDragging(session,engine); engine->live.thorns.hp = 40; }
        for (unsigned i = 0; i < 80 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
        if (!scenario) Check([session.snapshot[@"phase"] isEqual:@"complete"] && engine->live.moves == 1 &&
              engine->live.thorns.pendingDamage == 40 && engine->releases == 1,
              "legitimate pending thorn damage does not invalidate acknowledged input or cause a duplicate contact");
        else Check(!engine->live.moves && ![session.snapshot[@"completed"] unsignedIntValue] &&
              (!engine->stages || (scenario == 6 && engine->releases == 1)),
              "changed HP, cap, pending damage or unknown budget cancels before damaging input and releases owned orbs");
    }
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetThornRoute(engine); engine->live.board.types[3] = 4;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    NSDictionary *enabled = @{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[], @"allowThornsWithHeartMatch":@YES};
    Check([session configureSettings:enabled] && Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue],
        "heart-triple preview fixture");
    Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[]}] &&
        ![session.snapshot[@"canExecute"] boolValue] && [session.snapshot[@"allowThornsWithHeartMatch"] isEqual:@NO],
        "changing recovery policy invalidates the previous preview and legacy settings restore the strict default");
    Check([session configureSettings:enabled], "one-shot heart policy fixture");
    Tick(session,engine,Frame()); [session queueOnce];
    for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
    Check([session.snapshot[@"phase"] isEqual:@"complete"] && engine->releases == 1,
        "manual one-shot can execute the opted-in heart-triple thorn route");
    NSDictionary *record = [session.snapshot[@"executionHistory"] lastObject];
    Check([record[@"search"][@"allowThornsWithHeartMatch"] isEqual:@YES] &&
        [record[@"releaseEvaluation"][@"heartMatches"] isEqual:@1] &&
        [record[@"releaseEvaluation"][@"heartFours"] isEqual:@0],
        "release independently verifies the real first-wave heart triple and records the policy used");
    engine->live.thorns = {};
    Tick(session,engine,Frame());
    Check([session.snapshot[@"searchTrace"][@"thorns"][@"hp"] isEqual:@1000] &&
        [record[@"search"][@"thorns"][@"maximumAdditionalDamage"] isEqual:@999],
        "clearing live HP after the gesture does not erase the historical search budget");
}
