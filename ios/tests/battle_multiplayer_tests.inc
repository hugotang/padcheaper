// Production entry points must reject multiplayer before even native getters.
static void CheckMultiplayerDenied(Fixture &f, id<PHBattleEngine> engine, const PHBattleState &expected) {
    unsigned calls = f.nativeCalls, writes = f.writeCalls;
    PHBattleLoadout team[6];
    for (unsigned slot = 0; slot < 6; ++slot) team[slot] = Loadout(slot);
    PHBattleMember output[6] = {}; output[0].catalogID = 0xbeef;
    uint16_t boost = 77;
    Check([engine applyLoadout:team state:&expected slot:0 member:output reason:nullptr] == PHBattleMutationRejected,
        "multiplayer rejects manual full-profile replacement");
    Check([engine restoreLoadout:&expected.members[0] state:&expected slot:0 reason:nullptr] == PHBattleMutationRejected,
        "multiplayer rejects restoration and native profile release");
    Check([engine changeState:&expected slot:0 form:200 catalog:200 presentation:nullptr reason:nullptr] == PHBattleMutationRejected,
        "multiplayer rejects form-only replacement");
    Check([engine changeState:&expected slot:0 form:0 catalog:100 presentation:&expected.members[0] reason:nullptr] == PHBattleMutationRejected,
        "multiplayer rejects form-only restoration");
    Check([engine applyEntryLoadouts:team mask:63 state:&expected members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected,
        "multiplayer rejects automatic entry, SB and HP initialization");
    Check([engine rebindEntryMembers:63 state:&expected members:output reason:nullptr] == PHBattleMutationRejected,
        "the removed multiplayer rebind API cannot write");
    Check(boost == 77 && output[0].catalogID == 0xbeef && f.nativeCalls == calls && f.writeCalls == writes,
        "rejected multiplayer operations leave outputs unchanged and make zero native calls or write attempts");
}

static void TestMultiplayerDisabled() {
    const struct { uint16_t mode; uint32_t multi; } selectors[] = {
        {7,0}, {7,1}, {7,2}, {7,4}, {0,1}, {1,2}, {12,4}, {0,3}, {12,UINT32_MAX}
    };
    for (const auto &selector : selectors) for (unsigned phase : {0u, 2u, 46u}) {
        Fixture f; f.enter();
        Fixture::put<uint16_t>(f.header() + PHBattleModeRVA, selector.mode);
        Fixture::put<uint32_t>(f.header() + PHBattleMultiplayerRVA, selector.multi);
        Fixture::encode(f.impl() + PHBattlePhaseOffset, phase, 0x766cfc6du);
        PHBattleFrame frame = f.frame(); frame.policy.configured = true;
        frame.policy.modes.allowAll = frame.policy.sources.allowAll = frame.policy.multiplayer.allowAll = true;
        id<PHBattleEngine> engine = PHCreateBattleEngine();
        unsigned calls = f.nativeCalls, writes = f.writeCalls;
        PHBattleState state = {};
        Check([engine readState:&state frame:frame reason:nullptr] && state.present && state.multiplayerDisabled &&
            state.phase == phase && state.mode == selector.mode && state.multiplayer == selector.multi &&
            !state.digestKnown && !state.teamBanksValid && !state.entryWindow && !state.members[0].active,
            "multiplayer diagnostics decode metadata without native digest, bank helpers or member probes");
        Check(PHBattleStateGate(&state) == PHBattleGateMultiplayer && PHBattleEntryGate(&state) == PHBattleGateMultiplayer &&
            f.nativeCalls == calls && f.writeCalls == writes,
            "legacy allow-all filters and transition phases cannot authorize native calls");
        CheckMultiplayerDenied(f, engine, state);
        Check(!f.allocated && !f.released && !f.rebuilds && !f.refreshes && !f.advances && !f.memberAdvances &&
            !f.switches && !f.hpInitializations, "all multiplayer mutation counters remain zero");
    }

    for (unsigned count : {2u, 3u}) for (unsigned player = 0; player < count; ++player) {
        Fixture f; f.configureBanks(1, count, 6, player); f.enter();
        id<PHBattleEngine> engine = PHCreateBattleEngine();
        for (unsigned turn : {0u, player, player + count}) {
            Fixture::encode(f.impl() + PHBattlePhaseOffset, turn ? 2 : 46, 0x766cfc6du);
            Fixture::put<uint32_t>(f.impl() + PHBattleTurnSerialOffset, turn);
            f.loadView(player);
            unsigned calls = f.nativeCalls, writes = f.writeCalls;
            PHBattleState state = f.state(engine);
            Check(f.nativeCalls == calls && f.writeCalls == writes && state.multiplayerDisabled,
                "host, guest, own turn and remote turn all use metadata-only diagnostics");
            CheckMultiplayerDenied(f, engine, state);
        }
    }

    for (unsigned corrupt = 0; corrupt < 3; ++corrupt) {
        Fixture f; Fixture::put<uint32_t>(f.header() + PHBattleMultiplayerRVA, 1);
        if (corrupt == 0) Fixture::put<uint32_t>(f.impl() + PHBattlePhaseOffset, 0);
        if (corrupt == 1) Fixture::put<uintptr_t>(f.header() + PHSaveRVA + PHBattleCatalogPointerOffset, 0);
        if (corrupt == 2) Fixture::put<uint32_t>(f.impl() + PHBattleTeamCountOffset, UINT32_MAX);
        unsigned calls = f.nativeCalls, writes = f.writeCalls;
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState state = f.state(engine);
        Check(state.multiplayerDisabled && PHBattleStateGate(&state) == PHBattleGateMultiplayer &&
            f.nativeCalls == calls && f.writeCalls == writes,
            "invalid protected values, catalogs and bank layouts cannot bypass multiplayer disablement");
    }

    for (unsigned leased = 0; leased < 2; ++leased) {
        Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine();
        PHBattleState solo = f.state(engine); PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
        if (leased) {
            Check([engine applyLoadout:&loadout state:&solo slot:0 member:&output reason:nullptr] == PHBattleMutationApplied,
                "transition fixture begins with an owned single-player profile");
            solo = f.state(engine);
        }
        Fixture::put<uint32_t>(f.header() + PHBattleMultiplayerRVA, 1);
        auto imageBefore = f.image, implementationBefore = f.implementation;
        CheckMultiplayerDenied(f, engine, solo);
        Check(f.image == imageBefore && f.implementation == implementationBefore && !f.released,
            "a stale solo snapshot cannot restore, free or overwrite anything after a multiplayer transition");

        // Rollback helpers have their own guard; an internal caller cannot use
        // the special entry rollback exemption to restore in multiplayer.
        unsigned calls = f.nativeCalls, writes = f.writeCalls;
        BattleProvider provider = {}; uint64_t token = 0; uint32_t shadows[6] = {};
        Check(MutateProfile(&provider, &token, &solo, 0, nullptr, &solo.members[0], nullptr, nullptr, YES) ==
            PHBattleMutationRejected && !RestoreEntryTeam(&provider, &token, 0, solo, shadows),
            "immediate entry rollback also refuses multiplayer before native writes");
        BOOL mutated = NO; uint16_t charge = 77;
        Check(!Perform(&solo, 0, 0, 100, &solo.members[0], 4, NO, &mutated, &charge, nullptr) && !mutated &&
            f.nativeCalls == calls && f.writeCalls == writes, "the low-level mutation guard rechecks live selectors");

        Fixture::put<uint32_t>(f.header() + PHBattleMultiplayerRVA, 0);
        calls = f.nativeCalls;
        PHBattleState disconnected = f.state(engine);
        Check(disconnected.multiplayerDisabled && !disconnected.digestKnown && f.nativeCalls == calls,
            "resetting multiplayer selectors in the same battle cannot reenable native calls");
        CheckMultiplayerDenied(f, engine, disconnected);
        CheckMultiplayerDenied(f, engine, solo);
        if (!leased) {
            Fixture::put<uint8_t>(f.header() + PHSaveRVA, 0);
            Fixture::put<uint8_t>(f.header() + PHSaveRVA + 1, 0);
            Check(!f.state(engine).present, "leaving battle clears the multiplayer latch");
            Fixture::put<uint8_t>(f.header() + PHSaveRVA, 1);
            Fixture::put<uint8_t>(f.header() + PHSaveRVA + 1, 1);
            Fixture::put<uint8_t>(f.header() + PHBattleEntryHashRVA, 2);
            f.enter(); solo = f.state(engine); PHBattleMember team[6] = {}; uint16_t boost = 0;
            Check(!solo.multiplayerDisabled && [engine applyEntryLoadouts:&loadout mask:1 state:&solo members:team
                initialBoost:&boost reason:nullptr] == PHBattleMutationApplied && f.hpInitializations == 1 && f.advances == boost,
                "a subsequent fresh single-player entry retains automatic replacement, SB and HP initialization");
        }
    }

    for (unsigned entry = 0; entry < 2; ++entry) {
        Fixture f; if (entry) f.enter(); f.becomeMultiplayerAtRebuild = entry ? 3 : 1;
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
        PHBattleLoadout loadouts[6]; for (unsigned slot = 0; slot < 6; ++slot) loadouts[slot] = Loadout(slot);
        PHBattleMember output[6] = {}; uint16_t boost = 77;
        PHBattleMutationResult result = entry ?
            [engine applyEntryLoadouts:loadouts mask:63 state:&before members:output initialBoost:&boost reason:nullptr] :
            [engine applyLoadout:loadouts state:&before slot:0 member:output reason:nullptr];
        Check(result == PHBattleMutationUncertain && f.nativeCalls == f.callsAtMultiplayer &&
            f.writeCalls == f.writesAtMultiplayer && !f.released && !f.advances && !f.hpInitializations,
            "a native callback changing selectors stops the transaction without attempting multiplayer rollback or cleanup");
    }
}
