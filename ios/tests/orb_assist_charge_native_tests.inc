// Regression fixtures for JP 23.6.1's assist-loss clamp, not game execution.
static void AssistChargeFixture() {
    ObservationFixture();
    const uintptr_t member = Impl + PHBattleMemberOffset;
    // ObservationFixture started with charge/shadow 20 and body CD 5.
    // Native 0x10056A118 changes only the protected charge.
    Protected(member + PHBattleChargeOffset, 5, 0x6a875a8b);
    Put<uint16_t>(member + PHBattleEffectiveAssistOffset, 0);
    readinessFixture = {1000, 1001, 5, 21};
}

static void AssistChargeNativeTests() {
    const uintptr_t member = Impl + PHBattleMemberOffset;
    for (unsigned phase : {8u, 2u}) for (bool rebuilt : {false, true}) {
        AssistChargeFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
        Protected(Impl + PHBattlePhaseOffset, phase, 0x766cfc6d);
        if (!rebuilt) Put<uint16_t>(member + PHBattleEffectiveAssistOffset, 300);
        const auto beforeGuest = guest, beforeImpl = implBytes, beforeCatalog = catalogBytes;
        PHOrbSnapshot state{};
        Check([engine readAutomation:&state frame:Frame() reason:nil] && state.automation.valid &&
            state.automation.members[0].charge == 5 && state.automation.members[0].sourceCatalogIDs[1] == 300 &&
            state.automation.members[0].assistCatalogID == (rebuilt ? 0 : 300),
            "assist clamp remains readable during phase 8 and phase 2 before/after effective descriptor rebuild");
        if (phase == 2 && rebuilt) Check([engine readAssistRecovery:&state frame:Frame() reason:nil] &&
            state.assistRecovery.valid && state.assistRecovery.turns == 3 && state.assistRecovery.retainedAssists == 1 &&
            state.assistRecovery.perColor[3] == 2 && state.assistRecovery.sourceAssists[0] == 300 &&
            !state.assistRecovery.activeAssists[0],
            "fresh double-read L evidence survives the native assist clamp and uses only current awakenings");
        Check(guest == beforeGuest && implBytes == beforeImpl && catalogBytes == beforeCatalog &&
            !writes && !canUseCalls && !readinessCalls && !skillStartCalls,
            "assist clamp observations leave charge, shadow and all guest memory untouched without getters");
    }

    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        AssistChargeFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
        PHOrbSnapshot state{}; NSString *reason = nil;
        if (scenario == 1) skillAvailable = false;
        if (scenario == 2) Protected(member + PHBattleChargeOffset, 2, 0x6a875a8b);
        if (scenario == 3) {
            // Restoring the assist does not resync its old bookkeeping copy.
            Put<uint16_t>(Impl + PHOrbObservationAssistVoidOffset, 0);
            Put<uint16_t>(member + PHBattleEffectiveAssistOffset, 300);
            Protected(member + PHBattleChargeOffset, 6, 0x6a875a8b);
            Put<uint32_t>(member + PHBattleChargeShadowOffset, 21u ^ 0x68fb65ceu);
        }
        Check([engine readAutomation:&state frame:Frame() reason:&reason], "clamped/restored member can be observed before a skill");
        const uint32_t charge = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHBattleChargeOffset + 12));
        const uint32_t shadow = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHBattleChargeShadowOffset));
        const auto result = [engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillBody,1000}
            expected:&state frame:Frame() reason:&reason];
        if (scenario == 1 || scenario == 2) Check(result == PHOrbSkillUnavailable && !skillStartCalls &&
            [reason containsString:scenario == 2 ? @"冷卻尚未完成" : @"遊戲仍判定技能不可用"],
            "a higher copy cannot override actual cooldown or native can-use refusal");
        else Check(result == PHOrbSkillStarted && skillStartCalls == 1 && !sourceAtStart && effectiveStartedSkill == 1000,
            "body skill can start after assist loss/restoration; a charged copy cannot choose the assist");
        Check(canUseCalls == 1 && readinessCalls == 1 && !writes &&
            Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHBattleChargeOffset + 12)) == charge &&
            Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHBattleChargeShadowOffset)) == shadow,
            "normal native skill gating never rewrites charge or its stale copy");
    }

    AssistChargeFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil], "clamp race fixture reads");
    changeChargeInCanUse = true;
    Check([engine useSkill:0 expected:&state frame:Frame() reason:nil] == PHOrbSkillRejected &&
        canUseCalls == 1 && !skillStartCalls && !writes,
        "a protected charge change within the allowed copy bound still invalidates the pending activation");

    for (uint32_t recorded : {0u, 4u, 10000u, UINT32_MAX}) {
        AssistChargeFixture(); engine = [FixtureSkillEngine new]; NSString *reason = nil;
        Put<uint32_t>(member + PHBattleChargeShadowOffset, recorded ^ 0x68fb65ceu);
        Check(![engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.valid &&
            [reason containsString:@"第 1 格蓄技資料"] && [reason containsString:@"目前 5"] &&
            [reason containsString:[NSString stringWithFormat:@"副本 %u", recorded]] && !writes && !canUseCalls,
            "lower or out-of-range charge copies fail with a member-specific value diagnostic");
    }
    AssistChargeFixture(); engine = [FixtureSkillEngine new];
    Put<uint32_t>(member + PHBattleChargeShadowOffset, 9999u ^ 0x68fb65ceu);
    Check([engine readAutomation:&state frame:Frame() reason:nil] && state.automation.members[0].charge == 5,
        "native copy saturation at 9999 remains a bound, never the usable charge");
    Put<uint32_t>(member + PHBattleChargeOffset, 0); NSString *reason = nil;
    Check(![engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.valid &&
        [reason containsString:@"第 1 格蓄技受保護資料"] && !writes && !canUseCalls,
        "a valid higher copy never masks corruption of any protected charge component");

    AssistChargeFixture(); engine = [FixtureSkillEngine new];
    Check([engine readAutomation:&state frame:Frame() reason:nil], "reused automation fixture reads");
    Put<uint32_t>(member + PHOrbSkillMemberStateOffset, 8);
    Check(!ReadAutomationFields(state, false, &reason) && !state.automation.valid &&
        [reason containsString:@"第 1 格隊員動作狀態"], "failed refresh clears previous validity and identifies the failing member field");
}
