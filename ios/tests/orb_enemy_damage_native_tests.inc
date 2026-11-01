// Written adapter cases only. Native guest functions must never be invoked.
static void EnemyDamageFixture() {
    EnemyDispatchFixture();
    for (const auto &guard : PHOrbEnemyDamageTextGuards) std::memcpy(reinterpret_cast<void *>(Header + guard.rva), guard.bytes, 64);
    Put<uint8_t>(Impl + PHOrbEnemyDamageImmediateOffset, 0);
    Put<uintptr_t>(Impl + PHOrbEnemyDamageCallbackOffset, 0);
    const auto enemy = Impl + PHOrbEnemyOffset;
    Put<int64_t>(enemy + 120, 1000); Put<int64_t>(enemy + 128, 900);
    Put<uint16_t>(enemy + 2892, 50); Put<uint16_t>(enemy + 2894, 50); Put<uint16_t>(enemy + 2896, 30);
    Put<uint64_t>(enemy + 2904, 0); Put<int16_t>(enemy + 2912, 20); Put<uint16_t>(enemy + 2914, 7);
    Put<int16_t>(enemy + 2968, 1); Put<int16_t>(enemy + 2970, -2);
    Protected(enemy + 2464, 0, 0x6a875a8b); Protected(enemy + 2704, 0, 0x6a875a8b);
    Protected(enemy + 2288, 8, 0x6a875a8b); Protected(enemy + 2304, 100, 0x6a875a8b); Protected(enemy + 2320, 1, 0x6a875a8b);
}
static void EnemyDamageNativeTests() {
    EnemyDispatchFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    NSDictionary *trace = CaptureEnemyActions(engine);
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"] &&
        [trace[@"damageAnalysis"][@"status"] isEqual:@"unavailable"], "prior diagnostics work without optional damage guards");
    EnemyDamageFixture(); engine = [FixtureSkillEngine new]; PHOrbSnapshot state{};
    const auto oldGuest = guest, oldImpl = implBytes, oldCatalog = catalogBytes;
    trace = CaptureEnemyActions(engine, &state);
    NSDictionary *damage = trace[@"enemies"][0][@"damageAnalysis"], *summary = trace[@"damageAnalysis"];
    NSDictionary *lethal = damage[@"probes"][2][@"checkpoint"];
    Check([summary[@"status"] isEqual:@"ready"] && [damage[@"snapshotHPCurrent"] longLongValue] == 1000 &&
        [damage[@"secondarySnapshotHPCurrent"] longLongValue] == 900 && ![damage[@"snapshotEqualsCurrentHP"] boolValue] &&
        [damage[@"normalThresholdHP"] longLongValue] == 500 && [damage[@"superFloorHP"] longLongValue] == 300 &&
        [lethal[@"localFieldsKnown"] boolValue] && [lethal[@"hp"] longLongValue] == 300 &&
        [lethal[@"survivalFlags"] intValue] == 8 && ![lethal[@"phaseBeforeSurvival"] intValue],
        "adapter retains distinct snapshots and native thresholds; lethal zero HP reaches super floor before cleanup");
    Check([damage[@"probes"][1][@"checkpoint"][@"phaseBeforeSurvival"] intValue] == 1 &&
        [damage[@"probes"][1][@"checkpoint"][@"hp"] longLongValue] == 300 &&
        [damage[@"ifPhaseScanReachesEnemy"][@"phaseFlags"] intValue] == 0x13 &&
        [damage[@"ifSelectedTargetAnimationCompletes"][@"countdown"] intValue] == 0 &&
        ![damage[@"overrideStateAfterDamageKnown"] boolValue] && [damage[@"overrideAccumulationEnabledRaw"] intValue] == 1,
        "phase-before-floor, raw count trigger and unresolved override changes are separate conditional evidence");
    Check([summary[@"readBytes"] unsignedIntValue] <= EH::MaxReadBytes && [summary[@"readCalls"] unsignedIntValue] <= EH::MaxReads &&
        ![summary[@"scenarioCoverageComplete"] boolValue] && ![summary[@"enemyForecastKnown"] boolValue] &&
        ![damage[@"postPlayerStateKnown"] boolValue] && ![damage[@"survivalKnown"] boolValue] &&
        guest == oldGuest && implBytes == oldImpl && catalogBytes == oldCatalog && !writes && !canUseCalls &&
        !readinessCalls && !choiceCalls && !skillStartCalls, "bounded value-only probes never mutate game, call native skills or establish survival");
    const auto enemy = Impl + PHOrbEnemyOffset;
    Put<uint8_t>(Impl + PHOrbEnemyDamageImmediateOffset, 1); trace = CaptureEnemyActions(engine);
    damage = trace[@"enemies"][0][@"damageAnalysis"]; lethal = damage[@"probes"][2][@"checkpoint"];
    Check([lethal[@"localFieldsKnown"] boolValue] && ![lethal[@"survivalFlags"] intValue] &&
        [lethal[@"snapshotHP"] longLongValue] == 300 && [damage[@"ifSelectedTargetAnimationCompletes"][@"countdown"] intValue] == -2,
        "immediate cleanup rewrites snapshot and count animation uses signed negative interval");
    Put<uintptr_t>(Impl + PHOrbEnemyDamageCallbackOffset, Impl); trace = CaptureEnemyActions(engine);
    lethal = trace[@"enemies"][0][@"damageAnalysis"][@"probes"][2][@"checkpoint"];
    Check([lethal[@"status"] isEqual:@"hp-callback-unresolved"] && [lethal[@"hp"] isEqual:NSNull.null] &&
        [lethal[@"hpBeforeSurvival"] longLongValue] == 0 &&
        [trace[@"enemies"][0][@"damageAnalysis"][@"probes"][0][@"checkpoint"][@"localFieldsKnown"] boolValue],
        "positive HP callback stops local suffix while zero-damage prefix remains separate");
    EnemyDamageFixture(); engine = [FixtureSkillEngine new];
    Protected(enemy + 2704, 1, 0x6a875a8b); trace = CaptureEnemyActions(engine);
    Check([trace[@"enemies"][0][@"damageAnalysis"][@"probes"][2][@"checkpoint"][@"status"] isEqual:@"separate-hp-unmodeled"],
        "separate HP mode blocks ordinary damage probes without removing thresholds");
    Protected(enemy + 2704, UINT32_MAX, 0x6a875a8b); trace = CaptureEnemyActions(engine);
    Check([trace[@"enemies"][0][@"damageAnalysis"][@"separateHPModeCurrent"] intValue] == -1 &&
        [trace[@"enemies"][0][@"damageAnalysis"][@"probes"][2][@"checkpoint"][@"localFieldsKnown"] boolValue],
        "canonical sign-extended protected byte retains native negative semantics");
    EnemyDamageFixture(); engine = [FixtureSkillEngine new];
    Put<uint64_t>(enemy + 2904, uint64_t(1) << 63); trace = CaptureEnemyActions(engine);
    damage = trace[@"enemies"][0][@"damageAnalysis"];
    Check([damage[@"status"] isEqual:@"conditional"] && [damage[@"superFloorHP"] isEqual:NSNull.null] &&
        [damage[@"probes"][2][@"checkpoint"][@"status"] isEqual:@"numeric-conversion-unresolved"],
        "out-of-range fixed HP is retained as raw evidence without unsafe numeric conversion");
    for (unsigned offset : {2464u, 2704u}) {
        EnemyDamageFixture(); engine = [FixtureSkillEngine new]; Protected(enemy + offset, 256, 0x6a875a8b);
        trace = CaptureEnemyActions(engine);
        Check([trace[@"status"] isEqual:@"ready"] && [trace[@"enemies"][0][@"damageAnalysis"][@"status"] isEqual:@"unavailable"] &&
            [trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"], "noncanonical protected damage bytes invalidate only enemy damage fields");
    }
    EnemyDamageFixture(); engine = [FixtureSkillEngine new]; Protected(enemy + 2320, 65536, 0x6a875a8b);
    trace = CaptureEnemyActions(engine); damage = trace[@"enemies"][0][@"damageAnalysis"];
    Check([damage[@"status"] isEqual:@"conditional"] && ![damage[@"overrideAccumulationKnown"] boolValue] &&
        [damage[@"overrideAccumulationEnabledRaw"] isEqual:NSNull.null], "invalid override counter does not pretend absent damage side effects");
    EnemyDamageFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
    // Static audit verifies this byte is outside all prior guard groups.
    Put<uint8_t>(Header + PHOrbEnemyDamageSuperFloorSourceRVA, uint8_t(guest[PHOrbEnemyDamageSuperFloorSourceRVA] ^ 1u));
    trace = [engine observeEnemyActions:&state];
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"damageAnalysis"][@"status"] isEqual:@"unavailable"] &&
        [trace[@"selectorAnalysis"][@"status"] isEqual:@"ready"] && [trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"] &&
        [engine readAutomation:&state frame:Frame() reason:nil] && !writes, "damage fingerprint failure preserves input and prior diagnostic paths");
    EnemyDamageFixture(); engine = [FixtureSkillEngine new]; rejectEnemyDamageRead = true;
    trace = CaptureEnemyActions(engine);
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"damageAnalysis"][@"status"] isEqual:@"unavailable"] &&
        [trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"], "optional damage read failure clears only damage data");
    for (unsigned mutation = 1; mutation <= 6; ++mutation) {
        EnemyDamageFixture(); engine = [FixtureSkillEngine new]; enemyDamageMutation = mutation;
        trace = CaptureEnemyActions(engine);
        if (mutation == 6) Check(![trace[@"status"] isEqual:@"ready"] && !trace[@"enemies"], "identity race discards all diagnostics");
        else Check([trace[@"status"] isEqual:@"ready"] && [trace[@"damageAnalysis"][@"status"] isEqual:@"changed"] &&
            !trace[@"enemies"][0][@"damageAnalysis"][@"probes"] && [trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"],
            "snapshot, threshold, phase, immediate or callback races withdraw only optional damage checkpoints");
    }
    EnemyDamageFixture(); engine = [FixtureSkillEngine new];
    const auto second = enemy + PHOrbEnemyStride;
    std::memcpy(reinterpret_cast<void *>(second), reinterpret_cast<void *>(enemy), PHOrbEnemyStride);
    Put<uint8_t>(second + 1, 2); Put<uint16_t>(second + 532, 0x100); Protected(Impl + PHOrbEnemyCountOffset, 2, 0x766cfc6d);
    trace = CaptureEnemyActions(engine);
    Check([trace[@"damageAnalysis"][@"activeByteSum"] intValue] == 3 &&
        ![trace[@"enemies"][0][@"damageAnalysis"][@"ifPhaseScanReachesEnemy"][@"targetRequested"] boolValue] &&
        [trace[@"enemies"][1][@"damageAnalysis"][@"status"] isEqual:@"unavailable"],
        "raw active-byte aggregate includes parts even though ordinary damage modeling excludes them");
    EnemyDamageFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
    EnemyActionCapture actions{}; actions.enemyCount = EA::MaxEnemies;
    for (unsigned slot = 0; slot < EA::MaxEnemies; ++slot) {
        if (slot) std::memcpy(reinterpret_cast<void *>(enemy + slot * PHOrbEnemyStride), reinterpret_cast<void *>(enemy), PHOrbEnemyStride);
        actions.enemies[slot].memoryKnown = true; actions.enemies[slot].active[1] = 255;
    }
    EnemyDamageCapture capture{};
    Check(CollectEnemyDamage(state, actions, 17, 42, capture) && capture.activeByteSum == 255u * EA::MaxEnemies &&
        capture.readBytes <= EH::MaxReadBytes && capture.readCalls <= EH::MaxReads, "all eight slots fit shared optional data-read budget");
    uint8_t bytes[2]{}; capture = {}; capture.readBytes = EH::MaxReadBytes - 1;
    Check(!EnemyDamageRead(capture, Impl, bytes, 2) && capture.readBudgetExceeded && !capture.readCalls, "byte budget enforced before read");
    capture = {}; capture.readCalls = EH::MaxReads;
    Check(!EnemyDamageRead(capture, Impl, bytes, 1) && capture.readBudgetExceeded && !capture.readBytes, "call budget enforced before read");
    capture = {}; Check(!EnemyDamageRead(capture, UINTPTR_MAX - 1, bytes, 2), "optional pointer span overflow rejected");
    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        EnemyDamageFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
        state.multiplayer.kind = kind; trace = [engine observeEnemyActions:&state];
        Check([trace[@"status"] isEqual:@"multiplayer"] && !trace[@"damageAnalysis"] && !writes && !canUseCalls,
            "all multiplayer kinds including three-player solo continuation exclude damage diagnostics");
    }
}
