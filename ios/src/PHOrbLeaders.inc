// Optional read-only context; missing leaders do not disable Combo/manual.
// Six local members in modes 7/10/11: 0x10001956C / 0x100019BBC.
// Sources/transforms follow 0x1005A12F0; swaps preserve source slots.
// No native getter is called here, including catalog and team-bank getters.
bool ReadLeaderOwner(const PHOrbSnapshot &state, uintptr_t &bank, uint8_t &flags) {
    const uintptr_t h = state.identity.header, p = state.identity.impl;
    const auto &m = state.multiplayer;
    uint8_t busy = 0;
    return m.known && m.localSeat < m.teamCount && m.localSeat == m.viewTeam &&
        PHReadU8(p + PHBattleTeamBusyOffset, &busy) && !(busy & 8u) &&
        PHReadU8(p + PHBattleTeamDescriptorOffset + m.localSeat * PHBattleTeamDescriptorStride, &flags) && (flags & 1u) &&
        PHReadPointer(p + PHBattleTeamDescriptorOffset + m.localSeat * PHBattleTeamDescriptorStride + PHBattleTeamPointerOffset, &bank) &&
        bank == h + PHBattleTeamStorageRVA + m.localSeat * PHBattleTeamStorageStride &&
        PHMemoryHasProtection(bank, 6 * PHBattleMemberStride, VM_PROT_READ, VM_PROT_EXECUTE);
}

bool LeaderParameterMatches(uintptr_t catalog, uint32_t count, uint32_t id, uintptr_t parameter) {
    // 0x10052FC4C follows catalog+828 links for fixed-team variant records.
    // A variant's public number is only a display label; use its own skill ID.
    uint32_t index = id < count ? id : id % 100000u;
    for (unsigned hop = 0; hop < 128 && index && index < count; ++hop) {
        const uintptr_t record = catalog + index * PHBattleCatalogStride;
        uint32_t actual = 0;
        if (!PHReadU32(record, &actual)) return false;
        if (actual == id) return parameter == record;
        if (id < count || !PHReadU32(record + PHOrbLeaderCatalogNextOffset, &index)) return false;
    }
    return false;
}

bool ReadExtraHelper(PHOrbSnapshot &out, uintptr_t save, uintptr_t parameter, uint32_t skillCount) {
    // 4P source slot 5 may be an inactive ID 10000000 carrying an extra
    // active skill. 0x1005A1038 leaves active=0; 0x10006FB04 and
    // 0x10007156C require active=1 before applying the helper leader skill.
    // Confirm the actual private row selected by 0x1004C7E3C, not an absent
    // normal-card lookup. Registry/table changes invalidate the whole pair.
    auto &c = out.leaderContext;
    const uintptr_t registry = out.identity.header + PHOrbLeaderExtraRegistryRVA;
    if (!PHReadU16(registry + 4, &c.extraCount) || !c.extraCount || c.extraCount > 4096 ||
        !PHReadPointer(registry + 8, &c.extraRows) || !c.extraRows ||
        c.extraRows > UINTPTR_MAX - size_t(c.extraCount) * PHOrbLeaderExtraStride ||
        !PHReadU32(save + PHOrbLeaderExtraSelectionOffset, &c.extraSelection) || c.extraSelection > INT32_MAX) return false;
    const uint32_t key = c.extraSelection <= 10000000u ? c.extraSelection : c.extraSelection % 10000000u;
    for (unsigned row = 0; row < c.extraCount; ++row) {
        const uintptr_t entry = c.extraRows + row * PHOrbLeaderExtraStride;
        uint32_t id = 0;
        if (!PHReadU32(entry, &id)) return false;
        if (id % 10000000u != key) continue;
        if (parameter != entry + 8 || !PHReadU32(parameter + PHOrbLeaderExtraSkillOffset, &c.extraSkill) ||
            !c.extraSkill || c.extraSkill >= skillCount) return false;
        c.extraRecord = id; c.extraHelper = true;
        return true;
    }
    return false;
}

