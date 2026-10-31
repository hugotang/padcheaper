// This exercises the production reader and private-manager lease against a
// memory fixture. Native networking is deliberately never called or emulated.
static void SetNativeMultiplayer(unsigned players, unsigned seat = 0) {
    Put<uint16_t>(Header + PHBattleModeRVA, players == 3 ? 7 : players == 4 ? 11 : 10);
    Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    Put<uint32_t>(Impl + PHBattleTurnSerialOffset, players == 3 ? seat : 0);
    Put<uint32_t>(Impl + PHBattlePlayerTeamOffset, seat);
    Put<uint32_t>(Impl + PHBattleTeamCountOffset, players);
    Put<uint32_t>(Impl + PHBattleCurrentTeamOffset, seat);
    Put<uint32_t>(Impl + PHBattleTeamDescriptorOffset + seat * PHBattleTeamDescriptorStride + 28, 3);
    Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 1);
    Put<uint32_t>(Header + PHOrbMultiConnectionRVA, 2);
    Put<uint32_t>(Header + PHOrbMultiClockNumeratorRVA, 1);
    Put<uint32_t>(Header + PHOrbMultiClockDenominatorRVA, 1);
    Put<float>(Impl + PHOrbMultiTurnClockOffset + 20, 60);
    if (players != 3) {
        Put<uint8_t>(Impl + PHOrbMultiCountdownOffset, 1);
        Put<uint64_t>(Impl + PHOrbMultiMatchClockOffset, mach_absolute_time());
        Put<uint8_t>(Impl + PHOrbMultiMatchClockOffset + 24, 4);
        const uintptr_t dungeon = Header + 0x1800000;
        Put<uintptr_t>(Impl + PHOrbSkillDungeonRulesOffset, dungeon);
        Put<uint16_t>(dungeon + 12, 120);
    }
}
static void StartNativeMultiplayer(id<PHOrbEngine> engine, PHOrbSnapshot &state) {
    NSString *reason = nil;
    Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state), "native multiplayer start reads local board");
    [engine armMultiplayerOnce:YES];
    Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
        "explicit native multiplayer begin acquires the private manager");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
    Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason],
        "multiplayer token is acknowledged after original manager restoration");
}
#include "orb_multiplayer_leader_native_tests.inc"
#include "orb_multiplayer_peer_native_tests.inc"
#include "orb_multiplayer_entry_native_tests.inc"

