static NSDictionary *PlanSessionProfile(void) {
    NSData *data = [NSData dataWithContentsOfFile:@"ios/templates/ishida-uryu-gamewith-574233/team-plan.json"];
    id plan = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    NSMutableDictionary *profile = [PHOrbProfileFromPlan(plan,NULL) mutableCopy];
    // Keep actual opening identities/order, but isolate the state-machine
    // fixture from solver difficulty and real cooldown timing.
    NSMutableDictionary *loop = [PHOrbDefaultSkillRule() mutableCopy];
    loop[@"slot"] = @5; loop[@"source"] = @"body"; loop[@"skill"] = @64914;
    profile[@"settings"] = @{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[loop]};
    return profile;
}
static PHOrbSession *PlanSession(FakeOrbEngine *engine, NSDictionary *profile) {
    for (unsigned i = 0; i < 6; ++i) {
        engine->live.automation.members[i].number = [profile[@"team"][i] unsignedIntValue];
        PHBattleCatalogIDForNumber([profile[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue],
            &engine->live.automation.members[i].assistCatalogID);
    }
    for (NSDictionary *step in profile[@"opening"]) {
        unsigned slot = [step[@"slot"] unsignedIntValue] - 1;
        engine->chosenSource[slot] = [step[@"source"] isEqual:@"body"] ? PHOrbSkillBody : PHOrbSkillAssist;
        engine->chosenSkill[slot] = [step[@"skill"] unsignedIntValue];
        engine->transformAfterSkill[slot] = [step[@"afterMonster"] unsignedIntValue];
    }
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureProfiles:@{@"schema":@1,@"selected":profile[@"id"],@"autoSelect":@NO,@"profiles":@[profile]}],
        "complete plan accepted by the session");
    Tick(session,engine,Frame()); [session queueAutomatic]; return session;
}
static void SetTemporaryForm(FakeOrbEngine *engine, unsigned slot) {
    auto &member = engine->live.automation.members[slot];
    member.identityNumber = member.number; member.identityAssist = member.assistCatalogID;
    member.number = 4264; member.assistCatalogID = 0;
    member.temporaryKnown = true; member.temporaryTurns = 3;
    PHBattleCatalogIDForNumber(4264, &member.temporaryID);
    if (slot == 0 || slot == 5) { engine->live.leadersValid = true; engine->live.leaderNumbers[slot == 5] = 4264; }
}
static void TemporaryPlanSessionTests() {
    NSMutableDictionary *profile = [PlanSessionProfile() mutableCopy];
    NSMutableDictionary *leader = [PHOrbDefaultSkillRule() mutableCopy];
    leader[@"slot"] = @1; leader[@"monster"] = @14038; leader[@"source"] = @"body"; leader[@"skill"] = @64787;
    NSMutableDictionary *other = [PHOrbDefaultSkillRule() mutableCopy]; other[@"slot"] = @5; other[@"monster"] = @14065;
    profile[@"settings"] = @{@"quality":@"fast", @"mode":@"manual",
        @"goals":@[@{@"kind":@"combo",@"colors":@0,@"count":@1,@"length":@0}], @"rules":@[leader,other]};
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        NSMutableDictionary *currentProfile = [profile mutableCopy];
        if (scenario == 5) {
            NSMutableDictionary *settings = [currentProfile[@"settings"] mutableCopy];
            settings[@"mode"] = @"leaders"; currentProfile[@"settings"] = settings;
        }
        FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = PlanSession(engine,currentProfile);
        for (unsigned i = 0; i < 3000 && [[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] < 6; ++i)
            Tick(session,engine,Frame());
        Check(engine->skillOrder.size() == 6, "temporary form fixture completes opening once");
        engine->chosenSkill[0] = 64787;
        SetTemporaryForm(engine,0); engine->live.leaderNumbers[1] = 14065;
        if (scenario == 1) engine->live.automation.members[0].temporaryKnown = false;
        if (scenario == 2) ++engine->live.automation.members[0].identityAssist;
        if (scenario == 3) engine->live.automation.members[0].temporaryTurns = 0;
        if (scenario == 4) {
            [session queueStop:@"stop during temporary form"]; [session queueAutomatic];
        }
        if (scenario >= 1 && scenario <= 3) {
            for (unsigned i = 0; i < 60 && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
            Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.size() == 6 && !engine->stages,
                "unknown temporary state, changed underlying assist or persistent replacement cannot bypass plan binding");
            continue;
        }
        if (scenario == 5) {
            // Whether the current pair is supported or not, the new plan
            // must use the actual temporary leader, never its identity ID.
            for (unsigned i = 0; i < 2500 && ![[session snapshot][@"goalPlan"] count] &&
                [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
            NSDictionary *actual = [session snapshot][@"goalPlan"];
            NSDictionary *expected = PHOrbPlanForLeaders(&engine->live);
            Check([actual[@"leaders"] isEqual:expected[@"leaders"]] &&
                [actual[@"goals"] isEqual:expected[@"goals"]] &&
                [actual[@"available"] isEqual:expected[@"available"]] &&
                [[actual[@"leaders"] firstObject] containsString:@"#4264"] &&
                [[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 6,
                "leader mode rebuilds current temporary leader goals after preserving the completed opening");
            [session queueStop:@"end current temporary leader plan fixture"];
            continue;
        }
        Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 7 && engine->skillOrder.back() == 4 &&
            [[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 6 &&
            [[session snapshot][@"goalPlan"][@"warning"] containsString:@"暫時變身"],
            "a temporary leader permits other loop skills and manual goals, preserves opening and reports changed leader conditions");
        Check([[[session snapshot][@"ruleDecisions"] componentsJoinedByString:@" "] containsString:@"原形態技能暫停"],
            "the original leader skill is explicitly skipped without a native attempt");
        for (unsigned i = 0; i < 5; ++i) Tick(session,engine,Frame());
        auto &member = engine->live.automation.members[0];
        member.number = member.identityNumber; member.assistCatalogID = member.identityAssist; member.temporaryTurns = 0;
        engine->live.leaderNumbers[0] = member.number;
        engine->live.phase = 2; engine->live.moves = 0; engine->live.settled = true; ++engine->live.wave;
        for (unsigned i = 0; i < 2500 && engine->skillOrder.size() < 8 && [[session snapshot][@"automatic"] boolValue]; ++i)
            Tick(session,engine,Frame());
        Check(engine->skillOrder.size() >= 8 && engine->skillOrder[7] == 0 &&
            [[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 6,
            "the original leader resumes its skill after native restoration without replaying the opening");
        [session queueStop:@"end temporary leader fixture"];
    }
    FakeOrbEngine *explicitEngine = [FakeOrbEngine new]; SetTemporaryForm(explicitEngine,2);
    NSMutableDictionary *explicitRule = [PHOrbDefaultSkillRule() mutableCopy];
    explicitRule[@"slot"] = @3; explicitRule[@"monster"] = @4264;
    PHOrbSession *explicitSession = AutomaticSession(explicitEngine,@[explicitRule]);
    Check(UntilPhase(explicitSession,explicitEngine,@"skillWaiting") && explicitEngine->skillOrder.size() == 1 &&
        explicitEngine->skillOrder[0] == 2,
        "an explicit rule for the actual temporary monster may still request its current native skill");
    [explicitSession queueStop:@"end explicit temporary skill fixture"];
    // Required rescue is independent from the temporary caster's availability.
    for (bool rescueNeeded : {false,true}) {
        FakeOrbEngine *engine = [FakeOrbEngine new];
        NSMutableDictionary *rescue = [PHOrbDefaultSkillRule() mutableCopy];
        rescue[@"slot"] = @3; rescue[@"monster"] = @102; rescue[@"status"] = @"awoken_bind"; rescue[@"required"] = @YES;
        SetTemporaryForm(engine,2);
        engine->live.automation.playerEffects = rescueNeeded ? PHOrbPlayerAwokenBind : 0;
        PHOrbSession *session = AutomaticSession(engine,@[rescue]);
        if (rescueNeeded) {
            FinishSurvivalAttempt(session,engine);
            Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && engine->skillOrder.empty() &&
                [[session snapshot][@"message"] containsString:@"必要規則"] && [[session snapshot][@"message"] containsString:@"暫時變身"],
                "a required cleanse cannot disappear because its original caster temporarily became another monster");
        } else {
            Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty(),
                "temporary rescue caster does not stop a turn with no corresponding status");
            [session queueStop:@"end unneeded temporary rescue fixture"];
        }
    }
}
static void PlanSessionTests() {
    TemporaryPlanSessionTests();
    NSDictionary *profile = PlanSessionProfile();
    // The fifth real opening skill resizes the board without necessarily
    // changing phase away from two. Acknowledge it, then await full geometry.
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        FakeOrbEngine *resizeEngine = [FakeOrbEngine new]; resizeEngine->resizeWithSkill = YES;
        PHOrbSession *resizeSession = PlanSession(resizeEngine,profile);
        for (unsigned i = 0; i < 3000 && !resizeEngine->live.boardTransition &&
            [[resizeSession snapshot][@"automatic"] boolValue]; ++i) Tick(resizeSession,resizeEngine,Frame());
        Check(resizeEngine->live.boardTransition && resizeEngine->skillOrder.size() == 5 &&
            [[resizeSession snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 4 &&
            !resizeEngine->stages && [[resizeSession snapshot][@"automatic"] boolValue],
            "phase-2 resize keeps fifth opening skill in flight with no orb input or sixth activation");
        if (scenario == 0) {
            for (unsigned i = 0; i < 3000 && [[resizeSession snapshot][@"automatic"] boolValue] &&
                [[resizeSession snapshot][@"opening"][@"completed"] unsignedIntegerValue] < 6; ++i)
                Tick(resizeSession,resizeEngine,Frame());
            Check(resizeEngine->skillOrder == std::vector<NSUInteger>({4,0,3,1,2,5}) &&
                [[resizeSession snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 6 &&
                resizeEngine->live.board.width == 7 && resizeEngine->live.board.height == 6 &&
                [[resizeSession snapshot][@"boardTransitionFrames"] unsignedIntegerValue] > 0 && !resizeEngine->stages,
                "six-step opening continues exactly once after resize returns a stable 7x6 board");
        } else if (scenario == 1) {
            [resizeSession queueStop:@"manual stop while resizing"];
            for (unsigned i = 0; i < 40; ++i) Tick(resizeSession,resizeEngine,Frame());
            Check(![[resizeSession snapshot][@"automatic"] boolValue] && resizeEngine->skillOrder.size() == 5 &&
                [[resizeSession snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 5 && !resizeEngine->stages,
                "manual stop during resize records the finished fifth skill without starting the sixth");
            [resizeSession queueAutomatic];
            for (unsigned i = 0; i < 3000 && [[resizeSession snapshot][@"automatic"] boolValue] &&
                [[resizeSession snapshot][@"opening"][@"completed"] unsignedIntegerValue] < 6; ++i)
                Tick(resizeSession,resizeEngine,Frame());
            Check(resizeEngine->skillOrder.size() == 6 && resizeEngine->skillOrder.back() == 5 &&
                [[resizeSession snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 6,
                "explicit resume after resizing starts only the sixth, never repeats the fifth");
        } else if (scenario == 2) {
            resizeEngine->hangResize = YES;
            for (unsigned i = 0; i < 1700 && [[resizeSession snapshot][@"automatic"] boolValue]; ++i)
                Tick(resizeSession,resizeEngine,Frame());
            Check(![[resizeSession snapshot][@"automatic"] boolValue] &&
                [[resizeSession snapshot][@"message"] containsString:@"逾時"] && resizeEngine->skillOrder.size() == 5 &&
                [[resizeSession snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 4 && !resizeEngine->stages,
                "stuck resizing retains unconfirmed progress and stops at the bounded skill timeout");
        } else {
            PHBattleFrame changed = Frame();
            if (scenario == 3) changed.ready = false;
            if (scenario == 4) resizeEngine->live.input.rawActiveMask = 1;
            if (scenario == 5) ++resizeEngine->live.identity.entryHash[0];
            if (scenario == 6) resizeEngine->live.identity.multiplayer = 1;
            if (scenario == 7) resizeEngine->failRead = YES;
            if (scenario == 8) resizeEngine->live.present = false;
            Tick(resizeSession,resizeEngine,changed);
            Check(![[resizeSession snapshot][@"automatic"] boolValue] && resizeEngine->skillOrder.size() == 5 && !resizeEngine->stages,
                "foreground, input, identity, multiplayer, unreadable data and exit guards still stop during resizing");
        }
        [resizeSession queueStop:@"end resize regression fixture"];
    }
    std::printf("Opening resize regressions: 9 scenarios checked\n");
    FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = PlanSession(engine,profile);
    const std::vector<NSUInteger> expected{4,0,3,1,2,5};
    for (unsigned i = 0; i < 3000 && [[session snapshot][@"automatic"] boolValue] &&
        [[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] < 6; ++i) Tick(session,engine,Frame());
    Check(engine->skillOrder == expected && engine->live.automation.members[0].number == 14038 && !engine->stages,
        "six opening skills execute in article order, confirm transformation and send no orb input in between");
    engine->chosenSkill[4] = 64914;
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 7 && engine->skillOrder.back() == 4,
        "a newly-ready body may run after its opening assist, then ordinary per-turn repetition protection applies");
    [session queueStop:@"fixture stop after opening"];
    NSDictionary *progress = [session snapshot][@"opening"];
    [session queueAutomatic];
    for (unsigned i = 0; i < 50; ++i) Tick(session,engine,Frame());
    Check([[session snapshot][@"opening"][@"completed"] isEqual:progress[@"completed"]] && engine->skillOrder.size() == 7,
        "a repeated start does not replay the opening or the same-turn loop skill");
    [session queueStop:@"end plan fixture"];

    engine = [FakeOrbEngine new]; session = PlanSession(engine,profile);
    engine->chosenSource[4] = PHOrbSkillBody;
    for (unsigned i = 0; i < 300 && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.empty() && !engine->stages &&
        [[session snapshot][@"message"] containsString:@"開場第 1 步"], "wrong skill source stops at the current opening step without fallback or dragging");
    engine->chosenSource[4] = PHOrbSkillAssist; [session queueAutomatic];
    Check(UntilPhase(session,engine,@"openingWaiting") && engine->skillOrder.size() == 1,
        "an unavailable, unattempted opening step can be retried by explicit start");
    [session queueStop:@"manual stop during opening"];
    for (unsigned i = 0; i < 15; ++i) Tick(session,engine,Frame());
    Check([[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 1 && engine->skillOrder.size() == 1,
        "a skill started before stopping can finish and update progress without starting the next skill");
    [session queueAutomatic];
    for (unsigned i = 0; i < 300 && engine->skillOrder.size() < 2; ++i) Tick(session,engine,Frame());
    Check(engine->skillOrder.size() == 2 && engine->skillOrder[1] == 0, "resume continues at the next confirmed opening step");
    [session queueStop:@"end resume fixture"];

    for (bool stopped : {false,true}) {
        engine = [FakeOrbEngine new]; session = PlanSession(engine,profile); engine->failSkillAck = YES;
        if (stopped) {
            Check(UntilPhase(session,engine,@"openingWaiting"), "opening started before manual-stop receipt fixture");
            [session queueStop:@"manual stop before missing receipt"];
        }
        for (unsigned i = 0; i < 300; ++i) Tick(session,engine,Frame());
        ++engine->live.automation.skillUses; // An unrelated manual skill after the missing receipt.
        for (unsigned i = 0; i < 15; ++i) Tick(session,engine,Frame());
        [session queueAutomatic];
        for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
        Check(engine->skillOrder.size() == 1 && [[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 0 &&
            ![[session snapshot][@"automatic"] boolValue],
            "a missing opening receipt cannot be repaired by an unrelated later skill counter, even after stopping");
    }

    engine = [FakeOrbEngine new]; session = PlanSession(engine,profile); engine->rejectStartedSkill = YES;
    for (unsigned i = 0; i < 300 && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    for (unsigned i = 0; i < 12; ++i) Tick(session,engine,Frame());
    [session queueAutomatic];
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    Check(engine->skillOrder.size() == 1 && [[session snapshot][@"opening"][@"completed"] unsignedIntegerValue] == 0 &&
        ![[session snapshot][@"automatic"] boolValue] && !engine->stages,
        "uncertain activation cannot be replayed or silently inferred complete from a later counter");

    for (bool wrongAssist : {false,true}) {
        engine = [FakeOrbEngine new]; session = PlanSession(engine,profile);
        if (wrongAssist) ++engine->live.automation.members[5].assistCatalogID;
        else engine->live.wave = 1;
        for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
        Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.empty() && !engine->stages,
            "a changed assist or a fresh start on a later floor never replays the opening");
    }
    engine = [FakeOrbEngine new]; session = PlanSession(engine,profile);
    engine->live.multiplayerDisabled = true; engine->live.identity.multiplayer = 1;
    for (unsigned i = 0; i < 20; ++i) Tick(session,engine,Frame());
    Check(engine->skillOrder.empty() && !engine->stages, "complete-plan config never enables multiplayer writes");
}

static void ExecutionTraceSessionTests() {
    FakeOrbEngine *engine = [FakeOrbEngine new]; engine->omitResolvingBoard = YES;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    [session configureQuality:@"fast"];
    for (unsigned run = 0; run < 4; ++run) {
        engine->live.phase = 2; engine->live.moves = 0; engine->live.settled = true;
        Check(Preview(session,engine), "history fixture preview"); Execute(session);
        for (unsigned i = 0; i < 300 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
        NSDictionary *record = [[session snapshot][@"executionHistory"] lastObject];
        Check([record[@"status"] isEqual:@"released"] && [record[@"releaseConfirmed"] boolValue] &&
            [record[@"releaseEvaluation"][@"goalsMet"] boolValue] && [record[@"observedBoard"][@"types"] count] == 30,
            "accepted release keeps the pre-release board and validation despite omitted resolving cells");
        Check([record[@"search"][@"quality"] isEqual:@"fast"] && [record[@"search"][@"budgetMS"] unsignedIntValue] == 120 &&
            [record[@"search"][@"evaluated"] unsignedIntValue] > 0 && [record[@"search"][@"searchPasses"] unsignedIntValue] > 0 &&
            [record[@"search"][@"exploredDepth"] unsignedIntValue] >= [record[@"confirmedSteps"] unsignedIntValue],
            "completed route history keeps its own search budget, effort and depth for later low-combo diagnosis");
        bool equal = true;
        for (unsigned cell = 0; cell < 30; ++cell) equal &= [record[@"observedBoard"][@"types"][cell] unsignedIntValue] == engine->live.board.types[cell];
        Check(equal, "retained release board is the last observed native board, not a zeroed phase-three snapshot");
    }
    Check([[session snapshot][@"executionHistory"] count] == 3, "actual route history is bounded to the last three accepted gestures");
    NSDictionary *retained = [[session snapshot][@"executionHistory"] lastObject];
    engine->live.phase = 2; engine->live.moves = 0;
    for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = PHOrbPoison;
    [session queueOnce];
    for (unsigned i = 0; i < 2000 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
    Check([[[session snapshot][@"executionHistory"] lastObject] isEqual:retained] &&
        [[session snapshot][@"message"] containsString:@"毒 30 顆"] && ![[session snapshot][@"startSent"] boolValue],
        "failed poison search preserves the last executed route, explains remaining poison and sends no input");
}
