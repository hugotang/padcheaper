static unsigned canUseCalls, skillStartCalls, readinessCalls;
static bool skillAvailable, switchMultiInCanUse, changeSelectionInCanUse, changeStageInCanUse;
static bool switchMultiInReadiness, readinessKnown;
static NativeSkillReadiness readinessFixture;
static uint32_t selectionAtCanUse, selectionAtStart, effectiveStartedSkill;
static constexpr uint32_t CurrentFixtureSkill = 64863;
static const uintptr_t SurvivalDungeon = Header + 0x1800000;
static void SetSurvivalHP(uint32_t hp, uint64_t maximum) {
    Protected(Impl + PHBattleHPOffset, hp, 0x766cfc6d);
    Put<uint32_t>(Impl + PHBattleHPShadowOffset, hp ^ 0x8085a6beu);
    Protected(Impl + PHOrbHPBaseLowOffset, uint32_t(maximum), 0x766cfc6d);
    Protected(Impl + PHOrbHPBaseHighOffset, uint32_t(maximum >> 32), 0x766cfc6d);
}
static void SetSurvivalOptions(const char *options) {
    Put<uint32_t>(SurvivalDungeon + PHBattleDungeonRulesOffset, 1);
    Put<uintptr_t>(Header + PHSaveRVA + PHBattleDungeonRulePoolOffset, SurvivalDungeon + 0x100);
    Put<uintptr_t>(SurvivalDungeon + 0x110, SurvivalDungeon + 0x200);
    std::memcpy(reinterpret_cast<void *>(SurvivalDungeon + 0x201), options, std::strlen(options) + 1);
}
@interface FixtureSkillEngine : PHNativeOrbEngine
@end
@implementation FixtureSkillEngine
- (BOOL)nativeCanUseGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    ++canUseCalls;
    selectionAtCanUse = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHOrbSkillOverrideOffset));
    if (switchMultiInCanUse) Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    if (changeSelectionInCanUse) Put<uint32_t>(member + PHOrbSkillOverrideOffset, selectionAtCanUse + 1);
    if (changeStageInCanUse) Protected(member + 344, 1, 0x6a875a8b);
    return skillAvailable;
}
- (BOOL)nativeSkillReadiness:(NativeSkillReadiness *)out member:(uintptr_t)member header:(uintptr_t)header {
    ++readinessCalls; *out = readinessFixture;
    if (switchMultiInReadiness) Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    return readinessKnown;
}
- (void)nativeStartGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    ++skillStartCalls;
    selectionAtStart = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHOrbSkillOverrideOffset));
    // Native 0x10059F8DC consumes a retained positive ID before the current
    // base/charged-assist choice. This exposes a removed-gate-only regression.
    effectiveStartedSkill = int32_t(selectionAtStart) > 0 ? selectionAtStart : CurrentFixtureSkill;
    Put<uint32_t>(member + PHOrbSkillMemberStateOffset, 2);
    Protected(Impl + PHBattlePhaseOffset, 22, 0x766cfc6d);
}
@end

static void SkillFixture() {
    Reset(); canUseCalls = skillStartCalls = readinessCalls = 0; skillAvailable = readinessKnown = true;
    switchMultiInCanUse = changeSelectionInCanUse = changeStageInCanUse = switchMultiInReadiness = false;
    readinessFixture = {CurrentFixtureSkill, 0, 4, 4};
    selectionAtCanUse = selectionAtStart = UINT32_MAX; effectiveStartedSkill = 0;
    Protected(Impl + PHOrbSkillUsesOffset, 0, 0x6a875a8b);
    SetSurvivalHP(100, 1000);
    Put<uintptr_t>(Impl + PHOrbSkillDungeonRulesOffset, SurvivalDungeon);
    Protected(Impl + PHOrbSkillLimitOffset, UINT32_MAX, 0x766cfc6d);
    Protected(Impl + PHOrbSkillLimitRemainingOffset, 1, 0x6a875a8b);
    Put<uint16_t>(Impl + PHOrbSkillSpecialOffset, UINT16_MAX);
    for (unsigned i = 0; i < 6; ++i) {
        SetLeader(i, uint16_t(100 + i));
        const uintptr_t member = Impl + PHBattleMemberOffset + i * PHBattleMemberStride;
        for (uintptr_t offset : {uintptr_t(72), uintptr_t(344), uintptr_t(360), uintptr_t(488), uintptr_t(504)})
            Protected(member + offset, 0, 0x6a875a8b);
        Protected(member + PHBattleChargeOffset, 5, 0x6a875a8b);
        Put<uint32_t>(member + PHBattleChargeShadowOffset, 5u ^ 0x68fb65ceu);
        Put<uint32_t>(member + PHOrbSkillOverrideOffset, UINT32_MAX);
    }
    Protected(Impl + PHOrbEnemyCountOffset, 1, 0x766cfc6d);
    uintptr_t enemy = Impl + PHOrbEnemyOffset;
    Protected(enemy + 64, 100, 0x766cfc6d); Protected(enemy + 80, 0, 0x766cfc6d);
    Put<uint8_t>(enemy + 23, 255);
    for (uintptr_t offset : {uintptr_t(2400), uintptr_t(2192), uintptr_t(2256), uintptr_t(2224), uintptr_t(2176), uintptr_t(2240)})
        Protected(enemy + offset, 0, 0x6a875a8b);
}

