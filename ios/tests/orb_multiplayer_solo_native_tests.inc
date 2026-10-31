// Synthetic native memory only; device disconnection/synchronization is not
// emulated. These cases must run in the next explicitly authorized build.
static void SetNativeContinuation(unsigned seat) {
    SetNativeMultiplayer(3, seat); SetNativeLeaderOwner(seat);
    Put<uint32_t>(Header + PHBattleMultiplayerRVA, 2);
    Put<uint32_t>(Header + PHOrbMultiConnectionRVA, 0);
    Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 0);
    Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 0);
    Put<uint32_t>(Impl + PHOrbMultiSyncStepOffset, 0);
    Put<uint8_t>(Impl + PHOrbMultiRecoverySourceOffset, 0);
    Put<uint8_t>(Impl + PHOrbMultiRecoveryFlagsOffset, 0);
    Put<uintptr_t>(Impl + PHOrbMultiDialogOffset, 0);
    // Turn order and decision clock are retired, not reset to a fake local turn.
    Put<uint32_t>(Impl + PHBattleTurnSerialOffset, (seat + 1) % 3);
    Put<float>(Impl + PHOrbMultiTurnClockOffset + 20, 0);
    for (unsigned peer = 0; peer < 3; ++peer) if (peer != seat)
        Put<uint32_t>(Impl + PHBattleTeamDescriptorOffset + peer * PHBattleTeamDescriptorStride + 28, 6);
}

