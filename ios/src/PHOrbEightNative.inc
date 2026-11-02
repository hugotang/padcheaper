// Included after the ordinary skill and enemy readers. No polling getters,
// mutations or network calls. Fixed records are followed via their own chain.
bool EightSkillEffect(const ObservationTables &tables, uint32_t id, PHOrbEightMember &out,
                      bool leader, unsigned &budget, unsigned depth = 0) {
    if (!id || depth > 6 || !budget) return false;
    --budget;
    SkillDiagnosticRecord r{};
    if (!ObservationSkillRecord(tables, id, r)) return false;
    uint64_t &signature = leader ? out.leaderSignature : out.skillSignature;
    if (!signature) signature = UINT64_C(14695981039346656037);
    auto mix = [&](uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) { signature ^= uint8_t(value >> (i * 8)); signature *= UINT64_C(1099511628211); }
    };
    mix(id); mix(r.type); mix(r.maxLevel); mix(r.cooldown); mix(uint16_t(r.levelCooldownDelta));
    for (int32_t value : r.params) mix(uint32_t(value));
    const auto &p = r.params;
    if (r.type == (leader ? 138 : 116)) {
        unsigned children = 0; bool supported = true, ended = false;
        for (int child : p) {
            if (!child) { ended = true; continue; }
            if (ended || child < 0) return false;
            supported &= EightSkillEffect(tables, uint32_t(child), out, leader, budget, depth + 1);
            ++children;
        }
        return children > 0 && supported;
    }
    if (leader) {
        // Only independently confirmed connected-match bonuses are credited.
        // Other leaders may still use manual/Combo Auto without bonus credit.
        if (r.type == 163 || r.type == 177) {
            if (p[0] || p[1] || p[5] || p[6] || p[7]) return false;
            for (unsigned i = 2; i <= 4; ++i) if (p[i] && p[i] != 100) return false;
            out.noSkyfall = true; return true;
        }
        if ((r.type == 159 || r.type == 167) && p[0] > 0 && p[0] <= 63 && p[1] >= 3 && p[1] <= 42 &&
            p[2] >= 100 && p[2] <= UINT16_MAX && !out.attackLength) {
            if (r.type == 159 && (p[3] || (p[4] && p[4] != p[1]) || p[5] || p[6] || p[7])) return false;
            if (r.type == 167 && ((p[3] && p[3] != 100) || p[4] || p[5] || (p[6] && p[6] != p[1]) || p[7])) return false;
            out.attackColors = uint8_t(p[0]); out.attackLength = uint8_t(p[1]); out.attackPercent = uint16_t(p[2]);
            return true;
        }
        if ((r.type == 182 || r.type == 192) && p[0] > 0 && p[0] <= 63 && p[1] >= 3 && p[1] <= 42 &&
            p[2] >= 100 && p[2] <= UINT16_MAX && !out.attackLength) {
            if (r.type == 182 && p[3]) return false; // Unmodelled shield.
            if (r.type == 192 && (p[3] || (p[0] & (p[0] - 1)))) return false; // All selected colours, unlike OR.
            for (unsigned i = 4; i < 8; ++i) if (p[i]) return false;
            out.attackColors = uint8_t(p[0]); out.attackLength = uint8_t(p[1]); out.attackPercent = uint16_t(p[2]);
            return true;
        }
        if (r.type == 219 && p[0] > 0 && p[0] <= 63 && p[1] >= 3 && p[1] <= 42 &&
            p[2] > 0 && p[2] <= 20 && !out.comboLength) {
            for (unsigned i = 3; i < 8; ++i) if (p[i]) return false;
            out.comboColors = uint8_t(p[0]); out.comboLength = uint8_t(p[1]); out.addedCombos = uint8_t(p[2]);
            return true;
        }
        return false;
    }
    auto conversion = [&](int from, int to) {
        if (from < 0 || from >= 6 || to < 0 || to >= 6 || from == to) return false;
        for (unsigned c = 0; c < 6; ++c) if (out.convert[c] == from) out.convert[c] = uint8_t(to);
        return true;
    };
    auto generation = [&](int count, int colors, int exclude) {
        if (count <= 0 || count > 30 || colors <= 0 || colors > 63 ||
            exclude != colors) return false; // Only generated colours excluded from replacement are modelled.
        unsigned total = 0;
        for (unsigned color = 0; color < 6; ++color) {
            if (colors & (1u << color)) {
                if (out.generate[color]) return false;
                out.generate[color] = uint8_t(count);
            }
            total += out.generate[color];
        }
        return total <= 30;
    };
    auto tailZero = [&](unsigned first) {
        for (unsigned i = first; i < 8; ++i) if (p[i]) return false;
        return true;
    };
    switch (r.type) {
        case 9: return tailZero(2) && conversion(p[0],p[1]);
        case 20: return tailZero(4) && conversion(p[0],p[1]) && conversion(p[2],p[3]);
        case 154: {
            if (!tailZero(2) || p[0] <= 0 || p[0] > 63 || p[1] <= 0 || p[1] > 63 || (p[1] & (p[1] - 1))) return false;
            for (unsigned c = 0; c < 6; ++c) if ((p[0] & (1u << c)) && !conversion(c,__builtin_ctz(unsigned(p[1])))) return false;
            return true;
        }
        case 141: return tailZero(3) && generation(p[0],p[1],p[2]);
        case 71:
            if (out.boardColors) return false;
            for (int color : p) {
                if (color < 0) continue;
                if (color >= 6) return false;
                out.boardColors |= uint8_t(1u << color);
            }
            return out.boardColors != 0;
        case 172: if (!tailZero(0) || out.unlock) return false; out.unlock = true; return true;
        default: return false; // Selection, opponent effects, transforms, etc.
    }
}

