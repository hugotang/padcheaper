static unsigned canUseCalls, skillStartCalls, readinessCalls, choiceCalls, bodyCanUseCalls;
static bool skillAvailable, switchMultiInCanUse, changeSelectionInCanUse, changeStageInCanUse;
static bool switchMultiInReadiness, readinessKnown, checkOrbCountInCanUse;
static bool choiceAvailable, bodyAvailable, switchMultiInChoice, changeFamilyInChoice;
static bool changeSourceCardsInChoice, changeBodySkillInReadiness, invalidSourceInBody, changeAssistRulesInBody;
static NativeSkillReadiness readinessFixture;
static uint32_t selectionAtCanUse, selectionAtStart, effectiveStartedSkill;
static uint32_t effectsStartedSkill;
static uint8_t sourceAtStart;
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
- (BOOL)nativeCanUseGame:(uintptr_t)game member:(uintptr_t)member selector:(int)selector header:(uintptr_t)header {
    ++canUseCalls;
    bodyCanUseCalls += selector == 0;
    selectionAtCanUse = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHOrbSkillOverrideOffset));
    if (switchMultiInCanUse) Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    if (changeSelectionInCanUse) Put<uint32_t>(member + PHOrbSkillOverrideOffset, selectionAtCanUse + 1);
    if (changeStageInCanUse) Protected(member + 344, 1, 0x6a875a8b);
    const unsigned charge = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHBattleChargeOffset + 12));
    const bool chargedAssist = Load<int16_t>(reinterpret_cast<uint8_t *>(member + 1438)) > 0 &&
        !(Load<uint8_t>(reinterpret_cast<uint8_t *>(SurvivalDungeon + 33)) & 8) &&
        readinessFixture.totalCooldown > 0 && charge >= unsigned(readinessFixture.totalCooldown);
    const bool assist = selector == 1 || (selector == -1 && chargedAssist);
    Put<uint8_t>(Impl + PHOrbSkillSelectedSourceOffset, assist);
    if (selector == 0 && invalidSourceInBody) Put<uint8_t>(Impl + PHOrbSkillSelectedSourceOffset, 1);
    if (selector == 0 && changeAssistRulesInBody) Put<uint8_t>(SurvivalDungeon + 33, 8);
    const bool usable = (selector == 0 ? bodyAvailable : skillAvailable) && charge >= unsigned(readinessFixture.baseCooldown);
    if (checkOrbCountInCanUse) {
        unsigned light = 0;
        for (unsigned row = 0; row < 5; ++row) for (unsigned col = 0; col < 6; ++col) {
            auto token = Load<uintptr_t>(reinterpret_cast<uint8_t *>(Impl + PHOrbTableOffset + (row * 16 + col) * 8));
            light += Load<uint8_t>(reinterpret_cast<uint8_t *>(token)) == 3;
        }
        return usable && (assist || light >= 9);
    }
    return usable;
}
- (BOOL)nativeCanChooseSkill:(uintptr_t)member header:(uintptr_t)header {
    ++choiceCalls;
    SkillChoiceState source;
    PHOrbSnapshot state{};
    [self readAutomation:&state frame:Frame() reason:nil];
    const unsigned slot = unsigned((member - Impl - PHBattleMemberOffset) / PHBattleMemberStride);
    const bool eligible = ReadSkillChoice(state, slot, source) && source.family[0] == source.family[1];
    if (switchMultiInChoice) Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    if (changeFamilyInChoice)
        Put<uint32_t>(source.catalog + source.cards[1] * PHBattleCatalogStride + PHOrbSkillFamilyOffset, source.family[1] + 1);
    if (changeSourceCardsInChoice) Put<uint16_t>(member + PHBattleAssistIDOffset, uint16_t(source.cards[1] + 1));
    return choiceAvailable && eligible;
}
- (BOOL)nativeSkillReadiness:(NativeSkillReadiness *)out member:(uintptr_t)member header:(uintptr_t)header {
    ++readinessCalls; *out = readinessFixture;
    if (switchMultiInReadiness) Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
    if (changeBodySkillInReadiness && readinessCalls > 1) ++out->baseSkill;
    return readinessKnown;
}
- (void)nativeStartGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    ++skillStartCalls;
    selectionAtStart = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHOrbSkillOverrideOffset));
    sourceAtStart = Load<uint8_t>(reinterpret_cast<uint8_t *>(Impl + PHOrbSkillSelectedSourceOffset));
    // Native 0x10059F8DC consumes a retained positive ID before the current
    // base/charged-assist choice. This exposes a removed-gate-only regression.
    effectiveStartedSkill = int32_t(selectionAtStart) > 0 ? selectionAtStart :
        sourceAtStart ? readinessFixture.assistSkill : readinessFixture.baseSkill;
    // The later state-2 path recomputes default source only for a negative
    // selection; composite effects consume the source independently of ID.
    bool assist = sourceAtStart != 0;
    if (int32_t(selectionAtStart) < 0) {
        const unsigned charge = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHBattleChargeOffset + 12));
        assist = Load<int16_t>(reinterpret_cast<uint8_t *>(member + 1438)) > 0 &&
            !(Load<uint8_t>(reinterpret_cast<uint8_t *>(SurvivalDungeon + 33)) & 8) &&
            charge >= unsigned(readinessFixture.totalCooldown);
    }
    effectsStartedSkill = assist ? readinessFixture.assistSkill : readinessFixture.baseSkill;
    Put<uint32_t>(member + PHOrbSkillMemberStateOffset, 2);
    Protected(Impl + PHBattlePhaseOffset, 22, 0x766cfc6d);
}
@end

