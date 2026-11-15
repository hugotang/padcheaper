static void ThornFishingSessionTests() {
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        FakeOrbEngine *engine = MultiFishingSessionEngine();
        engine->live.board.flags[8] |= PHOrbFlagThorn; engine->live.board.thornDamage[8] = 4;
        engine->live.thorns = {81,1000,0,true};
        PHOrbSession *session = MultiFishingSession(engine);
        Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"diagonal":@YES,@"rules":@[],
            @"allowThornFishing":@(scenario != 1), @"allowThornsWithHeartMatch":@YES,
            @"goals":@[@{@"kind":@"four",@"colors":@32,@"count":@1}]}], "thorn fishing option is a separate saved policy");
        if (scenario == 1) {
            Preview(session,engine);
            Check(![session.snapshot[@"canExecute"] boolValue] && !engine->stages &&
                [session.snapshot[@"searchTrace"][@"multiFishingEligibilityReason"] isEqual:@"locked-or-thorn-token"],
                "disabled thorn fishing keeps the old no-take behavior");
            continue;
        }
        if (!MultiFishingSessionPreview(session,engine)) continue;
        Check([session.snapshot[@"thornDamage"] isEqual:@80] && [session.snapshot[@"thornContacts"] isEqual:@2] &&
            [session.snapshot[@"searchTrace"][@"allowThornFishing"] isEqual:@YES],
            "multi-take Session publishes verified thorn damage and the effective option");
        if (scenario == 2) {
            Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"diagonal":@YES,@"rules":@[],
                @"goals":@[@{@"kind":@"four",@"colors":@32,@"count":@1}]}] &&
                ![session.snapshot[@"canExecute"] boolValue] && [session.snapshot[@"allowThornFishing"] isEqual:@NO],
                "switching to legacy settings revokes the thorn fishing preview");
            continue;
        }
        Execute(session);
        bool changed = false, waited = false;
        unsigned movesAtChange = 0;
        for (unsigned frame = 0; frame < 1000 && [session.snapshot[@"busy"] boolValue]; ++frame) {
            const auto before = engine->live;
            if (scenario >= 3 && !changed && engine->live.moves == 2 && engine->live.dragged) {
                changed = true; movesAtChange = engine->live.moves;
                if (scenario == 3) engine->live.thorns.hp = 80;
                if (scenario == 4) engine->live.thorns.pendingDamage = 41;
                if (scenario == 5) engine->live.thorns.maximumHP = 2000;
                if (scenario == 6) engine->live.thorns.known = false;
                if (scenario == 7) engine->live.board.thornDamage[14] = 5;
            }
            Tick(session,engine,Frame(),1.0 / 60);
            if (!changed && before.dragged && before.moves == engine->live.moves && before.phase == 2 && engine->live.phase == 2) {
                waited = true;
                Check(before.thorns.pendingDamage == engine->live.thorns.pendingDamage,
                    "native-style stationary holds never repeat thorn contact damage");
            }
        }
        if (!scenario) {
            Check(waited && [session.snapshot[@"phase"] isEqual:@"complete"] && engine->live.moves == 4 &&
                engine->live.thorns.pendingDamage == 80 && engine->releases == 1 &&
                [session.snapshot[@"lastFishingSuccess"][@"releaseConfirmed"] boolValue],
                "affordable thorn fishing confirms both takes and a single release with heart recovery");
        } else Check(changed && engine->live.moves == movesAtChange && engine->releases == 1 &&
            !engine->live.dragged && ![session.snapshot[@"phase"] isEqual:@"complete"],
            "changed HP, pending cost, maximum, unknown HP or thorn metadata revokes waiting before another exchange");
    }
    for (bool continuation : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(true); SetMultiplayer(engine,3,1);
        if (continuation) SetSessionContinuation(engine,1);
        engine->live.board.flags[8] |= PHOrbFlagThorn; engine->live.board.thornDamage[8] = 4;
        engine->live.thorns = {1000,1000,0,true};
        PHOrbSession *session = MultiFishingSession(engine);
        Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"diagonal":@YES,@"rules":@[],
            @"allowThornFishing":@YES,@"allowThornsWithHeartMatch":@YES,
            @"goals":@[@{@"kind":@"four",@"colors":@32,@"count":@1}]}], "saved solo option can exist on a multiplayer team");
        FishingIntent(session,engine);
        Check([session.snapshot[@"allowThornFishing"] isEqual:@YES] &&
            [session.snapshot[@"searchTrace"][@"allowThornFishing"] isEqual:@NO] &&
            [session.snapshot[@"searchTrace"][@"multiFishingEligibilityReason"] isEqual:@"locked-or-thorn-token"] && !engine->stages,
            "3P and ThreeSolo never turn a saved single-player policy into thorn fishing permission");
        [session queueStop:@"thorn scope fixture complete"]; Tick(session,engine,Frame());
    }
    FakeOrbEngine *engine = MultiFishingSessionEngine();
    engine->live.board.flags[8] |= PHOrbFlagThorn; engine->live.board.thornDamage[8] = 4;
    engine->live.thorns = {81,1000,0,true};
    PHOrbSession *session = MultiFishingSession(engine);
    Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"diagonal":@YES,@"rules":@[],
        @"allowThornFishing":@YES,@"goals":@[@{@"kind":@"four",@"colors":@32,@"count":@1}]}],
        "ordinary single-player Auto can use the saved thorn fishing option");
    FishingIntent(session,engine,true);
    for (unsigned i = 0; i < 1000 && !engine->releases && [session.snapshot[@"busy"] boolValue]; ++i)
        Tick(session,engine,Frame(),1.0 / 60);
    Check(engine->releases == 1 && engine->live.thorns.pendingDamage == 80 &&
        [session.snapshot[@"lastFishingSuccess"][@"releaseConfirmed"] boolValue],
        "solo Auto completes affordable thorn fishing through the same public execution path");
    [session queueStop:@"thorn auto fixture complete"]; Tick(session,engine,Frame());
}
