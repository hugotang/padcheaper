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
static void EightSessionTests() {
    FakeOrbEngine *engine = EightEngineFixture(); PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Tick(session,engine,Frame()); [session configureQuality:@"fast"]; [session queueAutomatic];
    EightRunUntilRelease(session,engine,1);
    for (unsigned i = 0; i < 20; ++i) Tick(session,engine,Frame());
    Check(engine->releases == 1 && [session.snapshot[@"automatic"] boolValue],"resolve frames do not replay the released board");
    for (unsigned turn = 2; turn <= 7; ++turn) {
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
}
