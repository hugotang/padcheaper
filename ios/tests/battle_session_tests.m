#import <Foundation/Foundation.h>
#import "PHBattleSession.h"
#include <stdio.h>

static NSUInteger failures;
static void Check(BOOL condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

@interface FakeBattleEngine : NSObject <PHBattleEngine> {
@public
    PHBattleState live;
    NSUInteger reads, changes, validations;
    PHBattleMutationResult nextResult;
    BOOL readFailure, rejectTarget;
}
@end
@implementation FakeBattleEngine
- (instancetype)init {
    if ((self = [super init])) {
        live = (PHBattleState){.identity = {.header = 0x100000000, .game = 1, .impl = 2, .player = 42,
            .epoch = 1, .dungeon = 3, .floor = 4, .entryHash = {1}}, .present = true, .protectedValuesValid = true,
            .inputQuiet = true, .digestKnown = true, .digestMatches = true, .hpMatches = true,
            .phase = 2, .catalogCount = 1000, .catalog = 3};
        for (NSUInteger i = 0; i < 6; ++i) live.members[i] = (PHBattleMember){.active = true, .readable = true,
            .sourceSlot = (int8_t)i, .baseID = (uint16_t)(100 + i), .catalogID = (uint32_t)(100 + i), .charge = 4, .radius = 20};
    }
    return self;
}
- (BOOL)readState:(PHBattleState *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    reads++;
    if (readFailure || !frame.ready) { if (reason) *reason = @"probe unavailable"; return NO; }
    *state = live;
    return YES;
}
- (BOOL)validateTarget:(uint16_t)target state:(const PHBattleState *)state reason:(NSString **)reason {
    validations++;
    BOOL valid = !rejectTarget && target < state->catalogCount;
    if (!valid && reason) *reason = @"invalid catalog";
    return valid;
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

static PHBattleFrame Frame(void) { return (PHBattleFrame){0x100000000, 42, 1, true, true}; }
static PHBattleSession *Session(FakeBattleEngine *engine) {
    PHBattleSession *session = [[PHBattleSession alloc] initWithEngine:engine];
    [session onFrame:Frame() atTime:100];
    return session;
}

int main(void) {
    @autoreleasepool {
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
        Check(engine->changes == 1, "a second replacement requires restoration");
        engine->live.members[0].charge = 9;
        [session queueRestore];
        [session onFrame:Frame() atTime:100.6];
        Check(engine->changes == 2 && ![session.snapshot[@"applied"] boolValue] && engine->live.members[0].catalogID == 100 &&
              engine->live.members[0].formID == 0 && engine->live.members[0].charge == 6, "restore preserves progression and caps to the restored form");

        engine = [FakeBattleEngine new]; session = Session(engine);
        for (NSString *invalid in @[@"0", @"65536", @"9900", @"abc"]) [session queueApplySlot:0 targetText:invalid];
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
        for (NSUInteger scenario = 0; scenario < 7; ++scenario) {
            engine = [FakeBattleEngine new]; session = Session(engine);
            [session queueApplySlot:0 targetText:@"200"];
            switch (scenario) {
                case 0: engine->live.phase = 43; break;
                case 1: engine->live.multiplayer = 1; break;
                case 2: engine->live.mode = 7; break;
                case 3: engine->live.inputQuiet = false; break;
                case 4: engine->live.hpMatches = false; break;
                case 5: engine->live.digestMatches = false; break;
                case 6: engine->live.auditFlags = 0x10000; break;
            }
            [session onFrame:Frame() atTime:100.1];
            [session onFrame:Frame() atTime:104];
            Check(engine->changes == 0 && ![session.snapshot[@"phase"] isEqual:@"apply_pending"],
                  "busy/multiplayer/corrupt states cannot execute and expire after three seconds");
        }
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
        printf("Battle session tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
