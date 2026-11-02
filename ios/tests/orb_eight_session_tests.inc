#include "orb_eight_fixture.h"
static FakeOrbEngine *EightEngineFixture() {
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetMultiplayer(engine,8,7);
    EightTeamFixture(&engine->live,EightProfileFixture());
    for (unsigned i = 0; i < 6; ++i) { engine->chosenSource[i] = PHOrbSkillBody; engine->chosenSkill[i] = engine->live.eight.members[i].activeSkill; }
    return engine;
}
static void EightNextBoard(FakeOrbEngine *engine, bool floor) {
    engine->live.phase = 2; engine->live.moves = 0;
    engine->live.settled = engine->live.ordinary = true; engine->live.automation.busy = false;
    engine->live.multiplayer.resolvePending = 0; engine->live.multiplayer.networkFlags = 1;
    ++engine->live.multiplayer.turnSerial; if (floor) ++engine->live.wave;
    for (unsigned i = 0; i < 30; ++i) { engine->live.board.tokens[i] = 100 + i; engine->live.board.types[i] = uint8_t(i / 6); }
    for (auto &m : engine->live.automation.members) ++m.charge;
}
static void EightRunUntilRelease(PHOrbSession *session, FakeOrbEngine *engine, unsigned releases) {
    for (unsigned i = 0; i < 3000 && engine->releases < releases && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    if (engine->releases != releases) std::fprintf(stderr,"8P state: %s\n",[session.snapshot.description UTF8String]);
    Check(engine->releases == releases,"8P Auto reaches an acknowledged release");
}
static void EightSelectProfile(PHOrbSession *session, NSDictionary *profile) {
    Check([session configureProfiles:@{@"schema":@1,@"selected":profile[@"id"],@"autoSelect":@NO,@"profiles":@[profile]}],
        "8P activity profile selected for the current match");
}
static void EightWaterWood(FakeOrbEngine *engine) {
    for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = uint8_t(i / 15 + 1);
}
static void EightWaitForSkill(PHOrbSession *session, FakeOrbEngine *engine) {
    for (unsigned i = 0; i < 3000 && !engine->skillFrames && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(engine->skillFrames && [session.snapshot[@"eightSkillPending"] boolValue],"fixture reaches an in-flight native skill");
}
static void EightResumeTests() {
    // B2 and B5 use ordinary activity skills whose limited-use counters stay0.
    for (unsigned wave : {1u,4u}) {
        FakeOrbEngine *engine = EightEngineFixture(); engine->live.wave = wave; EightWaterWood(engine);
        if (wave == 4) for (unsigned i = 6; i < 12; ++i) engine->live.board.types[i] = 0;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        EightSelectProfile(session,EightProfileFixture()); [session queueAutomatic];
        EightRunUntilRelease(session,engine,1);
        Check(engine->skillOrder == std::vector<NSUInteger>{wave == 1 ? 1u : 5u} &&
            [session.snapshot[@"autoSkills"] isEqual:@1] && !engine->live.automation.members[wave == 1 ? 1 : 5].skillUses[0],
            "B2/B5 ordinary skill receipt permits the subsequent board solve and native release");
        [session queueStop:@"fixture done"];
    }
    for (unsigned pause = 0; pause < 5; ++pause) {
        FakeOrbEngine *engine = EightEngineFixture(); engine->live.wave = 1; EightWaterWood(engine);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        NSDictionary *profile = EightProfileFixture(); EightSelectProfile(session,profile); [session queueAutomatic];
        EightWaitForSkill(session,engine);
        if (pause == 1) {
            for (unsigned i = 0; i < 50 && ![session.snapshot[@"autoSkills"] isEqual:@1]; ++i) Tick(session,engine,Frame());
            Check([session.snapshot[@"autoSkills"] isEqual:@1] && !engine->stages,"stop after confirmation precedes a gesture");
        }
        [session queueStop:@"pause after native start"];
        if (pause == 4) {
            NSMutableDictionary *changed = [profile mutableCopy]; changed[@"name"] = @"same-match profile change";
            EightSelectProfile(session,changed); // Idle configuration must not stop receipt observation.
        }
        for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
        Check(!engine->eightArmed && !engine->stages && engine->skillAttempts == 1 && !engine->skillFrames &&
            [session.snapshot[@"eightProgress"][@"skillAcknowledged"] boolValue],
            "native effect finishes during pause, retained receipt does not start another skill or gesture");
        if (pause != 1) Check(engine->eightProgressReads > 0,"pending native completion is observed read-only after Stop");
        if (pause == 2 || pause == 3) {
            engine->live.phase = 3;
            for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
            EightNextBoard(engine,pause == 2); // User has played while Auto was stopped.
            if (pause == 3) --engine->live.multiplayer.turnSerial; // Versus may retain its serial.
            engine->live.automation.skillUses += 2;
            engine->live.eightReceipt = {1002,1004745,2,0,true};
            for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
        }
        const NSUInteger oldGeneration = [session.snapshot[@"generation"] unsignedIntegerValue];
        [session queueAutomatic];
        EightRunUntilRelease(session,engine,1);
        Check(engine->skillAttempts == 1 && [session.snapshot[@"autoSkills"] isEqual:@1] &&
            [session.snapshot[@"autoMoves"] isEqual:@1] && [session.snapshot[@"eightProgress"][@"resumes"] isEqual:@1] &&
            [session.snapshot[@"generation"] unsignedIntegerValue] > oldGeneration &&
            [session.snapshot[@"eightProgress"][@"lastStop"][@"reason"] containsString:@"pause after native start"],
            "same-match resume retains exactly one skill, counters, stop reason and a fresh route generation");
        Check([session.snapshot[@"autoTurn"] unsignedIntegerValue] == (pause == 2 || pause == 3 ? 2u : 1u),
            "manual same-floor and next-floor turns replan once without resetting Auto progress");
        [session queueStop:@"pause after release"];
        EightNextBoard(engine,false); [session queueAutomatic]; EightRunUntilRelease(session,engine,2);
        Check(engine->skillAttempts == 1 && [session.snapshot[@"autoMoves"] isEqual:@2] &&
            [session.snapshot[@"autoSkills"] isEqual:@1],"repeated Stop/Auto retains successful release and skill accounting");
        [session queueStop:@"fixture done"];
    }
    // A queued solver result from before Stop cannot acquire a gesture.
    FakeOrbEngine *engine = EightEngineFixture();
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame()); [session queueAutomatic];
    Check(UntilSolveStarts(session,engine),"8P solver is in flight before pause");
    [session queueStop:@"pause solver"];
    for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
    Check(!engine->stages && !engine->eightArmed,"late 8P solve callback is cancelled while paused");
    [session queueAutomatic]; EightRunUntilRelease(session,engine,1); [session queueStop:@"fixture done"];

    for (bool switchProfile : {false,true}) {
        engine = EightEngineFixture(); engine->live.wave = 1; EightWaterWood(engine);
        session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        NSMutableDictionary *profile = [EightProfileFixture() mutableCopy];
        NSMutableDictionary *activity = [profile[@"activity"] mutableCopy];
        NSMutableArray *floors = [activity[@"floors"] mutableCopy];
        NSMutableDictionary *floor = [floors[1] mutableCopy]; floor[@"sequences"] = @[@[@2,@3]];
        floors[1] = floor; activity[@"floors"] = floors; profile[@"activity"] = activity;
        EightSelectProfile(session,profile); [session queueAutomatic]; EightWaitForSkill(session,engine);
        [session queueStop:@"pause sequence"];
        if (switchProfile) { floor[@"sequences"] = @[]; EightSelectProfile(session,profile); }
        for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
        [session queueAutomatic]; EightRunUntilRelease(session,engine,1);
        Check(engine->skillOrder == (switchProfile ? std::vector<NSUInteger>{1} : std::vector<NSUInteger>{1,2}) &&
            [session.snapshot[@"autoSkills"] unsignedIntegerValue] == (switchProfile ? 1u : 2u),
            "pending sequence resumes at the next confirmed step, or discards the old remainder on profile change");
        [session queueStop:@"fixture done"];
    }
    engine = EightEngineFixture(); engine->live.wave = 1; EightWaterWood(engine);
    session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    EightSelectProfile(session,EightProfileFixture()); [session queueAutomatic]; EightWaitForSkill(session,engine);
    // Receipt may arrive in phase22 before its final board. Retain both stages.
    engine->hangSkill = YES; engine->live.automation.skillUses = 1;
    engine->live.eightReceipt = {1001,1001726,1,0,true};
    Tick(session,engine,Frame()); [session queueStop:@"pause after early receipt"];
    Check([session.snapshot[@"eightProgress"][@"skillAcknowledged"] boolValue] &&
        ![session.snapshot[@"eightProgress"][@"skillSettled"] boolValue],"early effect receipt does not claim a settled board");
    engine->hangSkill = NO; engine->failSkillAck = YES; // Global count was recorded at effect start.
    for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
    Check(engine->eightProgressReads && [session.snapshot[@"eightProgress"][@"skillSettled"] boolValue],
        "paused observation continues until the final skill board is visible");
    [session queueAutomatic]; EightRunUntilRelease(session,engine,1);
    Check(engine->skillAttempts == 1 && [session.snapshot[@"autoSkills"] isEqual:@1],
        "early receipt plus late settled board resumes without repeating the native skill");
    [session queueStop:@"fixture done"];
    engine = EightEngineFixture(); engine->live.wave = 1; EightWaterWood(engine);
    session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    EightSelectProfile(session,EightProfileFixture()); [session queueAutomatic]; EightRunUntilRelease(session,engine,1);
    EightNextBoard(engine,false); EightWaterWood(engine); engine->live.automation.members[1].charge = 20;
    EightRunUntilRelease(session,engine,2);
    Check(engine->skillOrder == std::vector<NSUInteger>({1,1}) && [session.snapshot[@"autoSkills"] isEqual:@2],
        "same slot is reusable on a later turn only with a new total-use receipt");
    engine->live.phase = 68; Tick(session,engine,Frame());
    EightNextBoard(engine,false); ++engine->live.multiplayer.clockStart;
    for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
    Check(!engine->eightArmed && ![session.snapshot[@"automatic"] boolValue] && engine->releases == 2,
        "next match cannot inherit the retained pause history or action intent");
    [session queueAutomatic]; EightRunUntilRelease(session,engine,3);
    Check([session.snapshot[@"autoMoves"] isEqual:@1] && [session.snapshot[@"autoSkills"] isEqual:@0] &&
        [session.snapshot[@"eightProgress"][@"resumes"] isEqual:@0],"a new explicit match starts fresh counters");
    [session queueStop:@"fixture done"];

    // Wrong or stale native receipts never permit replay on a second click.
    for (unsigned fault = 1; fault <= 6; ++fault) {
        engine = EightEngineFixture(); engine->live.wave = 1; EightWaterWood(engine); engine->eightReceiptFault = fault;
        session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        EightSelectProfile(session,EightProfileFixture()); [session queueAutomatic]; EightWaitForSkill(session,engine);
        for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
        [session queueAutomatic]; for (unsigned i = 0; i < 80; ++i) Tick(session,engine,Frame());
        Check(engine->skillAttempts == 1 && !engine->stages && ![session.snapshot[@"automatic"] boolValue] &&
            [session.snapshot[@"eightSkillPending"] boolValue],"invalid root/slot/source/catalog/count remains pending without recast");
    }
}
static void EightSessionTests() {
    FakeOrbEngine *engine = EightEngineFixture(); PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Tick(session,engine,Frame()); [session configureQuality:@"fast"]; [session queueAutomatic];
    EightRunUntilRelease(session,engine,1);
    for (unsigned i = 0; i < 20; ++i) Tick(session,engine,Frame());
    Check(engine->releases == 1 && [session.snapshot[@"automatic"] boolValue],"resolve frames do not replay the released board");
    for (unsigned turn = 2; turn <= 7; ++turn) {
        engine->live.phase = 69; Tick(session,engine,Frame());
        Check([session.snapshot[@"automatic"] boolValue],"native 8P next-board phase 69 preserves the current match intent");
        EightNextBoard(engine,turn != 3); // Includes a second turn on the same floor.
        EightRunUntilRelease(session,engine,turn);
    }
    Check(engine->multiplayerArms == 7 && engine->beginFrames == 7 && !engine->skillAttempts,
        "one click authorizes fresh per-board gestures through six floors and a repeated floor");
    engine->live.multiplayer.finished = 1; Tick(session,engine,Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->eightArmed,"match result revokes Auto and skills");

    for (unsigned failure = 0; failure < 6; ++failure) {
        engine = EightEngineFixture(); session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session,engine,Frame()); [session queueAutomatic];
        if (failure == 0) ++engine->live.multiplayer.clockStart;
        if (failure == 1) engine->live.identity.entryHash[0]++;
        if (failure == 2) engine->live.multiplayer.viewTeam = 0;
        if (failure == 3) engine->live.input.rawActiveMask = 1;
        if (failure == 4) engine->live.multiplayer.remainingSeconds = 0;
        if (failure == 5) engine->live.multiplayer.connectionState = 0;
        Tick(session,engine,Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->skillAttempts,
            "8P match nonce, epoch, owner, touch, deadline or disconnect cancels before input");
    }
    for (unsigned fail = 0; fail < 3; ++fail) {
        engine = EightEngineFixture(); engine->live.wave = 1;
        // 15 water, 15 wood: fire/dark row impossible until Kagutsuchi.
        for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = uint8_t(i / 15 + 1);
        engine->failSkillAck = fail == 1; engine->rejectStartedSkill = fail == 2;
        session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        NSDictionary *profile = EightProfileFixture();
        Check([session configureProfiles:@{@"schema":@1,@"selected":profile[@"id"],@"autoSelect":@NO,@"profiles":@[profile]}],
            "8P portable activity can be selected before Auto");
        [session queueAutomatic];
        for (unsigned i = 0; i < 3000 && !engine->releases && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
        Check(engine->skillOrder == std::vector<NSUInteger>{1},"activity casts the required current skill exactly once");
        if (!fail) Check(engine->releases == 1 && [session.snapshot[@"autoSkills"] isEqual:@1],"skill receipt and changed board precede the gesture");
        else {
            for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
            [session queueAutomatic]; for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
            Check(engine->skillAttempts == 1 && !engine->releases && ![session.snapshot[@"automatic"] boolValue],
                "uncertain or missing skill receipt stays blocked across Stop/Auto without a duplicate cast");
        }
        [session queueStop:@"fixture done"];
    }
    EightResumeTests();
}
