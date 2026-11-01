static PHOrbSnapshot CaptureEnemyCombo(FixtureSkillEngine *engine) {
    PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil], "enemy Combo fixture has a stable automation read");
    Check([engine readEnemyCombo:&state frame:Frame() reason:nil], "enemy Combo fresh double capture succeeds");
    return state;
}
static void EnemyComboNativeTests() {
    const uintptr_t enemy = Impl + PHOrbEnemyOffset;
    auto fixture = [&]() {
        SkillFixture();
        Protected(enemy + PHOrbEnemyComboDurationOffset, 3, 0x6a875a8b);
        Protected(enemy + PHOrbEnemyComboThresholdOffset, 7, 0x6a875a8b);
    };
    fixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    const auto originalGuest = guest, originalImpl = implBytes;
    PHOrbSnapshot state = CaptureEnemyCombo(engine); unsigned minimum = 0;
    Check(PHOrbEnemyComboRequirement(&state.enemyCombo, &minimum) && minimum == 8 &&
        state.enemyCombo.turns[0] == 3 && state.enemyCombo.hp[0] == 100,
        "protected threshold and duration derive an eight-Combo requirement");
    Check(guest == originalGuest && implBytes == originalImpl && !writes && !canUseCalls && !readinessCalls,
        "current enemy countermeasure capture is read only and never calls getters");
    Put<uint16_t>(Impl + PHOrbEnemyComboBypassOffset, 0x401);
    state = CaptureEnemyCombo(engine);
    Check(PHOrbEnemyComboRequirement(&state.enemyCombo, &minimum) && !minimum,
        "fresh packed positive bypass duration removes absorption requirement");
    Put<uint16_t>(Impl + PHOrbEnemyComboBypassOffset + 2, 1);
    state = CaptureEnemyCombo(engine);
    Check(PHOrbEnemyComboRequirement(&state.enemyCombo, &minimum) && minimum == 8,
        "delayed bypass is not treated as active");
    Put<uint16_t>(Impl + PHOrbEnemyComboAdjustmentOffset + 16,0x401);
    Protected(Impl + PHOrbEnemyComboAdjustmentOffset,uint32_t(int32_t(-2)),0x6a875a8b);
    Put<uint16_t>(Impl + PHOrbEnemyComboSubtractOffset,3);
    Protected(Impl + PHOrbEnemyComboSubtractOffset + 4,1,0x6a875a8b);
    state = CaptureEnemyCombo(engine);
    Check(state.enemyCombo.adjustment == -2 && state.enemyCombo.subtract == 1 &&
        PHOrbEnemyComboRequirement(&state.enemyCombo,&minimum) && minimum == 11,
        "protected signed-byte and unsigned-short debits are both reserved in the first-removal target");
    Protected(Impl + PHOrbEnemyComboAdjustmentOffset,254,0x6a875a8b);
    state = CaptureEnemyCombo(engine);
    Check(state.enemyCombo.adjustment == -2 && PHOrbEnemyComboRequirement(&state.enemyCombo,&minimum) && minimum == 11,
        "zero-extended signed-byte storage has the same native negative adjustment");
    Protected(Impl + PHOrbEnemyComboAdjustmentOffset,2,0x6a875a8b);
    state = CaptureEnemyCombo(engine);
    Check(PHOrbEnemyComboRequirement(&state.enemyCombo,&minimum) && minimum == 9,
        "positive native Combo credits are conservatively excluded");
    Protected(Impl + PHOrbEnemyComboAdjustmentOffset,256,0x6a875a8b);
    Check(![engine readEnemyCombo:&state frame:Frame() reason:nil] && !state.enemyCombo.valid,
        "malformed protected adjustment cannot narrow silently into valid credit");
    fixture(); engine = [FixtureSkillEngine new];
    Protected(enemy + PHOrbEnemyComboUpperDurationOffset,3,0x6a875a8b);
    state = CaptureEnemyCombo(engine);
    Check(state.enemyCombo.upperTurns[0] == 3 && !PHOrbEnemyComboRequirement(&state.enemyCombo,&minimum),
        "active upper-Combo absorption is captured but cannot authorize an unbounded maximum");
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        fixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
        if (scenario == 0) rejectComboRead = true;
        if (scenario == 1) changeComboDuringRead = true;
        if (scenario == 2) Put<uint16_t>(enemy + 532, 0x100);
        if (scenario == 3) state.multiplayer.kind = PHOrbMultiplayerThreeSolo;
        if (scenario == 4) Put<uint8_t>(Header + PHOrbEnemyComboTextGuards[0].rva,
            uint8_t(PHOrbEnemyComboTextGuards[0].bytes[0] ^ 1u));
        Check(![engine readEnemyCombo:&state frame:Frame() reason:nil] && !state.enemyCombo.valid && !writes,
            "missing threshold, mid-read changes, parts, multiplayer or guard mismatch revoke enemy policy evidence");
        rejectComboRead = false;
        if (scenario == 4) Check([engine readAutomation:&state frame:Frame() reason:nil],
            "enemy countermeasure fingerprints remain separate from ordinary automation reads");
    }
    fixture(); engine = [FixtureSkillEngine new];
    Put<uint16_t>(Impl + PHOrbEnemyComboBypassOffset, 1); state = CaptureEnemyCombo(engine);
    changeComboDuringCoordinateRead = true;
    Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil] &&
        !changeComboDuringCoordinateRead && !writes,
        "bypass expiring during coordinate conversion prevents the first native input write");
    fixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
    Put<uint16_t>(Impl + PHOrbEnemyComboSubtractOffset,1);
    Protected(Impl + PHOrbEnemyComboSubtractOffset + 4,2,0x6a875a8b);
    Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil] && !writes,
        "a newly applied Combo debit prevents the first native write even if absorption stayed unchanged");
    fixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
    const auto originalInput = realInput;
    Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil],
        "unchanged enemy evidence permits a scoped native start");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
    Check([engine finishInput:nil], "enemy countermeasure start restores the real input manager");
    Protected(enemy + PHOrbEnemyComboThresholdOffset, 9, 0x6a875a8b);
    const unsigned completedWrites = writes;
    Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:nil] && writes == completedWrites,
        "changed absorption does not emit another hold");
    Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:nil],
        "an owned release is still possible when the enemy requirement changes");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
    Check([engine finishInput:nil] && realInput == originalInput && GlobalInput() == Input,
        "enemy policy cleanup preserves the entire real input bank");
    Reset();
}
