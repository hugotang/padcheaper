#import "PHOrbSession.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#import "PHOrbPlanning.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

static unsigned failures;
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

@interface FakeOrbEngine : NSObject <PHOrbEngine> {
@public
    PHOrbSnapshot live;
    NSUInteger stages, releases, cell, reads, beginFrames;
    BOOL down, beginning, staged, ignoreStart, requireHoldForStart, ignoreMove, failRestore, corruptSwap, failRead, multiplayerLatched;
    BOOL replaceLeaderOnStart, replaceLeaderOnMove;
    BOOL wrongDiagonalTarget, closeWindowInUse;
    BOOL multiplayerArmed, ignoreRelease;
    NSUInteger multiplayerArms;
    BOOL unavailableSkills[6], hangSkill, failSkillAck, hasteFirst, changeBoardWithSkill, clearObstaclesWithSkill;
    BOOL healWithSkill, clearPlayerStatusWithSkill, dropHPOnStart, dropHPOnMove, rejectStartedSkill, makeHeartsWithSkill;
    BOOL tickSpinners, createSpinnerWithSkill, removeSpinnerWithSkill;
    BOOL requireNineLight, omitResolvingBoard, multiplayerReleaseMetadata;
    BOOL resizeWithSkill, hangResize;
    PHOrbSkillSource chosenSource[6];
    uint32_t chosenSkill[6];
    uint16_t transformAfterSkill[6];
    BOOL consumeAssistAfterSkill[6];
    unsigned lightAfterByakuya;
    NSUInteger skillFrames, resizeFrames, activeSkill, skillAttempts;
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
        live.automation.windowBlocksKnown = true;
        live.automation.hpKnown = live.automation.statusKnown = true;
        live.automation.hp = live.automation.maximumHP = 1000;
        live.thorns = {1000,1000,0,true}; live.unmatchableKnown = true;
        live.automation.activeEnemies = 1;
        for (unsigned i = 0; i < 6; ++i) {
            live.automation.members[i].active = true; live.automation.members[i].number = uint16_t(100 + i);
            live.automation.members[i].charge = 5;
            live.automation.members[i].selection = UINT32_MAX;
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
    if (!frame.ready || staged || failRead) {
        *state = {};
        state->multiplayer.kind = live.multiplayer.kind;
        state->multiplayerDisabled = live.multiplayerDisabled || multiplayerLatched;
        return NO;
    }
    if (!live.present) multiplayerLatched = NO;
    if (live.multiplayerDisabled) multiplayerLatched = YES;
    *state = live; state->multiplayerDisabled |= multiplayerLatched;
    const bool multiplayerMetadata = live.multiplayer.kind != PHOrbMultiplayerNone && !PHOrbMultiplayerWindow(&live);
    if ((omitResolvingBoard && live.phase != 2) || live.boardTransition || multiplayerMetadata) {
        std::fill(std::begin(state->board.types), std::end(state->board.types), 0);
        std::fill(std::begin(state->board.tokens), std::end(state->board.tokens), 0);
        if (multiplayerMetadata) state->ordinary = state->settled = false;
        if (live.boardTransition) {
            state->ordinary = state->settled = state->automation.window = state->automation.boardKnown = false;
            state->automation.busy = true;
            if (reason) *reason = @"等待原生改盤動畫完成";
        }
    }
    return YES;
}
- (BOOL)readAutomation:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    return [self read:state frame:frame reason:reason];
}
- (void)armMultiplayerOnce:(BOOL)armed {
    if (armed && !multiplayerArmed) ++multiplayerArms;
    multiplayerArmed = armed;
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (closeWindowInUse) {
        closeWindowInUse = NO; live.automation.window = false;
        live.automation.windowBlocks = PHOrbSkillWindowInput;
    }
    if (!frame.ready || !frame.enabled || slot >= 6 || staged || multiplayerLatched || live.multiplayerDisabled ||
        !PHBattleIsSinglePlayer(live.identity.entryMode, live.identity.multiplayer) ||
        live.phase != 2 || !live.settled || live.dragged || !PHInputIsQuiet(&live.input)) return PHOrbSkillRejected;
    if (!live.automation.window && PHOrbSameAutomationExceptWindow(expected, &live)) {
        if (reason) *reason = @"技能窗口關閉：原生操作尚未結束";
        return PHOrbSkillWindowClosed;
    }
    if (!PHOrbSameAutomation(&live, expected)) return PHOrbSkillRejected;
    ++skillAttempts;
    const PHOrbSkillMember &member = live.automation.members[slot];
    if (unavailableSkills[slot] || live.automation.busy || !member.active ||
        member.bound || member.state) return PHOrbSkillUnavailable;
    if (requireNineLight && slot == 0) {
        unsigned light = 0;
        for (unsigned i = 0; i < live.board.width * live.board.height; ++i) light += live.board.types[i] == 3;
        if (light < 9) {
            if (reason) *reason = [NSString stringWithFormat:@"本體使用條件未滿足：光珠至少 9 顆，目前 %u 顆", light];
            return PHOrbSkillUnavailable;
        }
    }
    Check(!live.dragged && !staged && !live.automation.busy, "skills and orb gestures never overlap");
    activeSkill = slot; skillOrder.push_back(slot); skillFrames = 4;
    live.automation.members[slot].selection = UINT32_MAX;
    live.phase = 22; live.automation.members[slot].state = 2; live.automation.busy = true;
    return rejectStartedSkill ? PHOrbSkillRejected : PHOrbSkillStarted;
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot request:(PHOrbSkillRequest)request expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (slot >= 6) return PHOrbSkillRejected;
    if ((request.source != PHOrbSkillAutomatic && request.source != chosenSource[slot]) ||
        (request.skill && request.skill != chosenSkill[slot])) {
        if (reason) *reason = @"fixture source or skill mismatch";
        return PHOrbSkillUnavailable;
    }
    return [self useSkill:slot expected:expected frame:frame reason:reason];
}
- (BOOL)stageCell:(NSUInteger)target down:(BOOL)hold beginning:(BOOL)begin
    expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (staged || !frame.ready || (hold && !frame.enabled) || live.multiplayerDisabled || multiplayerLatched ||
        (!PHBattleIsSinglePlayer(live.identity.entryMode, live.identity.multiplayer) &&
            (!PHOrbMultiplayerWindow(&live) || (hold && !multiplayerArmed))) ||
        !PHOrbSameTurn(&live, expected) || !PHOrbSamePlanBoard(&live.board, &expected->board) ||
        (hold && !PHOrbSameLeaders(&live, expected)) ||
        (hold && !PHInputIsQuiet(&live.input)) || target >= live.board.width * live.board.height ||
        (hold && (PHOrbBlockedCells(&live.board) & (UINT64_C(1) << target))) ||
        (hold && !PHOrbCanEnterThorn(&live,unsigned(target))) ||
        (hold && !live.dragged && !PHOrbCanStartAt(&live.board, unsigned(target)))) return NO;
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
    if (resizeFrames) {
        Check(!staged,"no synthetic input is staged during phase-2 native resizing");
        live.resizeProgress = resizeFrames == 1 ? 1 : 0.5f;
        if (hangResize || --resizeFrames) return;
        live.board.width = 7; live.board.height = 6;
        for (unsigned i = 0; i < 42; ++i) {
            live.board.types[i] = uint8_t((i * 7 + i / 7) % 6); live.board.tokens[i] = 200 + i;
            live.board.x[i] = int16_t(50 + i % 7 * 90); live.board.y[i] = int16_t(400 + i / 7 * 90);
        }
        live.resizeProgress = live.resizeTarget = 0; live.boardTransition = false;
        live.settled = live.ordinary = live.automation.window = live.automation.boardKnown = true;
        live.automation.busy = false;
        return;
    }
    if (skillFrames) {
        Check(!staged, "no synthetic input is staged during a native skill");
        if (hangSkill || --skillFrames) return;
        live.phase = 2; live.automation.members[activeSkill].state = 0; live.automation.busy = false;
        if (transformAfterSkill[activeSkill]) {
            live.automation.members[activeSkill].number = transformAfterSkill[activeSkill];
            transformAfterSkill[activeSkill] = 0;
        }
        if (consumeAssistAfterSkill[activeSkill] && chosenSource[activeSkill] == PHOrbSkillAssist) {
            live.automation.members[activeSkill].assistCatalogID = 0;
            live.automation.members[activeSkill].assistState = 0;
            consumeAssistAfterSkill[activeSkill] = NO;
        }
        chosenSource[activeSkill] = PHOrbSkillBody;
        if (!failSkillAck) ++live.automation.skillUses;
        if (hasteFirst) unavailableSkills[0] = NO;
        if (changeBoardWithSkill) for (unsigned i = 0; i < live.board.width * live.board.height; ++i) live.board.types[i] = uint8_t(i % 5);
        if (lightAfterByakuya && activeSkill == 4)
            for (unsigned i = 0; i < live.board.width * live.board.height; ++i) live.board.types[i] = i < lightAfterByakuya ? 3 : 5;
        if (clearObstaclesWithSkill) { live.obstacles = 0; live.ordinary = true; }
        if (createSpinnerWithSkill) {
            live.board.spinnerMask = 1 | (UINT64_C(1) << live.board.width); live.board.spinnerColors = 63;
            live.spinnerTurns = 4; live.spinnerPeriodFrames = 30; tickSpinners = YES;
        }
        if (removeSpinnerWithSkill) {
            live.board.spinnerMask = live.board.spinnerColors = live.spinnerTurns = live.spinnerPeriodFrames = 0;
            live.obstacles = 0; live.ordinary = true; tickSpinners = NO;
        }
        if (healWithSkill) live.automation.hp = live.automation.maximumHP;
        if (makeHeartsWithSkill) for (unsigned i = 0; i < 4; ++i) live.board.types[i] = 5;
        if (clearPlayerStatusWithSkill) {
            live.automation.playerEffects = 0;
            for (auto &member : live.automation.members) member.bound = false;
        }
        if (resizeWithSkill && chosenSkill[activeSkill] == 63581) {
            resizeFrames = 8; live.boardTransition = true; live.resizeTarget = 1;
            live.settled = false; live.automation.busy = true;
        }
        return;
    }
    if (tickSpinners && live.phase == 2) {
        // Independent fake of the native shared countdown, deliberately fast
        // enough to cross colors during stability sampling and every gesture.
        for (unsigned i = 0; i < live.board.width * live.board.height; ++i)
            if ((live.board.spinnerMask & (UINT64_C(1) << i)) && !(live.board.flags[i] & PHOrbFlagLocked))
                for (unsigned next = 1; next <= PHOrbTypes; ++next) {
                    unsigned color = (live.board.types[i] + next) % PHOrbTypes;
                    if (live.board.spinnerColors & (1u << color)) { live.board.types[i] = uint8_t(color); break; }
                }
    }
    if (!staged) return;
    if (!down) {
        if (ignoreRelease) return;
        if (multiplayerReleaseMetadata && live.multiplayer.kind != PHOrbMultiplayerNone) {
            live.dragged = 0; live.settled = false; live.phase = 2;
            live.multiplayer.resolvePending = 1; live.multiplayer.networkFlags |= 2u;
            return;
        }
        live.dragged = 0; live.settled = true; live.phase = 3; return;
    }
    if (beginning) {
        ++beginFrames;
        if (requireHoldForStart) return; // Native tall-layout began gate.
    }
    if (beginning || !live.dragged) {
        if (!ignoreStart) {
            live.dragged = live.board.tokens[cell]; live.settled = false;
            for (auto &flags : live.board.flags) flags &= ~PHOrbFlagStartDim;
        }
        if (replaceLeaderOnStart) { live.leaderNumbers[0] = 14242; replaceLeaderOnStart = NO; }
        if (dropHPOnStart) { live.automation.hp = 100; dropHPOnStart = NO; }
        return;
    }
    if (ignoreMove) return;
    const unsigned cells = live.board.width * live.board.height;
    unsigned from = 0;
    while (from < cells && live.board.tokens[from] != live.dragged) ++from;
    if (from == cell || from == cells) return;
    const int dx = int(from % live.board.width) - int(cell % live.board.width);
    const int dy = int(from / live.board.width) - int(cell / live.board.width);
    Check(std::abs(dx) <= 1 && std::abs(dy) <= 1 && (dx || dy), "executor only sends a neighboring cell center");
    if (wrongDiagonalTarget && dx && dy) {
        cell = from / live.board.width * live.board.width + cell % live.board.width;
    }
    if ((live.board.flags[cell] & PHOrbFlagThorn) && live.board.thornDamage[cell])
        live.thorns.pendingDamage += PHOrbThornDamage(live.board.thornDamage[cell],live.thorns.maximumHP);
    std::swap(live.board.tokens[from], live.board.tokens[cell]); std::swap(live.board.types[from], live.board.types[cell]);
    std::swap(live.board.flags[from], live.board.flags[cell]); std::swap(live.board.enhancement[from], live.board.enhancement[cell]);
    std::swap(live.board.blindTurns[from], live.board.blindTurns[cell]); std::swap(live.board.thornDamage[from], live.board.thornDamage[cell]);
    live.board.flags[from] &= ~PHOrbFlagBlindTouch; live.board.flags[cell] &= ~PHOrbFlagBlindTouch;
    ++live.moves;
    if (dropHPOnMove) { live.automation.hp = 100; dropHPOnMove = NO; }
    if (replaceLeaderOnMove) { live.leaderNumbers[0] = 14242; replaceLeaderOnMove = NO; }
    if (corruptSwap) live.board.types[cells - 1] = uint8_t((live.board.types[cells - 1] + 1) % 6);
}
@end

