// Optional read-only context; missing leaders do not disable Combo/manual.
// Six local members in modes 7/10/11: 0x10001956C / 0x100019BBC.
// Sources/transforms follow 0x1005A12F0; swaps preserve source slots.
// No native getter is called here, including catalog and team-bank getters.
bool ReadLeaderOwner(const PHOrbSnapshot &state, uintptr_t &bank, uint8_t &flags, PHOrbLeaderReadDiagnostics &d) {
    const uintptr_t h = state.identity.header, p = state.identity.impl;
    const auto &m = state.multiplayer;
    d.ownerBusy = d.ownerFlags = 0; d.ownerBankMatches = false;
    if (!m.known || m.localSeat >= m.teamCount || m.localSeat != m.viewTeam ||
        !PHReadU8(p + PHBattleTeamBusyOffset, &d.ownerBusy) || (d.ownerBusy & 8u) ||
        !PHReadU8(p + PHBattleTeamDescriptorOffset + m.localSeat * PHBattleTeamDescriptorStride, &d.ownerFlags) ||
        !(d.ownerFlags & 1u) ||
        !PHReadPointer(p + PHBattleTeamDescriptorOffset + m.localSeat * PHBattleTeamDescriptorStride + PHBattleTeamPointerOffset, &bank)) return false;
    flags = d.ownerFlags;
    d.ownerBankMatches = bank == h + PHBattleTeamStorageRVA + m.localSeat * PHBattleTeamStorageStride;
    return d.ownerBankMatches && PHMemoryHasProtection(bank, 6 * PHBattleMemberStride, VM_PROT_READ, VM_PROT_EXECUTE);
}

bool LeaderParameterMatches(uintptr_t catalog, uint32_t count, uint32_t records, uint32_t id,
                            uintptr_t parameter, PHOrbLeaderReadDiagnostics &d) {
    // 0x10052FC4C follows catalog+828 links for fixed-team variant records.
    // count (save+0x5ef158) ends ordinary IDs; records (+0x5ef15c) bounds
    // the allocation. 0x10052F558 appends variants at indices >= count.
    // A variant's public number is only a display label; use its own skill ID.
    uint32_t index = id < count ? id : id % 100000u;
    for (unsigned hop = 0; hop < 128; ++hop) {
        d.catalogIndex = index;
        if (!index || index >= records) return false;
        const uintptr_t record = catalog + index * PHBattleCatalogStride;
        if (!PHReadU32(record, &d.parameterID)) return false;
        if (d.parameterID == id) { d.parameterMatches = parameter == record; return d.parameterMatches; }
        if (id < count || !PHReadU32(record + PHOrbLeaderCatalogNextOffset, &index)) return false;
    }
    return false;
}

bool ReadExtraHelper(PHOrbSnapshot &out, uintptr_t save, uintptr_t parameter, uint32_t skillCount,
                     PHOrbLeaderReadDiagnostics &d) {
    // 4P source slot 5 may be an inactive ID 10000000 carrying an extra
    // active skill. 0x1005A1038 leaves active=0; 0x10006FB04 and
    // 0x10007156C require active=1 before applying the helper leader skill.
    // Confirm the actual private row selected by 0x1004C7E3C, not an absent
    // normal-card lookup. Registry/table changes invalidate the whole pair.
    auto &c = out.leaderContext;
    const uintptr_t registry = out.identity.header + PHOrbLeaderExtraRegistryRVA;
    d.status = PHOrbLeaderReadExtraRegistry;
    if (!PHReadU16(registry + 4, &d.extraCount) || !d.extraCount || d.extraCount > 4096 ||
        !PHReadPointer(registry + 8, &c.extraRows) || !c.extraRows ||
        c.extraRows > UINTPTR_MAX - size_t(d.extraCount) * PHOrbLeaderExtraStride) return false;
    c.extraCount = d.extraCount;
    d.status = PHOrbLeaderReadExtraSelection;
    if (!PHReadU32(save + PHOrbLeaderExtraSelectionOffset, &d.extraSelection) || d.extraSelection > INT32_MAX) return false;
    c.extraSelection = d.extraSelection;
    const uint32_t key = c.extraSelection <= 10000000u ? c.extraSelection : c.extraSelection % 10000000u;
    d.status = PHOrbLeaderReadExtraRow;
    for (unsigned row = 0; row < c.extraCount; ++row) {
        const uintptr_t entry = c.extraRows + row * PHOrbLeaderExtraStride;
        uint32_t id = 0;
        if (!PHReadU32(entry, &id)) return false;
        if (id % 10000000u != key) continue;
        d.extraRecord = id; d.parameterMatches = parameter == entry + 8;
        if (!d.parameterMatches) return false;
        d.status = PHOrbLeaderReadExtraSkill;
        if (!PHReadU32(parameter + PHOrbLeaderExtraSkillOffset, &d.extraSkill) ||
            !d.extraSkill || d.extraSkill >= skillCount) return false;
        c.extraSkill = d.extraSkill; c.extraRecord = id; c.extraHelper = true;
        return true;
    }
    return false;
}

