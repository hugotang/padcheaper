// Guarded skill-attempt context. The native dialog checks the source cards'
// evolution family, not the displayed transformed number or a name match.
struct SkillSourceState {
    uintptr_t rules = 0;
    uint8_t flags = 0;
    bool operator==(const SkillSourceState &other) const { return rules == other.rules && flags == other.flags; }
};
bool ReadSkillSource(const PHOrbSnapshot &state, SkillSourceState &out) {
    out = {};
    return PHReadPointer(state.identity.impl + PHOrbSkillDungeonRulesOffset, &out.rules) &&
        (!out.rules || (out.rules <= UINTPTR_MAX - 34 && PHReadU8(out.rules + 33, &out.flags)));
}
struct SkillChoiceState {
    uintptr_t catalog = 0;
    uint32_t count = 0, family[2]{};
    uint16_t cards[2]{};
    bool operator==(const SkillChoiceState &other) const {
        return catalog == other.catalog && count == other.count &&
            cards[0] == other.cards[0] && cards[1] == other.cards[1] &&
            family[0] == other.family[0] && family[1] == other.family[1];
    }
};
bool ReadSkillChoice(const PHOrbSnapshot &state, unsigned slot, SkillChoiceState &out) {
    out = {};
    if (slot >= 6) return false;
    const auto &member = state.automation.members[slot];
    if (!member.temporaryKnown || member.temporaryTurns || int16_t(member.assistCatalogID) < 1) return false;
    const uintptr_t save = state.identity.header + PHSaveRVA;
    const uintptr_t address = state.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    if (!PHReadU16(address + PHBattleBaseIDOffset, &out.cards[0]) ||
        !PHReadU16(address + PHBattleAssistIDOffset, &out.cards[1]) ||
        out.cards[0] != member.sourceCatalogIDs[0] || out.cards[1] != member.sourceCatalogIDs[1] ||
        !PHReadU32(save + PHBattleCatalogCountOffset, &out.count) || !out.count || out.count > 65536 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &out.catalog) || !out.catalog ||
        out.catalog > UINTPTR_MAX - size_t(out.count) * PHBattleCatalogStride) return false;
    for (unsigned i = 0; i < 2; ++i) {
        uint32_t id = 0;
        if (!out.cards[i] || out.cards[i] >= out.count ||
            !PHReadU32(out.catalog + out.cards[i] * PHBattleCatalogStride, &id) || id != out.cards[i] ||
            !PHReadU32(out.catalog + out.cards[i] * PHBattleCatalogStride + PHOrbSkillFamilyOffset, &out.family[i])) return false;
    }
    return true;
}
