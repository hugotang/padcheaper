// Real PHOrbSession + overlay actions with a read-only native-frame fixture.
// This tests the first UI observation rather than assigning canStart by hand.
#include <string.h>
@interface PHProbeReadinessEngine : NSObject <PHOrbEngine> {
@public
    PHOrbSnapshot live;
    NSUInteger reads, inputCalls;
    BOOL armed;
}
@end
@implementation PHProbeReadinessEngine
- (instancetype)init {
    if ((self = [super init])) {
        memset(&live,0,sizeof(live));
        live.identity.header = 1; live.identity.game = 2; live.identity.impl = 3;
        live.identity.player = 42; live.identity.epoch = 1; live.identity.dungeon = 1; live.identity.floor = 2;
        live.identity.entryHash[0] = 1; live.identity.entryMode = 10; live.identity.multiplayer = 1;
        live.inputManager = 4; live.present = live.valid = live.settled = live.ordinary = true;
        live.phase = 2; live.input.valid = true; live.minErase = 3; live.freeMovement = 1;
        live.remainingFrames = 300; live.fps = 60; live.board.width = 6; live.board.height = 5;
        for (unsigned i = 0; i < 30; ++i) {
            live.board.tokens[i] = 100 + i; live.board.types[i] = i % 6;
            live.board.x[i] = 50 + (i % 6) * 100; live.board.y[i] = 400 + (i / 6) * 100;
        }
        live.multiplayer.kind = PHOrbMultiplayerEight;
        live.multiplayer.known = live.multiplayer.clockKnown = true;
        live.multiplayer.teamCount = 8; live.multiplayer.localSeat = live.multiplayer.viewTeam = 7;
        live.multiplayer.localStatus = 3; live.multiplayer.networkFlags = 1; live.multiplayer.connectionState = 2;
        live.multiplayer.clockStart = 12345; live.multiplayer.clockFlags = 4; live.multiplayer.remainingSeconds = 180;
    }
    return self;
}
- (BOOL)read:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++reads; *state = live;
    if (!live.multiplayer.countdownReady) {
        state->ordinary = state->settled = false;
        memset(state->board.tokens,0,sizeof(state->board.tokens));
    }
    return frame.ready;
}
- (BOOL)readAutomation:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    return [self read:state frame:frame reason:reason];
}
- (void)armMultiplayerOnce:(BOOL)value { if (value) ++inputCalls; }
- (BOOL)armEightPlayerAuto:(const PHOrbSnapshot *)state { armed = state != NULL; return YES; }
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++inputCalls; return PHOrbSkillRejected;
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot request:(PHOrbSkillRequest)request expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++inputCalls; return PHOrbSkillRejected;
}
- (BOOL)stageCell:(NSUInteger)cell down:(BOOL)down beginning:(BOOL)beginning expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++inputCalls; return NO;
}
- (BOOL)finishInput:(NSString **)reason { return YES; }
@end

static void PHProbeReadinessTick(PHOrbSession *session, NSTimeInterval time) {
    PHBattleFrame frame = {0};
    frame.header = 1; frame.player = 42; frame.epoch = 1; frame.ready = frame.enabled = true;
    [session beforeFrame:frame time:time]; [session finishNativeFrame];
    [session afterFrame:frame time:time];
}

static void PHProbeColdEightAuto(UIWindow *guest, UIWindow *overlay) {
    PHOverlaySnapshot originalSnapshot = [overlay.rootViewController valueForKey:@"snapshot"];
    PHOverlayAction originalAction = [overlay.rootViewController valueForKey:@"action"];
    NSNumber *expanded = [overlay.rootViewController valueForKey:@"expanded"];
    NSNumber *collapsed = [overlay.rootViewController valueForKey:@"shortcutsCollapsed"];
    PHProbeReadinessEngine *engine = [PHProbeReadinessEngine new];
    PHOrbSession *session = [[NSClassFromString(@"PHOrbSession") alloc] initWithEngine:engine];
    PHProbeCheck(session != nil,@"Readiness probe uses the actual Session in the loaded dylib");
    NSMutableDictionary *state = [originalSnapshot() mutableCopy];
    __block NSUInteger observations = 0, starts = 0;
    PHOverlaySnapshot snapshot = [^{ state[@"orbState"] = session.snapshot; return state; } copy];
    [overlay.rootViewController setValue:snapshot forKey:@"snapshot"];
    [overlay.rootViewController setValue:[^(NSString *action, id value) {
        if ([action isEqual:@"orbsObserve"]) { ++observations; [session monitorReadiness]; }
        if ([action isEqual:@"orbsStart"]) { ++starts; [session queueAutomatic]; }
    } copy] forKey:@"action"];
    [overlay.rootViewController setValue:@NO forKey:@"expanded"];
    [overlay.rootViewController setValue:@YES forKey:@"shortcutsCollapsed"];
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    UIButton *bubble = (UIButton *)PHProbeFind(overlay,@"padcheaper.bubble");
    UIButton *start = (UIButton *)PHProbeFind(overlay,@"padcheaper.orbsAutoShortcut");
    UIButton *once = (UIButton *)PHProbeFind(overlay,@"padcheaper.orbsOnceShortcut");
    [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
    PHProbeReadinessTick(session,100);
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    PHProbeCheck(observations == 1 && engine->reads == 1 && !starts && !engine->inputCalls && !start.enabled && !once.enabled,
        @"Opening cold 8P shortcuts triggers a real readiness read and remains disabled through countdown");
    engine->live.multiplayer.countdownReady = 1;
    PHProbeReadinessTick(session,100.3);
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    PHProbeCheck(start.enabled && once.enabled && !starts && !engine->armed && !engine->inputCalls &&
        ![session.snapshot[@"searchTrace"] count],@"Native-frame countdown completion enables Auto without a prior one-shot or solver");
    PHProbeRender(guest,overlay,@"orbs-eight-first-ready");
    [start sendActionsForControlEvents:UIControlEventTouchUpInside];
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    PHProbeCheck(starts == 1 && engine->armed && [session.snapshot[@"automatic"] boolValue] &&
        start.enabled && [start.currentTitle isEqual:@"停止"],
        @"The newly enabled cold-start Auto button starts the real session once and becomes Stop");
    engine->live.phase = 28;
    PHProbeReadinessTick(session,100.6);
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    PHProbeCheck(![session.snapshot[@"automatic"] boolValue] && !engine->armed && !start.enabled && !once.enabled &&
        [session.snapshot[@"message"] containsString:@"Dungeon Clear"] && !engine->inputCalls,
        @"Dungeon Clear stops the session and disables UI actions with no result-screen input");
    PHProbeRender(guest,overlay,@"orbs-eight-clear-stopped");
    [overlay.rootViewController setValue:originalSnapshot forKey:@"snapshot"];
    [overlay.rootViewController setValue:originalAction forKey:@"action"];
    [overlay.rootViewController setValue:expanded forKey:@"expanded"];
    [overlay.rootViewController setValue:collapsed forKey:@"shortcutsCollapsed"];
    [overlay.rootViewController updateWithSnapshot:originalSnapshot()];
}
