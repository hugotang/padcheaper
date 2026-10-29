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
static void PlanSessionTests() {
    NSDictionary *profile = PlanSessionProfile();
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
