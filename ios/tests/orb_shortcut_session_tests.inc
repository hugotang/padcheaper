static void ShortcutSessionTests() {
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame());
        NSDictionary *rule = PHOrbDefaultSkillRule();
        NSMutableDictionary *enabled = [rule mutableCopy]; enabled[@"enabled"] = @YES;
        Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[enabled]}],
              "one-shot shortcut uses the saved quality/goals while keeping skill rules for continuous mode");
        [session queueOnce]; NSUInteger generation = [session.snapshot[@"generation"] unsignedIntegerValue];
        [session queueOnce]; [session queueAutomatic];
        Check([session.snapshot[@"once"] boolValue] && ![session.snapshot[@"automatic"] boolValue] &&
              [session.snapshot[@"busy"] boolValue] && [session.snapshot[@"generation"] unsignedIntegerValue] == generation &&
              ![session configureQuality:@"thorough"], "duplicate taps and configuration edits cannot replace an active one-shot intent");
        for (unsigned tick = 0; tick < 700 && ![session.snapshot[@"phase"] isEqual:@"complete"]; ++tick) Tick(session, engine, Frame());
        Check([session.snapshot[@"completed"] isEqual:@1] && ![session.snapshot[@"once"] boolValue] &&
              engine->beginFrames == 1 && engine->releases == 1 && engine->skillAttempts == 0,
              "one click reads, solves and executes exactly one route without invoking automatic skills");
        engine->live.phase = 2; engine->live.moves = 0;
        for (unsigned tick = 0; tick < 30; ++tick) Tick(session, engine, Frame());
        Check(engine->beginFrames == 1 && ![session.snapshot[@"busy"] boolValue], "the one-shot shortcut never starts the next turn");
    }
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueOnce];
        [session queueStop:@"shortcut cancelled"];
        for (unsigned tick = 0; tick < 40; ++tick) Tick(session, engine, Frame());
        Check(!engine->stages && ![session.snapshot[@"once"] boolValue], "stop cancels one-shot reading before any input");
        [session queueOnce]; PHBattleFrame disabled = Frame(); disabled.enabled = false;
        Tick(session, engine, disabled);
        Check(!engine->stages && ![session.snapshot[@"busy"] boolValue], "master-off cancels a pending one-shot shortcut");
    }
    for (unsigned failure = 0; failure < 3; ++failure) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame());
        if (failure == 0) {
            for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = 0;
            Check([session configureSettings:@{@"quality":@"fast", @"mode":@"manual", @"rules":@[],
                @"goals":@[@{@"kind":@"four",@"colors":@32,@"count":@1,@"length":@0}]}], "shortcut unmet-goal fixture is valid");
        }
        if (failure == 1) engine->failRead = YES;
        if (failure == 2) engine->live.multiplayerDisabled = true;
        [session queueOnce];
        for (unsigned tick = 0; tick < 300 && [session.snapshot[@"once"] boolValue]; ++tick) Tick(session, engine, Frame());
        Check(!engine->stages && ![session.snapshot[@"once"] boolValue], "unmet goals, unavailable board and multiplayer cancel the shortcut without input");
    }
}
