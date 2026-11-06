// Included in PHOrbSession.mm's anonymous namespace. Presentation only: keep
// search statuses and 8P skill/preparation recovery in their existing order.
NSString *InventoryFailureMessage(const PHOrbBoard &board, const PHOrbSolveOptions &options) {
    const unsigned minimum = std::max(unsigned(board.minErase ? board.minErase : 3), unsigned(board.minMatch));
    const unsigned capacity = unsigned(board.width) * board.height / minimum;
    for (unsigned g = 0; g < options.goals.count; ++g) {
        const auto &goal = options.goals.items[g];
        if (goal.kind == PHOrbGoalCombo && goal.count > capacity)
            return [NSString stringWithFormat:
                @"本次目標要求%@至少 %u Combo；%u×%u 盤面每組至少 %u 顆，既有珠最多 %u Combo（不計天降／加算）。",
                options.goals.comboFirstWave || options.activity.firstWaveOnly ? @"首消" : @"盤內消除",
                goal.count, board.width, board.height, minimum, capacity];
    }
    const auto issue = PHOrbInspectInventory(board, options);
    NSString *detail;
    NSArray<NSDictionary *> *goals = PHOrbGoalsJSON(&options.goals);
    switch (issue.kind) {
        case PHOrbInventoryKind::ShapeOrbs:
            detail = [NSString stringWithFormat:@"%@珠的獨立形狀合計至少需 %u 顆，目前按此色序只有 %u 顆。",
                PHOrbColorTitle(1u << issue.color), issue.required, issue.available];
            break;
        case PHOrbInventoryKind::MatchRule:
            detail = [NSString stringWithFormat:@"%@與最少消珠規則衝突：每組至少 %u 顆，目標指定剛好 %u 顆。",
                PHOrbGoalDescription(goals[issue.goal]), issue.required, issue.available];
            break;
        case PHOrbInventoryKind::GoalCapacity: {
            const auto &goal = options.goals.items[issue.goal];
            NSString *unit = goal.kind == PHOrbGoalCombo ? @" Combo" : goal.kind == PHOrbGoalColors ? @" 種顏色" : @" 組";
            detail = [NSString stringWithFormat:@"%@：按目前珠色數量及消除限制，上限 %u%@，需要 %u%@。",
                PHOrbGoalDescription(goals[issue.goal]), issue.available, unit, issue.required, unit];
            break;
        }
        case PHOrbInventoryKind::FishingExchange:
            detail = @"只撈一顆時，每種換回轉轉的珠色都有未通過的珠量條件，無法同時滿足目前目標。";
            break;
        default:
            return @"目前目標未通過珠量／消除規則檢查，未取得可執行路徑；請查看目標與盤面限制。";
    }
    if (board.unmatchableColors & 63u) detail = [detail stringByAppendingFormat:@"目前不可消：%@。",
        PHOrbColorTitle(board.unmatchableColors & 63u)];
    if (minimum > 3) detail = [detail stringByAppendingFormat:@"目前每組至少消 %u 顆。", minimum];
    if (issue.variants > 1) detail = [detail stringByAppendingString:options.fishCell >= 0 ?
        @"此檢查包含不撈及只撈一顆，扣除換回轉轉的珠後，要求放手時全部色序達標。" :
        @"避開轉轉的搜尋要求全部色序達標；至少一種色序未通過，不代表手動取色也無解。"];
    return detail;
}

NSString *FishingInventoryMessage(const PHOrbGoalSet &goals, const PHOrbFishingPlan &plan) {
    if (!plan.valid || !plan.count || plan.selectedCount) return @"";
    unsigned comboUpper = 0;
    for (unsigned i = 0; i < plan.count; ++i) {
        if (plan.candidates[i].inventory.kind == PHOrbInventoryKind::None) return @"";
        comboUpper = std::max(comboUpper, plan.candidates[i].comboUpper);
    }
    for (unsigned g = 0; g < goals.count; ++g) if (goals.items[g].kind == PHOrbGoalCombo && goals.items[g].count > comboUpper)
        return [NSString stringWithFormat:
            @"\n已檢查轉轉可取的 %u 種顏色：只撈一顆、扣除換回的珠並要求全部色序達標時，珠數上限 %u Combo，仍低於 %u；尚未計入形狀及路徑限制。這些取色未進入路徑搜尋。",
            plan.count, comboUpper, goals.items[g].count];
    return @"\n可取的目標色已做單顆交換檢查，沒有顏色能通過全部色序的珠量條件，因此未進入撈珠路徑搜尋。";
}

NSString *SolveFailureMessage(PHOrbSolveStatus status, const PHOrbBoard &board,
                             const PHOrbSolveOptions &options, NSDictionary *plan, bool assistCandidates,
                             const PHOrbFishingPlan &fishingPlan) {
    NSString *message;
    switch (status) {
        case PHOrbSolveStatus::Invalid: return @"求路輸入或設定未通過校驗，已停止；請保留診斷紀錄。";
        case PHOrbSolveStatus::Failed: return @"求路器執行或內部校驗失敗，已停止；請保留診斷紀錄。";
        case PHOrbSolveStatus::Cancelled: return @"本次求路已取消。";
        case PHOrbSolveStatus::InsufficientOrbs: message = InventoryFailureMessage(board, options); break;
        case PHOrbSolveStatus::IncompatibleGoals: message = @"目前目標組合未通過相容性檢查，已停止。"; break;
        default: message = @"本次搜尋在步數、時間及移動限制內未取得可執行路徑，不代表盤面無解。"; break;
    }
    message = [message stringByAppendingString:FishingInventoryMessage(options.goals, fishingPlan)];
    if (assistCandidates) message = [@"本次武裝回復 L 多候選搜尋未取得完整路徑；最後候選的結果：\n" stringByAppendingString:message];
    if ([plan[@"enemyComboActive"] boolValue]) message = [message stringByAppendingFormat:@"\n%@",
        plan[@"enemyCounterMessage"] ?: @"本回合另有敵人 Combo 吸收條件。"];
    if ([plan[@"recoveryActive"] boolValue]) message = [message stringByAppendingFormat:@"\n%@", plan[@"recoveryMessage"] ?: @"本回合另有補心條件。"];
    return [message stringByAppendingString:@"\n未開始轉珠；請調整目標、使用對應技能或手動處理。"];
}
