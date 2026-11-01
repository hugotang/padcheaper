// Included by PHOrbPlanning.m. No native reads and no saved-profile mutation.
static NSDictionary *AssistBlocked(NSMutableDictionary *plan, NSString *code, NSString *message) {
    plan[@"available"] = @NO; plan[@"message"] = message; plan[@"assistRecoveryMessage"] = message;
    plan[@"assistRecoveryBlocker"] = code;
    return [plan copy];
}

static BOOL AddAssistL(PHOrbGoalSet *goals, unsigned color, unsigned count) {
    if (!count) return YES;
    for (unsigned i = 0; i < goals->count; ++i) {
        PHOrbGoal *goal = &goals->items[i];
        if (goal->kind == PHOrbGoalL && goal->colors == (1u << color)) {
            goal->count = (uint8_t)MAX(count, goal->count); return YES;
        }
    }
    if (goals->count == PHOrbMaxGoals) return NO;
    goals->items[goals->count++] = (PHOrbGoal){PHOrbGoalL, (uint8_t)(1u << color), (uint8_t)count, 0};
    return YES;
}

NSDictionary *PHOrbPlanWithAssistRecovery(NSDictionary *plan, BOOL enabled, const PHOrbSnapshot *state, BOOL automatic) {
    if (![plan isKindOfClass:NSDictionary.class]) plan = @{};
    NSMutableDictionary *result = [plan mutableCopy];
    result[@"assistRecoveryActive"] = @NO; result[@"assistRecoveryMessage"] = @"";
    result[@"assistRecoveryCandidates"] = @[];
    result[@"assistRecoveryInventory"] = @[]; result[@"assistRecoveryBlocker"] = @"";
    if (!enabled) return [result copy];
    if (!automatic) {
        result[@"assistRecoveryMessage"] = @"武裝回復 L 僅用於單人連續模式"; return [result copy];
    }
    PHOrbGoalSet base = {0};
    if (![plan[@"available"] boolValue] || !PHOrbReadGoals(plan[@"goals"], &base))
        return AssistBlocked(result, @"base-plan-unavailable", plan[@"message"] ?: @"原有目標無法確認，未加入武裝回復 L");
    result[@"assistRecoveryBaseGoals"] = PHOrbGoalsJSON(&base);
    if (!state || !PHOrbCanPreview(state) || !state->automation.valid || state->automation.busy ||
        state->multiplayer.kind != PHOrbMultiplayerNone || state->identity.sourceMode != 0 ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) || !state->assistRecovery.valid)
        return AssistBlocked(result, @"state-unavailable", @"武裝回復 L：目前單人狀態或機制資料未確認，已停止");
    const PHOrbAssistRecoveryState *evidence = &state->assistRecovery;
    result[@"assistVoidTurns"] = @(evidence->turns);
    if (!evidence->turns) {
        result[@"assistRecoveryMessage"] = @"目前沒有武裝無效，使用原有目標"; return [result copy];
    }
    result[@"assistRecoveryActive"] = @YES;
    if (evidence->dungeonFlags & 8u)
        return AssistBlocked(result, @"assist-prohibited", @"關卡禁止武裝，不能以 L 回復；已停止");
    if ((state->board.minErase ?: 3) > 3 || state->board.minMatch > 5)
        return AssistBlocked(result, @"incompatible-match-rule", @"目前最少消珠規則無法形成有效 L，已停止");
    // Enumerate all five-color allocations, bounded by the board inventory
    // (at most 8 L groups on 7x6). Exact shapes retain their own minimum demand.
    unsigned capacity[5] = {0}, existing[5] = {0}, usable = 0;
    NSMutableArray *inventory = [NSMutableArray array];
    for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
        unsigned minimum = 0, maximum = 0;
        PHOrbColorCountRange(&state->board, color, &minimum, &maximum);
        unsigned baseL = 0;
        for (unsigned i = 0; i < base.count; ++i)
            if (base.items[i].kind == PHOrbGoalL && base.items[i].colors == (1u << color)) baseL = base.items[i].count;
        const unsigned required = PHOrbRequiredShapeOrbs(&base, color), reserved = required - 5u * baseL;
        const unsigned perL = color < 5 ? evidence->perColor[color] : 0;
        const BOOL eligible = perL && !(state->board.unmatchableColors & (1u << color));
        if (eligible) {
            usable |= 1u << color; existing[color] = baseL;
            capacity[color] = minimum >= reserved ? (minimum - reserved) / 5u : 0;
        }
        [inventory addObject:@{@"color":@(color), @"guaranteed":@(minimum), @"possible":@(maximum),
            @"baseShapeOrbs":@(required), @"reservedWithoutL":@(reserved), @"existingL":@(baseL),
            @"eligible":@(eligible), @"turnsPerL":@(perL), @"lCapacity":@(eligible ? capacity[color] : 0)}];
    }
    result[@"assistRecoveryInventory"] = [inventory copy];
    if (!usable) return AssistBlocked(result, @"no-eligible-l", evidence->unknownMembers ?
        @"武裝回復 L：潛覺、有效 L 或屬性資料不完整，已停止" :
        @"目前沒有可消的有效武裝回復 L；請先解綁／封覺醒／禁消或手動處理");
    NSMutableArray *candidates = [NSMutableArray array];
    BOOL goalLimit = NO;
    for (unsigned a = existing[0]; a <= capacity[0]; ++a) for (unsigned b = existing[1]; b <= capacity[1]; ++b)
    for (unsigned c = existing[2]; c <= capacity[2]; ++c) for (unsigned d = existing[3]; d <= capacity[3]; ++d)
    for (unsigned e = existing[4]; e <= capacity[4]; ++e) {
        const unsigned counts[5] = {a,b,c,d,e};
        unsigned amount = 0, groups = 0, added = 0, providerMask = 0;
        for (unsigned color = 0; color < 5; ++color) {
            amount += evidence->perColor[color] * counts[color]; groups += counts[color];
            added += counts[color] > existing[color] ? counts[color] - existing[color] : 0;
            if (counts[color]) providerMask |= evidence->providers[color];
        }
        if (amount < evidence->turns || groups * 5 > state->board.width * state->board.height) continue;
        // Keep only minimal allocations; existing goals may already satisfy
        // one of the colors. Nonminimal allocations cannot improve feasibility.
        BOOL minimal = YES;
        for (unsigned color = 0; color < 5; ++color)
            if (counts[color] > existing[color] && amount - evidence->perColor[color] >= evidence->turns) minimal = NO;
        if (!minimal) continue;
        PHOrbGoalSet goals = base; BOOL fits = YES;
        for (unsigned color = 0; color < 5; ++color) fits &= AddAssistL(&goals, color, counts[color]);
        if (!fits) { goalLimit = YES; continue; }
        if (!PHOrbShapeInventoryAvailable(&state->board, &goals)) continue;
        [candidates addObject:@{@"goals":PHOrbGoalsJSON(&goals), @"counts":@[@(a),@(b),@(c),@(d),@(e)],
            @"predictedTurns":@(amount), @"groups":@(groups), @"addedGroups":@(added), @"providers":@(providerMask)}];
    }
    if (!candidates.count) {
        if (goalLimit) return AssistBlocked(result, @"goal-limit",
            @"武裝回復 L 與原有目標超過 6 項，未刪除原有條件；請調整策略");
        NSMutableArray *details = [NSMutableArray array];
        const BOOL singleColor = !(usable & (usable - 1));
        for (NSDictionary *entry in inventory) {
            const unsigned color = [entry[@"color"] unsignedIntValue];
            const unsigned minimum = [entry[@"guaranteed"] unsignedIntValue];
            const unsigned required = [entry[@"baseShapeOrbs"] unsignedIntValue];
            NSString *name = PHOrbColorTitle(1u << color);
            if (singleColor && (usable & (1u << color))) {
                const unsigned groups = MAX(existing[color], (evidence->turns + evidence->perColor[color] - 1u) / evidence->perColor[color]);
                const unsigned needed = [entry[@"reservedWithoutL"] unsignedIntValue] + 5u * groups;
                [details addObject:[NSString stringWithFormat:@"%@珠保證 %u 顆；原有獨立形狀與 %u 組 %@ L 合計至少需 %u 顆",
                    name, minimum, groups, name, needed]];
            } else {
                if (required > minimum) [details addObject:[NSString stringWithFormat:
                    @"%@珠保證 %u 顆，原有獨立形狀至少需 %u 顆", name, minimum, required]];
                if (usable & (1u << color)) [details addObject:[NSString stringWithFormat:
                    @"%@珠保證 %u 顆；保留原目標後按珠數最多 %u 組 L，每組回復 %u 回合",
                    name, minimum, capacity[color], evidence->perColor[color]]];
            }
        }
        return AssistBlocked(result, @"insufficient-inventory", [NSString stringWithFormat:
            @"珠數不足以同時完成原有目標及足量武裝回復 L（剩 %u 回合）；%@。請補珠或手動處理",
            evidence->turns, [details componentsJoinedByString:@"；"]]);
    }
    [candidates sortUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
        for (NSString *key in @[@"addedGroups", @"groups", @"predictedTurns"])
            if (![a[key] isEqual:b[key]]) return [a[key] compare:b[key]];
        for (unsigned color = 0; color < 5; ++color)
            if (![a[@"counts"][color] isEqual:b[@"counts"][color]]) return [a[@"counts"][color] compare:b[@"counts"][color]];
        return NSOrderedSame;
    }];
    // Search a bounded selection, sharing one original deadline. Not finding
    // a route among these is explicitly not a proof that all choices fail.
    if (candidates.count > 12) [candidates removeObjectsInRange:NSMakeRange(12, candidates.count - 12)];
    result[@"assistRecoveryCandidates"] = [candidates copy];
    result[@"goals"] = candidates[0][@"goals"];
    result[@"assistRecoveryMessage"] = [NSString stringWithFormat:
        @"武裝無效剩 %u 回合：保留原目標並安排足量 L；轉珠後重新讀取確認", evidence->turns];
    result[@"message"] = PHOrbGoalsDescription(result[@"goals"]);
    return [result copy];
}
