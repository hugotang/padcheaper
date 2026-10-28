static bool UntilPhase(PHOrbSession *session, FakeOrbEngine *engine, NSString *phase, unsigned frames = 2000) {
    for (unsigned i = 0; i < frames; ++i) {
        Tick(session, engine, Frame());
        if ([[session snapshot][@"phase"] isEqual:phase]) return true;
        if (![[session snapshot][@"automatic"] boolValue]) return false;
    }
    return false;
}
static PHOrbSession *AutomaticSession(FakeOrbEngine *engine, NSArray *rules) {
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":rules}], "automation rules accepted");
    Tick(session, engine, Frame()); [session queueAutomatic]; return session;
}
static void AutomationSessionTests() {
    FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = AutomaticSession(engine, @[]);
    Check([[session snapshot][@"busy"] boolValue] && [[session snapshot][@"canStop"] boolValue] &&
        ![session configureQuality:@"thorough"], "automatic waiting exposes stop and captures immutable settings");
    Check(UntilPhase(session, engine, @"turnWaiting"), "one start solves, executes and awaits native orb resolution");
    NSUInteger releases = engine->releases;
    NSUInteger firstGeneration = [[session snapshot][@"generation"] unsignedIntegerValue];
    for (unsigned i = 0; i < 25; ++i) Tick(session, engine, Frame());
    Check(engine->releases == releases && engine->skillOrder.empty(), "resolution wait does not start another gesture");
    engine->live.phase = 2; // Returning to phase 2 without native move reset is insufficient.
    for (unsigned i = 0; i < 25; ++i) Tick(session, engine, Frame());
    Check([[session snapshot][@"autoTurn"] unsignedIntegerValue] == 1, "requires move reset as well as the player phase");
    engine->live.moves = 0;
    for (unsigned i = 0; i < 2000 && engine->releases == releases && [[session snapshot][@"automatic"] boolValue]; ++i)
        Tick(session, engine, Frame());
    Check([[session snapshot][@"phase"] isEqual:@"turnWaiting"] && engine->releases == releases + 1 &&
        [[session snapshot][@"autoTurn"] unsignedIntegerValue] == 2 && [[session snapshot][@"generation"] unsignedIntegerValue] > firstGeneration,
        "next stable player turn executes automatically with a new route generation");
    [session queueStop:@"user stop while waiting"];
    engine->live.phase = 2; engine->live.moves = 0;
    for (unsigned i = 0; i < 40; ++i) Tick(session, engine, Frame());
    Check(engine->releases == releases + 1 && ![[session snapshot][@"busy"] boolValue], "stop waiting never resumes on the next turn");

    NSArray *oneRule = @[PHOrbDefaultSkillRule()];
    engine = [FakeOrbEngine new]; session = AutomaticSession(engine, oneRule);
    Check(UntilPhase(session, engine, @"skillWaiting") && engine->stages == 0, "skills precede the solver and synthetic drag");
    Check(UntilPhase(session, engine, @"turnWaiting") && engine->skillOrder.size() == 1 &&
        [[session snapshot][@"autoSkills"] unsignedIntegerValue] == 1 && [[session snapshot][@"autoTurn"] unsignedIntegerValue] == 1,
        "skill phase 22 returning to 2 does not reset per-turn rules even if cooldown remains ready");
    engine->live.phase = 2; engine->live.moves = 0;
    Check(UntilPhase(session, engine, @"skillWaiting") && engine->skillOrder.size() == 2, "same rule may run once on the next orb turn");
    [session queueStop:@"stop a started skill"];
    for (unsigned i = 0; i < 30; ++i) Tick(session, engine, Frame());
    Check(engine->skillOrder.size() == 2 && engine->releases == 1 && ![[session snapshot][@"automatic"] boolValue],
        "stopping a skill lets it finish but schedules no new input");

    engine = [FakeOrbEngine new]; session = AutomaticSession(engine, oneRule);
    Check(UntilPhase(session, engine, @"skillWaiting"), "floor-clearing skill starts");
    engine->skillFrames = 0; engine->live.phase = 7; engine->live.automation.members[0].state = 0;
    engine->live.automation.busy = false; engine->live.automation.skillUses = 1;
    Tick(session, engine, Frame()); // Observe native effect acknowledgement before the next floor initializes.
    ++engine->live.wave; engine->live.phase = 2; engine->live.automation.skillUses = 0;
    Check(UntilPhase(session, engine, @"turnWaiting") && engine->skillOrder.size() == 1 &&
        [[session snapshot][@"autoTurn"] unsignedIntegerValue] == 1,
        "floor-clearing skill retains its acknowledgement and cannot refresh per-turn casts");
    [session queueStop:@"end floor-clearing fixture"];

    NSMutableDictionary *second = [PHOrbDefaultSkillRule() mutableCopy]; second[@"slot"] = @2;
    engine = [FakeOrbEngine new]; engine->unavailableSkills[0] = YES; engine->hasteFirst = YES;
    session = AutomaticSession(engine, @[oneRule[0], second, oneRule[0]]);
    Check(UntilPhase(session, engine, @"turnWaiting") && engine->skillOrder.size() == 2 &&
        engine->skillOrder[0] == 1 && engine->skillOrder[1] == 0,
        "unavailable priority rule can become ready after haste, with no duplicate position cast");
    [session queueStop:@"end haste fixture"];

    second[@"color"] = @5; second[@"below"] = @1;
    engine = [FakeOrbEngine new]; engine->changeBoardWithSkill = YES;
    session = AutomaticSession(engine, @[oneRule[0], second]);
    Check(UntilPhase(session, engine, @"turnWaiting") && engine->skillOrder.size() == 2,
        "board conditions are reevaluated after a skill changes the board"); [session queueStop:@"end board fixture"];

    engine = [FakeOrbEngine new]; session = AutomaticSession(engine, oneRule);
    Check(UntilPhase(session, engine, @"skillArming"), "skill prepared for stale-context fixture");
    ++engine->live.automation.members[0].number; Tick(session, engine, Frame());
    Check(engine->skillOrder.empty() && ![[session snapshot][@"automatic"] boolValue], "changed member rejects the pending skill before its native call");

    for (unsigned change = 0; change < 6; ++change) {
        engine = [FakeOrbEngine new]; session = AutomaticSession(engine, oneRule);
        Check(UntilPhase(session, engine, @"skillArming"), "guard interruption fixture ready");
        PHBattleFrame frame = Frame();
        if (change == 0) engine->live.input.rawActiveMask = 1;
        if (change == 1) engine->live.identity.multiplayer = 1;
        if (change == 2) ++engine->live.identity.epoch;
        if (change == 3) frame.enabled = false;
        if (change == 4) frame.ready = false;
        if (change == 5) engine->failRead = YES;
        Tick(session, engine, frame);
        Check(engine->skillOrder.empty() && engine->stages == 0 && ![[session snapshot][@"automatic"] boolValue],
            "manual, multiplayer, account, master, background and read failure stop all pending actions");
    }
    engine = [FakeOrbEngine new]; engine->failSkillAck = YES; session = AutomaticSession(engine, oneRule);
    for (unsigned i = 0; i < 80; ++i) Tick(session, engine, Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.size() == 1 && engine->stages == 0,
        "unacknowledged skill completion stops instead of retrying or dragging");
    engine = [FakeOrbEngine new]; engine->hangSkill = YES; session = AutomaticSession(engine, oneRule);
    for (unsigned i = 0; i < 1600; ++i) Tick(session, engine, Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.size() == 1 && engine->stages == 0,
        "skill or interactive dialog timeout clears continuous intent");

    engine = [FakeOrbEngine new]; session = AutomaticSession(engine, @[]);
    Check(UntilPhase(session, engine, @"solving"), "automatic worker cancellation fixture ready");
    [session queueStop:@"cancel automatic solver"];
    for (unsigned i = 0; i < 250; ++i) Tick(session, engine, Frame());
    Check(engine->stages == 0 && ![[session snapshot][@"automatic"] boolValue], "late automatic solve cannot revive a stopped run");
    engine = [FakeOrbEngine new]; engine->live.present = false; session = AutomaticSession(engine, @[]);
    Tick(session, engine, Frame()); engine->live.present = true;
    for (unsigned i = 0; i < 50; ++i) Tick(session, engine, Frame());
    Check(engine->stages == 0 && ![[session snapshot][@"automatic"] boolValue], "start outside a battle never arms a future dungeon");

    engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
    for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = 0;
    Check([session configureSettings:@{@"quality":@"fast", @"mode":@"manual", @"rules":@[],
        @"goals":@[@{@"kind":@"l", @"colors":@2, @"count":@1, @"length":@0}]}], "unreachable automatic goal fixture configured");
    Tick(session, engine, Frame()); [session queueAutomatic];
    for (unsigned i = 0; i < 2000 && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session, engine, Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->stages == 0, "unreachable board goals terminate automatic intent without dragging");
    engine = [FakeOrbEngine new];
    NSMutableDictionary *enemyRule = [PHOrbDefaultSkillRule() mutableCopy]; enemyRule[@"enemy"] = @"damage_void";
    engine->live.automation.enemiesKnown = false; session = AutomaticSession(engine, @[enemyRule]);
    for (unsigned i = 0; i < 60; ++i) Tick(session, engine, Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.empty() && engine->stages == 0,
        "a needed unknown enemy condition stops before skills or solver input");
}
