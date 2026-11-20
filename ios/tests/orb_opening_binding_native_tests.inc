// Written for the next explicitly requested build; synthetic IDs are not
// evidence of Magnard's actual post-transform skill or its native effects.
static void OpeningBindingNativeTests() {
    ObservationFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    PHOrbSnapshot state{}; uint32_t skill = 0;
    Check([engine readAutomation:&state frame:Frame() reason:nil], "native opening binding fixture is readable");
    const auto originalGuest = guest, originalImpl = implBytes, originalCatalog = catalogBytes;
    Check([engine readOpeningBodySkill:0 skill:&skill expected:&state frame:Frame() reason:nil] && skill == 1000 &&
        !canUseCalls && !readinessCalls && !skillStartCalls && !writes &&
        guest == originalGuest && implBytes == originalImpl && catalogBytes == originalCatalog,
        "opening binding reads the exact current body root twice without getters, native calls or writes");
    readinessFixture = {1000,1001,5,21};
    Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillBody,skill} expected:&state frame:Frame() reason:nil] == PHOrbSkillStarted &&
        skillStartCalls == 1 && effectiveStartedSkill == 1000 && sourceAtStart == 0,
        "a bound opening ID goes through the existing native body-only execution path");

    for (unsigned scenario = 0; scenario < 14; ++scenario) {
        ObservationFixture(); engine = [FixtureSkillEngine new]; skill = 123;
        Check([engine readAutomation:&state frame:Frame() reason:nil], "invalid-binding fixture base read");
        const uintptr_t member = Impl + PHBattleMemberOffset;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(member + PHBattleCardParamOffset));
        if (scenario == 0) Put<uint32_t>(parameter + 84, 0);
        if (scenario == 1) Put<uint32_t>(parameter + 84, 0x20000);
        if (scenario == 2) SetSkillRecord(1000,232,{1001});
        if (scenario == 3) SetSkillRecord(1000,233,{1001});
        if (scenario == 4) {
            Protected(member + 344,1,0x6a875a8b); Put<uint8_t>(member + 1290,1);
            [engine readAutomation:&state frame:Frame() reason:nil];
        }
        if (scenario == 5) Put<uint8_t>(member + 1290,1);
        if (scenario == 6) Put<uintptr_t>(member + PHBattleCardParamOffset,0);
        if (scenario == 7) Put<uint32_t>(Header + PHSaveRVA + PHOrbSkillCountOffset,1000);
        if (scenario == 8) ++state.automation.members[0].number;
        if (scenario == 9) Put<uint32_t>(Header + PHBattleMultiplayerRVA,1);
        if (scenario == 10) Put<uint32_t>(Impl + 601584,1);
        if (scenario == 11) {
            const uintptr_t code = Header + PHOrbObservationTextGuards[0].rva;
            Put<uint8_t>(code,Load<uint8_t>(reinterpret_cast<uint8_t *>(code)) ^ 1u);
        }
        if (scenario == 12) changeOpeningRootOnRead = parameter + 84;
        if (scenario == 13) Put<uintptr_t>(Header + PHSaveRVA + PHBattleSkillPointerOffset,0);
        state.observation.status = PHOrbObservationReady;
        state.observation.members[0].skillKnown[0] = true;
        state.observation.members[0].skillIDs[0] = 1000;
        Check(![engine readOpeningBodySkill:0 skill:&skill expected:&state frame:Frame() reason:nil] && !skill &&
            !canUseCalls && !readinessCalls && !skillStartCalls && !writes,
            "unknown/evolving/stale roots, changed actors/windows/code and mid-read mutations cannot bind, even with cached diagnostics");
    }
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        ObservationFixture(); engine = [FixtureSkillEngine new];
        [engine readAutomation:&state frame:Frame() reason:nil];
        Check([engine readOpeningBodySkill:0 skill:&skill expected:&state frame:Frame() reason:nil], "bind before native refusal");
        // Changed native ID, or an overcharged unrelated assist, must not cast.
        readinessFixture = scenario ? NativeSkillReadiness{1000,1001,5,20} : NativeSkillReadiness{1001,0,5,21};
        Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillBody,skill} expected:&state frame:Frame() reason:nil] == PHOrbSkillUnavailable &&
            !skillStartCalls && !writes,
            "bound opening never substitutes a changed native skill or an unrelated overcharged assist");
    }
}
