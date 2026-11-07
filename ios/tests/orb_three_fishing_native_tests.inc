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
static void ThreeFishingNativeTests() {
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
