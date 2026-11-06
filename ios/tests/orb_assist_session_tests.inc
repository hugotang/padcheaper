static NSDictionary *AssistSettings(BOOL enabled, NSArray *goals = @[], NSArray *rules = @[]) {
    return @{@"quality":@"fast", @"mode":goals.count ? @"manual" : @"combo", @"goals":goals, @"rules":rules,
        @"assistRecoveryL":@(enabled)};
}
static void SetAssistBoard(FakeOrbEngine *engine, unsigned turns = 2) {
    auto &board = engine->live.board;
    const uint8_t background[] = {0,1,2,4};
    for (unsigned i = 0; i < 30; ++i) board.types[i] = background[(i % 6 + 2 * (i / 6)) % 4];
    for (unsigned cell : {0u,6u,12u,13u,14u}) board.types[cell] = 3;
    for (unsigned cell : {24u,25u,26u,27u}) board.types[cell] = 5;
    // One harmless legal swap isolates policy/state-machine behavior from search difficulty.
    board.tapeMask = ((UINT64_C(1) << 30) - 1) & ~((UINT64_C(1) << 3) | (UINT64_C(1) << 4));
    for (unsigned i = 0; i < 30; ++i)
        board.flags[i] = (board.flags[i] & ~PHOrbFlagTape) |
            ((board.tapeMask & (UINT64_C(1) << i)) ? PHOrbFlagTape : 0);
    board.fixedStart = 4;
    Check(PHOrbBoardValid(&board), "assist session fixture satisfies native board metadata invariants");
    engine->live.assistRecovery = {}; engine->live.assistRecovery.valid = true;
    engine->live.assistRecovery.turns = turns; engine->live.assistRecovery.perColor[3] = 2;
    engine->live.assistRecovery.providers[3] = 1; engine->live.assistRecovery.colors[0] = 8;
    engine->live.assistRecovery.awakenings[0] = 2; engine->live.assistRecovery.latents[0] = 1;
}
static PHOrbSession *AssistSession(FakeOrbEngine *engine, NSDictionary *settings = nil) {
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:settings ?: AssistSettings(YES)], "assist policy settings accepted");
    Tick(session,engine,Frame()); [session queueAutomatic]; return session;
}
static void AssistPlanningTraceTests() {
    NSArray *base = @[@{@"kind":@"t",@"colors":@8,@"count":@1,@"length":@0},
        @{@"kind":@"four",@"colors":@32,@"count":@1,@"length":@0}];
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetAssistBoard(engine,0);
    for (unsigned i = 0; i < 30; ++i) if (engine->live.board.types[i] == 3) engine->live.board.types[i] = 2;
    for (unsigned i : {0u,1u,2u,7u,13u}) engine->live.board.types[i] = 3;
    PHOrbSession *session = AssistSession(engine,AssistSettings(YES,base));
    Check(UntilPhase(session,engine,@"turnWaiting"), "trace regression first executes its original T and heart-four goals");
    NSDictionary *executed = [session.snapshot[@"executionHistory"] lastObject];
    NSDictionary *previousSearch = session.snapshot[@"searchTrace"];
    NSUInteger stages = engine->stages;
    engine->live.board = {}; auto &board = engine->live.board;
    board.width = 7; board.height = 6;
    for (unsigned i = 0; i < 42; ++i) {
        board.tokens[i] = i + 200; board.types[i] = i < 9 ? 3 : 5;
        board.x[i] = 20 + (i % 7) * 50; board.y[i] = 100 + (i / 7) * 50;
    }
    board.spinnerMask = UINT64_C(1) << 8; board.spinnerColors = 63;
    engine->live.assistRecovery.turns = 3; engine->live.assistRecovery.perColor[3] = 4;
    Check(PHOrbBoardValid(&board), "shortage trace fixture has eight guaranteed light orbs and a ninth on a spinner");
    Check(!NextOrbTurn(session,engine), "new assist shortage stops before dispatching another search");
    NSDictionary *snapshot = session.snapshot, *trace = snapshot[@"searchTrace"];
    Check([trace[@"status"] isEqual:@"planning-blocked"] && ![trace[@"solverStarted"] boolValue] &&
        ![trace[@"generation"] isEqual:previousSearch[@"generation"]] &&
        [trace[@"board"][@"width"] isEqual:@7] && [trace[@"board"][@"types"] count] == 42 &&
        [trace[@"goals"] isEqual:base] && [trace[@"baseGoals"] isEqual:base] &&
        [trace[@"assistRecoveryInventory"][3][@"guaranteed"] isEqual:@8] &&
        [trace[@"assistRecoveryInventory"][3][@"possible"] isEqual:@9] &&
        [trace[@"reason"] containsString:@"10 顆"] && [trace[@"assistRecovery"][@"beforeTurns"] isEqual:@3],
        "pre-search stop captures the new board, original goals, full-phase inventory and current mechanism");
    Check(!trace[@"path"] && !trace[@"evaluation"] && !trace[@"candidateAttempts"] &&
        ![snapshot[@"goalEvaluationAvailable"] boolValue] && [snapshot[@"plannedGoalCount"] isEqual:@2] &&
        [snapshot[@"goalCount"] isEqual:@0] && [snapshot[@"confirmed"] isEqual:@0] &&
        [snapshot[@"searchTraceRelation"] isEqual:@"captured-at-current-position"] &&
        engine->stages == stages && engine->releases == 1 &&
        [[snapshot[@"executionHistory"] lastObject] isEqual:executed],
        "blocked planning has no stale result or new input and preserves the actual prior release record");
    Check([NSJSONSerialization isValidJSONObject:snapshot], "blocked planning diagnostics remain serializable");
    Check([session configureSettings:AssistSettings(NO)], "stopped automatic settings may be reloaded");
    for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
    Check([session.snapshot[@"searchTrace"] isEqual:trace], "idle configuration reload retains the captured stop evidence");

    for (bool finalRead : {false,true}) {
        engine = [FakeOrbEngine new]; SetAssistBoard(engine,0);
        session = AssistSession(engine);
        Check(UntilPhase(session,engine,@"turnWaiting"), "mechanism read failure fixture retains a completed first turn");
        executed = [session.snapshot[@"executionHistory"] lastObject]; previousSearch = session.snapshot[@"searchTrace"];
        engine->live.phase = 2; engine->live.moves = 0;
        Check(UntilPhase(session,engine,@"autoWaiting"), "second turn reaches its new decision boundary");
        engine->live.board.types[29] = 2;
        // The first read is stable pre-skill evidence; the second is final goal capture.
        engine->failAssistRead = engine->assistReads + (finalRead ? 2 : 1);
        for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
        snapshot = session.snapshot; trace = snapshot[@"searchTrace"];
        NSDictionary *failure = snapshot[@"assistRecoveryTrace"][@"failedRead"];
        Check(![snapshot[@"automatic"] boolValue] && [failure[@"turn"] isEqual:@2] &&
            [failure[@"boardRead"] boolValue] && [failure[@"board"][@"types"][29] isEqual:@2] &&
            ![snapshot[@"assistRecoveryTrace"][@"known"] boolValue] && engine->releases == 1 &&
            [[snapshot[@"executionHistory"] lastObject] isEqual:executed],
            "failed mechanism reads save their own current board without mutating prior execution evidence");
        if (finalRead) Check([trace[@"status"] isEqual:@"assist-unavailable"] && ![trace[@"solverStarted"] boolValue] &&
            [trace[@"turn"] isEqual:@2] && !trace[@"evaluation"] && !trace[@"path"] &&
            [trace[@"board"][@"types"][29] isEqual:@2] && ![snapshot[@"goalEvaluationAvailable"] boolValue],
            "final mechanism read failure replaces the old search with an unexecuted current capture");
        else Check([trace isEqual:previousSearch] && [snapshot[@"searchTraceRelation"] isEqual:@"captured-in-earlier-turn"],
            "a failure before planning identifies the retained search as an earlier turn");
    }
}
static void AssistSessionTests() {
    AssistPlanningTraceTests();
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetAssistBoard(engine); engine->failObservation = YES;
    NSArray *heart = @[@{@"kind":@"four",@"colors":@32,@"count":@1,@"length":@0}];
    PHOrbSession *session = AssistSession(engine,AssistSettings(YES,heart));
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->assistReads > 0 && engine->releases == 1 &&
        [session.snapshot[@"goalPlan"][@"assistRecoveryActive"] boolValue] &&
        [session.snapshot[@"goalCount"] unsignedIntValue] == 2 &&
        [session.snapshot[@"assistRecoveryTrace"][@"status"] isEqual:@"awaiting-next-turn"] &&
        ![session.snapshot[@"assistRecoveryTrace"][@"actualConfirmed"] boolValue],
        "fresh effective L plan retains heart-four, executes once and awaits actual confirmation despite missing diagnostics");
    NSDictionary *trace = session.snapshot[@"searchTrace"];
    CheckSearchAttemptTrace(trace);
    Check([trace[@"candidateAttempts"] count] > 0 &&
        [trace[@"goals"] isEqual:session.snapshot[@"goalPlan"][@"goals"]],
        "chosen candidate goals reach replay, release checks and execution diagnostics");
    NSUInteger allocated = 0;
    for (NSDictionary *attempt in trace[@"candidateAttempts"]) allocated += [attempt[@"budgetMS"] unsignedIntegerValue];
    Check(allocated <= [trace[@"budgetMS"] unsignedIntegerValue] &&
        [session.snapshot[@"searchBudgetMS"] isEqual:trace[@"budgetMS"]],
        "one candidate uses the shared public deadline rather than multiplying the search budget");
    engine->live.assistRecovery.turns = 0;
    Check(NextOrbTurn(session,engine) && ![session.snapshot[@"goalPlan"][@"assistRecoveryActive"] boolValue] &&
        [session.snapshot[@"goalPlan"][@"goals"] isEqual:heart] &&
        [session.snapshot[@"assistRecoveryTrace"][@"previousOutcome"][@"actualConfirmed"] boolValue] &&
        ![session.snapshot[@"assistRecoveryTrace"][@"previousOutcome"][@"lOnlyAmountConfirmed"] boolValue],
        "observed zero removes temporary L next turn without claiming its cause or dropping base hearts");
    [session queueStop:@"end confirmed recovery"];

    for (unsigned residual : {1u,3u}) {
        engine = [FakeOrbEngine new]; SetAssistBoard(engine); session = AssistSession(engine);
        Check(UntilPhase(session,engine,@"turnWaiting"), "residual recovery fixture releases its first route");
        engine->live.assistRecovery.turns = residual;
        Check(!NextOrbTurn(session,engine) && ![session.snapshot[@"automatic"] boolValue] && engine->releases == 1 &&
            [session.snapshot[@"assistRecoveryTrace"][@"status"] isEqual:@"unconfirmed-after-turn"] &&
            ![session.snapshot[@"assistRecoveryTrace"][@"actualConfirmed"] boolValue],
            "residual or re-applied assist void stops before another automatic route");
    }
    for (unsigned condition = 0; condition < 4; ++condition) {
        engine = [FakeOrbEngine new]; SetAssistBoard(engine); session = AssistSession(engine);
        Check(UntilPhase(session,engine,@"arming"), "assist fixture reaches the pre-input boundary");
        if (condition == 0) ++engine->live.assistRecovery.turns;
        if (condition == 1) engine->live.assistRecovery.perColor[3] = 0;
        if (condition == 2) engine->live.assistRecovery.providers[3] = 2;
        if (condition == 3) engine->failAssist = YES;
        Tick(session,engine,Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages,
            "duration, effective amount, provider identity or unreadability changes cancel before input");
    }
    for (bool duringMove : {false,true}) {
        engine = [FakeOrbEngine new]; SetAssistBoard(engine);
        engine->changeAssistOnStart = !duringMove; engine->changeAssistOnMove = duringMove;
        session = AssistSession(engine);
        for (unsigned i = 0; i < 2000 && !engine->releases; ++i) Tick(session,engine,Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && engine->releases == 1 && !engine->live.dragged &&
            engine->live.moves == unsigned(duringMove) && [session.snapshot[@"completed"] isEqual:@0],
            "a mechanism change inside native start or swap revokes holds and releases only the owned token");
    }
    engine = [FakeOrbEngine new]; SetAssistBoard(engine); session = AssistSession(engine);
    Check(UntilSolveStarts(session,engine), "assist cancellation captures an outstanding worker");
    NSUInteger generation = [session.snapshot[@"generation"] unsignedIntegerValue];
    ++engine->live.assistRecovery.turns;
    Tick(session,engine,Frame(),0.30,0,false);
    for (unsigned i = 0; i < 20; ++i) Tick(session,engine,Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages &&
        [session.snapshot[@"generation"] unsignedIntegerValue] != generation && ![session.snapshot[@"canExecute"] boolValue],
        "the bounded preview watcher cancels a stale worker and ignores its later completion");
    engine = [FakeOrbEngine new]; SetAssistBoard(engine); session = AssistSession(engine);
    Check(UntilPhase(session,engine,@"arming"), "lifecycle fixture prepares its assist route");
    UntilDragging(session,engine); [session stopForLifecycle:@"fixture background"];
    for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
    Check(engine->releases == 1 && !engine->live.dragged && ![session.snapshot[@"automatic"] boolValue],
        "lifecycle cancellation retains owned release with assist policy enabled");

    NSMutableDictionary *required = [PHOrbDefaultSkillRule() mutableCopy]; required[@"required"] = @YES;
    engine = [FakeOrbEngine new]; SetAssistBoard(engine); engine->unavailableSkills[0] = YES;
    session = AssistSession(engine,AssistSettings(YES,@[],@[required]));
    for (unsigned i = 0; i < 200 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(!engine->stages && [session.snapshot[@"message"] containsString:@"必要規則"],
        "a complete L recovery plan never bypasses an unavailable required rescue skill");
    engine = [FakeOrbEngine new]; SetAssistBoard(engine); engine->live.automation.playerEffects = PHOrbPlayerAwokenBind;
    engine->clearPlayerStatusWithSkill = YES;
    session = AssistSession(engine,AssistSettings(YES,@[],@[required]));
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && !engine->live.automation.playerEffects,
        "required skills are confirmed first and final L planning reads the resulting board and state");
    [session queueStop:@"end required rescue"];

    engine = [FakeOrbEngine new]; SetAssistBoard(engine); engine->failAssist = YES;
    session = AssistSession(engine,AssistSettings(NO));
    Check(UntilPhase(session,engine,@"turnWaiting") && !engine->assistReads,
        "disabled policy is independent of unsupported action reads");
    [session queueStop:@"end disabled policy"];
    engine = [FakeOrbEngine new]; SetAssistBoard(engine); engine->failAssist = YES;
    session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureSettings:AssistSettings(YES)];
    Check(Preview(session,engine) && !engine->assistReads && ![session.snapshot[@"goalPlan"][@"assistRecoveryActive"] boolValue],
        "manual preview does not inherit the continuous L policy or its data dependency");
    [session queueStop:@"end manual preview"];
    for (unsigned players : {3u,4u,8u,1u}) {
        engine = [FakeOrbEngine new];
        if (players == 1) SetSessionContinuation(engine);
        else if (players == 8) engine = EightEngineFixture();
        else SetMultiplayer(engine,players);
        engine->failAssist = YES; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:AssistSettings(YES)], "multiplayer may save the solo L policy");
        Tick(session,engine,Frame()); [session queueAutomatic];
        if (players == 8) {
            EightRunUntilRelease(session,engine,1);
            Check([session.snapshot[@"automatic"] boolValue] && !engine->assistReads && !engine->skillAttempts &&
                ![session.snapshot[@"goalPlan"][@"assistRecoveryActive"] boolValue],
                "8P Auto executes its board without importing the saved solo L recovery policy");
            [session queueStop:@"end 8P policy isolation"];
            engine = [FakeOrbEngine new]; SetMultiplayer(engine,8); engine->failAssist = YES;
            session = [[PHOrbSession alloc] initWithEngine:engine];
            [session configureSettings:AssistSettings(YES)]; Tick(session,engine,Frame());
        } else Check(![session.snapshot[@"automatic"] boolValue] && !engine->assistReads,
            "3/4-player and co-op continuation cannot enter continuous L automation");
        [session queueOnce];
        for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
        Check(engine->releases == 1 && !engine->assistReads && !engine->skillAttempts &&
            [session.snapshot[@"completed"] isEqual:@1], "3/4/8-player and co-op continuation retain the independent one-shot action");
    }
    engine = [FakeOrbEngine new]; SetAssistBoard(engine); engine->live.settled = false; session = AssistSession(engine);
    for (unsigned i = 0; i < 15; ++i) Tick(session,engine,Frame());
    Check([session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->assistReads,
        "profile binding waits for stable mechanism evidence without stopping an initial animation");
    engine->live.settled = true;
    Check(UntilPhase(session,engine,@"turnWaiting"), "stable state completes deferred policy binding");
    [session queueStop:@"end delayed binding"];

    // A dark candidate sorts first but is blocked by the fixed/taped route;
    // the existing light L is reachable. This catches replaying the first goals
    // after a later allocation solved.
    engine = [FakeOrbEngine new]; SetAssistBoard(engine);
    for (unsigned cell : {16u,17u,21u,23u,28u}) engine->live.board.types[cell] = 4;
    engine->live.assistRecovery.perColor[4] = 2; engine->live.assistRecovery.providers[4] = 1;
    session = AssistSession(engine);
    Check(UntilPhase(session,engine,@"turnWaiting"), "a later reachable color allocation can complete the route");
    trace = session.snapshot[@"searchTrace"];
    NSDictionary *choice = session.snapshot[@"goalPlan"][@"assistRecoveryChoice"];
    CheckSearchAttemptTrace(trace);
    Check([trace[@"candidateAttempts"] count] == 2 && [choice[@"counts"][3] unsignedIntValue] == 1 &&
        [choice[@"counts"][4] unsignedIntValue] == 0 && [trace[@"goals"] isEqual:choice[@"goals"]] &&
        [session.snapshot[@"goalHits"][0] unsignedIntValue] == 1,
        "the second color candidate supplies the final goals, replay checks and actual release evaluation");
    Check([trace[@"selectedAttemptIndex"] isEqual:@1] && ![trace[@"soloRetrySkipped"] length],
        "assist selection stays in its shared budget and does not enter the solo retry policy");
    const NSUInteger sharedBudget = [trace[@"budgetMS"] unsignedIntegerValue];
    for (NSDictionary *attempt in trace[@"candidateAttempts"])
        Check([attempt[@"budgetMS"] unsignedIntegerValue] <= sharedBudget,
            "each alternative receives only a portion of the remaining shared deadline");
    [session queueStop:@"end alternate color route"];

    // A selected full plan can bind an exactly retained but voided weapon.
    // Stop after the first body skill arms, so no missing-assist skill is used.
    engine = [FakeOrbEngine new]; SetAssistBoard(engine);
    NSMutableDictionary *profile = [PlanSessionProfile() mutableCopy];
    profile[@"opening"] = @[]; profile[@"settings"] = AssistSettings(YES,@[],@[PHOrbDefaultSkillRule()]);
    session = PlanSession(engine,profile);
    engine->live.automation.members[0].assistCatalogID = 0;
    Check(UntilPhase(session,engine,@"skillArming") && engine->skillOrder.empty() && engine->assistReads > 0,
        "fresh retained source evidence lets the opted-in formation reach its normal body-skill decision");
    [session queueStop:@"end retained binding"];

    engine = [FakeOrbEngine new]; SetAssistBoard(engine); session = AssistSession(engine);
    Check(UntilPhase(session,engine,@"turnWaiting"), "unknown outcome fixture completes one gesture");
    engine->failAssist = YES;
    Check(!NextOrbTurn(session,engine) && engine->releases == 1 &&
        [session.snapshot[@"assistRecoveryTrace"][@"status"] isEqual:@"unavailable"] &&
        ![session.snapshot[@"assistRecoveryTrace"][@"actualConfirmed"] boolValue],
        "missing next-turn evidence retains the original decision and stops without retry");
}
