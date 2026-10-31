static void SkillWindowNativeTests() {
    PHOrbSnapshot state{}, changed{}; NSString *reason = nil;
    for (unsigned gate = 0; gate < 8; ++gate) {
        SkillFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && state.automation.windowBlocksKnown &&
            !state.automation.windowBlocks && state.automation.window, "known open window has no block reasons");
        uint32_t expectedReason = 0;
        if (gate == 0) { Put<uint32_t>(Impl + 601584,1); expectedReason = PHOrbSkillWindowInput; }
        if (gate == 1) { Put<uint16_t>(Impl + PHOrbSkillSealOffset,1); expectedReason = PHOrbSkillWindowSeal; }
        if (gate == 2) {
            Protected(Impl + PHOrbSkillLimitOffset,0,0x766cfc6d);
            Protected(Impl + PHOrbSkillLimitRemainingOffset,0,0x6a875a8b);
            expectedReason = PHOrbSkillWindowLimit;
        }
        if (gate == 3) { Put<uint8_t>(Header + PHOrbTutorialTableRVA + 2,75); expectedReason = PHOrbSkillWindowTutorial; }
        if (gate == 4) { Put<uint8_t>(SurvivalDungeon + 32,0x40); expectedReason = PHOrbSkillWindowDungeon; }
        if (gate == 5) { Put<uint8_t>(Impl + PHOrbEnemyOffset + 23,0); expectedReason = PHOrbSkillWindowEnemyAction; }
        if (gate == 6) { Put<float>(Impl + PHBattleMemberOffset + 1116,1); expectedReason = PHOrbSkillWindowMemberAnimation; }
        if (gate == 7) { Put<uint16_t>(Impl + PHOrbSkillSpecialOffset,0); expectedReason = PHOrbSkillWindowSpecial; }
        Check([engine readAutomation:&changed frame:Frame() reason:&reason] && !changed.automation.window &&
            changed.automation.windowBlocksKnown && (changed.automation.windowBlocks & expectedReason) && !writes && !canUseCalls,
            "each original native gate reports a read-only reason without polling availability");
        Check([engine useSkill:0 expected:&changed frame:Frame() reason:&reason] == PHOrbSkillWindowClosed &&
            [reason containsString:@"技能窗口關閉"] && !canUseCalls && !readinessCalls && !skillStartCalls && !writes,
            "a captured closed window returns the no-native-call result for temporary and lasting restrictions");
    }
    SkillFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Put<uint32_t>(Impl + 601584,1);
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillWindowClosed &&
        !canUseCalls && !skillStartCalls && !writes,
        "a newly closed window immediately before native dispatch does not become a retained refusal");
    Put<uint32_t>(Impl + 601584,0);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
        canUseCalls == 1 && skillStartCalls == 1, "freshly reopened window uses the normal native availability gate");
    for (unsigned race = 0; race < 3; ++race) {
        SkillFixture(); engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
        Put<uint32_t>(Impl + 601584,1);
        if (race == 0) SetSurvivalHP(99,1000);
        if (race == 1) Put<uint32_t>(Impl + PHBattleMemberOffset + PHOrbSkillOverrideOffset,65063);
        if (race == 2) Put<uint32_t>(Header + PHBattleMultiplayerRVA,1);
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
            !canUseCalls && !skillStartCalls && !writes,
            "window-only normalization cannot hide changed HP, selection or multiplayer");
    }
}
