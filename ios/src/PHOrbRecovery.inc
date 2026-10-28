// Included by PHOrbPlanning.m. Uses only the captured, read-only snapshot.
NSDictionary *PHOrbDefaultRecovery(void) {
    return @{@"hpAtMost":@0, @"groups":@1, @"shape":@"match"};
}

BOOL PHOrbRecoveryValid(id settings) {
    if (![settings isKindOfClass:NSDictionary.class] ||
        ![[NSSet setWithArray:[settings allKeys]] isEqual:[NSSet setWithArray:@[@"hpAtMost", @"groups", @"shape"]]]) return NO;
    uint8_t hp = 0, groups = 0;
    return UnsignedNumber(settings[@"hpAtMost"], 100, &hp) &&
        UnsignedNumber(settings[@"groups"], 3, &groups) && groups >= 1 &&
        [settings[@"shape"] isKindOfClass:NSString.class] && [@[@"match", @"four"] containsObject:settings[@"shape"]];
}

NSString *PHOrbRecoveryDescription(NSDictionary *settings) {
    if (!PHOrbRecoveryValid(settings)) return @"低血心珠設定無效";
    if (![settings[@"hpAtMost"] unsignedIntegerValue]) return @"低血心珠：已關閉";
    return [NSString stringWithFormat:@"連續模式 HP ≤ %@%%：心珠%@ × %@ 組", settings[@"hpAtMost"],
        [settings[@"shape"] isEqual:@"four"] ? @"四顆直線" : @"至少三連", settings[@"groups"]];
}

static NSDictionary *RecoveryBlocked(NSMutableDictionary *plan, NSString *message) {
    plan[@"available"] = @NO; plan[@"message"] = message; plan[@"recoveryMessage"] = message;
    return [plan copy];
}

NSDictionary *PHOrbPlanWithRecovery(NSDictionary *plan, NSDictionary *settings, const PHOrbSnapshot *state, BOOL automatic) {
    NSMutableDictionary *result = [plan isKindOfClass:NSDictionary.class] ? [plan mutableCopy] : [NSMutableDictionary dictionary];
    result[@"recoveryActive"] = @NO; result[@"recoveryMessage"] = @"";
    if (![plan isKindOfClass:NSDictionary.class]) return RecoveryBlocked(result, @"原有盤面目標無法確認，已停止");
    if (!PHOrbRecoveryValid(settings)) return RecoveryBlocked(result, @"低血心珠設定無效，請重新儲存");
    PHOrbGoalSet base = {0};
    if (!PHOrbReadGoals(plan[@"goals"], &base)) return RecoveryBlocked(result, @"原有盤面目標無法確認，已停止");
    result[@"baseGoals"] = PHOrbGoalsJSON(&base);
    result[@"goals"] = result[@"baseGoals"];
    // Unsupported leader clauses remain blocking; recovery must not replace them.
    if (![plan[@"available"] boolValue]) return [result copy];
    unsigned hpLimit = [settings[@"hpAtMost"] unsignedIntValue];
    if (!hpLimit) return [result copy];
    if (!automatic) {
        result[@"recoveryMessage"] = @"低血心珠僅用於連續模式；本次手動求路未套用";
        return [result copy];
    }
    if (!state || !state->present || !state->valid || !state->automation.valid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) || state->identity.sourceMode != 0)
        return RecoveryBlocked(result, @"無法確認低血心珠所需的單人戰鬥資料，已停止");
    const PHOrbAutomationState *player = &state->automation;
    if (!player->hpKnown || !player->maximumHP || !player->hp || player->hp > player->maximumHP)
        return RecoveryBlocked(result, @"低血心珠：目前 HP 或有效上限無法確認，已停止");
    if ((uint64_t)player->hp * 100 > (uint64_t)player->maximumHP * hpLimit) {
        result[@"recoveryMessage"] = [NSString stringWithFormat:@"本回合 HP %.1f%% 高於 %@%%，未增加心珠要求",
            100.0 * player->hp / player->maximumHP, settings[@"hpAtMost"]];
        return [result copy];
    }
    result[@"recoveryActive"] = @YES;
    if (!player->boardKnown || !PHOrbCanPreview(state))
        return RecoveryBlocked(result, @"低血心珠：盤面或操作條件尚未確認，已停止");
    BOOL four = [settings[@"shape"] isEqual:@"four"];
    PHOrbGoal heart = {.kind = (uint8_t)(four ? PHOrbGoalFour : PHOrbGoalMatch), .colors = 32,
        .count = (uint8_t)[settings[@"groups"] unsignedIntValue], .length = (uint8_t)(four ? 0 : 3)};
    unsigned same = 0;
    for (; same < base.count; ++same) {
        PHOrbGoal *goal = &base.items[same];
        if (goal->kind == heart.kind && goal->colors == heart.colors && goal->length == heart.length) break;
    }
    if (same == base.count) {
        if (base.count == PHOrbMaxGoals)
            return RecoveryBlocked(result, @"低血心珠與原有目標合共超過 6 項，請調整目標後再開始");
        base.items[base.count++] = heart;
    } else {
        // The same matches may meet both requirements; retain the stronger count.
        heart.count = MAX(heart.count, base.items[same].count); base.items[same] = heart;
    }
    unsigned hearts = 0;
    for (unsigned i = 0; i < state->board.width * state->board.height; ++i) hearts += state->board.types[i] == 5;
    unsigned minimum = heart.count * (four ? 4u : 3u);
    if (hearts < minimum)
        return RecoveryBlocked(result, [NSString stringWithFormat:@"低血心珠不足：目前 %u 顆，合併後目標至少需要 %u 顆，已停止", hearts, minimum]);
    result[@"goals"] = PHOrbGoalsJSON(&base);
    result[@"message"] = PHOrbGoalsDescription(result[@"goals"]);
    result[@"recoveryMessage"] = [NSString stringWithFormat:@"本回合 HP %.1f%% ≤ %@%%：要求心珠%@ × %u 組",
        100.0 * player->hp / player->maximumHP, settings[@"hpAtMost"], four ? @"四顆直線" : @"至少三連", heart.count];
    NSString *warning = @"心珠目標只核對消除，不估算回復量，也不保證能承受下一次攻擊。";
    result[@"warning"] = [plan[@"warning"] length] ? [plan[@"warning"] stringByAppendingFormat:@"\n%@", warning] : warning;
    return [result copy];
}
