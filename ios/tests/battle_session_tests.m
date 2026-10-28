#import <Foundation/Foundation.h>
#import "PHBattleSession.h"
#include <stdio.h>

static NSUInteger failures;
static void Check(BOOL condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

static void SyncFakeBanks(PHBattleState *state) {
    if (!state->teamBanksValid || state->teamCount > PHBattleTeamCapacity) return;
    for (NSUInteger i = 0; i < 6; ++i) {
        PHBattleMember *view = &state->members[i];
        for (NSUInteger team = 0; team < state->teamCount; ++team) for (NSUInteger slot = 0; slot < 6; ++slot) {
            PHBattleMember *stored = &state->storedMembers[team][slot];
            if (PHBattleSameOrigin(view, stored)) *stored = *view;
        }
    }
}

@interface FakeBattleEngine : NSObject <PHBattleEngine> {
@public
    PHBattleState live;
    NSUInteger reads, changes, validations, fullApplies, fullRestores;
    NSUInteger entryCalls;
    NSUInteger rebindCalls, unboundCancels;
    PHBattleMember entryProfiles[6];
    uint16_t rejectNumber;
    uint64_t nextToken;
    PHBattleMutationResult nextResult;
    BOOL readFailure, rejectTarget;
}
- (void)makeBanked:(uint32_t)mode;
- (void)makeThreePlayer:(NSUInteger)count player:(NSUInteger)player;
- (void)showTeam:(NSUInteger)team;
- (void)dropEntryBindings;
@end
@implementation FakeBattleEngine
- (instancetype)init {
    if ((self = [super init])) {
        live = (PHBattleState){.identity = {.header = 0x100000000, .game = 1, .impl = 2, .player = 42,
            .epoch = 1, .dungeon = 3, .floor = 4, .entryHash = {1}}, .present = true, .protectedValuesValid = true,
            .input = {.valid = true}, .digestKnown = true, .digestMatches = true, .hpMatches = true,
            .phase = 2, .catalogCount = 15000, .catalog = 3};
        for (NSUInteger i = 0; i < 6; ++i) live.members[i] = (PHBattleMember){.active = true, .readable = true,
            .sourceSlot = (int8_t)i, .baseID = (uint16_t)(100 + i), .catalogID = (uint32_t)(100 + i), .charge = 4, .radius = 20};
    }
    return self;
}
- (BOOL)readState:(PHBattleState *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    reads++;
    if (readFailure || !frame.ready) { if (reason) *reason = @"probe unavailable"; return NO; }
    live.identity.entryMode = live.mode;
    live.identity.sourceMode = live.sourceMode;
    live.identity.multiplayer = live.multiplayer;
    SyncFakeBanks(&live);
    *state = live;
    state->policy = frame.policy;
    return YES;
}
- (void)makeBanked:(uint32_t)mode {
    live.multiplayer = mode; live.teamCount = 2; live.teamSize = 5; live.teamBanksValid = true;
    live.teamBanks[0] = 10; live.teamBanks[1] = 20;
    for (NSUInteger team = 0; team < 2; ++team) for (NSUInteger slot = 0; slot < 6; ++slot) {
        PHBattleMember m = live.members[slot];
        m.sourceTeam = (uint8_t)team; m.active = slot < 5;
        m.baseID = (uint16_t)(100 + 1000 * team + slot); m.catalogID = m.baseID;
        live.storedMembers[team][slot] = m;
    }
    live.members[5] = live.storedMembers[1][0];
}
- (void)showTeam:(NSUInteger)team {
    SyncFakeBanks(&live); live.currentTeam = (uint8_t)team;
    for (NSUInteger slot = 0; slot < live.teamSize; ++slot) live.members[slot] = live.storedMembers[team][slot];
    if (live.teamSize == 5) live.members[5] = live.storedMembers[team ^ 1][0];
}
- (void)makeThreePlayer:(NSUInteger)count player:(NSUInteger)player {
    live.mode = 7; live.multiplayer = 1; live.teamSize = 6; live.teamCount = (uint8_t)count;
    live.teamBanksValid = live.turnInfoValid = true; live.playerTeam = (uint8_t)player;
    for (NSUInteger team = 0; team < count; ++team) {
        live.teamBanks[team] = 10 * (team + 1);
        for (NSUInteger slot = 0; slot < 6; ++slot) {
            PHBattleMember m = live.members[slot];
            m.sourceTeam = (uint8_t)team; m.baseID = (uint16_t)(100 + 1000 * team + slot); m.catalogID = m.baseID;
            live.storedMembers[team][slot] = m;
        }
    }
}
- (void)dropEntryBindings {
    SyncFakeBanks(&live);
    for (NSUInteger slot = 0; slot < 6; ++slot) {
        PHBattleMember *m = &live.storedMembers[live.playerTeam][slot];
        m->rebindToken = m->providerToken; m->providerToken = 0; m->scriptSlot = 0;
        m->baseID = (uint16_t)(100 + 1000 * live.playerTeam + slot); m->assistID = 0;
        m->formID = slot == 2 ? 450 : 0; m->catalogID = m->formID ?: m->baseID;
        m->charge = (uint16_t)slot; m->level = 9;
    }
    if (live.currentTeam == live.playerTeam)
        memcpy(live.members,live.storedMembers[live.playerTeam],sizeof(live.members));
}
- (BOOL)validateTarget:(uint16_t)target state:(const PHBattleState *)state reason:(NSString **)reason {
    validations++;
    BOOL valid = !rejectTarget && target < state->catalogCount;
    if (!valid && reason) *reason = @"invalid catalog";
    return valid;
}
- (BOOL)validateLoadout:(const PHBattleLoadout *)loadout state:(const PHBattleState *)state reason:(NSString **)reason {
    validations++;
    uint16_t target = 0;
    BOOL valid = !rejectTarget && loadout->bodyNumber != rejectNumber &&
        PHBattleCatalogIDForNumber(loadout->bodyNumber, &target) && target < state->catalogCount;
    if (!valid && reason) *reason = @"invalid loadout";
    return valid;
}
- (PHBattleMutationResult)applyLoadout:(const PHBattleLoadout *)loadout state:(const PHBattleState *)expected
    slot:(NSUInteger)slot member:(PHBattleMember *)member reason:(NSString **)reason {
    uint16_t target = 0;
    PHBattleCatalogIDForNumber(loadout->bodyNumber, &target);
    PHBattleMutationResult result = [self changeState:expected slot:slot form:0 catalog:target presentation:NULL reason:reason];
    if (result != PHBattleMutationApplied) return result;
    fullApplies++;
    PHBattleMember *m = &live.members[slot];
    m->baseID = target; m->scriptSlot = (int8_t)(slot + 1); m->providerToken = ++nextToken;
    if (loadout->assistNumber) PHBattleCatalogIDForNumber(loadout->assistNumber, &m->assistID);
    m->level = loadout->level ?: 120; m->skillLevel = loadout->skillLevel ?: 5;
    *member = *m;
    return result;
}
- (PHBattleMutationResult)restoreLoadout:(const PHBattleMember *)original state:(const PHBattleState *)expected
    slot:(NSUInteger)slot reason:(NSString **)reason {
    if (live.members[slot].rebindToken && nextResult == PHBattleMutationApplied) {
        Check(PHBattleStateGate(expected) == PHBattleGateReady, "unbound cancellation still waits for local operation ownership");
        live.members[slot].rebindToken = 0;
        ++fullRestores; ++unboundCancels;
        SyncFakeBanks(&live);
        return PHBattleMutationApplied;
    }
    PHBattleMutationResult result = [self changeState:expected slot:slot form:original->formID
        catalog:original->catalogID presentation:original reason:reason];
    if (result != PHBattleMutationApplied) return result;
    uint16_t charge = live.members[slot].charge;
    live.members[slot] = *original;
    live.members[slot].charge = charge;
    fullRestores++;
    return result;
}
- (PHBattleMutationResult)applyEntryLoadouts:(const PHBattleLoadout *)loadouts mask:(uint8_t)mask
    state:(const PHBattleState *)expected members:(PHBattleMember *)members initialBoost:(uint16_t *)boost
    reason:(NSString **)reason {
    entryCalls++;
    Check(PHBattleEntryGate(expected) == PHBattleGateReady, "session uses the fresh entry gate for its atomic native command");
    if (!expected->entryChargesValid) return PHBattleMutationRejected;
    if (nextResult != PHBattleMutationApplied) {
        PHBattleMutationResult result = nextResult; nextResult = PHBattleMutationApplied;
        if (reason) *reason = @"entry transaction failed";
        return result;
    }
    for (NSUInteger i = 0; i < 6; ++i) if ((mask & (1u << i)) &&
        ![self validateLoadout:&loadouts[i] state:expected reason:reason]) return PHBattleMutationRejected;
    NSUInteger originalView = live.currentTeam;
    PHBattleState selected = *expected;
    if (PHBattleIsThreePlayer(expected)) {
        [self showTeam:expected->playerTeam];
        selected = live;
    }
    for (NSUInteger i = 0; i < 6; ++i) if (mask & (1u << i))
        [self applyLoadout:&loadouts[i] state:&selected slot:i member:&members[i] reason:reason];
    for (NSUInteger i = 0; i < 6; ++i) live.members[i].charge = live.members[i].baseID < 200 ? 6 : 8;
    memcpy(members,live.members,sizeof(live.members)); *boost = 8;
    memcpy(entryProfiles,live.members,sizeof(entryProfiles));
    if (PHBattleIsThreePlayer(expected)) [self showTeam:originalView];
    return PHBattleMutationApplied;
}
- (PHBattleMutationResult)rebindEntryMembers:(uint8_t)mask state:(const PHBattleState *)expected
    members:(PHBattleMember *)members reason:(NSString **)reason {
    ++rebindCalls;
    Check(PHBattleIsThreePlayer(expected) && PHBattleStateGate(expected) == PHBattleGateReady,
        "session rebind requires the local player's quiet operation turn");
    if (nextResult != PHBattleMutationApplied) {
        PHBattleMutationResult result = nextResult; nextResult = PHBattleMutationApplied;
        if (reason) *reason = @"retained profile rebind failed";
        return result;
    }
    for (NSUInteger slot = 0; slot < 6; ++slot) if (mask & (1u << slot)) {
        PHBattleMember old = live.members[slot];
        Check(old.rebindToken && !old.providerToken && !old.temporaryID && !old.temporaryTurns,
            "session only rebinds released native bindings, after temporary transformations end");
        live.members[slot] = entryProfiles[(NSUInteger)old.sourceSlot];
        live.members[slot].charge = old.charge; live.members[slot].formID = old.formID;
        live.members[slot].catalogID = old.formID ?: live.members[slot].baseID;
    }
    SyncFakeBanks(&live); memcpy(members,live.members,sizeof(live.members));
    return PHBattleMutationApplied;
}
- (PHBattleMutationResult)changeState:(const PHBattleState *)expected slot:(NSUInteger)slot
    form:(uint16_t)form catalog:(uint32_t)catalog presentation:(const PHBattleMember *)presentation reason:(NSString **)reason {
    changes++;
    if (nextResult != PHBattleMutationApplied) {
        PHBattleMutationResult result = nextResult;
        nextResult = PHBattleMutationApplied;
        if (reason) *reason = result == PHBattleMutationRolledBack ? @"verified rollback" : @"unconfirmed rollback";
        return result;
    }
    Check(PHBattleSameIdentity(&expected->identity, &live.identity), "engine receives the fresh expected identity");
    uint16_t charge = live.members[slot].charge;
    uint16_t maximum = catalog < 200 ? 6 : 12;
    live.members[slot].formID = form;
    live.members[slot].catalogID = catalog;
    live.members[slot].charge = MIN(charge, maximum);
    live.members[slot].previousID = presentation ? presentation->previousID : (uint16_t)expected->members[slot].catalogID;
    live.members[slot].radius = presentation ? presentation->radius : 30;
    return PHBattleMutationApplied;
}
@end

static PHBattleFrame Frame(void) { return (PHBattleFrame){.header = 0x100000000, .player = 42, .epoch = 1, .ready = true, .enabled = true}; }
static PHBattleFrame BankFrame(void) {
    PHBattleFrame frame = Frame(); frame.policy.configured = true;
    frame.policy.modes.allowAll = frame.policy.sources.allowAll = frame.policy.multiplayer.allowAll = true;
    return frame;
}
static PHBattleSession *Session(FakeBattleEngine *engine) {
    PHBattleSession *session = [[PHBattleSession alloc] initWithEngine:engine];
    [session onFrame:Frame() atTime:100];
    return session;
}

static void FullTeam(PHBattleLoadout loadouts[6]) {
    for (NSUInteger i = 0; i < 6; ++i) loadouts[i] = (PHBattleLoadout){.bodyNumber = (uint16_t)(200 + i),
        .assistNumber = (uint16_t)(300 + i), .plus = {297, 297, 297}, .awakenings = -1,
        .assistAwakenings = -1, .superAwakening = -1, .assistPlus = true, .parameters = true, .assistSkillLevel = 1};
}
static PHBattleFrame AutomaticFrame(void) { PHBattleFrame frame = Frame(); frame.autoApplyTeam = true; return frame; }
static void FreshEntry(FakeBattleEngine *engine) {
    engine->live.present = true; engine->live.phase = 0;
    engine->live.entryWindow = engine->live.entryChargesValid = true;
}

#include "battle_multiplayer_session_tests.inc"

int main(void) {
    @autoreleasepool {
        TestMultiplayerSessionDisabled();
        FakeBattleEngine *engine = [FakeBattleEngine new];
        // Reproduce the real-device report: mode 1 / source 0 / multi 0.
        engine->live.mode = 1;
        PHBattleSession *session = Session(engine);
        [session queueApplySlot:0 targetText:@"200"];
        Check(engine->changes == 0 && engine->validations == 0, "UI only queues; validation/native writes stay in drawFrame");
        [session onFrame:Frame() atTime:100.1];
        Check(engine->changes == 1 && [session.snapshot[@"applied"] boolValue] && engine->live.members[0].catalogID == 200,
              "category 1 queued replacement executes at a fresh frame");
        Check(engine->live.members[1].catalogID == 101, "one-member scope preserves other members");
        [session onFrame:Frame() atTime:100.5];
        [session queueApplySlot:1 targetText:@"201"];
        Check(engine->changes == 1 && [session.snapshot[@"pendingMask"] isEqual:@2], "another position can queue independently");
        engine->live.members[0].charge = 9;
        [session queueRestore];
        [session onFrame:Frame() atTime:100.6];
        Check(engine->changes == 2 && ![session.snapshot[@"applied"] boolValue] && engine->live.members[0].catalogID == 100 &&
              engine->live.members[0].formID == 0 && engine->live.members[0].charge == 6, "restore preserves progression and caps to the restored form");

        engine = [FakeBattleEngine new]; session = Session(engine);
        for (NSString *invalid in @[@"0", @"65536", @"65436", @"abc"]) [session queueApplySlot:0 targetText:invalid];
        [session onFrame:Frame() atTime:101];
        Check(engine->changes == 0 && engine->validations == 0, "invalid target text never reaches native code");
        [session queueApplySlot:6 targetText:@"200"];
        [session onFrame:Frame() atTime:101.1];
        Check(engine->changes == 0, "out-of-bounds slot is refused");
        engine->rejectTarget = YES;
        [session queueApplySlot:0 targetText:@"200"];
        [session onFrame:Frame() atTime:101.2];
        Check(engine->changes == 0 && engine->validations == 1, "catalog gaps are rejected at execution time");

        for (NSUInteger scenario = 0; scenario < 9; ++scenario) {
            engine = [FakeBattleEngine new]; session = Session(engine);
            [session queueApplySlot:0 targetText:@"200"];
            PHBattleFrame frame = Frame();
            switch (scenario) {
                case 0: frame.enabled = false; break;
                case 1: frame.epoch++; engine->live.identity.epoch++; break;
                case 2: engine->live.identity.impl++; break;
                case 3: engine->live.identity.dungeon++; break;
                case 4: engine->live.identity.floor++; break;
                case 5: engine->live.identity.entryHash[31]++; break;
                case 6: engine->live.members[0].sourceSlot = 1; break;
                case 7: engine->live.members[0].formID = 300; engine->live.members[0].catalogID = 300; break;
                case 8: engine->live.members[0].temporaryID = 300; break;
            }
            [session onFrame:frame atTime:100.1];
            Check(engine->changes == 0 && ![session.snapshot[@"phase"] isEqual:@"apply_pending"],
                  "disabled or stale account/battle/member request is cancelled before writes");
        }
        for (NSUInteger scenario = 0; scenario < 9; ++scenario) {
            engine = [FakeBattleEngine new]; session = Session(engine);
            [session queueApplySlot:0 targetText:@"200"];
            switch (scenario) {
                case 0: engine->live.phase = 43; break;
                case 1: engine->live.multiplayer = 1; break;
                case 2: engine->live.sourceMode = 3; break;
                case 3: engine->live.input.rawActiveMask = 1; break;
                case 4: engine->live.hpMatches = false; break;
                case 5: engine->live.digestMatches = false; break;
                case 6: engine->live.auditFlags = 0x10000; break;
                case 7: engine->live.input.valid = false; break;
                case 8: engine->live.dragActive = true; break;
            }
            [session onFrame:Frame() atTime:100.1];
            [session onFrame:Frame() atTime:104];
            Check(engine->changes == 0 && ![session.snapshot[@"phase"] isEqual:@"apply_pending"],
                  "busy/multiplayer/corrupt states cannot execute and expire after three seconds");
        }
        engine = [FakeBattleEngine new]; engine->live.mode = 1; session = Session(engine);
        [session queueApplySlot:0 targetText:@"200"];
        uint8_t inputBytes[PHInputDataSize] = {0};
        inputBytes[215] = 1; inputBytes[1320 + 215] = 1;
        Check(PHInputDecode(inputBytes, sizeof(inputBytes), &engine->live.input), "native release fixture decodes");
        [session onFrame:Frame() atTime:100.1];
        NSDictionary *waiting = session.snapshot;
        Check(engine->changes == 0 && [waiting[@"phase"] isEqual:@"apply_pending"] &&
              [waiting[@"gate"] containsString:@"等待放開觸控或輸入處理完成"] &&
              ![waiting[@"gate"] containsString:@"等待玩家操作階段"] && [waiting[@"battlePhase"] isEqual:@2],
              "phase 2 release waits for input with the correct explanation and no native write");
        Check([waiting[@"inputValid"] boolValue] && ![waiting[@"inputQuiet"] boolValue] &&
              [waiting[@"rawInputFlags"] unsignedIntValue] == 0x10000u &&
              [waiting[@"frameInputFlags"] unsignedIntValue] == 0x10000u && [waiting[@"frameInputMask"] isEqual:@1],
              "diagnostics expose the actual native event bytes and active slots");
        inputBytes[1320 + 215] = 0;
        PHInputDecode(inputBytes, sizeof(inputBytes), &engine->live.input);
        [session onFrame:Frame() atTime:100.2];
        Check(engine->changes == 1 && [session.snapshot[@"applied"] boolValue],
              "queued apply runs after release consumption without needing raw ended to reset");
        engine->live.input.frameActiveMask = 1;
        [session queueRestore]; [session onFrame:Frame() atTime:100.3];
        Check(engine->changes == 1 && [session.snapshot[@"restorePending"] boolValue], "restoration also waits for native input");
        engine->live.input.frameActiveMask = 0;
        [session onFrame:Frame() atTime:100.4];
        Check(engine->changes == 2 && ![session.snapshot[@"applied"] boolValue], "restoration resumes once input is quiet");

        engine = [FakeBattleEngine new]; engine->live.phase = 43; session = Session(engine);
        Check([session.snapshot[@"gate"] containsString:@"等待玩家操作階段（phase 43）"], "non-player phase keeps a phase-specific explanation");
        engine = [FakeBattleEngine new]; engine->live.dragActive = true; session = Session(engine);
        Check(![session.snapshot[@"canApply"] boolValue] && [session.snapshot[@"gate"] containsString:@"等待本次拖珠結束"],
              "drag waiting differs from phase and touch waiting");
        engine = [FakeBattleEngine new]; engine->live.input.valid = false; session = Session(engine);
        Check(![session.snapshot[@"canApply"] boolValue] && [session.snapshot[@"gate"] containsString:@"觸控狀態未通過檢查"],
              "failed input read has a separate explanation");
        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueApplySlot:0 targetText:@"200"];
        PHBattleFrame frame = Frame(); frame.ready = false;
        [session onFrame:frame atTime:100.1];
        Check(engine->changes == 0 && ![session.snapshot[@"canApply"] boolValue], "unvalidated frame never calls the engine");
        [session onFrame:Frame() atTime:104];
        Check(engine->changes == 0, "a suspended frame cannot replay an old queued command");

        for (NSUInteger scenario = 0; scenario < 5; ++scenario) {
            engine = [FakeBattleEngine new]; session = Session(engine);
            [session queueApplySlot:0 targetText:@"200"]; [session onFrame:Frame() atTime:100.1];
            switch (scenario) {
                case 0: engine->live.identity.entryHash[0]++; break;
                case 1: engine->live.members[0].sourceSlot = 2; break;
                case 2: engine->live.members[0].formID = 300; engine->live.members[0].catalogID = 300; break;
                case 3: engine->live.members[0].previousID = 200; break;
                case 4: engine->live.present = false; break;
            }
            [session queueRestore]; [session onFrame:Frame() atTime:100.2];
            Check(engine->changes == 1 && ![session.snapshot[@"applied"] boolValue], "restore cannot overwrite a superseding native battle/member/form");
        }
        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueApplySlot:0 targetText:@"200"]; [session onFrame:Frame() atTime:100.1];
        engine->live.members[0].temporaryID = 300; engine->live.members[0].catalogID = 300;
        [session queueRestore]; [session onFrame:Frame() atTime:100.2];
        Check(engine->changes == 1 && [session.snapshot[@"restorePending"] boolValue], "temporary transformation delays restoration");
        engine->live.members[0].temporaryID = 0; engine->live.members[0].catalogID = 200;
        [session onFrame:Frame() atTime:100.3];
        Check(engine->changes == 2 && ![session.snapshot[@"applied"] boolValue], "restore executes after temporary transformation ends");

        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueApplySlot:0 targetText:@"200"]; [session onFrame:Frame() atTime:100.1];
        frame = Frame(); frame.enabled = false; engine->live.phase = 43;
        [session onFrame:frame atTime:100.2];
        Check(engine->changes == 1 && [session.snapshot[@"restorePending"] boolValue], "master-off waits for a safe restore frame");
        engine->live.phase = 2; [session onFrame:frame atTime:100.3];
        Check(engine->changes == 2 && ![session.snapshot[@"applied"] boolValue], "master-off restoration does not require master to be enabled");

        engine = [FakeBattleEngine new]; session = Session(engine);
        engine->nextResult = PHBattleMutationRolledBack;
        [session queueApplySlot:0 targetText:@"200"]; [session onFrame:Frame() atTime:100.1];
        Check(![session.snapshot[@"applied"] boolValue] && ![session.snapshot[@"fault"] boolValue] &&
              [session.snapshot[@"rollbackCount"] isEqual:@1], "verified rollback never creates an applied lease");
        engine = [FakeBattleEngine new]; session = Session(engine);
        engine->nextResult = PHBattleMutationUncertain;
        [session queueApplySlot:0 targetText:@"200"]; [session onFrame:Frame() atTime:100.1];
        [session onFrame:Frame() atTime:101]; [session queueApplySlot:0 targetText:@"201"]; [session onFrame:Frame() atTime:101.1];
        Check(engine->changes == 1 && [session.snapshot[@"fault"] boolValue], "unconfirmed rollback latches off future writes");

        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueApplySlot:0 targetText:@"200"]; [session onFrame:Frame() atTime:100.1];
        engine->nextResult = PHBattleMutationRolledBack;
        frame = Frame(); frame.enabled = false;
        [session onFrame:frame atTime:100.2]; [session onFrame:frame atTime:100.5]; [session onFrame:frame atTime:101];
        Check(engine->changes == 2 && [session.snapshot[@"applied"] boolValue], "failed automatic restore is not retried every frame");
        [session queueRestore]; [session onFrame:frame atTime:101.1];
        Check(engine->changes == 3 && ![session.snapshot[@"applied"] boolValue], "explicit restore can retry a verified rolled-back restore");
        engine = [FakeBattleEngine new]; engine->live.mode = 12; session = Session(engine);
        [session queueApplySlot:0 targetText:@"14238"]; [session onFrame:Frame() atTime:100.1];
        [session onFrame:Frame() atTime:100.5];
        Check(engine->live.members[0].catalogID == 14338 &&
              [session.snapshot[@"members"][0][@"current"] isEqual:@14238],
              "mode 12 accepts the reported encyclopedia number and exposes it without a second conversion");

        PHBattleLoadout loadouts[6]; FullTeam(loadouts);
        engine = [FakeBattleEngine new]; engine->live.mode = 12; session = Session(engine);
        [session queueLoadouts:loadouts mask:63];
        loadouts[0].bodyNumber = 999; // Queued batches must own their data.
        for (NSUInteger i = 0; i < 6; ++i) {
            [session onFrame:Frame() atTime:100.1 + i * 0.1];
            Check(engine->changes == i + 1 && engine->validations == 6,
                  "all six loadouts validate before the first write, with exactly one write per frame");
            Check(engine->live.members[i].catalogID == 200 + i && engine->live.members[i].assistID == 300 + i,
                  "each position receives its own immutable body and weapon pair");
        }
        [session onFrame:Frame() atTime:100.7];
        Check([session.snapshot[@"activeCount"] isEqual:@6] && ![session.snapshot[@"canApply"] boolValue],
              "six independent leases remain restorable");
        [session queueRestoreSlot:5]; [session onFrame:Frame() atTime:100.8];
        Check(engine->fullRestores == 1 && engine->live.members[5].catalogID == 105 &&
              [session.snapshot[@"activeCount"] isEqual:@5] && engine->live.members[0].catalogID == 200,
              "restoring the helper preserves the other five replacements");
        [session queueRestore];
        for (NSUInteger i = 0; i < 5; ++i) [session onFrame:Frame() atTime:100.9 + i * 0.1];
        Check(engine->fullRestores == 6 && [session.snapshot[@"activeCount"] isEqual:@0], "restore-all consumes one lease each frame");

        FullTeam(loadouts); engine = [FakeBattleEngine new]; session = Session(engine);
        engine->rejectNumber = 203; [session queueLoadouts:loadouts mask:63]; [session onFrame:Frame() atTime:100.1];
        Check(engine->changes == 0 && [session.snapshot[@"pendingMask"] isEqual:@0],
              "an invalid fourth loadout rejects the whole batch before the first mutation");
        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueLoadouts:loadouts mask:63];
        [session onFrame:Frame() atTime:100.1]; [session onFrame:Frame() atTime:100.2];
        engine->nextResult = PHBattleMutationRolledBack; [session onFrame:Frame() atTime:100.3];
        [session onFrame:Frame() atTime:100.7];
        Check(engine->changes == 3 && [session.snapshot[@"activeCount"] isEqual:@2] &&
              [session.snapshot[@"pendingMask"] isEqual:@0] && ![session.snapshot[@"fault"] boolValue],
              "partial batch failure cancels remaining entries and preserves successful leases");
        [session queueRestore]; [session onFrame:Frame() atTime:100.8]; [session onFrame:Frame() atTime:100.9];
        Check(engine->fullRestores == 2 && ![session.snapshot[@"applied"] boolValue], "successful members can restore after another slot rolls back");

        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueLoadouts:loadouts mask:1];
        PHBattleMember swapped = engine->live.members[0];
        engine->live.members[0] = engine->live.members[5]; engine->live.members[5] = swapped;
        [session onFrame:Frame() atTime:100.1];
        Check(engine->live.members[5].catalogID == 200 && engine->live.members[0].catalogID == 105,
              "a queued source follows a leader swap to its unique new position");
        engine->live.members[5].formID = 250; engine->live.members[5].catalogID = 250;
        engine->live.members[5].charge = 9;
        [session onFrame:Frame() atTime:100.5];
        Check([session.snapshot[@"activeCount"] isEqual:@1], "full parameter ownership survives native transformation");
        [session queueRestoreSlot:5]; [session onFrame:Frame() atTime:100.6];
        Check(engine->fullRestores == 1 && engine->live.members[5].catalogID == 100 && engine->live.members[5].charge == 6,
              "restore after transformation and leader swap uses the original source and current charge");

        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueLoadouts:loadouts mask:63]; [session onFrame:Frame() atTime:100.1];
        [session queueRestore]; [session onFrame:Frame() atTime:100.2];
        Check(engine->fullApplies == 1 && engine->fullRestores == 1 && [session.snapshot[@"pendingMask"] isEqual:@0],
              "restore cancels the unfinished batch and never adds a second mutation to its frame");

        engine = [FakeBattleEngine new]; session = Session(engine);
        [session queueLoadouts:loadouts mask:3];
        [session onFrame:Frame() atTime:100.1]; [session onFrame:Frame() atTime:100.2];
        frame = Frame(); frame.enabled = false;
        [session onFrame:frame atTime:100.3]; [session onFrame:frame atTime:100.4];
        Check(engine->fullRestores == 2 && ![session.snapshot[@"applied"] boolValue], "master-off restores every owned position");
        engine = [FakeBattleEngine new]; engine->live.mode = 12; session = Session(engine);
        [session queueLoadouts:loadouts mask:63]; [session onFrame:Frame() atTime:100.1];
        frame = Frame();
        frame.policy = (PHBattlePolicy){.configured = true, .modes = {.count = 1, .values = {0}},
            .sources = {.count = 1, .values = {0}}, .multiplayer = {.count = 1, .values = {0}}};
        [session onFrame:frame atTime:100.2];
        Check(engine->fullApplies == 1 && engine->fullRestores == 1 && [session.snapshot[@"pendingMask"] isEqual:@0],
              "restricting a solo mode mid-batch cancels future writes and restores the owned member with its original policy");
        [session onFrame:frame atTime:100.6]; [session queueLoadouts:loadouts mask:1];
        Check(![session.snapshot[@"canApply"] boolValue] && [session.snapshot[@"pendingMask"] isEqual:@0],
              "a hot-reloaded list rejects new operations without recompile");
        frame.policy.modes.allowAll = true; [session onFrame:frame atTime:101];
        [session queueLoadouts:loadouts mask:1]; [session onFrame:frame atTime:101.1];
        Check(engine->fullApplies == 2 && [session.snapshot[@"activeCount"] isEqual:@1],
              "allow-all becomes effective again on the next frame");
        engine = [FakeBattleEngine new]; engine->live.sourceMode = 3;
        session = [PHBattleSession.alloc initWithEngine:engine];
        frame = Frame(); frame.policy = (PHBattlePolicy){.configured = true, .modes = {.allowAll = true},
            .sources = {.count = 1, .values = {3}}, .multiplayer = {.count = 1, .values = {0}}};
        [session onFrame:frame atTime:100]; [session queueLoadouts:loadouts mask:1]; [session onFrame:frame atTime:100.1];
        frame.policy.sources.count = 0; [session onFrame:frame atTime:100.2];
        Check(engine->fullApplies == 1 && engine->fullRestores == 1,
              "source filter edits also restore using the owned transaction policy");

        PHBattleLoadout automatic[6]; FullTeam(automatic);
        engine = [FakeBattleEngine new]; session = [PHBattleSession.alloc initWithEngine:engine];
        [session configureAutomaticLoadouts:automatic mask:63];
        automatic[0].bodyNumber = 999;
        Check(!engine->reads && !engine->changes && !engine->entryCalls, "saving or selecting an automatic team only copies configuration");
        frame = AutomaticFrame(); engine->live.present = false;
        [session onFrame:frame atTime:100]; FreshEntry(engine);
        [session onFrame:frame atTime:100.01];
        Check(engine->entryCalls == 1 && engine->fullApplies == 6 && engine->live.members[0].baseID == 200 &&
              [session.snapshot[@"automaticCount"] isEqual:@1] && [session.snapshot[@"initialBoost"] isEqual:@8] &&
              [session.snapshot[@"activeCount"] isEqual:@6], "a menu observation arms exactly one immutable six-member entry transaction");
        [session onFrame:frame atTime:100.02]; [session onFrame:frame atTime:100.5];
        Check(engine->entryCalls == 1, "more intro or gameplay frames never recharge the same entry");
        NSDictionary *acceptedRecipe = session.snapshot[@"members"][0][@"appliedRecipe"];
        Check([acceptedRecipe[@"body"][@"id"] isEqual:@200] && [acceptedRecipe[@"assist"][@"id"] isEqual:@300] &&
            [acceptedRecipe[@"body"][@"plus"] isEqual:@[@297, @297, @297]] &&
            [acceptedRecipe[@"assist"][@"skill"] isEqual:@1],
            "the visible lease exposes its immutable body, assist and training recipe");
        [session configureAutomaticLoadouts:automatic mask:63];
        Check([session.snapshot[@"members"][0][@"appliedRecipe"] isEqual:acceptedRecipe],
            "selecting a different next-entry team must not relabel the currently applied recipe");
        engine->live.phase = 2; engine->live.entryWindow = false; [session queueRestore];
        for (NSUInteger i = 0; i < 6; ++i) [session onFrame:frame atTime:101+i*0.1];
        Check(engine->fullRestores == 6 && [session.snapshot[@"activeCount"] isEqual:@0], "auto entry installs six independently restorable leases");
        [session onFrame:frame atTime:101.8];
        Check([session.snapshot[@"members"][0][@"appliedRecipe"] count] == 0,
            "restored slots cannot retain an applied recipe label");
        engine->live.present = false; [session onFrame:frame atTime:102];
        engine->live.identity.entryHash[0]++; FreshEntry(engine); [session onFrame:frame atTime:102.01];
        Check(engine->entryCalls == 2 && [session.snapshot[@"automaticCount"] isEqual:@2], "returning to the menu arms the next dungeon once");

        FullTeam(automatic); engine = [FakeBattleEngine new]; session = Session(engine);
        [session configureAutomaticLoadouts:automatic mask:63];
        FreshEntry(engine); frame = AutomaticFrame(); [session onFrame:frame atTime:100.3];
        Check(!engine->entryCalls && !engine->changes, "enabling automatic mode inside an already observed battle cannot recharge it");

        engine = [FakeBattleEngine new]; session = [PHBattleSession.alloc initWithEngine:engine];
        [session configureAutomaticLoadouts:automatic mask:63]; engine->live.present = false;
        [session onFrame:AutomaticFrame() atTime:100]; FreshEntry(engine); engine->live.input.rawActiveMask = 1;
        [session onFrame:AutomaticFrame() atTime:100.01];
        Check(!engine->entryCalls && [session.snapshot[@"automaticWaiting"] boolValue], "entry waits for the dungeon button's live touch release");
        automatic[0].bodyNumber = 999; [session configureAutomaticLoadouts:automatic mask:1];
        engine->live.input.rawActiveMask = 0; [session onFrame:AutomaticFrame() atTime:100.02];
        Check(engine->entryCalls == 1 && engine->fullApplies == 6 && engine->live.members[0].baseID == 200,
              "changing saved selection during the intro cannot modify the captured entry batch");

        FullTeam(automatic); engine = [FakeBattleEngine new]; session = [PHBattleSession.alloc initWithEngine:engine];
        [session configureAutomaticLoadouts:automatic mask:63]; engine->live.present = false;
        [session onFrame:AutomaticFrame() atTime:100]; FreshEntry(engine); engine->live.input.rawActiveMask = 1;
        [session onFrame:AutomaticFrame() atTime:100.01]; [session queueRestore];
        engine->live.input.rawActiveMask = 0; [session onFrame:AutomaticFrame() atTime:100.02];
        Check(!engine->entryCalls && ![session.snapshot[@"automaticWaiting"] boolValue], "Restore/Cancel consumes a pending intro without applying or charging it later");

        for (NSUInteger scenario = 0; scenario < 9; ++scenario) {
            engine = [FakeBattleEngine new]; session = [PHBattleSession.alloc initWithEngine:engine];
            [session configureAutomaticLoadouts:automatic mask:63]; engine->live.present = false; frame = AutomaticFrame();
            [session onFrame:frame atTime:100]; FreshEntry(engine);
            switch (scenario) {
                case 0: frame.enabled = false; break;
                case 1: frame.autoApplyTeam = false; break;
                case 2: frame.ready = false; break;
                case 3: frame.epoch++; engine->live.identity.epoch++; break;
                case 4: frame.player++; engine->live.identity.player++; break;
                case 5: engine->live.entryWindow = false; break;
                case 6: engine->live.phase = 2; engine->live.entryWindow = false; break;
                case 7: engine->live.sourceMode = 3; break;
                case 8: frame.policy = (PHBattlePolicy){.configured = true}; break;
            }
            [session onFrame:frame atTime:100.01];
            frame.enabled = frame.ready = frame.autoApplyTeam = true;
            engine->live.phase = 2; engine->live.entryWindow = false;
            [session onFrame:frame atTime:100.5];
            Check(!engine->entryCalls && !engine->changes, "disabled, account-changed, resumed/missed-window or excluded entries never fall back to a phase-two recharge");
        }
        engine = [FakeBattleEngine new]; session = [PHBattleSession.alloc initWithEngine:engine];
        [session configureAutomaticLoadouts:NULL mask:0]; engine->live.present = false;
        [session onFrame:AutomaticFrame() atTime:100]; FreshEntry(engine); [session onFrame:AutomaticFrame() atTime:100.01];
        Check(!engine->entryCalls && ![session.snapshot[@"automaticWaiting"] boolValue], "an empty saved team skips auto entry without a native call");

        for (NSUInteger scenario = 0; scenario < 3; ++scenario) {
            engine = [FakeBattleEngine new]; session = [PHBattleSession.alloc initWithEngine:engine];
            [session configureAutomaticLoadouts:automatic mask:63]; engine->live.present = false;
            [session onFrame:AutomaticFrame() atTime:100]; FreshEntry(engine);
            engine->nextResult = scenario == 0 ? PHBattleMutationRejected : scenario == 1 ? PHBattleMutationRolledBack : PHBattleMutationUncertain;
            [session onFrame:AutomaticFrame() atTime:100.01]; [session onFrame:AutomaticFrame() atTime:100.5];
            Check(engine->entryCalls == 1 && [session.snapshot[@"activeCount"] isEqual:@0] &&
                [session.snapshot[@"rollbackCount"] unsignedIntegerValue] == (scenario == 1 ? 1u : 0u) &&
                [session.snapshot[@"fault"] boolValue] == (scenario == 2), "failed atomic entry is consumed once, counts confirmed rollback, and faults only on unconfirmed restoration");
        }
        printf("Battle session tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
