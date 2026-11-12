// Included inside PHOrbNative's anonymous namespace. All reads use the same
// protected-value unanimity checks as the orb/battle reader; no native calls.
bool SmallProtected(uintptr_t address, uint32_t key1, uint32_t key2, unsigned maximum, uint32_t &value) {
    return Decode(address, key1, key2, 0x6a875a8bu, value) && value <= maximum;
}

bool PositiveDuration(uint16_t value) {
    // Native durations use a signed ten-bit turn count, with flags above it.
    return int16_t(uint16_t(value << 6)) > 0;
}

#include "PHOrbSurvival.inc"
#include "PHOrbEnemyParts.inc"

bool ReadAutomationFields(PHOrbSnapshot &state, bool eight = false, NSString **reason = nullptr) {
    auto &out = state.automation;
    out.valid = false;
    auto fail = [&](NSString *field, int slot = -1) {
        if (reason) *reason = slot < 0 ? [NSString stringWithFormat:@"%@未通過讀取校驗，已停止", field] :
            [NSString stringWithFormat:@"第 %d 格%@未通過讀取校驗，已停止", slot + 1, field];
        return false;
    };
    const uintptr_t header = state.identity.header, impl = state.identity.impl, save = header + PHSaveRVA;
    uint32_t key1 = 0, key2 = 0, skillUses = 0, enemies = 0, catalogCount = 0, hp = 0;
    uintptr_t catalog = 0;
    uint32_t records = 0;
    if (!PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
        !SmallProtected(impl + PHOrbSkillUsesOffset, key1, key2, 255, skillUses) ||
        !Decode(impl + PHOrbEnemyCountOffset, key1, key2, 0x766cfc6du, enemies) || enemies > 8 ||
        !Decode(impl + PHBattleHPOffset, key1, key2, 0x766cfc6du, hp) ||
        !PHReadU32(save + PHBattleCatalogCountOffset, &catalogCount) || !catalogCount || catalogCount > 65536 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &catalog) || !catalog ||
        catalog > UINTPTR_MAX - catalogCount * PHBattleCatalogStride) return fail(@"技能次數／敵人數／HP／圖鑑資料");
    records = catalogCount;
    if (eight && (!PHReadU32(save + PHOrbLeaderCatalogRecordsOffset, &records) || records < catalogCount ||
        records > 0x20000 || catalog > UINTPTR_MAX - size_t(records) * PHBattleCatalogStride)) return fail(@"8 人圖鑑資料");
    out.skillUses = uint16_t(skillUses);
    out.window &= hp > 0;
    if (!hp) out.windowBlocks |= PHOrbSkillWindowHP;
    for (unsigned i = 0; i < 6; ++i) {
        const uintptr_t member = impl + PHBattleMemberOffset + i * PHBattleMemberStride;
        auto &m = out.members[i];
        uint8_t flags = 0; uint32_t nativeState = 0, record = 0, bind = 0, charge = 0, shadow = 0, overrideID = 0, assistState = 0;
        uintptr_t parameter = 0;
        if (!PHReadU8(member, &flags) || !PHReadU32(member + PHOrbSkillMemberStateOffset, &nativeState) || nativeState > 7)
            return fail(@"隊員動作狀態", i);
        m.state = uint8_t(nativeState); out.busy |= nativeState != 0;
        m.active = (flags & 1u) != 0;
        if (!m.active) continue;
        PHOrbLeaderReadDiagnostics diagnostic{};
        PHBattleMemberIdentity identity{};
        if (!PHReadBattleMemberIdentity(member, key1, key2, identity)) return fail(@"本體／武裝身分", i);
        const uint32_t id = identity.currentCatalogID;
        if (!id || id > INT32_MAX || (!eight && id >= catalogCount) ||
            !PHBattleNumberForCatalogID(eight ? id % 1000000u : id, &m.number) ||
            !PHReadPointer(member + PHBattleCardParamOffset, &parameter) ||
            (eight ? !LeaderParameterMatches(catalog, catalogCount, records, id, parameter, diagnostic) :
                parameter != catalog + id * PHBattleCatalogStride) || !PHReadU32(parameter, &record) || record != id)
            return fail(@"本體圖鑑資料", i);
        if (!SmallProtected(member + PHBattleChargeOffset, key1, key2, UINT16_MAX, charge) ||
            !PHReadU32(member + PHBattleChargeShadowOffset, &shadow)) return fail(@"蓄技受保護資料", i);
        const uint32_t recordedCharge = shadow ^ 0x68fb65ceu;
        // JP 23.6.1: 0x10056A118 caps +456 at body CD after assist loss,
        // without changing +1512. Rebuild (0x1005A1588) and later assist
        // restoration do not repair that bookkeeping copy. 0x100569F9C
        // increments both independently, saturating at 9999. It can remain
        // above the unanimous protected charge until a native reset/delay.
        // Use only +456 for readiness. Never restore charge from the copy,
        // invoke a getter to normalize it, or weaken the separate 8P gate.
        if (recordedCharge != charge && (eight || recordedCharge < charge || recordedCharge > 9999))
            return fail([NSString stringWithFormat:@"蓄技資料（目前 %u，副本 %u）", charge, recordedCharge], i);
        if (!SmallProtected(member + PHOrbSkillBindOffset, key1, key2, UINT8_MAX, bind) ||
            !PHReadU32(member + PHOrbSkillOverrideOffset, &overrideID) ||
            !SmallProtected(member + PHBattleAssistStateOffset, key1, key2, UINT16_MAX, assistState))
            return fail(@"封鎖／技能選擇／武裝保留狀態", i);
        m.bound = int8_t(uint8_t(bind)) > 0; m.charge = uint16_t(charge);
        m.assistState = uint16_t(assistState);
        if (m.bound) out.playerEffects |= PHOrbPlayerMemberBind;
        m.assistCatalogID = identity.effectiveAssistID;
        m.sourceCatalogIDs[0] = identity.sourceBodyID; m.sourceCatalogIDs[1] = identity.sourceAssistID;
        m.temporaryID = identity.temporaryFormID; m.temporaryTurns = identity.temporaryTurns;
        m.identityNumber = m.number; m.identityAssist = m.assistCatalogID;
        if (identity.temporaryTurns) {
            // Keep the old source re-read at the temporary-form boundary.
            // Compare the complete shared identity so none of its fields can
            // come from a different form transition than the catalog check.
            PHBattleMemberIdentity current{};
            uint32_t underlying = 0; uint16_t assist = 0, ignored = 0;
            if (!PHReadBattleMemberIdentity(member, key1, key2, current) || !PHBattleSameMemberIdentity(&identity, &current) ||
                !PHBattlePlanningIdentity(&identity, &underlying, &assist) || underlying >= catalogCount ||
                !PHBattleNumberForCatalogID(underlying, &m.identityNumber) ||
                (assist && (assist >= catalogCount || !PHBattleNumberForCatalogID(assist, &ignored))))
                return fail(@"暫時變身身分", i);
            m.identityAssist = assist;
        }
        m.temporaryKnown = true;
        // Keep authoritative stages/use counts and the derived stage cache
        // separately: can-use refreshes the cache only after basic readiness.
        for (unsigned skill = 0; skill < 2; ++skill) {
            uint32_t stage = 0, uses = 0;
            if (!SmallProtected(member + 344 + 16 * skill, key1, key2, UINT8_MAX, stage) ||
                !SmallProtected(member + 488 + 16 * skill, key1, key2, UINT16_MAX, uses) ||
                !PHReadU8(member + 1290 + skill, &m.cachedSkillStage[skill]) ||
                !PHReadU16(member + (skill ? PHBattleEffectiveAssistOffset + PHBattleAssistSkillLevelByte : 1298),
                           &m.skillLevel[skill])) return fail(skill ? @"武裝技能階段／次數／等級" : @"本體技能階段／次數／等級", i);
            m.skillStage[skill] = uint8_t(stage); m.skillUses[skill] = uint16_t(uses);
        }
        float motion = 0, target = 0;
        if (!PHReadMemory(member + 1116, &motion, sizeof(motion)) || !PHReadMemory(member + 1120, &target, sizeof(target)) ||
            !std::isfinite(motion) || !std::isfinite(target)) return fail(@"隊員動畫資料", i);
        // Native preparation clears an old choice when starting a new use.
        // Keep its exact value for race checks; state, not this retained ID,
        // tells us whether a selection dialog or skill is still in progress.
        m.selection = overrideID;
        out.window &= motion == target;
        if (motion != target) out.windowBlocks |= PHOrbSkillWindowMemberAnimation;
        out.busy |= motion != target;
    }
    uint16_t bindTurns = 0, sealTurns = 0, special = 0, timed = 0;
    uint8_t movement = 0, enemyTurn = 0;
    uint32_t specialMode = 0, skillLimit = 0, limitRemaining = 0, inputMode = 0;
    float animation = 0, animationTarget = 0;
    uintptr_t dungeonRules = 0;
    if (!PHReadU16(impl + PHOrbSkillSealOffset, &bindTurns) || !PHReadU16(impl + PHOrbSkillSealExtraOffset, &sealTurns) ||
        !PHReadU16(impl + PHOrbSkillSpecialOffset, &special) || !PHReadU16(impl + PHOrbSkillTimedOffset, &timed) ||
        !PHReadU8(impl + PHOrbSkillMovementOffset, &movement) || !PHReadU8(impl + 600488, &enemyTurn) ||
        !PHReadU32(impl + PHOrbSkillSpecialModeOffset, &specialMode) || !PHReadU32(impl + 601584, &inputMode) ||
        !Decode(impl + PHOrbSkillLimitOffset, key1, key2, 0x766cfc6du, skillLimit) ||
        !SmallProtected(impl + PHOrbSkillLimitRemainingOffset, key1, key2, UINT16_MAX, limitRemaining) ||
        !PHReadMemory(impl + PHOrbSkillAnimationOffset, &animation, sizeof(animation)) ||
        !PHReadMemory(impl + PHOrbSkillAnimationTargetOffset, &animationTarget, sizeof(animationTarget)) ||
        !std::isfinite(animation) || !std::isfinite(animationTarget) ||
        !PHReadPointer(impl + PHOrbSkillDungeonRulesOffset, &dungeonRules)) return fail(@"技能操作窗口資料");
    out.window &= !PositiveDuration(bindTurns) && !PositiveDuration(sealTurns) && int16_t(special) < 0 &&
        !(movement & 1u) && !enemyTurn && !inputMode && specialMode != 0x12345678 &&
        (int32_t(skillLimit) < 0 || int16_t(limitRemaining) > 0) && (int16_t(timed) < 1 || animation == animationTarget);
    if (PositiveDuration(bindTurns) || PositiveDuration(sealTurns)) out.windowBlocks |= PHOrbSkillWindowSeal;
    if (int16_t(special) >= 0) out.windowBlocks |= PHOrbSkillWindowSpecial;
    if (movement & 1u) out.windowBlocks |= PHOrbSkillWindowMovement;
    if (enemyTurn) out.windowBlocks |= PHOrbSkillWindowEnemyTurn;
    if (inputMode) out.windowBlocks |= PHOrbSkillWindowInput;
    if (specialMode == 0x12345678) out.windowBlocks |= PHOrbSkillWindowSpecialMode;
    if (int32_t(skillLimit) >= 0 && int16_t(limitRemaining) <= 0) out.windowBlocks |= PHOrbSkillWindowLimit;
    if (int16_t(timed) >= 1 && animation != animationTarget) out.windowBlocks |= PHOrbSkillWindowTimedAnimation;
    out.busy |= int16_t(special) >= 0 || (movement & 1u) || enemyTurn || inputMode || specialMode == 0x12345678 ||
        (int16_t(timed) >= 1 && animation != animationTarget);
    uint8_t tutorial = 0, tutorialStep = 0;
    if (!PHReadU8(header + PHOrbTutorialIndexRVA, &tutorial)) return fail(@"教學狀態");
    if (int8_t(tutorial) >= 0) {
        if (!PHReadU8(header + PHOrbTutorialTableRVA + 8u * tutorial + 2, &tutorialStep)) return fail(@"教學步驟");
        out.window &= tutorialStep != 75; // Native tap handler restricts this tutorial step to its leader.
        if (tutorialStep == 75) out.windowBlocks |= PHOrbSkillWindowTutorial;
    }
    if (dungeonRules) {
        uint8_t rules = 0;
        if (dungeonRules > UINTPTR_MAX - 33 || !PHReadU8(dungeonRules + 32, &rules)) return fail(@"地下城技能限制");
        out.window &= !(rules & 0x40u);
        if (rules & 0x40u) out.windowBlocks |= PHOrbSkillWindowDungeon;
    }
    if (PositiveDuration(bindTurns) || PositiveDuration(sealTurns)) out.playerEffects |= PHOrbPlayerSkillSeal;
    if (!eight) ReadSurvival(state, dungeonRules, key1, key2, hp);
    // No single-player derived maximum HP or thorn budget is reused in 8P.
    out.enemiesKnown = true;
    // Mapping verified in 0x100562F74. These are enemy effects, distinct from
    // the player's bypass durations below. Parts use separate HP. Including
    // their effects is conservative: native 0x100562F74 can bypass these for
    // positive part mode, depending on the attack; we never assume that bypass.
    constexpr uintptr_t durationOffsets[]{2400, 2192, 2256, 2224};
    constexpr uintptr_t bypassOffsets[]{PHOrbDamageAbsorbBypassOffset, PHOrbAttributeAbsorbBypassOffset,
                                       PHOrbDamageVoidBypassOffset, PHOrbEnemyComboBypassOffset};
    bool partsCodeChecked = false;
    for (unsigned i = 0; i < enemies; ++i) {
        uintptr_t enemy = impl + PHOrbEnemyOffset + i * PHOrbEnemyStride;
        uint16_t flags = 0; uint32_t low = 0, high = 0; uint8_t action = 0;
        if (!PHReadU16(enemy + 532, &flags)) return fail(@"敵人旗標");
        if (flags & 0x100u) {
            out.partsMask |= uint8_t(1u << i);
            if (eight || (!partsCodeChecked && !CheckEnemyPartsCode(header)) ||
                !ReadEnemyPart(impl, enemies, i, key1, key2, out.parts[i])) {
                out.enemiesKnown = false; out.window = false;
                out.windowBlocks |= PHOrbSkillWindowEnemyParts;
                continue;
            }
            partsCodeChecked = true;
            if (!out.parts[i].hp || out.parts[i].hp > INT64_MAX) continue;
        } else {
            if (!Decode(enemy + 64, key1, key2, 0x766cfc6du, low) ||
                !Decode(enemy + 80, key1, key2, 0x766cfc6du, high)) return fail(@"敵人 HP");
            if (int64_t((uint64_t(high) << 32) | low) <= 0) continue;
        }
        ++out.activeEnemies;
        // 0x1005B0268 checks action only for living ordinary enemies.
        if (!(flags & 0x100u)) {
            if (!PHReadU8(enemy + 23, &action)) return fail(@"敵人動作狀態");
            out.window &= int8_t(action) < 0;
            if (int8_t(action) >= 0) out.windowBlocks |= PHOrbSkillWindowEnemyAction;
            out.busy |= int8_t(action) >= 0;
        }
        for (unsigned effect = 0; effect < 4; ++effect) {
            uint32_t duration = 0, mask = 0;
            if (!SmallProtected(enemy + durationOffsets[effect], key1, key2, UINT16_MAX, duration)) return fail(@"敵人效果回合數");
            if (int16_t(duration) < 1) continue;
            if (effect == 1 || effect == 2) {
                if (!SmallProtected(enemy + (effect == 1 ? 2176 : 2240), key1, key2, UINT16_MAX, mask)) return fail(@"敵人效果屬性");
                if (!(mask & 31u)) continue;
            }
            out.enemyEffects |= 1u << effect;
        }
    }
    out.window &= out.activeEnemies > 0;
    if (!out.activeEnemies) out.windowBlocks |= PHOrbSkillWindowNoEnemy;
    out.busy |= !out.activeEnemies || !hp;
    for (unsigned effect = 0; effect < 4; ++effect) {
        uint16_t turns = 0, delay = 0;
        if (!PHReadU16(impl + bypassOffsets[effect], &turns) || !PHReadU16(impl + bypassOffsets[effect] + 2, &delay))
            return fail(@"玩家對策效果回合數");
        if (PositiveDuration(turns) && !PositiveDuration(delay)) out.playerBypass |= 1u << effect;
    }
    out.boardKnown = state.phase == 2 && !state.boardTransition && PHOrbDimensionsSupported(state.board.width, state.board.height);
    for (unsigned i = 0; out.boardKnown && i < state.board.width * state.board.height; ++i)
        if (state.board.types[i] > 9) out.boardKnown = false;
    uint32_t lastKey1 = 0, lastKey2 = 0; uintptr_t lastImpl = 0;
    if (!PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2 ||
        !PHReadPointer(state.identity.game + 0x70, &lastImpl) || lastImpl != impl) return fail(@"讀取期間金鑰／戰鬥物件一致性");
    out.valid = true;
    out.windowBlocksKnown = true;
    return true;
}