static void MultiplayerNativeTests() {
    MultiplayerEntryNativeTests();
    MultiplayerPeerNativeTests();
    MultiplayerLeaderNativeTests();
    for (unsigned players : {3u,4u,8u}) for (unsigned seat = 0; seat < players; ++seat) {
        Reset(); SetNativeMultiplayer(players, seat);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        const auto originalImpl = implBytes, originalGuest = guest, originalInput = realInput;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
            !state.automation.valid && !state.automation.window && !state.leadersValid && !state.thorns.known && writes == 0,
            "multiplayer without optional leader data retains the local board without solo skill or HP derivation");
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.valid &&
            implBytes == originalImpl && guest == originalGuest && realInput == originalInput && writes == 0,
            "readAutomation cannot enter multiplayer skill or bank routines");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == 0,
            "a readable multiplayer board alone never authorizes input");
        [engine armMultiplayerOnce:YES];
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
            "explicit multiplayer once begins one native gesture");
        NativeInputFrame(); Check([engine finishInput:&reason] && GlobalInput() == Input, "deferred began restores real manager");
        Check([engine read:&state frame:Frame() reason:&reason] && !state.dragged, "multiplayer tall-layout began can defer selection");
        [engine armMultiplayerOnce:YES]; // Must not reinitialize the in-flight input bank.
        const unsigned before = writes;
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == before,
            "a duplicate multiplayer begin cannot reset the gesture");
        Check(![engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "unacknowledged multiplayer selection cannot change the starting cell");
        Check([engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason],
            "deferred selection continues at the captured start without another began");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
        Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason], "held start acknowledges exactly its token");
        if (players == 3) {
            Put<uint8_t>(Impl + PHOrbMultiTurnClockOffset + 8, 3);
            Put<uint32_t>(Impl + PHOrbMultiTurnClockOffset + 16, 1000u ^ 0x33c2f3a1u);
            Check([engine read:&state frame:Frame() reason:&reason] && PHOrbMultiplayerWindow(&state) &&
                state.multiplayer.remainingSeconds == 50, "native co-op clock pause after a swap remains readable for this held gesture");
        }
        [engine armMultiplayerOnce:NO];
        const unsigned stoppedWrites = writes;
        Check(![engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] && writes == stoppedWrites,
            "revocation immediately forbids another multiplayer hold");
        for (unsigned retry = 0; retry < 2; ++retry) {
            Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
                "owned multiplayer release remains available after revocation until acknowledged");
            NativeInputFrame(); Check([engine finishInput:&reason], "each release retry restores the manager");
            Check([engine read:&state frame:Frame() reason:&reason], "unaccepted release keeps an observable owned token");
        }
        Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Protected(Impl + PHBattlePhaseOffset, 3, 0x766cfc6d);
        Put<uintptr_t>(Impl + PHOrbTableOffset, 0); // Phase 3 board objects are no longer a route source.
        Check([engine read:&state frame:Frame() reason:&reason] && state.valid && !state.dragged &&
            !PHOrbCanPreview(&state) && GlobalInput() == Input && realInput == originalInput,
            "multiplayer release acknowledgement uses identity metadata while preserving all real touch bytes");
    }
    for (unsigned change = 0; change < 9; ++change) {
        Reset(); SetNativeMultiplayer(3,1);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        StartNativeMultiplayer(engine, state);
        const unsigned before = writes;
        if (change == 0) Put<uint32_t>(Impl + PHBattleTurnSerialOffset, 4);
        if (change == 1) Put<uint32_t>(Impl + PHBattleCurrentTeamOffset, 2);
        if (change == 2) Put<uint32_t>(Impl + PHBattlePlayerTeamOffset, 2);
        if (change == 3) Put<uint32_t>(Impl + PHOrbMultiSyncStateOffset, 1);
        if (change == 4) Put<uint32_t>(Header + PHOrbMultiConnectionRVA, 0);
        if (change == 5) Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 0x11);
        if (change == 6) Put<uint32_t>(Header + PHBattleMultiplayerRVA, 0);
        if (change == 7) Put<float>(Impl + PHOrbResizeProgressOffset + 4, 1);
        if (change == 8) Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[1]);
        Check(![engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] &&
            ![engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "handoff, selector reset, reconnect, resize or hand takeover forbids both stale hold and stale release");
    }
    for (unsigned players : {3u, 4u, 8u}) {
        Reset(); SetNativeMultiplayer(players, players - 1);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        StartNativeMultiplayer(engine, state);
        const PHOrbBoard first = state.board;
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
            "first native gesture can release before the next-board lifecycle");
        NativeInputFrame(); Check([engine finishInput:&reason], "first release restores the native input manager");
        Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Put<uint8_t>(Impl + PHOrbMultiResolveOffset, 1);
        Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 3);
        const auto realAfterRelease = realInput;
        const unsigned afterRelease = writes;
        Check([engine read:&state frame:Frame() reason:&reason] && state.valid && state.phase == 2 &&
            !state.board.tokens[0] && !PHOrbCanPreview(&state) && writes == afterRelease,
            "phase-2 native resolution acknowledges release through metadata only");
        [engine armMultiplayerOnce:YES];
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] &&
            writes == afterRelease, "a resolving board cannot arm a future gesture");
        Put<uint8_t>(Impl + PHOrbMultiResolveOffset, 0);
        Put<uint32_t>(Impl + PHOrbMoveSerialOffset, 0);
        Put<uint16_t>(Impl + PHOrbRemainingOffset, 300);
        for (unsigned i = 0; i < first.width * first.height; ++i)
            Put<uint8_t>(first.tokens[i], uint8_t((first.types[i] + 1) % 6));
        if (players != 4) Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 1);
        if (players == 3) Put<uint32_t>(Impl + PHBattleTurnSerialOffset, players * 2 - 1);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
            state.board.types[0] != first.types[0] && writes == afterRelease,
            "fresh local board returns after resolution, retaining bit 1 only in 4P");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] &&
            writes == afterRelease, "availability alone cannot reuse the previous native arm");
        [engine armMultiplayerOnce:YES];
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
            "a second explicit native arm may begin on the newly read board");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
        Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason],
            "second native begin is independently acknowledged");
        [engine armMultiplayerOnce:NO];
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
            "second gesture retains only its own release after revocation");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason] &&
            realInput == realAfterRelease && GlobalInput() == Input,
            "two successive native one-shots leave the real touch manager intact");
    }
    for (unsigned players : {3u, 4u, 8u}) {
        Reset(); SetNativeMultiplayer(players, players - 1);
        Put<uint32_t>(Impl + PHOrbMultiNetworkFlagsOffset, 3);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) == (players == 4) &&
            bool(state.board.tokens[0]) == (players == 4) && !writes,
            "only 4P reads a full next board with the retained submitted flag; 3P/8P stay metadata only");
    }
    for (unsigned stateChange = 0; stateChange < 8; ++stateChange) {
        Reset(); SetNativeMultiplayer(8,7);
        if (stateChange == 0) Put<uint8_t>(Impl + PHOrbMultiCountdownOffset, 0);
        if (stateChange == 1) Put<uint8_t>(Impl + PHOrbMultiFinishedOffset, 1);
        if (stateChange == 2) Put<uint8_t>(Impl + PHOrbMultiMatchClockOffset + 24, 0x24);
        if (stateChange == 3) Put<uint8_t>(Impl + PHOrbMultiMatchClockOffset + 24, 0x14);
        if (stateChange == 4) Put<uint32_t>(Impl + PHBattleCurrentTeamOffset, 0);
        if (stateChange == 5) Put<uint32_t>(Impl + PHBattleTeamDescriptorOffset + 7 * PHBattleTeamDescriptorStride + 28, 5);
        if (stateChange == 6) Put<uint32_t>(Header + PHOrbMultiClockDenominatorRVA, 0);
        if (stateChange == 7) Put<uint8_t>(Impl + PHOrbMultiResolveOffset, 1);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        const auto before = implBytes;
        Check([engine read:&state frame:Frame() reason:&reason] && !PHOrbCanPreview(&state) &&
            !state.board.tokens[0] && implBytes == before && writes == 0,
            "competitive countdown, result, paused/invalid clock and remote views remain metadata only");
        [engine armMultiplayerOnce:YES];
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == 0,
            "an ineligible competitive window cannot be armed");
    }
    {
        Reset(); SetNativeMultiplayer(3,1);
        Put<uint32_t>(Impl + PHBattleTurnSerialOffset, 0);
        Put<uintptr_t>(Impl + PHOrbTableOffset, 1); // A remote board must not be dereferenced.
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && state.valid && !PHOrbCanPreview(&state) &&
            !state.board.tokens[0] && !writes, "other co-op seats expose only metadata even when their board table is unavailable");
    }
    for (unsigned race = 0; race < 2; ++race) {
        Reset(); SetNativeMultiplayer(3);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        if (race == 0) changeTurnDuringMultiRead = true;
        if (race == 1) changeHashDuringBoardRead = true;
        Check(![engine read:&state frame:Frame() reason:&reason] && !state.valid && !writes,
            "turn or battle identity changes during multiplayer read reject the entire snapshot");
    }
    {
        Reset(); SetNativeMultiplayer(4,2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "unavailable-mode fixture observes multiplayer first");
        PHBattleFrame unavailable = Frame(); unavailable.ready = false;
        Check(![engine read:&state frame:unavailable reason:&reason] && !state.valid && !state.present &&
            state.multiplayer.kind == PHOrbMultiplayerFour && !PHOrbCanPreview(&state) && !writes,
            "an early read failure retains only the observed multiplayer kind, never an executable board");
        Put<uint8_t>(Impl + PHOrbMoveBlockedOffset, 1);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbMultiplayerWindow(&state) && !state.ordinary &&
            [reason containsString:@"特殊操作規則"], "valid multiplayer ownership preserves the specific board-operation error");
    }
    for (unsigned mode : {7u,10u,11u}) {
        Reset(); SetNativeMultiplayer(mode == 7 ? 3 : mode == 10 ? 8 : 4);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state), "selector-reset fixture starts recognized");
        Put<uint32_t>(Header + PHBattleMultiplayerRVA, 0);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled, "zero selector does not turn co-op or versus into solo");
        Put<uint16_t>(Header + PHBattleModeRVA, 0);
        const uintptr_t firstNativeToken = Load<uintptr_t>(reinterpret_cast<const uint8_t *>(Impl + PHOrbTableOffset));
        Put<uintptr_t>(Impl + PHOrbTableOffset, 1);
        Check([engine read:&state frame:Frame() reason:&reason] && state.multiplayerDisabled && !state.board.tokens[0] &&
            !state.leadersValid, "mode plus selector reset remains latched before any solo board or leader read");
        Put<uintptr_t>(Impl + PHOrbTableOffset, firstNativeToken);
        Put<uint16_t>(Header + PHSaveRVA, 0);
        Check([engine read:&state frame:Frame() reason:&reason], "multiplayer exit is observable");
        Put<uint16_t>(Header + PHSaveRVA, 0x0101);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state), "new solo battle clears only the scene latch");
    }
    {
        Reset(); SetNativeMultiplayer(3);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "coordinate-race fixture read");
        [engine armMultiplayerOnce:YES]; changeTurnDuringCoordinateRead = true;
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] &&
            writes == 0 && GlobalInput() == Input, "turn change during coordinate conversion is rejected before leasing input");
    }
    {
        Reset(); SetNativeMultiplayer(4,2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "failed-stage fixture read");
        [engine armMultiplayerOnce:YES]; failWrite = 1;
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && GlobalInput() == Input,
            "failed begin pointer write never establishes a multiplayer gesture");
        [engine armMultiplayerOnce:NO]; failWrite = 0;
        Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
        Check([engine read:&state frame:Frame() reason:&reason], "later native hand token remains readable");
        const unsigned before = writes;
        Check(![engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "a failed begin cannot later claim the same token for cleanup");
    }
    for (unsigned stop = 0; stop < 2; ++stop) {
        Reset(); SetNativeMultiplayer(4,2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "pending native start fixture read");
        [engine armMultiplayerOnce:YES];
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason], "pending native start staged");
        NativeInputFrame(); Check([engine finishInput:&reason], "pending native start restored");
        [engine armMultiplayerOnce:NO];
        if (stop == 0) Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
        const unsigned before = writes;
        Check([engine read:&state frame:Frame() reason:&reason], "pending start acknowledgement after stop can be read");
        Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "stop before acknowledgement never sends another multiplayer hold");
        BOOL released = [engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason];
        Check(released == (stop == 0), "only an acknowledged captured start may be released after stop");
        Check([engine finishInput:&reason], "pending-start cleanup leaves no private pointer");
    }
    {
        Reset(); SetNativeMultiplayer(8,3);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        StartNativeMultiplayer(engine, state);
        Put<uint8_t>(Input + 214, 1); const auto hand = realInput;
        const unsigned before = writes;
        Check(![engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "real hand input cannot be replaced by another multiplayer move");
        [engine armMultiplayerOnce:NO];
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
            "already-acknowledged token may be released after hand interruption");
        NativeInputFrame(); Check([engine finishInput:&reason] && realInput == hand,
            "multiplayer release preserves the pending real hand events byte for byte");
    }
    for (unsigned players : {3u,4u,8u}) {
        SkillFixture(); SetNativeMultiplayer(players, players - 1);
        id<PHOrbEngine> engine = [FixtureSkillEngine new]; PHOrbSnapshot state{}; NSString *reason = nil;
        const auto originalImpl = implBytes, originalGuest = guest;
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.valid, "native skill fixture stays outside automation in multiplayer");
        [engine armMultiplayerOnce:YES];
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
            !canUseCalls && !skillStartCalls && !readinessCalls && !choiceCalls && !writes &&
            implBytes == originalImpl && guest == originalGuest,
            "one-shot authorization never calls a multiplayer skill getter, selection write or skill start");
    }
    Reset();
}
