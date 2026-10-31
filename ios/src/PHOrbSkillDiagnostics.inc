// Shared native record layout. Refusal descriptions below run only after a
// guarded attempt; optional observations read the table without calling any
// native getter. Neither can authorize activation or replace native can-use.
struct SkillDiagnosticRecord {
    uint32_t strings[3];
    uint16_t type, maxLevel, cooldown;
    int16_t levelCooldownDelta; // Signed per-level adjustment, not a mutable cache.
    int32_t params[8];
};
static_assert(sizeof(SkillDiagnosticRecord) == 52, "native skill record layout");

static NSString *SkillOrbCondition(const PHOrbSnapshot &state, unsigned slot,
                                  const NativeSkillReadiness &readiness, bool sameBoard, int selector = -1) {
    if (slot >= 6 || !state.automation.boardKnown || !state.settled || state.dragged ||
        !PHOrbDimensionsSupported(state.board.width, state.board.height)) return nil;
    const auto &member = state.automation.members[slot];
    SkillSourceState source;
    if (!ReadSkillSource(state, source)) return nil;
    // Diagnose the source actually checked by can-use, including a permitted
    // body choice while its same-family assist is already charged.
    const bool assist = selector == 1 || (selector == -1 && !(source.flags & 8u) &&
        int16_t(member.assistCatalogID) > 0 && readiness.totalCooldown > 0 && member.charge >= readiness.totalCooldown);
    const int32_t selected = assist ? readiness.assistSkill : readiness.baseSkill;
    const uintptr_t save = state.identity.header + PHSaveRVA;
    uintptr_t table = 0;
    uint32_t count = 0;
    if (!PHReadU32(save + PHOrbSkillCountOffset, &count) || !count || count > 0x20000 ||
        !PHReadPointer(save + PHBattleSkillPointerOffset, &table) || !table || (table & 3u) ||
        table > UINTPTR_MAX - size_t(count) * sizeof(SkillDiagnosticRecord)) return nil;
    auto readRecord = [&](int32_t id, SkillDiagnosticRecord &record) {
        return id > 0 && uint32_t(id) < count &&
            PHReadMemory(table + size_t(id) * sizeof(record), &record, sizeof(record));
    };
    SkillDiagnosticRecord root{};
    if (!readRecord(selected, root) || (root.type != 116 && root.type != 120)) return nil;
    // Native 116 checks direct children; 120 checks only its first child.
    // Do not recursively invent conditions for random/evolving skill wrappers.
    std::array<SkillDiagnosticRecord, 8> conditions{};
    unsigned found = 0;
    for (unsigned i = 0; i < (root.type == 120 ? 1u : 8u); ++i) {
        if (root.params[i] <= 0) continue;
        SkillDiagnosticRecord child{};
        if (!readRecord(root.params[i], child)) return nil;
        if (child.type != 255) continue;
        if (child.params[0] <= 0 || (uint32_t(child.params[0]) & ~0x3ffu) ||
            child.params[1] < 1 || child.params[1] > UINT16_MAX) return nil;
        conditions[found++] = child;
    }
    uintptr_t lastTable = 0;
    uint32_t lastCount = 0;
    if (!found || !PHReadPointer(save + PHBattleSkillPointerOffset, &lastTable) || lastTable != table ||
        !PHReadU32(save + PHOrbSkillCountOffset, &lastCount) || lastCount != count) return nil;
    // The native counter uses alternate dimensions during a resize animation.
    // A normal board snapshot cannot certify counts during that transition.
    float resize = 0, resizeTarget = 0;
    bool countsKnown = sameBoard &&
        PHReadMemory(state.identity.impl + PHOrbSkillBoardResizeOffset, &resize, sizeof(resize)) &&
        PHReadMemory(state.identity.impl + PHOrbSkillBoardResizeOffset + 4, &resizeTarget, sizeof(resizeTarget)) &&
        std::isfinite(resize) && std::isfinite(resizeTarget) && resize == resizeTarget;
    unsigned counts[PHOrbTypes]{};
    for (unsigned i = 0; i < state.board.width * state.board.height; ++i) {
        if (state.board.types[i] >= PHOrbTypes) countsKnown = false;
        else ++counts[state.board.types[i]];
    }
    NSArray<NSString *> *colors = @[@"火珠", @"水珠", @"木珠", @"光珠", @"暗珠", @"心珠", @"邪魔珠", @"毒珠", @"猛毒珠", @"爆彈珠"];
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    bool met = true;
    for (unsigned i = 0; i < found; ++i) {
        for (unsigned color = 0; color < PHOrbTypes; ++color) {
            if (!(uint32_t(conditions[i].params[0]) & (1u << color))) continue;
            const unsigned required = unsigned(conditions[i].params[1]);
            met &= counts[color] >= required;
            [parts addObject:countsKnown ? [NSString stringWithFormat:@"%@至少 %u 顆，目前 %u 顆", colors[color], required, counts[color]] :
                [NSString stringWithFormat:@"%@至少 %u 顆，目前珠數未能確認", colors[color], required]];
        }
    }
    NSString *status = !countsKnown ? @"珠數使用條件" : met ? @"珠數條件已滿足，遊戲仍判定不可用" : @"使用條件未滿足";
    return [NSString stringWithFormat:@"%@%@：%@", assist ? @"武裝" : @"本體", status, [parts componentsJoinedByString:@"；"]];
}
