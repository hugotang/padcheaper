static unsigned canUseCalls, skillStartCalls;
static bool skillAvailable, switchMultiInCanUse;
@interface FixtureSkillEngine : PHNativeOrbEngine
@end
@implementation FixtureSkillEngine
- (BOOL)nativeCanUseGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    ++canUseCalls;
    if (switchMultiInCanUse) Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    return skillAvailable;
}
- (void)nativeStartGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    ++skillStartCalls;
    Put<uint32_t>(member + PHOrbSkillMemberStateOffset, 2);
    Protected(Impl + PHBattlePhaseOffset, 22, 0x766cfc6d);
}
@end

static void SkillFixture() {
    Reset(); canUseCalls = skillStartCalls = 0; skillAvailable = true; switchMultiInCanUse = false;
    Protected(Impl + PHOrbSkillUsesOffset, 0, 0x6a875a8b);
    Protected(Impl + PHBattleHPOffset, 100, 0x766cfc6d);
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
    SkillFixture(); engine = [FixtureSkillEngine new];
    Put<uint32_t>(Impl + PHBattleMemberOffset + PHOrbSkillOverrideOffset, 123);
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(state.automation.members[0].overridden &&
        [engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted,
        "a retained manual selection on another member does not disable this member's skill");
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
        if (change == 4) Put<uint32_t>(member + PHOrbSkillOverrideOffset, 123);
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
    SkillFixture(); engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Put<uint8_t>(Header + PHOrbStartSkillRVA, 0);
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected && !canUseCalls && !skillStartCalls,
        "skill activation rechecks instruction fingerprints even after an earlier successful read");

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
    Put<uint8_t>(state.board.tokens[0], 6); [engine readAutomation:&state frame:Frame() reason:&reason];
    Check(!PHOrbCanPreview(&state) && state.automation.window &&
        [engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted,
        "skill remediation does not weaken the unsupported-orb drag restriction");
}
