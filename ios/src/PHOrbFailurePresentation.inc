// Included in PHOrbSession.mm's anonymous namespace. Presentation only: keep
// search statuses and 8P skill/preparation recovery in their existing order.
NSString *FishingSkipReasonMessage(NSString *reason) {
    if ([reason isEqual:@"mode-not-supported"]) return @"此模式尚未支援取色";
    if ([reason isEqual:@"continuation-manual-once-required"]) return @"3 人協力・單人續玩須在目前操作窗口手動點「轉一次」";
    if ([reason isEqual:@"three-player-manual-once-required"]) return @"3 人協力取色須在自己的目前回合手動點「轉一次」";
    if ([reason isEqual:@"local-window-closed"]) return @"本機操作窗口已關閉或剩餘操作時間無法確認";
    if ([reason isEqual:@"state-unavailable"] || [reason isEqual:@"invalid-board"]) return @"盤面或本機操作狀態無法確認";
    if ([reason isEqual:@"unsupported-color-table"]) return @"轉轉色表含尚未支援的珠色";
    if ([reason isEqual:@"timing-unavailable"]) return @"轉轉速度、倒數或遊戲影格時間無法確認";
    if ([reason isEqual:@"motion-unavailable"]) return @"轉轉珠子的移動狀態無法確認";
    if ([reason isEqual:@"insufficient-time"] || [reason isEqual:@"insufficient-move-time"]) return @"剩餘原生操作時間不足以保留取色等待及放手時間";
    if ([reason isEqual:@"requires-one-unlocked-spinner"]) return @"單次取色目前需要剛好一個未鎖定的轉轉";
    if ([reason isEqual:@"thorn-token"]) return @"單次取色目前不支援有荊棘的盤面";
    if ([reason isEqual:@"assist-multi-plan-required"]) return @"目前回復目標需要連續取色規劃";
    if ([reason isEqual:@"invalid-goals"]) return @"連續取色目標未通過解析";
    return @"";
}
NSString *FishingNotAttemptedMessage(const PHOrbBoard &board, NSString *eligibility,
    NSString *singleEligibility, NSString *multiEligibility, NSDictionary *coverage) {
    if (PHOrbSpinnerVariants(&board) < 2) return @"";
    NSString *common = FishingSkipReasonMessage(eligibility);
    if (common.length) return [NSString stringWithFormat:@"本次未嘗試取色：%@。",common];
    NSMutableArray<NSString *> *reasons = [NSMutableArray array];
    NSString *single = FishingSkipReasonMessage(singleEligibility);
    if (single.length) [reasons addObject:single];
    if ([multiEligibility isEqual:@"locked-or-thorn-token"]) {
        bool locked = false, thorn = false;
        for (unsigned i = 0; i < board.width * board.height; ++i) {
            locked |= (board.flags[i] & PHOrbFlagLocked) != 0;
            thorn |= (board.flags[i] & PHOrbFlagThorn) != 0;
        }
        [reasons addObject:[NSString stringWithFormat:@"連續取色目前不支援有%@的盤面",
            locked && thorn ? @"鎖珠及荊棘" : locked ? @"鎖珠" : @"荊棘"]];
    } else {
        NSString *multi = FishingSkipReasonMessage(multiEligibility);
        if (multi.length && ![multi isEqual:single]) [reasons addObject:multi];
        if ([multiEligibility isEqual:@"eligible"]) [reasons addObject:
            [coverage[@"endReason"] isEqual:@"no-eligible-cell"] ? @"沒有未被封條擋住的取色入口" :
            @"本次共用搜尋時間內未進入連續取色搜尋"];
    }
    if (!reasons.count) return @"";
    return [NSString stringWithFormat:@"本次未嘗試取色：%@。這些限制不代表手動取色無解。",
        [reasons componentsJoinedByString:@"；"]];
}

NSString *ThornSearchRestrictionMessage(const PHOrbBoard &board, const PHOrbSolveOptions &options) {
    const auto &t = options.thorns;
    if (t.known && t.hp > 0 && t.hp <= t.maximumHP && t.maximumHP <= INT32_MAX && t.pendingDamage < t.hp) return @"";
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if ((board.flags[i] & PHOrbFlagThorn) && board.thornDamage[i]) return
            @"荊棘傷害預算未確認，普通求路只允許零荊棘接觸；起手持有荊棘珠仍可，途中不可碰入其他會造成傷害的荊棘珠。";
    return @"";
}

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

