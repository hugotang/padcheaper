// Source-only: fixtures do not execute game code or simulate native L credits.
static PHOrbSnapshot CaptureAssist(FixtureSkillEngine *engine) {
    PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil], "assist fixture has a valid ordinary read");
    Check([engine readAssistRecovery:&state frame:Frame() reason:nil], "action-specific assist capture succeeds");
    return state;
}
static void AssistNativeTests() {
    ObservationFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    const auto originalGuest = guest, originalImpl = implBytes, originalCatalog = catalogBytes;
    PHOrbSnapshot state = CaptureAssist(engine);
    Check(state.assistRecovery.valid && state.assistRecovery.turns == 3 && state.assistRecovery.fresh &&
        state.assistRecovery.perColor[3] == 2 && state.assistRecovery.perColor[4] == 2 &&
        state.assistRecovery.providers[3] == 1 && state.assistRecovery.latents[0] == 1 &&
        state.assistRecovery.retainedAssists == 1,
        "one L+ with latent 48 supplies two turns for each distinct current attack color");
    Check(guest == originalGuest && implBytes == originalImpl && catalogBytes == originalCatalog &&
        !writes && !canUseCalls && !readinessCalls && !choiceCalls && !skillStartCalls,
        "action capture reads only and never initializes native cooldown caches or invokes a getter");
    state.observation.status = PHOrbObservationReady; state.observation.assistVoidTurns = 99;
    Check([engine readAssistRecovery:&state frame:Frame() reason:nil] && state.assistRecovery.turns == 3,
        "a fresh action capture never borrows the optional observation cache");

    for (unsigned scenario = 0; scenario < 15; ++scenario) {
        ObservationFixture(); engine = [FixtureSkillEngine new];
        const uintptr_t member = Impl + PHBattleMemberOffset;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(member + PHBattleCardParamOffset));
        if (scenario == 0) Put<uint8_t>(parameter + 216, 3);
        if (scenario == 1) { Put<int8_t>(member + 1287, 0); Put<int8_t>(member + 1288, 2); }
        if (scenario == 2) {
            const uintptr_t second = member + PHBattleMemberStride;
            const uintptr_t row = Load<uintptr_t>(reinterpret_cast<uint8_t *>(second + PHBattleCardParamOffset));
            Put<uintptr_t>(second + PHOrbObservationProfileOffset, Header + 0x1900000);
            Put<uint8_t>(second + 1286, 1); Put<uint16_t>(row + 748, 60);
        }
        if (scenario == 3) Protected(member + PHOrbSkillBindOffset, 1, 0x6a875a8b);
        if (scenario == 4) Put<uint16_t>(Impl + PHBattleAwakeningStateOffset, 3);
        if (scenario == 5) Put<uint8_t>(SurvivalDungeon + 33, 4);
        if (scenario == 6) Put<uint8_t>(SurvivalDungeon + 33, 0x20);
        if (scenario == 7) Put<uintptr_t>(member + PHOrbObservationProfileOffset, 0);
        if (scenario == 8) Put<uint8_t>(member + 1286, 0);
        if (scenario == 9) Put<uint8_t>(Impl + PHOrbObservationAwakeningGrantsOffset + 4, 60);
        if (scenario == 10) Put<uint8_t>(member + 1444, 1);
        if (scenario == 11) Put<uint32_t>(member + PHOrbObservationAddedAwakeningsOffset, 0);
        if (scenario == 12) Put<uint8_t>(parameter + 12, 5);
        if (scenario == 13) {
            const uintptr_t equip = reinterpret_cast<uintptr_t>(catalogBytes.data()) + 300 * PHBattleCatalogStride;
            Put<uint16_t>(equip + 748, 49); Put<uint16_t>(equip + 750, 108);
            Put<uint8_t>(member + 1286, 0); Put<uint8_t>(member + 1440, 2); Put<uint16_t>(member + 1438, 0);
        }
        if (scenario == 14) Put<uint8_t>(member, 3); // Skill active bit is insufficient for the L consumer.
        state = CaptureAssist(engine); const auto &evidence = state.assistRecovery;
        if (scenario == 0) Check(evidence.perColor[3] == 2 && evidence.perColor[4] == 2,
            "duplicate main/sub/third attributes credit a provider only once per color");
        else if (scenario == 1) Check(evidence.perColor[0] == 2 && evidence.perColor[2] == 2 && !evidence.perColor[3],
            "current attribute overrides replace base colors without losing an explicit subattribute");
        else if (scenario == 2) Check(evidence.perColor[3] == 3 && evidence.providers[3] == 3 && evidence.perColor[4] == 2,
            "independent eligible members add their effective L counts");
        else Check(!evidence.perColor[3] && evidence.blockers[0] && !evidence.providers[3],
            "disabled, absent, unsupported or assist-only awakenings never provide recovery credit");
        if (scenario >= 9 && scenario <= 12) Check(evidence.unknownMembers & 1,
            "unsupported grants, variant assists and malformed awakening/attribute reads remain explicit unknowns");
        if (scenario == 13) Check(evidence.retainedAssists == 1 && evidence.sourceAssists[0] == 300 && !evidence.activeAssists[0],
            "voided equipment retains its exact source identity without lending its suppressed L awakenings");
        if (scenario == 14) Check(!evidence.retainedAssists && (evidence.blockers[0] & PHOrbLInactive),
            "native active==1 rule also gates retained-equipment evidence");
        Check(!writes && !canUseCalls && !readinessCalls && !skillStartCalls, "all coverage gaps remain read-only");
    }
    for (unsigned mutation = 1; mutation <= 7; ++mutation) {
        ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureAssist(engine);
        observationMutation = mutation;
        Check(![engine readAssistRecovery:&state frame:Frame() reason:nil] && !state.assistRecovery.valid && !writes,
            "duration, entry, key, drag, table, profile or queue races revoke action evidence");
    }
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureAssist(engine);
        if (scenario == 0) Put<uint32_t>(Impl + PHOrbAssistRecoveryQueueCountOffset, 1);
        if (scenario == 1) Put<uintptr_t>(Impl + PHOrbAssistRecoveryQueueOffset, Impl + 22480);
        if (scenario == 2) Put<uint32_t>(Impl + PHOrbAssistRecoveryDispatchOffset, 525190);
        if (scenario == 3) Put<uint16_t>(Impl + PHOrbObservationAssistVoidOffset, 0x3ff);
        if (scenario == 4) state.multiplayer.kind = PHOrbMultiplayerFour;
        if (scenario == 5) state.multiplayer.kind = PHOrbMultiplayerThreeSolo;
        if (scenario == 6) state.boardTransition = true;
        if (scenario == 7) state.phase = 22;
        Check(![engine readAssistRecovery:&state frame:Frame() reason:nil] && !state.assistRecovery.valid && !writes,
            "pending native effects, unsupported dispatch, negative duration, multiplayer and transitions cannot authorize L policy");
    }
    ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureAssist(engine);
    Put<uint8_t>(Header + PHOrbAssistRecoveryTextGuards[0].rva,
        uint8_t(PHOrbAssistRecoveryTextGuards[0].bytes[0] ^ 1u));
    Check(![engine readAssistRecovery:&state frame:Frame() reason:nil] &&
        [engine readAutomation:&state frame:Frame() reason:nil] && state.automation.valid,
        "policy fingerprint mismatch does not broaden the ordinary read gate");

    ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureAssist(engine);
    NSString *inputReason = nil;
    changeAssistDuringCoordinateRead = true;
    Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&inputReason] &&
        !changeAssistDuringCoordinateRead && !writes && [inputReason containsString:@"武裝無效"] &&
        [inputReason containsString:@"未繼續輸入"],
        "duration changing during coordinate conversion blocks the very first input write");
    ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureAssist(engine);
    const auto preservedInput = realInput;
    Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil] && GlobalInput() != Input,
        "unchanged action evidence permits the first native input lease");
    NativeInputFrame();
    Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
    Check([engine finishInput:nil] && GlobalInput() == Input && realInput == preservedInput,
        "an assist-policy start restores every byte of real input");
    Check([engine readAutomation:&state frame:Frame() reason:nil] &&
        [engine readAssistRecovery:&state frame:Frame() reason:nil] && state.assistRecovery.valid,
        "owned held-orb snapshots can obtain fresh mechanism evidence");
    Check([engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:nil] && GlobalInput() != Input,
        "unchanged held evidence permits a continued hold without repeating began");
    NativeInputFrame();
    Check([engine finishInput:nil] && GlobalInput() == Input && realInput == preservedInput,
        "an assist-policy hold restores every byte of real input");
    const unsigned completedWrites = writes;
    changeAssistDuringCoordinateRead = true; inputReason = nil;
    Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&inputReason] &&
        !changeAssistDuringCoordinateRead && writes == completedWrites && [inputReason containsString:@"武裝無效"],
        "a stale held route never emits another hold");
    Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:nil],
        "cleanup release remains available after mechanism evidence changed");
    NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
    Check([engine finishInput:nil] && GlobalInput() == Input && realInput == preservedInput,
        "cleanup release also restores every byte of real input");
    Reset();
}
