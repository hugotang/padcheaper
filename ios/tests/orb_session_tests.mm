#import "PHOrbSession.h"
#import "PHOrbRules.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

static unsigned failures;
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

@interface FakeOrbEngine : NSObject <PHOrbEngine> {
@public
    PHOrbSnapshot live;
    NSUInteger stages, releases, cell, reads;
    BOOL down, beginning, staged, ignoreStart, ignoreMove, failRestore, corruptSwap, failRead, multiplayerLatched;
    BOOL replaceLeaderOnStart, replaceLeaderOnMove;
    BOOL unavailableSkills[6], hangSkill, failSkillAck, hasteFirst, changeBoardWithSkill;
    NSUInteger skillFrames, activeSkill, skillAttempts;
    std::vector<NSUInteger> skillOrder;
}
- (instancetype)initWithWidth:(unsigned)width height:(unsigned)height;
- (void)nativeFrame;
@end
@implementation FakeOrbEngine
- (instancetype)init { return [self initWithWidth:6 height:5]; }
- (instancetype)initWithWidth:(unsigned)width height:(unsigned)height {
    if ((self = [super init])) {
        live = {}; live.identity = {1,2,3,42,1,1,2,{1},0,0,0};
        live.inputManager = 4; live.present = live.valid = live.settled = live.ordinary = true;
        live.phase = 2; live.input.valid = true; live.minErase = 3; live.freeMovement = 1;
        live.remainingFrames = 300; live.fps = 60; live.board.width = uint8_t(width); live.board.height = uint8_t(height);
        live.automation.valid = live.automation.window = live.automation.enemiesKnown = live.automation.boardKnown = true;
        live.automation.activeEnemies = 1;
        for (unsigned i = 0; i < 6; ++i) {
            live.automation.members[i].active = true; live.automation.members[i].number = uint16_t(100 + i);
            live.automation.members[i].charge = 5;
        }
        uint32_t random = 731;
        for (unsigned i = 0; i < width * height; ++i) {
            random = random * 1664525u + 1013904223u;
            live.board.types[i] = uint8_t((random >> 12) % 6); live.board.tokens[i] = 100 + i;
            live.board.x[i] = int16_t(50 + i % width * 100); live.board.y[i] = int16_t(400 + i / width * 100);
        }
    }
    return self;
}
- (BOOL)read:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++reads;
    if (!frame.ready || staged || failRead) return NO;
    if (!live.present) multiplayerLatched = NO;
    if (live.multiplayerDisabled) multiplayerLatched = YES;
    *state = live; state->multiplayerDisabled |= multiplayerLatched; return YES;
}
- (BOOL)readAutomation:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    return [self read:state frame:frame reason:reason];
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!frame.ready || !frame.enabled || slot >= 6 || staged || multiplayerLatched || live.multiplayerDisabled ||
        !PHBattleIsSinglePlayer(live.identity.entryMode, live.identity.multiplayer) ||
        !PHOrbSameAutomation(&live, expected) || live.phase != 2 || !PHInputIsQuiet(&live.input)) return PHOrbSkillRejected;
    ++skillAttempts;
    if (unavailableSkills[slot]) return PHOrbSkillUnavailable;
    Check(!live.dragged && !staged && !live.automation.busy, "skills and orb gestures never overlap");
    activeSkill = slot; skillOrder.push_back(slot); skillFrames = 4;
    live.phase = 22; live.automation.members[slot].state = 2; live.automation.busy = true;
    return PHOrbSkillStarted;
}
- (BOOL)stageCell:(NSUInteger)target down:(BOOL)hold beginning:(BOOL)begin
    expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (staged || !frame.ready || (hold && !frame.enabled) || live.multiplayerDisabled || multiplayerLatched ||
        !PHOrbSameTurn(&live, expected) || !PHOrbSameBoard(&live.board, &expected->board) ||
        (hold && !PHOrbSameLeaders(&live, expected)) ||
        (hold && !PHInputIsQuiet(&live.input)) || target >= live.board.width * live.board.height) return NO;
    ++stages; cell = target; down = hold; beginning = begin; staged = YES;
    if (!down) ++releases;
    return YES;
}
- (BOOL)finishInput:(NSString **)reason {
    if (!staged) return YES;
    if (failRestore) { failRestore = NO; if (reason) *reason = @"fixture restoration failure"; return NO; }
    staged = NO; return YES;
}
- (void)nativeFrame {
    if (skillFrames) {
        Check(!staged, "no synthetic input is staged during a native skill");
        if (hangSkill || --skillFrames) return;
        live.phase = 2; live.automation.members[activeSkill].state = 0; live.automation.busy = false;
        if (!failSkillAck) ++live.automation.skillUses;
        if (hasteFirst) unavailableSkills[0] = NO;
        if (changeBoardWithSkill) for (unsigned i = 0; i < live.board.width * live.board.height; ++i) live.board.types[i] = uint8_t(i % 5);
        return;
    }
    if (!staged) return;
    if (!down) { live.dragged = 0; live.settled = true; live.phase = 3; return; }
    if (beginning) {
        if (!ignoreStart) { live.dragged = live.board.tokens[cell]; live.settled = false; }
        if (replaceLeaderOnStart) { live.leaderNumbers[0] = 14242; replaceLeaderOnStart = NO; }
        return;
    }
    if (ignoreMove) return;
    const unsigned cells = live.board.width * live.board.height;
    unsigned from = 0;
    while (from < cells && live.board.tokens[from] != live.dragged) ++from;
    if (from == cell || from == cells) return;
    Check(PHOrbAdjacent(from, unsigned(cell), live.board.width, live.board.height), "executor never skips to a nonadjacent cell");
    std::swap(live.board.tokens[from], live.board.tokens[cell]); std::swap(live.board.types[from], live.board.types[cell]);
    std::swap(live.board.flags[from], live.board.flags[cell]); std::swap(live.board.enhancement[from], live.board.enhancement[cell]);
    ++live.moves;
    if (replaceLeaderOnMove) { live.leaderNumbers[0] = 14242; replaceLeaderOnMove = NO; }
    if (corruptSwap) live.board.types[cells - 1] = uint8_t((live.board.types[cells - 1] + 1) % 6);
}
@end

