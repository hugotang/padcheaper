// Extends the ordinary action fixture only here; legacy capture tests retain
// their previous guard set and must work with selector analysis unavailable.
static void EnemySelectorFixture() {
    EnemyActionFixture();
    for (const auto &guard : PHOrbEnemySelectorTextGuards) std::memcpy(reinterpret_cast<void *>(Header + guard.rva), guard.bytes, 64);
    std::memset(reinterpret_cast<void *>(EnemyActionParameterAddress() + PHOrbEnemyActionsRowsOffset), 0, EA::Rows * 8);
    EnemyActionSetRow(0, 1, 20, 4); // <=20% jumps to compound at row4.
    EnemyActionSetRow(1, 3); EnemyActionSetRow(2, 9); EnemyActionSetRow(3, 2); EnemyActionSetRow(4, 9);
    EnemyActionSetRecord(2, 83, {3,4,3,7,0}); EnemyActionSetRecord(7, 82, {});
    EnemyActionSetRecord(9, 0x24, {});
}
static NSDictionary *EnemySelectorCandidate(NSDictionary *selector, unsigned row) {
    for (NSDictionary *candidate in selector[@"candidates"]) if ([candidate[@"row"] unsignedIntValue] == row) return candidate;
    return nil;
}
static void EnemySelectorNativeTests() {
    EnemyActionFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    NSDictionary *trace = CaptureEnemyActions(engine);
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"definitions"] count] &&
        [trace[@"selectorAnalysis"][@"status"] isEqual:@"unavailable"],
        "prior action capture remains available without the new optional selector fingerprints");
    EnemySelectorFixture(); engine = [FixtureSkillEngine new];
    const auto originalGuest = guest, originalImpl = implBytes, originalCatalog = catalogBytes;
    PHOrbSnapshot state{}; trace = CaptureEnemyActions(engine, &state);
    NSDictionary *selector = trace[@"enemies"][0][@"selectorAnalysis"];
    NSDictionary *low = EnemySelectorCandidate(selector, 4), *high = EnemySelectorCandidate(selector, 2);
    Check([trace[@"selectorAnalysis"][@"status"] isEqual:@"ready"] &&
        [selector[@"conditionalWalkComplete"] boolValue] && [selector[@"candidates"] count] == 2 &&
        [low[@"nativeHPPercent"][@"maximum"] intValue] == 20 &&
        ![low[@"nativeHPPercent"][@"maximumExclusive"] boolValue] &&
        [high[@"nativeHPPercent"][@"minimum"] intValue] == 20 &&
        [high[@"nativeHPPercent"][@"minimumExclusive"] boolValue] &&
        [high[@"nativeHPPercent"][@"maximum"] intValue] == 100,
        "native adapter publishes both full-HP partitions without pruning by current ten-percent HP");
    Check([selector[@"candidateSet"] isEqual:@"conditional-conservative-superset"] &&
        ![selector[@"nextEntryKnown"] boolValue] && ![selector[@"aiStateProjected"] boolValue] &&
        ![trace[@"enemyForecastKnown"] boolValue] && ![trace[@"actionReachabilityKnown"] boolValue] &&
        ![trace[@"damageBoundKnown"] boolValue] && ![trace[@"survivalKnown"] boolValue] &&
        guest == originalGuest && implBytes == originalImpl && catalogBytes == originalCatalog && !writes && !canUseCalls,
        "conditional selector analysis never mutates guest state, invokes feasibility, or grants next-turn/survival authority");
    Check([trace[@"enemies"][0][@"selectedActionCached"] intValue] == 8 &&
        [trace[@"enemies"][0][@"attackAdditionRaw"] intValue] == 25,
        "cached dispatch and override values remain raw even when conditional row walking completes");
    for (NSDictionary *candidate in selector[@"candidates"]) Check([candidate[@"actionID"] intValue] != 8,
        "cached-only action ID is not substituted into ordinary row candidates");
    NSDictionary *compound = nil;
    for (NSDictionary *sequence in selector[@"actionSequences"]) if ([sequence[@"actionID"] intValue] == 2) compound = sequence;
    NSArray *steps = compound[@"steps"], *events = [steps.firstObject objectForKey:@"eventsInOrder"];
    Check(steps.count == 4 && [compound[@"definitionOrderKnown"] boolValue] &&
        [steps[0][@"actionID"] isEqual:steps[2][@"actionID"]] && events.count == 2 &&
        [events[0][@"kind"] isEqual:@"attached-attack"] && [events[1][@"kind"] isEqual:@"awoken-bind-attempt"] &&
        [steps[3][@"skippedCompound82"] boolValue] && [steps[3][@"eventsInOrder"] count] == 0 &&
        ![compound[@"executionKnown"] boolValue] && ![compound[@"damageBoundKnown"] boolValue],
        "compound presentation retains duplicates and attached attack before status, with no zero-damage assumption");
    for (bool secondPass : {false, true}) {
        EnemyActionEntry saved{}; saved.rowsKnown = saved.stateKnown = true;
        EA::Graph graph{}; graph.count = 1; graph.nodes[0].id = 1; graph.nodes[0].known = true;
        Store<uint16_t>(graph.nodes[0].record.bytes.data() + 4, 88);
        Store<int32_t>(graph.nodes[0].record.bytes.data() + 56, -1);
        Store<int32_t>(saved.rows, 1); saved.rows[4] = 100; saved.rows[5] = 100;
        Store<uint16_t>(saved.definitionHeader + 4, secondPass ? 0 : 1);
        ES::Budget budget{}; NSDictionary *mode = EnemySelectorTrace(saved, graph, budget);
        Check([mode[@"mode"] isEqual:secondPass ? @"legacy-two-pass" : @"ordered-weighted"] &&
            [mode[@"conditionalWalkComplete"] boolValue] &&
            (secondPass ? [mode[@"candidates"] count] == 1 &&
                [mode[@"candidates"][0][@"origin"] isEqual:@"secondary-choice"] : [mode[@"candidates"] count] == 0),
            "native parameter selector bit chooses the correct HP-filtered primary or unfiltered legacy second scan");
    }

    // This first byte belongs to the isolated selector group, outside every
    // earlier operation/observation/policy/action fingerprint (static audited).
    Put<uint8_t>(Header + PHOrbEnemySelectorOrderedPoolRVA,
        uint8_t(guest[PHOrbEnemySelectorOrderedPoolRVA] ^ 1u));
    trace = [engine observeEnemyActions:&state]; [engine observeBattle:&state];
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"definitions"] count] &&
        [trace[@"selectorAnalysis"][@"status"] isEqual:@"unavailable"] &&
        !trace[@"enemies"][0][@"selectorAnalysis"][@"candidates"] &&
        state.observation.status == PHOrbObservationReady && [engine readAutomation:&state frame:Frame() reason:nil] && !writes,
        "selector-only guard failure drops candidates while raw definitions, earlier diagnostics and automation remain available");
    EnemySelectorFixture(); engine = [FixtureSkillEngine new];
    EnemyActionSetRecord(2, 65535, {}); trace = CaptureEnemyActions(engine);
    selector = trace[@"enemies"][0][@"selectorAnalysis"];
    Check([selector[@"status"] isEqual:@"unknown"] && ![selector[@"conditionalWalkComplete"] boolValue] &&
        [selector[@"candidateSet"] isEqual:@"partial-with-unknown-frontier"] &&
        [selector[@"unsupportedRows"] isEqual:@[@4]] && EnemySelectorCandidate(selector, 2),
        "a valid branch can be retained beside an unknown branch but never advertised as an exclusion proof");
    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        EnemySelectorFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
        state.multiplayer.kind = kind; trace = [engine observeEnemyActions:&state];
        Check([trace[@"status"] isEqual:@"multiplayer"] && !trace[@"selectorAnalysis"] && !writes && !canUseCalls,
            "the selector diagnostic adds no multiplayer path including three-player solo continuation");
    }
}