static void MultiplayerSoloNativeTests() {
    for (unsigned seat = 0; seat < 3; ++seat) {
        Reset(); SetNativeContinuation(seat);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        // The real log failed an overbroad tail check before reading seats.
        // Only the actual core and modal pointers must remain mapped.
        implMappedBytes = PHOrbMultiDialogOffset + sizeof(uintptr_t);
        const auto originalGuest = guest, originalImpl = implBytes;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
            state.multiplayer.kind == PHOrbMultiplayerThreeSolo && state.multiplayer.continuationOwnerKnown &&
            !state.multiplayer.clockKnown && !state.multiplayer.connectionState && !writes,
            "3P solo continuation reads all local seats without retired turn/clock or unused allocation-tail pages");
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.valid &&
            !state.automation.window && !state.thorns.known && guest == originalGuest && implBytes == originalImpl,
            "co-op continuation never enables solo skill, HP, timer or team mutation paths");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "continuation readiness does not arm a gesture");
        StartNativeMultiplayer(engine, state);
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
            "one explicitly armed continuation gesture can release its own token");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Protected(Impl + PHBattlePhaseOffset, 3, 0x766cfc6d);
        Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason] && !state.dragged,
            "continuation release remains acknowledged through resolving metadata");
        Protected(Impl + PHBattlePhaseOffset, 2, 0x766cfc6d);
        Put<uint32_t>(Impl + PHOrbMoveSerialOffset, 0);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state),
            "continuation next board can become ready");
        const unsigned before = writes;
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == before,
            "next continuation board needs a second explicit tap");
    }
    for (unsigned seat = 0; seat < 2; ++seat) {
        Reset(); SetNativeContinuation(seat);
        Put<uint32_t>(Impl + PHBattleTeamCountOffset, 2);
        Put<uint8_t>(Impl + PHOrbMultiRecoverySourceOffset, 1);
        Put<uint8_t>(Impl + PHOrbMultiRecoveryFlagsOffset, 0x40);
        Put<uint8_t>(Impl + PHOrbMultiRecoveryFlagsOffset + 6, 0);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
            state.multiplayer.teamCount == 2 && state.multiplayer.recoverySource == 1 && !state.multiplayer.recoveryFlags,
            "mode 7 continuation retains a two-seat descriptor bank and checks only the selected recovery source");
    }
    for (unsigned failure = 0; failure < 14; ++failure) {
        Reset(); SetNativeContinuation(1);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        if (failure == 0) Put<uint32_t>(Impl + PHBattleCurrentTeamOffset, 2);
        if (failure == 1) Put<uint32_t>(Impl + PHBattleTeamCountOffset, 1);
        if (failure == 2) Put<uint32_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride + 28, 6);
        if (failure == 3) Put<uint8_t>(Impl + PHOrbMultiRecoveryFlagsOffset, 0x40);
        if (failure == 4) Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 3);
        if (failure == 5) Put<uint32_t>(Impl + PHOrbMultiSyncStepOffset, 1);
        if (failure == 6) Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 0x10001);
        if (failure == 7) Put<uintptr_t>(Impl + PHOrbMultiDialogOffset, Impl + 1);
        if (failure == 8) Put<uint8_t>(Impl + PHBattleTeamBusyOffset, 8);
        if (failure == 9) Put<uintptr_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride + PHBattleTeamPointerOffset, Header);
        if (failure == 10) Put<uint8_t>(Impl + PHOrbMultiRecoverySourceOffset, 2);
        if (failure == 11) { Put<uint8_t>(Impl + PHOrbMultiRecoverySourceOffset, 1); Put<uint8_t>(Impl + PHOrbMultiRecoveryFlagsOffset + 6, 0x40); }
        if (failure == 12) Put<uint16_t>(Impl + PHOrbRemainingOffset, 0);
        if (failure == 13) Put<uint8_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride, 0);
        const BOOL read = [engine read:&state frame:Frame() reason:&reason];
        Check((!read || !PHOrbCanPreview(&state)) && !state.board.tokens[0] && !writes,
            "incomplete native continuation, remote view, modal, pending recovery and missing timer stop before board access");
    }
    for (unsigned failure = 0; failure < 4; ++failure) {
        Reset(); SetNativeContinuation(1);
        if (failure == 0) implMappedBytes = PHOrbDialogOffset + sizeof(uintptr_t) - 1;
        if (failure == 1) implMappedBytes = PHOrbMultiDialogOffset + sizeof(uintptr_t) - 1;
        if (failure == 2) dialogProtection = VM_PROT_READ;
        if (failure == 3) dialogProtection = VM_PROT_READ | VM_PROT_WRITE | VM_PROT_EXECUTE;
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check(![engine read:&state frame:Frame() reason:&reason] && !state.valid && !writes &&
            [reason containsString:@"彈窗記憶體"],
            "exact modal protection checks still reject truncated, readonly and executable mappings");
    }
    {
        Reset();
        implMappedBytes = PHOrbDialogOffset + sizeof(uintptr_t);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state),
            "ordinary solo reading also does not require unused tail pages after its actual modal pointer");
    }
    for (unsigned timing = 0; timing < 2; ++timing) {
        Reset(); SetNativeMultiplayer(3, 1);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot old{}, state{}; NSString *reason = nil;
        if (timing) StartNativeMultiplayer(engine, old);
        else { [engine read:&old frame:Frame() reason:&reason]; [engine armMultiplayerOnce:YES]; }
        SetNativeContinuation(1);
        const unsigned before = writes;
        Check([engine read:&state frame:Frame() reason:&reason] && !state.multiplayerDisabled &&
            ![engine stageCell:0 down:YES beginning:NO expected:&old frame:Frame() reason:&reason] &&
            ![engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "same-entry 1 to 2 transition revokes an old arm or held token even when native token memory is reused");
        Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state), "continuation recovers after native selection clears");
        StartNativeMultiplayer(engine, state);
        SetNativeMultiplayer(3, 1);
        const unsigned reverseWrites = writes;
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled &&
            !state.board.tokens[0] && writes == reverseWrites, "same-entry continuation cannot switch back to online ownership");
        SetNativeContinuation(1);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled,
            "a reversed/unknown selector latch cannot be cleared by repeating selector 2");
        Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Put<uint8_t>(Header + PHBattleEntryHashRVA, 2); SetNativeMultiplayer(3, 1);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && !state.multiplayerDisabled,
            "a verified new 3P entry retires the previous continuation latch without an app restart");
    }
    for (unsigned change = 1; change <= 4; ++change) {
        Reset(); SetNativeContinuation(1); changeContinuationDuringRead = change;
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check(![engine read:&state frame:Frame() reason:&reason] && !state.valid && !writes,
            "recovery byte, modal, selector and owner-flag races invalidate the whole continuation snapshot");
    }
    {
        NativeLeaderFixture(3, 1); SetNativeContinuation(1);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbLeaderContextReady(&state),
            "continuation preserves independently validated local leader/helper provenance");
        [engine armMultiplayerOnce:YES]; changeContinuationDuringCoordinateRead = true;
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "recovery restarted during coordinate conversion cannot acquire a private input lease");
    }
}