bool ReadLeaderPair(PHOrbSnapshot &out, uintptr_t save, uintptr_t impl, uintptr_t catalog, uint32_t count,
                    bool multiplayer, uint32_t skillCount, uint32_t key1, uint32_t key2) {
    auto &c = out.leaderContext;
    for (unsigned i = 0; i < 2; ++i) {
        const uintptr_t member = impl + PHBattleMemberOffset + (i ? 5 : 0) * PHBattleMemberStride;
        uint8_t active = 0, extra = 0; uint32_t id = 0, record = 0; uintptr_t parameter = 0;
        if (!PHReadU8(member, &active) || !PHReadU32(member + PHBattleCatalogIDOffset, &id) || !id || id > INT32_MAX ||
            !PHReadPointer(member + PHBattleCardParamOffset, &parameter) || !parameter) return false;
        if (!multiplayer) {
            if (!(active & 1u) || id >= count || !PHBattleNumberForCatalogID(id, &out.leaderNumbers[i]) ||
                parameter != catalog + id * PHBattleCatalogStride || !PHReadU32(parameter, &record) || record != id) return false;
            continue;
        }
        uint32_t source = 0, form = 0, temporary = 0, turns = 0;
        if (!PHReadU8(member + PHOrbLeaderExtraFlagOffset, &extra) ||
            !Decode(member + PHBattleSourceSlotOffset, key1, key2, 0x6a875a8bu, source) || source > 5 ||
            !PHReadU16(member + PHBattleBaseIDOffset, &c.sourceCatalogIDs[i]) || !c.sourceCatalogIDs[i] ||
            !Decode(member + PHBattleFormOffset, key1, key2, 0x6a875a8bu, form) || form >= count ||
            !Decode(member + PHBattleTempIDOffset, key1, key2, 0x6a875a8bu, temporary) || temporary >= count ||
            !Decode(member + PHBattleTempTurnsOffset, key1, key2, 0x6a875a8bu, turns) || turns > UINT16_MAX) return false;
        if (id == 10000000u) {
            if (out.multiplayer.kind != PHOrbMultiplayerFour || i != 1 || active || !(extra & 2u) || source != 5 ||
                c.sourceCatalogIDs[i] != uint16_t(id) || form || temporary || turns ||
                !ReadExtraHelper(out, save, parameter, skillCount)) return false;
            out.leaderNumbers[i] = 0;
        } else {
            const uint32_t currentBase = id % 1000000u;
            if (active != 1 || (extra & 2u) || c.sourceCatalogIDs[i] >= count ||
                !PHBattleNumberForCatalogID(currentBase, &out.leaderNumbers[i]) ||
                (turns ? (!temporary || id != temporary) : currentBase != (form ? form : c.sourceCatalogIDs[i])) ||
                !LeaderParameterMatches(catalog, count, id, parameter)) return false;
        }
        if (!PHReadU32(parameter, &c.parameterIDs[i]) ||
            !PHReadU32(parameter + PHOrbLeaderSkillOffset, &c.skillIDs[i]) || c.skillIDs[i] >= skillCount) return false;
        c.parameters[i] = parameter; c.catalogIDs[i] = id;
        c.sourceSlots[i] = uint8_t(source); c.formIDs[i] = uint16_t(form);
        c.temporaryIDs[i] = uint16_t(temporary); c.temporaryTurns[i] = uint16_t(turns);
        c.activeFlags[i] = active; c.extraFlags[i] = extra;
    }
    return !multiplayer || c.sourceSlots[0] != c.sourceSlots[1];
}

bool ReadLeaders(PHOrbSnapshot &state, uintptr_t save, uintptr_t impl) {
    const bool multiplayer = state.multiplayer.kind != PHOrbMultiplayerNone;
    PHOrbSnapshot first = state; first.valid = true; first.leadersValid = true;
    first.leaderContext = {}; first.leaderNumbers[0] = first.leaderNumbers[1] = 0;
    uint32_t count = 0, skillCount = 0, key1 = 0, key2 = 0; uintptr_t catalog = 0;
    uint8_t ownerFlags = 0;
    if (!PHOrbInputContextReady(&first) ||
        !PHReadU32(save + PHBattleCatalogCountOffset, &count) || !count || count > 65536 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &catalog) || !catalog ||
        catalog > UINTPTR_MAX - size_t(count) * PHBattleCatalogStride) return false;
    if (multiplayer) {
        if (!ReadLeaderOwner(first, first.leaderContext.teamBank, ownerFlags) ||
            !PHReadU32(save + PHOrbSkillCountOffset, &skillCount) || !skillCount || skillCount > 0x20000 ||
            !PHReadU32(state.identity.header + PHBattleKey1RVA, &key1) ||
            !PHReadU32(state.identity.header + PHBattleKey2RVA, &key2)) return false;
        first.leaderContext.known = true;
    }
    if (!ReadLeaderPair(first, save, impl, catalog, count, multiplayer, skillCount, key1, key2)) return false;
    // Re-read the whole pair. Replacing the leader while reading the helper
    // must invalidate both, even when the visible encyclopedia IDs match.
    PHOrbSnapshot second = first; second.leaderContext = {};
    second.leaderContext.known = first.leaderContext.known; second.leaderContext.teamBank = first.leaderContext.teamBank;
    if (!ReadLeaderPair(second, save, impl, catalog, count, multiplayer, skillCount, key1, key2) ||
        !PHOrbSameLeaders(&first, &second)) return false;
    uint32_t lastCount = 0; uintptr_t lastCatalog = 0;
    if (!PHReadU32(save + PHBattleCatalogCountOffset, &lastCount) || count != lastCount ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &lastCatalog) || catalog != lastCatalog) return false;
    if (multiplayer) {
        uintptr_t bank = 0; uint8_t flags = 0; uint32_t lastKey1 = 0, lastKey2 = 0, lastSkills = 0;
        if (!ReadLeaderOwner(first, bank, flags) || bank != first.leaderContext.teamBank || flags != ownerFlags ||
            !PHReadU32(save + PHOrbSkillCountOffset, &lastSkills) || lastSkills != skillCount ||
            !PHReadU32(state.identity.header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
            !PHReadU32(state.identity.header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2) return false;
    }
    state.leaderNumbers[0] = first.leaderNumbers[0]; state.leaderNumbers[1] = first.leaderNumbers[1];
    state.leaderContext = first.leaderContext;
    return true;
}
