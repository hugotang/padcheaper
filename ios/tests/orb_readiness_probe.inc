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

static void PHProbeReadinessTick(PHOrbSession *session, NSTimeInterval time);

@interface PHProbeEightResumeEngine : PHProbeReadinessEngine {
@public
    NSUInteger skills, progressReads, activeSlot;
}
- (void)completeSkill;
@end
@implementation PHProbeEightResumeEngine
- (instancetype)init {
    if ((self = [super init])) {
        NSData *data = [NSData dataWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"eight-activity" ofType:@"json"]];
        NSDictionary *package = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
        NSDictionary *profile = package[@"profile"];
        live.multiplayer.countdownReady = 1; live.wave = 1;
        live.automation.valid = live.automation.window = live.automation.boardKnown = live.automation.enemiesKnown = true;
        live.automation.windowBlocksKnown = true; live.automation.activeEnemies = 1;
        live.eight.valid = true; live.eight.teamBank = 55; live.eight.catalog = 66; live.eight.skills = 77;
        live.enemyCombo.valid = true; live.enemyCombo.enemyCount = live.enemyCombo.aliveMask = 1; live.enemyCombo.hp[0] = 100;
        for (unsigned slot = 0; slot < 6; ++slot) {
            NSDictionary *source = profile[@"activity"][@"members"][slot];
            PHOrbSkillMember *member = &live.automation.members[slot]; PHOrbEightMember *m = &live.eight.members[slot];
            member->active = true; member->number = [source[@"number"] unsignedIntValue]; member->selection = UINT32_MAX;
            member->sourceCatalogIDs[0] = member->number; member->charge = 20;
            m->sourceSlot = slot; m->catalogID = 1000000 + member->number; m->parameter = 100 + slot;
            m->activeSkill = m->rootSkill = 1000 + slot; m->leaderSkill = 900; m->cooldown = [source[@"cooldown"] unsignedIntValue];
            m->skillKnown = ![source[@"effect"] isEqual:@"observe"]; m->leaderKnown = m->noSkyfall = true;
            m->boardColors = [source[@"boardColors"] unsignedIntValue]; m->unlock = [source[@"unlock"] boolValue];
            for (unsigned c = 0; c < 6; ++c) { m->convert[c] = [source[@"convert"][c] unsignedIntValue]; m->generate[c] = [source[@"generate"][c] unsignedIntValue]; }
            m->attackColors = m->comboColors = 17; m->attackLength = 6; m->comboLength = 12; m->attackPercent = 1000; m->addedCombos = 1;
        }
        for (unsigned i = 0; i < 30; ++i) live.board.types[i] = i / 15 + 1;
    }
    return self;
}
- (BOOL)readEightProgress:(PHOrbSnapshot *)state expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++progressReads; *state = live; state->automation.window = false; return frame.ready && frame.enabled;
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot request:(PHOrbSkillRequest)request expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!armed || slot >= 6 || live.phase != 2 || request.source != PHOrbSkillBody || request.skill != live.eight.members[slot].activeSkill)
        return PHOrbSkillRejected;
    ++skills; activeSlot = slot;
    live.phase = 22; live.automation.busy = true; live.automation.members[slot].state = 2;
    return PHOrbSkillStarted;
}
- (void)completeSkill {
    live.phase = 2; live.automation.busy = false; live.automation.members[activeSlot].state = 0;
    live.automation.members[activeSlot].charge = 0; ++live.automation.skillUses;
    const PHOrbEightMember *m = &live.eight.members[activeSlot];
    live.eightReceipt = (PHOrbEightSkillReceipt){m->activeSkill,m->catalogID,(uint16_t)activeSlot,0,true};
    for (unsigned i = 0; i < 30; ++i) live.board.types[i] = m->convert[live.board.types[i]];
}
@end

