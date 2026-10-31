// Included inside PHOrbNative's anonymous namespace. No native getters/writes.
bool CheckAssistRecoveryCode(uintptr_t header) {
    if (!CheckObservationCode(header)) return false;
    for (const auto &guard : PHOrbAssistRecoveryTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}

bool CollectAssistRecovery(const PHOrbSnapshot &state, uint32_t key1, uint32_t key2,
    PHOrbAssistRecoveryState &out) {
    std::memset(&out, 0, sizeof(out));
    const uintptr_t impl = state.identity.impl, save = state.identity.header + PHSaveRVA;
    uint16_t duration = 0; uint32_t queueCount = 0, dispatchMode = 0; uintptr_t queue = 0;
    ObservationTables tables{}; SkillSourceState rules{};
    if (!PHReadU16(impl + PHOrbObservationAssistVoidOffset, &duration) || !ReadSkillSource(state, rules) ||
        !PHReadU32(impl + PHOrbAssistRecoveryDispatchOffset, &dispatchMode) || dispatchMode == 525190 ||
        !PHReadU32(impl + PHOrbAssistRecoveryQueueCountOffset, &queueCount) || queueCount != 0 ||
        !PHReadPointer(impl + PHOrbAssistRecoveryQueueOffset, &queue) || queue ||
        !PHReadU32(save + PHBattleCatalogCountOffset, &tables.catalogCount) || !tables.catalogCount || tables.catalogCount > 65536 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &tables.catalog) || !tables.catalog ||
        tables.catalog > UINTPTR_MAX - size_t(tables.catalogCount) * PHBattleCatalogStride || !state.automation.statusKnown) return false;
    const int turns = int16_t(uint16_t(duration << 6)) / 64;
    if (turns < 0) return false;
    out.turns = uint16_t(turns); out.fresh = (duration & 0x400u) != 0; out.dungeonFlags = rules.flags;
    uint8_t dynamic[24]{};
    const bool grantsKnown = PHReadMemory(impl + PHOrbObservationAwakeningGrantsOffset, dynamic, sizeof(dynamic));
    bool dynamicL = false;
    for (unsigned group = 0; grantsKnown && group < 2; ++group)
        for (unsigned i = 0; i < 6; ++i) dynamicL |= ObservationLWeight(dynamic[group * 12 + 4 + i]) != 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const auto &member = state.automation.members[slot];
        const uintptr_t address = impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        out.numbers[slot] = member.number; out.sourceAssists[slot] = member.sourceCatalogIDs[1];
        out.activeAssists[slot] = member.assistCatalogID; out.assistStates[slot] = member.assistState;
        out.temporaryIDs[slot] = member.temporaryID; out.temporaryTurns[slot] = member.temporaryTurns;
        uint8_t active = 0;
        if (!PHReadU8(address, &active)) return false;
        uint16_t &block = out.blockers[slot];
        if (active != 1) { block = PHOrbLInactive; continue; }
        if (!member.temporaryTurns && member.sourceCatalogIDs[1] && (member.assistState & 255u) == 172)
            out.retainedAssists |= 1u << slot;
        if (member.bound) block |= PHOrbLBound;
        if (state.automation.playerEffects & PHOrbPlayerAwokenBind) block |= PHOrbLAwokenBind;
        if (rules.flags & 4u) block |= PHOrbLAwakeningsDisabled;
        if (rules.flags & 0x20u) block |= PHOrbLLatentsDisabled;
        uintptr_t parameter = 0, actual = 0, profile = 0; uint32_t id = 0;
        const bool cardKnown = PHReadU32(address + PHBattleCatalogIDOffset, &id) && ObservationCard(tables, id, parameter) &&
            PHReadPointer(address + PHBattleCardParamOffset, &actual) && actual == parameter;
        if (!PHReadPointer(address + PHOrbObservationProfileOffset, &profile) || !ObservationLatents(profile, out.latents[slot]))
            block |= PHOrbLUnknownLatents;
        else if (!out.latents[slot]) block |= PHOrbLNoLatent;
        unsigned count = 0; uint8_t variant = 0;
        const bool variantKnown = !member.assistCatalogID || (PHReadU8(address + 1444, &variant) && !variant);
        if (!cardKnown || !variantKnown || !ObservationLAwakenings(address, parameter, profile, tables, member, key1, key2, count) || !grantsKnown)
            block |= PHOrbLUnknownAwakenings;
        else if (dynamicL) block |= PHOrbLDynamicAwakenings;
        else if (!count) block |= PHOrbLNoAwakening;
        else out.awakenings[slot] = uint8_t(count);
        if (!cardKnown || !ObservationAttributes(address, parameter, out.colors[slot])) block |= PHOrbLUnknownAttributes;
        const unsigned definitive = PHOrbLInactive | PHOrbLBound | PHOrbLAwokenBind | PHOrbLAwakeningsDisabled |
            PHOrbLLatentsDisabled | PHOrbLNoLatent | PHOrbLNoAwakening;
        if (block && !(block & definitive)) out.unknownMembers |= 1u << slot;
        if (block) continue;
        for (unsigned color = 0; color < 5; ++color) if (out.colors[slot] & (1u << color)) {
            out.perColor[color] += out.awakenings[slot]; out.providers[color] |= 1u << slot;
        }
    }
    out.valid = true;
    return true;
}

