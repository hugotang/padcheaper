static NSUInteger ObservedSkillTurn(PHOrbSession *session) {
    return [session.snapshot[@"ruleTiming"][@"observedTurn"] unsignedIntegerValue];
}
static void ObservedManualPuzzle(PHOrbSession *session, FakeOrbEngine *engine, bool zeroSwap = false) {
    if (zeroSwap) {
        engine->live.phase = 2; engine->live.moves = 0; engine->live.settled = false;
        engine->live.dragged = engine->live.board.tokens[0]; Tick(session,engine,Frame(),0.12);
    }
    engine->live.dragged = 0; engine->live.phase = 3; engine->live.moves = zeroSwap ? 0 : 1;
    Tick(session,engine,Frame(),0.12);
    engine->live.phase = 2; engine->live.moves = 0; engine->live.settled = true;
    engine->live.automation.skillUses = 0; Tick(session,engine,Frame(),0.12);
}
static void SkillTimingSessionTests() {
    NSMutableDictionary *a = [PHOrbDefaultSkillRule() mutableCopy];
    a[@"slot"] = @4; a[@"group"] = @1; a[@"every"] = @2;
    a[@"intervalMode"] = @"accepted"; a[@"startAfter"] = @2;
    NSMutableDictionary *b = [a mutableCopy]; b[@"slot"] = @5;
    FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = AutomaticSession(engine,@[a,b]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty() && ObservedSkillTurn(session) == 1,
        "first warmup puzzle does not consume either rotation member");
    Check(NextOrbTurn(session,engine) && engine->skillOrder.empty() && ObservedSkillTurn(session) == 2,
        "second warmup puzzle still preserves both members");
    [session queueStop:@"stop during second puzzle resolution"];
    engine->live.phase = 2; engine->live.moves = 0;
    for (unsigned i = 0; i < 8; ++i) Tick(session,engine,Frame(),0.12);
    NSDictionary *common = @{@"quality":@"fast",@"mode":@"combo",@"goals":@[],@"rules":@[]};
    Check(ObservedSkillTurn(session) == 3 && [session configureSettings:common],
        "stopped resolution advances once and an idle settings reload is allowed");
    Check([session configureSettings:@{@"quality":@"fast",@"mode":@"combo",@"goals":@[],@"rules":@[a,b]}],
        "restore the same cycle after an idle common-settings reload");
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder == std::vector<NSUInteger>{3} &&
        ObservedSkillTurn(session) == 3 && [session.snapshot[@"autoTurn"] unsignedIntegerValue] == 1,
        "third observed puzzle starts slot four despite the run display restarting at one");
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 1 && ObservedSkillTurn(session) == 4,
        "next observed turn keeps the two-turn interval");
    Check(NextOrbTurn(session,engine) && engine->skillOrder == (std::vector<NSUInteger>{3,4}) &&
        [session.snapshot[@"ruleTiming"][@"groupLastUseTurns"][0] unsignedIntegerValue] == 5,
        "slot five takes over on observed turn five and records the shared use");
    [session queueStop:@"end warmup fixture"];

    a[@"startAfter"] = @0; b[@"startAfter"] = @0;
    engine = [FakeOrbEngine new]; engine->unavailableSkills[3] = engine->unavailableSkills[4] = YES;
    session = AutomaticSession(engine,@[a,b]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty() &&
        [session.snapshot[@"ruleTiming"][@"groupLastUseTurns"][0] unsignedIntegerValue] == 0,
        "native unavailable results never start an interval");
    engine->unavailableSkills[3] = NO;
    Check(NextOrbTurn(session,engine) && engine->skillOrder == std::vector<NSUInteger>{3} &&
        [session.snapshot[@"ruleTiming"][@"groupLastUseTurns"][0] unsignedIntegerValue] == 2,
        "a missed first turn is retried on the next turn instead of waiting for odd parity");
    engine->unavailableSkills[4] = NO;
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 1, "late acceptance delays the next member by two turns");
    Check(NextOrbTurn(session,engine) && engine->skillOrder == (std::vector<NSUInteger>{3,4}),
        "the late cycle resumes on observed turn four");
    [session queueStop:@"end late-use fixture"];

    a[@"startAfter"] = @2; b[@"startAfter"] = @2;
    NSMutableDictionary *rescueA = [a mutableCopy], *rescueB = [b mutableCopy];
    rescueA[@"every"] = rescueB[@"every"] = @1;
    rescueA[@"startAfter"] = rescueB[@"startAfter"] = @0;
    rescueA[@"enemy"] = rescueB[@"enemy"] = @"damage_absorb";
    rescueA[@"required"] = rescueB[@"required"] = @YES;
    for (bool unavailable : {false,true}) {
        engine = [FakeOrbEngine new]; engine->live.automation.enemyEffects = PHOrbEnemyDamageAbsorb;
        engine->unavailableSkills[3] = engine->unavailableSkills[4] = unavailable;
        session = AutomaticSession(engine,@[a,b,rescueA,rescueB]);
        if (unavailable) {
            for (unsigned i = 0; i < 180 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
            Check(![session.snapshot[@"automatic"] boolValue] && engine->skillOrder.empty() && !engine->stages &&
                [session.snapshot[@"message"] containsString:@"必要規則"],
                "warmup cannot hide a required absorption rescue when both members are unavailable");
            continue;
        }
        Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder == std::vector<NSUInteger>{3},
            "absorption rescue remains eligible on the first warmup turn");
        engine->live.automation.playerBypass = PHOrbEnemyDamageAbsorb;
        Check(UntilPhase(session,engine,@"turnWaiting"), "the accepted rescue completes before the first puzzle");
        engine->live.automation.enemyEffects = engine->live.automation.playerBypass = 0;
        Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 1,
            "ordinary rotation does not prematurely resume during the second warmup turn");
        Check(NextOrbTurn(session,engine) && engine->skillOrder == (std::vector<NSUInteger>{3,4}),
            "a rescue row advances the next rotation position rather than resetting to its previous caster");
        [session queueStop:@"end early rescue fixture"];
    }

    a[@"startAfter"] = @0;
    engine = [FakeOrbEngine new]; engine->rejectStartedSkill = YES;
    session = AutomaticSession(engine,@[a]);
    for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(engine->skillOrder.size() == 1 && [session.snapshot[@"ruleTiming"][@"groupLastUseTurns"][0] unsignedIntegerValue] == 1,
        "uncertain native starts retain conservative timing alongside the per-turn attempt");
    engine->rejectStartedSkill = NO;
    for (unsigned i = 0; i < 12; ++i) Tick(session,engine,Frame(),0.12);
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1,
        "stop and restart cannot retry an uncertain start in the same turn");
    Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 1,
        "uncertain starts also preserve the next-turn interval");
    [session queueStop:@"end uncertain-use fixture"];

    engine = [FakeOrbEngine new]; engine->closeWindowInUse = YES;
    session = AutomaticSession(engine,@[a]);
    for (unsigned i = 0; i < 100 && engine->closeWindowInUse; ++i) Tick(session,engine,Frame());
    Check(!engine->closeWindowInUse && engine->skillOrder.empty() && !engine->stages &&
        [session.snapshot[@"ruleTiming"][@"groupLastUseTurns"][0] unsignedIntegerValue] == 0,
        "a last-moment closed window does not consume a timed group's interval");
    engine->live.automation.window = true; engine->live.automation.windowBlocks = 0;
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder.size() == 1 &&
        [session.snapshot[@"ruleTiming"][@"groupLastUseTurns"][0] unsignedIntegerValue] == 1,
        "the same turn may start its timed skill after the window reopens");
    [session queueStop:@"end timed window fixture"];

    engine = [FakeOrbEngine new]; engine->rejectStartedSkill = YES;
    engine->live.automation.enemyEffects = PHOrbEnemyDamageAbsorb;
    session = AutomaticSession(engine,@[rescueA,rescueB]);
    for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    engine->rejectStartedSkill = NO;
    for (unsigned i = 0; i < 12; ++i) Tick(session,engine,Frame(),0.12);
    [session queueAutomatic];
    for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(engine->skillOrder.size() == 1 && !engine->stages && ![session.snapshot[@"automatic"] boolValue] &&
        [session.snapshot[@"message"] containsString:@"必要規則"],
        "an uncertain same-turn rescue cannot become permission to drag merely by reserving its interval");

    a[@"startAfter"] = @2;
    engine = [FakeOrbEngine new]; session = AutomaticSession(engine,@[a]);
    for (unsigned i = 0; i < 5; ++i) Tick(session,engine,Frame());
    [session queueStop:@"observe manual turns while stopped"];
    engine->live.phase = 22; engine->live.automation.busy = true; Tick(session,engine,Frame(),0.12);
    engine->live.phase = 3; engine->live.moves = 0; Tick(session,engine,Frame(),0.12);
    ++engine->live.wave; engine->live.phase = 2; engine->live.automation.busy = false;
    Tick(session,engine,Frame(),0.12);
    Check(ObservedSkillTurn(session) == 1, "skill animation and floor clear without an orb gesture do not count as warmup");
    engine->failRead = YES; engine->live.phase = 3; engine->live.moves = 1; Tick(session,engine,Frame(),0.12);
    engine->failRead = NO; engine->live.phase = 2; engine->live.moves = 0; Tick(session,engine,Frame(),0.12);
    Check(ObservedSkillTurn(session) == 1, "an unreadable resolution is not guessed to be a completed puzzle");
    ObservedManualPuzzle(session,engine,true);
    Check(ObservedSkillTurn(session) == 2, "an observed zero-swap gesture plus resolution counts once");
    ObservedManualPuzzle(session,engine);
    Check(ObservedSkillTurn(session) == 3, "a manually completed second puzzle preserves warmup progress");
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder == std::vector<NSUInteger>{3},
        "manual warmup resumes the delayed rule without resetting its observed clock");
    [session queueStop:@"end manual warmup fixture"];
    for (unsigned i = 0; i < 12; ++i) Tick(session,engine,Frame(),0.12);
    ++engine->live.identity.entryHash[0]; engine->live.automation.skillUses = 0;
    Tick(session,engine,Frame(),0.12); [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && ObservedSkillTurn(session) == 1,
        "a new battle starts its own warmup and never inherits an old group's accepted use");
    [session queueStop:@"end new-battle timing fixture"];

    NSMutableDictionary *legacy = [PHOrbDefaultSkillRule() mutableCopy]; legacy[@"group"] = @1;
    engine = [FakeOrbEngine new]; session = AutomaticSession(engine,@[legacy]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1,
        "legacy idle-history fixture completes its first skill and puzzle");
    [session queueStop:@"pause legacy rule during resolution"];
    engine->live.phase = 2; engine->live.moves = 0;
    engine->live.automation.valid = false; Tick(session,engine,Frame(),0.12);
    engine->live.automation.valid = true; [session queueAutomatic];
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillOrder.size() == 2,
        "ordinary idle reads without automation fields still clear legacy per-turn history");
    [session queueStop:@"end legacy idle-history fixture"];

    NSMutableDictionary *first = [@{@"id":@"timed-a",@"name":@"A",@"team":@[@0,@0,@0,@0,@0,@0],@"dungeon":@0,@"floor":@0,
        @"settings":@{@"quality":@"fast",@"mode":@"combo",@"goals":@[],@"rules":@[a]}} mutableCopy];
    NSMutableDictionary *second = [first mutableCopy]; second[@"id"] = @"timed-b"; second[@"name"] = @"B";
    engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureProfiles:@{@"schema":@1,@"selected":@"timed-a",@"autoSelect":@NO,@"profiles":@[first,second]}],
        "independent profiles may contain identical timed rules");
    Tick(session,engine,Frame()); [session queueAutomatic];
    for (unsigned i = 0; i < 5; ++i) Tick(session,engine,Frame());
    [session queueStop:@"pause profile A"]; ObservedManualPuzzle(session,engine); ObservedManualPuzzle(session,engine);
    NSMutableDictionary *renamed = [first mutableCopy]; renamed[@"name"] = @"A renamed";
    NSMutableDictionary *retargeted = [first[@"settings"] mutableCopy]; retargeted[@"quality"] = @"balanced";
    renamed[@"settings"] = retargeted;
    Check([session configureProfiles:@{@"schema":@1,@"selected":@"timed-a",@"autoSelect":@NO,@"profiles":@[renamed,second]}],
        "renaming a profile and changing search quality remain independent of skill timing");
    [session queueAutomatic];
    for (unsigned i = 0; i < 5; ++i) Tick(session,engine,Frame());
    Check(ObservedSkillTurn(session) == 3 && engine->skillOrder.empty(),
        "non-timing profile edits retain the observed warmup before a new skill decision");
    [session queueStop:@"pause renamed profile A"];
    Check(ObservedSkillTurn(session) == 3 && [session configureProfiles:
        @{@"schema":@1,@"selected":@"timed-b",@"autoSelect":@NO,@"profiles":@[first,second]}],
        "selecting profile B occurs after A's observed warmup");
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && ObservedSkillTurn(session) == 1 && engine->skillOrder.empty(),
        "a different profile cannot inherit warmup just because its rule array is identical");
    [session queueStop:@"end profile timing binding fixture"];
}
