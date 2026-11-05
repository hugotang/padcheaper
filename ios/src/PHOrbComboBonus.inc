// PAD 23.6.0: read the effective battle descriptors populated by 0x10006FB04
// and 0x10006B0BC. This naturally follows leader swaps, forms, dungeon badge
// overrides and badge upgrades. No saved badge ID or localized card text.
bool CheckComboBonusCode(uintptr_t header) {
    if (!CheckObservationCode(header)) return false;
    for (const auto &guard : PHOrbComboBonusTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva,reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva,bytes,sizeof(bytes)) || std::memcmp(bytes,guard.bytes,sizeof(bytes))) return false;
    }
    return true;
}
bool CollectComboBonus(const PHOrbSnapshot &state, uint32_t key1, uint32_t key2, PHOrbComboBonusState &out) {
    std::memset(&out,0,sizeof(out));
    if (state.multiplayer.kind != PHOrbMultiplayerNone || !CheckComboBonusCode(state.identity.header)) return false;
    const uintptr_t impl = state.identity.impl;
    ObservationTables tables{}; SkillSourceState dungeon{};
    uint8_t leaderFlags = 0;
    if (!ReadObservationTables(state.identity.header,tables) || !ReadSkillSource(state,dungeon) || !dungeon.rules ||
        !PHReadU8(dungeon.rules + 32,&leaderFlags) || !state.automation.statusKnown) return false;
    uint64_t signature = UINT64_C(14695981039346656037);
    auto mix = [&](const void *p, size_t n) {
        const uint8_t *b = static_cast<const uint8_t *>(p);
        for (size_t i = 0; i < n; ++i) { signature ^= b[i]; signature *= UINT64_C(1099511628211); }
    };
    auto read = [&](uintptr_t address, void *p, size_t n) {
        if (!PHReadMemory(address,p,n)) return false;
        mix(p,n); return true;
    };
    auto u16 = [](const uint8_t *p) { uint16_t v; std::memcpy(&v,p,2); return v; };
    auto u32 = [](const uint8_t *p) { uint32_t v; std::memcpy(&v,p,4); return v; };
    mix(&leaderFlags,1); mix(&dungeon.flags,1);
    mix(&tables.catalog,sizeof(tables.catalog)); mix(&tables.catalogCount,sizeof(tables.catalogCount));
    mix(&tables.skills,sizeof(tables.skills)); mix(&tables.skillCount,sizeof(tables.skillCount));
    auto add = [&](unsigned source, unsigned kind, unsigned condition, unsigned minimum, unsigned combos,
                   const uint8_t *colors = nullptr) {
        if (!combos || !condition) return true;
        if (combos > 20 || minimum > PHOrbMaxCells || out.count >= PHOrbMaxComboEffects) return false;
        auto &e = out.effects[out.count++]; e.source = uint8_t(source); e.kind = uint8_t(kind);
        e.condition = condition; e.minimum = uint8_t(minimum); e.combos = uint16_t(combos);
        if (colors) std::memcpy(e.colors,colors,5);
        return true;
    };
    for (unsigned source = 0; source < 2; ++source) {
        const unsigned slot = source ? 5 : 0;
        const auto &m = state.automation.members[slot];
        uint8_t active = 0; uintptr_t parameter = 0, actual = 0; uint32_t id = 0, skill = 0, sourceSlot = 0;
        const uintptr_t member = impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        if (!read(member,&active,1)) return false;
        mix(&m.bound,sizeof(m.bound));
        if (active != 1 || m.bound || (leaderFlags & 0x80u)) continue;
        if (!read(member + PHBattleCatalogIDOffset,&id,4) || !ObservationCard(tables,id,parameter) ||
            !PHReadPointer(member + PHBattleCardParamOffset,&actual) || actual != parameter ||
            !read(parameter + PHOrbLeaderSkillOffset,&skill,4) || skill >= tables.skillCount ||
            !SmallProtected(member + PHBattleSourceSlotOffset,key1,key2,5,sourceSlot)) return false;
        mix(&sourceSlot,sizeof(sourceSlot));
        const uintptr_t base = impl + PHOrbComboBonusLeaderOffset + source * 352;
        uint8_t groups[9]{}, link[6]{}, ell[5]{}, cross[2]{}, colors[6]{}, allLinks[8]{};
        if (!PHReadMemory(base + 156,groups,sizeof(groups)) || !PHReadMemory(base + 180,link,sizeof(link)) ||
            !PHReadMemory(base + 186,ell,sizeof(ell)) || !PHReadMemory(base + 166,cross,sizeof(cross)) ||
            !PHReadMemory(impl + PHOrbComboBonusColorsOffset + source * 16,colors,sizeof(colors)) ||
            !PHReadMemory(impl + PHOrbComboBonusAllLinksOffset + source * 16,allLinks,sizeof(allLinks))) return false;
        // Only descriptor inputs are identity. Last-turn activation marks,
        // consumed-color masks and counters reset on grab and are not inputs.
        mix(groups,6); mix(groups + 7,2); mix(link,4); mix(link + 5,1);
        mix(ell,3); mix(ell + 4,1); mix(cross,2); mix(colors,4); mix(colors + 5,1);
        mix(allLinks,2); mix(allLinks + 4,2); mix(allLinks + 7,1);
        if (groups[7] && groups[5] && !add(source,PHOrbComboGroups,1,groups[5],groups[7],groups)) return false;
        if (!add(source,PHOrbComboLink,u16(link),link[2],link[3]) ||
            !add(source,PHOrbComboL,u16(ell),5,ell[2]) ||
            !add(source,PHOrbComboHeartCross,32,5,cross[0]) ||
            !add(source,PHOrbComboColors,u16(colors),colors[3],colors[2])) return false;
        // Type 192 requires a connected match in EVERY selected color.
        if (!add(source,PHOrbComboAllLinks,u16(allLinks),allLinks[4],allLinks[5])) return false;
        for (unsigned k = 0; k < 3; ++k) {
            uint8_t effect[28]{};
            if (!PHReadMemory(base + 268 + k * 28,effect,sizeof(effect)) ||
                !add(source,k == 0 ? PHOrbComboAwakenings : k == 1 ? PHOrbComboColumns : PHOrbComboAwakening,
                    u32(effect),1,u16(effect + 22))) return false;
            mix(effect,20); mix(effect + 22,2);
        }
    }
    for (unsigned k = 0; k < 3; ++k) {
        uint8_t effect[28]{};
        if (!PHReadMemory(impl + PHOrbComboBonusBadgeOffset + k * 28,effect,sizeof(effect)) ||
            !add(2,k == 0 ? PHOrbComboAwakenings : k == 1 ? PHOrbComboAwakening : PHOrbComboColumns,
                u32(effect),1,u16(effect + 22))) return false;
        mix(effect,20); mix(effect + 22,2);
    }
    // Prove providers from current effective awakenings and attributes. An
    // unknown provider contributes nothing; it cannot make a shape activate.
    const bool awakenings = !(dungeon.flags & 4u) && !(state.automation.playerEffects & PHOrbPlayerAwokenBind);
    const unsigned ids[] = {27,48,60,78,126,149,150,151,152,153};
    unsigned needed = 0;
    for (unsigned i = 0; i < out.count; ++i) {
        const auto &e = out.effects[i];
        if (e.kind == PHOrbComboAwakenings) needed |= e.condition & 31u;
        if (e.kind == PHOrbComboColumns) needed |= (e.condition & 31u) << 5;
        if (e.kind == PHOrbComboAwakening)
            for (unsigned k = 0; k < 10; ++k) if (e.condition == ids[k]) needed |= 1u << k;
    }
    unsigned (*weights[])(unsigned) = {
        +[](unsigned a){return unsigned(a == 27 || a == 96);},
        +[](unsigned a){return unsigned(a == 48 || a == 109);},
        +[](unsigned a){return unsigned(a == 60 || a == 108);},
        +[](unsigned a){return unsigned(a == 78 || a == 110);},
        +[](unsigned a){return unsigned(a == 126);},
        +[](unsigned a){return unsigned(a == 149);}, +[](unsigned a){return unsigned(a == 150);},
        +[](unsigned a){return unsigned(a == 151);}, +[](unsigned a){return unsigned(a == 152);},
        +[](unsigned a){return unsigned(a == 153);}
    };
    for (unsigned slot = 0; slot < 6; ++slot) {
        const auto &m = state.automation.members[slot];
        const uintptr_t address = impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        if (!m.active || m.bound) continue;
        uint32_t id = 0; uintptr_t parameter = 0, actual = 0, profile = 0; uint8_t colors = 0, variant = 0, active = 0;
        if (!PHReadU8(address,&active) || active != 1 ||
            !PHReadU32(address + PHBattleCatalogIDOffset,&id) || !ObservationCard(tables,id,parameter) ||
            !PHReadPointer(address + PHBattleCardParamOffset,&actual) || actual != parameter ||
            !ObservationAttributes(address,parameter,colors)) continue;
        out.attackColors |= colors;
        if (!awakenings || !PHReadPointer(address + PHOrbObservationProfileOffset,&profile) ||
            (m.assistCatalogID && (!PHReadU8(address + 1444,&variant) || variant))) continue;
        for (unsigned k = 0; k < 10; ++k) {
            if (!(needed & (1u << k))) continue;
            unsigned count = 0;
            if (!ObservationAwakenings(address,parameter,profile,tables,m,key1,key2,count,weights[k]) || !count) continue;
            if (k < 5) out.awakeningColors[k] |= colors;
            else if (colors & (1u << (ids[k] - 149))) out.columnColors |= uint8_t(1u << (ids[k] - 149));
        }
    }
    // Dynamic leader grants can add providers; omitted until their recipient
    // predicates are modelled. They never remove the above proven providers.
    out.sourceSignature = signature; out.valid = true;
    return true;
}
