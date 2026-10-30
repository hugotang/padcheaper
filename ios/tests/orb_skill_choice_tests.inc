static void SameFamilySkillFixture(unsigned slot = 0, uint16_t weapon = 14039) {
    SkillFixture();
    SetLeader(slot, 14038);
    const uintptr_t member = Impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    uint16_t body = 0, assist = 0;
    PHBattleCatalogIDForNumber(14037, &body); PHBattleCatalogIDForNumber(weapon, &assist);
    const uintptr_t catalog = reinterpret_cast<uintptr_t>(catalogBytes.data());
    Put<uint32_t>(catalog + body * PHBattleCatalogStride, body);
    Put<uint32_t>(catalog + assist * PHBattleCatalogStride, assist);
    Put<uint32_t>(catalog + body * PHBattleCatalogStride + PHOrbSkillFamilyOffset, 14037);
    Put<uint32_t>(catalog + assist * PHBattleCatalogStride + PHOrbSkillFamilyOffset, weapon == 14039 ? 14037 : weapon);
    Put<uint16_t>(member + PHBattleBaseIDOffset, body);
    Put<uint16_t>(member + PHBattleAssistIDOffset, assist);
    Put<uint16_t>(member + 1438, assist);
    Protected(member + PHBattleChargeOffset, 18, 0x6a875a8b);
    Put<uint32_t>(member + PHBattleChargeShadowOffset, 18u ^ 0x68fb65ceu);
    readinessFixture = {64787, 65021, 4, 18};
}

