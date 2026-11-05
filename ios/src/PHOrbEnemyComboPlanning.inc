// Included by PHOrbPlanning.m. Adds a current enemy countermeasure after
// skills, before assist-L candidate expansion; never changes saved goals.
NSDictionary *PHOrbPlanWithEnemyCombo(NSDictionary *plan, const PHOrbSnapshot *state, BOOL automatic) {
    NSMutableDictionary *result = [plan mutableCopy];
    result[@"enemyComboActive"] = @NO; result[@"enemyComboRequired"] = @0; result[@"enemyComboFirstWave"] = @NO;
    result[@"enemyCounterMessage"] = @""; result[@"enemyCounterBlocker"] = @"";
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
        result[@"enemyComboActive"] = @YES; result[@"enemyComboRequired"] = @(minimum); result[@"enemyComboFirstWave"] = @YES;
        if (minimum > 14) {
            blocker = @"combo-limit";
            message = [NSString stringWithFormat:@"敵人 Combo 吸收對策要求首消至少 %u Combo，超出目前支援的 14 Combo；未計隊長／技能加算，請先使用對應技能或手動處理", minimum];
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
                    @"敵人 Combo 吸收對策：首消至少 %u Combo（已預留減 Combo）；未計隊長加算或後續落珠", minimum];
            }
        }
    }
    if (blocker) {
        result[@"available"] = @NO; result[@"message"] = message;
        result[@"enemyCounterMessage"] = message; result[@"enemyCounterBlocker"] = blocker;
    }
    return [result copy];
}
