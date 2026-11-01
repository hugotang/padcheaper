// Independent optional reads. Failure removes only this diagnostic; the
// existing action capture, selector walk and all operation gates are separate.
namespace ED = PHEnemyDispatch;
struct EnemyDispatchExtra {
    uint32_t interpolation[2]{}, countWords[4]{};
    uint16_t protectedCount = 0;
    uint8_t survivalFlags = 0;
    bool countKnown = false;
};
struct EnemyDispatchCapture {
    ED::Context context{};
    EnemyDispatchExtra enemies[EA::MaxEnemies]{};
    uintptr_t dungeon = 0; // Private identities; never exported.
    EA::Record redirectDefinition{};
    uint32_t redirectWords[12]{};
    int32_t redirectAction = 0;
    unsigned readBytes = 0, readCalls = 0;
    uint16_t dungeonFlags = 0;
    uint8_t redirectFlags = 0;
    bool redirectDefinitionKnown = false, readBudgetExceeded = false;
};
static_assert(std::is_trivially_copyable<EnemyDispatchCapture>::value, "optional dispatch capture stays plain");
bool CheckEnemyDispatchCode(uintptr_t header) {
    for (const auto &guard : PHOrbEnemyDispatchTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}
bool EnemyDispatchRead(EnemyDispatchCapture &capture, uintptr_t address, void *out, size_t bytes) {
    if (bytes > ED::MaxReadBytes - capture.readBytes || capture.readCalls == ED::MaxReads) {
        capture.readBudgetExceeded = true; return false;
    }
    capture.readBytes += unsigned(bytes); ++capture.readCalls;
    return address <= UINTPTR_MAX - bytes && PHMemoryHasProtection(address, bytes, VM_PROT_READ, VM_PROT_EXECUTE) &&
        PHReadMemory(address, out, bytes);
}
bool CollectEnemyDispatch(const PHOrbSnapshot &state, const EnemyActionCapture &actions,
    uint32_t key1, uint32_t key2, EnemyDispatchCapture &out) {
    std::memset(&out, 0, sizeof(out));
    if (actions.enemyCount > EA::MaxEnemies) return false;
    const uintptr_t impl = state.identity.impl;
    uintptr_t active = 0;
    auto read = [&](uintptr_t p, void *bytes, size_t n) { return EnemyDispatchRead(out, p, bytes, n); };
    auto &c = out.context;
    if (!read(state.identity.header + PHBattleImplRVA, &active, sizeof(active)) || active != impl ||
        !read(impl + PHOrbEnemyDispatchCacheOffset, &c.cacheMode, sizeof(c.cacheMode)) ||
        !read(impl + PHOrbEnemyDispatchHookOffset, &c.preDispatchHook, sizeof(c.preDispatchHook)) ||
        !read(impl + PHOrbEnemyDispatchSpecialOffset, &c.specialMarker, sizeof(c.specialMarker)) ||
        !read(impl + PHOrbEnemyDispatchLoopOffset, &c.loopState, sizeof(c.loopState)) ||
        !read(impl + PHBattleDungeonPointerOffset, &out.dungeon, sizeof(out.dungeon))) return false;
    if (out.dungeon && (out.dungeon > UINTPTR_MAX - 26 ||
        !read(out.dungeon + 24, &out.dungeonFlags, sizeof(out.dungeonFlags)))) return false;
    c.actionsEnabled = out.dungeon && (out.dungeonFlags & 0x80u);
    // The native predicate reads the same global impl identity checked above.
    // A matching marker keeps its remaining runtime predicate unresolved.
    if (!read(impl + PHOrbEnemyDispatchRedirectOffset, out.redirectWords, sizeof(out.redirectWords))) return false;
    uint32_t slot = 0, action = 0, flags = 0;
    if (!PHBattleDecodeProtected(out.redirectWords, key1, key2, 0x6a875a8bu, &slot)) return false;
    const int8_t signedSlot = int8_t(uint8_t(slot));
    if (slot != uint32_t(uint8_t(signedSlot)) && slot != uint32_t(int32_t(signedSlot))) return false;
    c.redirectSlot = signedSlot;
    c.redirect = ED::Redirect::Inactive;
    if (signedSlot >= 0) {
        c.redirect = ED::Redirect::Unknown;
        if (!PHBattleDecodeProtected(out.redirectWords + 8, key1, key2, 0x6a875a8bu, &action) ||
            !PHBattleDecodeProtected(out.redirectWords + 4, key1, key2, 0x6a875a8bu, &flags) || flags > UINT8_MAX) return false;
        out.redirectAction = int32_t(action); out.redirectFlags = uint8_t(flags);
        // Negative IDs are not passed to the native lookup. Missing captured
        // definitions remain unknown, including the otherwise valid ID zero.
        if (out.redirectAction >= 0 && action < actions.tables.actionCount) {
            if (!read(actions.tables.actions + size_t(action) * EA::RecordBytes,
                out.redirectDefinition.bytes.data(), EA::RecordBytes)) return false;
            out.redirectDefinitionKnown = true;
            c.redirect = out.redirectDefinition.opcode() == 180 ? ED::Redirect::RowChange : ED::Redirect::OtherAction;
            if (c.redirect == ED::Redirect::RowChange) c.redirectRow = out.redirectDefinition.parameter((flags & 2u) ? 6 : 7);
        }
    }
    for (unsigned i = 0; i < actions.enemyCount; ++i) {
        const auto &enemy = actions.enemies[i]; auto &extra = out.enemies[i];
        if (!enemy.memoryKnown || (enemy.flags & 0x100u)) continue;
        const uintptr_t p = impl + PHOrbEnemyOffset + i * PHOrbEnemyStride;
        if (!read(p + 60, &extra.survivalFlags, sizeof(extra.survivalFlags)) ||
            !read(p + 904, extra.interpolation, sizeof(extra.interpolation)) ||
            !read(p + 2736, extra.countWords, sizeof(extra.countWords))) return false;
        uint32_t count = 0;
        extra.countKnown = PHBattleDecodeProtected(extra.countWords, key1, key2, 0x6a875a8bu, &count) && count <= UINT16_MAX;
        if (extra.countKnown) extra.protectedCount = uint16_t(count);
    }
    c.known = true;
    return true;
}
ED::Enemy EnemyDispatchInput(const EnemyActionEntry &enemy, const EnemyDispatchExtra &extra) {
    ED::Enemy out;
    out.known = enemy.memoryKnown && enemy.stateKnown && enemy.rowsKnown && !(enemy.flags & 0x100u);
    out.hp = enemy.hp; out.flags = enemy.flags; out.survivalFlags = extra.survivalFlags;
    out.compound = enemy.compound; out.selected = enemy.selected; out.special = enemy.special;
    out.countdown = enemy.countdown; out.overrideState = enemy.overrideState;
    out.overrideAction = enemy.overrideAction; out.attackAddition = int32_t(enemy.attackAddition);
    out.ordered = (Load<uint16_t>(enemy.definitionHeader + 4) & 1u) != 0;
    float interpolation[2]; std::memcpy(interpolation, extra.interpolation, sizeof(interpolation));
    // Native FCMP: +0 == -0; NaN is unequal even when its bit patterns match.
    out.interpolationEqual = interpolation[0] == interpolation[1];
    return out;
}
NSDictionary *EnemyDispatchSelectionTrace(const EnemyActionEntry &enemy, const EnemyDispatchExtra &extra,
    const EA::Graph &graph, int32_t selected) {
    ED::SelectionInput input;
    input.actionCount = enemy.actionCount; input.compound = enemy.compound; input.budget = enemy.aiBudget;
    input.maximumBudget = Load<int16_t>(enemy.definitionHeader + 6);
    input.regeneration = Load<int16_t>(enemy.definitionHeader + 8);
    input.protectedCount = extra.protectedCount; input.protectedCountKnown = extra.countKnown;
    const auto *node = graph.find(selected);
    const auto out = ED::AfterFreshSelection(input, selected, node && node->known ? &node->record : nullptr);
    return @{@"status":out.checkpointKnown ? @"conditional-checkpoint" : @"partial-prefix",
        @"scope":@"after-assumed-fresh-selection-before-action-preparation",
        @"selectionReturnAssumed":@YES, @"actionCountAtCheckpoint":@(out.actionCount),
        @"protectedCountKnown":@(out.protectedCountKnown),
        @"protectedCountAtCheckpoint":out.protectedCountKnown ? @(out.protectedCount) : NSNull.null,
        @"budgetAfterRegeneration":@(out.budgetAfterRegeneration),
        @"budgetAtCheckpoint":out.checkpointKnown ? @(out.budgetAtCheckpoint) : NSNull.null,
        @"selectedAtCheckpoint":out.checkpointKnown ? @(out.selected) : NSNull.null,
        @"compoundAtCheckpoint":out.checkpointKnown ? @(out.compound) : NSNull.null,
        @"costApplied":out.checkpointKnown ? @(out.costApplied) : NSNull.null,
        @"costLow16":out.costApplied ? @(out.chargedCost) : NSNull.null,
        @"compoundWritten":out.checkpointKnown ? @(out.compoundWritten) : NSNull.null,
        @"negativeSelectionFallback":out.checkpointKnown ? @(out.negativeFallback) : NSNull.null,
        @"normalAttackFallbackRetained":@YES,
        @"preparation":!out.checkpointKnown ? @"unknown" : out.negativeFallback ? @"negative-return" :
            out.compoundPreparation ? @"compound" : @"ordinary",
        @"postPreparationStateKnown":@NO, @"nextEntryKnown":@NO, @"enemyForecastKnown":@NO,
        @"damageBoundKnown":@NO, @"survivalKnown":@NO};
}
NSDictionary *EnemyDispatchEntryTrace(const ED::Entry &entry) {
    const bool classified = entry.route != ED::Route::Unknown && entry.route != ED::Route::PreDispatchHook &&
        entry.route != ED::Route::SpecialUnresolved;
    return @{@"route":[NSString stringWithUTF8String:ED::RouteName(entry.route)],
        @"conditionalRouteClassified":@(classified), @"selectedCacheCleared":@(entry.cacheCleared),
        @"selectedAfterCacheClear":@(entry.selectedAfterClear),
        @"ordinaryRowZero":@(entry.route == ED::Route::OrdinaryRows),
        @"redirectRow":entry.route == ED::Route::RedirectedRows ? @(entry.redirectRow) : NSNull.null,
        @"directSelection":entry.directSelectionKnown ? @(entry.directSelection) : NSNull.null,
        @"executionKnown":@NO, @"nextEntryKnown":@NO};
}
NSDictionary *EnemyDispatchTrace(const EnemyActionEntry &enemy, const EnemyDispatchCapture &capture,
    const EA::Graph &graph, unsigned slot) {
    const auto &extra = capture.enemies[slot]; const auto input = EnemyDispatchInput(enemy, extra);
    if (!input.known) return @{@"status":@"unavailable", @"nextEntryKnown":@NO, @"enemyForecastKnown":@NO};
    const auto entry = ED::Classify(capture.context, input, slot);
    const auto tick = ED::ConditionalTick(capture.context, input);
    auto tickEnemy = input; auto tickContext = capture.context;
    tickEnemy.countdown = tick.countdown; tickContext.cacheMode = tick.cacheMode;
    NSMutableDictionary *trace = [@{@"status":@"conditional", @"version":@(ED::Version),
        @"scope":@"captured-context-scheduler-checkpoints",
        @"survivalFlagsRaw":@(extra.survivalFlags),
        @"interpolationWordsRaw":@[@(extra.interpolation[0]), @(extra.interpolation[1])],
        @"protectedCountKnown":@(extra.countKnown),
        @"protectedCountCurrent":extra.countKnown ? @(extra.protectedCount) : NSNull.null,
        @"current":EnemyDispatchEntryTrace(entry),
        @"afterConditionalTick":@{@"countdown":@(tick.countdown), @"decremented":@(tick.decremented),
            @"cacheMode":@(tick.cacheMode), @"entry":EnemyDispatchEntryTrace(ED::Classify(tickContext, tickEnemy, slot))},
        @"assumptions":@[@"scheduler-invocation-assumed", @"tick-routine-reached-without-external-early-return",
            @"all-other-captured-state-unchanged", @"no-player-damage-skills-or-phase-change",
            @"no-other-enemy-or-hook-state-changes"],
        @"postPlayerStateKnown":@NO, @"nextEntryKnown":@NO, @"enemyForecastKnown":@NO,
        @"actionReachabilityKnown":@NO, @"damageBoundKnown":@NO, @"survivalKnown":@NO} mutableCopy];
    if (entry.directSelectionKnown) trace[@"postSelectionCheckpoint"] = EnemyDispatchSelectionTrace(enemy, extra, graph, entry.directSelection);
    return trace;
}
