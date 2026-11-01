// Written regressions for the next explicit build/test request.
static constexpr uint32_t EnemyActionFixtureCount = 512;
static const uintptr_t EnemyActionTable = Header + 0x1d00000;
static uintptr_t EnemyActionParameterAddress() {
    return reinterpret_cast<uintptr_t>(catalogBytes.data()) + 400 * PHBattleCatalogStride;
}
static void EnemyActionSetRecord(unsigned id, uint16_t opcode, std::initializer_list<int32_t> parameters, int32_t attached = 0) {
    EA::Record record{};
    Store<uint32_t>(record.bytes.data(), id); Store<uint16_t>(record.bytes.data() + 4, opcode);
    unsigned i = 0;
    for (int32_t parameter : parameters) { Store<int32_t>(record.bytes.data() + 16 + 4 * i, parameter); ++i; }
    Store<int32_t>(record.bytes.data() + 48, 100); Store<int32_t>(record.bytes.data() + 52, 100);
    Store<int32_t>(record.bytes.data() + 56, 100); Store<int32_t>(record.bytes.data() + 68, attached);
    Put<EA::Record>(EnemyActionTable + id * EA::RecordBytes, record);
}
static void EnemyActionSetRow(unsigned row, int32_t id, uint8_t a = 100, uint8_t b = 0) {
    const uintptr_t address = EnemyActionParameterAddress() + PHOrbEnemyActionsRowsOffset + row * 8;
    Put<int32_t>(address, id); Put<uint8_t>(address + 4, a); Put<uint8_t>(address + 5, b);
}
static void EnemyActionFixture() {
    ObservationFixture();
    for (const auto &guard : PHOrbEnemyActionsTextGuards) std::memcpy(reinterpret_cast<void *>(Header + guard.rva), guard.bytes, 64);
    Put<uint32_t>(Header + PHSaveRVA + PHOrbLeaderCatalogRecordsOffset, uint32_t(catalogBytes.size() / PHBattleCatalogStride));
    Put<uint32_t>(Header + PHOrbEnemyActionsMetadataRVA, 4);
    Put<uint32_t>(Header + PHOrbEnemyActionsMetadataRVA + 4, 17);
    Put<uint32_t>(Header + PHOrbEnemyActionsCountRVA, EnemyActionFixtureCount);
    Put<uintptr_t>(Header + PHOrbEnemyActionsPointerRVA, EnemyActionTable);
    const uintptr_t enemy = Impl + PHOrbEnemyOffset, parameter = EnemyActionParameterAddress();
    Put<uint32_t>(parameter, 400); Put<uint8_t>(parameter + 12, 3); Put<uint16_t>(parameter + 224, 1);
    Put<uint16_t>(parameter + 226, 100); Put<uint16_t>(parameter + 228, 1);
    Put<uintptr_t>(enemy + PHOrbEnemyActionsParameterOffset, parameter);
    Put<uint32_t>(enemy + PHOrbEnemyActionsCatalogIDOffset, 400);
    Put<int8_t>(enemy + 575, -1); Put<uint16_t>(enemy + 312, 10);
    Put<uint8_t>(enemy + 1, 1); Put<int32_t>(enemy + 284, -1);
    Put<int32_t>(enemy + 1680, 8); Put<int32_t>(enemy + 2008, -1);
    Put<uint32_t>(enemy + 1720, 4); Put<uint32_t>(enemy + 1724, 2); Put<uint32_t>(enemy + 1728, 9);
    Put<uint16_t>(enemy + 2004, 20);
    Protected(enemy + 592, 1000, 0x766cfc6d); Protected(enemy + 608, 0, 0x766cfc6d);
    Protected(enemy + 624, 20000, 0x766cfc6d); Protected(enemy + 640, 0, 0x766cfc6d);
    Protected(enemy + 296, 1, 0x766cfc6d);
    Protected(enemy + 2032, 25, 0x6a875a8b); Protected(enemy + 2128, 0x3fc00000, 0x6a875a8b);
    Protected(enemy + 2144, 2, 0x6a875a8b); Protected(enemy + 2336, 0, 0x6a875a8b);
    Protected(enemy + 2352, 0, 0x6a875a8b);
    EnemyActionSetRecord(1, 0x1c, {}); EnemyActionSetRecord(2, 83, {3,4,3,0,5});
    EnemyActionSetRecord(3, 88, {4}, 150); EnemyActionSetRecord(4, 141, {5});
    EnemyActionSetRecord(5, 65535, {-9}); EnemyActionSetRecord(8, 88, {2});
    EnemyActionSetRow(0, 1, 20, 64); EnemyActionSetRow(3, 2); EnemyActionSetRow(63, 5);
}
static NSDictionary *EnemyActionDefinition(NSDictionary *trace, unsigned id) {
    for (NSDictionary *record in trace[@"definitions"]) if ([record[@"actionID"] unsignedIntValue] == id) return record;
    return nil;
}
static NSDictionary *CaptureEnemyActions(FixtureSkillEngine *engine, PHOrbSnapshot *out = nullptr) {
    PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil], "enemy action fixture can read the ordinary solo board");
    NSDictionary *trace = [engine observeEnemyActions:&state];
    if (out) *out = state;
    return trace;
}
static void EnemyActionNativeTests() {
    Check([EnemyActionDigest("abc", 3) isEqual:@"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"],
        "the exported digest uses SHA-256 bytes in canonical lowercase hex");
    EnemyActionFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    const auto initialGuest = guest, initialImpl = implBytes, initialCatalog = catalogBytes;
    PHOrbSnapshot state{}; NSDictionary *trace = CaptureEnemyActions(engine, &state);
    NSDictionary *enemy = trace[@"enemies"][0];
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"rawCaptureComplete"] boolValue] && ![trace[@"definitionCaptureComplete"] boolValue] &&
        ![trace[@"structureSupported"] boolValue] && [trace[@"definitions"] count] == 6 &&
        [enemy[@"rows"] count] == 3 && [enemy[@"rows"][2][@"row"] unsignedIntValue] == 64 &&
        [enemy[@"rows"][0][@"jumpTargetRow"] unsignedIntValue] == 64,
        "all 64 rows, gaps, cached-only definitions and unknown nodes survive a bounded double capture");
    Check([enemy[@"stateKnown"] boolValue] && [enemy[@"hp"] unsignedLongLongValue] == 100 &&
        [enemy[@"maximumHP"] unsignedLongLongValue] == 1000 && [enemy[@"baseAttack"] unsignedLongLongValue] == 20000 &&
        [enemy[@"enrageMultiplierCurrent"] doubleValue] == 1.5 && [enemy[@"countdownRaw"] intValue] == 1 &&
        [enemy[@"selectedActionCached"] intValue] == 8 && ![trace[@"enemyForecastKnown"] boolValue] &&
        ![trace[@"damageBoundKnown"] boolValue] && ![trace[@"spawnIdentityKnown"] boolValue],
        "current raw attack/countdown/AI state never masquerades as next-turn damage or spawn identity");
    NSDictionary *compound = EnemyActionDefinition(trace, 2), *bind = EnemyActionDefinition(trace, 3);
    Check([compound[@"childrenInOrder"] isEqual:@[@3,@4,@3]] && [compound[@"parameters"][4] intValue] == 5 &&
        [bind[@"attachedAttackPercentRaw"] intValue] == 150 && [bind[@"attachedAttackPrecedesEffect"] boolValue] &&
        [EnemyActionDefinition(trace, 5)[@"kind"] isEqual:@"unknown"] && [bind[@"rawHex"] length] == 144 &&
        [enemy[@"rowsRawHex"] length] == 1024 && [bind[@"sha256"] length] == 64,
        "compound duplicates, status-attached attacks, unsupported definitions and exact byte hashes remain reviewable");
    Check(guest == initialGuest && implBytes == initialImpl && catalogBytes == initialCatalog &&
        !writes && !canUseCalls && !readinessCalls && !choiceCalls && !skillStartCalls,
        "enemy diagnostics leave the guest, RNG and skill state unchanged without invoking native helpers");
    const uintptr_t address = Impl + PHOrbEnemyOffset, parameter = EnemyActionParameterAddress();
    Put<uint64_t>(parameter + PHOrbEnemyActionsRowsOffset + 8, UINT64_C(0x0100000000000000));
    NSDictionary *withEmptyOperand = CaptureEnemyActions(engine);
    Check(![enemy[@"definitionSHA256"] isEqual:withEmptyOperand[@"enemies"][0][@"definitionSHA256"]] &&
        [withEmptyOperand[@"enemies"][0][@"rows"] count] == 3,
        "raw operands in an empty row affect the definition hash without creating an action");
    EnemyActionSetRecord(5, 88, {9});
    trace = CaptureEnemyActions(engine);
    Check([trace[@"rawCaptureComplete"] boolValue] && [trace[@"definitionCaptureComplete"] boolValue] &&
        [trace[@"structureSupported"] boolValue] && ![trace[@"issues"] count] && ![trace[@"enemyForecastKnown"] boolValue],
        "a supported acyclic definition is complete without asserting next-turn reachability");
    // Real native variant chain, same public number but a different parameter row.
    EnemyActionFixture();
    const uintptr_t variant = reinterpret_cast<uintptr_t>(catalogBytes.data()) + 14610 * PHBattleCatalogStride;
    std::memcpy(reinterpret_cast<void *>(variant), reinterpret_cast<void *>(parameter), PHBattleCatalogStride);
    Put<uint32_t>(variant, 100400); Put<uint32_t>(parameter + PHOrbLeaderCatalogNextOffset, 14610);
    Put<uint32_t>(address + PHOrbEnemyActionsCatalogIDOffset, 100400); Put<uintptr_t>(address + PHOrbEnemyActionsParameterOffset, variant);
    engine = [FixtureSkillEngine new]; trace = CaptureEnemyActions(engine);
    Check([trace[@"enemies"][0][@"identityKnown"] boolValue] && [trace[@"enemies"][0][@"catalogID"] unsignedIntValue] == 100400 &&
        [trace[@"enemies"][0][@"catalogIndex"] unsignedIntValue] == 14610,
        "a linked variant uses its actual ID and parameter rather than the ordinary card's action table");
    Put<uintptr_t>(address + PHOrbEnemyActionsParameterOffset, parameter);
    trace = CaptureEnemyActions(engine);
    Check(![trace[@"enemies"][0][@"identityKnown"] boolValue] && !trace[@"enemies"][0][@"rows"] &&
        ![trace[@"definitionCaptureComplete"] boolValue], "a matching public number cannot substitute for the current native parameter");
    // Start each rejection case from known structure so an unrelated unknown
    // opcode cannot make the completeness assertion pass accidentally.
    for (unsigned scenario = 0; scenario < 13; ++scenario) {
        EnemyActionFixture(); engine = [FixtureSkillEngine new];
        EnemyActionSetRecord(5, 88, {9});
        if (scenario == 0) EnemyActionSetRow(2, -7);
        if (scenario == 1) EnemyActionSetRow(2, EnemyActionFixtureCount);
        if (scenario == 2) Put<uint32_t>(address + 2128, 0);
        if (scenario == 3) Protected(address + 2128, 0x7fc00000, 0x6a875a8b);
        if (scenario == 4) Put<uintptr_t>(address + PHOrbEnemyActionsParameterOffset, parameter + 4);
        if (scenario == 5) Put<uint32_t>(Header + PHSaveRVA + PHOrbLeaderCatalogRecordsOffset, 1);
        if (scenario == 6) Put<uint32_t>(Header + PHOrbEnemyActionsCountRVA, 0x20001);
        if (scenario == 7) Put<uintptr_t>(Header + PHOrbEnemyActionsPointerRVA, UINTPTR_MAX - 31);
        if (scenario == 8) Put<uint16_t>(address + 532, 0x100);
        if (scenario == 9) EnemyActionSetRow(0, 1, 20, 0);
        if (scenario == 10) Protected(address + 608, 0x80000000u, 0x766cfc6d);
        if (scenario == 11) Protected(address + 2144, 0x10000u, 0x6a875a8b);
        if (scenario == 12) Protected(address + 592, 99, 0x766cfc6d);
        trace = CaptureEnemyActions(engine);
        if (scenario == 9) Check([trace[@"status"] isEqual:@"ready"] && ![trace[@"structureSupported"] boolValue] &&
            ![trace[@"enemies"][0][@"rows"][0][@"jumpTargetValid"] boolValue], "invalid jumps are not clamped to a harmless row");
        else Check(![trace[@"rawCaptureComplete"] boolValue], "invalid IDs, protected values, parts and catalog bounds stay incomplete");
        if (scenario == 2 || scenario == 3 || scenario >= 10) Check(![trace[@"enemies"][0][@"stateKnown"] boolValue] &&
            !trace[@"enemies"][0][@"baseAttack"] && !trace[@"enemies"][0][@"enrageMultiplierCurrent"],
            "invalid protected stats cannot be exported as zero or a known attack value");
        Check(![trace[@"enemyForecastKnown"] boolValue] && !writes && !canUseCalls, "incomplete definitions never authorize or execute an action");
    }
    EnemyActionFixture(); engine = [FixtureSkillEngine new]; trace = CaptureEnemyActions(engine, &state);
    Put<uint8_t>(Header + PHOrbEnemyActionsLookupRVA, 0);
    trace = [engine observeEnemyActions:&state];
    [engine observeBattle:&state];
    Check([trace[@"status"] isEqual:@"unavailable"] && !trace[@"definitions"] &&
        state.observation.status == PHOrbObservationReady && [engine readAutomation:&state frame:Frame() reason:nil],
        "enemy-only fingerprint failure removes its data without disabling prior diagnostics or ordinary automation");
    for (unsigned mutation = 1; mutation <= 12; ++mutation) {
        EnemyActionFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
        enemyActionMutation = mutation;
        trace = [engine observeEnemyActions:&state];
        Check([trace[@"status"] isEqual:@"changed"] && !trace[@"enemies"] && !trace[@"definitions"] && !writes,
            "AI, definition, source, entry, key, phase, input and board races discard the entire capture");
    }
    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        EnemyActionFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
        state.multiplayer.kind = kind;
        trace = [engine observeEnemyActions:&state];
        Check([trace[@"status"] isEqual:@"multiplayer"] && !trace[@"enemies"] && !writes && !canUseCalls,
            "multiplayer including solo continuation never reads single-player enemy actions");
    }
    EnemyActionFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
    [engine observeBattle:&state];
    Protected(address + 64, 99, 0x766cfc6d);
    trace = [engine observeEnemyActions:&state];
    Check([trace[@"status"] isEqual:@"changed"] && !trace[@"definitions"],
        "enemy HP changing between the older observation and action capture cannot be presented as one coherent sample");
    EnemyActionFixture(); engine = [FixtureSkillEngine new];
    Protected(address + 80, 2, 0x766cfc6d); Protected(address + 608, 3, 0x766cfc6d);
    Protected(address + 640, 4, 0x766cfc6d);
    trace = CaptureEnemyActions(engine);
    Check([trace[@"enemies"][0][@"stateKnown"] boolValue] &&
        [trace[@"enemies"][0][@"hp"] unsignedLongLongValue] == (UINT64_C(2) << 32) + 100 &&
        [trace[@"enemies"][0][@"maximumHP"] unsignedLongLongValue] == (UINT64_C(3) << 32) + 1000 &&
        [trace[@"enemies"][0][@"baseAttack"] unsignedLongLongValue] == (UINT64_C(4) << 32) + 20000,
        "HP and attack above 32 bits retain both independently protected halves");
    EnemyActionFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
    PHOrbSnapshot transitional = state; transitional.phase = 22;
    Check([[engine observeEnemyActions:&transitional][@"status"] isEqual:@"transition"],
        "a skill phase never reuses the prior action capture");
    transitional = state; transitional.dragged = Impl + PHOrbPoolOffset;
    Check([[engine observeEnemyActions:&transitional][@"status"] isEqual:@"transition"],
        "a held orb never starts diagnostic reads");
    EnemyActionFixture(); engine = [FixtureSkillEngine new];
    Put<uint32_t>(address + PHOrbEnemyActionsCatalogIDOffset, 100400);
    Put<uint32_t>(parameter + PHOrbLeaderCatalogNextOffset, 400);
    trace = CaptureEnemyActions(engine);
    Check(![trace[@"enemies"][0][@"identityKnown"] boolValue] && ![trace[@"rawCaptureComplete"] boolValue] &&
        !trace[@"enemies"][0][@"rows"] && [trace[@"readCalls"] unsignedIntValue] < EA::MaxReads,
        "a catalog link cycle ends with unknown identity before exhausting the read budget");
    // Empty row lists are useful captured data, never evidence of no attack.
    EnemyActionFixture(); engine = [FixtureSkillEngine new];
    std::memset(reinterpret_cast<void *>(parameter + PHOrbEnemyActionsRowsOffset), 0, EA::Rows * 8);
    Put<int32_t>(address + 1680, -1);
    trace = CaptureEnemyActions(engine);
    Check([trace[@"rawCaptureComplete"] boolValue] && [trace[@"definitions"] count] == 0 &&
        ![trace[@"enemyForecastKnown"] boolValue] && ![trace[@"damageBoundKnown"] boolValue],
        "an empty enemy row table does not predict a skipped turn or zero damage");
    // Whole-table read budget: a long legal allocation chain cannot exceed it.
    EnemyActionFixture(); engine = [FixtureSkillEngine new];
    Put<uint32_t>(address + PHOrbEnemyActionsCatalogIDOffset, 100400);
    Put<uint32_t>(parameter + PHOrbLeaderCatalogNextOffset, 14600);
    for (unsigned i = 14600; i < 14728; ++i) {
        const uintptr_t row = reinterpret_cast<uintptr_t>(catalogBytes.data()) + i * PHBattleCatalogStride;
        Put<uint32_t>(row, 200400); Put<uint32_t>(row + PHOrbLeaderCatalogNextOffset, i == 14727 ? 0 : i + 1);
    }
    std::memcpy(reinterpret_cast<void *>(address + PHOrbEnemyStride), reinterpret_cast<void *>(address), PHOrbEnemyStride);
    Protected(Impl + PHOrbEnemyCountOffset, 2, 0x766cfc6d);
    trace = CaptureEnemyActions(engine);
    Check([trace[@"readBudgetExceeded"] boolValue] && [trace[@"readBytes"] unsignedIntValue] <= EA::MaxReadBytes &&
        [trace[@"readCalls"] unsignedIntValue] <= EA::MaxReads && ![trace[@"rawCaptureComplete"] boolValue],
        "catalog chains share the same bounded byte/call budget across all enemy slots");
    EnemyActionFixture(); engine = [FixtureSkillEngine new];
    std::memcpy(reinterpret_cast<void *>(address + PHOrbEnemyStride), reinterpret_cast<void *>(address), PHOrbEnemyStride);
    Protected(Impl + PHOrbEnemyCountOffset, 2, 0x766cfc6d);
    trace = CaptureEnemyActions(engine);
    Check([trace[@"enemies"] count] == 2 && [trace[@"definitions"] count] == 6 &&
        [trace[@"enemies"][0][@"slot"] intValue] == 1 && [trace[@"enemies"][1][@"slot"] intValue] == 2 &&
        ![trace[@"survivalKnown"] boolValue], "multiple enemies preserve native slot order and share records without summing hypothetical attacks");
}