static void SkillChoiceNativeTests() {
    PHOrbSnapshot state{}; NSString *reason = nil;
    const PHOrbSkillRequest bodyRequest{PHOrbSkillBody, 64787};
    for (unsigned slot : {0u, 5u}) for (bool defaultAvailable : {false, true}) {
        SameFamilySkillFixture(slot);
        skillAvailable = defaultAvailable;
        FixtureSkillEngine *engine = [FixtureSkillEngine new];
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !choiceCalls && !readinessCalls && !canUseCalls,
            "same-family source metadata remains read-only during polling");
        const auto before = implBytes;
        Check([engine useSkill:slot request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
            choiceCalls == 1 && bodyCanUseCalls == 1 && skillStartCalls == 1 && writes == 1 && sourceAtStart == 0 &&
            selectionAtStart == 64787 && effectiveStartedSkill == 64787 && effectsStartedSkill == 64787,
            "charged same-family assist permits current body, including when default assist is refused, through effects");
        for (unsigned memberIndex = 0; memberIndex < 6; ++memberIndex) {
            const size_t offset = PHBattleMemberOffset + memberIndex * PHBattleMemberStride;
            Check(!std::memcmp(before.data() + offset + PHBattleChargeOffset, implBytes.data() + offset + PHBattleChargeOffset, 20) &&
                !std::memcmp(before.data() + offset + PHBattleChargeShadowOffset, implBytes.data() + offset + PHBattleChargeShadowOffset, 4) &&
                !std::memcmp(before.data() + offset + 488, implBytes.data() + offset + 488, 32),
                "body selection leaves every member's protected charge, charge mirror and use counts unchanged");
        }
    }
    for (uint32_t retained : {0u, 65021u, 64787u, 65778u}) {
        SameFamilySkillFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
        Put<uint32_t>(Impl + PHBattleMemberOffset + PHOrbSkillOverrideOffset, retained);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
            effectiveStartedSkill == 64787 && effectsStartedSkill == 64787 && writes == unsigned(retained != 64787),
            "a stale positive choice is replaced by the current native body ID; the correct existing choice needs no write");
    }
    SameFamilySkillFixture(); bodyAvailable = false;
    FixtureSkillEngine *engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        bodyCanUseCalls == 1 && !skillStartCalls && !writes,
        "body refusal never falls back to the available assist or forces activation");

    SameFamilySkillFixture(5, 13654); engine = [FixtureSkillEngine new];
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:5 request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        [reason containsString:@"不同進化鏈"] && !bodyCanUseCalls && !skillStartCalls && !writes,
        "unrelated helper assist cannot be bypassed and is not discharged by a body rule");
    SameFamilySkillFixture(); choiceAvailable = false; engine = [FixtureSkillEngine new];
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        !bodyCanUseCalls && !skillStartCalls && !writes, "same metadata cannot replace the native dialog's refusal");

    for (PHOrbSkillSource source : {PHOrbSkillAutomatic, PHOrbSkillAssist}) {
        SameFamilySkillFixture(); engine = [FixtureSkillEngine new];
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Check([engine useSkill:0 request:PHOrbSkillRequest{source, 65021} expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
            !choiceCalls && !bodyCanUseCalls && !writes && sourceAtStart == 1 &&
            effectiveStartedSkill == 65021 && effectsStartedSkill == 65021,
            "automatic and explicit assist retain the fully charged native assist choice");
    }
    SameFamilySkillFixture(); engine = [FixtureSkillEngine new];
    Protected(Impl + PHBattleMemberOffset + PHBattleChargeOffset, 4, 0x6a875a8b);
    Put<uint32_t>(Impl + PHBattleMemberOffset + PHBattleChargeShadowOffset, 4u ^ 0x68fb65ceu);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillAssist, 65021} expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        !choiceCalls && !bodyCanUseCalls && !skillStartCalls && !writes,
        "same-family choice never permits an undercharged assist");
    for (PHOrbSkillSource source : {PHOrbSkillBody, PHOrbSkillAssist}) {
        SameFamilySkillFixture(); engine = [FixtureSkillEngine new]; Put<uint8_t>(SurvivalDungeon + 33, 8);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        const bool body = source == PHOrbSkillBody;
        Check([engine useSkill:0 request:PHOrbSkillRequest{source, body ? 64787u : 65021u} expected:&state frame:Frame() reason:&reason] ==
            (body ? PHOrbSkillStarted : PHOrbSkillUnavailable) && skillStartCalls == unsigned(body) && !choiceCalls && !writes,
            "dungeon-disabled assists leave the naturally available body and cannot be explicitly selected");
    }
    SameFamilySkillFixture(); engine = [FixtureSkillEngine new]; readinessFixture.baseSkill = 65778;
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        !bodyCanUseCalls && !skillStartCalls && !writes, "wrong requested body ID is not written into the game");
    Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillBody, 65778} expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
        effectiveStartedSkill == 65778 && effectsStartedSkill == 65778, "current evolving body IDs retain all 32 bits");

    for (unsigned fault = 0; fault < 11; ++fault) {
        SameFamilySkillFixture(); engine = [FixtureSkillEngine new];
        [engine readAutomation:&state frame:Frame() reason:&reason];
        if (fault == 0) switchMultiInChoice = true;
        if (fault == 1) changeFamilyInChoice = true;
        if (fault == 2) changeSourceCardsInChoice = true;
        if (fault == 3) changeBodySkillInReadiness = true;
        if (fault == 4) invalidSourceInBody = true;
        if (fault == 5) changeAssistRulesInBody = true;
        if (fault == 6) failWrite = 1;
        if (fault == 7) ignoreSelectionWrite = true;
        if (fault == 8) switchMultiInSelectionWrite = true;
        if (fault == 9) changeSourceInSelectionWrite = true;
        if (fault == 10) Put<uint16_t>(Impl + PHBattleMemberOffset + PHBattleBaseIDOffset, 0);
        Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
            !skillStartCalls && (fault < 6 || fault == 10 ? !writes : writes == 1),
            "family, source, skill, dungeon, write and multiplayer races all stop before native activation");
    }
    SameFamilySkillFixture(); engine = [FixtureSkillEngine new];
    uint16_t temporary = 0; PHBattleCatalogIDForNumber(4264, &temporary);
    SetLeader(0, 4264); Protected(Impl + PHBattleMemberOffset + PHBattleTempIDOffset, temporary, 0x6a875a8b);
    Protected(Impl + PHBattleMemberOffset + PHBattleTempTurnsOffset, 2, 0x6a875a8b);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] != PHOrbSkillStarted &&
        !choiceCalls && !bodyCanUseCalls && !writes, "temporary transformation cannot use the underlying family's body choice");
    SameFamilySkillFixture(); engine = [FixtureSkillEngine new];
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
        !canUseCalls && !choiceCalls && !readinessCalls && !skillStartCalls && !writes,
        "multiplayer exclusion applies before every new choice call or write");

    for (unsigned light : {8u, 9u}) {
        SameFamilySkillFixture(); engine = [FixtureSkillEngine new]; checkOrbCountInCanUse = true;
        SetSkillRecord(64787, 116, {64788}); SetSkillRecord(64788, 255, {8, 9});
        for (unsigned row = 0; row < 5; ++row) for (unsigned col = 0; col < 6; ++col) {
            auto token = Load<uintptr_t>(reinterpret_cast<uint8_t *>(Impl + PHOrbTableOffset + (row * 16 + col) * 8));
            Put<uint8_t>(token, row * 6 + col < light ? 3 : 5);
        }
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Check([engine useSkill:0 request:bodyRequest expected:&state frame:Frame() reason:&reason] ==
            (light == 9 ? PHOrbSkillStarted : PHOrbSkillUnavailable) && skillStartCalls == unsigned(light == 9) &&
            (light == 9 || ([reason containsString:@"本體使用條件未滿足"] && [reason containsString:@"光珠至少 9 顆，目前 8 顆"])),
            "overcharged Uryu still requires nine light, and refusal diagnoses body rather than the default assist");
    }
}
