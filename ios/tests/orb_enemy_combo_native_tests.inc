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
        "raw requirement leaves positive credits for solo planning and preserves the 8P policy");
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
    state = CaptureEnemyCombo(engine);
    Check(state.dragged == state.board.tokens[0] &&
        [engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:nil],
        "fresh owned-drag evidence permits a held frame before absorption changes");
    NativeInputFrame(); Check([engine finishInput:nil], "unchanged held frame restores input");
    Protected(enemy + PHOrbEnemyComboThresholdOffset, 9, 0x6a875a8b);
    const unsigned completedWrites = writes;
    NSString *holdReason = nil;
    Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&holdReason] &&
        writes == completedWrites && [holdReason containsString:@"Combo"],
        "changed absorption does not emit another hold");
    Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:nil],
        "an owned release is still possible when the enemy requirement changes");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
    Check([engine finishInput:nil] && realInput == originalInput && GlobalInput() == Input,
        "enemy policy cleanup preserves the entire real input bank");
    Reset();
}

static void ComboBonusNativeTests() {
    const uintptr_t leader = Impl + PHOrbComboBonusLeaderOffset;
    const uintptr_t helper = leader + 352, badge = Impl + PHOrbComboBonusBadgeOffset;
    auto fixture = [&]() {
        ObservationFixture();
        for (unsigned slot : {0u,5u}) {
            const uintptr_t member = Impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
            const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(member + PHBattleCardParamOffset));
            Put<uint32_t>(parameter + PHOrbLeaderSkillOffset,slot ? 64920 : 64791);
            Protected(member + PHBattleSourceSlotOffset,slot,0x6a875a8b);
        }
        const uintptr_t member = Impl + PHBattleMemberOffset;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(member + PHBattleCardParamOffset));
        Put<uint16_t>(parameter + 748,126);
        Put<uint32_t>(leader + 268,16); Put<uint16_t>(leader + 290,2);
        const uint8_t groups[] = {3,5,255,255,255,2,0,4,32};
        std::memcpy(reinterpret_cast<void *>(helper + 156),groups,sizeof(groups));
        Put<uint32_t>(badge,16); Put<uint16_t>(badge + 22,2);
    };
    PHOrbGoalSet goals{}; goals.count = 2;
    goals.items[0] = PHOrbGoal{PHOrbGoalT,8,1,0}; goals.items[1] = PHOrbGoal{PHOrbGoalFour,32,1,0};
    fixture(); auto engine = [FixtureSkillEngine new];
    const auto oldGuest = guest, oldImpl = implBytes, oldCatalog = catalogBytes;
    auto state = CaptureEnemyCombo(engine); unsigned sources[3]{};
    Check(state.enemyCombo.bonus.valid && PHOrbGuaranteedComboBonuses(&state.enemyCombo.bonus,&goals,6,5,sources) == 8 &&
        sources[0] == 2 && sources[1] == 4 && sources[2] == 2,
        "effective leader/helper/badge descriptors supply the Uryu T plus light/heart bonuses");
    Check(guest == oldGuest && implBytes == oldImpl && catalogBytes == oldCatalog && !writes && !canUseCalls && !readinessCalls,
        "Combo bonus capture does not mutate guest memory or call native getters");
    // Last-turn activation flags reset at grab; this must not look like a new skill.
    Put<uint16_t>(leader + 288,25865); Put<uint8_t>(helper + 162,198); Put<uint16_t>(badge + 20,25865);
    auto next = CaptureEnemyCombo(engine);
    Check(PHOrbSameEnemyCombo(&state.enemyCombo,&next.enemyCombo),"consumption markers are not bonus-source identity");
    Put<uint16_t>(badge + 22,3); next = CaptureEnemyCombo(engine);
    Check(!PHOrbSameEnemyCombo(&state.enemyCombo,&next.enemyCombo),"changed effective badge amount revokes the previous proof");
    for (unsigned provider = 0; provider < 5; ++provider) {
        fixture(); engine = [FixtureSkillEngine new];
        const uintptr_t member = Impl + PHBattleMemberOffset;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(member + PHBattleCardParamOffset));
        const uintptr_t profile = Load<uintptr_t>(reinterpret_cast<uint8_t *>(member + PHOrbObservationProfileOffset));
        Put<uint16_t>(parameter + 748,0);
        if (provider < 2 || provider == 4) Put<uint32_t>(profile + 16,126);
        if (provider == 1) { Put<uint16_t>(member + 1296,1); Put<uint16_t>(parameter + 788,126); }
        if (provider == 2) {
            const uintptr_t equip = reinterpret_cast<uintptr_t>(catalogBytes.data()) + 300 * PHBattleCatalogStride;
            Put<uint16_t>(equip + 748,49); Put<uint16_t>(equip + 750,126); Put<uint8_t>(member + 1440,2);
        }
        if (provider == 3) Protected(member + PHOrbObservationAddedAwakeningsOffset,126,0x6a875a8b);
        if (provider == 4) Put<uint16_t>(member + 1296,99);
        state = CaptureEnemyCombo(engine);
        Check(PHOrbGuaranteedComboBonuses(&state.enemyCombo.bonus,&goals,6,5,sources) == (provider == 4 ? 4u : 8u),
            "super, sync, equipment and added T awakenings require their current native eligibility");
    }
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        fixture(); engine = [FixtureSkillEngine new];
        if (scenario == 0) Protected(Impl + PHBattleMemberOffset + PHOrbSkillBindOffset,1,0x6a875a8b);
        if (scenario == 1) Put<uint16_t>(Impl + PHBattleAwakeningStateOffset,1);
        if (scenario == 2) Put<uint8_t>(SurvivalDungeon + 33,4);
        if (scenario == 3) Put<uint8_t>(SurvivalDungeon + 32,0x80);
        if (scenario == 4) Put<uint8_t>(Header + PHOrbComboBonusTextGuards[0].rva,
            uint8_t(PHOrbComboBonusTextGuards[0].bytes[0] ^ 1u));
        if (scenario == 5) Put<uint32_t>(leader + 268,1u << 13); // Unsupported awakening 45.
        state = CaptureEnemyCombo(engine);
        const unsigned bonus = PHOrbGuaranteedComboBonuses(&state.enemyCombo.bonus,&goals,6,5,sources);
        Check(bonus == (scenario < 3 ? 4u : scenario == 3 ? 2u : scenario == 4 ? 0u : 6u),
            "bind, awakening suppression, leader disable and unknown policy never supply unproved bonuses");
    }
    fixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
    Put<uint16_t>(badge + 22,3);
    Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil] && !writes,
        "changing a credited badge before input cannot start a stale route");
    for (unsigned mutation : {1u,2u}) {
        fixture(); engine = [FixtureSkillEngine new]; state = CaptureEnemyCombo(engine);
        changeComboBonusDuringRead = mutation;
        if (mutation == 1)
            Check(![engine readEnemyCombo:&state frame:Frame() reason:nil] && !state.enemyCombo.valid && !writes,
                "a badge descriptor changing between the two reads cannot supply action evidence");
        else
            Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil] && !writes,
                "loss of a T provider during the input recheck cannot use the old credited route");
        Check(!changeComboBonusDuringRead,"native bonus mutation was observed");
    }
    Reset();
}
