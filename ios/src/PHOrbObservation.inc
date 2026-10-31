// Optional current-state diagnostics. No native function is called here,
// including getters that initialize the game's dungeon cooldown cache.
// This file is included inside PHOrbNative's anonymous namespace.
struct ObservationTables {
    uintptr_t catalog = 0, skills = 0;
    uint32_t catalogCount = 0, skillCount = 0;
};

bool CheckObservationCode(uintptr_t header) {
    for (const auto &guard : PHOrbObservationTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}

bool ReadObservationTables(uintptr_t header, ObservationTables &tables) {
    const uintptr_t save = header + PHSaveRVA;
    return PHReadPointer(save + PHBattleCatalogPointerOffset, &tables.catalog) && tables.catalog &&
        PHReadU32(save + PHBattleCatalogCountOffset, &tables.catalogCount) &&
        tables.catalogCount && tables.catalogCount <= 65536 &&
        tables.catalog <= UINTPTR_MAX - size_t(tables.catalogCount) * PHBattleCatalogStride &&
        PHReadPointer(save + PHBattleSkillPointerOffset, &tables.skills) && tables.skills && !(tables.skills & 3u) &&
        PHReadU32(save + PHOrbSkillCountOffset, &tables.skillCount) &&
        tables.skillCount && tables.skillCount <= 0x20000 &&
        tables.skills <= UINTPTR_MAX - size_t(tables.skillCount) * sizeof(SkillDiagnosticRecord);
}

bool ObservationCard(const ObservationTables &tables, uint32_t id, uintptr_t &parameter) {
    uint32_t actual = 0;
    if (!id || id >= tables.catalogCount) return false;
    parameter = tables.catalog + size_t(id) * PHBattleCatalogStride;
    return PHReadU32(parameter, &actual) && actual == id;
}

bool ObservationSkillRecord(const ObservationTables &tables, uint32_t id, SkillDiagnosticRecord &record) {
    return id < tables.skillCount && PHReadMemory(tables.skills + size_t(id) * sizeof(record), &record, sizeof(record));
}

bool ObservationSkill(const ObservationTables &tables, uintptr_t parameter, uint8_t stage, uint16_t level,
    uint32_t &rootID, uint32_t &skillID, uint16_t &cooldown, bool &cooldownKnown) {
    SkillDiagnosticRecord root{};
    if (!PHReadU32(parameter + 84, &rootID) || !ObservationSkillRecord(tables, rootID, root)) return false;
    skillID = rootID;
    SkillDiagnosticRecord cdRecord = root;
    bool levelAdjusted = true;
    if (root.type == 232 || root.type == 233) {
        unsigned count = 0;
        while (count < 8 && root.params[count]) {
            if (root.params[count] < 0 || uint32_t(root.params[count]) >= tables.skillCount) return false;
            ++count;
        }
        if (!count) return false;
        const unsigned index = stage < count ? stage : root.type == 233 ? 0 : count - 1;
        skillID = uint32_t(root.params[index]);
        SkillDiagnosticRecord child{};
        if (!ObservationSkillRecord(tables, skillID, child)) return false;
        // Type 233 resets to root CD at/after the end of its cycle. Type
        // 232 stays on its last child. Child stages use their base CD only.
        if (stage && (root.type != 233 || stage < count)) { cdRecord = child; levelAdjusted = false; }
    }
    const int64_t cd = !cdRecord.maxLevel ? 0 : !levelAdjusted ? cdRecord.cooldown :
        int64_t(cdRecord.cooldown) + (int64_t(std::min(cdRecord.maxLevel, level)) - 1) * cdRecord.levelCooldownDelta;
    cooldownKnown = cd >= 0 && cd <= UINT16_MAX;
    if (cooldownKnown) cooldown = uint16_t(cd);
    return true;
}

unsigned ObservationLWeight(unsigned awakening) { return awakening == 60 ? 1 : awakening == 108 ? 2 : 0; }

bool ObservationLatents(uintptr_t profile, uint8_t &count) {
    count = 0;
    if (!profile) return true;
    uint64_t encoded = 0;
    if (profile > UINTPTR_MAX - 20 || !PHReadMemory(profile + 4, &encoded, sizeof(encoded))) return false;
    unsigned entries = unsigned(encoded & 15u);
    if (entries == 15) entries = unsigned((encoded >> 4) & 15u);
    entries = std::min(entries, 8u);
    uint64_t remaining = encoded >> 8;
    for (unsigned i = 0; i < entries; ++i) {
        const unsigned latent = unsigned(remaining & 127u);
        if (latent > 52) return false; // Unknown IDs are not evidence of no recovery latent.
        count += latent == 48;
        // The native decoder skips occupied slots but iterates the encoded
        // entry count. Repeated six-slot bits are not six separate latents.
        const unsigned cost = latent < 12 ? 1 :
            latent <= 51 && ((UINT64_C(0x3c7e00000e000) >> latent) & 1u) ? 6 : latent == 51 ? 4 : 2;
        remaining >>= cost * 7;
    }
    return true;
}

bool ObservationAttributes(uintptr_t member, uintptr_t parameter, uint8_t &colors) {
    uint8_t overrides[2]{}, raw[3]{};
    if (!PHReadMemory(member + 1287, overrides, sizeof(overrides)) ||
        !PHReadU8(parameter + 12, &raw[0]) || !PHReadU8(parameter + 204, &raw[1]) ||
        !PHReadU8(parameter + 216, &raw[2])) return false;
    const int main = int8_t(overrides[0]), sub = int8_t(overrides[1]);
    const int attrs[]{main < 0 ? int8_t(raw[0]) : main,
        sub >= 0 ? sub : int8_t(raw[1]) < 0 ? -1 : main >= 0 ? main : int8_t(raw[1]),
        int8_t(raw[2]) < 0 ? -1 : main >= 0 ? main : int8_t(raw[2])};
    if (attrs[0] < 0 || attrs[0] >= 5) return false;
    colors = 0;
    for (int attr : attrs) {
        if (attr >= 5) return false;
        if (attr >= 0) colors |= uint8_t(1u << attr);
    }
    return colors != 0;
}

bool ObservationLAwakenings(uintptr_t member, uintptr_t parameter, uintptr_t profile,
    const ObservationTables &tables, const PHOrbSkillMember &skill, uint32_t key1, uint32_t key2,
    unsigned &count) {
    uint8_t unlocked = 0;
    uint16_t awakenings[10]{};
    if (!PHReadU8(member + 1286, &unlocked) || int8_t(unlocked) < 0 ||
        !PHReadMemory(parameter + 748, awakenings, sizeof(awakenings))) return false;
    count = 0;
    for (unsigned i = 0; i < std::min(unsigned(unlocked), 10u); ++i) count += ObservationLWeight(awakenings[i]);
    if (profile) {
        uint32_t superAwakening = 0; uint16_t level = 0, superCount = 0, sync = 0;
        if (profile > UINTPTR_MAX - 20 || !PHReadU32(profile + 16, &superAwakening) ||
            !PHReadU16(member + 1296, &level) || !PHReadU16(parameter + 782, &superCount) ||
            !PHReadU16(parameter + 788, &sync)) return false;
        if (superAwakening >= 116 && superAwakening <= 120) superAwakening -= 94;
        if (level >= 100 || (!superCount && sync && superAwakening == sync)) count += ObservationLWeight(superAwakening);
    }
    if (int16_t(skill.assistCatalogID) > 0) {
        uintptr_t assist = 0; uint8_t available = 0;
        if (!ObservationCard(tables, skill.assistCatalogID, assist) || !PHReadU8(member + 1440, &available) ||
            !PHReadMemory(assist + 748, awakenings, sizeof(awakenings))) return false;
        bool equip = false;
        for (unsigned i = 0; i < std::min(unsigned(available & 127u), 10u) && int16_t(awakenings[i]) > 0; ++i)
            equip |= awakenings[i] == 49;
        if (equip) for (unsigned i = 0; i < std::min(unsigned(available & 127u), 10u); ++i)
            count += ObservationLWeight(awakenings[i]);
    }
    for (unsigned i = 0; i < 9; ++i) {
        uint32_t added = 0;
        if (!Decode(member + PHOrbObservationAddedAwakeningsOffset + 16 * i, key1, key2, 0x6a875a8bu, added)) return false;
        const int16_t awakening = int16_t(added);
        if (added != uint16_t(awakening) && added != uint32_t(int32_t(awakening))) return false;
        if (awakening > 0) count += ObservationLWeight(unsigned(awakening));
    }
    return count <= UINT8_MAX;
}

void CollectObservation(const PHOrbSnapshot &state, uint32_t key1, uint32_t key2, PHOrbBattleObservation &out) {
    // Explicit zeroing makes the bounded second collection compare padding as
    // well as values deterministically. No pointers are published in this POD.
    std::memset(&out, 0, sizeof(out));
    const auto &automation = state.automation;
    const uintptr_t impl = state.identity.impl;
    out.hp = automation.hp; out.maximumHP = automation.maximumHP; out.hpKnown = automation.hpKnown;
    SkillSourceState source{};
    out.rulesKnown = ReadSkillSource(state, source);
    if (out.rulesKnown) out.dungeonFlags = source.flags;
    uint16_t duration = 0;
    out.assistVoidKnown = PHReadU16(impl + PHOrbObservationAssistVoidOffset, &duration);
    if (out.assistVoidKnown) {
        out.assistVoidTurns = int16_t(uint16_t(duration << 6)) / 64;
        out.assistVoidFresh = (duration & 0x400u) != 0;
    }
    uint32_t adjustment = 0;
    out.cooldownAdjustmentKnown = Decode(impl + PHOrbObservationCooldownOffset, key1, key2, 0x766cfc6du, adjustment) &&
        int32_t(adjustment) >= 0 && adjustment <= UINT16_MAX;
    if (out.cooldownAdjustmentKnown) out.dungeonCooldownAdjustment = int32_t(adjustment);
    ObservationTables tables{};
    const bool tablesKnown = ReadObservationTables(state.identity.header, tables);
    uint8_t dynamic[24]{};
    const bool dynamicKnown = PHReadMemory(impl + PHOrbObservationAwakeningGrantsOffset, dynamic, sizeof(dynamic));
    bool dynamicL = false;
    for (unsigned group = 0; dynamicKnown && group < 2; ++group)
        for (unsigned i = 0; i < 6; ++i) dynamicL |= ObservationLWeight(dynamic[group * 12 + 4 + i]) != 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const auto &skill = automation.members[slot];
        auto &member = out.members[slot];
        const uintptr_t address = impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        member.active = skill.active; member.bound = skill.bound; member.number = skill.number;
        member.sourceAssist = skill.sourceCatalogIDs[1]; member.activeAssist = skill.assistCatalogID;
        member.charge = skill.charge; member.temporaryForm = skill.temporaryTurns != 0;
        member.assistRetained = (skill.assistState & 255u) == 172;
        for (unsigned i = 0; i < 2; ++i) { member.stage[i] = skill.skillStage[i]; member.cachedStage[i] = skill.cachedSkillStage[i]; }
        uint8_t active = 0;
        const bool activeKnown = PHReadU8(address, &active);
        if (!activeKnown) { member.lBlockers = PHOrbLUnknownAwakenings; continue; }
        if (active != 1) { member.active = false; member.lBlockers = PHOrbLInactive; member.lEligibilityKnown = true; continue; }
        uintptr_t parameter = 0, actual = 0, profile = 0;
        uint32_t id = 0;
        const bool parameterKnown = tablesKnown && PHReadU32(address + PHBattleCatalogIDOffset, &id) &&
            ObservationCard(tables, id, parameter) && PHReadPointer(address + PHBattleCardParamOffset, &actual) && actual == parameter;
        if (parameterKnown) member.skillKnown[0] = ObservationSkill(tables, parameter, skill.skillStage[0], skill.skillLevel[0],
            member.rootSkillIDs[0], member.skillIDs[0], member.catalogCooldown[0], member.cooldownKnown[0]);
        if (!skill.assistCatalogID) { member.skillKnown[1] = member.cooldownKnown[1] = true; }
        else {
            uint8_t variant = 0, level = 0; uintptr_t assist = 0;
            const bool formKnown = PHReadU8(address + 1444, &variant);
            member.assistVariantUnsupported = !formKnown || variant != 0;
            // Variant catalog lookup is not ordinary base-index addressing.
            if (tablesKnown && !member.assistVariantUnsupported && int16_t(skill.assistCatalogID) > 0 &&
                ObservationCard(tables, skill.assistCatalogID, assist) && PHReadU8(address + 1442, &level))
                member.skillKnown[1] = ObservationSkill(tables, assist, skill.skillStage[1], level,
                    member.rootSkillIDs[1], member.skillIDs[1], member.catalogCooldown[1], member.cooldownKnown[1]);
        }
        if (member.cooldownKnown[0] && member.cooldownKnown[1] && out.cooldownAdjustmentKnown && out.rulesKnown) {
            const uint32_t body = member.catalogCooldown[0] ? member.catalogCooldown[0] + adjustment : 0;
            const uint32_t sum = uint32_t(member.catalogCooldown[0]) + member.catalogCooldown[1];
            const uint32_t total = sum ? sum + adjustment * (skill.assistCatalogID ? 2u : 1u) : 0;
            member.thresholdsKnown = body <= UINT16_MAX && total >= body && total <= UINT16_MAX;
            if (member.thresholdsKnown) {
                member.bodyCooldown = uint16_t(body); member.totalCooldown = uint16_t(total);
                member.chargeToBody = uint16_t(body > member.charge ? body - member.charge : 0);
                member.chargeToAssist = uint16_t(total > member.charge ? total - member.charge : 0);
                member.chargeWindow = skill.assistCatalogID && !(source.flags & 8u) && total && member.charge >= total ?
                    PHOrbChargeAssist : !body ? PHOrbChargeNoSkill : member.charge < body ? PHOrbChargeBelowBody : PHOrbChargeBody;
            }
        }
        SkillChoiceState family{};
        member.familyKnown = ReadSkillChoice(state, slot, family);
        member.sourceFamilyEqual = member.familyKnown && family.family[0] == family.family[1];
        member.latentKnown = PHReadPointer(address + PHOrbObservationProfileOffset, &profile) && ObservationLatents(profile, member.assistRecoveryLatents);
        unsigned lCount = 0;
        member.lAwakeningsKnown = parameterKnown && member.latentKnown &&
            ObservationLAwakenings(address, parameter, profile, tables, skill, key1, key2, lCount) && dynamicKnown && !dynamicL;
        if (member.lAwakeningsKnown) member.lAwakenings = uint8_t(lCount);
        member.lAttributesKnown = parameterKnown && ObservationAttributes(address, parameter, member.lColors);
        if (!out.rulesKnown || !automation.statusKnown) member.lBlockers |= PHOrbLUnknownRules;
        if (skill.bound) member.lBlockers |= PHOrbLBound;
        if (automation.statusKnown && (automation.playerEffects & PHOrbPlayerAwokenBind)) member.lBlockers |= PHOrbLAwokenBind;
        if (out.rulesKnown && (source.flags & 4u)) member.lBlockers |= PHOrbLAwakeningsDisabled;
        if (out.rulesKnown && (source.flags & 0x20u)) member.lBlockers |= PHOrbLLatentsDisabled;
        if (!member.latentKnown) member.lBlockers |= PHOrbLUnknownLatents;
        else if (!member.assistRecoveryLatents) member.lBlockers |= PHOrbLNoLatent;
        if (!member.lAwakeningsKnown) member.lBlockers |= dynamicL ? PHOrbLDynamicAwakenings : PHOrbLUnknownAwakenings;
        else if (!member.lAwakenings) member.lBlockers |= PHOrbLNoAwakening;
        if (!member.lAttributesKnown) member.lBlockers |= PHOrbLUnknownAttributes;
        const unsigned disabled = PHOrbLBound | PHOrbLAwokenBind | PHOrbLAwakeningsDisabled;
        if (member.lBlockers & disabled) { member.lAwakenings = 0; member.lAwakeningsKnown = true; }
        const unsigned definitive = disabled | PHOrbLLatentsDisabled | PHOrbLNoLatent | PHOrbLNoAwakening;
        member.lEligibilityKnown = !member.lBlockers || (member.lBlockers & definitive);
        if (member.lBlockers) member.lColors = 0;
    }
    uint32_t enemies = 0;
    if (Decode(impl + PHOrbEnemyCountOffset, key1, key2, 0x766cfc6du, enemies) && enemies <= 8) {
        out.enemyCount = uint8_t(enemies); out.enemiesKnown = true;
        constexpr uintptr_t offsets[]{2400, 2192, 2256, 2224};
        for (unsigned i = 0; i < enemies; ++i) {
            auto &enemy = out.enemies[i]; const uintptr_t address = impl + PHOrbEnemyOffset + i * PHOrbEnemyStride;
            uint16_t flags = 0; uint32_t low = 0, high = 0; uint8_t action = 0;
            enemy.present = true;
            if (!PHReadU16(address + 532, &flags)) { out.enemiesKnown = false; continue; }
            enemy.parts = (flags & 0x100u) != 0;
            if (enemy.parts || !Decode(address + 64, key1, key2, 0x766cfc6du, low) ||
                !Decode(address + 80, key1, key2, 0x766cfc6du, high) || (high & 0x80000000u)) {
                out.enemiesKnown = false; continue;
            }
            enemy.hpKnown = true; enemy.hp = (uint64_t(high) << 32) | low;
            if (!enemy.hp) continue;
            ++out.activeEnemies;
            enemy.effectsKnown = PHReadU8(address + 23, &action);
            enemy.actionBusy = int8_t(action) >= 0;
            for (unsigned effect = 0; effect < 4; ++effect) {
                uint32_t turns = 0, mask = 0;
                if (!SmallProtected(address + offsets[effect], key1, key2, UINT16_MAX, turns) ||
                    ((effect == 1 || effect == 2) && int16_t(turns) > 0 &&
                     !SmallProtected(address + (effect == 1 ? 2176 : 2240), key1, key2, UINT16_MAX, mask))) {
                    enemy.effectsKnown = false; break;
                }
                enemy.effectTurns[effect] = int16_t(turns) > 0 ? uint16_t(turns) : 0;
                enemy.effectMasks[effect] = uint16_t(mask);
                if (enemy.effectTurns[effect] && (effect == 0 || effect == 3 || (mask & 31u))) enemy.effects |= 1u << effect;
            }
            out.enemiesKnown &= enemy.effectsKnown;
        }
    }
    out.status = PHOrbObservationReady;
}

