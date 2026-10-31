// One engine survives the whole sequence. No intervening menu read is needed,
// and fixture transitions never call game networking or modify real peers.
static void MultiplayerEntryNativeTests() {
    for (unsigned from : {3u, 4u, 8u}) for (unsigned to : {3u, 4u, 8u}) {
        Reset(); SetNativeMultiplayer(from, from - 1);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}, old{}; NSString *reason = nil;
        Check([engine read:&old frame:Frame() reason:&reason] && PHOrbCanPreview(&old),
            "mode-switch fixture captures the previous match in one persistent engine");
        [engine armMultiplayerOnce:YES];
        PHBattleFrame unavailable = Frame(); unavailable.ready = false;
        Check(![engine read:&state frame:unavailable reason:&reason] && !state.valid,
            "unobserved lobby/background does not count as a confirmed scene exit");
        SetNativeMultiplayer(to, to - 1);
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 2);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
            !state.multiplayerDisabled && state.multiplayer.localSeat == to - 1 && !writes,
            "a verified new entry accepts 4P to 3P and every 3/4/8P pair without restarting the engine");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&old frame:Frame() reason:&reason] &&
            ![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "new-entry readiness carries neither the old arm nor a stale prepared gesture");
    }
    {
        Reset(); SetNativeMultiplayer(4, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "same-entry transition observes 4P");
        SetNativeMultiplayer(3, 1);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled &&
            !state.board.tokens[0], "changing only mode within the same entry remains latched before board access");
        Put<uintptr_t>(Impl + PHOrbTableOffset, 1);
        Put<uint16_t>(Header + PHSaveRVA + PHBattleDungeonOffset, 7);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled &&
            !state.board.tokens[0] && !writes, "a different dungeon or floor alone cannot retire a multiplayer latch");
        Reset(); SetNativeMultiplayer(3, 1); // Same native allocation and old entry nonce.
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 2);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && !state.multiplayerDisabled,
            "a confirmed new entry recovers an already-latched engine with reused game memory");
        StartNativeMultiplayer(engine, state);
        [engine armMultiplayerOnce:NO];
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
            "recovered 3P entry accepts exactly one fresh explicit local gesture");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Check([engine finishInput:&reason] && GlobalInput() == Input,
            "cross-match recovery restores the input manager after the new gesture");
    }
    for (unsigned failure = 0; failure < 4; ++failure) {
        Reset(); SetNativeMultiplayer(4, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "failed new-entry fixture first captures 4P");
        SetNativeMultiplayer(3, 1);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled,
            "failed new-entry fixture starts with an established latch");
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 2);
        if (failure == 0) Put<uint8_t>(Header + PHBattleEntryHashRVA, 0);
        if (failure == 1) Put<uint16_t>(Header + PHSaveRVA, 0x0001);
        if (failure == 2) changeHashDuringEntryRead = true;
        if (failure == 3) { Put<uint8_t>(Header + PHBattleEntryHashRVA, 3); changeHashDuringBoardRead = true; }
        Check(![engine read:&state frame:Frame() reason:&reason] && !state.valid && !writes,
            "empty nonce, loading scene, and early/late identity races cannot publish a new entry");
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 1); Put<uint16_t>(Header + PHSaveRVA, 0x0101);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled && !state.board.tokens[0],
            "a failed replacement read does not erase the original latch or rebind its entry");
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 2);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state),
            "a later complete read of the new match recovers without restarting");
    }
    {
        Reset(); SetNativeMultiplayer(4, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot old{}, state{}; NSString *reason = nil;
        StartNativeMultiplayer(engine, old);
        SetNativeMultiplayer(3, 1); Put<uint8_t>(Header + PHBattleEntryHashRVA, 2);
        // Deliberately reuse even the selected token to ensure identity wins.
        const unsigned before = writes;
        Check([engine read:&state frame:Frame() reason:&reason] && !state.multiplayerDisabled &&
            ![engine stageCell:0 down:YES beginning:NO expected:&old frame:Frame() reason:&reason] &&
            ![engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "a new match revokes old token ownership even when native memory and dragged token are reused");
    }
    {
        Reset(); SetNativeMultiplayer(4, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "account-epoch fixture starts in 4P");
        SetNativeMultiplayer(3, 1); PHBattleFrame next = Frame(); ++next.epoch;
        Check([engine read:&state frame:next reason:&reason] && PHOrbCanPreview(&state) && !writes,
            "a verified new account epoch cannot inherit another epoch's multiplayer mode");
    }
    {
        Reset(); SetNativeMultiplayer(4, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "new unsupported-entry fixture observes 4P");
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 2); Put<uint16_t>(Header + PHBattleModeRVA, 12);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled && !state.board.tokens[0],
            "a new entry cannot authorize an unsupported multiplayer mode");
        SetNativeMultiplayer(3, 1);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled,
            "an unsupported mode latches its own new entry until another verified entry");
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 3);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state),
            "the following supported match is independent of the unsupported entry");
    }
}