NSString *MonochromeGeometryFailureMessage(const PHOrbGoalSet &goals, unsigned minimumExchanges) {
    NSString *target = @"指定色", *donor = @"同色";
    for (unsigned g = 0; g < goals.count; ++g) {
        if (goals.items[g].kind == PHOrbGoalT) target = PHOrbColorTitle(goals.items[g].colors);
        if (goals.items[g].kind == PHOrbGoalFour) donor = PHOrbColorTitle(goals.items[g].colors);
    }
    return [NSString stringWithFormat:
        @"29 顆同色＋1 個轉轉：%@ T 與獨立%@四連要在轉轉任何終色都成立，至少需取出 %u 顆異色珠；目前最多連撈 %u 次。"
         "四連還需要額外異色珠隔開旁邊的同色消除，因此已略過無法達標的搜尋。"
         "至少 %u 顆只是必要條件，尚未確認更多取色的路徑或時間是否可行。未開始轉珠；可用技能改盤或手動處理。",
        target,donor,minimumExchanges,PHOrbMultiFishingMaxExchanges,minimumExchanges];
}

NSString *MultiFishingFailureMessage(NSString *base, const PHOrbBoard &board,
    const PHOrbGoalSet &goals, NSArray *multiAttempts) {
    if (!multiAttempts.count) return base;
    for (NSDictionary *attempt in multiAttempts)
        if ([attempt[@"exactGeometryBound"] boolValue] &&
            [attempt[@"minimumExchangesLowerBound"] unsignedIntValue] > PHOrbMultiFishingMaxExchanges)
            return MonochromeGeometryFailureMessage(goals,[attempt[@"minimumExchangesLowerBound"] unsignedIntValue]);
    NSMutableArray *missing = [NSMutableArray array];
    for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
        const unsigned required = PHOrbRequiredShapeOrbs(&goals,color);
        unsigned minimum = 0, maximum = 0;
        PHOrbColorCountRange(&board,color,&minimum,&maximum);
        if (required > maximum && !(board.spinnerColors & (1u << color)))
            [missing addObject:[NSString stringWithFormat:@"%@需 %u 顆，盤面最多 %u 顆，而且轉轉色表沒有此色",
                PHOrbColorTitle(1u << color),required,maximum]];
    }
    if (missing.count) return [NSString stringWithFormat:
        @"%@。等待更久也不會轉出缺少的顏色；沒有開始取色或轉珠。",
        [missing componentsJoinedByString:@"；"]];

    NSUInteger searched = 0, timeRejected = 0, evaluated = 0, slicesExpired = 0, budgetSkipped = 0;
    NSUInteger geometryRequested = 0, geometrySearched = 0, geometrySelected = 0, geometryTimeRejected = 0;
    NSUInteger geometryBudgetSkipped = 0;
    BOOL limited = NO, inventoryIncomplete = NO, anyValidInventory = NO, invalid = NO;
    BOOL geometryLimited = NO, geometryTruncated = NO, geometrySlotsFull = NO, retried = NO;
    NSMutableSet *entryCells = [NSMutableSet set];
    for (NSDictionary *attempt in multiAttempts) {
        if (attempt[@"fishCell"]) [entryCells addObject:attempt[@"fishCell"]];
        retried |= [attempt[@"soloRetry"] boolValue];
        searched += [attempt[@"plansSearched"] unsignedIntegerValue];
        timeRejected += [attempt[@"plansTimeRejected"] unsignedIntegerValue];
        evaluated += [attempt[@"evaluated"] unsignedIntegerValue];
        slicesExpired += [attempt[@"planSlicesExpired"] unsignedIntegerValue];
        budgetSkipped += [attempt[@"plansBudgetSkipped"] unsignedIntegerValue];
        limited |= [attempt[@"searchBudgetExhausted"] boolValue] || [attempt[@"deadline"] boolValue] ||
            [attempt[@"inventoryDeadline"] boolValue];
        inventoryIncomplete |= [attempt[@"inventoryStateLimit"] boolValue] ||
            [attempt[@"planSelectionTruncated"] boolValue] || [attempt[@"inventoryGoalLimit"] boolValue];
        anyValidInventory |= [attempt[@"inventoryValid"] boolValue];
        invalid |= [attempt[@"status"] isEqual:@"invalid"] || [attempt[@"status"] isEqual:@"failed"];
        geometryRequested = std::max(geometryRequested,[attempt[@"geometryRequestedExtra"] unsignedIntegerValue]);
        geometrySelected += [attempt[@"geometrySelectedPlans"] unsignedIntegerValue];
        geometrySearched += [attempt[@"geometryPlansSearched"] unsignedIntegerValue];
        geometryTimeRejected += [attempt[@"geometryPlansTimeRejected"] unsignedIntegerValue];
        geometryBudgetSkipped += [attempt[@"geometryPlansBudgetSkipped"] unsignedIntegerValue];
        geometryLimited |= [attempt[@"geometryDeadlineReached"] boolValue] ||
            [attempt[@"geometryStateLimitReached"] boolValue];
        geometryTruncated |= [attempt[@"geometrySelectionTruncated"] boolValue];
        geometrySlotsFull |= [attempt[@"geometrySlotLimitReached"] boolValue];
    }
    // Replace the earlier single-take explanation after multi-take attempts;
    // neither a capped inventory enumeration nor beam failure proves no route.
    NSMutableString *message = [[NSString stringWithFormat:
        @"已按完整目標嘗試連續撈珠（最多 %u 次），檢查 %lu 個轉轉入口，搜尋 %lu 個方案、評估 %lu 個候選；尚未取得同時符合形狀、取色與妨礙處理條件的可執行路徑。",
        PHOrbMultiFishingMaxExchanges,(unsigned long)(entryCells.count ?: multiAttempts.count),(unsigned long)searched,(unsigned long)evaluated] mutableCopy];
    if (retried) [message appendFormat:@"單人盤已因時間不足，以延長的 %u ms 重試全部入口一次；方案與候選數含兩輪。",PHOrbSoloRetryMilliseconds];
    if (!searched && anyValidInventory) [message appendString:@"珠量／可用色表或時間預檢尚未留下可搜尋的方案。"];
    if (timeRejected) [message appendFormat:@"有 %lu 個方案的移動、逐次等待及放手預留超出操作時間。",(unsigned long)timeRejected];
    if (limited) [message appendString:@"本次搜尋／規劃時間額度已用盡。"];
    if (slicesExpired) [message appendFormat:@"有 %lu 個方案用完分配的搜尋時間；單一方案逾時並不代表整體無解。",(unsigned long)slicesExpired];
    if (budgetSkipped) [message appendFormat:@"有 %lu 個方案因分配的時間已用盡而略過，沒有進入路徑搜尋。",(unsigned long)budgetSkipped];
    if (inventoryIncomplete) [message appendString:@"候選保留或狀態上限有截斷，並非窮盡所有可能。"];
    if (geometryRequested) {
        if (geometrySearched) [message appendFormat:
            @"也搜尋了 %lu 個以額外取色改善珠子位置的方案（比原補珠方案最多多 %lu 次，總取色仍最多 %u 次），未找到完整路徑。",
            (unsigned long)geometrySearched,(unsigned long)geometryRequested,PHOrbMultiFishingMaxExchanges];
        else if (geometrySelected) [message appendFormat:
            @"已保留 %lu 個額外取色的位置改善方案，但本次尚未進入它們的路徑搜尋。",(unsigned long)geometrySelected];
        else [message appendString:@"本次未保留可供路徑搜尋的額外取色位置改善方案。"];
        if (geometryTimeRejected) [message appendFormat:
            @"其中 %lu 個位置改善方案未通過操作時間預檢。",(unsigned long)geometryTimeRejected];
        if (geometryBudgetSkipped) [message appendFormat:
            @"其中 %lu 個位置改善方案因搜尋時間不足而略過，未計入已搜尋數。",(unsigned long)geometryBudgetSkipped];
        if (geometrySlotsFull) [message appendString:@"原補珠候選已佔用方案名額，限制了可保留的位置改善方案。"];
        if (geometryLimited) [message appendString:@"位置改善規劃達到本次時間或狀態額度。"];
        if (geometryTruncated) [message appendString:@"額外取色候選亦有保留上限。"];
    }
    if (invalid || !anyValidInventory) [message appendString:@"部分入口的資料或重播校驗未通過，請保留診斷紀錄。"];
    [message appendString:@"這不代表手動撈珠無解。未開始轉珠；可用技能改盤或手動處理。"];
    return message;
}
