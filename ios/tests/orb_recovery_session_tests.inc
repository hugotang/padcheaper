static NSDictionary *HeartPolicy(unsigned hp = 40, unsigned groups = 1, NSString *shape = @"match") {
    return @{@"hpAtMost":@(hp), @"groups":@(groups), @"shape":shape};
}
static NSDictionary *HeartSettings(NSDictionary *recovery, NSArray *rules) {
    return @{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":rules, @"recovery":recovery};
}
static void SetHeartBoard(FakeOrbEngine *engine, unsigned hearts) {
    auto &board = engine->live.board;
    for (unsigned i = 0; i < board.width * board.height; ++i) board.types[i] = uint8_t(i % 5);
    for (unsigned i = 0; i < hearts; ++i) board.types[hearts == 6 && i >= 3 ? 2 * board.width + i - 3 : i] = 5;
}
static PHOrbSession *HeartSession(FakeOrbEngine *engine, NSDictionary *settings) {
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:settings], "recovery settings accepted before explicit start");
    Tick(session,engine,Frame()); [session queueAutomatic]; return session;
}
static void RecoverySessionTests() {
    for (unsigned groups = 1; groups <= 2; ++groups) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetHeartBoard(engine,3 * groups); engine->live.automation.hp = 400;
        PHOrbSession *session = HeartSession(engine,HeartSettings(HeartPolicy(40,groups),@[]));
        Check(UntilPhase(session,engine,@"turnWaiting") && [[session snapshot][@"recoveryActive"] boolValue] &&
            [[session snapshot][@"goalCount"] unsignedIntegerValue] == 1 &&
            [[session snapshot][@"goalHits"][0] unsignedIntegerValue] >= groups && [[session snapshot][@"goals"] count] == 0 &&
            [[session snapshot][@"goalPlan"][@"baseGoals"] isEqual:@[]],
            "low-HP route satisfies all requested heart groups without persisting them as unconditional manual goals");
        Check(![session configureSettings:HeartSettings(PHOrbDefaultRecovery(),@[])], "an active run cannot change its captured heart policy");
        if (groups == 1) {
            engine->live.automation.hp = 1000; SetHeartBoard(engine,0);
            Check(NextOrbTurn(session,engine) && ![[session snapshot][@"recoveryActive"] boolValue] &&
                [[session snapshot][@"goalCount"] unsignedIntegerValue] == 0 && [[session snapshot][@"recoveryTurn"] unsignedIntegerValue] == 2,
                "a later healthy turn rebuilds its goals without retaining the previous turn's heart requirement");
        }
        [session queueStop:@"end completed heart route"];
    }
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetHeartBoard(engine,0); engine->live.automation.hp = 400;
    PHOrbSession *session = HeartSession(engine,HeartSettings(HeartPolicy(),@[])); FinishSurvivalAttempt(session,engine);
    Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && [[session snapshot][@"message"] containsString:@"心珠不足"],
        "insufficient hearts stop before the solver or any synthetic input");

    engine = [FakeOrbEngine new]; SetHeartBoard(engine,0); engine->live.automation.hp = 400; engine->healWithSkill = YES;
    session = HeartSession(engine,HeartSettings(HeartPolicy(),@[PHOrbDefaultSkillRule()]));
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 &&
        ![[session snapshot][@"recoveryActive"] boolValue] && [[session snapshot][@"recoveryStatus"] containsString:@"高於"],
        "skill healing removes the low-HP requirement before a board with no hearts is evaluated");
    [session queueStop:@"end skill-healed route"];
    engine = [FakeOrbEngine new]; SetHeartBoard(engine,0); engine->live.automation.hp = 400; engine->makeHeartsWithSkill = YES;
    session = HeartSession(engine,HeartSettings(HeartPolicy(40,1,@"four"),@[PHOrbDefaultSkillRule()]));
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 &&
        [[session snapshot][@"recoveryActive"] boolValue] && [[session snapshot][@"goalPlan"][@"goals"][0][@"kind"] isEqual:@"four"] &&
        [[session snapshot][@"goalHits"][0] unsignedIntegerValue] >= 1,
        "skill-created hearts are read after completion and support a required straight four before dragging");
    [session queueStop:@"end skill-created heart route"];
    engine = [FakeOrbEngine new]; SetHeartBoard(engine,3); engine->live.automation.hp = 400; engine->changeBoardWithSkill = YES;
    session = HeartSession(engine,HeartSettings(HeartPolicy(),@[PHOrbDefaultSkillRule()])); FinishSurvivalAttempt(session,engine);
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.size() == 1 && !engine->stages &&
        [[session snapshot][@"message"] containsString:@"心珠不足"], "pre-skill hearts cannot authorize a route after the skill removes them");

    NSMutableDictionary *required = [PHOrbDefaultSkillRule() mutableCopy]; required[@"required"] = @YES;
    engine = [FakeOrbEngine new]; SetHeartBoard(engine,6); engine->live.automation.hp = 400; engine->unavailableSkills[0] = YES;
    session = HeartSession(engine,HeartSettings(HeartPolicy(),@[required])); FinishSurvivalAttempt(session,engine);
    Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && [[session snapshot][@"message"] containsString:@"必要規則"],
        "plenty of hearts cannot bypass an unresolved required skill");

    engine = [FakeOrbEngine new]; SetHeartBoard(engine,3); engine->live.automation.hpKnown = false;
    session = HeartSession(engine,HeartSettings(HeartPolicy(),@[])); FinishSurvivalAttempt(session,engine);
    Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && [[session snapshot][@"message"] containsString:@"HP"],
        "enabled recovery refuses unknown HP instead of assuming a healthy turn");
    session = HeartSession(engine,HeartSettings(PHOrbDefaultRecovery(),@[]));
    Check(UntilPhase(session,engine,@"turnWaiting") && ![[session snapshot][@"recoveryActive"] boolValue],
        "disabled recovery retains legacy routes even when HP is unknown");
    [session queueStop:@"end disabled recovery fixture"];

    engine = [FakeOrbEngine new]; SetHeartBoard(engine,0); engine->live.automation.hpKnown = false;
    session = [[PHOrbSession alloc] initWithEngine:engine]; NSDictionary *settings = HeartSettings(HeartPolicy(),@[]);
    Check([session configureSettings:settings] && Preview(session,engine) && ![[session snapshot][@"recoveryActive"] boolValue] &&
        [[session snapshot][@"recoveryStatus"] containsString:@"手動"], "manual preview explicitly keeps its ordinary goals and HP-free read path");
    NSUInteger generation = [[session snapshot][@"generation"] unsignedIntegerValue];
    Check([session configureQuality:@"balanced"] && [[session snapshot][@"recovery"] isEqual:HeartPolicy()] &&
        [[session snapshot][@"generation"] unsignedIntegerValue] > generation,
        "quality changes cancel the old preview without resetting the saved recovery policy");
    Check(Preview(session,engine), "reconfigured manual preview can be requested again");
    generation = [[session snapshot][@"generation"] unsignedIntegerValue];
    Check([session configureSettings:HeartSettings(HeartPolicy(50),@[])] &&
        [[session snapshot][@"generation"] unsignedIntegerValue] > generation && ![[session snapshot][@"canExecute"] boolValue],
        "a recovery-only setting change invalidates an old preview");

    engine = [FakeOrbEngine new]; session = HeartSession(engine,HeartSettings(HeartPolicy(),@[]));
    Check(UntilPhase(session,engine,@"arming"), "healthy recovery-only fixture prepares an ordinary route");
    engine->live.automation.hp = 100; Tick(session,engine,Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && [[session snapshot][@"message"] containsString:@"HP"],
        "recovery-only settings watch HP before input even when no skill rules are configured");
    engine = [FakeOrbEngine new]; engine->dropHPOnStart = YES;
    session = HeartSession(engine,HeartSettings(HeartPolicy(),@[]));
    for (unsigned i = 0; i < 2000 && !engine->releases; ++i) Tick(session,engine,Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->releases == 1 && !engine->live.dragged && !engine->live.moves,
        "HP changing in the accepted start frame releases the owned orb without executing a stale route");

    engine = [FakeOrbEngine new]; SetHeartBoard(engine,3); engine->live.automation.hp = 400;
    session = [[PHOrbSession alloc] initWithEngine:engine];
    NSDictionary *profile = @{@"id":@"hearts", @"name":@"心珠策略", @"team":@[@0,@0,@0,@0,@0,@0], @"dungeon":@0, @"floor":@0,
        @"settings":HeartSettings(HeartPolicy(),@[])};
    Check([session configureSettings:HeartSettings(PHOrbDefaultRecovery(),@[])] &&
        [session configureProfiles:@{@"schema":@1,@"selected":@"",@"autoSelect":@YES,@"profiles":@[profile]}],
        "recovery strategy may differ from common disabled settings");
    Tick(session,engine,Frame()); [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && [[session snapshot][@"recoveryActive"] boolValue] &&
        [[session snapshot][@"activeProfile"][@"id"] isEqual:@"hearts"], "explicit-start profile selection captures its own recovery threshold");
    [session queueStop:@"end recovery profile fixture"];
}
