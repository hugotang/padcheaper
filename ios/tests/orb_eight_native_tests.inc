static void EightNativeFixture(unsigned seat) {
    std::fill(catalogBytes.begin(),catalogBytes.end(),0);
    SkillFixture(); SetNativeMultiplayer(8,seat); SetNativeLeaderOwner(seat);
    const uintptr_t catalog = reinterpret_cast<uintptr_t>(catalogBytes.data());
    const uint16_t numbers[]{1645,1726,4745,3757,325,1645};
    for (unsigned slot = 0; slot < 6; ++slot) {
        SetNativeLeader(slot,numbers[slot],900,slot);
        const uintptr_t address = Impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        const uint32_t index = numbers[slot] == 1645 ? 14600 : 14600 + slot;
        const uintptr_t parameter = catalog + index * PHBattleCatalogStride;
        Put<uint32_t>(catalog + numbers[slot] * PHBattleCatalogStride + PHOrbLeaderCatalogNextOffset,index);
        Put<uint32_t>(parameter,1000000u + numbers[slot]);
        Put<uint32_t>(parameter + PHOrbLeaderSkillOffset,900);
        Put<uint32_t>(address + PHBattleCatalogIDOffset,1000000u + numbers[slot]);
        Put<uintptr_t>(address + PHBattleCardParamOffset,parameter);
        Put<uint32_t>(parameter + 84,slot == 5 ? 1000 : 1000 + slot);
        // The ordinary catalog entry intentionally has another active skill.
        Put<uint32_t>(catalog + numbers[slot] * PHBattleCatalogStride + 84,2000 + slot);
        Put<uint16_t>(address + 1298,1); ObservationSetCharge(slot,20);
    }
    Put<uint32_t>(Header + PHSaveRVA + PHOrbLeaderCatalogRecordsOffset,14610);
    SetSkillRecord(1000,141,{6,17,17}); ObservationSetCooldown(1000,1,6,0);
    SetSkillRecord(1001,20,{1,0,5,4}); ObservationSetCooldown(1001,1,4,0);
    SetSkillRecord(1002,20,{3,0,2,4}); ObservationSetCooldown(1002,1,4,0);
    SetSkillRecord(1003,71,{0,4,5,-1,-1,-1,-1,-1}); ObservationSetCooldown(1003,1,6,0);
    SetSkillRecord(1004,999,{}); ObservationSetCooldown(1004,1,15,0);
    SetSkillRecord(900,138,{901,902,903}); SetSkillRecord(901,163,{});
    SetSkillRecord(902,182,{17,6,1000,0}); SetSkillRecord(903,219,{17,12,1});
    readinessFixture = {1001,0,4,4};
    Put<uint32_t>(Impl + PHOrbEightReceiptSkillOffset,UINT32_MAX);
    Put<uint16_t>(Impl + PHOrbEightReceiptSlotOffset,UINT16_MAX);
    Put<uint8_t>(Impl + PHOrbSkillSelectedSourceOffset,1); // Stale source without a receipt is not a cast.
}
static void EightNativeTests() {
    for (bool eight : {false,true}) for (uint32_t terminal : {28u,29u,30u,31u,67u,68u}) {
        if (eight) EightNativeFixture(7); else SkillFixture();
        FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{};
        Check([engine read:&state frame:Frame() reason:nil],"terminal native fixture reads its initial board");
        if (eight) Check([engine armEightPlayerAuto:&state],"terminal native fixture arms an explicit match");
        Protected(Impl + PHBattlePhaseOffset,terminal,0x766cfc6d);
        // Retired actor/skill storage must not hide valid clear metadata.
        Put<uintptr_t>(Impl + PHBattleMemberOffset + PHBattleCardParamOffset,0);
        Put<uintptr_t>(Impl + PHOrbTableOffset,0);
        NSString *reason = nil;
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && PHOrbBattleEnded(&state) &&
            !state.automation.valid && !state.automation.window && !state.eight.valid &&
            !PHOrbCanPreview(&state) && ![engine armEightPlayerAuto:&state] &&
            [reason containsString:@"結算"] && !writes && !readinessCalls && !canUseCalls && !skillStartCalls,
            "native clear reader confirms phase/identity without consulting retired actors or calling native actions");
    }
    for (unsigned seat = 0; seat < 8; ++seat) {
        EightNativeFixture(seat); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{}, after{}; NSString *reason = nil;
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !state.eight.valid && !state.automation.valid,
            "8P ordinary read has no skill authority without a match Auto click");
        Check([engine armEightPlayerAuto:&state] && [engine readAutomation:&state frame:Frame() reason:&reason] &&
            state.eight.valid && state.automation.valid && state.automation.window && state.enemyCombo.valid,
            "8P explicit match reader verifies all six fixed variant records and local bank");
        if (!state.eight.valid) std::fprintf(stderr,"8P native read: %s\n",reason.UTF8String);
        Check(state.eight.members[0].generate[0] == 6 && state.eight.members[0].generate[4] == 6 &&
            state.eight.members[1].activeSkill == 1001 && state.eight.members[1].catalogID == 1001726 &&
            state.eight.members[1].convert[5] == 4 && state.eight.members[3].boardColors == 49 &&
            state.eight.members[0].addedCombos == 1 && state.eight.members[5].noSkyfall &&
            !state.eight.members[4].skillKnown && !writes && !canUseCalls && !readinessCalls,
            "fixed semantic reads follow in-match variants, not ordinary catalog skills, and independently identify slots");
        Check([engine useSkill:1 request:PHOrbSkillRequest{PHOrbSkillBody,1001} expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
            skillStartCalls == 1 && canUseCalls == 1 && !writes,
            "8P uses normal native can-use/start with a current fixed skill constraint");
        Check([engine readAutomation:&after frame:Frame() reason:&reason] && after.phase == 22 && after.automation.members[1].state == 2,
            "8P skill animation is readable without an input-window leader pair");
        [engine armEightPlayerAuto:nullptr];
        Check([engine readAutomation:&after frame:Frame() reason:&reason] && !after.eight.valid && !after.automation.valid,
            "revoking Auto stops automation reads and further skill authorization");
        Put<uint32_t>(Impl + PHOrbEightReceiptSkillOffset,1001);
        Put<uint32_t>(Impl + PHOrbEightReceiptCatalogOffset,state.eight.members[1].catalogID);
        Put<uint16_t>(Impl + PHOrbEightReceiptSlotOffset,1);
        Put<uint8_t>(Impl + PHOrbSkillSelectedSourceOffset,0);
        Protected(Impl + PHOrbSkillUsesOffset,state.automation.skillUses + 1,0x6a875a8b);
        Check([engine readEightProgress:&after expected:&state frame:Frame() reason:&reason] &&
            after.automation.valid && !after.automation.window && after.eightReceipt.valid &&
            after.automation.members[1].skillUses[0] == state.automation.members[1].skillUses[0] &&
            PHOrbEightSkillAcknowledged(&state,&after,1),
            "paused native reader confirms root, owner, visible slot and total with unchanged limited-use count");
        Check([engine useSkill:1 request:PHOrbSkillRequest{PHOrbSkillBody,1001} expected:&after frame:Frame() reason:nil] == PHOrbSkillRejected &&
            [engine readAutomation:&after frame:Frame() reason:nil] && !after.eight.valid &&
            skillStartCalls == 1 && canUseCalls == 1 && !writes,
            "read-only pending-skill observation never re-arms native skill or gesture authority");
    }
    for (unsigned change = 0; change < 5; ++change) {
        EightNativeFixture(7); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot before{}, after{};
        [engine read:&before frame:Frame() reason:nil]; [engine armEightPlayerAuto:&before];
        [engine readAutomation:&before frame:Frame() reason:nil]; [engine armEightPlayerAuto:nullptr];
        if (change == 0) changeEightReceiptDuringRead = true;
        if (change == 1) rejectEightReceiptRead = true;
        if (change == 2) Put<uint64_t>(Impl + PHOrbMultiMatchClockOffset,1);
        if (change == 3) Protected(Impl + PHBattlePhaseOffset,68,0x766cfc6d);
        PHBattleFrame frame = Frame(); if (change == 4) frame.enabled = false;
        const BOOL read = [engine readEightProgress:&after expected:&before frame:frame reason:nil];
        Check((change < 2 ? !read : read) && !after.eight.valid && !after.automation.valid &&
            !writes && !readinessCalls && !canUseCalls && !skillStartCalls,
            "paused receipt race/read failure rejects evidence; new match, results or disabled feature has metadata only");
    }
    for (unsigned change = 0; change < 7; ++change) {
        EightNativeFixture(6); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot before{};
        [engine read:&before frame:Frame() reason:nil]; [engine armEightPlayerAuto:&before];
        Check([engine readAutomation:&before frame:Frame() reason:nil] && before.eight.valid,"capture 8P native state before a race");
        const uintptr_t member = Impl + PHBattleMemberOffset + PHBattleMemberStride;
        if (change == 0) Put<uint8_t>(Header + PHBattleEntryHashRVA,2);
        if (change == 1) Put<uint64_t>(Impl + PHOrbMultiMatchClockOffset,1);
        if (change == 2) Put<uint32_t>(Impl + PHBattleCurrentTeamOffset,0);
        if (change == 3) Protected(member + PHBattleSourceSlotOffset,0,0x6a875a8b);
        if (change == 4) Put<uint32_t>(before.eight.members[1].parameter,42);
        if (change == 5) SetSkillRecord(1001,20,{1,0,5,3});
        if (change == 6) Protected(Impl + PHOrbEnemyOffset + PHOrbEnemyComboDurationOffset,2,0x6a875a8b);
        PHOrbSkillResult result = [engine useSkill:1 request:PHOrbSkillRequest{PHOrbSkillBody,1001} expected:&before frame:Frame() reason:nil];
        Check(result == PHOrbSkillRejected && !skillStartCalls && !canUseCalls && !writes,
            "entry, clock, view, duplicate source, parameter, semantic or absorption race rejects a stale 8P skill");
    }
    EightNativeFixture(5); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{};
    [engine read:&state frame:Frame() reason:nil]; [engine armEightPlayerAuto:&state]; [engine readAutomation:&state frame:Frame() reason:nil];
    [engine armMultiplayerOnce:YES];
    Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil],
        "8P Auto gesture checks fixed team and enemy evidence after coordinate conversion");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset,state.board.tokens[0]); [engine finishInput:nil];
    [engine readAutomation:&state frame:Frame() reason:nil];
    [engine armEightPlayerAuto:nullptr];
    Check(![engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:nil] &&
        [engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:nil],
        "Stop revokes 8P holds but retains release of the same owned orb");
    NativeInputFrame(); [engine finishInput:nil];
}
