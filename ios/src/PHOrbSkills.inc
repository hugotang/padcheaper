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

bool ReadAutomationFields(PHOrbSnapshot &state) {
    auto &out = state.automation;
    const uintptr_t header = state.identity.header, impl = state.identity.impl, save = header + PHSaveRVA;
    uint32_t key1 = 0, key2 = 0, skillUses = 0, enemies = 0, catalogCount = 0, hp = 0;
    uintptr_t catalog = 0;
    if (!PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
        !SmallProtected(impl + PHOrbSkillUsesOffset, key1, key2, 255, skillUses) ||
        !Decode(impl + PHOrbEnemyCountOffset, key1, key2, 0x766cfc6du, enemies) || enemies > 8 ||
        !Decode(impl + PHBattleHPOffset, key1, key2, 0x766cfc6du, hp) ||
        !PHReadU32(save + PHBattleCatalogCountOffset, &catalogCount) || !catalogCount || catalogCount > 65536 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &catalog) || !catalog ||
        catalog > UINTPTR_MAX - catalogCount * PHBattleCatalogStride) return false;
    out.skillUses = uint16_t(skillUses);
    out.window &= hp > 0;
    for (unsigned i = 0; i < 6; ++i) {
        const uintptr_t member = impl + PHBattleMemberOffset + i * PHBattleMemberStride;
        auto &m = out.members[i];
        uint8_t flags = 0; uint32_t nativeState = 0, id = 0, record = 0, bind = 0, charge = 0, shadow = 0, overrideID = 0;
        uintptr_t parameter = 0;
        if (!PHReadU8(member, &flags) || !PHReadU32(member + PHOrbSkillMemberStateOffset, &nativeState) || nativeState > 7) return false;
        m.state = uint8_t(nativeState); out.busy |= nativeState != 0;
        m.active = (flags & 1u) != 0;
        if (!m.active) continue;
        if (!PHReadU32(member + PHBattleCatalogIDOffset, &id) || !id || id >= catalogCount ||
            !PHBattleNumberForCatalogID(id, &m.number) || !PHReadPointer(member + PHBattleCardParamOffset, &parameter) ||
            parameter != catalog + id * PHBattleCatalogStride || !PHReadU32(parameter, &record) || record != id ||
            !SmallProtected(member + PHOrbSkillBindOffset, key1, key2, UINT8_MAX, bind) ||
            !SmallProtected(member + PHBattleChargeOffset, key1, key2, UINT16_MAX, charge) ||
            !PHReadU32(member + PHBattleChargeShadowOffset, &shadow) || (shadow ^ charge) != 0x68fb65ceu ||
            !PHReadU32(member + PHOrbSkillOverrideOffset, &overrideID)) return false;
        m.bound = int8_t(uint8_t(bind)) > 0; m.charge = uint16_t(charge);
        if (m.bound) out.playerEffects |= PHOrbPlayerMemberBind;
        // These are decoded by native can-use before it refreshes CardParam.
        for (uintptr_t offset : {uintptr_t(344), uintptr_t(360), uintptr_t(488), uintptr_t(504)}) {
            uint32_t value = 0;
            if (!SmallProtected(member + offset, key1, key2, offset < 400 ? UINT8_MAX : UINT16_MAX, value)) return false;
        }
        float motion = 0, target = 0;
        if (!PHReadMemory(member + 1116, &motion, sizeof(motion)) || !PHReadMemory(member + 1120, &target, sizeof(target)) ||
            !std::isfinite(motion) || !std::isfinite(target)) return false;
        // A retained manual skill choice blocks only that member. A moving
        // team animation still prevents automation from acting on any slot.
        m.overridden = overrideID != UINT32_MAX;
        out.window &= motion == target;
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
        !PHReadMemory(impl + 751400, &animation, sizeof(animation)) ||
        !PHReadMemory(impl + 751404, &animationTarget, sizeof(animationTarget)) ||
        !std::isfinite(animation) || !std::isfinite(animationTarget) ||
        !PHReadPointer(impl + PHOrbSkillDungeonRulesOffset, &dungeonRules)) return false;
    out.window &= !PositiveDuration(bindTurns) && !PositiveDuration(sealTurns) && int16_t(special) < 0 &&
        !(movement & 1u) && !enemyTurn && !inputMode && specialMode != 0x12345678 &&
        (int32_t(skillLimit) < 0 || int16_t(limitRemaining) > 0) && (int16_t(timed) < 1 || animation == animationTarget);
    out.busy |= int16_t(special) >= 0 || (movement & 1u) || enemyTurn || inputMode || specialMode == 0x12345678 ||
        (int16_t(timed) >= 1 && animation != animationTarget);
    uint8_t tutorial = 0, tutorialStep = 0;
    if (!PHReadU8(header + PHOrbTutorialIndexRVA, &tutorial)) return false;
    if (int8_t(tutorial) >= 0) {
        if (!PHReadU8(header + PHOrbTutorialTableRVA + 8u * tutorial + 2, &tutorialStep)) return false;
        out.window &= tutorialStep != 75; // Native tap handler restricts this tutorial step to its leader.
    }
    if (dungeonRules) {
        uint8_t rules = 0;
        if (dungeonRules > UINTPTR_MAX - 33 || !PHReadU8(dungeonRules + 32, &rules)) return false;
        out.window &= !(rules & 0x40u);
    }
    if (PositiveDuration(bindTurns) || PositiveDuration(sealTurns)) out.playerEffects |= PHOrbPlayerSkillSeal;
    ReadSurvival(state, dungeonRules, key1, key2, hp);
    out.enemiesKnown = true;
    // Mapping verified in 0x100562F74. These are enemy effects, distinct from
    // the player's bypass durations below. Only living ordinary enemies count.
    constexpr uintptr_t durationOffsets[]{2400, 2192, 2256, 2224};
    constexpr uintptr_t bypassOffsets[]{739196, 739156, 739668, 739176};
    for (unsigned i = 0; i < enemies; ++i) {
        uintptr_t enemy = impl + PHOrbEnemyOffset + i * PHOrbEnemyStride;
        uint16_t flags = 0; uint32_t low = 0, high = 0; uint8_t action = 0;
        if (!PHReadU16(enemy + 532, &flags)) return false;
        if (flags & 0x100u) { out.enemiesKnown = false; out.window = false; continue; } // Enemy parts not yet modeled.
        if (!Decode(enemy + 64, key1, key2, 0x766cfc6du, low) ||
            !Decode(enemy + 80, key1, key2, 0x766cfc6du, high)) return false;
        if (int64_t((uint64_t(high) << 32) | low) <= 0) continue;
        ++out.activeEnemies;
        if (!PHReadU8(enemy + 23, &action)) return false;
        out.window &= int8_t(action) < 0;
        out.busy |= int8_t(action) >= 0;
        for (unsigned effect = 0; effect < 4; ++effect) {
            uint32_t duration = 0, mask = 0;
            if (!SmallProtected(enemy + durationOffsets[effect], key1, key2, UINT16_MAX, duration)) return false;
            if (int16_t(duration) < 1) continue;
            if (effect == 1 || effect == 2) {
                if (!SmallProtected(enemy + (effect == 1 ? 2176 : 2240), key1, key2, UINT16_MAX, mask)) return false;
                if (!(mask & 31u)) continue;
            }
            out.enemyEffects |= 1u << effect;
        }
    }
    out.window &= out.activeEnemies > 0;
    out.busy |= !out.activeEnemies || !hp;
    for (unsigned effect = 0; effect < 4; ++effect) {
        uint16_t turns = 0, delay = 0;
        if (!PHReadU16(impl + bypassOffsets[effect], &turns) || !PHReadU16(impl + bypassOffsets[effect] + 2, &delay)) return false;
        if (PositiveDuration(turns) && !PositiveDuration(delay)) out.playerBypass |= 1u << effect;
    }
    out.boardKnown = state.phase == 2 && PHOrbDimensionsSupported(state.board.width, state.board.height);
    for (unsigned i = 0; out.boardKnown && i < state.board.width * state.board.height; ++i)
        if (state.board.types[i] > 9) out.boardKnown = false;
    uint32_t lastKey1 = 0, lastKey2 = 0; uintptr_t lastImpl = 0;
    if (!PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2 ||
        !PHReadPointer(state.identity.game + 0x70, &lastImpl) || lastImpl != impl) return false;
    out.valid = true;
    return true;
}