bool CollectEightTeam(const PHOrbSnapshot &state, PHOrbEightState &out, uint32_t key1, uint32_t key2) {
    std::memset(&out, 0, sizeof(out));
    const uintptr_t save = state.identity.header + PHSaveRVA;
    ObservationTables tables{}; PHOrbLeaderReadDiagnostics d{}; uint8_t flags = 0;
    if (!ReadObservationTables(state.identity.header,tables) ||
        !PHReadU32(save + PHOrbLeaderCatalogRecordsOffset,&out.catalogRecords) ||
        out.catalogRecords < tables.catalogCount || out.catalogRecords > 0x20000 ||
        tables.catalog > UINTPTR_MAX - size_t(out.catalogRecords) * PHBattleCatalogStride ||
        !ReadLeaderOwner(state,out.teamBank,flags,d)) return false;
    out.catalog = tables.catalog; out.skills = tables.skills; out.catalogCount = tables.catalogCount; out.skillCount = tables.skillCount;
    unsigned sources = 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const uintptr_t address = state.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        const auto &member = state.automation.members[slot]; auto &m = out.members[slot];
        uint8_t active = 0, extra = 0; uint32_t source = 0, form = 0;
        if (!member.active || member.temporaryTurns || member.assistCatalogID || member.sourceCatalogIDs[1] ||
            !PHReadU8(address,&active) || active != 1 || !PHReadU8(address + PHOrbLeaderExtraFlagOffset,&extra) || (extra & 2u) ||
            !SmallProtected(address + PHBattleSourceSlotOffset,key1,key2,5,source) || (sources & (1u << source)) ||
            !SmallProtected(address + PHBattleFormOffset,key1,key2,UINT16_MAX,form) ||
            !PHReadU32(address + PHBattleCatalogIDOffset,&m.catalogID) || !m.catalogID || m.catalogID > INT32_MAX ||
            m.catalogID % 1000000u != (form ? form : member.sourceCatalogIDs[0]) ||
            !PHReadPointer(address + PHBattleCardParamOffset,&m.parameter) ||
            !LeaderParameterMatches(tables.catalog,tables.catalogCount,out.catalogRecords,m.catalogID,m.parameter,d) ||
            !PHReadU32(m.parameter + PHOrbLeaderSkillOffset,&m.leaderSkill) || m.leaderSkill >= tables.skillCount) return false;
        sources |= 1u << source; m.sourceSlot = uint8_t(source);
        for (unsigned c = 0; c < 6; ++c) m.convert[c] = uint8_t(c);
        bool cooldownKnown = false;
        if (!ObservationSkill(tables,m.parameter,member.skillStage[0],member.skillLevel[0],
            m.rootSkill,m.activeSkill,m.cooldown,cooldownKnown)) return false;
        unsigned activeBudget = 32, leaderBudget = 32;
        m.skillKnown = cooldownKnown && EightSkillEffect(tables,m.activeSkill,m,false,activeBudget);
        m.leaderKnown = EightSkillEffect(tables,m.leaderSkill,m,true,leaderBudget);
    }
    ObservationTables last{}; uint32_t lastRecords = 0; uintptr_t lastBank = 0; uint8_t lastFlags = 0;
    if (!ReadObservationTables(state.identity.header,last) || last.catalog != tables.catalog || last.skills != tables.skills ||
        last.catalogCount != tables.catalogCount || last.skillCount != tables.skillCount ||
        !PHReadU32(save + PHOrbLeaderCatalogRecordsOffset,&lastRecords) || lastRecords != out.catalogRecords ||
        !ReadLeaderOwner(state,lastBank,lastFlags,d) || lastBank != out.teamBank || lastFlags != flags) return false;
    out.valid = true;
    return true;
}

