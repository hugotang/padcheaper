static void SetEnemyPart(unsigned slot, int8_t mode, uint64_t hp, int8_t owner = 0) {
    const uintptr_t part = Impl + PHOrbEnemyOffset + slot * PHOrbEnemyStride;
    Put<uint16_t>(part + 532, 0x100);
    Put<uint8_t>(part, 0); Put<uint8_t>(part + 1, 0); // Not native liveness evidence.
    Put<uint8_t>(part + 23, 255); Put<uint8_t>(part + 36, uint8_t(slot - 1)); Put<int8_t>(part + 37, owner);
    Protected(part + 2704, uint32_t(int32_t(mode)), 0x6a875a8b);
    Protected(part + 2672, uint32_t(hp >> 32), 0x6a875a8b); Protected(part + 2688, uint32_t(hp), 0x6a875a8b);
    // Intentionally leave ordinary HP undecodable: parts never use it.
    for (uintptr_t offset : {uintptr_t(2400), uintptr_t(2192), uintptr_t(2256), uintptr_t(2224),
                            uintptr_t(2176), uintptr_t(2240), uintptr_t(2432)})
        Protected(part + offset, 0, 0x6a875a8b);
}
static void EnemyPartsFixture() {
    SkillFixture(); Protected(Impl + PHOrbEnemyCountOffset, 3, 0x766cfc6d);
    SetEnemyPart(1, -1, 900); SetEnemyPart(2, 1, 0);
}
static void EnemyPartsNativeTests() {
    const uintptr_t part = Impl + PHOrbEnemyOffset + PHOrbEnemyStride;
    const uintptr_t boss = Impl + PHOrbEnemyOffset;
    const uintptr_t caster = Impl + PHBattleMemberOffset + 2 * PHBattleMemberStride;
    PHOrbSnapshot state{}, next{}; NSString *reason = nil; unsigned minimum = 0;
    for (bool living : {false, true}) {
        EnemyPartsFixture(); auto engine = [FixtureSkillEngine new];
        if (living) SetEnemyPart(1, 1, UINT64_C(500000000000));
        SetLeader(2, 14060);
        const uint16_t assist = 13993; // Actual active/source assist catalog ID in the log.
        Put<uint16_t>(caster + PHBattleAssistIDOffset, assist); Put<uint16_t>(caster + 1438, assist);
        Protected(caster + PHBattleChargeOffset, 11, 0x6a875a8b);
        Put<uint32_t>(caster + PHBattleChargeShadowOffset, 11u ^ 0x68fb65ceu);
        Put<uint16_t>(Impl + PHBattleAwakeningStateOffset, 5);
        readinessFixture = {64720, 63581, 5, 21};
        state = CaptureEnemyCombo(engine);
        Check(state.automation.window && state.automation.enemiesKnown && state.automation.partsMask == 6 &&
            state.automation.activeEnemies == (living ? 2 : 1) &&
            (state.automation.playerEffects & PHOrbPlayerAwokenBind) &&
            PHOrbEnemyComboRequirement(&state.enemyCombo, &minimum) && !minimum && !writes,
            "boss plus two parts permits the ready position-three cleanse and subsequent Combo planning");
        Check([engine useSkill:2 request:PHOrbSkillRequest{PHOrbSkillBody,64720} expected:&state frame:Frame() reason:&reason] ==
            PHOrbSkillStarted && canUseCalls == 1 && skillStartCalls == 1 && sourceAtStart == 0 &&
            effectiveStartedSkill == 64720 && effectsStartedSkill == 64720,
            "eleven charges selects body CD five, not assist CD twenty-one, with inactive or living parts");
    }
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        EnemyPartsFixture(); auto engine = [FixtureSkillEngine new]; SetEnemyPart(1, 1, 200);
        if (scenario == 0) Put<uint32_t>(part + 2704, 0); // Disagreeing protected copies.
        if (scenario == 1) Protected(part + 2704, 256, 0x6a875a8b);
        if (scenario == 2) Put<int8_t>(part + 37, -1);
        if (scenario == 3) Put<int8_t>(part + 37, 3);
        if (scenario == 4) Put<int8_t>(part + 37, 2); // A part cannot own a part.
        if (scenario == 5) Protected(boss + 64, 0, 0x766cfc6d);
        if (scenario == 6) Put<uint32_t>(part + 2688, 0);
        if (scenario == 7) Put<uint8_t>(Header + PHOrbEnemyPartsTextGuards[0].rva,
            uint8_t(PHOrbEnemyPartsTextGuards[0].bytes[0] ^ 1u));
        if (scenario == 8) Put<int8_t>(part + 37, 1);
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.window &&
            !state.automation.enemiesKnown && (state.automation.windowBlocks & PHOrbSkillWindowEnemyParts),
            "corrupt part values, invalid/dead owners and fingerprints cannot silently reopen the skill window");
        Check([engine useSkill:2 expected:&state frame:Frame() reason:&reason] == PHOrbSkillWindowClosed &&
            !canUseCalls && !skillStartCalls && !writes,
            "unknown part proof stops before native skill calls");
        if (scenario == 7) {
            Protected(Impl + PHOrbEnemyCountOffset, 1, 0x766cfc6d);
            Check([engine readAutomation:&state frame:Frame() reason:nil] && state.automation.window,
                "part-only fingerprint mismatch does not disable ordinary floors");
        }
    }
    EnemyPartsFixture(); auto engine = [FixtureSkillEngine new];
    Protected(part + 2704, 255, 0x6a875a8b); Put<uint32_t>(part + 2688, 0);
    state = CaptureEnemyCombo(engine);
    Check(state.enemyCombo.parts[1].mode == -1 && state.enemyCombo.hp[1] == UINT64_MAX &&
        PHOrbEnemyComboRequirement(&state.enemyCombo, &minimum),
        "native zero-extended absent mode skips stale part HP, never using raw active bytes");
    SetEnemyPart(1, 0, 200); state = CaptureEnemyCombo(engine);
    Check(state.enemyCombo.aliveMask == 3 && state.enemyCombo.hp[1] == 200,
        "mode zero and positive separate HP are living even with both raw active bytes zero");
    Protected(part + PHOrbEnemyComboDurationOffset, 3, 0x6a875a8b);
    Protected(part + PHOrbEnemyComboThresholdOffset, 9, 0x6a875a8b);
    state = CaptureEnemyCombo(engine);
    Check(PHOrbEnemyComboRequirement(&state.enemyCombo, &minimum) && minimum == 10 &&
        (state.automation.enemyEffects & PHOrbEnemyComboAbsorb),
        "a living part's absorption is conservatively included instead of assumed absent");
    Protected(part + PHOrbEnemyComboUpperDurationOffset, 1, 0x6a875a8b); state = CaptureEnemyCombo(engine);
    Check(!PHOrbEnemyComboRequirement(&state.enemyCombo, &minimum), "living part upper absorption remains unsupported");
    SetEnemyPart(1, 1, 200); Put<uint8_t>(part + 23, 0);
    Check([engine readAutomation:&state frame:Frame() reason:nil] && state.automation.window && !state.automation.busy,
        "part action bytes do not become ordinary-enemy skill action gates");
    Check(![engine readEnemyCombo:&state frame:Frame() reason:nil], "a busy living part still prevents a new Auto route");
    Put<uint8_t>(boss + 23, 0);
    Check([engine readAutomation:&state frame:Frame() reason:nil] && !state.automation.window &&
        (state.automation.windowBlocks & PHOrbSkillWindowEnemyAction), "ordinary boss action still closes the skill window");
    EnemyPartsFixture(); engine = [FixtureSkillEngine new];
    Protected(boss + 64, 0, 0x766cfc6d);
    Check([engine readAutomation:&state frame:Frame() reason:nil] && !state.automation.window &&
        (state.automation.windowBlocks & PHOrbSkillWindowNoEnemy), "only inactive parts cannot fake a living enemy");
    EnemyPartsFixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
    changePartInCanUse = true;
    Check([engine useSkill:2 expected:&state frame:Frame() reason:nil] == PHOrbSkillRejected &&
        canUseCalls == 1 && !skillStartCalls && !writes, "part revival during native availability cancels the pending skill");
    EnemyPartsFixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
    changePartAfterReads = 2; // After automation sandwich entry, during first Combo collection.
    Check(![engine readEnemyCombo:&state frame:Frame() reason:nil] && !changePartAfterReads && !state.enemyCombo.valid && !writes,
        "part identity changes between captures revoke the Combo evidence");
    EnemyPartsFixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
    changePartDuringCoordinateRead = true;
    Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil] &&
        !changePartDuringCoordinateRead && !writes, "part identity changes at coordinate conversion prevent the first input write");
    EnemyPartsFixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
    Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil], "verified part floor can begin input");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
    Check([engine finishInput:nil], "part floor restores native input after grabbing");
    state = CaptureEnemyCombo(engine); SetEnemyPart(1, 1, 900);
    const unsigned priorWrites = writes;
    Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:nil] && writes == priorWrites,
        "reviving a part while holding cannot continue a stale route");
    Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:nil], "owned release remains possible after part revival");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
    Check([engine finishInput:nil], "part-change cleanup restores native input");
    EnemyPartsFixture(); engine = [FixtureSkillEngine new];
    Check([engine readAutomation:&next frame:Frame() reason:nil], "part diagnostic fixture reads automation");
    [engine observeBattle:&next];
    Check(next.observation.status == PHOrbObservationReady && next.observation.enemiesKnown && next.observation.enemies[1].part.known &&
        next.observation.enemies[1].part.mode == -1 && !next.observation.enemies[1].hp,
        "diagnostics expose authoritative absent mode separately from displayed zero HP");
    Reset();
}
