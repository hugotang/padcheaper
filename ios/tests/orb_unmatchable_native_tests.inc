static void UnmatchableNativeFixture() {
    ObservationFixture();
    const uintptr_t member=Impl+PHBattleMemberOffset;
    const uintptr_t parameter=Load<uintptr_t>(reinterpret_cast<uint8_t *>(member+PHBattleCardParamOffset));
    Put<uint16_t>(parameter+748,96); // TPA+ supplies two effective awakenings.
    Put<int8_t>(parameter+12,0); Put<int8_t>(parameter+204,2);
    uint64_t latent=0x6fu;
    for (unsigned i=0;i<6;++i) latent|=UINT64_C(41)<<(8+7*i);
    Put<uint64_t>(Header+0x1900000+4,latent);
    Put<uint16_t>(Impl+PHOrbUnmatchableDurationOffset,0x400|1);
    Protected(Impl+PHOrbUnmatchableColorsOffset,16,0x6a875a8b);
}
static PHOrbSnapshot CaptureUnmatchable(FixtureSkillEngine *engine) {
    PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil],"unmatchable fixture has a fresh ordinary read");
    Check([engine readUnmatchableRecovery:&state frame:Frame() reason:nil],"unmatchable provider capture succeeds");
    return state;
}
static void UnmatchableNativeTests() {
    UnmatchableNativeFixture(); auto engine=[FixtureSkillEngine new];
    const auto savedGuest=guest, savedImpl=implBytes, savedCatalog=catalogBytes;
    auto state=CaptureUnmatchable(engine);
    Check(state.unmatchableRecovery.valid && state.unmatchableRecovery.turns==1 && state.unmatchableRecovery.fresh &&
        state.unmatchableRecovery.latents[0]==1 && state.unmatchableRecovery.perColor[0]==2 &&
        state.unmatchableRecovery.perColor[2]==2 && state.unmatchableRecovery.providers[0]==1 &&
        state.unmatchableRecovery.providers[2]==1,
        "latent 41 and effective TPA+ authorize two turns for distinct current fire and wood attributes");
    Check(guest==savedGuest && implBytes==savedImpl && catalogBytes==savedCatalog &&
        !writes && !canUseCalls && !readinessCalls && !skillStartCalls,
        "recovery capture is read-only and invokes no native skill, timer or latent getter");
    for (unsigned scenario=0;scenario<11;++scenario) {
        UnmatchableNativeFixture(); engine=[FixtureSkillEngine new];
        const uintptr_t member=Impl+PHBattleMemberOffset;
        const uintptr_t parameter=Load<uintptr_t>(reinterpret_cast<uint8_t *>(member+PHBattleCardParamOffset));
        if (!scenario) Put<uint16_t>(parameter+748,27);
        if (scenario==1) Put<int8_t>(parameter+204,0); // Duplicate attribute must not double-credit.
        if (scenario==2) Put<uint64_t>(Header+0x1900000+4,UINT64_C(48)<<8|1u);
        if (scenario==3) Put<uint16_t>(parameter+748,108); // L+ is not TPA+.
        if (scenario==4) Protected(member+PHOrbSkillBindOffset,1,0x6a875a8b);
        if (scenario==5) Put<uint16_t>(Impl+PHBattleAwakeningStateOffset,1);
        if (scenario==6) Put<uint8_t>(SurvivalDungeon+33,4);
        if (scenario==7) Put<uint8_t>(SurvivalDungeon+33,0x20);
        if (scenario==8) Put<uintptr_t>(member+PHOrbObservationProfileOffset,0);
        if (scenario==9) Put<uint8_t>(Impl+PHOrbObservationAwakeningGrantsOffset+4,96);
        if (scenario==10) Put<uint8_t>(member,3);
        state=CaptureUnmatchable(engine); const auto &r=state.unmatchableRecovery;
        if (!scenario) Check(r.perColor[0]==1 && r.perColor[2]==1,"ordinary TPA counts once");
        else if (scenario==1) Check(r.perColor[0]==2 && !r.perColor[2],"same main and sub color counts the provider only once");
        else Check(!r.perColor[0] && !r.providers[0] && r.blockers[0],
            "wrong latent/awakening, binds, disabled dungeon rules, unknown grants and inactive members provide no credit");
        Check(!writes && !readinessCalls && !skillStartCalls,"ineligible captures never mutate the battle");
    }
    for (unsigned mutation=1;mutation<=7;++mutation) {
        UnmatchableNativeFixture(); engine=[FixtureSkillEngine new]; state=CaptureUnmatchable(engine);
        observationMutation=mutation; observationUnmatchableMutation=true;
        Check(![engine readUnmatchableRecovery:&state frame:Frame() reason:nil] && !state.unmatchableRecovery.valid && !writes,
            "duration, identity, key, input, table, latent-source and queue races revoke the fresh recovery evidence");
    }
    for (unsigned scenario=0;scenario<7;++scenario) {
        UnmatchableNativeFixture(); engine=[FixtureSkillEngine new]; state=CaptureUnmatchable(engine);
        if (!scenario) Protected(Impl+PHOrbUnmatchableColorsOffset,32,0x6a875a8b);
        if (scenario==1) state.unmatchableKnown=false;
        if (scenario==2) Put<uint32_t>(Impl+PHOrbAssistRecoveryQueueCountOffset,1);
        if (scenario==3) Put<uintptr_t>(Impl+PHOrbAssistRecoveryQueueOffset,Impl+22480);
        if (scenario==4) state.multiplayer.kind=PHOrbMultiplayerThreeSolo;
        if (scenario==5) state.boardTransition=true;
        if (scenario==6) Put<uint32_t>(Impl+PHOrbAssistRecoveryDispatchOffset,525190);
        Check(![engine readUnmatchableRecovery:&state frame:Frame() reason:nil] && !state.unmatchableRecovery.valid,
            "mask mismatch, unknown effect, pending queues, continuation and unsupported resolver cannot authorize recovery");
    }
    UnmatchableNativeFixture(); engine=[FixtureSkillEngine new]; state=CaptureUnmatchable(engine);
    const auto &guard=PHOrbUnmatchableRecoveryTextGuards[0]; Put<uint8_t>(Header+guard.rva,guard.bytes[0]^1u);
    Check(![engine readUnmatchableRecovery:&state frame:Frame() reason:nil] &&
        [engine readAutomation:&state frame:Frame() reason:nil],"a policy code mismatch rejects recovery without disabling ordinary reads");
    UnmatchableNativeFixture(); engine=[FixtureSkillEngine new]; state=CaptureUnmatchable(engine);
    changeUnmatchableDuringCoordinateRead=true;
    Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil] &&
        !changeUnmatchableDuringCoordinateRead && !writes,"a change at coordinate conversion prevents the first native input write");
    UnmatchableNativeFixture(); engine=[FixtureSkillEngine new]; state=CaptureUnmatchable(engine);
    const auto savedInput=realInput;
    Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:nil],"fresh provider evidence permits native began");
    NativeInputFrame(); Put<uintptr_t>(Impl+PHBattleDragPointerOffset,state.board.tokens[0]);
    Check([engine finishInput:nil] && GlobalInput()==Input && realInput==savedInput,"native began restores real input bytes");
    state=CaptureUnmatchable(engine); const unsigned sent=writes;
    changeUnmatchableDuringCoordinateRead=true;
    Check(![engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:nil] && writes==sent,
        "changed held evidence forbids an additional movement");
    Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:nil],"owned cleanup release remains permitted");
    NativeInputFrame(); Put<uintptr_t>(Impl+PHBattleDragPointerOffset,0);
    Check([engine finishInput:nil] && GlobalInput()==Input && realInput==savedInput,"cleanup restores the input lease");
    Reset();
}
