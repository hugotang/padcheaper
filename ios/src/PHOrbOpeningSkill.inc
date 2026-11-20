// Action-specific read-only binding, included inside PHOrbNative's namespace.
// Shares raw table readers with diagnostics, never its optional/cached snapshot.
struct OpeningBodySkillBinding {
    ObservationTables tables{};
    uintptr_t parameter = 0;
    uint32_t skill = 0;
    SkillDiagnosticRecord record{};
    bool operator==(const OpeningBodySkillBinding &other) const {
        return tables.catalog == other.tables.catalog && tables.catalogCount == other.tables.catalogCount &&
            tables.catalogRecords == other.tables.catalogRecords && tables.skills == other.tables.skills &&
            tables.skillCount == other.tables.skillCount && parameter == other.parameter && skill == other.skill &&
            !std::memcmp(&record, &other.record, sizeof(record));
    }
};

bool ReadOpeningBodySkillBinding(const PHOrbSnapshot &state, unsigned slot, OpeningBodySkillBinding &out) {
    if (slot >= 6) return false;
    const auto &member = state.automation.members[slot];
    uint16_t catalogID = 0;
    if (!member.active || !member.temporaryKnown || member.temporaryTurns || member.bound || member.state ||
        member.skillStage[0] || member.cachedSkillStage[0] ||
        !PHBattleCatalogIDForNumber(member.number, &catalogID) || member.nativeCatalogIDs[0] != catalogID ||
        !ReadObservationTables(state.identity.header, out.tables) ||
        !ObservationCard(out.tables, catalogID, out.parameter)) return false;
    uintptr_t actual = 0;
    const uintptr_t address = state.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    if (!PHReadPointer(address + PHBattleCardParamOffset, &actual) || actual != out.parameter ||
        !PHReadU32(out.parameter + 84, &out.skill) || !out.skill || out.skill >= 0x20000 ||
        !ObservationSkillRecord(out.tables, out.skill, out.record)) return false;
    // No implicit choice of an evolved/cycling child. Such skills still need
    // an explicit per-step ID and their existing native stage checks.
    return out.record.type != 232 && out.record.type != 233;
}
