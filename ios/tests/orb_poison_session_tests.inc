static void SetPoisonHeartBoard(FakeOrbEngine *engine, unsigned poison = PHOrbPoison, bool hearts = true) {
    SetEffectBoard(engine, false);
    for (unsigned i = 0; i < 3; ++i) engine->live.board.types[i] = uint8_t(poison);
    if (hearts) for (unsigned i = 6; i < 10; ++i) engine->live.board.types[i] = 5;
}

static void PoisonHeartFourSessionTests() {
    // The sealed board permits one harmless final swap, so eligibility and
    // release checks are independent of beam quality or a fortunate deadline.
    for (bool enabled : {false, true}) for (bool hearts : {false, true})
        for (unsigned poison : {unsigned(PHOrbPoison), unsigned(PHOrbMortalPoison)}) {
            FakeOrbEngine *engine = [FakeOrbEngine new]; SetPoisonHeartBoard(engine, poison, hearts);
            PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
            Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[],
                @"allowPoisonWithHeartFour":@(enabled)}] && [session configureQuality:@"fast"],
                "changing quality preserves the captured poison policy");
            const bool allowed = enabled && hearts;
            Check(Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue] == allowed &&
                [session.snapshot[@"hazardsSafe"] boolValue] == allowed &&
                [session.snapshot[@"allowPoisonWithHeartFour"] boolValue] == enabled &&
                [session.snapshot[@"searchTrace"][@"allowPoisonWithHeartFour"] boolValue] == enabled,
                "preview eligibility for either poison requires explicit permission and first-wave hearts");
            Execute(session);
            for (unsigned i = 0; i < 150 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
            if (allowed) {
                NSDictionary *record = [session.snapshot[@"executionHistory"] lastObject];
                Check([session.snapshot[@"phase"] isEqual:@"complete"] && engine->releases == 1 &&
                    [record[@"releaseConfirmed"] boolValue] &&
                    [record[@"search"][@"allowPoisonWithHeartFour"] isEqual:@YES] &&
                    [record[@"releaseEvaluation"][@"poisonErased"] isEqual:@3] &&
                    [record[@"releaseEvaluation"][@"mortalPoisonErased"] unsignedIntValue] == (poison == PHOrbMortalPoison ? 3u : 0u) &&
                    [record[@"releaseEvaluation"][@"heartFours"] isEqual:@1] &&
                    [record[@"releaseEvaluation"][@"hazardsSafe"] boolValue],
                    "fresh release validation uses the same poison/mortal policy as search and replay");
            } else Check(!engine->stages && !engine->releases,
                "missing hearts and strict mode cannot stage input for either poison");
        }
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetPoisonHeartBoard(engine);
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    NSDictionary *enabled = @{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[], @"allowPoisonWithHeartFour":@YES};
    Check([session configureSettings:enabled] && Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue],
        "accepted poison preview is prepared before testing settings invalidation");
    Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[]}] &&
        ![session.snapshot[@"canExecute"] boolValue] && [session.snapshot[@"allowPoisonWithHeartFour"] isEqual:@NO],
        "revoking poison permission invalidates the old preview before any input");
    Execute(session); Tick(session,engine,Frame()); Check(!engine->stages, "an invalidated poison preview cannot execute");
    engine = [FakeOrbEngine new]; SetPoisonHeartBoard(engine);
    session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:enabled] && Preview(session,engine), "poison route preflight mutation fixture");
    engine->live.board.types[9] = 1; Execute(session);
    for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
    Check(!engine->stages && !engine->releases, "lost hearts cancel the stale preview before starting");

    engine = [FakeOrbEngine new]; SetEffectBoard(engine, false); engine->live.board.types[0] = PHOrbPoison;
    session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"fast", @"mode":@"manual", @"rules":@[],
        @"goals":@[@{@"kind":@"t", @"colors":@8, @"count":@1, @"length":@0}]}] && Preview(session,engine) &&
        ![session.snapshot[@"goalsMet"] boolValue] && [session.snapshot[@"hazardsSafe"] boolValue] &&
        ![session.snapshot[@"message"] containsString:@"可先使用能轉走毒珠"] &&
        ![session.snapshot[@"message"] containsString:@"候選路徑會消除毒"] &&
        [session.snapshot[@"message"] containsString:@"未達成"],
        "an unmet light T with a harmless lone poison reports the missing goal without blaming poison");

    engine = [FakeOrbEngine new]; SetPoisonHeartBoard(engine); session = [[PHOrbSession alloc] initWithEngine:engine];
    engine->live.board.flags[29] = PHOrbFlagThorn; engine->live.board.thornDamage[29] = 4;
    Check([session configureSettings:enabled] && Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue] &&
        [session.snapshot[@"thornContacts"] isEqual:@1],
        "fresh thorn preflight preserves the captured normal-poison permission");
    Execute(session);
    for (unsigned i = 0; i < 150 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
    Check([session.snapshot[@"phase"] isEqual:@"complete"] && engine->live.thorns.pendingDamage == 40 &&
        engine->releases == 1 && [[session.snapshot[@"executionHistory"] lastObject][@"releaseEvaluation"][@"hazardsSafe"] boolValue],
        "a poison-and-thorn route keeps the strict live damage budget while passing its accepted release policy");
    engine = [FakeOrbEngine new]; SetPoisonHeartBoard(engine); session = [[PHOrbSession alloc] initWithEngine:engine];
    engine->live.board.flags[29] = PHOrbFlagThorn; engine->live.board.thornDamage[29] = 4;
    Check([session configureSettings:enabled] && Preview(session,engine), "poison-and-thorn changed HP fixture");
    engine->live.thorns.hp = 40; Execute(session);
    for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
    Check(!engine->stages && !engine->releases, "normal poison permission never spends future heart healing on live thorn damage");
    engine = [FakeOrbEngine new]; SetPoisonHeartBoard(engine); session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:enabled], "automatic accepted poison settings fixture");
    Tick(session,engine,Frame()); [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->releases == 1,
        "continuous single-player turns execute accepted normal poison routes");
    [session queueStop:@"end accepted normal poison turn"];
    engine = [FakeOrbEngine new]; SetPoisonHeartBoard(engine); engine->live.identity.multiplayer = 1;
    session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureSettings:enabled];
    Tick(session,engine,Frame()); [session queueOnce];
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    Check(!engine->stages && !engine->releases, "normal poison permission cannot override the single-player restriction");
}
