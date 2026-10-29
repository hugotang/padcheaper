static void TestRestoreAssistPadding() {
    for (bool hasAssist : {false, true}) {
        Fixture f; f.unstableAssistPadding = true;
        for (unsigned i = 0; i < 6; ++i) {
            if (hasAssist) {
                Fixture::put<uint16_t>(f.member(i) + PHBattleAssistIDOffset, 400 + i);
                Fixture::put<uint16_t>(f.member(i) + 0x59e, 400 + i);
                Fixture::put<uint16_t>(f.member(i) + 0x59e + 4, 3);
            } else Fixture::put<uint16_t>(f.member(i) + 0x59e + 4, 0x7530 + i);
            Fixture::put<uint8_t>(f.member(i) + 0x59e + 3, hasAssist ? 99 : 1);
            Fixture::put<uint8_t>(f.member(i) + 0x59e + 7, 0xa0 + i);
            std::memcpy(f.originals[i], (void *)f.member(i), sizeof(f.originals[i]));
        }
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState original = f.state(engine), state;
        PHBattleMember applied[6] = {};
        for (unsigned i = 0; i < 6; ++i) {
            auto loadout = Loadout(i); state = f.state(engine);
            Check([engine applyLoadout:&loadout state:&state slot:i member:&applied[i] reason:nullptr] == PHBattleMutationApplied,
                  "six-profile replacement accepts original native assist padding");
        }
        for (unsigned i = 0; i < 6; ++i) {
            state = f.state(engine);
            Check([engine restoreLoadout:&original.members[i] state:&state slot:i reason:nullptr] == PHBattleMutationApplied,
                  "all six restore when the first five regenerate unrelated assist padding");
        }
        state = f.state(engine);
        Check(f.allocated == f.released && !state.scriptProgram,
              "successful full restoration releases provider ownership before another team");
        for (unsigned i = 0; i < 6; ++i) {
            auto loadout = Loadout(i); loadout.bodyNumber += 100; state = f.state(engine);
            Check([engine applyLoadout:&loadout state:&state slot:i member:&applied[i] reason:nullptr] == PHBattleMutationApplied,
                  "a different six-member team applies after real native restoration");
        }
    }
    // Real fields must still fail and retain the original lease for retry.
    for (int field : {2, 3, 4, 5, 6}) {
        Fixture f;
        Fixture::put<uint16_t>(f.member(0) + PHBattleAssistIDOffset, 400);
        Fixture::put<uint16_t>(f.member(0) + 0x59e, 400);
        Fixture::put<uint16_t>(f.member(0) + 0x59e + 4, 2);
        std::memcpy(f.originals[0], (void *)f.member(0), sizeof(f.originals[0]));
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState original = f.state(engine), state;
        auto loadout = Loadout(); PHBattleMember applied = {}; NSString *reason = nil;
        Check([engine applyLoadout:&loadout state:&original slot:0 member:&applied reason:&reason] == PHBattleMutationApplied,
              "assist-field negative test starts with a confirmed replacement");
        f.corruptRestoredAssistByte = field; state = f.state(engine);
        Check([engine restoreLoadout:&original.members[0] state:&state slot:0 reason:&reason] == PHBattleMutationRolledBack &&
              [reason containsString:@"武裝 不同"] && !f.released,
              "a changed real assist field is diagnosed, rolled back and never silently accepted");
        state = f.state(engine);
        Check(state.members[0].providerToken == applied.providerToken &&
              [engine restoreLoadout:&original.members[0] state:&state slot:0 reason:&reason] == PHBattleMutationApplied,
              "a confirmed rollback retains the lease and permits a successful native retry");
    }
    {
        Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState state = f.state(engine);
        auto loadout = Loadout(); PHBattleMember applied = {};
        Check([engine applyLoadout:&loadout state:&state slot:0 member:&applied reason:nullptr] == PHBattleMutationApplied,
              "native fault lifetime starts from a confirmed provider");
        PHBattleState original = state;
        state = f.state(engine); f.failWriteAddress = f.header() + PHBattleScriptRVA + 8;
        Check([engine restoreLoadout:&original.members[0] state:&state slot:0 reason:nullptr] == PHBattleMutationUncertain,
              "unconfirmed native provider detach faults restoration");
        unsigned writes = f.writeCalls;
        state = f.state(engine);
        Check([engine applyLoadout:&loadout state:&state slot:1 member:&applied reason:nullptr] == PHBattleMutationRejected &&
              f.writeCalls == writes, "same-battle reads never clear an uncertain native fault");
        Fixture::put<uintptr_t>(f.header() + PHSceneTableRVA + 24, 0); state = f.state(engine);
        Check(!state.present, "fault reset observes a real scene exit");
        Fixture::put<uintptr_t>(f.header() + PHSceneTableRVA + 24, f.game());
        Fixture::put<uint8_t>(f.header() + PHBattleEntryHashRVA, 2);
        Fixture::put<uintptr_t>(f.header() + PHBattleScriptRVA + 8, 0);
        for (unsigned i = 0; i < 6; ++i) {
            std::memcpy((void *)f.member(i), f.originals[i], sizeof(f.originals[i]));
            std::memset((void *)f.parameter(i), 0, PHBattleProfileSize);
        }
        f.shadows(); state = f.state(engine);
        Check([engine applyLoadout:&loadout state:&state slot:1 member:&applied reason:nullptr] == PHBattleMutationApplied,
              "new validated single-player battle no longer inherits the old native fault");
    }
}