static void SurvivalNativeTests() {
    SkillFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{}; NSString *reason = nil;
    Check([engine readAutomation:&state frame:Frame() reason:&reason] && state.automation.hpKnown &&
        state.automation.hp == 100 && state.automation.maximumHP == 1000 && state.automation.statusKnown,
        "current HP uses its shadow and the ordinary effective cap is decoded read-only");
    SetSurvivalOptions("hpper:5000|HFFIX:900|HPFIX:200|hpfix:300");
    Check([engine readAutomation:&state frame:Frame() reason:&reason] && state.automation.hpKnown && state.automation.maximumHP == 200,
        "first case-insensitive fixed HP option takes priority over percentage and later fixed options");
    SetSurvivalOptions("hpfix:0|hpper:5000");
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.hpKnown && state.automation.maximumHP == 1000, "a zero fixed option shadows the percentage option like native lookup");
    SetSurvivalHP(1, 3); SetSurvivalOptions("hpper:5000");
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.hpKnown && state.automation.maximumHP == 2, "dungeon percentage preserves native float32 half-up rounding");
    SkillFixture(); SetSurvivalHP(1, 3); engine = [FixtureSkillEngine new];
    Put<uint16_t>(Impl + PHOrbHPModifierTurnsOffset, 0x401);
    Protected(Impl + PHOrbHPModifierOffset, uint32_t(-50), 0x6a875a8b);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.hpKnown && state.automation.maximumHP == 2, "temporary percentage uses the active signed duration and float64 rounding");
    Protected(Impl + PHOrbHPModifierOffset, 200, 0x6a875a8b);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.hpKnown && state.automation.maximumHP == 200, "temporary positive cap replaces the ordinary maximum");
    Put<uint16_t>(Impl + PHOrbHPModifierTurnsOffset, 0x3ff);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.hpKnown && state.automation.maximumHP == 3, "negative signed duration does not activate an old HP modifier");
    SetSurvivalHP(1, UINT64_C(4294967296));
    Put<uint16_t>(Impl + PHOrbHPModifierTurnsOffset, 1);
    Protected(Impl + PHOrbHPModifierOffset, uint32_t(-100), 0x6a875a8b);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.hpKnown && state.automation.maximumHP == INT32_MAX, "temporary percentage clamps the decoded 64-bit base to the native signed cap");
    SetSurvivalHP(1, 0);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.hpKnown && state.automation.maximumHP == 1, "temporary percentage retains the native minimum of one HP");

    for (unsigned change = 0; change < 5; ++change) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        if (change == 0) Put<uint32_t>(Impl + PHBattleHPShadowOffset, 0);
        if (change == 1) SetSurvivalHP(1001, 1000);
        if (change == 2) SetSurvivalOptions("hpper:invalid");
        if (change == 3) Put<uint32_t>(SurvivalDungeon + PHBattleDungeonRulesOffset, 1);
        if (change == 4) Put<uint32_t>(Impl + PHOrbHPBaseLowOffset, 0);
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.hpKnown &&
            state.automation.maximumHP == 0 && !canUseCalls && !skillStartCalls && !writes,
            "unconfirmed HP inputs remain unknown without inventing a maximum or calling native code");
    }
    SkillFixture(); engine = [FixtureSkillEngine new];
    Put<uint8_t>(SurvivalDungeon + 33, 4);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.statusKnown && state.automation.awakeningRestricted && !state.automation.playerEffects,
        "fixed dungeon awakening restriction is not a cleanseable temporary bind");
    Put<uint16_t>(Impl + PHBattleAwakeningStateOffset, 0x401);
    Protected(Impl + PHBattleMemberOffset + PHBattleMemberStride + PHOrbSkillBindOffset, 2, 0x6a875a8b);
    Put<uint16_t>(Impl + PHOrbSkillSealOffset, 1);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.playerEffects == (PHOrbPlayerAwokenBind | PHOrbPlayerMemberBind | PHOrbPlayerSkillSeal),
        "temporary awakening bind, any-member bind and skill seal remain distinct player conditions");
    Put<uint16_t>(Impl + PHBattleAwakeningStateOffset, 0x3ff);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(!(state.automation.playerEffects & PHOrbPlayerAwokenBind), "expired signed awakening duration does not request a rescue");

    for (unsigned change = 0; change < 3; ++change) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        [engine readAutomation:&state frame:Frame() reason:&reason];
        if (change == 0) SetSurvivalHP(99, 1000);
        if (change == 1) SetSurvivalHP(100, 2000);
        if (change == 2) Put<uint16_t>(Impl + PHBattleAwakeningStateOffset, 1);
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected && !canUseCalls && !skillStartCalls,
            "changed HP, effective maximum or player effect invalidates a pending native skill before cache evaluation");
    }
}

