// Conditional row traversal over an already captured graph. The only extra
// memory reads here are optional code guards. A separately guarded dispatch
// capture can add conditional post-selection checkpoints; never authorization.
namespace ES = PHEnemySelector;

bool CheckEnemySelectorCode(uintptr_t header) {
    for (const auto &guard : PHOrbEnemySelectorTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}
NSArray *EnemySelectorIssues(uint32_t issues) {
    NSMutableArray *out = [NSMutableArray array];
    const char *names[]{"missing-definition", "unsupported-opcode", "invalid-jump", "repeated-state",
        "state-limit", "step-limit", "candidate-limit", "weighted-pool-limit"};
    for (unsigned i = 0; i < 8; ++i) if (issues & (1u << i)) [out addObject:[NSString stringWithUTF8String:names[i]]];
    return out;
}
NSArray *EnemySelectorRows(uint64_t mask) {
    NSMutableArray *out = [NSMutableArray array];
    for (unsigned i = 0; i < EA::Rows; ++i) if (mask & (UINT64_C(1) << i)) [out addObject:@(i + 1)];
    return out;
}
NSDictionary *EnemySelectorSequence(const EA::Graph &graph, int32_t id) {
    const auto sequence = ES::DescribeSequence(graph, id);
    NSMutableArray *steps = [NSMutableArray array];
    for (unsigned i = 0; i < sequence.count; ++i) {
        const auto &step = sequence.steps[i];
        NSMutableArray *events = [NSMutableArray array];
        if (step.definitionKnown && step.kindKnown && !step.skipped) {
            if (step.opcode == 82) [events addObject:@{@"kind":@"negative-selection-fallback", @"normalAttackRetained":@YES}];
            else {
                if (step.attachedPercent > 0) [events addObject:@{@"kind":@"attached-attack",
                    @"percentRaw":@(step.attachedPercent), @"damageKnown":@NO}];
                [events addObject:@{@"kind":step.opcode == 88 ? @"awoken-bind-attempt" : @"assist-void-attempt",
                    @"parameter0Raw":@(step.parameter0), @"effectiveDurationKnown":@NO}];
            }
        } else if (!step.skipped) [events addObject:@{@"kind":@"unknown-effect", @"damageKnown":@NO}];
        [steps addObject:@{@"index":@(i + 1), @"actionID":@(step.id), @"definitionKnown":@(step.definitionKnown),
            @"opcode":step.definitionKnown ? @(step.opcode) : NSNull.null, @"skippedCompound82":@(step.skipped),
            @"eligibilityUnknown":@(sequence.compound && !step.skipped), @"eventsInOrder":events}];
    }
    return @{@"actionID":@(id), @"compound":@(sequence.compound), @"steps":steps,
        @"definitionOrderKnown":@(sequence.structureKnown), @"normalAttackFallbackRetained":@(sequence.fallbackRetained),
        @"executionKnown":@NO, @"damageBoundKnown":@NO, @"effectsResolved":@NO};
}
NSDictionary *EnemySelectorTrace(const EnemyActionEntry &enemy, const EA::Graph &graph, ES::Budget &budget,
    const EnemyDispatchCapture *dispatch = nullptr, unsigned slot = 0) {
    if (!enemy.rowsKnown || !enemy.stateKnown || (enemy.flags & 0x100u))
        return @{@"status":@"unavailable", @"nextEntryKnown":@NO, @"enemyForecastKnown":@NO};
    ES::Input input{};
    input.mode = (Load<uint16_t>(enemy.definitionHeader + 4) & 1u) ? ES::Mode::OrderedWeighted : ES::Mode::LegacyTwoPass;
    input.flags = enemy.aiFlags; input.counter = enemy.aiCounter; input.level = enemy.level; input.budget = enemy.aiBudget;
    for (unsigned i = 0; i < EA::Rows; ++i) {
        const uint8_t *p = enemy.rows + 8 * i;
        input.rows[i] = {Load<int32_t>(p), p[4], p[5]};
    }
    const auto result = ES::Analyze(input, graph, budget);
    NSMutableArray *candidates = [NSMutableArray array], *sequences = [NSMutableArray array];
    NSMutableSet *described = [NSMutableSet set];
    for (const auto &candidate : result.candidates) {
        NSMutableDictionary *item = [@{@"row":@(candidate.row + 1), @"actionID":@(candidate.id),
            @"origin":[NSString stringWithUTF8String:ES::OriginName(candidate.origin)],
            @"nativeHPPercent":@{@"minimum":@(candidate.hp.low), @"maximum":@(candidate.hp.high),
                @"minimumExclusive":@(candidate.hp.lowOpen), @"maximumExclusive":@(candidate.hp.highOpen)},
            @"flagsAtReturn":@(candidate.flags), @"counterAtReturn":@(candidate.counter),
            @"selectionGuaranteed":@NO} mutableCopy];
        if (dispatch && slot < EA::MaxEnemies) item[@"postSelectionCheckpoint"] =
            EnemyDispatchSelectionTrace(enemy, dispatch->enemies[slot], graph, candidate.id);
        [candidates addObject:item];
        if (![described containsObject:@(candidate.id)]) {
            [described addObject:@(candidate.id)];
            [sequences addObject:EnemySelectorSequence(graph, candidate.id)];
        }
    }
    return @{@"status":result.rowWalkComplete() ? @"bounded-rows" : @"unknown", @"version":@(ES::Version),
        @"scope":@"ordinary-row-zero-selector-with-current-AI-state",
        @"mode":input.mode == ES::Mode::OrderedWeighted ? @"ordered-weighted" : @"legacy-two-pass",
        @"conditionalWalkComplete":@(result.rowWalkComplete()),
        @"candidateSet":result.rowWalkComplete() ? @"conditional-conservative-superset" : @"partial-with-unknown-frontier",
        @"hpAssumption":@"all-native-percentages-0-through-100-no-current-HP-pruning",
        @"aiStateProjected":@NO, @"nextEntryKnown":@NO, @"enemyForecastKnown":@NO,
        @"probabilitiesEvaluated":@NO, @"feasibilityEvaluated":@NO, @"enemyOrderCombined":@NO,
        @"noSelectionRetained":@(result.noSelectionPossible), @"normalAttackFallbackRetained":@YES,
        @"damageBoundKnown":@NO, @"survivalKnown":@NO,
        @"states":@(result.states), @"steps":@(result.steps), @"issues":EnemySelectorIssues(result.issues),
        @"missingRows":EnemySelectorRows(result.missingRows), @"unsupportedRows":EnemySelectorRows(result.unsupportedRows),
        @"invalidJumpRows":EnemySelectorRows(result.invalidJumpRows), @"candidates":candidates, @"actionSequences":sequences,
        @"outsideScope":@[@"dispatch-entry-and-countdown", @"cached-or-compound-continuation", @"special-or-override-entry",
            @"player-or-other-enemy-state-changes", @"post-player-AI-state", @"next-floor-preemptives"]};
}
