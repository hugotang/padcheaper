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
    if (engine->releases != releases) std::fprintf(stderr,"8P state: %s\n",[[session.snapshot dictionaryWithValuesForKeys:
        @[@"phase",@"message",@"automatic",@"autoTurn",@"autoMoves",@"autoSkills",@"eightProgress"]].description UTF8String]);
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
static void EightPreparationBoard(FakeOrbEngine *engine) {
    engine->live.identity.dungeon = 6535; engine->live.identity.floor = 1; engine->live.wave = 5;
    engine->live.enemyCombo.turns[0] = 1; engine->live.enemyCombo.thresholds[0] = 5;
    const uint8_t cells[30]{0,1,2,3,4,3, 1,2,3,4,0,1, 2,3,4,2,3,4, 3,4,1,3,2,3, 4,5,4,2,3,1};
    std::copy(std::begin(cells),std::end(cells),engine->live.board.types);
    for (auto &m : engine->live.automation.members) m.charge = 0;
    engine->live.automation.members[1].charge = 4; engine->live.automation.members[2].charge = 3;
}
static void EightAfterPreparationBoard(FakeOrbEngine *engine) {
    EightNextBoard(engine,false); engine->live.enemyCombo.turns[0] = 0;
    // A fresh, ordinary board requiring the real pair of conversions; no
    // future generated cells are borrowed from the planning estimate.
    const uint8_t cells[30]{0,1,2,3,4,3, 1,2,3,4,0,1, 2,3,4,2,3,4, 3,4,1,3,2,3, 4,5,4,2,3,1};
    std::copy(std::begin(cells),std::end(cells),engine->live.board.types);
}
static void EightPreparationSessionTests() {
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        FakeOrbEngine *engine = EightEngineFixture(); EightPreparationBoard(engine);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        EightSelectProfile(session,EightGuideProfileFixture(YES)); [session queueAutomatic];
        EightRunUntilRelease(session,engine,1);
        NSDictionary *prepared = session.snapshot[@"eightProgress"][@"preparation"];
        NSDictionary *blocked = session.snapshot[@"eightProgress"][@"firstBlocked"];
        Check(!engine->skillAttempts && [prepared[@"releaseEvaluation"][@"preparationReady"] boolValue] &&
            [prepared[@"releaseEvaluation"][@"firstWaveCombos"] isEqual:@1] &&
            [session.snapshot[@"eightProgress"][@"preparationUsed"] boolValue] &&
            [blocked[@"board"][@"types"] count] == 30,
            "B6 performs one bounded reserve puzzle and retains its original board and CD decision");
        for (unsigned i = 0; i < 20; ++i) Tick(session,engine,Frame());
        Check(engine->releases == 1 && !engine->skillAttempts,"preparation waits for native resolution before casting or moving again");
        if (scenario == 1) [session queueStop:@"pause after preparation"];
        EightAfterPreparationBoard(engine);
        if (scenario == 2) engine->live.enemyCombo.turns[0] = 1;
        if (scenario == 3) engine->live.automation.members[2].charge = 2;
        Tick(session,engine,Frame());
        if (scenario == 1) [session queueAutomatic];
        if (scenario >= 2) {
            for (unsigned i = 0; i < 500 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
            Check(![session.snapshot[@"automatic"] boolValue] && !engine->skillAttempts && engine->releases == 1,
                "unexpected next-turn absorption or CD stops without a second preparation");
        } else {
            EightRunUntilRelease(session,engine,2);
            Check(engine->skillOrder == std::vector<NSUInteger>{1,2} && [session.snapshot[@"autoSkills"] isEqual:@2] &&
                [session.snapshot[@"eightProgress"][@"preparation"][@"status"] isEqual:@"next-turn-confirmed"] &&
                [session.snapshot[@"eightProgress"][@"firstBlocked"] isEqual:blocked],
                "native next turn and actual CD lead to fresh ordered skills, including after Stop/Auto");
        }
        [session queueStop:@"later stop"];
        Check([session.snapshot[@"eightProgress"][@"firstBlocked"] isEqual:blocked],"later stops cannot overwrite the original failure snapshot");
        EightAfterPreparationBoard(engine); ++engine->live.multiplayer.clockStart;
        Tick(session,engine,Frame()); [session queueAutomatic];
        Check(![session.snapshot[@"eightProgress"][@"preparationUsed"] boolValue] &&
            [session.snapshot[@"eightProgress"][@"firstBlocked"] count] == 0,"new match resets the bounded preparation and blocked evidence");
        [session queueStop:@"fixture done"];
    }
    FakeOrbEngine *engine = EightEngineFixture(); EightPreparationBoard(engine);
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    EightSelectProfile(session,EightGuideProfileFixture(NO)); [session queueAutomatic];
    for (unsigned i = 0; i < 300 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(!engine->skillAttempts && !engine->releases && ![session.snapshot[@"eightProgress"][@"preparationUsed"] boolValue],
        "guide off never adds a deliberate preparation turn");

    engine = EightEngineFixture(); session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    [session configureQuality:@"fast"]; [session queueOnce];
    for (unsigned i = 0; i < 3000 && !engine->releases && [session.snapshot[@"once"] boolValue]; ++i) Tick(session,engine,Frame());
    NSDictionary *evaluation = [session.snapshot[@"executionHistory"] lastObject][@"releaseEvaluation"];
    Check(engine->releases == 1 && !engine->skillAttempts && [evaluation[@"comboFirstWaveRequired"] boolValue] &&
        [evaluation[@"combos"] isEqual:evaluation[@"firstWaveCombos"]],"8P once uses first-removal accounting without casting activity skills");
    [session queueStop:@"fixture done"];
}
static void EightGuideSessionTests() {
    for (bool enabled : {false,true}) for (unsigned wave : {1u,2u,3u}) {
        FakeOrbEngine *engine = EightEngineFixture(); engine->live.wave = wave;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        EightSelectProfile(session,EightGuideProfileFixture(enabled)); [session queueAutomatic];
        EightRunUntilRelease(session,engine,1);
        const NSUInteger slot = wave == 3 ? 0 : wave;
        Check(engine->skillOrder == (enabled ? std::vector<NSUInteger>{slot} : std::vector<NSUInteger>{}),
            "guide enabled casts the floor opener before an already-solvable board; disabled keeps board-first behavior");
        EightNextBoard(engine,false); engine->live.automation.members[slot].charge = 20;
        EightRunUntilRelease(session,engine,2);
        Check(engine->skillAttempts == (enabled ? 1u : 0u),"a repeated floor with enough orbs does not replay its guide opener");
        [session queueStop:@"fixture done"];
    }
    for (unsigned pause = 0; pause < 3; ++pause) {
        FakeOrbEngine *engine = EightEngineFixture(); engine->live.wave = 4;
        for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = i % 6;
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
        EightSelectProfile(session,EightGuideProfileFixture(YES)); [session queueAutomatic]; EightWaitForSkill(session,engine);
        if (pause == 1) for (unsigned i = 0; i < 50 && [session.snapshot[@"autoSkills"] unsignedIntegerValue] < 1; ++i) Tick(session,engine,Frame());
        [session queueStop:@"pause guide prefix"];
        if (pause == 2) EightSelectProfile(session,EightGuideProfileFixture(NO));
        for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
        [session queueAutomatic]; EightRunUntilRelease(session,engine,1);
        Check(engine->skillOrder == (pause == 2 ? std::vector<NSUInteger>{3} : std::vector<NSUInteger>{3,5}) &&
            [session.snapshot[@"autoSkills"] unsignedIntegerValue] == engine->skillOrder.size(),
            "guide resumes after the confirmed Ney exactly once; disabling discards the unsent opener remainder");
        [session queueStop:@"fixture done"];
    }
    FakeOrbEngine *engine = EightEngineFixture(); engine->live.wave = 1; engine->live.automation.members[1].charge = 0;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    EightSelectProfile(session,EightGuideProfileFixture(YES)); [session queueAutomatic];
    for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->skillAttempts && !engine->stages &&
        [session.snapshot[@"message"] containsString:@"攻略指定技能"],"guide cooldown failure stops with a specific reason before any puzzle");
    engine->live.automation.members[1].charge = 20; Tick(session,engine,Frame()); [session queueAutomatic];
    EightRunUntilRelease(session,engine,1);
    Check(engine->skillOrder == std::vector<NSUInteger>{1},"fresh native readiness after a stop permits the previously unavailable guide opener");
    [session queueStop:@"fixture done"];

    engine = EightEngineFixture(); engine->live.wave = 1; engine->unavailableSkills[1] = YES;
    session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    EightSelectProfile(session,EightGuideProfileFixture(YES)); [session queueAutomatic];
    for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    [session queueAutomatic];
    for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(engine->skillAttempts == 1 && !engine->stages && ![session.snapshot[@"automatic"] boolValue],
        "native unavailable guide skill is not silently skipped or spammed across resume");

    // Toggle on after a confirmed off-mode rescue. The cast remains accounted
    // for even though the selected profile no longer compares equal.
    engine = EightEngineFixture(); engine->live.wave = 1; EightWaterWood(engine);
    session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    EightSelectProfile(session,EightGuideProfileFixture(NO)); [session queueAutomatic]; EightWaitForSkill(session,engine);
    [session queueStop:@"enable guide after cast"];
    EightSelectProfile(session,EightGuideProfileFixture(YES));
    for (unsigned i = 0; i < 25; ++i) Tick(session,engine,Frame());
    [session queueAutomatic]; EightRunUntilRelease(session,engine,1);
    Check(engine->skillOrder == std::vector<NSUInteger>{1} && [session.snapshot[@"autoSkills"] isEqual:@1],
        "turning the guide on retains a confirmed same-floor off-mode skill");
    [session queueStop:@"fixture done"];

    engine = EightEngineFixture();
    session = [[PHOrbSession alloc] initWithEngine:engine]; Tick(session,engine,Frame());
    EightSelectProfile(session,EightGuideProfileFixture(YES)); [session queueAutomatic]; EightRunUntilRelease(session,engine,1);
    NSDictionary *first = session.snapshot[@"eightProgress"][@"firstPuzzle"];
    Check([first[@"wave"] isEqual:@0] && [first[@"releaseConfirmed"] boolValue] &&
        [session.snapshot[@"searchTrace"][@"eightPolicy"][@"comboPriority"] boolValue],"B1 retains confirmed release evidence and the actual Combo policy");
    for (unsigned turn = 2; turn <= 4; ++turn) { EightNextBoard(engine,false); EightRunUntilRelease(session,engine,turn); }
    Check([session.snapshot[@"eightProgress"][@"firstPuzzle"] isEqual:first],"B1 evidence survives the last-three-executions history bound");
    [session queueStop:@"new match"];
    EightNextBoard(engine,false); ++engine->live.multiplayer.clockStart; engine->live.wave = 1;
    Tick(session,engine,Frame()); [session queueAutomatic]; EightRunUntilRelease(session,engine,5);
    Check([session.snapshot[@"eightProgress"][@"firstPuzzle"] count] == 0 &&
        [session.snapshot[@"autoMoves"] isEqual:@1] && engine->skillOrder == std::vector<NSUInteger>{1},
        "new explicit match resets retained B1 evidence and completed guide slots");
    [session queueStop:@"fixture done"];
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
        Check([session.snapshot[@"phase"] isEqual:@"stopped"],
            "Stop immediately after a confirmed release cannot revive an already-finished staged gesture");
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
    EightPreparationSessionTests();
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
    EightGuideSessionTests();
}
