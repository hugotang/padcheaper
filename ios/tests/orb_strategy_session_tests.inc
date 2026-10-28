static bool NextOrbTurn(PHOrbSession *session, FakeOrbEngine *engine) {
    NSUInteger releases = engine->releases;
    engine->live.phase = 2; engine->live.moves = 0;
    for (unsigned i = 0; i < 2200 && [[session snapshot][@"automatic"] boolValue]; ++i) {
        Tick(session,engine,Frame());
        if (engine->releases > releases && [[session snapshot][@"phase"] isEqual:@"turnWaiting"]) return true;
    }
    return false;
}
static void StrategySessionTests() {
    NSMutableDictionary *a = [PHOrbDefaultSkillRule() mutableCopy], *b = [a mutableCopy];
    a[@"group"] = @1; b[@"group"] = @1; b[@"slot"] = @2;
    FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = AutomaticSession(engine,@[a,b]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && engine->skillOrder[0] == 0,
        "first grouped turn spends only the first available skill");
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 2 && engine->skillOrder[1] == 1,
        "second turn rotates to the next group member even if the first remains ready");
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 3 && engine->skillOrder[2] == 0,
        "rotation wraps while keeping one group skill per orb turn");
    [session queueStop:@"end cycle fixture"];

    engine = [FakeOrbEngine new]; engine->unavailableSkills[0] = YES; engine->hasteFirst = YES;
    session = AutomaticSession(engine,@[a,b]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && engine->skillOrder[0] == 1,
        "unavailable first member falls back, and haste cannot spend the same group again this turn");
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 2 && engine->skillOrder[1] == 0,
        "fallback rotates only after native acceptance"); [session queueStop:@"end fallback fixture"];

    a = [PHOrbDefaultSkillRule() mutableCopy]; a[@"once"] = @YES;
    engine = [FakeOrbEngine new]; session = AutomaticSession(engine,@[a]);
    Check(UntilPhase(session,engine,@"skillWaiting"), "once skill starts before stop");
    [session queueStop:@"stop during once skill"];
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1,
        "stop and immediate restart do not replay a once skill or erase per-turn history");
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 1, "once history survives subsequent orb turns");
    [session queueStop:@"leave once battle"];
    engine->live.identity.entryHash[0] = 2; engine->live.phase = 2; engine->live.moves = 0;
    Tick(session,engine,Frame()); [session queueAutomatic];
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder.size() == 2, "a confirmed new battle resets once history");
    [session queueStop:@"end once fixture"];

    a = [PHOrbDefaultSkillRule() mutableCopy]; a[@"reserveUntil"] = @3;
    b = [PHOrbDefaultSkillRule() mutableCopy];
    engine = [FakeOrbEngine new]; session = AutomaticSession(engine,@[a,b]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty(),
        "another rule cannot consume a position reserved for a later floor");
    engine->live.wave = 2;
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 1,
        "reservation is released at the target floor with ordinary per-position deduplication");
    [session queueStop:@"end reservation fixture"];

    a = [PHOrbDefaultSkillRule() mutableCopy]; a[@"obstacle"] = @"spinner";
    engine = [FakeOrbEngine new]; engine->live.obstacles = PHOrbObstacleSpinner; engine->live.ordinary = false;
    engine->clearObstaclesWithSkill = YES; session = AutomaticSession(engine,@[a]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1,
        "spinner remedy runs before solving and requires a freshly cleared board");
    [session queueStop:@"end remedy fixture"];
    engine = [FakeOrbEngine new]; engine->live.obstacles = PHOrbObstacleSpinner; engine->live.ordinary = false;
    session = AutomaticSession(engine,@[a]);
    for (unsigned i = 0; i < 100 && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.size() == 1 && engine->stages == 0 &&
        [[session snapshot][@"message"] containsString:@"轉轉"], "a skill that leaves the spinner active cannot authorize a static route");
    engine = [FakeOrbEngine new]; engine->live.obstacles = PHOrbObstacleSpinner; engine->live.ordinary = false;
    session = AutomaticSession(engine,@[a]);
    for (unsigned i = 0; i < 700 && [[session snapshot][@"automatic"] boolValue]; ++i) {
        engine->live.board.types[0] = (engine->live.board.types[0] + 1) % 6;
        Tick(session,engine,Frame());
    }
    Check(engine->skillOrder.empty() && engine->stages == 0 && ![[session snapshot][@"automatic"] boolValue],
        "continuously changing spinner never produces a fake stable skill or drag snapshot");

    NSMutableDictionary *profileRule = [PHOrbDefaultSkillRule() mutableCopy]; profileRule[@"slot"] = @2;
    NSDictionary *profile = @{@"id":@"bound",@"name":@"指定隊伍",@"dungeon":@1,@"floor":@2,
        @"team":@[@100,@101,@102,@103,@104,@0],
        @"settings":@{@"quality":@"fast",@"mode":@"combo",@"goals":@[],@"rules":@[profileRule]}};
    NSDictionary *library = @{@"schema":@1,@"selected":@"bound",@"autoSelect":@NO,@"profiles":@[profile]};
    engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureProfiles:library], "profile library configured before the run");
    Tick(session,engine,Frame()); [session queueAutomatic];
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder[0] == 1 &&
        ![session configureProfiles:PHOrbDefaultProfiles()], "selected settings load only at explicit start and stay locked during the run");
    ++engine->live.automation.members[0].number;
    Check(UntilPhase(session,engine,@"turnWaiting") && [[session snapshot][@"activeProfile"][@"id"] isEqual:@"bound"],
        "transformation after start does not rematch or swap strategy settings");
    [session queueStop:@"end bound profile fixture"];
    engine = [FakeOrbEngine new]; ++engine->live.automation.members[0].number;
    session = [[PHOrbSession alloc] initWithEngine:engine]; [session configureProfiles:library];
    Tick(session,engine,Frame()); [session queueAutomatic];
    Tick(session,engine,Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && engine->stages == 0 && engine->skillOrder.empty(),
        "mismatched profile stops before any native skill or synthetic input");

    a = [PHOrbDefaultSkillRule() mutableCopy]; a[@"group"] = @1;
    b = [a mutableCopy]; b[@"slot"] = @2;
    NSDictionary *common = @{@"quality":@"fast",@"mode":@"combo",@"goals":@[],@"rules":@[]};
    NSMutableDictionary *cycling = [profile mutableCopy];
    cycling[@"settings"] = @{@"quality":@"fast",@"mode":@"combo",@"goals":@[],@"rules":@[a,b]};
    library = @{@"schema":@1,@"selected":@"",@"autoSelect":@YES,@"profiles":@[cycling]};
    engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureProfiles:library] && [session configureSettings:common], "automatic profile starts from different common rules");
    Tick(session,engine,Frame()); [session queueAutomatic];
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder.size() == 1 && engine->skillOrder[0] == 0,
        "automatic matching starts the profile cycle at its first member");
    [session queueStop:@"pause matched profile before dragging"];
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    Check([session configureSettings:common], "idle runtime reload restores the persisted common settings");
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1,
        "reloading common settings cannot allow a second group cast in the same orb turn");
    [session queueStop:@"pause matched profile after dragging"];
    engine->live.phase = 2; engine->live.moves = 0;
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    Check([session configureSettings:common], "another idle config reload remains independent of cycle history");
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder.size() == 2 && engine->skillOrder[1] == 1,
        "the same automatically matched profile resumes at its next member after stop and idle reload");
    [session queueStop:@"end matched cycle history fixture"];
}
