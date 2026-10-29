static void TestRestoreContinuity(void) {
    PHBattleLoadout loadouts[6]; FullTeam(loadouts);
    for (NSUInteger interruption = 0; interruption < 4; ++interruption) {
        FakeBattleEngine *engine = [FakeBattleEngine new]; PHBattleSession *session = Session(engine);
        [session queueLoadouts:loadouts mask:63];
        for (NSUInteger i = 0; i < 6; ++i) [session onFrame:Frame() atTime:100.1 + i * .1];
        [session queueRestore]; [session onFrame:Frame() atTime:100.8];
        Check(engine->fullRestores == 1 && [session.snapshot[@"restorePendingMask"] isEqual:@62],
              "after the first restore all five remaining positions stay queued");
        uint64_t tokens[6];
        for (NSUInteger i = 1; i < 6; ++i) {
            tokens[i] = engine->live.members[i].providerToken;
            if (interruption == 0) {
                engine->live.members[i].providerToken = 0;
                engine->live.members[i].providerUncertain = true;
            }
        }
        if (interruption == 1) engine->live.scriptRunning = true;
        if (interruption == 2) engine->live.phase = 22;
        if (interruption == 3) engine->readFailure = YES;
        for (NSUInteger tick = 0; tick < 6; ++tick) [session onFrame:Frame() atTime:101 + tick * .1];
        Check(engine->fullRestores == 1 && [session.snapshot[@"activeCount"] isEqual:@5] &&
              [session.snapshot[@"restorePendingMask"] isEqual:@62],
              "unknown ownership, script execution, animation and failed reads never discard remaining restore intent");
        engine->live.scriptRunning = false; engine->live.phase = 2; engine->readFailure = NO;
        for (NSUInteger i = 1; i < 6; ++i) {
            engine->live.members[i].providerToken = tokens[i];
            engine->live.members[i].providerUncertain = false;
        }
        for (NSUInteger tick = 0; tick < 5; ++tick) [session onFrame:Frame() atTime:102 + tick * .1];
        Check(engine->fullRestores == 6 && [session.snapshot[@"activeCount"] isEqual:@0] &&
              [session.snapshot[@"restorePendingMask"] isEqual:@0],
              "the original restore-all request completes without another click after the interruption");
    }
    {
        FakeBattleEngine *engine = [FakeBattleEngine new]; PHBattleSession *session = Session(engine);
        [session queueLoadouts:loadouts mask:63];
        for (NSUInteger i = 0; i < 6; ++i) [session onFrame:Frame() atTime:100.1 + i * .1];
        [session queueRestore]; [session onFrame:Frame() atTime:100.8];
        // A proven replacement has no uncertain marker and must never be
        // overwritten by an old restore request.
        engine->live.members[1].providerToken = 0;
        for (NSUInteger tick = 0; tick < 5; ++tick) [session onFrame:Frame() atTime:101 + tick * .1];
        Check(engine->fullRestores == 5 && engine->live.members[1].catalogID == 201 &&
              [session.snapshot[@"activeCount"] isEqual:@0],
              "actual native takeover remains excluded while other positions complete restoration");
    }
    {
        FakeBattleEngine *engine = [FakeBattleEngine new]; PHBattleSession *session = Session(engine);
        [session queueLoadouts:loadouts mask:3];
        [session onFrame:Frame() atTime:100.1]; [session onFrame:Frame() atTime:100.2];
        [session queueRestore]; engine->nextResult = PHBattleMutationRolledBack;
        [session onFrame:Frame() atTime:100.3];
        Check([session.snapshot[@"restoreFailedMask"] isEqual:@1] &&
              [session.snapshot[@"restorePendingMask"] isEqual:@2],
              "a failed position stays separately reported while other restores remain pending");
        [session onFrame:Frame() atTime:100.4];
        Check(engine->fullRestores == 1 && [session.snapshot[@"restoreFailedMask"] isEqual:@1],
              "a later successful restore does not erase another position's failure diagnostic");
        Check([session.snapshot[@"message"] containsString:@"位置 1"] &&
              [session.snapshot[@"phase"] isEqual:@"restore_failed"] &&
              [session.snapshot[@"canRetryRestore"] boolValue] &&
              [session.snapshot[@"restoreFailures"][0][@"reason"] isEqual:@"verified rollback"],
              "later successes retain the failed slot, exact reason and explicit retry control");
    }
    {
        FakeBattleEngine *engine = [FakeBattleEngine new]; PHBattleSession *session = Session(engine);
        [session queueLoadouts:loadouts mask:63];
        for (NSUInteger i = 0; i < 6; ++i) [session onFrame:Frame() atTime:100.1 + i * .1];
        [session queueRestore];
        for (NSUInteger i = 0; i < 6; ++i) {
            if (i < 5) engine->nextResult = PHBattleMutationRolledBack;
            [session onFrame:Frame() atTime:101 + i * .1];
        }
        [session onFrame:Frame() atTime:101.8];
        Check(engine->fullRestores == 1 && [session.snapshot[@"restoreFailedMask"] isEqual:@31] &&
              [session.snapshot[@"restoreFailures"] count] == 5 && [session.snapshot[@"message"] containsString:@"1、2、3、4、5"],
              "reported failure pattern preserves five rolled-back slots when only helper restore succeeds");
        PHBattleLoadout another[6]; FullTeam(another);
        for (NSUInteger i = 0; i < 6; ++i) another[i].bodyNumber += 100;
        [session queueLoadouts:another mask:63];
        Check([session.snapshot[@"pendingMask"] isEqual:@0] &&
              [session.snapshot[@"message"] containsString:@"重試未完成還原"],
              "another team cannot silently replace the original restore ownership and explains how to recover");
        [session queueRestore];
        for (NSUInteger i = 0; i < 5; ++i) [session onFrame:Frame() atTime:102 + i * .1];
        [session onFrame:Frame() atTime:102.8];
        Check(engine->fullRestores == 6 && [session.snapshot[@"activeCount"] isEqual:@0] &&
              [session.snapshot[@"restoreFailures"] count] == 0 && [session.snapshot[@"canApply"] boolValue],
              "retry restores only the unfinished five slots and clears all failed ownership");
        [session queueLoadouts:another mask:63];
        for (NSUInteger i = 0; i < 6; ++i) [session onFrame:Frame() atTime:103 + i * .1];
        Check(engine->fullApplies == 12 && [session.snapshot[@"activeCount"] isEqual:@6],
              "a different complete team applies after failed restoration is successfully retried");
    }
    {
        FakeBattleEngine *engine = [FakeBattleEngine new]; PHBattleSession *session = Session(engine);
        [session queueLoadouts:loadouts mask:1]; [session onFrame:Frame() atTime:100.1];
        [session queueRestore]; engine->nextResult = PHBattleMutationUncertain;
        [session onFrame:Frame() atTime:100.2]; [session onFrame:Frame() atTime:100.5];
        NSUInteger calls = engine->changes;
        [session queueRestore]; [session queueLoadouts:loadouts mask:2];
        [session onFrame:Frame() atTime:101];
        Check([session.snapshot[@"fault"] boolValue] && engine->changes == calls,
              "unconfirmed rollback cannot be bypassed by retrying or applying another slot in the same battle");
        engine->live.present = false; [session onFrame:Frame() atTime:102];
        engine->live.present = true; ++engine->live.identity.entryHash[0];
        [session onFrame:Frame() atTime:103]; [session queueLoadouts:loadouts mask:2];
        [session onFrame:Frame() atTime:103.1];
        Check(![session.snapshot[@"fault"] boolValue] && engine->changes == calls + 1,
              "an observed battle exit retires the old fault and permits a new single-player battle");
    }
}