static double now = 100;
static PHBattleFrame Frame() { PHBattleFrame frame{}; frame.header = 1; frame.player = 42; frame.epoch = 1; frame.ready = frame.enabled = true; return frame; }
static void Tick(PHOrbSession *session, FakeOrbEngine *engine, PHBattleFrame frame) {
    now += 0.02;
    [session beforeFrame:frame time:now];
    @try { [engine nativeFrame]; } @finally { [session finishNativeFrame]; }
    [session afterFrame:frame time:now];
    [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
}
static bool Preview(PHOrbSession *session, FakeOrbEngine *engine) {
    Tick(session, engine, Frame()); [session queueRead];
    for (unsigned i = 0; i < 2000; ++i) {
        Tick(session, engine, Frame());
        if ([[session snapshot][@"phase"] isEqual:@"preview"]) return true;
        if ([[session snapshot][@"phase"] isEqual:@"blocked"] || [[session snapshot][@"phase"] isEqual:@"fault"]) return false;
    }
    return false;
}
static void Execute(PHOrbSession *session) { [session queueExecute:[[session snapshot][@"generation"] unsignedIntegerValue]]; }
static void UntilDragging(PHOrbSession *session, FakeOrbEngine *engine) {
    for (unsigned i = 0; i < 100 && !engine->live.dragged; ++i) Tick(session, engine, Frame());
    Check(engine->live.dragged != 0, "native accepted start fixture");
}

#include "orb_automation_session_tests.inc"

int main() {
    @autoreleasepool {
        FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        [session queueStop:@"idle background"];
        Check(engine->reads == 0 && [[session snapshot][@"cancelled"] unsignedIntegerValue] == 0,
            "unused feature neither polls the board nor counts idle cancellation");
        Check(Preview(session, engine), "read -> background solve -> preview");
        Check(engine->stages == 0, "preview never stages input");
        auto generation = [[session snapshot][@"generation"] unsignedIntegerValue];
        [session queueExecute:generation + 1];
        Check([[session snapshot][@"phase"] isEqual:@"preview"], "stale UI generation cannot execute a different preview");
        Execute(session);
        for (unsigned i = 0; i < 250 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"complete"] && engine->releases == 1 && !engine->live.dragged,
              "each swap acknowledged, explicit release and completed-once");
        NSUInteger stages = engine->stages;
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages && [[session snapshot][@"completed"] unsignedIntegerValue] == 1, "no silent next-turn autoplay");

        for (auto dimensions : {std::pair<unsigned, unsigned>{7,6}, {5,4}}) {
            engine = [[FakeOrbEngine alloc] initWithWidth:dimensions.first height:dimensions.second];
            session = [[PHOrbSession alloc] initWithEngine:engine];
            Check(Preview(session, engine), "expanded board can solve and preview");
            Execute(session);
            for (unsigned i = 0; i < 250 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
            Check([[session snapshot][@"phase"] isEqual:@"complete"] && engine->releases == 1,
                "expanded-board route acknowledges every swap and releases once");
        }

        engine = [[FakeOrbEngine alloc] initWithWidth:7 height:6]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "last-cell manual-release setup"); Execute(session); UntilDragging(session, engine);
        unsigned from = 0;
        while (from < 42 && engine->live.board.tokens[from] != engine->live.dragged) ++from;
        if (from >= 42) return 1;
        std::swap(engine->live.board.tokens[from], engine->live.board.tokens[41]);
        std::swap(engine->live.board.types[from], engine->live.board.types[41]);
        engine->live.input.rawActiveMask = 1;
        Tick(session, engine, Frame());
        Check(engine->releases == 1 && engine->cell == 41 && !engine->live.dragged,
            "manual stop releases the owned orb in the extra row, including the final cell");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "resize during execution setup"); Execute(session); UntilDragging(session, engine);
        stages = engine->stages; engine->live.board.width = 5; engine->live.board.height = 4;
        Tick(session, engine, Frame());
        Check(engine->stages == stages && ![[session snapshot][@"busy"] boolValue],
            "board resize cancels old input without injecting cleanup into the new geometry");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(![session configureQuality:nil] && ![session configureQuality:@"unlimited"], "unknown quality is rejected");
        Check(Preview(session, engine), "quality-change preview setup");
        generation = [[session snapshot][@"generation"] unsignedIntegerValue];
        Check([session configureQuality:@"fast"], "saved quality can change while idle or previewing");
        [session queueExecute:generation];
        Check(![[session snapshot][@"canExecute"] boolValue] && [[session snapshot][@"path"] count] == 0 && engine->stages == 0,
            "quality change invalidates the previous route and execute token");
        Check(Preview(session, engine) && [[session snapshot][@"quality"] isEqual:@"fast"] &&
            [[session snapshot][@"steps"] unsignedIntegerValue] <= 24, "fast setting is used by the next solver job");
        Execute(session);
        Check(![session configureQuality:@"thorough"] && [[session snapshot][@"quality"] isEqual:@"fast"],
            "quality cannot rewrite the options of an already armed gesture");
        [session queueStop:@"end quality fixture"];

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "goal-change preview setup");
        generation = [[session snapshot][@"generation"] unsignedIntegerValue];
        NSArray *simpleGoal = @[@{@"kind":@"combo", @"colors":@0, @"count":@1, @"length":@0}];
        NSDictionary *manualSettings = @{@"quality":@"fast", @"mode":@"manual", @"goals":simpleGoal};
        Check([session configureSettings:manualSettings], "valid goal settings are accepted as one transaction");
        [session queueExecute:generation];
        Check(engine->stages == 0 && [[session snapshot][@"path"] count] == 0,
              "changing a goal invalidates the previous executable generation");
        Check(Preview(session, engine) && [[session snapshot][@"goalsMet"] boolValue], "new solver job uses the captured target");
        Execute(session);
        Check(![session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[]}],
              "an armed gesture cannot change its target mid-execution");
        [session queueStop:@"end goal fixture"];

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        [session configureSettings:@{@"quality":@"thorough", @"mode":@"manual", @"goals":simpleGoal}];
        Tick(session, engine, Frame()); [session queueRead];
        for (unsigned i = 0; i < 100 && ![[session snapshot][@"phase"] isEqual:@"solving"]; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"solving"], "in-flight goal cancellation setup");
        generation = [[session snapshot][@"generation"] unsignedIntegerValue];
        [session configureSettings:@{@"quality":@"balanced", @"mode":@"combo", @"goals":@[]}];
        for (unsigned i = 0; i < 100; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"idle"] && [[session snapshot][@"generation"] unsignedIntegerValue] != generation &&
              [[session snapshot][@"path"] count] == 0 && engine->stages == 0,
              "an old goal worker completion cannot publish a route after settings change");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = uint8_t(1 + (i % 6 + 2 * (i / 6)) % 4);
        for (unsigned i : {0u,1u,2u,6u,7u,8u,12u,13u,14u}) engine->live.board.types[i] = 0;
        NSArray *conflict = @[@{@"kind":@"square", @"colors":@1, @"count":@1, @"length":@0},
            @{@"kind":@"l", @"colors":@1, @"count":@1, @"length":@0}];
        [session configureSettings:@{@"quality":@"fast", @"mode":@"manual", @"goals":conflict}];
        Check(Preview(session, engine) && ![[session snapshot][@"goalsMet"] boolValue] &&
              ![[session snapshot][@"canExecute"] boolValue] && [[session snapshot][@"goalCount"] unsignedIntegerValue] == 2,
              "conflicting shapes yield a labelled non-executable partial preview");
        Execute(session); for (unsigned i = 0; i < 30; ++i) Tick(session, engine, Frame());
        Check(engine->stages == 0, "even a direct execute request cannot bypass unmet goals");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        NSDictionary *leaderSettings = @{@"quality":@"fast", @"mode":@"leaders", @"goals":@[]};
        [session configureSettings:leaderSettings];
        Check(!Preview(session, engine) && [[session snapshot][@"phase"] isEqual:@"blocked"] && engine->stages == 0,
              "leader mode requires live validated IDs and never falls back to ordinary autoplay");
        engine->live.leadersValid = true; engine->live.leaderNumbers[0] = engine->live.leaderNumbers[1] = 1;
        Check(Preview(session, engine) && [[session snapshot][@"goalPlan"][@"available"] boolValue],
              "live leaders without board conditions use combo priority with explicit scope");
        generation = [[session snapshot][@"generation"] unsignedIntegerValue];
        engine->live.leaderNumbers[0] = 14242;
        for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
        [session queueExecute:generation];
        Check(engine->stages == 0 && ![[session snapshot][@"canExecute"] boolValue],
              "leader change invalidates a stale preview even when all orb tokens remain identical");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        engine->live.leadersValid = true; engine->live.leaderNumbers[0] = engine->live.leaderNumbers[1] = 1;
        [session configureSettings:leaderSettings];
        Check(Preview(session, engine), "leader change during gesture setup"); Execute(session); UntilDragging(session, engine);
        stages = engine->stages; engine->live.leaderNumbers[0] = 14242;
        Tick(session, engine, Frame());
        Check(engine->stages == stages + 1 && engine->releases == 1 && !engine->down && !engine->live.dragged &&
              [[session snapshot][@"completed"] unsignedIntegerValue] == 0,
              "mid-gesture leader change sends only the owned release and never another route step");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        engine->live.leadersValid = true; engine->live.leaderNumbers[0] = engine->live.leaderNumbers[1] = 1;
        [session configureSettings:leaderSettings];
        Check(Preview(session, engine), "leader change inside native start setup");
        engine->replaceLeaderOnStart = YES; Execute(session);
        for (unsigned i = 0; i < 120 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check(!engine->replaceLeaderOnStart && engine->live.moves == 0 && engine->releases == 1 && !engine->live.dragged &&
              [[session snapshot][@"completed"] unsignedIntegerValue] == 0,
              "leader change inside an accepted native start releases the owned orb without beginning the old route");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        engine->live.leadersValid = true; engine->live.leaderNumbers[0] = engine->live.leaderNumbers[1] = 1;
        [session configureSettings:leaderSettings];
        Check(Preview(session, engine), "leader change inside native swap setup"); Execute(session); UntilDragging(session, engine);
        engine->replaceLeaderOnMove = YES;
        for (unsigned i = 0; i < 120 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check(!engine->replaceLeaderOnMove && engine->live.moves == 1 && engine->releases == 1 && !engine->live.dragged &&
              [[session snapshot][@"confirmed"] unsignedIntegerValue] == 0 &&
              [[session snapshot][@"completed"] unsignedIntegerValue] == 0,
              "an accepted native swap cannot replace the plan's leader identity and continue the old route");

        engine = [[FakeOrbEngine alloc] initWithWidth:7 height:6]; session = [[PHOrbSession alloc] initWithEngine:engine];
        [session configureQuality:@"thorough"]; engine->live.fps = 20; engine->live.remainingFrames = 200;
        Check(Preview(session, engine), "thorough profile with lower native frame rate");
        Check([[session snapshot][@"searchSteps"] unsignedIntegerValue] * 0.1 + 0.4 <= 4.000001 &&
            [[session snapshot][@"searchBudgetMS"] unsignedIntegerValue] == 800,
            "thorough search respects the frame-quantized execution deadline rather than only the move timer");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "manual-interruption setup"); Execute(session); UntilDragging(session, engine);
        engine->live.input.rawActiveMask = 1;
        for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
        Check(!engine->live.dragged && engine->releases == 1 && engine->live.input.rawActiveMask == 1 &&
            [[session snapshot][@"completed"] unsignedIntegerValue] == 0, "manual input cancels and remains intact after own release");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "stale-board setup");
        std::swap(engine->live.board.tokens[0], engine->live.board.tokens[1]);
        for (unsigned i = 0; i < 8; ++i) Tick(session, engine, Frame());
        Execute(session); Check(engine->stages == 0 && ![[session snapshot][@"canExecute"] boolValue], "changed token ordering invalidates preview");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "multi transition setup"); Execute(session); UntilDragging(session, engine);
        stages = engine->stages; engine->live.multiplayerDisabled = true; engine->live.identity.multiplayer = 1;
        Tick(session, engine, Frame());
        Check(engine->stages == stages && ![[session snapshot][@"busy"] boolValue], "multiplayer transition never receives cleanup input");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        engine->live.multiplayerDisabled = true; engine->live.identity.multiplayer = 1;
        Tick(session, engine, Frame()); [session queueRead];
        for (unsigned i = 0; i < 120; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"blocked"] && engine->multiplayerLatched,
            "multiplayer read remains blocked after request timeout");
        engine->live.multiplayerDisabled = false; engine->live.identity.multiplayer = 0;
        [session queueRead];
        for (unsigned i = 0; i < 120; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"blocked"] && engine->multiplayerLatched,
            "selector reset and a new read cannot clear the multiplayer latch");
        engine->failRead = YES;
        for (unsigned i = 0; i < 8; ++i) Tick(session, engine, Frame());
        engine->failRead = NO; engine->live.present = false; engine->live.valid = false;
        for (unsigned i = 0; i < 8; ++i) Tick(session, engine, Frame());
        Check(!engine->multiplayerLatched, "scene exit is still observed after a transient read failure");
        engine->live.present = engine->live.valid = true;
        Check(Preview(session, engine) && engine->stages == 0,
            "a new solo battle can preview after the observed multiplayer exit without injecting input");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "ignored native move setup"); Execute(session); UntilDragging(session, engine); engine->ignoreMove = YES;
        for (unsigned i = 0; i < 100 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check(engine->releases == 1 && engine->live.moves == 0 && [[session snapshot][@"completed"] unsignedIntegerValue] == 0,
              "unacknowledged movement times out instead of skipping steps");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "corrupted native swap setup"); Execute(session); UntilDragging(session, engine); engine->corruptSwap = YES;
        for (unsigned i = 0; i < 100 && [[session snapshot][@"busy"] boolValue]; ++i) Tick(session, engine, Frame());
        Check(engine->releases == 1 && [[session snapshot][@"completed"] unsignedIntegerValue] == 0, "mismatched board stops and releases at actual cell");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "background setup"); Execute(session); UntilDragging(session, engine);
        stages = engine->stages; [session queueStop:@"background"]; PHBattleFrame background = Frame(); background.ready = false;
        Tick(session, engine, background); engine->live.dragged = 0;
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages, "background cancellation does not resume path");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "restore failure setup"); Execute(session); engine->failRestore = YES;
        for (unsigned i = 0; i < 35; ++i) Tick(session, engine, Frame());
        stages = engine->stages;
        Check([[session snapshot][@"inputFault"] boolValue], "input restoration failure is visible");
        [session queueRead]; for (unsigned i = 0; i < 10; ++i) Tick(session, engine, Frame());
        Check(engine->stages == stages && ![[session snapshot][@"canExecute"] boolValue], "restoration fault prevents further injection");

        engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
        Tick(session, engine, Frame()); [session queueRead];
        for (unsigned i = 0; i < 20; ++i) Tick(session, engine, Frame());
        [session queueStop:@"cancel worker"];
        for (unsigned i = 0; i < 400; ++i) Tick(session, engine, Frame());
        Check([[session snapshot][@"phase"] isEqual:@"stopped"] && engine->stages == 0, "late solver completion cannot revive canceled intent");
        AutomationSessionTests();
        std::printf("Orb session tests: %s (preview, execute once, continuous turns, skills, cancellation and acknowledgments)\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
