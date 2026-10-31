// Read-only native fixtures, not a network simulation. Cases are unrun until
// the next explicit build/test request; all mutations below are fixture data.
static void SetNativePeerExit(unsigned peer, uint32_t status = 5, uint32_t sync = 0) {
    Put<uint32_t>(Impl + PHOrbMultiDisconnectSeatOffset, peer);
    Put<uint32_t>(Impl + PHBattleTeamDescriptorOffset + peer * PHBattleTeamDescriptorStride + 28, status);
    Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 0x10003);
    Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, sync);
    Put<uint32_t>(Impl + PHOrbMultiSyncStepOffset, 0);
}
static void MultiplayerPeerNativeTests() {
    for (unsigned seat = 0; seat < 4; ++seat) for (unsigned peer = 0; peer < 4; ++peer) {
        if (seat == peer) continue;
        for (uint32_t status : {5u, 6u}) for (uint32_t sync : {0u, 15u}) {
            Reset(); SetNativeMultiplayer(4, seat); SetNativePeerExit(peer, status, sync);
            id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
            const auto originalImpl = implBytes, originalGuest = guest;
            Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
                state.multiplayer.disconnectKnown && state.multiplayer.disconnectSeat == peer &&
                state.multiplayer.disconnectStatus == status && state.board.tokens[0] &&
                implBytes == originalImpl && guest == originalGuest && !writes,
                "4P native read distinguishes an exited peer from the live local seat without modifying the game");
            Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
                "peer exit readiness does not itself arm a native gesture");
        }
    }
    for (unsigned players : {3u, 4u, 8u}) {
        Reset(); SetNativeMultiplayer(players, 1);
        Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 5);
        Put<uint32_t>(Impl + PHOrbMultiSyncStepOffset, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) == (players == 4) &&
            bool(state.board.tokens[0]) == (players == 4) && !writes,
            "initialized peer recovery reads the board only in 4P; 3P and 8P keep their earlier behavior");
    }
    {
        NativeLeaderFixture(4, 2); SetNativeExtraHelper(); SetNativePeerExit(0);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbLeaderContextReady(&state) &&
            state.leaderContext.extraHelper && state.leaderContext.skillIDs[0] == 66063 &&
            state.leaderContext.skillIDs[1] == 0 && !writes,
            "peer departure preserves the local leader source and the inactive 4P helper's lack of leader skill");
    }
    for (unsigned failure = 0; failure < 10; ++failure) {
        Reset(); SetNativeMultiplayer(4, 2); SetNativePeerExit(0);
        if (failure == 0) Put<uint32_t>(Impl + PHOrbMultiDisconnectSeatOffset, 4);
        if (failure == 1) Put<uint32_t>(Impl + PHOrbMultiDisconnectSeatOffset, UINT32_MAX);
        if (failure == 2) SetNativePeerExit(2);
        if (failure == 3) SetNativePeerExit(0, 3);
        if (failure == 4) Put<uint32_t>(Header + PHOrbMultiConnectionRVA, 0);
        if (failure == 5) Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 2);
        if (failure == 6) Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 0x10013);
        if (failure == 7) Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 5); // Entry step 0 clears selection.
        if (failure == 8) Put<uint8_t>(Impl + PHOrbMultiFinishedOffset, 1);
        if (failure == 9) Put<uint8_t>(Impl + PHOrbMultiResolveOffset, 1);
        Put<uintptr_t>(Impl + PHOrbTableOffset, 1); // Rejected windows must not read the orb table.
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        const BOOL read = [engine read:&state frame:Frame() reason:&reason];
        Check((failure < 2 ? !read : read) && !PHOrbCanPreview(&state) && !state.board.tokens[0] && !writes,
            "invalid exit seats fail before dereference; local loss, recovery entry and resolution stay metadata only");
        [engine armMultiplayerOnce:YES];
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "a peer-exit marker cannot authorize an ineligible local board");
    }
    for (unsigned race = 1; race <= 3; ++race) {
        Reset(); SetNativeMultiplayer(4, 2); SetNativePeerExit(0);
        Put<uint32_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride + 28, 5);
        Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 5);
        Put<uint32_t>(Impl + PHOrbMultiSyncStepOffset, 2);
        changePeerDuringMultiRead = race;
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check(![engine read:&state frame:Frame() reason:&reason] && !state.valid && !writes,
            "exit seat, peer descriptor and recovery substep must agree across both native reads");
    }
    {
        Reset(); SetNativeMultiplayer(4, 2); SetNativePeerExit(0);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "peer coordinate race starts from a valid local board");
        [engine armMultiplayerOnce:YES]; changePeerDuringCoordinateRead = true;
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "a local departure during coordinate conversion rejects began before swapping the input manager");
    }
    {
        Reset(); SetNativeMultiplayer(4, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        StartNativeMultiplayer(engine, state);
        SetNativePeerExit(0, 5, 15);
        Check([engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason],
            "departure of another seat preserves a validated owned local gesture");
        NativeInputFrame(); Check([engine finishInput:&reason], "peer departure hold restores the real manager");
        Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 0);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbMultiplayerWindow(&state),
            "retained peer marker remains playable after the completion callback");
        [engine armMultiplayerOnce:NO];
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
            "peer departure does not prevent release of the same owned local token");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Check([engine finishInput:&reason], "peer departure release restores the real manager");
        Put<uint8_t>(Impl + PHOrbMultiResolveOffset, 1);
        Check([engine read:&state frame:Frame() reason:&reason] && !PHOrbCanPreview(&state),
            "peer exit still waits for native resolution after release");
        Put<uint8_t>(Impl + PHOrbMultiResolveOffset, 0);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state),
            "next local board can recover with the peer marker still set");
        const unsigned before = writes;
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == before,
            "the next board after peer departure cannot reuse the first gesture's arm");
        [engine armMultiplayerOnce:YES];
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
            "a second explicit arm may start with a validated remote exit marker");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
        Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason],
            "second peer-exit gesture independently acknowledges its token");
        [engine armMultiplayerOnce:NO];
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
            "second peer-exit gesture releases only its own token");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Check([engine finishInput:&reason] && GlobalInput() == Input,
            "two explicit peer-exit gestures leave the real input manager restored");
    }
    for (unsigned interruption = 0; interruption < 3; ++interruption) {
        Reset(); SetNativeMultiplayer(4, 2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        StartNativeMultiplayer(engine, state);
        if (interruption == 0) { // Native recovery entry clears the selected orb.
            Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 5);
            Put<uint32_t>(Impl + PHOrbMultiSyncStepOffset, 2);
            Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        }
        if (interruption == 1) SetNativePeerExit(2);
        if (interruption == 2) Put<uint32_t>(Header + PHOrbMultiConnectionRVA, 0);
        const unsigned before = writes;
        Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] &&
            ![engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "native selection reset or local departure revokes the old hold and never releases into an unowned window");
    }
}
