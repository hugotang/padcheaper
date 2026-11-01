static void DungeonFixture() {
    EnemyActionFixture();
    const uintptr_t table = Header + PHOrbDungeonDumpWavesAddress;
    Put<uint16_t>(Header + PHOrbDungeonDumpCountAddress,2);
    for (unsigned wave = 0; wave < 2; ++wave) {
        Put<uint16_t>(table + wave * 264 + 2,1);
        Put<uint32_t>(table + wave * 264 + 12,400);
        Put<uint16_t>(table + wave * 264 + 16,uint16_t(wave + 10));
    }
    Put<uintptr_t>(Impl + PHOrbEnemyOffset + PHOrbDungeonDumpSpawnPointerOffset,table + 8);
    Put<uint16_t>(Impl + PHOrbEnemyOffset + PHOrbDungeonDumpSpawnWaveOffset,0);
}
static NSDictionary *CaptureDungeon(FixtureSkillEngine *engine, unsigned wave, PHOrbSnapshot *out = nullptr) {
    PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil],"dungeon fixture can read the first player window");
    if (out) *out = state;
    return [engine observeDungeon:&state waveIndex:wave];
}
static void DungeonNativeTests() {
    DungeonFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    const auto savedGuest = guest, savedImpl = implBytes, savedCatalog = catalogBytes;
    PHOrbSnapshot state{}; NSDictionary *first = CaptureDungeon(engine,0,&state), *next = CaptureDungeon(engine,1);
    Check([first[@"status"] isEqual:@"ready"] && [next[@"status"] isEqual:@"ready"] &&
        [first[@"floors"] count] == 2 && [next[@"floorDefinitions"][@"rawCaptureComplete"] boolValue] &&
        [next[@"floorDefinitions"][@"catalogs"][0][@"level"] intValue] == 11 && state.wave == 0,
        "the first floor can capture the next loaded floor without changing native wave or spawning it");
    NSDictionary *compound = EnemyActionDefinition(next[@"floorDefinitions"],2);
    Check([compound[@"childrenInOrder"] isEqual:@[@3,@4,@3]] &&
        [next[@"floorDefinitions"][@"catalogs"][0][@"catalogPrefixRawHex"] length] == 440 &&
        [EnemyActionDefinition(next[@"floorDefinitions"],5)[@"kind"] isEqual:@"unknown"] &&
        ![next[@"randomAlternativesKnown"] boolValue] && ![next[@"preemptiveSelectionKnown"] boolValue],
        "all-floor capture retains catalog coefficients, repeated compound children and unknown opcodes without forecasting");
    Check(guest == savedGuest && implBytes == savedImpl && catalogBytes == savedCatalog &&
        !writes && !canUseCalls && !readinessCalls && !skillStartCalls,"dungeon export collection performs no native writes or helper calls");
    // A future enemy uses a linked catalog variant, independent of the current enemy parameter.
    const uintptr_t parameter = EnemyActionParameterAddress();
    const uintptr_t variant = reinterpret_cast<uintptr_t>(catalogBytes.data()) + 14610 * PHBattleCatalogStride;
    std::memcpy(reinterpret_cast<void *>(variant),reinterpret_cast<void *>(parameter),PHBattleCatalogStride);
    Put<uint32_t>(variant,100400); Put<uint32_t>(parameter + PHOrbLeaderCatalogNextOffset,14610);
    Put<uint32_t>(Header + PHOrbDungeonDumpWavesAddress + 264 + 12,100400);
    next = CaptureDungeon(engine,1);
    Check([next[@"floorDefinitions"][@"catalogs"][0][@"catalogIndex"] intValue] == 14610 &&
        [next[@"floorDefinitions"][@"rawCaptureComplete"] boolValue],"future variant catalog follows its verified chain instead of borrowing the current enemy");
    for (unsigned scenario = 0; scenario < 10; ++scenario) {
        DungeonFixture(); engine = [FixtureSkillEngine new]; CaptureDungeon(engine,0,&state);
        if (scenario == 0) Put<uint16_t>(Header + PHOrbDungeonDumpCountAddress,101);
        if (scenario == 1) Put<uintptr_t>(Impl + PHOrbEnemyOffset + PHOrbDungeonDumpSpawnPointerOffset,Header + PHOrbDungeonDumpWavesAddress + 272);
        if (scenario == 2) Put<uint16_t>(Impl + PHOrbEnemyOffset + PHOrbDungeonDumpSpawnWaveOffset,1);
        if (scenario == 3) rejectDungeonRead = true;
        if (scenario == 4) changeDungeonDuringRead = true;
        if (scenario == 5) state.multiplayer.kind = PHOrbMultiplayerThreeSolo;
        if (scenario == 6) Put<uint16_t>(Impl + PHOrbEnemyOffset + 532,0x100);
        if (scenario == 7) Put<uint8_t>(Header + PHOrbDungeonDumpTextGuards[0].rva,uint8_t(PHOrbDungeonDumpTextGuards[0].bytes[0] ^ 1u));
        if (scenario == 8) state.dragged = state.board.tokens[0];
        if (scenario == 9) state.identity.entryHash[0] ^= 1u;
        NSDictionary *bad = [engine observeDungeon:&state waveIndex:1];
        Check(![bad[@"status"] isEqual:@"ready"] && !bad[@"floors"] && !bad[@"floorDefinitions"] && !writes,
            "bad bounds, stale source, unstable copy, multiplayer, parts, guard or held input cannot publish all-floor evidence");
        if (scenario == 7) Check([engine readAutomation:&state frame:Frame() reason:nil],"dungeon guards stay independent of ordinary board input");
    }
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        DungeonFixture(); engine = [FixtureSkillEngine new];
        if (scenario == 0) Put<uint16_t>(Header + PHOrbDungeonDumpWavesAddress + 264 + 2,9);
        if (scenario == 1) Put<uint32_t>(Header + PHOrbDungeonDumpWavesAddress + 264 + 12,99999);
        if (scenario == 2) EnemyActionSetRow(2,EnemyActionFixtureCount);
        next = CaptureDungeon(engine,1);
        Check([next[@"status"] isEqual:@"ready"] && ![next[@"floorDefinitions"][@"rawCaptureComplete"] boolValue] &&
            [next[@"floors"] count] == 2,"bad future count, unknown catalog or missing action remains explicitly partial");
    }
    DungeonFixture(); engine = [FixtureSkillEngine new];
    Check(![CaptureDungeon(engine,2)[@"status"] isEqual:@"ready"],"out-of-range requested floors cannot read beyond the loaded table");
    Reset();
}
