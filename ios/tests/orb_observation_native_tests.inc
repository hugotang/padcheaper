// Source-only regressions: run with the next explicitly requested build.
static void ObservationSetCooldown(uint32_t id, uint16_t maximum, uint16_t base, int16_t delta) {
    const uintptr_t record = Header + 0x2000000 + size_t(id) * sizeof(SkillDiagnosticRecord);
    Put<uint16_t>(record + 14, maximum); Put<uint16_t>(record + 16, base); Put<int16_t>(record + 18, delta);
}
static void ObservationSetCharge(unsigned slot, unsigned charge) {
    const uintptr_t address = Impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    Protected(address + PHBattleChargeOffset, charge, 0x6a875a8b);
    Put<uint32_t>(address + PHBattleChargeShadowOffset, charge ^ 0x68fb65ceu);
}
static void ObservationFixture() {
    std::fill(catalogBytes.begin(), catalogBytes.end(), 0);
    SkillFixture();
    SetSkillRecord(1000, 0, {}); ObservationSetCooldown(1000, 10, 14, -1); // Level 10 -> 5.
    SetSkillRecord(1001, 0, {}); ObservationSetCooldown(1001, 5, 20, -1); // Level 5 -> 16.
    Protected(Impl + PHOrbObservationCooldownOffset, 0, 0x766cfc6d);
    Put<uint16_t>(Impl + PHOrbObservationAssistVoidOffset, 0x400 | 3);
    for (unsigned slot = 0; slot < 6; ++slot) {
        const uintptr_t address = Impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(address + PHBattleCardParamOffset));
        Put<uint32_t>(parameter + 84, 1000); Put<uint8_t>(parameter + 12, 3);
        Put<int8_t>(parameter + 204, -1); Put<int8_t>(parameter + 216, -1);
        Put<int8_t>(address + 1287, -1); Put<int8_t>(address + 1288, -1);
        Put<uint16_t>(address + 1298, 10); Put<uint16_t>(address + 1296, 120);
        Put<uint16_t>(address + PHBattleBaseIDOffset, uint16_t(Load<uint32_t>(reinterpret_cast<uint8_t *>(parameter))));
        for (unsigned i = 0; i < 9; ++i) Protected(address + PHOrbObservationAddedAwakeningsOffset + 16 * i, 0, 0x6a875a8b);
    }
    const uintptr_t address = Impl + PHBattleMemberOffset;
    const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(address + PHBattleCardParamOffset));
    Put<uint8_t>(address + 1286, 1); Put<uint16_t>(parameter + 748, 108); // L+ = two effective L.
    Put<int8_t>(parameter + 204, 4);
    const uintptr_t profile = Header + 0x1900000;
    Put<uintptr_t>(address + PHOrbObservationProfileOffset, profile);
    uint64_t latent = 0x6fu;
    for (unsigned i = 0; i < 6; ++i) latent |= UINT64_C(48) << (8 + 7 * i);
    Put<uint64_t>(profile + 4, latent);
    Put<uint32_t>(parameter + PHOrbSkillFamilyOffset, 123);
    // Use an ordinary catalog entry for the source and current assist.
    const uint16_t assist = 300;
    const uintptr_t equip = reinterpret_cast<uintptr_t>(catalogBytes.data()) + assist * PHBattleCatalogStride;
    Put<uint32_t>(equip, assist); Put<uint32_t>(equip + 84, 1001);
    Put<uint32_t>(equip + PHOrbSkillFamilyOffset, 456);
    Put<uint16_t>(address + PHBattleAssistIDOffset, assist); Put<uint16_t>(address + 1438, assist);
    Put<uint8_t>(address + 1442, 5); Put<uint8_t>(address + 1443, 0xe7); // Adjacent byte is not assist skill level.
    Protected(address + 472, 172, 0x6a875a8b);
    ObservationSetCharge(0, 20);
}
static PHOrbSnapshot CaptureObservation(FixtureSkillEngine *engine) {
    PHOrbSnapshot state{};
    Check([engine readAutomation:&state frame:Frame() reason:nil], "observation fixture has a valid single-player base read");
    [engine observeBattle:&state];
    return state;
}
static void ObservationNativeTests() {
    ObservationFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    const auto originalGuest = guest, originalImpl = implBytes, originalCatalog = catalogBytes;
    PHOrbSnapshot state = CaptureObservation(engine);
    const auto &observation = state.observation; const auto &member = observation.members[0];
    Check(observation.status == PHOrbObservationReady && observation.hpKnown && observation.hp == 100 &&
        observation.enemiesKnown && observation.activeEnemies == 1 && observation.enemies[0].hp == 100 &&
        observation.assistVoidKnown && observation.assistVoidTurns == 3 && observation.assistVoidFresh,
        "optional reader reports current enemy HP, player HP and signed assist-void duration");
    Check(member.thresholdsKnown && member.bodyCooldown == 5 && member.totalCooldown == 21 &&
        member.chargeToAssist == 1 && member.chargeWindow == PHOrbChargeBody &&
        member.skillIDs[0] == 1000 && member.skillIDs[1] == 1001 && !member.sourceFamilyEqual,
        "skill windows use level-adjusted body CD and the one-byte assist level with no turn prediction");
    Check(member.lEligibilityKnown && !member.lBlockers && member.lAwakenings == 2 && member.assistRecoveryLatents == 1 &&
        member.lColors == ((1u << 3) | (1u << 4)),
        "one six-slot recovery latent with L+ grants the member's distinct light/dark L colors");
    Check(guest == originalGuest && implBytes == originalImpl && catalogBytes == originalCatalog &&
        !writes && !readinessCalls && !canUseCalls && !choiceCalls && !skillStartCalls,
        "diagnostics leave every guest byte unchanged and never invoke native skill/getter/choice calls");
    for (unsigned charge : {4u, 5u, 20u, 21u}) {
        ObservationSetCharge(0, charge); state = CaptureObservation(engine);
        const auto &window = state.observation.members[0];
        Check(window.thresholdsKnown && window.chargeWindow == (charge < 5 ? PHOrbChargeBelowBody :
            charge < 21 ? PHOrbChargeBody : PHOrbChargeAssist) &&
            window.chargeToBody == (charge < 5 ? 5 - charge : 0) && window.chargeToAssist == 21 - charge,
            "B-1, B, T-1 and T are distinct charge boundaries");
    }
    Protected(Impl + PHOrbObservationCooldownOffset, 2, 0x766cfc6d); state = CaptureObservation(engine);
    Check(state.observation.members[0].bodyCooldown == 7 && state.observation.members[0].totalCooldown == 25,
        "initialized dungeon CD adjustment applies once to body and twice to an active assist total");
    Protected(Impl + PHOrbObservationCooldownOffset, UINT32_MAX, 0x766cfc6d);
    const auto uninitialized = implBytes;
    state = CaptureObservation(engine);
    Check(!state.observation.cooldownAdjustmentKnown && !state.observation.members[0].thresholdsKnown &&
        state.observation.members[0].skillKnown[0] && state.automation.valid && implBytes == uninitialized && !readinessCalls,
        "an uninitialized cooldown cache stays unknown without lazily writing it or disabling base automation");

    for (unsigned type : {232u, 233u}) for (unsigned stage : {0u, 1u, 2u, 255u}) {
        ObservationFixture(); engine = [FixtureSkillEngine new];
        SetSkillRecord(1000, uint16_t(type), {1002,1003}); ObservationSetCooldown(1000, 10, 14, -1);
        SetSkillRecord(1002, 0, {}); ObservationSetCooldown(1002, 1, 8, 0);
        SetSkillRecord(1003, 0, {}); ObservationSetCooldown(1003, 4, 9, -1);
        const uintptr_t address = Impl + PHBattleMemberOffset;
        Protected(address + 344, stage, 0x6a875a8b); Put<uint8_t>(address + 1290, 0);
        state = CaptureObservation(engine);
        const bool rootCD = stage == 0 || (type == 233 && stage >= 2);
        const auto &evolved = state.observation.members[0];
        Check(evolved.skillKnown[0] && evolved.skillIDs[0] == (stage == 0 || (type == 233 && stage >= 2) ? 1002u : 1003u) &&
            evolved.bodyCooldown == (rootCD ? 5 : 9) && evolved.stage[0] == stage && !evolved.cachedStage[0],
            "authoritative evolved stages use root or child base CD and expose stale native cache without updating it");
    }
    for (unsigned mutation = 1; mutation <= 4; ++mutation) {
        ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureObservation(engine);
        observationMutation = mutation; [engine observeBattle:&state];
        Check(state.observation.status == PHOrbObservationChanged && !state.observation.members[0].thresholdsKnown &&
            !writes && !canUseCalls && !skillStartCalls,
            "changed assist duration, entry, protected key or held orb invalidates the entire optional observation");
    }
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        ObservationFixture(); engine = [FixtureSkillEngine new];
        const uintptr_t address = Impl + PHBattleMemberOffset;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(address + PHBattleCardParamOffset));
        if (scenario == 0) ObservationSetCooldown(1000, 10, 4, -1); // Invalid negative result.
        if (scenario == 1) { SetSkillRecord(1000, 232, {70000}); ObservationSetCooldown(1000, 1, 5, 0); }
        if (scenario == 2) { Put<uint8_t>(SurvivalDungeon + 33, 8); ObservationSetCharge(0, 21); }
        if (scenario == 3) {
            Put<uint32_t>(parameter + PHOrbSkillFamilyOffset, 456);
            Protected(Impl + PHOrbEnemyOffset + 2400, 2, 0x6a875a8b);
            Protected(Impl + PHOrbEnemyOffset + 2192, 3, 0x6a875a8b);
            Protected(Impl + PHOrbEnemyOffset + 2176, 8, 0x6a875a8b);
        }
        if (scenario == 4) { Put<uint8_t>(address + 1286, 0); Put<uint32_t>(Header + 0x1900000 + 16, 108); }
        if (scenario >= 5) {
            Put<uint8_t>(address + 1286, 0);
            const uintptr_t equip = reinterpret_cast<uintptr_t>(catalogBytes.data()) + 300 * PHBattleCatalogStride;
            Put<uint16_t>(equip + 748, 108); Put<uint16_t>(equip + 750, 49);
            Put<uint8_t>(address + 1440, scenario == 5 ? 1 : 2);
        }
        state = CaptureObservation(engine); const auto &entry = state.observation.members[0];
        Check(state.observation.status == PHOrbObservationReady && state.automation.valid, "partial skill diagnostics preserve ordinary automation reads");
        if (scenario < 2) Check(!entry.thresholdsKnown, "negative CD arithmetic and out-of-table evolved children stay unknown");
        if (scenario == 2) Check(entry.chargeWindow == PHOrbChargeBody && state.observation.dungeonFlags == 8,
            "dungeon assist prohibition suppresses the charged-assist classification");
        if (scenario == 3) Check(entry.familyKnown && entry.sourceFamilyEqual &&
            state.observation.enemies[0].effects == (PHOrbEnemyDamageAbsorb | PHOrbEnemyAttributeAbsorb) &&
            state.observation.enemies[0].effectTurns[0] == 2 && state.observation.enemies[0].effectMasks[1] == 8,
            "source-family identity and per-enemy current effects are factual diagnostics only");
        if (scenario == 4) Check(entry.lEligibilityKnown && !entry.lBlockers && entry.lAwakenings == 2,
            "a level-120 super L+ awakening contributes two effective L awakenings");
        if (scenario == 5) Check(entry.lEligibilityKnown && (entry.lBlockers & PHOrbLNoAwakening),
            "an equip marker outside the unlocked assist awakenings cannot grant its L+");
        if (scenario == 6) Check(entry.lEligibilityKnown && !entry.lBlockers && entry.lAwakenings == 2,
            "an unlocked equip marker enables the active assist's L+");
    }
    for (unsigned scenario = 0; scenario < 12; ++scenario) {
        ObservationFixture(); engine = [FixtureSkillEngine new];
        const uintptr_t address = Impl + PHBattleMemberOffset;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(address + PHBattleCardParamOffset));
        if (scenario == 0) Protected(address + 72, 1, 0x6a875a8b);
        if (scenario == 1) Put<uint16_t>(Impl + PHBattleAwakeningStateOffset, 3);
        if (scenario == 2) Put<uint8_t>(SurvivalDungeon + 33, 4);
        if (scenario == 3) Put<uint8_t>(SurvivalDungeon + 33, 0x20);
        if (scenario == 4) Put<uint16_t>(parameter + 748, 0);
        if (scenario == 5) Put<uintptr_t>(address + PHOrbObservationProfileOffset, 0);
        if (scenario == 6) Put<uint8_t>(Impl + PHOrbObservationAwakeningGrantsOffset + 4, 60);
        if (scenario == 7) Put<uint32_t>(address + PHOrbObservationAddedAwakeningsOffset, 0);
        if (scenario == 8) Put<uint8_t>(parameter + 12, 5);
        if (scenario == 9) Put<uint8_t>(address + 1444, 1);
        if (scenario == 10) { Put<uint16_t>(address + 1438, 0); Protected(address + 472, 0, 0x6a875a8b); }
        if (scenario == 11) Put<int8_t>(address + 1287, 0);
        state = CaptureObservation(engine); const auto &entry = state.observation.members[0];
        Check(state.observation.status == PHOrbObservationReady && state.automation.valid && !writes && !readinessCalls,
            "optional L/skill coverage gaps never become ordinary automation read failures");
        if (scenario < 6) Check(entry.lEligibilityKnown && entry.lBlockers && !entry.lColors,
            "bind, awoken bind, dungeon restrictions, absent L and absent latent each prevent L recovery eligibility");
        else if (scenario < 9) Check(!entry.lEligibilityKnown && entry.lBlockers && !entry.lColors,
            "unmodeled dynamic L grants, corrupt protected awakenings and unknown attack colors stay unknown");
        else if (scenario == 9) Check(entry.assistVariantUnsupported && !entry.thresholdsKnown && entry.skillKnown[0],
            "assist variants never reuse an ordinary parameter row or fabricate a cooldown threshold");
        else if (scenario == 10) Check(entry.thresholdsKnown && entry.totalCooldown == 5 && !entry.activeAssist &&
            entry.sourceAssist == 300 && !entry.assistRetained, "consumed assist and retained source remain distinct");
        else Check(entry.lEligibilityKnown && entry.lColors == 1, "a main-attribute override replaces all existing fallback attributes");
    }
    ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureObservation(engine);
    Put<uint8_t>(Header + PHOrbObservationCooldownCacheRVA, 0);
    [engine observeBattle:&state];
    Check(state.observation.status == PHOrbObservationUnavailable && !state.observation.members[0].thresholdsKnown &&
        [engine readAutomation:&state frame:Frame() reason:nil] && state.automation.valid,
        "an optional fingerprint mismatch removes diagnostics without changing the existing automation code gate");
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        ObservationFixture(); engine = [FixtureSkillEngine new]; state = CaptureObservation(engine);
        if (scenario == 0) state.multiplayer.kind = PHOrbMultiplayerFour;
        if (scenario == 1) state.multiplayer.kind = PHOrbMultiplayerThreeSolo;
        if (scenario == 2) state.phase = 22;
        if (scenario == 3) state.dragged = 1;
        [engine observeBattle:&state];
        Check(state.observation.status == (scenario < 2 ? PHOrbObservationMultiplayer : PHOrbObservationTransition) &&
            !state.observation.members[0].thresholdsKnown && !writes && !canUseCalls,
            "multiplayer including native solo continuation, skill animations and held orbs never use solo diagnostics");
    }
}