static double now = 100;
static PHBattleFrame Frame() { PHBattleFrame frame{}; frame.header = 1; frame.player = 42; frame.epoch = 1; frame.ready = frame.enabled = true; return frame; }
static void Tick(PHOrbSession *session, FakeOrbEngine *engine, PHBattleFrame frame, double interval = 0.02,
                 double nativeDuration = 0, bool dispatchCallbacks = true) {
    now += interval;
    [session beforeFrame:frame time:now];
    @try { [engine nativeFrame]; } @finally { [session finishNativeFrame]; }
    // Runtime samples separate uptimes before and after rendering/maintenance.
    [session afterFrame:frame time:now + nativeDuration];
    if (dispatchCallbacks) [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
}
static bool UntilSolveStarts(PHOrbSession *session, FakeOrbEngine *engine) {
    // Observe the synchronous transition before servicing worker completions
    // on the main queue. A fast solve may otherwise publish its preview inside
    // Tick's run-loop drain, so polling only after Tick can miss "solving".
    // After cancellation the ordinary ticks drain all queued completions.
    for (unsigned i = 0; i < 100; ++i) {
        Tick(session, engine, Frame(), 0.02, 0, false);
        if ([session.snapshot[@"phase"] isEqual:@"solving"]) return true;
    }
    return false;
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
#include "orb_strategy_session_tests.inc"
#include "orb_timing_session_tests.inc"
#include "orb_survival_session_tests.inc"
#include "orb_recovery_session_tests.inc"
#include "orb_effect_session_tests.inc"

#include "orb_shortcut_session_tests.inc"
#include "orb_thorn_session_tests.inc"
#include "orb_poison_session_tests.inc"
#include "orb_lifecycle_session_tests.inc"
#include "orb_color_condition_session_tests.inc"
#include "orb_plan_session_tests.inc"
#include "orb_uryu_startup_session_tests.inc"
#include "orb_skill_window_session_tests.inc"
#include "orb_diagonal_session_tests.inc"
#include "orb_multiplayer_session_tests.inc"

int main() {
    @autoreleasepool {
        SkillWindowSessionTests();
        DiagonalSessionTests();
        ShortcutSessionTests();
        MultiplayerSessionTests();
        FramePacingSessionTests();
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

        for (auto dimensions : {std::pair<unsigned, unsigned>{6,5}, {7,6}, {5,4}}) {
            engine = [[FakeOrbEngine alloc] initWithWidth:dimensions.first height:dimensions.second];
            engine->requireHoldForStart = YES; engine->live.fps = 30;
            session = [[PHOrbSession alloc] initWithEngine:engine];
            Check(Preview(session, engine), "tall-layout held-start preview at 30 fps");
            Execute(session);
            for (unsigned i = 0; i < 180 && [[session snapshot][@"busy"] boolValue]; ++i)
                Tick(session, engine, Frame(), 1.0 / 30);
            NSDictionary *result = [session snapshot];
            Check([result[@"phase"] isEqual:@"complete"] && engine->beginFrames == 1 && engine->releases == 1 &&
                engine->live.moves == [result[@"steps"] unsignedIntegerValue] &&
                [result[@"confirmed"] isEqual:result[@"steps"]] && [result[@"completed"] unsignedIntegerValue] == 1 &&
                [result[@"startSent"] boolValue] && [result[@"startAccepted"] boolValue],
                "one began followed by held input accepts the start, confirms every swap and releases once");
        }

        engine = [FakeOrbEngine new]; engine->ignoreStart = YES; engine->live.fps = 30;
        session = [[PHOrbSession alloc] initWithEngine:engine];
        Check(Preview(session, engine), "permanently rejected native start setup"); Execute(session);
        for (unsigned i = 0; i < 90 && [[session snapshot][@"busy"] boolValue]; ++i)
            Tick(session, engine, Frame(), 1.0 / 30);
        Check([[session snapshot][@"phase"] isEqual:@"stopped"] && engine->beginFrames == 1 &&
            engine->live.moves == 0 && engine->releases == 0 && [[session snapshot][@"completed"] unsignedIntegerValue] == 0 &&
            ![[session snapshot][@"startAccepted"] boolValue] &&
            [[session snapshot][@"message"] containsString:@"遊戲未接受起點"],
            "start timeout preserves its cause and never releases an unowned orb");

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
        Check(UntilSolveStarts(session, engine), "in-flight goal cancellation setup");
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
        NSDictionary *partial = [session snapshot];
        NSString *partialMessage = partial[@"message"];
        NSArray *partialGoals = partial[@"goalPlan"][@"goals"], *partialHits = partial[@"goalHits"];
        Check(partialGoals.count == partialHits.count, "partial preview retains every goal result");
        for (NSUInteger i = 0; i < MIN(partialGoals.count, partialHits.count); ++i) if ([partialHits[i] unsignedIntegerValue] < [partialGoals[i][@"count"] unsignedIntegerValue])
            Check([partialMessage containsString:PHOrbGoalDescription(partialGoals[i])],
                "partial preview names each unmet goal so failure logs identify the missing condition");
        Check([partialMessage containsString:@"求路耗時"] && [partialMessage containsString:@"ms"] &&
            [partialMessage containsString:[partial[@"deadline"] boolValue] ? @"已到時間上限" : @"未到時間上限"],
            "partial preview distinguishes search elapsed time and deadline from drag duration");
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
        Check(engine->stages == stages && [[session snapshot][@"message"] isEqual:@"background"],
            "background cleanup preserves the original cancellation and does not resume the path");

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
        StrategySessionTests();
        SkillTimingSessionTests();
        SurvivalSessionTests();
        RecoverySessionTests();
        OrbEffectSessionTests();
        PoisonHeartFourSessionTests();
        ThornSessionTests();
        LifecycleSessionTests();
        ColorConditionSessionTests();
        PlanSessionTests();
        UryuStartupSessionTests();
        ExecutionTraceSessionTests();
        std::printf("Orb session tests: %s (preview, execute once, continuous turns, skills, cancellation and acknowledgments)\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