static void SkillFixture() {
    Reset(); canUseCalls = skillStartCalls = readinessCalls = choiceCalls = bodyCanUseCalls = 0;
    skillAvailable = readinessKnown = choiceAvailable = bodyAvailable = true;
    switchMultiInCanUse = changeSelectionInCanUse = changeStageInCanUse = switchMultiInReadiness = false;
    checkOrbCountInCanUse = false;
    switchMultiInChoice = changeFamilyInChoice = changeSourceCardsInChoice = changeBodySkillInReadiness = false;
    invalidSourceInBody = changeAssistRulesInBody = false;
    readinessFixture = {CurrentFixtureSkill, 0, 4, 4};
    selectionAtCanUse = selectionAtStart = UINT32_MAX; effectiveStartedSkill = effectsStartedSkill = 0; sourceAtStart = 255;
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
        for (uintptr_t offset : {PHBattleTempIDOffset, PHBattleTempTurnsOffset, PHBattleFormOffset})
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

static void SetSkillRecord(uint32_t id, uint16_t type, std::initializer_list<int32_t> params) {
    const uintptr_t table = Header + 0x2000000;
    Put<uintptr_t>(Header + PHSaveRVA + PHBattleSkillPointerOffset, table);
    Put<uint32_t>(Header + PHSaveRVA + PHOrbSkillCountOffset, 70000);
    SkillDiagnosticRecord record{};
    record.type = type;
    std::copy(params.begin(), params.end(), record.params);
    Put<SkillDiagnosticRecord>(table + id * sizeof(record), record);
}
static void SkillOrbConditionTests() {
    const uintptr_t member = Impl + PHBattleMemberOffset;
    auto fixture = [&]() {
        SkillFixture(); skillAvailable = false;
        SetLeader(0, 14038);
        Put<uint16_t>(member + 1438, 14139);
        Protected(member + PHBattleChargeOffset, 4, 0x6a875a8b);
        Put<uint32_t>(member + PHBattleChargeShadowOffset, 4u ^ 0x68fb65ceu);
        readinessFixture = {64787, 65021, 4, 18};
        // 64787 is conditional even though its charge is exactly its CD.
        SetSkillRecord(64787, 116, {64788, 64789, 64790, 64820});
        SetSkillRecord(64788, 255, {8, 9});
        SetSkillRecord(64789, 191, {4});
        SetSkillRecord(64790, 173, {4, 0, 0, 1});
        SetSkillRecord(64820, 258, {4, 350, 1});
        SetSkillRecord(65021, 116, {65022});
        SetSkillRecord(65022, 172, {});
    };
    auto lightBoard = [](unsigned light) {
        for (unsigned row = 0; row < 5; ++row) for (unsigned col = 0; col < 6; ++col) {
            uintptr_t token = Load<uintptr_t>(reinterpret_cast<uint8_t *>(Impl + PHOrbTableOffset + (row * 16 + col) * 8));
            Put<uint8_t>(token, row * 6 + col < light ? 3 : 5);
        }
    };
    PHOrbSnapshot state{}; NSString *reason = nil;
    for (unsigned light : {5u, 8u, 9u, 12u}) {
        fixture(); lightBoard(light);
        FixtureSkillEngine *engine = [FixtureSkillEngine new];
        Check([engine readAutomation:&state frame:Frame() reason:&reason] && !readinessCalls && !writes,
            "live conditional skill metadata is never consulted by ordinary automation reads");
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
            [reason containsString:@"光珠至少 9 顆"] &&
            [reason containsString:[NSString stringWithFormat:@"目前 %u 顆", light]] &&
            [reason containsString:light < 9 ? @"本體使用條件未滿足" : @"珠數條件已滿足，遊戲仍判定不可用"] &&
            [reason containsString:@"充能 4"] && !skillStartCalls && !writes,
            "full-CD Uryu reports the exact board requirement without overriding a native refusal");
    }
    fixture(); lightBoard(8); skillAvailable = checkOrbCountInCanUse = true;
    FixtureSkillEngine *engine = [FixtureSkillEngine new];
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable && !skillStartCalls,
        "independent native count fixture refuses eight light orbs at full cooldown");
    lightBoard(9); [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillStarted && skillStartCalls == 1 &&
        canUseCalls == 2 && readinessCalls == 1 && !writes,
        "a later native approval at nine light orbs starts normally without diagnostic getters or charge writes");

    for (bool assistDisabled : {false, true}) {
        fixture(); lightBoard(5);
        Protected(member + PHBattleChargeOffset, 18, 0x6a875a8b);
        Put<uint32_t>(member + PHBattleChargeShadowOffset, 18u ^ 0x68fb65ceu);
        if (assistDisabled) Put<uint8_t>(SurvivalDungeon + 33, 8);
        engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
            [reason containsString:@"光珠至少 9 顆"] == assistDisabled && !skillStartCalls && !writes,
            "conditional base requirements are not misattributed to a charged assist; dungeon assist disable selects the base");
    }
    fixture(); lightBoard(5);
    Protected(member + PHBattleChargeOffset, 18, 0x6a875a8b);
    Put<uint32_t>(member + PHBattleChargeShadowOffset, 18u ^ 0x68fb65ceu);
    SetSkillRecord(65021, 116, {66001}); SetSkillRecord(66001, 255, {32, 26});
    engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        [reason containsString:@"武裝使用條件未滿足"] && [reason containsString:@"心珠至少 26 顆，目前 25 顆"] && !writes,
        "the charged assist uses its own condition and full 32-bit child ID");

    for (unsigned fault = 0; fault < 8; ++fault) {
        fixture(); lightBoard(5);
        if (fault == 0) Put<uintptr_t>(Header + PHSaveRVA + PHBattleSkillPointerOffset, 0);
        if (fault == 1) Put<uint32_t>(Header + PHSaveRVA + PHOrbSkillCountOffset, 64787);
        if (fault == 2) SetSkillRecord(64787, 116, {70000});
        if (fault == 3) SetSkillRecord(64788, 255, {1024, 9});
        if (fault == 4) SetSkillRecord(64788, 255, {8, -1});
        if (fault == 5) SetSkillRecord(64787, 118, {64788});
        if (fault == 6) SetSkillRecord(64787, 120, {64789, 64788});
        if (fault == 7) Put<uint32_t>(Header + PHSaveRVA + PHOrbSkillCountOffset, UINT32_MAX);
        engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
        Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
            [reason containsString:@"冷卻已足"] && ![reason containsString:@"光珠至少"] && !writes && !skillStartCalls,
            "missing, out-of-bounds or unsupported native condition metadata stays unknown and cannot authorize activation");
    }
    fixture(); lightBoard(5); SetSkillRecord(64787, 120, {64788}); SetSkillRecord(64788, 255, {40, 9});
    engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        [reason containsString:@"光珠至少 9 顆，目前 5 顆"] && [reason containsString:@"心珠至少 9 顆，目前 25 顆"],
        "native first-child wrapper and multi-color thresholds require each selected color independently");
    fixture(); lightBoard(5); Put<float>(Impl + PHOrbSkillBoardResizeOffset, 0.5f);
    engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable &&
        [reason containsString:@"目前珠數未能確認"] && ![reason containsString:@"目前 5 顆"] && !writes,
        "a resizing native board preserves the requirement but does not claim an incorrect current count");
    fixture(); lightBoard(5); engine = [FixtureSkillEngine new];
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([SkillOrbCondition(state, 0, readinessFixture, false) containsString:@"目前珠數未能確認"],
        "a changed spinner sample does not mislabel its later color as the refused native observation");
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

