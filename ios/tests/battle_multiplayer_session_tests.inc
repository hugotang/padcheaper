static void CheckSessionDisabled(PHBattleSession *session) {
    NSDictionary *state = session.snapshot;
    Check([state[@"singlePlayerOnly"] boolValue] && [state[@"multiplayerDisabled"] boolValue] &&
        [state[@"phase"] isEqual:@"multiplayer_disabled"] && ![state[@"canApply"] boolValue] &&
        ![state[@"canRestore"] boolValue] && ![state[@"automaticWaiting"] boolValue] &&
        ![state[@"restorePending"] boolValue] && [state[@"pendingMask"] isEqual:@0] &&
        [state[@"activeCount"] isEqual:@0] && [state[@"rebindPendingCount"] isEqual:@0],
        "multiplayer diagnostics disable every action and discard all pending intent");
    for (NSDictionary *member in state[@"members"])
        Check(![member[@"replaceable"] boolValue], "multiplayer members never offer a replacement action");
}

static void TestMultiplayerSessionDisabled(void) {
    for (NSUInteger count = 2; count <= 3; ++count) for (NSUInteger player = 0; player < count; ++player) {
        FakeBattleEngine *engine = [FakeBattleEngine new]; [engine makeThreePlayer:count player:player];
        PHBattleSession *session = [PHBattleSession.alloc initWithEngine:engine];
        PHBattleLoadout team[6]; FullTeam(team); [session configureAutomaticLoadouts:team mask:63];
        PHBattleFrame frame = BankFrame(); frame.autoApplyTeam = true;
        engine->live.present = false; [session onFrame:frame atTime:100];
        FreshEntry(engine); engine->live.phase = 46; [session onFrame:frame atTime:100.01];
        CheckSessionDisabled(session);
        for (NSUInteger turn = 0; turn < count * 2; ++turn) {
            engine->live.phase = 2; engine->live.entryWindow = false;
            engine->live.turnSerial = (uint32_t)turn; engine->live.turnTeam = (uint8_t)(turn % count);
            [engine showTeam:player];
            [session onFrame:frame atTime:101 + turn];
            [session queueLoadouts:team mask:63]; [session queueApplySlot:0 targetText:@"200"];
            [session queueRestore]; [session onFrame:frame atTime:101.1 + turn];
            CheckSessionDisabled(session);
        }
        Check(!engine->changes && !engine->validations && !engine->fullApplies && !engine->fullRestores &&
            !engine->entryCalls && !engine->rebindCalls && !engine->unboundCancels,
            "host and every guest seat reach no engine mutation or preparation methods in any turn");
    }
    for (uint32_t multi = 0; multi <= 5; ++multi) {
        FakeBattleEngine *engine = [FakeBattleEngine new];
        if (multi == 1 || multi == 4) [engine makeBanked:multi];
        engine->live.mode = multi ? 12 : 7; engine->live.multiplayer = multi;
        engine->live.protectedValuesValid = multi != 3;
        PHBattleSession *session = [PHBattleSession.alloc initWithEngine:engine];
        [session onFrame:BankFrame() atTime:100];
        [session queueApplySlot:0 targetText:@"200"]; [session queueRestore];
        [session onFrame:BankFrame() atTime:101];
        CheckSessionDisabled(session);
        Check(!engine->changes && !engine->validations && !engine->entryCalls && !engine->rebindCalls,
            "banked, disconnected, unknown and mode-7 transitional states all reject writes");
    }
    for (NSUInteger scenario = 0; scenario < 5; ++scenario) {
        FakeBattleEngine *engine = [FakeBattleEngine new];
        PHBattleSession *session = [PHBattleSession.alloc initWithEngine:engine];
        PHBattleLoadout team[6]; FullTeam(team); [session configureAutomaticLoadouts:team mask:63];
        PHBattleFrame frame = BankFrame();
        if (scenario == 3) {
            frame.autoApplyTeam = true; engine->live.present = false;
            [session onFrame:frame atTime:100]; FreshEntry(engine); engine->live.input.rawActiveMask = 1;
            [session onFrame:frame atTime:100.01];
            Check([session.snapshot[@"automaticWaiting"] boolValue], "transition fixture has a pending solo entry");
        } else {
            [session onFrame:frame atTime:100]; [session queueLoadouts:team mask:63];
            if (scenario) [session onFrame:frame atTime:100.1];
            if (scenario == 2) [session queueRestore];
            if (scenario == 4) frame.enabled = false;
        }
        NSUInteger changes = engine->changes, applied = engine->fullApplies, restores = engine->fullRestores;
        engine->live.multiplayer = 1; engine->live.input.rawActiveMask = 0;
        [session onFrame:frame atTime:101]; CheckSessionDisabled(session);
        [session queueRestore]; [session onFrame:frame atTime:101.1];
        Check(engine->changes == changes && engine->fullApplies == applied && engine->fullRestores == restores &&
            !engine->entryCalls && !engine->rebindCalls,
            "a multiplayer transition cancels queued apply, restore, auto-entry and disable-triggered restoration");

        // A reset selector or an own-turn phase must not revive canceled work.
        engine->live.multiplayer = 0; engine->live.mode = 0;
        frame.enabled = frame.autoApplyTeam = true;
        FreshEntry(engine); [session onFrame:frame atTime:102];
        [session queueLoadouts:team mask:63]; [session onFrame:frame atTime:102.1];
        CheckSessionDisabled(session);
        Check(engine->changes == changes && !engine->entryCalls, "selector reset within the same battle cannot enable writes");

        // Emulate native scene cleanup, then a genuinely new single-player entry.
        FakeBattleEngine *fresh = [FakeBattleEngine new]; engine->live = fresh->live;
        engine->live.present = false; [session onFrame:frame atTime:103];
        engine->live.identity.entryHash[0] = 2; FreshEntry(engine);
        [session onFrame:frame atTime:103.01];
        Check(engine->entryCalls == 1 && engine->fullApplies == applied + 6 &&
            ![session.snapshot[@"multiplayerDisabled"] boolValue] && [session.snapshot[@"initialBoost"] isEqual:@8],
            "leaving multiplayer restores normal automatic replacement and SB on a fresh solo entry");
    }
}
