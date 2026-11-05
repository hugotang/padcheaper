static void SharedMemberStateNativeTests() {
    for (bool temporary : {false, true}) {
        SkillFixture();
        const uintptr_t address = Impl + PHBattleMemberOffset;
        SetLeader(0, temporary ? 401 : 201);
        Put<uint16_t>(address + PHBattleBaseIDOffset, 101);
        Put<uint16_t>(address + PHBattleAssistIDOffset, 301);
        Put<uint16_t>(address + PHBattleEffectiveAssistOffset, 0);
        Protected(address + PHBattleFormOffset, 201, 0x6a875a8b);
        Protected(address + PHBattleTempIDOffset, temporary ? 401 : 0, 0x6a875a8b);
        Protected(address + PHBattleTempTurnsOffset, temporary ? 3 : 0, 0x6a875a8b);
        Protected(address + PHBattleAssistStateOffset, 172, 0x6a875a8b);
        Put<uint8_t>(SurvivalDungeon + 33, 8);
        FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{};
        Check([engine readAutomation:&state frame:Frame() reason:nil] && state.automation.valid,
              "shared member decoder feeds the production skill snapshot");
        const auto &member = state.automation.members[0];
        Check(member.number == (temporary ? 401 : 201) && member.sourceCatalogIDs[0] == 101 &&
              member.sourceCatalogIDs[1] == 301 && !member.assistCatalogID && member.temporaryKnown &&
              member.temporaryTurns == (temporary ? 3 : 0) && member.identityNumber == 201 &&
              member.identityAssist == (temporary ? 301 : 0),
              "current skill source stays separate from underlying plan identity and configured assist");
        Check(!canUseCalls && !readinessCalls && !skillStartCalls && !writes,
              "shared identity and dungeon readers remain read-only");
        if (temporary) {
            Protected(address + PHBattleTempIDOffset, 402, 0x6a875a8b);
            Check(![engine readAutomation:&state frame:Frame() reason:nil],
                  "temporary ID mismatch still fails the skill reader after shared decoding");
        } else {
            Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillBody, CurrentFixtureSkill}
                expected:&state frame:Frame() reason:nil] == PHOrbSkillStarted && sourceAtStart == 0 &&
                effectsStartedSkill == CurrentFixtureSkill,
                "assist-disabled dungeon still activates the current body skill through native gates");
        }
    }
    SkillFixture();
    const uintptr_t address = Impl + PHBattleMemberOffset;
    Put<uint16_t>(address + PHBattleAssistIDOffset, 301);
    Put<uint16_t>(address + PHBattleEffectiveAssistOffset, 301);
    Protected(address + PHBattleAssistStateOffset, 172, 0x6a875a8b);
    Put<uint8_t>(SurvivalDungeon + 33, 8);
    readinessFixture = {CurrentFixtureSkill, 99, 4, 5};
    FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil], "stale positive effective assist remains observable");
    Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillAssist, 99} expected:&state frame:Frame() reason:nil] ==
          PHOrbSkillUnavailable && !skillStartCalls && !writes,
          "known dungeon ban prevents assist activation even while a descriptor is stale");
}
