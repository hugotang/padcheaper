// Interprets every captured row, independently of the conditional selector's
// supported frontier. No action is excluded by current HP or probability.
namespace ET = PHEnemyThreat;
bool CheckEnemyThreatCode(uintptr_t header) {
    for (const auto &guard : PHOrbEnemyThreatTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}
NSDictionary *EnemyThreatAction(const EA::Graph &graph, int32_t id, const EnemyActionEntry &enemy) {
    const auto sequence = ET::Describe(graph,id);
    NSMutableArray *events = [NSMutableArray array];
    for (unsigned i = 0; i < sequence.count; ++i) {
        const auto &event = sequence.events[i];
        [events addObject:@{@"index":@(i + 1), @"kind":[NSString stringWithUTF8String:ET::Name(event.kind)],
            @"actionID":@(event.actionID), @"opcode":@(event.opcode), @"value":@(event.value),
            @"maximum":@(event.maximum), @"colors":@(event.colors), @"targets":@(event.targets),
            @"attached":@(event.attached), @"optionalHit":@(event.optional), @"randomColor":@(event.randomColor)}];
    }
    const ET::AttackState attack{int64_t(enemy.attack), int32_t(enemy.attackAddition),
        enemy.enrage, enemy.enrageTurns, enemy.stateKnown};
    // No current HP/maxHP is offered as a proof of the post-player cap. Pure
    // attack bounds can be shown; gravity's HP operand stays unavailable.
    const auto bound = ET::BeforeShields(sequence,attack,0);
    return @{@"actionID":@(id), @"eventOrderKnown":@(sequence.complete()), @"events":events,
        @"issues":@(bound.issues), @"preShieldBoundKnown":@(bound.known),
        @"preShieldDamageUpper":bound.known ? @(bound.damage) : NSNull.null,
        @"hitsUpper":@(bound.hits), @"effectKinds":@(bound.effects),
        @"attackStateAssumption":@"captured-current-enemy-values-without-special-mode",
        @"executionKnown":@NO, @"postPlayerStateKnown":@NO, @"survivalKnown":@NO};
}
NSDictionary *EnemyThreatTrace(const EnemyActionEntry &enemy, const EA::Graph &graph, bool codeKnown) {
    if (!codeKnown || !enemy.rowsKnown || !enemy.stateKnown || (enemy.flags & 0x100u))
        return @{@"status":@"unavailable", @"enemyForecastKnown":@NO, @"survivalKnown":@NO};
    NSMutableArray *actions = [NSMutableArray array]; NSMutableSet *seen = [NSMutableSet set];
    // Empty/no-selected rows are never interpreted as a harmless enemy turn.
    [actions addObject:EnemyThreatAction(graph,-1,enemy)];
    for (unsigned row = 0; row < EA::Rows; ++row) {
        const int32_t id = Load<int32_t>(enemy.rows + 8 * row);
        if (!id || [seen containsObject:@(id)]) continue;
        [seen addObject:@(id)];
        if (id < 0) {
            [actions addObject:@{@"actionID":@(id), @"eventOrderKnown":@NO, @"preShieldBoundKnown":@NO,
                @"issues":@(ET::Missing), @"survivalKnown":@NO}];
            continue; // A malformed row ID is distinct from the native no-selection fallback.
        }
        const auto *node = graph.find(id);
        if (node && node->known && EA::DescribeControl(node->record.opcode(),0).name) continue;
        [actions addObject:EnemyThreatAction(graph,id,enemy)];
    }
    return @{@"status":@"conditional-events", @"version":@(ET::Version), @"actions":actions,
        @"scope":@"all-captured-row-definitions-plus-normal-fallback",
        @"enemyForecastKnown":@NO, @"nextFloorKnown":@NO, @"survivalKnown":@NO,
        @"missingForDefense":@[@"post-player-branches", @"post-player-HP-healing",
            @"per-hit-effective-shields", @"status-dependent-team-refresh", @"next-floor-preemptives", @"next-rescue-readiness"]};
}
