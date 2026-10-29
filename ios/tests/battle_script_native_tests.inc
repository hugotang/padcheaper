// Bytecode follows the independently recovered compiler/interpreter grammar.
// The allocation header models the game's category 4 / tag 44 allocator.
static uintptr_t InstallNativeScript(Fixture &f, bool party = false) {
    uintptr_t allocation = f.header() + 0x1700000, program = allocation + 56;
    Fixture::put<uint32_t>(allocation, 0x494d0001u);
    Fixture::put<uint32_t>(allocation + 4, 96);
    Fixture::put<uint32_t>(allocation + 8, 0x0400002cu);
    const uint8_t safe[] = {0, 13,'p','l','a','y',' ','1',10, 19, 5,8,0,1,1,'O','K',0, 0};
    const uint8_t formation[] = {0, 26,'p','a','r','a','m',' ','1',' ','1',' ','2','0','0',10,0};
    std::memcpy((void *)program, party ? formation : safe, party ? sizeof(formation) : sizeof(safe));
    Fixture::put<uint32_t>(f.header() + PHBattleScriptRVA, 64);
    Fixture::put<uintptr_t>(f.header() + PHBattleScriptRVA + 8, program);
    Fixture::put<uintptr_t>(f.header() + PHBattleScriptRVA + 16, allocation + 256);
    Fixture::put<uint32_t>(f.header() + PHBattleScriptRVA + PHBattleScriptEventsOffset + 156 * 8, 1);
    return program;
}

