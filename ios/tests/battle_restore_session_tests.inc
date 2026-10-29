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
    }
}