bool ReadAssistRecoveryFields(PHOrbSnapshot &state) {
    state.assistRecovery = {};
    if (!state.valid || !state.present || !state.automation.valid || state.automation.busy ||
        state.phase != 2 || state.boardTransition || (!state.settled && !state.dragged) || !PHInputIsQuiet(&state.input) ||
        state.multiplayerDisabled || state.multiplayer.kind != PHOrbMultiplayerNone || state.identity.sourceMode != 0 ||
        !PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer)) return false;
    PHOrbSnapshot first = state, second = state;
    first.automation = {}; second.automation = {};
    first.automation.window = second.automation.window = true;
    const uintptr_t header = state.identity.header, impl = state.identity.impl;
    uint32_t key1 = 0, key2 = 0, lastKey1 = 0, lastKey2 = 0, phase = 0, wave = 0, moves = 0;
    uintptr_t dragged = 0, catalog = 0, lastCatalog = 0; uint32_t count = 0, lastCount = 0;
    if (!ConfirmOrbEntryIdentity(state.identity) ||
        !PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
        !PHReadPointer(header + PHSaveRVA + PHBattleCatalogPointerOffset, &catalog) ||
        !PHReadU32(header + PHSaveRVA + PHBattleCatalogCountOffset, &count) ||
        !ReadAutomationFields(first) || first.automation.busy || !PHOrbSameAutomationExceptWindow(&state, &first) ||
        !CollectAssistRecovery(first, key1, key2, first.assistRecovery) ||
        !ReadAutomationFields(second) || second.automation.busy || !PHOrbSameAutomation(&first, &second) ||
        !CollectAssistRecovery(second, key1, key2, second.assistRecovery) ||
        !PHOrbSameAssistRecovery(&first.assistRecovery, &second.assistRecovery) ||
        !PHReadPointer(header + PHSaveRVA + PHBattleCatalogPointerOffset, &lastCatalog) || lastCatalog != catalog ||
        !PHReadU32(header + PHSaveRVA + PHBattleCatalogCountOffset, &lastCount) || lastCount != count ||
        !ConfirmOrbEntryIdentity(state.identity) ||
        !PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2 ||
        !Decode(impl + PHBattlePhaseOffset, key1, key2, 0x766cfc6du, phase) || phase != state.phase ||
        !Decode(impl + PHBattleWaveOffset, key1, key2, 0x766cfc6du, wave) || wave != state.wave ||
        !PHReadU32(impl + PHOrbMoveSerialOffset, &moves) || moves != state.moves ||
        !PHReadPointer(impl + PHBattleDragPointerOffset, &dragged) || dragged != state.dragged) return false;
    state.assistRecovery = first.assistRecovery;
    return true;
}
