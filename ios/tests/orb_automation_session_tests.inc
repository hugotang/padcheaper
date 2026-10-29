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
static void SkillSelectionSessionTests() {
    NSMutableArray *rules = [NSMutableArray array];
    for (NSUInteger slot : {1u, 5u, 6u, 4u, 3u, 2u}) {
        NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy]; rule[@"slot"] = @(slot);
        if (slot == 5 || slot == 6) rule[@"group"] = @1;
        [rules addObject:rule];
    }
    FakeOrbEngine *engine = [FakeOrbEngine new];
    for (unsigned slot = 0; slot < 6; ++slot) engine->unavailableSkills[slot] = slot != 1;
    engine->live.automation.members[1].number = 14055;
    engine->live.automation.members[1].charge = 7;
    engine->live.automation.members[1].selection = 65063;
    PHOrbSession *session = AutomaticSession(engine, rules);
    Check(UntilPhase(session, engine, @"skillArming") &&
        [[[session snapshot][@"ruleDecisionDetails"] firstObject][@"slot"] unsignedIntegerValue] == 1,
        "rule decisions start in configured rule order rather than position order");
    Check(UntilPhase(session, engine, @"skillWaiting") && engine->skillOrder.size() == 1 && engine->skillOrder[0] == 1,
        "rule six reaches ready position two despite a retained old assist choice and five preceding unavailable rules");
    NSDictionary *detail = [session snapshot][@"ruleDecisionDetails"][5];
    Check([detail[@"rule"] unsignedIntegerValue] == 6 && [detail[@"slot"] unsignedIntegerValue] == 2 &&
        [detail[@"number"] unsignedIntegerValue] == 14055 && [detail[@"charge"] unsignedIntegerValue] == 7 &&
        [detail[@"selection"] unsignedIntValue] == 65063 && [detail[@"decision"] containsString:@"已啟動"],
        "diagnostics preserve the exact rule-to-position mapping and member context that led to the skill attempt");
    Check(UntilPhase(session, engine, @"turnWaiting") && engine->skillOrder.size() == 1 &&
        [[[session snapshot][@"ruleDecisions"] lastObject] containsString:@"本回合已確認完成"],
        "a still-ready position remains limited to one confirmed use per orb turn after selection preparation");
    [session queueStop:@"end retained selection fixture"];
    // Real idle reads omit automation context; copied decisions must keep the
    // observation which caused them, rather than report zeroed current fields.
    engine->live.automation = {};
    for (unsigned i = 0; i < 12; ++i) Tick(session, engine, Frame());
    detail = [session snapshot][@"ruleDecisionDetails"][5];
    Check([detail[@"number"] unsignedIntegerValue] == 14055 && [detail[@"charge"] unsignedIntegerValue] == 7 &&
        [detail[@"decision"] containsString:@"本回合已確認完成"], "stopping and subsequent idle reads retain the recorded decision context");
    Check([session configureSettings:@{@"quality":@"balanced", @"mode":@"combo", @"goals":@[], @"rules":@[]}],
        "idle common settings can be restored after the run");
    detail = [session snapshot][@"ruleDecisionDetails"][5];
    Check([detail[@"rule"] unsignedIntegerValue] == 6 && [detail[@"slot"] unsignedIntegerValue] == 2 &&
        [detail[@"number"] unsignedIntegerValue] == 14055,
        "restoring different idle settings preserves the prior run's decisions and rule-position mapping");

    NSMutableDictionary *second = [PHOrbDefaultSkillRule() mutableCopy]; second[@"slot"] = @2;
    for (bool required : {false, true}) {
        engine = [FakeOrbEngine new]; engine->live.automation.members[1].selection = 65063;
        engine->unavailableSkills[1] = YES; second[@"required"] = @(required);
        session = AutomaticSession(engine, @[second]);
        if (required) {
            for (unsigned i = 0; i < 120 && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session, engine, Frame());
            Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages &&
                [[session snapshot][@"message"] containsString:@"必要規則"], "required retained-choice skill still blocks dragging when native readiness refuses");
        } else {
            Check(UntilPhase(session, engine, @"turnWaiting"), "optional retained-choice skill may be skipped on actual native refusal");
            [session queueStop:@"end unavailable selection fixture"];
        }
        Check(engine->skillAttempts == 1 && engine->skillOrder.empty() &&
            engine->live.automation.members[1].selection == 65063,
            "native refusal is not retried and does not prepare or consume the unavailable skill");
    }
    engine = [FakeOrbEngine new]; engine->live.automation.members[1].selection = 65063;
    engine->live.automation.members[1].state = 4; engine->live.automation.busy = true;
    session = AutomaticSession(engine, @[second]);
    for (unsigned i = 0; i < 40; ++i) Tick(session, engine, Frame());
    Check(!engine->skillAttempts && !engine->stages && engine->live.automation.members[1].selection == 65063,
        "an open native selection dialog prevents all automatic skill and orb input");
    [session queueStop:@"end open selection fixture"];
}
static void RouteProgressSessionTests() {
    FakeOrbEngine *engine = [FakeOrbEngine new];
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"fast", @"mode":@"manual", @"rules":@[],
        @"goals":@[@{@"kind":@"four", @"colors":@32, @"count":@1, @"length":@0}]}], "progress regression uses a heart-four objective");
    // One movable fire orb leaves an isolated heart four after its first swap.
    for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = 0;
    engine->live.board.types[6] = engine->live.board.types[12] = engine->live.board.types[18] = engine->live.board.types[25] = 5;
    engine->live.board.fixedStart = 25;
    Tick(session, engine, Frame()); [session queueAutomatic];
    Check(UntilPhase(session, engine, @"turnWaiting") && [[session snapshot][@"confirmed"] unsignedIntegerValue] > 0,
        "first successful route has observed progress before the next search");
    NSUInteger completed = [[session snapshot][@"completed"] unsignedIntegerValue];
    const NSUInteger stages = engine->stages;
    engine->live.phase = 2; engine->live.moves = 0; engine->live.board.fixedStart = 0;
    for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = 0;
    for (unsigned i = 0; i < 2000 && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session, engine, Frame());
    NSDictionary *result = [session snapshot];
    Check(![result[@"automatic"] boolValue] && ![result[@"goalsMet"] boolValue] &&
        [result[@"confirmed"] unsignedIntegerValue] == 0 && ![result[@"startSent"] boolValue] && ![result[@"startAccepted"] boolValue] &&
        [result[@"completed"] unsignedIntegerValue] == completed && engine->stages == stages,
        "an unsuccessful second search shows zero route progress without losing the completed count or sending new input");
    [session queueRead];
    Check([[session snapshot][@"confirmed"] unsignedIntegerValue] == 0 && [[session snapshot][@"steps"] unsignedIntegerValue] == 0,
        "starting another manual read clears the previous candidate and its progress together");
    [session queueStop:@"end progress fixture"];
}
static void EvolvedSkillTurnSessionTests() {
    NSMutableArray *rules = [NSMutableArray array];
    for (unsigned slot = 1; slot <= 6; ++slot) {
        NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy]; rule[@"slot"] = @(slot);
        [rules addObject:rule];
    }
    FakeOrbEngine *engine = [FakeOrbEngine new];
    engine->live.automation.members[2].number = 14175;
    engine->live.automation.members[2].charge = 4;
    engine->live.automation.members[2].skillStage[0] = engine->live.automation.members[2].cachedSkillStage[0] = 1;
    engine->unavailableSkills[2] = YES;
    PHOrbSession *session = AutomaticSession(engine, rules);
    Check(UntilPhase(session, engine, @"turnWaiting") && engine->skillOrder.size() == 5,
        "five independent ready skills finish while the evolved third position is still one turn short");
    for (NSUInteger slot : engine->skillOrder) Check(slot != 2, "no cast bypasses the third position's native cooldown refusal");
    NSDictionary *detail = [session snapshot][@"ruleDecisionDetails"][2];
    engine->live.automation.members[2].charge = 5; // Native orb resolution charges the next turn.
    Check([detail[@"charge"] unsignedIntegerValue] == 4 &&
        [[[session snapshot][@"ruleDecisionDetails"][2] objectForKey:@"charge"] unsignedIntegerValue] == 4,
        "the previous skill decision keeps four charge even when post-resolution live charge is five");
    for (unsigned slot = 0; slot < 6; ++slot) engine->unavailableSkills[slot] = slot != 2;
    engine->live.phase = 2; engine->live.moves = 0;
    Check(UntilPhase(session, engine, @"skillWaiting") && engine->skillOrder.size() == 6 && engine->skillOrder.back() == 2 &&
        [[session snapshot][@"autoTurn"] unsignedIntegerValue] == 2,
        "the next native orb turn clears old refusals and casts the newly ready evolved skill exactly once");
    [session queueStop:@"end evolved skill turn fixture"];
}
static void AutomationSessionTests() {
    SkillSelectionSessionTests();
    RouteProgressSessionTests();
    EvolvedSkillTurnSessionTests();
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

    engine = [FakeOrbEngine new]; engine->requireHoldForStart = YES; engine->live.fps = 30;
    session = AutomaticSession(engine, @[]);
    for (NSUInteger turn = 1; turn <= 2; ++turn) {
        if (turn > 1) { engine->live.phase = 2; engine->live.moves = 0; }
        for (unsigned i = 0; i < 2000 && [[session snapshot][@"automatic"] boolValue] &&
            [[session snapshot][@"autoMoves"] unsignedIntegerValue] < turn; ++i)
            Tick(session, engine, Frame(), 1.0 / 30);
        Check([[session snapshot][@"phase"] isEqual:@"turnWaiting"] && engine->beginFrames == turn &&
            engine->releases == turn && [[session snapshot][@"autoTurn"] unsignedIntegerValue] == turn,
            "continuous held-start execution emits one fresh began and one release per orb turn");
    }
    [session queueStop:@"end continuous held-start fixture"];

    engine = [FakeOrbEngine new]; engine->ignoreStart = YES; session = AutomaticSession(engine, @[]);
    for (unsigned i = 0; i < 2000 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
    NSString *startLog = [[session snapshot][@"automationLog"] componentsJoinedByString:@"\n"];
    Check([[session snapshot][@"phase"] isEqual:@"stopped"] && engine->beginFrames == 1 && engine->releases == 0 &&
        [[session snapshot][@"autoMoves"] unsignedIntegerValue] == 0 &&
        [[session snapshot][@"message"] containsString:@"遊戲未接受起點"] && [startLog containsString:@"遊戲未接受起點"],
        "continuous start rejection retains the same cause in both status and skill log after cleanup");

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

static void FramePacingSessionTests() {
    FakeOrbEngine *engine = [[FakeOrbEngine alloc] initWithWidth:7 height:6];
    engine->requireHoldForStart = YES; engine->live.fps = 30; engine->live.remainingFrames = 330;
    // The existing IMG_0407 regression produces a long, fully verified route.
    constexpr uint8_t board[]{3,1,0,0,3,4,5, 3,5,5,0,3,3,5, 5,3,0,5,1,5,2,
        3,4,5,5,3,5,4, 1,3,5,0,3,1,2, 0,5,2,0,3,5,4};
    std::copy(std::begin(board), std::end(board), engine->live.board.types);
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"thorough", @"mode":@"manual", @"rules":@[],
        @"goals":@[@{@"kind":@"t", @"colors":@8, @"count":@1},
                    @{@"kind":@"four", @"colors":@32, @"count":@1},
                    @{@"kind":@"combo", @"colors":@0, @"count":@1}]}], "render-latency pacing goals");
    Check(Preview(session, engine), "long route preview for 30-fps rendering fixture");
    const NSUInteger steps = [[session snapshot][@"steps"] unsignedIntegerValue];
    Check(steps > 40 && [[session snapshot][@"goalsMet"] boolValue],
        "pacing regression exercises a valid route longer than the old render-latency cutoff");
    Execute(session);
    // Twelve ms of native rendering/read work causes old confirmation-based
    // timing to require three frames per step despite budgeting only two.
    for (unsigned i = 0; i < 180 && [[session snapshot][@"busy"] boolValue]; ++i)
        Tick(session, engine, Frame(), 1.0 / 30, 0.012);
    NSDictionary *result = [session snapshot];
    Check([result[@"phase"] isEqual:@"complete"] && engine->live.moves == steps &&
        [result[@"confirmed"] unsignedIntegerValue] == steps && engine->releases == 1 && !engine->live.dragged &&
        [result[@"executionElapsedMS"] unsignedIntegerValue] < 4000,
        "native render latency does not accumulate an extra frame per swap or truncate an acknowledged long route");

    engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session, engine), "execution deadline diagnostic setup"); Execute(session); UntilDragging(session, engine);
    Tick(session, engine, Frame(), 4.1);
    result = [session snapshot];
    Check([result[@"phase"] isEqual:@"stopped"] && [result[@"message"] containsString:@"4.0 秒上限"] &&
        [result[@"executionElapsedMS"] unsignedIntegerValue] > 4000 && !engine->live.dragged && engine->releases == 1 &&
        [result[@"completed"] unsignedIntegerValue] == 0,
        "a real execution stall still releases once and preserves the specific deadline cause");
}
