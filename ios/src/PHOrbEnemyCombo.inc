// Included inside PHOrbNative's anonymous namespace. No native calls/writes.
bool CheckEnemyComboCode(uintptr_t header) {
    for (const auto &guard : PHOrbEnemyComboTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}

bool CollectEnemyCombo(const PHOrbSnapshot &state, uint32_t key1, uint32_t key2, PHOrbEnemyComboState &out) {
    // Bonus evidence is compared byte-for-byte, including its padding.
    std::memset(&out,0,sizeof(out));
    const uintptr_t impl = state.identity.impl;
    uint32_t count = 0, adjustment = 0, subtract = 0;
    if (!Decode(impl + PHOrbEnemyCountOffset, key1, key2, 0x766cfc6du, count) || !count || count > 8 ||
        !PHReadU16(impl + PHOrbEnemyComboBypassOffset, &out.bypassTurns) ||
        !PHReadU16(impl + PHOrbEnemyComboBypassOffset + 2, &out.bypassDelay) ||
        !PHReadU16(impl + PHOrbEnemyComboAdjustmentOffset + 16,&out.adjustmentTurns) ||
        !PHReadU16(impl + PHOrbEnemyComboSubtractOffset,&out.subtractTurns)) return false;
    if (PositiveDuration(out.adjustmentTurns)) {
        if (!Decode(impl + PHOrbEnemyComboAdjustmentOffset,key1,key2,0x6a875a8bu,adjustment)) return false;
        out.adjustment = int8_t(adjustment);
        if (adjustment != uint8_t(out.adjustment) && adjustment != uint32_t(int32_t(out.adjustment))) return false;
    }
    if (PositiveDuration(out.subtractTurns)) {
        if (!SmallProtected(impl + PHOrbEnemyComboSubtractOffset + 4,key1,key2,UINT16_MAX,subtract)) return false;
        out.subtract = uint16_t(subtract);
    }
    out.enemyCount = uint8_t(count);
    unsigned alive = 0; bool absorption = false, partsCodeChecked = false;
    for (unsigned slot = 0; slot < count; ++slot) {
        const uintptr_t enemy = impl + PHOrbEnemyOffset + slot * PHOrbEnemyStride;
        uint32_t low = 0, high = 0, turns = 0, threshold = 0, upper = 0; uint8_t action = 0;
        if (!PHReadU16(enemy + 532, &out.flags[slot])) return false;
        if (out.flags[slot] & 0x100u) {
            if ((!partsCodeChecked && !CheckEnemyPartsCode(state.identity.header)) ||
                !ReadEnemyPart(impl, count, slot, key1, key2, out.parts[slot]) ||
                !(state.automation.partsMask & (1u << slot)) ||
                std::memcmp(&out.parts[slot], &state.automation.parts[slot], sizeof(out.parts[slot]))) return false;
            partsCodeChecked = true; out.hp[slot] = out.parts[slot].hp;
        } else {
            if ((state.automation.partsMask & (1u << slot)) ||
                !Decode(enemy + 64, key1, key2, 0x766cfc6du, low) ||
                !Decode(enemy + 80, key1, key2, 0x766cfc6du, high)) return false;
            out.hp[slot] = (uint64_t(high) << 32) | low;
        }
        if (!out.hp[slot] || out.hp[slot] > INT64_MAX) continue;
        ++alive; out.aliveMask |= uint8_t(1u << slot);
        if (!PHReadU8(enemy + 23, &action) || int8_t(action) >= 0 ||
            !SmallProtected(enemy + PHOrbEnemyComboDurationOffset, key1, key2, UINT16_MAX, turns) ||
            !SmallProtected(enemy + PHOrbEnemyComboUpperDurationOffset,key1,key2,UINT16_MAX,upper)) return false;
        out.upperTurns[slot] = uint16_t(upper);
        if (int16_t(turns) > 0 &&
            !SmallProtected(enemy + PHOrbEnemyComboThresholdOffset, key1, key2, UINT16_MAX, threshold)) return false;
        absorption |= int16_t(turns) > 0;
        out.turns[slot] = uint16_t(turns); out.thresholds[slot] = uint16_t(threshold);
    }
    if (!alive || !state.automation.enemiesKnown || alive != state.automation.activeEnemies ||
        absorption != ((state.automation.enemyEffects & PHOrbEnemyComboAbsorb) != 0) ||
        ((state.automation.playerBypass & PHOrbEnemyComboAbsorb) != 0) !=
            (PositiveDuration(out.bypassTurns) && !PositiveDuration(out.bypassDelay))) return false;
    PHOrbComboBonusState bonus{};
    if (state.multiplayer.kind == PHOrbMultiplayerNone && CollectComboBonus(state,key1,key2,bonus))
        std::memcpy(&out.bonus,&bonus,sizeof(bonus));
    out.valid = true;
    return true;
}

bool ReadEnemyComboFields(PHOrbSnapshot &state) {
    state.enemyCombo = {};
    if (!state.valid || !state.present || !state.automation.valid || state.automation.busy ||
        state.phase != 2 || state.boardTransition || (!state.settled && !state.dragged) || !PHInputIsQuiet(&state.input) ||
        state.multiplayerDisabled || state.multiplayer.kind != PHOrbMultiplayerNone || state.identity.sourceMode != 0 ||
        !PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer)) return false;
    PHOrbSnapshot first = state, second = state;
    first.automation = {}; second.automation = {};
    first.automation.window = second.automation.window = true;
    const uintptr_t header = state.identity.header, impl = state.identity.impl;
    uint32_t key1 = 0, key2 = 0, lastKey1 = 0, lastKey2 = 0, phase = 0, wave = 0, moves = 0;
    uintptr_t dragged = 0;
    if (!ConfirmOrbEntryIdentity(state.identity) ||
        !PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
        !ReadAutomationFields(first) || first.automation.busy || !PHOrbSameAutomationExceptWindow(&state, &first) ||
        !CollectEnemyCombo(first, key1, key2, first.enemyCombo) ||
        !ReadAutomationFields(second) || second.automation.busy || !PHOrbSameAutomation(&first, &second) ||
        !CollectEnemyCombo(second, key1, key2, second.enemyCombo) || !PHOrbSameEnemyCombo(&first.enemyCombo, &second.enemyCombo) ||
        !ConfirmOrbEntryIdentity(state.identity) ||
        !PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2 ||
        !Decode(impl + PHBattlePhaseOffset, key1, key2, 0x766cfc6du, phase) || phase != state.phase ||
        !Decode(impl + PHBattleWaveOffset, key1, key2, 0x766cfc6du, wave) || wave != state.wave ||
        !PHReadU32(impl + PHOrbMoveSerialOffset, &moves) || moves != state.moves ||
        !PHReadPointer(impl + PHBattleDragPointerOffset, &dragged) || dragged != state.dragged) return false;
    state.enemyCombo = first.enemyCombo;
    return true;
}