static void SkillSourceNativeTests() {
    for (bool chargedAssist : {false,true}) for (PHOrbSkillSource requested : {PHOrbSkillBody,PHOrbSkillAssist}) {
        SkillFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{}; NSString *reason = nil;
        const uintptr_t member = Impl + PHBattleMemberOffset;
        Put<uint16_t>(member + 1438, 14139);
        uint16_t body = 0; PHBattleCatalogIDForNumber(100, &body);
        Put<uint16_t>(member + PHBattleBaseIDOffset, body);
        Put<uint16_t>(member + PHBattleAssistIDOffset, 14139);
        const auto catalog = reinterpret_cast<uintptr_t>(catalogBytes.data());
        Put<uint32_t>(catalog + 14139 * PHBattleCatalogStride, 14139);
        Put<uint32_t>(catalog + body * PHBattleCatalogStride + PHOrbSkillFamilyOffset, 100);
        Put<uint32_t>(catalog + 14139 * PHBattleCatalogStride + PHOrbSkillFamilyOffset, 14037);
        const unsigned charge = chargedAssist ? 18 : 4;
        Protected(member + PHBattleChargeOffset, charge, 0x6a875a8b);
        Put<uint32_t>(member + PHBattleChargeShadowOffset, charge ^ 0x68fb65ceu);
        readinessFixture = {64787,65021,4,18};
        Check([engine readAutomation:&state frame:Frame() reason:&reason], "skill source native fixture reads without getters");
        Check(!readinessCalls, "passive reads never invoke cooldown getters for plan constraints");
        PHOrbSkillRequest request{requested, requested == PHOrbSkillBody ? 64787u : 65021u};
        PHOrbSkillResult result = [engine useSkill:0 request:request expected:&state frame:Frame() reason:&reason];
        const bool matches = chargedAssist == (requested == PHOrbSkillAssist);
        Check(result == (matches ? PHOrbSkillStarted : PHOrbSkillUnavailable) && skillStartCalls == unsigned(matches) && !writes,
            "unrelated body/assist request checks the naturally charged choice without changing skill ID or charge");
    }
    SkillFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{}; NSString *reason = nil;
    [engine readAutomation:&state frame:Frame() reason:&reason];
    Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillBody,CurrentFixtureSkill + 1} expected:&state frame:Frame() reason:&reason] == PHOrbSkillUnavailable && !skillStartCalls && !writes,
        "wrong expected skill ID refuses even a ready native skill");
    SkillFixture(); engine = [FixtureSkillEngine new]; [engine readAutomation:&state frame:Frame() reason:&reason];
    switchMultiInReadiness = true;
    Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillBody,CurrentFixtureSkill} expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected && !skillStartCalls && !writes,
        "source getter race into multiplayer cancels before selection preparation and activation");
}
static void TemporarySkillNativeTests() {
    const uintptr_t member = Impl + PHBattleMemberOffset;
    uint16_t original = 0, temporary = 0, assist = 0;
    PHBattleCatalogIDForNumber(14038, &original); PHBattleCatalogIDForNumber(4264, &temporary);
    PHBattleCatalogIDForNumber(14039, &assist);
    SkillFixture(); SetLeader(0, 4264);
    Put<uint16_t>(member + PHBattleBaseIDOffset, original);
    Put<uint16_t>(member + PHBattleAssistIDOffset, assist);
    Protected(member + PHBattleTempIDOffset, temporary, 0x6a875a8b);
    Protected(member + PHBattleTempTurnsOffset, 3, 0x6a875a8b);
    FixtureSkillEngine *engine = [FixtureSkillEngine new]; PHOrbSnapshot state{}, changed{}; NSString *reason = nil;
    Check([engine readAutomation:&state frame:Frame() reason:&reason] && state.automation.members[0].number == 4264 &&
        state.automation.members[0].identityNumber == 14038 && state.automation.members[0].identityAssist == assist &&
        state.automation.members[0].temporaryKnown && state.automation.members[0].temporaryTurns == 3 &&
        !state.automation.members[0].assistCatalogID && !canUseCalls && !readinessCalls && !skillStartCalls && !writes,
        "temporary body and underlying form/assist are decoded independently with no native getter or write");
    uint16_t base = 0; PHBattleCatalogIDForNumber(14037, &base);
    Put<uint16_t>(member + PHBattleBaseIDOffset, base);
    Protected(member + PHBattleFormOffset, original, 0x6a875a8b);
    Check([engine readAutomation:&changed frame:Frame() reason:&reason] && changed.automation.members[0].identityNumber == 14038,
        "persistent transformed form takes priority over the pre-transform base ID");
    Protected(member + PHBattleTempTurnsOffset, 2, 0x6a875a8b);
    Check([engine readAutomation:&changed frame:Frame() reason:&reason] && !PHOrbSameAutomation(&state,&changed) &&
        [engine useSkill:1 expected:&state frame:Frame() reason:&reason] == PHOrbSkillRejected && !canUseCalls && !writes,
        "changed temporary state invalidates even another slot's pending native activation");
    Protected(member + PHBattleTempIDOffset, original, 0x6a875a8b);
    Check(![engine readAutomation:&changed frame:Frame() reason:&reason], "inconsistent current and temporary IDs fail the automation read");
    Protected(member + PHBattleTempIDOffset, temporary, 0x6a875a8b);
    Put<uint32_t>(member + PHBattleTempTurnsOffset, 0);
    Check(![engine readAutomation:&changed frame:Frame() reason:&reason], "damaged protected temporary state cannot be interpreted as no transformation");
    Protected(member + PHBattleTempTurnsOffset, 0, 0x6a875a8b);
    SetLeader(0,14038); Put<uint16_t>(member + 1438,assist);
    Check([engine readAutomation:&changed frame:Frame() reason:&reason] && changed.automation.members[0].number == 14038 &&
        !changed.automation.members[0].temporaryTurns && changed.automation.members[0].identityAssist == assist,
        "expired native temporary duration resumes current body and assist even with retained temporary ID");
}
#include "orb_skill_choice_tests.inc"

static void SkillNativeTests() {
    TemporarySkillNativeTests();
    SkillSourceNativeTests();
    SkillOrbConditionTests();
    SkillChoiceNativeTests();
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
        PHOrbAssistSkillRVA, PHOrbBaseCooldownRVA, PHOrbTotalCooldownRVA, PHOrbSkillConditionRVA,
        PHOrbSkillOrbConditionRVA, PHOrbSkillCompositeRVA, PHOrbSkillTableLoadRVA, PHOrbSkillBoardSizeRVA,
        PHOrbCanChooseSkillRVA, PHOrbSkillChoiceSourceRVA, PHOrbSkillFamilyRVA, PHOrbSkillFamilyCompareRVA,
        PHOrbSkillChoiceDialogRVA, PHOrbSkillChoiceCheckRVA, PHOrbSkillBodyCheckRVA, PHOrbSkillCanUseSelectorRVA,
        PHOrbSkillChooseBodyRVA, PHOrbSkillChosenSourceRVA, PHOrbSkillSourceEffectsRVA}) {
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