void ReadBattleObservation(PHOrbSnapshot &state) {
    state.observation = {};
    if (state.multiplayerDisabled || state.multiplayer.kind != PHOrbMultiplayerNone ||
        !PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer)) {
        state.observation.status = PHOrbObservationMultiplayer; return;
    }
    if (!state.valid || !state.present || state.identity.sourceMode != 0) {
        state.observation.status = PHOrbObservationUnavailable; return;
    }
    if (state.phase != 2 || !state.settled || state.boardTransition || state.dragged || !PHInputIsQuiet(&state.input)) {
        state.observation.status = PHOrbObservationTransition; return;
    }
    PHOrbSnapshot first = state, second = state;
    first.automation = {}; first.automation.window = true;
    second.automation = {}; second.automation.window = true;
    uint32_t key1 = 0, key2 = 0, lastKey1 = 0, lastKey2 = 0, phase = 0, wave = 0, moves = 0;
    uintptr_t dragged = 0;
    const uintptr_t header = state.identity.header, impl = state.identity.impl;
    if (!ConfirmOrbEntryIdentity(state.identity) || !ReadAutomationFields(first) || first.automation.busy ||
        !PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2)) {
        state.observation.status = PHOrbObservationUnavailable; return;
    }
    PHOrbBattleObservation observation{}, confirmation{};
    ObservationTables firstTables{}, lastTables{};
    const bool firstTablesKnown = ReadObservationTables(header, firstTables);
    CollectObservation(first, key1, key2, observation);
    if (!ReadAutomationFields(second)) { state.observation.status = PHOrbObservationChanged; return; }
    CollectObservation(second, key1, key2, confirmation);
    const bool lastTablesKnown = ReadObservationTables(header, lastTables);
    if (firstTablesKnown != lastTablesKnown || firstTables.catalog != lastTables.catalog ||
        firstTables.catalogCount != lastTables.catalogCount || firstTables.skills != lastTables.skills ||
        firstTables.skillCount != lastTables.skillCount ||
        !PHOrbSameAutomation(&first, &second) || std::memcmp(&observation, &confirmation, sizeof(observation)) ||
        !ConfirmOrbEntryIdentity(state.identity) ||
        !PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2 ||
        !Decode(impl + PHBattlePhaseOffset, key1, key2, 0x766cfc6du, phase) || phase != state.phase ||
        !Decode(impl + PHBattleWaveOffset, key1, key2, 0x766cfc6du, wave) || wave != state.wave ||
        !PHReadU32(impl + PHOrbMoveSerialOffset, &moves) || moves != state.moves ||
        !PHReadPointer(impl + PHBattleDragPointerOffset, &dragged) || dragged != state.dragged) {
        state.observation.status = PHOrbObservationChanged; return;
    }
    state.observation = observation;
}