bool ReadLeaderPair(PHOrbSnapshot &out, uintptr_t save, uintptr_t impl, uintptr_t catalog, uint32_t count,
                    uint32_t records, bool multiplayer, uint32_t skillCount, uint32_t key1, uint32_t key2,
                    PHOrbLeaderReadDiagnostics &d) {
    auto &c = out.leaderContext;
    for (unsigned i = 0; i < 2; ++i) {
        d.visibleSlot = i ? 5 : 0;
        d.catalogID = d.parameterID = d.skillID = d.catalogIndex = d.sourceSlot = d.sourceCatalogID = 0;
        d.formID = d.temporaryID = d.temporaryTurns = d.extraSelection = d.extraRecord = d.extraSkill = 0;
        d.activeFlags = d.extraFlags = 0; d.extraCount = 0; d.parameterMatches = false;
        const uintptr_t member = impl + PHBattleMemberOffset + (i ? 5 : 0) * PHBattleMemberStride;
        uint8_t &active = d.activeFlags, &extra = d.extraFlags;
        uint32_t &id = d.catalogID; uintptr_t parameter = 0;
        d.status = PHOrbLeaderReadMember;
        if (!PHReadU8(member, &active) || !PHReadU32(member + PHBattleCatalogIDOffset, &id) || !id || id > INT32_MAX ||
            !PHReadPointer(member + PHBattleCardParamOffset, &parameter) || !parameter) return false;
        if (!multiplayer) {
            d.status = PHOrbLeaderReadParameter;
            if (!(active & 1u) || id >= count || !PHBattleNumberForCatalogID(id, &out.leaderNumbers[i]) ||
                parameter != catalog + id * PHBattleCatalogStride || !PHReadU32(parameter, &d.parameterID) || d.parameterID != id) return false;
            d.parameterMatches = true;
            continue;
        }
        uint32_t &source = d.sourceSlot, &form = d.formID, &temporary = d.temporaryID, &turns = d.temporaryTurns;
        d.status = PHOrbLeaderReadSource;
        if (!PHReadU8(member + PHOrbLeaderExtraFlagOffset, &extra) ||
            !Decode(member + PHBattleSourceSlotOffset, key1, key2, 0x6a875a8bu, source) || source > 5) return false;
        d.status = PHOrbLeaderReadBase;
        if (!PHReadU16(member + PHBattleBaseIDOffset, &c.sourceCatalogIDs[i])) return false;
        d.sourceCatalogID = c.sourceCatalogIDs[i];
        if (!c.sourceCatalogIDs[i]) return false;
        d.status = PHOrbLeaderReadForm;
        if (!Decode(member + PHBattleFormOffset, key1, key2, 0x6a875a8bu, form) || form >= count) return false;
        d.status = PHOrbLeaderReadTemporary;
        if (!Decode(member + PHBattleTempIDOffset, key1, key2, 0x6a875a8bu, temporary) || temporary >= count) return false;
        d.status = PHOrbLeaderReadTemporaryTurns;
        if (!Decode(member + PHBattleTempTurnsOffset, key1, key2, 0x6a875a8bu, turns) || turns > UINT16_MAX) return false;
        if (id == 10000000u) {
            d.status = PHOrbLeaderReadExtraHelper;
            if (out.multiplayer.kind != PHOrbMultiplayerFour || i != 1 || active || !(extra & 2u) || source != 5 ||
                c.sourceCatalogIDs[i] != uint16_t(id) || form || temporary || turns) return false;
            if (!ReadExtraHelper(out, save, parameter, skillCount, d)) return false;
            out.leaderNumbers[i] = 0;
        } else {
            const uint32_t currentBase = id % 1000000u;
            d.status = PHOrbLeaderReadMemberState;
            if (active != 1 || (extra & 2u) || c.sourceCatalogIDs[i] >= count ||
                !PHBattleNumberForCatalogID(currentBase, &out.leaderNumbers[i])) return false;
            d.status = PHOrbLeaderReadTransform;
            if (turns ? (!temporary || id != temporary) : currentBase != (form ? form : c.sourceCatalogIDs[i])) return false;
            d.status = PHOrbLeaderReadParameter;
            if (!LeaderParameterMatches(catalog, count, records, id, parameter, d)) return false;
        }
        d.status = PHOrbLeaderReadSkill;
        if (!PHReadU32(parameter, &d.parameterID) ||
            (!c.extraHelper && (!PHReadU32(parameter + PHOrbLeaderSkillOffset, &d.skillID) || d.skillID >= skillCount))) return false;
        // The verified inactive extra helper never contributes a leader
        // effect. Its private row's unused leader field is not a requirement.
        c.parameterIDs[i] = d.parameterID; c.skillIDs[i] = d.skillID;
        c.parameters[i] = parameter; c.catalogIDs[i] = id;
        c.sourceSlots[i] = uint8_t(source); c.formIDs[i] = uint16_t(form);
        c.temporaryIDs[i] = uint16_t(temporary); c.temporaryTurns[i] = uint16_t(turns);
        c.activeFlags[i] = active; c.extraFlags[i] = extra;
    }
    if (multiplayer && c.sourceSlots[0] == c.sourceSlots[1]) { d.status = PHOrbLeaderReadDuplicateSource; return false; }
    return true;
}