static void SkillNativeTests() {
    SkillFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{}; NSString *reason = nil;
    Check([engine readAutomation:&state frame:Frame() reason:&reason] && state.automation.valid && state.automation.window &&
        !state.automation.busy && state.automation.members[0].number == 100 && canUseCalls == 0 && skillStartCalls == 0 && writes == 0,
        "automation snapshots decode team/enemies without native getter calls or writes");
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
        canUseCalls == 1 && skillStartCalls == 1 && writes == 0, "guarded native current-skill start needs no synthetic gesture or charge write");

    SkillFixture(); engine = [FixtureSkillEngine new]; skillAvailable = false;
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable && canUseCalls == 1 && !skillStartCalls,
        "game readiness refusal never enters native activation");
    Check(readinessCalls == 1 && [reason containsString:@"冷卻已足"] && [reason containsString:@"充能 5"],
        "a native refusal with sufficient charge is distinct from actual cooldown");
    for (int cooldown : {5, 6}) {
        SkillFixture(); engine = [FixtureSkillEngine new]; skillAvailable = false;
        const uintptr_t member = Impl + PHBattleMemberOffset + 2 * PHBattleMemberStride;
        SetLeader(2, 14175);
        Put<uint16_t>(member + 1438, 11698); Put<uint16_t>(member + 1298, 5); Put<uint16_t>(member + 1442, 1);
        Protected(member + 344, 1, 0x6a875a8b); Put<uint8_t>(member + 1290, 1);
        readinessFixture = {65778, 47509, cooldown, cooldown + 20};
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Check(state.automation.members[2].skillStage[0] == 1 && state.automation.members[2].cachedSkillStage[0] == 1 &&
            !readinessCalls && !writes, "evolved stages are read without invoking native cooldown getters");
        Check([engine useSkill:2 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
            readinessCalls == 1 && !skillStartCalls && !writes && [reason containsString:@"65778"] &&
            [reason containsString:@"47509"] && [reason containsString:@"階段 1／0"] &&
            [reason containsString:cooldown == 5 ? @"冷卻已足" : @"冷卻尚未完成"],
            "evolved base readiness uses its five-turn CD, not the assist's total CD, and retains full 32-bit skill IDs");
    }
    SkillFixture(); engine = [FixtureSkillEngine new]; skillAvailable = false; readinessKnown = false;
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        [reason containsString:@"冷卻明細未取得"] && !skillStartCalls && !writes,
        "unavailable diagnostic data never invents a cooldown or permits a refused skill");
    SkillFixture(); engine = [FixtureSkillEngine new]; skillAvailable = false; switchMultiInReadiness = true;
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
        readinessCalls == 1 && !skillStartCalls && !writes,
        "changed multiplayer context after refusal diagnostics never reaches activation or selector writes");
    SkillFixture(); engine = [FixtureSkillEngine new]; changeStageInCanUse = true;
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
        canUseCalls == 1 && !readinessCalls && !skillStartCalls && !writes,
        "an authoritative skill-stage change during can-use invalidates the attempted skill");
    SkillFixture(); engine = [FixtureSkillEngine new];
    Put<uint32_t>(Impl + PHBattleMemberOffset + PHOrbSkillOverrideOffset, 123);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.members[0].selection == 123 &&
        [engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted && writes == 0 &&
        Load<uint32_t>(reinterpret_cast<uint8_t *>(Impl + PHBattleMemberOffset + PHOrbSkillOverrideOffset)) == 123,
        "a retained selection on another idle member neither blocks nor gets changed by this member's skill");
    const uintptr_t secondMember = Impl + PHBattleMemberOffset + PHBattleMemberStride;
    for (uint32_t selection : {0u, 123u, 65063u}) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        Put<uint32_t>(secondMember + PHOrbSkillOverrideOffset, selection);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Check(state.automation.members[1].selection == selection && !writes && !canUseCalls &&
            !state.automation.members[1].state, "retained idle selection is read exactly without preparing or polling native skills");
        const auto before = implBytes;
        Check([engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted &&
            canUseCalls == 1 && skillStartCalls == 1 && writes == 1 && selectionAtCanUse == selection &&
            selectionAtStart == UINT32_MAX && effectiveStartedSkill == CurrentFixtureSkill,
            "a ready idle position clears the old choice before native start consumes the current skill");
        for (unsigned slot = 0; slot < 6; ++slot) {
            const size_t memberOffset = PHBattleMemberOffset + slot * PHBattleMemberStride;
            Check(std::memcmp(before.data() + memberOffset + PHBattleChargeOffset,
                implBytes.data() + memberOffset + PHBattleChargeOffset, 16) == 0 &&
                std::memcmp(before.data() + memberOffset + PHBattleChargeShadowOffset,
                implBytes.data() + memberOffset + PHBattleChargeShadowOffset, 4) == 0,
                "selection preparation never rewrites a member's accumulated charge or shadow");
        }
    }
    SkillFixture(); engine = [FixtureSkillEngine new]; skillAvailable = false;
    Put<uint32_t>(secondMember + PHOrbSkillOverrideOffset, 65063);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        canUseCalls == 1 && !skillStartCalls && !writes &&
        Load<uint32_t>(reinterpret_cast<uint8_t *>(secondMember + PHOrbSkillOverrideOffset)) == 65063,
        "a cooldown or native-effect refusal leaves the retained selection untouched");
    for (bool duringCheck : {false, true}) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        Put<uint32_t>(secondMember + PHOrbSkillOverrideOffset, 65063);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        if (duringCheck) changeSelectionInCanUse = true;
        else Put<uint32_t>(secondMember + PHOrbSkillOverrideOffset, 65064);
        Check([engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
            canUseCalls == (duringCheck ? 1u : 0u) && !skillStartCalls && !writes,
            "a changed selection ID is rejected even when both old and new values are non-default");
    }
    for (unsigned fault = 0; fault < 3; ++fault) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        Put<uint32_t>(secondMember + PHOrbSkillOverrideOffset, 65063);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        if (fault == 0) failWrite = 1;
        if (fault == 1) ignoreSelectionWrite = true;
        if (fault == 2) switchMultiInSelectionWrite = true;
        Check([engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
            canUseCalls == 1 && !skillStartCalls && writes == 1,
            "selection write failure, failed read-back and changed multiplayer context stop before native activation");
    }
    for (unsigned stateValue = 1; stateValue <= 7; ++stateValue) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        Put<uint32_t>(secondMember + PHOrbSkillOverrideOffset, 65063);
        Put<uint32_t>(secondMember + PHOrbSkillMemberStateOffset, stateValue);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Check([engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
            !canUseCalls && !skillStartCalls && !writes,
            "every in-progress member state, including an active selection dialog, prevents preparation and start");
    }
    SkillFixture(); engine = [FixtureSkillEngine new]; switchMultiInCanUse = true;
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected && canUseCalls == 1 && !skillStartCalls,
        "multiplayer change during native cache evaluation cannot reach activation");

    for (unsigned change = 0; change < 9; ++change) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        const uintptr_t member = Impl + PHBattleMemberOffset;
        if (change == 0) Protected(member + PHOrbSkillBindOffset, 1, 0x6a875a8b);
        if (change == 1) Put<uint16_t>(Impl + PHOrbSkillSealOffset, 1);
        if (change == 2) Put<uint8_t>(Impl + PHOrbEnemyOffset + 23, 0);
        if (change == 3) Put<uint32_t>(member + PHOrbSkillMemberStateOffset, 1);
        if (change == 4) Put<uint32_t>(member + PHBattleMemberStride + PHOrbSkillMemberStateOffset, 4);
        if (change == 5) { Protected(Impl + PHOrbSkillLimitOffset, 0, 0x766cfc6d); Protected(Impl + PHOrbSkillLimitRemainingOffset, 0, 0x6a875a8b); }
        if (change == 6) Put<uint32_t>(Impl + PHOrbSkillSpecialModeOffset, 0x12345678);
        if (change == 7) Put<uint8_t>(Header + PHOrbTutorialTableRVA + 2, 75);
        if (change == 8) Protected(Impl + PHOrbSkillUsesOffset, 255, 0x6a875a8b);
        Check([engine readAutomation:&state frame:Frame() reason:&reason], "native blocking state remains readable");
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] != PHOrbSkillStarted && !canUseCalls && !skillStartCalls,
            "tap/readiness gates reject bound, sealed, enemy action, selection, limits, cutscene, tutorial and saturated acknowledgement");
    }
    SkillFixture(); engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Put<uint32_t>(Impl + PHBattleMemberOffset + PHBattleChargeShadowOffset, 0);
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected && !canUseCalls && !skillStartCalls,
        "charge shadow inconsistency refuses native skill evaluation");
    SkillFixture(); engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected && !canUseCalls && !skillStartCalls,
        "multiplayer refusal happens before native calls");
    for (uintptr_t guard : {PHOrbStartSkillRVA, PHOrbSkillSelectionResetRVA, PHOrbBaseSkillRVA,
        PHOrbAssistSkillRVA, PHOrbBaseCooldownRVA, PHOrbTotalCooldownRVA}) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        Put<uint32_t>(Impl + PHBattleMemberOffset + PHOrbSkillOverrideOffset, 65063);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Put<uint8_t>(Header + guard, 0);
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected &&
            !canUseCalls && !skillStartCalls && !writes,
            "skill activation rechecks start and selection-reset fingerprints before calls or writes");
    }

    for (unsigned effect = 0; effect < 4; ++effect) {
        SkillFixture(); engine = [FixtureSkillEngine new];
        uintptr_t durations[]{2400,2192,2256,2224}, bypass[]{739196,739156,739668,739176};
        uintptr_t enemy = Impl + PHOrbEnemyOffset;
        Protected(enemy + durations[effect], 2, 0x6a875a8b);
        Protected(enemy + 2176, 31, 0x6a875a8b); Protected(enemy + 2240, 31, 0x6a875a8b);
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && state.automation.enemyEffects == (1u << effect),
            "each enemy effect uses its protected duration and only living enemies");
        Put<uint16_t>(Impl + bypass[effect], 2); Put<uint16_t>(Impl + bypass[effect] + 2, 1);
        [engine readAutomation:&state frame:Frame() reason:&reason];
        Check(!state.automation.playerBypass, "delayed bypass is not active yet");
        Put<uint16_t>(Impl + bypass[effect] + 2, 0); [engine readAutomation:&state frame:Frame() reason:&reason];
        Check(state.automation.playerBypass == (1u << effect), "active team-wide bypass has a separate mask");
        Protected(enemy + 64, 0, 0x766cfc6d); [engine readAutomation:&state frame:Frame() reason:&reason];
        Check(!state.automation.enemyEffects && !state.automation.activeEnemies, "dead enemy's shield cannot trigger a rule");
    }
    SkillFixture(); engine = [FixtureSkillEngine new]; Put<uint16_t>(Impl + PHOrbEnemyOffset + 532, 0x100);
    Check([engine readAutomation:&state frame:Frame() reason:&reason] && !state.automation.enemiesKnown && !state.automation.window,
        "unmodeled enemy parts never pretend to be a known effect-free enemy");
    SkillFixture(); engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Put<uint8_t>(state.board.tokens[0], 10); [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(!PHOrbCanPreview(&state) && state.automation.window &&
        [engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted,
        "skill remediation does not weaken the unsupported-orb drag restriction");
}