static void TestScriptedProvider() {
    for (bool entry : {false, true}) {
        Fixture f; uintptr_t program = InstallNativeScript(f);
        Fixture::put<uint16_t>(f.header() + PHBattleModeRVA, 1);
        if (entry) f.enter();
        id<PHBattleEngine> engine = PHCreateBattleEngine();
        PHBattleState original = f.state(engine), state = original;
        PHBattleLoadout team[6]; for (unsigned i = 0; i < 6; ++i) team[i] = Loadout(i);
        PHBattleMember applied[6] = {}; uint16_t boost = 0; NSString *reason = nil;
        Check(original.scriptReadable && original.scriptEventCount == 1 && original.parameterEmptyMask == 1023,
              "script diagnostics distinguish harmless registered events and ten empty records");
        if (entry) {
            Check([engine applyEntryLoadouts:team mask:63 state:&state members:applied initialBoost:&boost reason:&reason] ==
                PHBattleMutationApplied, "new single-player entry applies six profiles with a harmless native event script");
        } else for (unsigned i = 0; i < 6; ++i) {
            state = f.state(engine);
            Check([engine applyLoadout:&team[i] state:&state slot:i member:&applied[i] reason:&reason] == PHBattleMutationApplied,
                  "manual application shares empty records with harmless native events");
        }
        Check(f.allocated == 0 && f.released == 0, "a real script is neither replaced nor allocated by the provider");
        Fixture::Set32(f.impl() + PHBattlePhaseOffset, 2);
        for (unsigned i = 0; i < 6; ++i) {
            if (i == 1) {
                // The native script may run between restored members. Its
                // runner and mutable event counters do not destroy ownership.
                Fixture::put<uintptr_t>(f.header() + PHBattleScriptRVA + PHBattleScriptRunnerOffset, program + 1);
                Fixture::put<uint16_t>(f.header() + PHBattleScriptRVA + PHBattleScriptEventsOffset + 156 * 8 + 6, 1);
                state = f.state(engine);
                for (unsigned j = 1; j < 6; ++j) Check(state.members[j].providerToken == applied[j].providerToken,
                    "read-only ownership survives a temporary script runner after the first restore");
                unsigned writes = f.writeCalls, rebuilds = f.rebuilds;
                Check([engine restoreLoadout:&original.members[i] state:&state slot:i reason:&reason] == PHBattleMutationRejected &&
                    f.writeCalls == writes && f.rebuilds == rebuilds, "running scripts still block all restoration writes");
                Fixture::put<uintptr_t>(f.header() + PHBattleScriptRVA + PHBattleScriptRunnerOffset, 0);
            }
            state = f.state(engine);
            Check([engine restoreLoadout:&original.members[i] state:&state slot:i reason:&reason] == PHBattleMutationApplied,
                  "all six records restore after the harmless script becomes idle");
        }
        Check(Fixture::get<uintptr_t>(f.header() + PHBattleScriptRVA + 8) == program && !f.released,
              "last restore preserves the game's original script allocation and event entries");
    }
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        Fixture f; uintptr_t program = InstallNativeScript(f, scenario == 0);
        if (scenario == 1) Fixture::put<uintptr_t>(f.header() + PHBattleScriptRVA + PHBattleScriptRunnerOffset, program + 1);
        if (scenario == 2) Fixture::put<uint32_t>(program - 56, 0);
        if (scenario == 3) Fixture::put<uint32_t>(program - 52, 95);
        if (scenario == 4) Fixture::put<uint32_t>(f.header() + PHBattleScriptRVA + PHBattleScriptEventsOffset, 96);
        if (scenario == 5) for (unsigned i = 0; i < 10; ++i) Fixture::put<uint8_t>(f.parameter(i), 1);
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState state = f.state(engine);
        PHBattleLoadout loadout = Loadout(); PHBattleMember output = {}; NSString *reason = nil;
        unsigned writes = f.writeCalls, rebuilds = f.rebuilds;
        Check([engine applyLoadout:&loadout state:&state slot:0 member:&output reason:&reason] == PHBattleMutationRejected &&
            f.writeCalls == writes && f.rebuilds == rebuilds && !f.allocated && reason.length,
            "future party commands, running scripts, invalid allocations/events and full parameter records reject before writes");
        if (scenario == 0) Check([reason containsString:@"party"], "a real formation command has a precise diagnostic");
        if (scenario == 5) Check([reason containsString:@"十個"], "occupied slots are distinguished from registered events");
    }
    for (unsigned failure = 0; failure < 3; ++failure) {
        Fixture f; uintptr_t program = InstallNativeScript(f);
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState state = f.state(engine);
        PHBattleMember original = state.members[0], applied = {};
        PHBattleLoadout loadout = Loadout(); NSString *reason = nil;
        Check([engine applyLoadout:&loadout state:&state slot:0 member:&applied reason:&reason] == PHBattleMutationApplied,
              "ownership recovery starts from a confirmed profile");
        if (failure == 0) f.failReadAddress = f.header() + PHBattleScriptRVA;
        if (failure == 1) f.failReadAddress = f.parameter(0);
        if (failure == 2) Fixture::put<uint8_t>(program + 1, 26);
        state = f.state(engine);
        Check(!state.members[0].providerToken && state.members[0].providerUncertain,
              "transient context/record reads and changed bytecode mark unknown ownership instead of takeover");
        unsigned writes = f.writeCalls, rebuilds = f.rebuilds;
        Check([engine restoreLoadout:&original state:&state slot:0 reason:&reason] == PHBattleMutationRejected &&
            f.writeCalls == writes && f.rebuilds == rebuilds, "uncertain ownership permits no restoration writes");
        f.failReadAddress = 0;
        if (failure == 2) Fixture::put<uint8_t>(program + 1, 13);
        state = f.state(engine);
        Check(state.members[0].providerToken == applied.providerToken && !state.members[0].providerUncertain,
              "matching native context restores the same ownership token");
        Check([engine restoreLoadout:&original state:&state slot:0 reason:&reason] == PHBattleMutationApplied,
              "recovered ownership remains fully restorable");
    }
    {
        Fixture f; InstallNativeScript(f);
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState state = f.state(engine);
        PHBattleLoadout loadout = Loadout(); PHBattleMember applied = {};
        Check([engine applyLoadout:&loadout state:&state slot:0 member:&applied reason:nullptr] == PHBattleMutationApplied,
              "takeover case starts with a confirmed profile");
        Fixture::put<uint16_t>(f.parameter(0) + 6, 99);
        state = f.state(engine);
        Check(!state.members[0].providerToken && !state.members[0].providerUncertain,
              "a readable changed record proves takeover and cannot regain the old token");
    }
}