bool ReadEightReceipt(PHOrbSnapshot &state) {
    auto &r = state.eightReceipt;
    r = {};
    const uintptr_t impl = state.identity.impl;
    if (!PHReadU32(impl + PHOrbEightReceiptSkillOffset,&r.skill) ||
        !PHReadU32(impl + PHOrbEightReceiptCatalogOffset,&r.catalogID) ||
        !PHReadU16(impl + PHOrbEightReceiptSlotOffset,&r.slot) ||
        !PHReadU8(impl + PHOrbSkillSelectedSourceOffset,&r.source)) return false;
    r.valid = r.skill > 0 && r.skill < state.eight.skillCount && r.catalogID > 0 &&
        r.catalogID <= INT32_MAX && r.slot < 6 && r.source < 2;
    return true;
}

bool ReadEightAutomation(PHOrbSnapshot &state) {
    if (!PHOrbEightMatchReady(&state)) return true; // Results/transition metadata stays observable.
    // During resolve only metadata is needed. Skill animation is observed to
    // acknowledge the native root/owner/slot and global use count.
    if (state.phase != 2 && state.phase != 22) return true;
    if (state.phase == 2 && !PHOrbMultiplayerWindow(&state)) return true;
    uint32_t key1 = 0, key2 = 0, last1 = 0, last2 = 0;
    const uintptr_t header = state.identity.header;
    PHOrbSnapshot first = state, second = state;
    if (!CheckObservationCode(header) || !ConfirmMultiplayerIdentity(state) ||
        !PHReadU32(header + PHBattleKey1RVA,&key1) || !PHReadU32(header + PHBattleKey2RVA,&key2) ||
        !ReadAutomationFields(first,true) || !CollectEightTeam(first,first.eight,key1,key2) || !ReadEightReceipt(first) ||
        !ReadAutomationFields(second,true) || !CollectEightTeam(second,second.eight,key1,key2) || !ReadEightReceipt(second) ||
        !PHOrbSameEightTeam(&first.eight,&second.eight) ||
        std::memcmp(&first.eightReceipt,&second.eightReceipt,sizeof(first.eightReceipt)) ||
        std::memcmp(&first.automation,&second.automation,sizeof(first.automation))) return false;
    if (state.phase == 2 && (state.settled || state.dragged) && !first.automation.busy) {
        if (!CheckEnemyComboCode(header) || !CollectEnemyCombo(first,key1,key2,first.enemyCombo) ||
            !CollectEnemyCombo(second,key1,key2,second.enemyCombo) || !PHOrbSameEnemyCombo(&first.enemyCombo,&second.enemyCombo)) return false;
    }
    PHOrbSnapshot final = state; uint32_t phase = 0, wave = 0, moves = 0; uintptr_t dragged = 0;
    if (!ConfirmMultiplayerIdentity(state) || !ReadMultiplayerFields(final) || !SameMultiplayerRead(state.multiplayer,final.multiplayer) ||
        !PHReadU32(header + PHBattleKey1RVA,&last1) || last1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA,&last2) || last2 != key2 ||
        !Decode(state.identity.impl + PHBattlePhaseOffset,key1,key2,0x766cfc6du,phase) || phase != state.phase ||
        !Decode(state.identity.impl + PHBattleWaveOffset,key1,key2,0x766cfc6du,wave) || wave != state.wave ||
        !PHReadU32(state.identity.impl + PHOrbMoveSerialOffset,&moves) || moves != state.moves ||
        !PHReadPointer(state.identity.impl + PHBattleDragPointerOffset,&dragged) || dragged != state.dragged) return false;
    state.automation = first.automation; state.eight = first.eight; state.enemyCombo = first.enemyCombo;
    state.eightReceipt = first.eightReceipt;
    return true;
}
