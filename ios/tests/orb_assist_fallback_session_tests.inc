static FakeOrbEngine *AssistFallbackEngine() {
    FakeOrbEngine *engine = [FakeOrbEngine new];
    engine->chosenSource[5] = PHOrbSkillAssist; engine->chosenSkill[5] = 61891;
    engine->fallbackBodySkill[5] = 64914; engine->live.automation.members[5].charge = 20;
    engine->chosenSource[4] = PHOrbSkillBody; engine->chosenSkill[4] = 64914;
    return engine;
}
static void AssistFallbackSessionTests() {
    NSMutableDictionary *a = [PHOrbDefaultSkillRule() mutableCopy];
    a[@"slot"] = @6; a[@"source"] = @"body_or_assist"; a[@"skill"] = @64914;
    a[@"group"] = @1; a[@"every"] = @2; a[@"intervalMode"] = @"accepted";
    NSMutableDictionary *b = [a mutableCopy]; b[@"slot"] = @5; b[@"source"] = @"body";
    FakeOrbEngine *engine = AssistFallbackEngine();
    PHOrbSession *session = AutomaticSession(engine,@[a,b]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder == (std::vector<NSUInteger>{5,4}),
        "overcharged helper assist settles before the other Byakuya body can supply the same puzzle");
    NSDictionary *timing = session.snapshot[@"ruleTiming"];
    Check([timing[@"ruleLastUseTurns"][0] unsignedIntegerValue] == 0 &&
        [timing[@"groupLastUseTurns"][0] unsignedIntegerValue] == 1 &&
        [session.snapshot[@"ruleDecisionDetails"][0][@"assistFallback"] boolValue] &&
        [session.snapshot[@"ruleDecisionDetails"][0][@"skillStartReason"] containsString:@"61891"],
        "only the actual body starts the shared interval; fallback source remains in diagnostics");
    [session queueStop:@"end assist grouped fallback"];

    a[@"group"] = @0; a[@"once"] = @YES;
    engine = AssistFallbackEngine(); session = AutomaticSession(engine,@[a]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder == std::vector<NSUInteger>{5} &&
        [session.snapshot[@"ruleTiming"][@"ruleLastUseTurns"][0] unsignedIntegerValue] == 0,
        "confirmed assist does not consume the one-time body or its interval");
    engine->live.automation.members[5].charge = 2; // Native haste/turn charging is supplied by this fixture.
    Check(NextOrbTurn(session,engine) && engine->skillOrder == (std::vector<NSUInteger>{5,5}),
        "next turn can use the body despite the fallback on the previous turn");
    Check(NextOrbTurn(session,engine) && NextOrbTurn(session,engine) && engine->skillOrder.size() == 2,
        "the actual body consumes its one-time receipt normally");
    [session queueStop:@"end assist once fallback"];

    a[@"required"] = @YES; a[@"status"] = @"awoken_bind";
    for (bool cleanses : {false,true}) {
        engine = AssistFallbackEngine(); engine->live.automation.playerEffects = PHOrbPlayerAwokenBind;
        engine->clearPlayerStatusWithSkill = cleanses; session = AutomaticSession(engine,@[a]);
        if (cleanses) {
            Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1,
                "assist that actually removes the required status permits the freshly checked puzzle");
        } else {
            for (unsigned i = 0; i < 180 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
            Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && engine->skillOrder.size() == 1 &&
                [session.snapshot[@"message"] containsString:@"必要規則"],
                "unrelated assist cannot fulfill a required body cleanse or permit dragging");
            [session queueAutomatic];
            for (unsigned i = 0; i < 100; ++i) Tick(session,engine,Frame());
            Check(engine->skillOrder.size() == 1 && !engine->stages,
                "stop/start retains same-turn fallback use and cannot repeat the skill");
        }
        [session queueStop:@"end assist required fallback"];
    }
    a[@"required"] = @NO; a[@"status"] = @"none";
    engine = AssistFallbackEngine(); engine->failSkillAck = YES; session = AutomaticSession(engine,@[a]);
    for (unsigned i = 0; i < 180 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && engine->skillOrder.size() == 1 &&
        [session.snapshot[@"message"] containsString:@"使用次數未確認"],
        "assist fallback still needs a native completion acknowledgement");
    [session queueStop:@"end assist missing acknowledgement"];

    engine = AssistFallbackEngine(); session = AutomaticSession(engine,@[a]);
    Check(UntilPhase(session,engine,@"skillWaiting"), "fallback starts before a user interruption");
    [session queueStop:@"stop during assist animation"];
    for (unsigned i = 0; i < 12; ++i) Tick(session,engine,Frame());
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 &&
        [session.snapshot[@"ruleTiming"][@"ruleLastUseTurns"][0] unsignedIntegerValue] == 0,
        "stopping during assist animation preserves the used slot without fabricating a body receipt");
    engine->live.automation.members[5].charge = 2;
    Check(NextOrbTurn(session,engine) && engine->skillOrder == (std::vector<NSUInteger>{5,5}),
        "the body one-time rule survives an interrupted fallback and resumes next turn");
    [session queueStop:@"end interrupted fallback"];

    a[@"reserveUntil"] = @3;
    engine = AssistFallbackEngine(); session = AutomaticSession(engine,@[a]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty(),
        "assist fallback cannot discharge a slot reserved for a later floor");
    [session queueStop:@"end reserved fallback"];

    a[@"reserveUntil"] = @0; a[@"required"] = @YES; a[@"status"] = @"awoken_bind";
    a[@"group"] = @1; b[@"required"] = @YES; b[@"status"] = @"awoken_bind";
    engine = AssistFallbackEngine(); engine->live.automation.playerEffects = PHOrbPlayerAwokenBind;
    engine->unavailableSkills[4] = YES; session = AutomaticSession(engine,@[a,b]);
    for (unsigned i = 0; i < 180 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && engine->skillOrder == std::vector<NSUInteger>{5} &&
        [session.snapshot[@"ruleTiming"][@"groupLastUseTurns"][0] unsignedIntegerValue] == 0 &&
        [session.snapshot[@"message"] containsString:@"必要規則"],
        "fallback cannot satisfy a required group when its body alternative remains unavailable");
    [session queueStop:@"end required group fallback"];
}