static void PHProbeEightResume(UIWindow *guest, UIWindow *overlay) {
    PHOverlaySnapshot originalSnapshot = [overlay.rootViewController valueForKey:@"snapshot"];
    PHOverlayAction originalAction = [overlay.rootViewController valueForKey:@"action"];
    NSNumber *expanded = [overlay.rootViewController valueForKey:@"expanded"];
    NSNumber *collapsed = [overlay.rootViewController valueForKey:@"shortcutsCollapsed"];
    PHProbeEightResumeEngine *engine = [PHProbeEightResumeEngine new];
    PHOrbSession *session = [[NSClassFromString(@"PHOrbSession") alloc] initWithEngine:engine];
    NSData *data = [NSData dataWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"eight-activity" ofType:@"json"]];
    NSDictionary *package = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    NSDictionary *profile = package[@"profile"];
    PHProbeCheck(session && profile && [session configureProfiles:@{@"schema":@1,@"selected":profile[@"id"],@"autoSelect":@NO,@"profiles":@[profile]}],
        @"8P resume GUI uses a real Session with the bundled activity and fixed-skill fixture");
    NSMutableDictionary *state = [originalSnapshot() mutableCopy];
    PHOverlaySnapshot snapshot = [^{ state[@"orbState"] = session.snapshot; return state; } copy];
    [overlay.rootViewController setValue:snapshot forKey:@"snapshot"];
    [overlay.rootViewController setValue:[^(NSString *action, id value) {
        if ([action isEqual:@"orbsObserve"]) [session monitorReadiness];
        if ([action isEqual:@"orbsStart"]) [session queueAutomatic];
        if ([action isEqual:@"orbsStop"]) [session queueStop:@"GUI pause during skill"];
    } copy] forKey:@"action"];
    [overlay.rootViewController setValue:@NO forKey:@"expanded"];
    [overlay.rootViewController setValue:@NO forKey:@"shortcutsCollapsed"];
    [session monitorReadiness]; NSTimeInterval time = 200;
    PHProbeReadinessTick(session,time); [overlay.rootViewController updateWithSnapshot:snapshot()];
    UIButton *start = (UIButton *)PHProbeFind(overlay,@"padcheaper.orbsAutoShortcut");
    [start sendActionsForControlEvents:UIControlEventTouchUpInside];
    for (unsigned i = 0; i < 200 && !engine->skills && [session.snapshot[@"automatic"] boolValue]; ++i) {
        time += 0.05; PHProbeReadinessTick(session,time);
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.005]];
    }
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    PHProbeCheck(engine->skills == 1 && [session.snapshot[@"eightSkillPending"] boolValue] && [start.currentTitle isEqual:@"停止"],
        @"8P board shortage starts exactly one native fixed skill and leaves Stop available");
    [start sendActionsForControlEvents:UIControlEventTouchUpInside];
    [engine completeSkill]; time += 0.3; PHProbeReadinessTick(session,time);
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    PHProbeCheck(!engine->armed && engine->progressReads && !engine->inputCalls && start.enabled &&
        [session.snapshot[@"eightProgress"][@"skillAcknowledged"] boolValue] && !engine->live.automation.members[1].skillUses[0],
        @"Skill completes after UI Stop with zero limited uses, no input authority and Auto available again");
    PHProbeRender(guest,overlay,@"orbs-eight-skill-paused");
    [start sendActionsForControlEvents:UIControlEventTouchUpInside];
    for (unsigned i = 0; i < 25 && ![session.snapshot[@"autoSkills"] isEqual:@1]; ++i) {
        time += 0.05; PHProbeReadinessTick(session,time);
    }
    [overlay.rootViewController updateWithSnapshot:snapshot()];
    PHProbeCheck(engine->armed && engine->skills == 1 && !engine->inputCalls &&
        [session.snapshot[@"autoSkills"] isEqual:@1] && [session.snapshot[@"eightProgress"][@"resumes"] isEqual:@1] &&
        [session.snapshot[@"eightProgress"][@"lastStop"] count] && [start.currentTitle isEqual:@"停止"],
        @"UI Auto resumes the same match with one confirmed skill, retained stop reason and no duplicate cast");
    PHProbeRender(guest,overlay,@"orbs-eight-skill-resumed");
    [session queueStop:@"GUI resume probe done"];
    [overlay.rootViewController setValue:originalSnapshot forKey:@"snapshot"];
    [overlay.rootViewController setValue:originalAction forKey:@"action"];
    [overlay.rootViewController setValue:expanded forKey:@"expanded"];
    [overlay.rootViewController setValue:collapsed forKey:@"shortcutsCollapsed"];
    [overlay.rootViewController updateWithSnapshot:originalSnapshot()];
}
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