bool ReadLeaders(PHOrbSnapshot &state, uintptr_t save, uintptr_t impl) {
    const bool multiplayer = state.multiplayer.kind != PHOrbMultiplayerNone;
    state.leadersValid = false; state.leaderContext = {};
    state.leaderNumbers[0] = state.leaderNumbers[1] = 0; state.leaderRead = {};
    auto &d = state.leaderRead; d.visibleSlot = UINT8_MAX;
    PHOrbSnapshot first = state; first.valid = true; first.leadersValid = true;
    uint32_t &count = d.catalogCount, &records = d.catalogRecords, &skillCount = d.skillCount;
    uint32_t key1 = 0, key2 = 0; uintptr_t catalog = 0;
    uint8_t ownerFlags = 0;
    d.status = PHOrbLeaderReadInputContext;
    if (!PHOrbInputContextReady(&first)) return false;
    d.status = PHOrbLeaderReadCatalog;
    if (!PHReadU32(save + PHBattleCatalogCountOffset, &count) || !count || count > 65536 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &catalog) || !catalog ||
        catalog > UINTPTR_MAX - size_t(count) * PHBattleCatalogStride) return false;
    records = count;
    if (multiplayer) {
        // Native allocation and linking distinguish normal-card count from
        // total records. Never use the former as the variant-chain bound.
        d.status = PHOrbLeaderReadCatalogCapacity;
        if (!PHReadU32(save + PHOrbLeaderCatalogRecordsOffset, &records) || records < count || records > 0x20000 ||
            catalog > UINTPTR_MAX - size_t(records) * PHBattleCatalogStride ||
            !PHMemoryHasProtection(catalog, size_t(records) * PHBattleCatalogStride, VM_PROT_READ, VM_PROT_EXECUTE)) return false;
        d.status = PHOrbLeaderReadOwner;
        if (!ReadLeaderOwner(first, first.leaderContext.teamBank, ownerFlags, d)) return false;
        d.status = PHOrbLeaderReadSkillTable;
        if (!PHReadU32(save + PHOrbSkillCountOffset, &skillCount) || !skillCount || skillCount > 0x20000) return false;
        d.status = PHOrbLeaderReadKeys;
        if (!PHReadU32(state.identity.header + PHBattleKey1RVA, &key1) ||
            !PHReadU32(state.identity.header + PHBattleKey2RVA, &key2)) return false;
        first.leaderContext.known = true;
    }
    d.pass = 1;
    if (!ReadLeaderPair(first, save, impl, catalog, count, records, multiplayer, skillCount, key1, key2, d)) return false;
    // Re-read the whole pair. Replacing the leader while reading the helper
    // must invalidate both, even when the visible encyclopedia IDs match.
    PHOrbSnapshot second = first; second.leaderContext = {};
    second.leaderContext.known = first.leaderContext.known; second.leaderContext.teamBank = first.leaderContext.teamBank;
    d.pass = 2;
    if (!ReadLeaderPair(second, save, impl, catalog, count, records, multiplayer, skillCount, key1, key2, d)) return false;
    d.status = PHOrbLeaderReadPairChanged;
    if (!PHOrbSameLeaders(&first, &second)) return false;
    d.pass = 3; d.visibleSlot = UINT8_MAX;
    uint32_t lastCount = 0; uintptr_t lastCatalog = 0;
    d.status = PHOrbLeaderReadCatalogChanged;
    if (!PHReadU32(save + PHBattleCatalogCountOffset, &lastCount) || count != lastCount ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &lastCatalog) || catalog != lastCatalog) return false;
    if (multiplayer) {
        uint32_t lastRecords = 0;
        if (!PHReadU32(save + PHOrbLeaderCatalogRecordsOffset, &lastRecords) || records != lastRecords) return false;
        uintptr_t bank = 0; uint8_t flags = 0; uint32_t lastKey1 = 0, lastKey2 = 0, lastSkills = 0;
        d.status = PHOrbLeaderReadOwnerChanged;
        if (!ReadLeaderOwner(first, bank, flags, d) || bank != first.leaderContext.teamBank || flags != ownerFlags) return false;
        d.status = PHOrbLeaderReadSkillTableChanged;
        if (!PHReadU32(save + PHOrbSkillCountOffset, &lastSkills) || lastSkills != skillCount) return false;
        d.status = PHOrbLeaderReadKeysChanged;
        if (!PHReadU32(state.identity.header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
            !PHReadU32(state.identity.header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2) return false;
    }
    state.leaderNumbers[0] = first.leaderNumbers[0]; state.leaderNumbers[1] = first.leaderNumbers[1];
    state.leaderContext = first.leaderContext;
    d.status = PHOrbLeaderReadReady;
    return true;
}
