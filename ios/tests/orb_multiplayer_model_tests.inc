#include "orb_leader_fixture.h"

static PHOrbSnapshot MultiplayerModelState(unsigned players, unsigned seat) {
    PHOrbSnapshot s{};
    s.identity = {1,2,3,42,1,1,2,{1},int16_t(players == 3 ? 7 : players == 4 ? 11 : 10),0,1};
    s.inputManager = 4; s.present = s.valid = s.settled = s.ordinary = true;
    s.phase = 2; s.input.valid = true; s.minErase = 3; s.freeMovement = 1;
    s.remainingFrames = 300; s.fps = 60; s.board = Board(6,5,71);
    auto &m = s.multiplayer;
    m.kind = PHOrbMultiplayerMode(s.identity.entryMode, s.identity.multiplayer);
    m.known = m.clockKnown = true; m.teamCount = players; m.localSeat = m.viewTeam = seat;
    m.turnSerial = players == 3 ? seat : 0; m.localStatus = 3; m.networkFlags = 1; m.connectionState = 2;
    m.countdownReady = 1; m.clockStart = players == 3 ? 0 : 100; m.clockFlags = players == 3 ? 0 : 4;
    m.remainingSeconds = 60;
    return s;
}
static void MultiplayerModelTests() {
    for (unsigned players : {3u,4u,8u}) for (unsigned seat = 0; seat < players; ++seat) {
        auto s = MultiplayerModelState(players, seat);
        Check(PHOrbMultiplayerWindow(&s) && PHOrbCanPreview(&s), "all local seats have a recognized multiplayer input window");
        SetLeaderContextFixture(&s,14242,66063,13834,63039);
        Check(PHOrbLeaderContextReady(&s), "leader context follows every validated local seat");
        for (unsigned field = 0; field < 10; ++field) {
            auto changed = s;
            if (field == 0) changed.leaderContext.sourceSlots[0] = 2;
            if (field == 1) ++changed.leaderContext.sourceCatalogIDs[0];
            if (field == 2) ++changed.leaderContext.skillIDs[1];
            if (field == 3) ++changed.leaderContext.catalogIDs[1];
            if (field == 4) ++changed.leaderContext.parameters[0];
            if (field == 5) ++changed.leaderContext.formIDs[0];
            if (field == 6) ++changed.leaderContext.temporaryIDs[1];
            if (field == 7) ++changed.leaderContext.temporaryTurns[1];
            if (field == 8) changed.leaderContext.known = false;
            if (field == 9) ++changed.leaderContext.teamBank;
            Check(!PHOrbSameLeaders(&s,&changed), "same displayed IDs cannot hide a changed multiplayer leader source");
        }
        if (players == 4) {
            auto extra = s; SetExtraLeaderFixture(&extra);
            Check(PHOrbLeaderContextReady(&extra) && PHOrbSameLeaders(&extra,&extra),
                "validated inactive 4P extra helper is a known absence of helper leader skill");
            auto changed = extra; ++changed.leaderContext.extraRecord;
            Check(!PHOrbSameLeaders(&extra,&changed), "extra-skill selection changes invalidate the pair");
            changed = extra; changed.leaderContext.extraHelper = false;
            Check(!PHOrbLeaderContextReady(&changed), "missing helper ID alone never implies an extra helper");
        }
        auto next = s; next.multiplayer.remainingSeconds -= 0.1;
        Check(PHOrbSameTurn(&s, &next), "decreasing clock time does not change multiplayer operation identity");
        next.multiplayer.turnSerial += players;
        Check(!PHOrbSameTurn(&s, &next), "same local seat in a later turn cannot reuse the earlier path");
        next = s; next.multiplayer.viewTeam = (seat + 1) % players;
        Check(!PHOrbMultiplayerWindow(&next) && !PHOrbSameTurn(&s, &next), "another displayed team has no local input window");
        if (players == 3) {
            next = s; next.multiplayer.turnSerial = (seat + 1) % 3;
            Check(!PHOrbCanPreview(&next), "co-op requires modulo turn ownership as well as the local view");
            next = s; next.multiplayer.clockFlags = 3;
            Check(!PHOrbCanPreview(&next), "paused co-op clock before selection cannot begin a gesture");
            next.dragged = next.board.tokens[0];
            Check(PHOrbMultiplayerWindow(&next) && PHOrbSameTurn(&s, &next),
                "native first-swap pause retains the selected co-op gesture window");
        } else {
            next = s; ++next.multiplayer.clockStart;
            Check(!PHOrbSameTurn(&s, &next), "competitive clock restart invalidates a previously captured path");
            next = s; next.multiplayer.clockFlags |= 0x10;
            Check(!PHOrbCanPreview(&next), "competitive paused clock is not an operation window");
        }
    }
    auto reduced = MultiplayerModelState(3,1);
    reduced.multiplayer.teamCount = 2;
    Check(PHOrbCanPreview(&reduced), "mode 7 with two remaining seats retains three-player co-op ownership semantics");
    for (unsigned mode : {0u,7u,10u,11u,12u}) for (unsigned selector = 0; selector <= 5; ++selector) {
        const bool solo = selector == 0 && mode != 7 && mode != 10 && mode != 11;
        const bool recognized = selector == 1 && (mode == 7 || mode == 10 || mode == 11);
        const auto kind = PHOrbMultiplayerMode(int16_t(mode), selector);
        Check((kind == PHOrbMultiplayerNone) == solo && (kind == PHOrbMultiplayerUnsupported) == (!solo && !recognized),
            "selector values are not player counts and selector resets cannot become single player");
    }
    for (unsigned failure = 0; failure < 15; ++failure) {
        auto s = MultiplayerModelState(8,5);
        if (failure == 0) s.multiplayer.known = false;
        if (failure == 1) s.multiplayer.localStatus = 5;
        if (failure == 2) s.multiplayer.networkFlags = 0;
        if (failure == 3) s.multiplayer.networkFlags |= 0x10;
        if (failure == 4) s.multiplayer.syncState = 1;
        if (failure == 5) s.multiplayer.connectionState = 5;
        if (failure == 6) s.multiplayer.countdownReady = 0;
        if (failure == 7) s.multiplayer.finished = 1;
        if (failure == 8) s.multiplayer.resolvePending = 1;
        if (failure == 9) s.multiplayer.remainingSeconds = 0;
        if (failure == 10) s.multiplayer.clockKnown = false;
        if (failure == 11) s.boardTransition = true;
        if (failure == 12) s.multiplayer.teamCount = 4;
        if (failure == 13) s.multiplayerDisabled = true;
        if (failure == 14) s.identity.sourceMode = 1;
        Check(!PHOrbMultiplayerWindow(&s) && !PHOrbCanPreview(&s),
            "incomplete, remote, resolving or disconnected multiplayer state cannot preview an executable board");
    }
    for (unsigned players : {3u, 4u, 8u}) {
        auto next = MultiplayerModelState(players, players - 1);
        next.multiplayer.networkFlags = 3;
        Check(PHOrbCanPreview(&next) == (players == 4),
            "only 4P may retain the submitted bit on an otherwise eligible next local board");
        if (players == 4) {
            next.multiplayer.resolvePending = 1;
            Check(!PHOrbCanPreview(&next), "4P submitted-bit support never bypasses active resolution");
            next.multiplayer.resolvePending = 0;
            next.multiplayer.viewTeam = 0;
            Check(!PHOrbCanPreview(&next), "4P submitted-bit support never grants another player's board");
            next.multiplayer.viewTeam = next.multiplayer.localSeat;
            next.multiplayer.networkFlags = 0x23;
            Check(PHOrbCanPreview(&next), "the existing optional visual bit may coexist with 4P submission");
        }
        for (uint32_t flags : {2u, 7u, 0xbu, 0x13u, 0x83u, 0x203u, 0x10003u}) {
            next = MultiplayerModelState(players, players - 1);
            next.multiplayer.networkFlags = flags;
            Check(!PHOrbCanPreview(&next), "missing active flag and send/pass/sync/terminal bits still block all modes");
        }
    }
    auto thorn = MultiplayerModelState(3,0);
    thorn.dragged = thorn.board.tokens[0]; thorn.board.flags[1] |= PHOrbFlagThorn; thorn.board.thornDamage[1] = 1;
    thorn.thorns = {1000,1000,0,true};
    Check(!PHOrbCanEnterThorn(&thorn,1), "even a supplied solo HP budget cannot authorize multiplayer thorn damage");
}
