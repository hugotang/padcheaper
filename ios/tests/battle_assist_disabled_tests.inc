// Match the game's source/effective split: source assists remain installed,
// while dungeon+33 bit 3 strips the effective descriptor during rebuilding.
static void ConfigureDisabledAssists(Fixture &f) {
    Fixture::put<uint8_t>((uintptr_t)f.dungeon.data() + 33, 8);
    for (unsigned i = 0; i < 6; ++i) {
        Fixture::put<uint16_t>(f.member(i) + PHBattleAssistIDOffset, 400 + i);
        Fixture::put<uint32_t>(f.member(i) + 0x59e, 0x01000000u);
        Fixture::put<uint16_t>(f.member(i) + 0x5a2, 1000 + i);
        std::memcpy(f.originals[i], (void *)f.member(i), sizeof(f.originals[i]));
    }
}

static void TestAssistDisabledProfiles() {
    for (bool entry : {false, true}) {
        Fixture f; ConfigureDisabledAssists(f);
        if (entry) f.enter();
        id<PHBattleEngine> engine = PHCreateBattleEngine();
        const PHBattleState original = f.state(engine);
        PHBattleLoadout team[6]; PHBattleMember output[6] = {};
        for (unsigned i = 0; i < 6; ++i) {
            team[i] = Loadout(i);
            f.awakening(200 + i, 0, 21); // Six body SB.
            f.awakening(300 + i, 0, 56); // Disabled assists must add nothing.
        }
        if (entry) {
            uint16_t boost = 0;
            Check([engine applyEntryLoadouts:team mask:63 state:&original members:output initialBoost:&boost reason:nullptr] ==
                  PHBattleMutationApplied, "assist-disabled entry replaces all six bodies without enabling assists");
            Check(boost == 8 && f.advances == 8 && f.hpInitializations == 1,
                  "entry SB includes only six body SB and the badge, then initializes HP once");
        } else for (unsigned i = 0; i < 6; ++i) {
            PHBattleState before = f.state(engine);
            Check([engine applyLoadout:&team[i] state:&before slot:i member:&output[i] reason:nullptr] == PHBattleMutationApplied,
                  "manual profile replacement honors the same dungeon assist restriction");
        }
        PHBattleState applied = f.state(engine);
        const uint8_t disabled[8] = {0, 0, 0, 1, 0, 0, 0, 0};
        for (unsigned i = 0; i < 6; ++i) {
            Check(output[i].baseID == 200 + i && output[i].assistID == 300 + i && output[i].providerToken &&
                  !std::memcmp(output[i].assist, disabled, sizeof(disabled)),
                  "source assist and provider ownership survive while effective assist is canonically disabled");
            Check(Fixture::get<uint16_t>(f.parameter(i) + 14) == 300 + i &&
                  Fixture::get<uint16_t>(f.parameter(i) + 18) == 2,
                  "dungeon rules never erase the saved profile assist or its skill level");
        }
        if (entry) {
            uint16_t boost = 77;
            Check([engine applyEntryLoadouts:team mask:63 state:&applied members:output initialBoost:&boost reason:nullptr] ==
                  PHBattleMutationRejected && boost == 77 && f.advances == 8,
                  "assist-disabled entry retains the once-per-entry guard");
            Fixture::Set32(f.impl() + PHBattlePhaseOffset, 2);
        }
        for (unsigned i = 0; i < 6; ++i) {
            PHBattleState state = f.state(engine);
            Check([engine restoreLoadout:&original.members[i] state:&state slot:i reason:nullptr] == PHBattleMutationApplied,
                  "original sources restore with their assists still disabled");
        }
        PHBattleState restored = f.state(engine);
        Check(f.allocated == f.released && !restored.scriptProgram, "restoration releases all profile leases");
        for (unsigned i = 0; i < 6; ++i)
            Check(PHBattleSameSource(&original.members[i], &restored.members[i]) &&
                  !std::memcmp(restored.members[i].assist, disabled, sizeof(disabled)),
                  "restoration preserves each original source ID and the dungeon's effective assist ban");
    }
    {
        Fixture f; ConfigureDisabledAssists(f); f.enter(); f.failRebuild = 4;
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
        PHBattleLoadout team[6]; for (unsigned i = 0; i < 6; ++i) team[i] = Loadout(i);
        PHBattleMember output[6] = {}; uint16_t boost = 77;
        Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] ==
              PHBattleMutationRolledBack && boost == 77, "partial assist-disabled entry still rolls back atomically");
        PHBattleState after = f.state(engine);
        Check(SameEntryMembers(before, after, YES) && after.hp == before.hp && after.hpMatches &&
              after.digestMatches && after.entryChargesValid && f.allocated == f.released,
              "failed entry restores sources, effective descriptors, charge mirrors and HP");
    }
    for (int byte : {0, 1, 2, 3, 6}) {
        Fixture f; ConfigureDisabledAssists(f); f.corruptAppliedAssistByte = byte;
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
        PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
        Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRolledBack,
              "disabled assists do not exempt meaningful effective descriptor bytes from verification");
        PHBattleState after = f.state(engine);
        Check(PHBattleSameSource(&before.members[0], &after.members[0]) && f.allocated == f.released,
              "invalid effective descriptor restores the original source and releases ownership");
    }
    {
        Fixture f; f.forceAssistDisabled = true;
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
        PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
        Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRolledBack,
              "missing active assist is still rejected when no dungeon assist ban was observed");
    }
    for (unsigned unavailable = 0; unavailable < 2; ++unavailable) {
        Fixture f; ConfigureDisabledAssists(f);
        id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
        f.failReadAddress = unavailable ? (uintptr_t)f.dungeon.data() + 33 : f.impl() + PHBattleDungeonPointerOffset;
        PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
        unsigned writes = f.writeCalls, rebuilds = f.rebuilds;
        Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRejected &&
              f.writeCalls == writes && f.rebuilds == rebuilds && !f.allocated,
              "unreadable dungeon assist rules reject before allocating or rebuilding");
    }
}
