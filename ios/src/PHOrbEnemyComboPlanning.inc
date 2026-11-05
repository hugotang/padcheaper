// Included by PHOrbPlanning.m. Adds a current enemy countermeasure after
// skills, before assist-L candidate expansion; never changes saved goals.
NSDictionary *PHOrbPlanWithEnemyCombo(NSDictionary *plan, const PHOrbSnapshot *state, BOOL automatic) {
    NSMutableDictionary *result = [plan mutableCopy];
    result[@"enemyComboActive"] = @NO; result[@"enemyComboRequired"] = @0; result[@"enemyComboFirstWave"] = @NO;
    result[@"enemyCounterMessage"] = @""; result[@"enemyCounterBlocker"] = @"";
    result[@"enemyComboBonus"] = @0; result[@"enemyComboBonusSources"] = @[@0,@0,@0,@0];
    result[@"enemyComboBeforeBonus"] = @0; result[@"enemyComboBonusFirstWave"] = @NO; result[@"enemyComboBonusKnown"] = @NO;
    if (!automatic || ![plan[@"available"] boolValue]) return [result copy];
    PHOrbGoalSet goals = {0}; unsigned minimum = 0;
    NSString *blocker = nil, *message = nil;
    result[@"enemyCounterBaseGoals"] = plan[@"goals"] ?: @[];
    if (!state || !PHOrbCanPreview(state) || !state->automation.valid || state->automation.busy ||
        !state->automation.enemiesKnown || state->multiplayer.kind != PHOrbMultiplayerNone ||
        state->identity.sourceMode != 0 || !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) ||
        !PHOrbEnemyComboRequirement(&state->enemyCombo, &minimum) || !PHOrbReadGoals(plan[@"goals"], &goals)) {
        blocker = @"state-unavailable"; message = @"敵人對策：Combo 吸收或解吸收狀態未確認，已停止";
        if (state && state->enemyCombo.valid && state->enemyCombo.enemyCount <= 8)
            for (unsigned i = 0; i < state->enemyCombo.enemyCount; ++i)
                if ((state->enemyCombo.aliveMask & (1u << i)) && (int16_t)state->enemyCombo.upperTurns[i] > 0) {
                    blocker = @"upper-combo-unsupported";
                    message = @"敵人有「Combo 以上吸收」；目前無法核對加算與天降後的 Combo 上限，已停止";
                    break;
                }
    } else if (!minimum) {
        result[@"enemyCounterMessage"] = @"目前沒有需要突破的 Combo 吸收，保留原目標";
        return [result copy];
    } else {
        const unsigned beforeBonus = minimum;
        unsigned sources[3] = {0};
        const unsigned leaderBadge = PHOrbGuaranteedComboBonuses(&state->enemyCombo.bonus,&goals,
            state->board.width,state->board.height,sources);
        const unsigned active = (int16_t)(uint16_t)(state->enemyCombo.adjustmentTurns << 6) > 0 &&
            state->enemyCombo.adjustment > 0 ? (unsigned)state->enemyCombo.adjustment : 0;
        const unsigned bonus = leaderBadge + active;
        minimum = minimum > bonus ? minimum - bonus : 1;
        result[@"enemyComboBeforeBonus"] = @(beforeBonus);
        result[@"enemyComboBonus"] = @(bonus);
        result[@"enemyComboBonusSources"] = @[@(sources[0]),@(sources[1]),@(sources[2]),@(active)];
        result[@"enemyComboBonusFirstWave"] = @(leaderBadge > 0);
        result[@"enemyComboBonusKnown"] = @(state->enemyCombo.bonus.valid);
        result[@"enemyComboActive"] = @YES; result[@"enemyComboRequired"] = @(minimum); result[@"enemyComboFirstWave"] = @YES;
        if (minimum > 14) {
            blocker = @"combo-limit";
            message = [NSString stringWithFormat:@"敵人 Combo 吸收對策已扣除可確認的 %u 加 Combo，仍要求首消至少 %u Combo，超出目前支援的 14 Combo；請先使用對應技能或手動處理", bonus, minimum];
        } else {
            unsigned index = goals.count;
            for (unsigned i = 0; i < goals.count; ++i) if (goals.items[i].kind == PHOrbGoalCombo) { index = i; break; }
            if (index == PHOrbMaxGoals) {
                blocker = @"goal-limit"; message = @"敵人 Combo 對策與原目標超過 6 項，未撤下原條件；請調整策略或先解吸收";
            } else {
                if (index == goals.count) goals.items[goals.count++] = (PHOrbGoal){PHOrbGoalCombo, 0, (uint8_t)minimum, 0};
                else goals.items[index].count = (uint8_t)MAX(minimum, goals.items[index].count);
                result[@"goals"] = PHOrbGoalsJSON(&goals);
                result[@"message"] = PHOrbGoalsDescription(result[@"goals"]);
                result[@"enemyCounterMessage"] = [NSString stringWithFormat:
                    @"敵人 Combo 吸收對策：首消至少 %u Combo；已預留減 Combo，計入隊長 +%u／助人 +%u／徽章 +%u／技能 +%u。加算條件須首消達成；未計天降或未確認效果。",
                    minimum,sources[0],sources[1],sources[2],active];
            }
        }
    }
    if (blocker) {
        result[@"available"] = @NO; result[@"message"] = message;
        result[@"enemyCounterMessage"] = message; result[@"enemyCounterBlocker"] = blocker;
    }
    return [result copy];
}
