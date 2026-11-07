// Production native boundary against memory fixtures, no network or timer
// edits by the product. Fixture changes model an external native event.
static bool BeginThreeFishingNative(id<PHOrbEngine> engine, PHOrbSnapshot &state) {
    NSString *reason = nil;
    if (![engine read:&state frame:Frame() reason:&reason]) return false;
    [engine armMultiplayerOnce:YES]; [engine allowSpinnerEntry:0];
    if (![engine stageCell:1 down:YES beginning:YES expected:&state frame:Frame() reason:&reason]) return false;
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset,state.board.tokens[1]);
    return [engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason];
}
static void ThreeSoloFishingNativeTests() {
    for (unsigned seat = 0; seat < 3; ++seat) {
        Reset(); SetNativeContinuation(seat); SpinnerFixture(6,5,{0,10,19,22});
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && !state.multiplayer.clockKnown,
            "four-spinner native continuation uses its move timer with no retired decision clock");
        [engine allowSpinnerEntry:0];
        Check(![engine stageCell:1 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "opening a continuation spinner grants no unarmed gesture");
        const bool began = BeginThreeFishingNative(engine,state);
        Check(began,"every continuation local seat can expressly begin a fishing gesture"); if (!began) continue;
        Put<uint64_t>(Impl + PHOrbMultiTurnClockOffset,98765);
        Put<float>(Impl + PHOrbMultiTurnClockOffset + 20,0.1f);
        Check([engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason],
            "continuation owned hold is unaffected by stale peer clock contents");
        NativeInputFrame(); Check([engine finishInput:&reason],"continuation hold restores native input");
        Check([engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason],
            "continuation may take from the exact opened stable spinner");
        NativeInputFrame(); Check([engine finishInput:&reason],"continuation take restores native input");
        [engine armMultiplayerOnce:NO]; [engine allowSpinnerEntry:-1];
        const auto before = writes;
        Check(![engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "continuation cleanup cannot target the unheld spinner token");
        Check([engine stageCell:1 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] &&
            [engine finishInput:&reason],"continuation cleanup releases only its captured token");
    }
    // Both selected native recovery sources and owner flags remain valid in
    // isolation. Changing either still retires the previous bound gesture.
    for (bool owner : {false,true}) {
        Reset(); SetNativeContinuation(1); SpinnerFixture(6,5,{0});
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot old{}, current{}; NSString *reason = nil;
        Check([engine read:&old frame:Frame() reason:&reason],"continuation source fixture reads its original snapshot");
        if (owner) Put<uint8_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride,3);
        else Put<uint8_t>(Impl + PHOrbMultiRecoverySourceOffset,1);
        Check([engine read:&current frame:Frame() reason:&reason] && PHOrbCanPreview(&current) &&
            !PHOrbSameTurn(&old,&current) && !writes,
            "native snapshot captures changed valid recovery source or owner flags without preserving old turn identity");
    }
    for (unsigned change = 0; change < 7; ++change) {
        Reset(); SetNativeContinuation(1); SpinnerFixture(6,5,{0});
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason],"continuation pre-hold fixture reads");
        [engine armMultiplayerOnce:YES]; [engine allowSpinnerEntry:0];
        if (change == 0) Put<uint16_t>(Impl + PHOrbRemainingOffset,12);
        if (change == 1) Put<uint8_t>(Impl + PHOrbMultiRecoverySourceOffset,1);
        if (change == 2) Put<uint8_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride,3);
        if (change == 3) Protected(Impl + PHBattlePhaseOffset,22,0x766cfc6d);
        if (change == 4) changeFishingDuringCoordinateRead = 2;
        if (change == 5) changeFishingDuringCoordinateRead = 4;
        if (change == 6) changeFishingDuringCoordinateRead = 5;
        Check(![engine stageCell:1 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes && GlobalInput() == Input,
            "short move clock, source/owner change or skill phase before or during conversion blocks the first fishing touch");
    }
    for (unsigned cell : {0u,1u}) for (bool duringConversion : {false,true}) {
        Reset(); SetNativeContinuation(1); SpinnerFixture(6,5,{0});
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        const bool began = BeginThreeFishingNative(engine,state);
        Check(began,"continuation short move-clock fixture begins"); if (!began) continue;
        const unsigned before = writes;
        if (duringConversion) changeFishingDuringCoordinateRead = 2;
        else Put<uint16_t>(Impl + PHOrbRemainingOffset,12);
        Check(![engine stageCell:cell down:YES beginning:NO expected:&state frame:Frame() reason:&reason] &&
            writes == before && GlobalInput() == Input,
            "short continuation move time rejects both a held wait and a take before leasing input");
        [engine armMultiplayerOnce:NO]; [engine allowSpinnerEntry:-1];
        Check([engine read:&state frame:Frame() reason:&reason] &&
            [engine stageCell:1 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && [engine finishInput:&reason],
            "positive short move time retains only a same-window owned cleanup release");
    }
    // Exercise actual production input gates for swaps, stationary waits and
    // releases, including changes inside native coordinate conversion.
    for (unsigned action = 0; action < 3; ++action) for (unsigned change = 0; change < 16; ++change) {
        Reset(); SetNativeContinuation(1); SpinnerFixture(6,5,{0});
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        const bool began = BeginThreeFishingNative(engine,state);
        Check(began,"continuation ownership withdrawal fixture begins"); if (!began) continue;
        const unsigned before = writes;
        if (change == 0) Put<uint8_t>(Impl + PHOrbMultiRecoverySourceOffset,1);
        if (change == 1) Put<uint8_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride,3);
        if (change == 2) Put<uint8_t>(Impl + PHOrbMultiRecoveryFlagsOffset,0x40);
        if (change == 3) Put<uintptr_t>(Impl + PHOrbMultiDialogOffset,Impl + 1);
        if (change == 4) Protected(Impl + PHBattlePhaseOffset,22,0x766cfc6d);
        if (change == 5) SetNativeMultiplayer(3,1);
        if (change == 6) Put<uintptr_t>(Impl + PHBattleDragPointerOffset,state.board.tokens[2]);
        if (change == 7) Put<uint32_t>(Impl + PHBattleCurrentTeamOffset,2);
        if (change == 8) Put<uintptr_t>(Header + PHBattleInputRVA,reinterpret_cast<uintptr_t>(newInput.data()));
        if (change == 9) Put<uintptr_t>(Impl + PHBattleTeamDescriptorOffset + PHBattleTeamDescriptorStride + PHBattleTeamPointerOffset,Header);
        if (change == 10) changeFishingDuringCoordinateRead = 4;
        if (change == 11) changeFishingDuringCoordinateRead = 5;
        if (change == 12) changeFishingDuringCoordinateRead = 6;
        if (change == 13) changeFishingDuringCoordinateRead = 7;
        if (change == 14) changeContinuationDuringCoordinateRead = true;
        if (change == 15) changeTurnDuringCoordinateRead = true;
        const uintptr_t wantedInput = change == 8 || change == 13 ? reinterpret_cast<uintptr_t>(newInput.data()) : Input;
        Check(![engine stageCell:(action ? 1 : 0) down:(action != 2) beginning:NO expected:&state frame:Frame() reason:&reason] &&
            writes == before && GlobalInput() == wantedInput,
            "continuation ownership, phase, token, input or rejoin change blocks stale take, wait and release before native input writes");
        Check(![engine stageCell:1 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "a revoked continuation context cannot receive a later stale cleanup release");
    }
}
static void ThreeFishingNativeTests() {
    ThreeSoloFishingNativeTests();
    for (unsigned players : {3u,4u,8u}) {
        Reset(); SetNativeMultiplayer(players); SpinnerFixture(6,5,{0});
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason],"multiplayer fishing boundary reads its board");
        [engine allowSpinnerEntry:0];
        Check(![engine stageCell:1 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "opening a spinner never grants an unarmed multiplayer gesture");
        [engine armMultiplayerOnce:YES];
        const BOOL began = [engine stageCell:1 down:YES beginning:YES expected:&state frame:Frame() reason:&reason];
        Check(bool(began) == (players == 3),"only expressly armed 3P may begin with a fishing cell open");
        if (!began) continue;
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset,state.board.tokens[1]);
        Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason],
            "3P fishing acknowledges the originally selected token");
        Check([engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason],
            "3P local once may enter the exact opened stable spinner");
        NativeInputFrame(); Check([engine finishInput:&reason],"3P fishing input restores the original manager");
        // The memory fixture does not execute swaps; the held token remains
        // at cell 1. Only that observed owned cell may receive cleanup.
        [engine armMultiplayerOnce:NO]; [engine allowSpinnerEntry:-1];
        const unsigned before = writes;
        Check(![engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "a revoked 3P fishing lease cannot release another cell's token");
        Check([engine stageCell:1 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] &&
            [engine finishInput:&reason],"revocation retains only owned-token cleanup");
    }
    for (unsigned change = 0; change < 9; ++change) {
        Reset(); SetNativeMultiplayer(3); SpinnerFixture(6,5,{0});
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        const bool began = BeginThreeFishingNative(engine,state);
        Check(began,"native 3P clock/identity withdrawal fixture begins"); if (!began) continue;
        const unsigned before = writes;
        if (change == 0) Put<float>(Impl + PHOrbMultiTurnClockOffset + 20,0.2f);
        if (change == 1) Put<uint16_t>(Impl + PHOrbRemainingOffset,12);
        if (change == 2) changeTurnDuringCoordinateRead = true;
        if (change == 3) changeFishingDuringCoordinateRead = 1;
        if (change == 4) changeFishingDuringCoordinateRead = 2;
        if (change == 5) changeFishingDuringCoordinateRead = 3;
        if (change == 6) Put<uint16_t>(state.board.tokens[0] + 18,3);
        if (change == 7) Protected(Impl + PHOrbSpinnerPeriodOffset + 16,5,0x6a875a8b);
        if (change == 8) Put<uintptr_t>(Impl + PHBattleDragPointerOffset,state.board.tokens[2]);
        Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] &&
            writes == before && GlobalInput() == Input,
            "clock loss, handoff, phase/color change during conversion, motion or foreign drag rejects the take before leasing input");
        if (change == 2 || change == 8) {
            Check(![engine stageCell:1 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
                "turn or held-token change cannot receive a stale cleanup release");
        } else {
            [engine armMultiplayerOnce:NO]; [engine allowSpinnerEntry:-1];
            Check([engine read:&state frame:Frame() reason:&reason] &&
                [engine stageCell:1 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] &&
                [engine finishInput:&reason],"same-window owned cleanup remains possible after a rejected take");
        }
    }
}
