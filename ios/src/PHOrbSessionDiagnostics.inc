// Session-owned diagnostic histories and public snapshot assembly.
// Snapshot retains bounded readiness, failed-search and mechanism-success
// evidence. These in-memory records add no native reads or actions.
- (void)recordReadIssue:(const PHOrbSnapshot *)state reason:(NSString *)reason multiplayer:(BOOL)multiplayer {
    _lastReadIssue = ReadIssueTrace(*state, reason, _openingStep + (_openingInFlight ? 1 : 0),
        _auto.active(), _phase, _searchTrace[@"generation"] ?: @0, multiplayer);
}
- (NSDictionary *)eightProgressTrace {
    const auto &r = _eightLastObserved.eightReceipt;
    return @{@"sameMatch":@(PHOrbSameEightMatch(&_eightHistory,&_live)),@"resumes":@(_eightResumes),
        @"skillPending":@(_eightSkillPending),@"skillStarted":@(_eightSkillStarted),
        @"skillAcknowledged":@(_eightSkillAcknowledged),@"skillSettled":@(_eightSkillSettled),
        @"pendingSlot":@(_eightSkillPending ? _eightSlot + 1 : 0),
        @"beforeTotalUses":@(_eightSkillBefore.automation.skillUses),
        @"confirmedTotalUses":@(_eightSkillAfter.automation.skillUses),
        @"latestTotalUses":@(_eightLastObserved.automation.skillUses),
        @"receipt":@{@"valid":@(r.valid),@"skill":@(r.skill),@"catalogID":@(r.catalogID),
            @"slot":@(r.valid ? r.slot + 1 : 0),@"source":@(r.source)},
        @"floor":@(_live.wave + 1),@"sequence":_eightPlan.sequence ?: @[],
        @"sequenceStep":@(_eightPlan.step),@"sequenceCount":@(_eightPlan.sequence.count),@"recoveryCursor":@(_eightPlan.cursor),
        @"attemptedSlots":@(_eightTurn.attemptedSlots),@"boardSubmitted":@(_eightBoardSubmitted),
        @"guideEnabled":@(PHOrbEightGuideEnabled(_activeProfile[@"activity"])),@"guidePrepared":@(_eightPlan.guidePrepared),
        @"guideSequence":@(_eightPlan.guide),@"floorSkillMask":@(_live.wave < _eightFloorSkillMask.size() ? _eightFloorSkillMask[_live.wave] : 0),
        @"firstPuzzle":_eightFirstPuzzle ?: @{},
        @"firstBlocked":_eightFirstBlocked ?: @{},@"lastBlocked":_eightLastBlocked ?: @{},@"preparation":_eightPrep.trace ?: @{},
        @"preparationUsed":@(_eightPrep.used),@"preparationAwaiting":@(_eightPrep.awaiting),
        @"preparationDeclined":@(_eightTurn.preparationDeclined),
        @"lastStop":_eightLastStop ?: @{}};
}

- (void)rememberFailedSearch {
    if (![@[@"planning-blocked", @"assist-unavailable", @"enemy-counter-unavailable", @"window-closed",
        @"insufficient-orbs", @"incompatible-goals", @"goals-unmet", @"unsafe", @"no-route", @"invalid", @"failed",
        @"replay-failed"] containsObject:_searchTrace[@"status"]]) return;
    // Diagnostic evidence only. Later successful searches and config reloads
    // must not erase the most recent failed attempt in this session.
    _lastFailedSearch = _searchTrace;
}
- (void)recordPlanningStatus:(NSString *)status reason:(NSString *)reason {
    // The capture is created before mechanism reads or plan validation. A
    // blocked attempt must not borrow a previous route, evaluation or board.
    NSMutableDictionary *trace = [_searchTrace mutableCopy];
    PHOrbGoalSet goals{};
    const BOOL goalsKnown = PHOrbReadGoals(_goalPlan[@"goals"], &goals);
    trace[@"status"] = status; trace[@"solverStarted"] = @([status isEqual:@"searching"]);
    trace[@"reason"] = reason ?: @""; trace[@"planAvailable"] = _goalPlan[@"available"] ?: @NO;
    trace[@"goalsKnown"] = @(goalsKnown); trace[@"goals"] = PHOrbGoalsJSON(&goals);
    trace[@"goalCount"] = @(goals.count);
    const bool fishingOnce = _oneShot && _oneShotBound && PHOrbSameTurn(&_oneShotOrigin,&_preview);
    trace[@"fishingEligibilityReason"] = FishingEligibilityReason(_preview,fishingOnce,_auto.active());
    trace[@"multiFishingEligibilityReason"] = MultiFishingEligibilityReason(_preview,fishingOnce,_auto.active());
    trace[@"fishingNotAttemptedReason"] = [status isEqual:@"searching"] ? @"search-pending" : @"planning-blocked";
    trace[@"soloRetrySkipped"] = PHBattleIsSinglePlayer(_preview.identity.entryMode,_preview.identity.multiplayer) ?
        @"search-not-completed" : @"not-single-player";
    PHOrbSolveOptions colorOptions = _solveOptions; colorOptions.goals = goals;
    trace[@"colorRecoveryEligibilityReason"] = @(PHOrbColorRecoveryEligibilityReason(_preview.board,colorOptions));
    trace[@"colorRecoveryConfiguredPasses"] = @(colorOptions.recoveryPasses);
    trace[@"baseGoals"] = _goalPlan[@"enemyCounterBaseGoals"] ?: _goalPlan[@"assistRecoveryBaseGoals"] ?: trace[@"goals"];
    trace[@"enemyCountermeasure"] = @{@"comboActive":_goalPlan[@"enemyComboActive"] ?: @NO,
        @"minimumCombo":_goalPlan[@"enemyComboRequired"] ?: @0,
        @"bonusCombo":_goalPlan[@"enemyComboBonus"] ?: @0,
        @"bonusSources":_goalPlan[@"enemyComboBonusSources"] ?: @[],
        @"bonusStateKnown":_goalPlan[@"enemyComboBonusKnown"] ?: @NO,
        @"beforeBonus":_goalPlan[@"enemyComboBeforeBonus"] ?: @0,
        @"bonusTriggersFirstRemovalOnly":_goalPlan[@"enemyComboBonusFirstWave"] ?: @NO,
        @"firstRemovalOnly":_goalPlan[@"enemyComboFirstWave"] ?: @NO,
        @"message":_goalPlan[@"enemyCounterMessage"] ?: @"", @"blocker":_goalPlan[@"enemyCounterBlocker"] ?: @"",
        @"stateKnown":@(_preview.enemyCombo.valid), @"survivalKnown":@NO};
    trace[@"stepLimit"] = @(_solveOptions.steps); trace[@"budgetMS"] = @(_solveOptions.milliseconds);
    if (_auto.eight()) trace[@"eightPolicy"] = @{@"guideSkills":@(PHOrbEightGuideEnabled(_activeProfile[@"activity"])),
        @"comboPriority":@(_solveOptions.activity.comboPriority),@"firstRemovalOnly":@YES,
        @"leaderAddedCombos":_goalPlan[@"leaderAddedCombos"] ?: @0,@"damageKnown":@NO,
        @"linkRewardLimit":@(_solveOptions.activity.linkRewardLimit),@"preparation":@(_eightPrep.active),
        @"attackRanking":_activeProfile[@"activity"][@"floors"] && _preview.wave < [_activeProfile[@"activity"][@"floors"] count] ?
            _activeProfile[@"activity"][@"floors"][_preview.wave][@"attack"] ?: @{} : @{},
        @"preparationPlan":_eightPrep.active ? _eightPrep.plan ?: @{} : @{}};
    trace[@"assistRecovery"] = _assistTrace;
    trace[@"assistRecoveryL"] = _goalPlan[@"assistRecoveryActive"] ?: @NO;
    trace[@"assistRecoveryInventory"] = _goalPlan[@"assistRecoveryInventory"] ?: @[];
    trace[@"assistRecoveryBlocker"] = _goalPlan[@"assistRecoveryBlocker"] ?: @"";
    _searchTrace = [trace copy];
    [self rememberFailedSearch];
}
- (void)recordExecution:(NSString *)status state:(const PHOrbSnapshot *)state evaluation:(NSDictionary *)evaluation {
    if (!_recordingExecution || !_executionHistory.count) return;
    NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
    record[@"status"] = status; record[@"confirmedSteps"] = @(_confirmed);
    if (state) {
        record[@"observedPhase"] = @(state->phase);
        record[@"observedMultiplayer"] = MultiplayerTrace(*state);
        const bool sameTurn = PHOrbSameTurn(&_preview, state);
        const bool boardRead = HasBoardObservation(*state) && sameTurn;
        record[@"observedRead"] = !state->valid || !state->present ? @"unreadable" :
            !sameTurn ? @"different-turn" : boardRead ? @"board" : @"metadata-only";
        record[@"observedBoardPreserved"] = @(!boardRead);
        if (boardRead) {
            record[@"observedBoard"] = BoardTrace(state->board);
            record[@"observedBoardMoves"] = @(state->moves);
        }
    }
    if (evaluation) record[@"releaseEvaluation"] = evaluation;
    record[@"elapsedMS"] = @(unsigned(_executionElapsed * 1000));
    _executionHistory[_executionHistory.count - 1] = [record copy];
}
- (void)beginExecutionRecord:(const PHOrbSnapshot *)state {
    PHOrbBoard predicted{};
    if (!PHOrbReplay(&_preview.board, &_route, _route.count - 1, &predicted)) return;
    NSDictionary *record = @{@"schema":@1, @"generation":@(_generation),
        @"turn":@(_auto.active() ? _auto.turn : 0), @"wave":@(_preview.wave),
        @"dungeon":@(_preview.identity.dungeon), @"floor":@(_preview.identity.floor),
        @"status":@"drag-accepted", @"goals":PHOrbGoalsJSON(&_solveOptions.goals),
        @"goalMode":_goalMode, @"leadersValid":@(_preview.leadersValid),
        @"leaderNumbers":@[@(_preview.leaderNumbers[0]), @(_preview.leaderNumbers[1])],
        @"leaderContext":LeaderTrace(_preview),
        @"scope":_auto.active() ? @"continuous" : _oneShot ? @"once" : @"preview-execute",
        @"multiplayer":MultiplayerTrace(_preview),
        @"startBoard":BoardTrace(_preview.board), @"path":RouteCells(_route),
        @"fishing":@(_route.fishing), @"fishingExchanges":FishingExchangesTrace(_route),
        @"geometryExtraExchanges":_searchTrace[@"geometryExtraExchanges"] ?: @0,
        @"evaluationScope":_route.fishingExchangeCount ? @"target-independent-other-shared-phases" : @"all-shared-spinner-phases",
        @"timing":@{@"fps":@(_preview.fps), @"remainingFrames":@(_preview.remainingFrames),
            @"spinnerPeriodFrames":@(_preview.spinnerPeriodFrames), @"spinnerCountdownKnown":@(_preview.spinnerCountdownKnown),
            @"spinnerCountdown":@(_preview.spinnerCountdown),
            @"executionLimitMS":@(unsigned(RouteExecutionLimit(_preview,_route) * 1000))},
        @"search":@{@"quality":_quality, @"elapsedMS":@(_route.elapsedMS), @"budgetMS":@(_solveOptions.milliseconds),
            @"thornComboPriority":@(_solveOptions.thornComboPriority),
            @"allowPoisonWithHeartFour":@(_solveOptions.allowPoisonWithHeartFour),
            @"diagonal":@(_route.diagonal),
            @"evaluated":@(_route.evaluated), @"deadline":@(_route.deadlineReached), @"stepLimit":@(_solveOptions.steps),
            @"evaluationCalls":@(_route.evaluationCalls), @"evaluationCacheHits":@(_route.evaluationCacheHits),
            @"evaluationBreakdownKnown":_searchTrace[@"evaluationBreakdownKnown"] ?: @NO,
            @"searchPasses":@(_route.searchPasses), @"backtracks":@(_route.backtracks), @"exploredDepth":@(_route.exploredDepth),
            @"thornContacts":@(_route.thornContacts), @"thornDamage":@(_route.thornDamage)},
        @"predictedBoard":BoardTrace(predicted), @"observedBoard":BoardTrace(state->board),
        @"observedRead":@"board", @"observedBoardPreserved":@NO, @"observedBoardMoves":@(state->moves),
        @"observedPhase":@(state->phase), @"observedMultiplayer":MultiplayerTrace(*state),
        @"confirmedSteps":@0, @"releaseEvaluation":@{}, @"releaseConfirmed":@NO};
    NSMutableDictionary *withMechanic = [record mutableCopy];
    withMechanic[@"assistRecovery"] = _assistTrace; record = [withMechanic copy];
    if (_executionHistory.count >= 3) [_executionHistory removeObjectAtIndex:0];
    [_executionHistory addObject:record]; _recordingExecution = true;
}
- (NSDictionary *)snapshot {
    NSMutableArray *path = [NSMutableArray array], *goalHits = [NSMutableArray array];
    NSMutableArray *ruleUses = [NSMutableArray array], *groupUses = [NSMutableArray array];
    for (NSUInteger turn : _ruleLastUseTurns) [ruleUses addObject:@(turn)];
    for (NSUInteger turn : _groupLastUseTurns) [groupUses addObject:@(turn)];
    NSMutableArray *decisionDetails = [NSMutableArray array];
    // A retained native refusal must keep its own observation, even when a
    // later scan sees another spinner phase or charge. Unattempted decisions
    // use the current scan; successful attempts keep their pre-cast context.
    for (NSUInteger i = 0; i < _ruleDecisions.count && i < _decisionRules.count; ++i) {
        NSUInteger slot = [_decisionRules[i][@"slot"] unsignedIntegerValue] - 1;
        if (slot >= 6) continue;
        NSMutableDictionary *detail = [(i < _decisionContexts.count ? _decisionContexts[i] :
            SkillDecisionContext(_decisionState, slot)) mutableCopy];
        [detail addEntriesFromDictionary:@{@"rule":@(i + 1), @"slot":@(slot + 1),
            @"decision":_ruleDecisions[i]}];
        [decisionDetails addObject:detail];
    }
    for (unsigned i = 0; i < _route.count; ++i) [path addObject:@(_route.cells[i])];
    for (unsigned i = 0; i < _route.goalCount && i < PHOrbMaxGoals; ++i) [goalHits addObject:@(_route.goalHits[i])];
    BOOL preview = (_phase == PHOrbSessionPhase::Preview);
    const bool liveReadable = _hasLive && _live.valid && _live.present && !_live.multiplayerDisabled;
    const bool multiplayer = _live.multiplayer.kind != PHOrbMultiplayerNone || _live.multiplayerDisabled || (_live.present &&
        !PHBattleIsSinglePlayer(_live.identity.entryMode, _live.identity.multiplayer));
    NSDictionary *liveLeaderPlan = multiplayer && [_goalMode isEqual:@"leaders"] ?
        PHOrbPlanForLeaders(&_live) : @{};
    const bool leaderReady = ![_goalMode isEqual:@"leaders"] || [liveLeaderPlan[@"available"] boolValue];
    const bool previewReady = PHOrbCanPreview(&_live);
    const bool timeReady = TimeAvailable(_live) > ArmDelay + TimeMargin + StepDuration(_live);
    const bool multiplayerOnceReady = liveReadable && previewReady && leaderReady && timeReady;
    const bool busy = _auto.active() || _oneShot || [self executing];
    const bool battleEnded = PHOrbBattleEnded(&_live);
    const bool canOnce = _frame.ready && _frame.enabled && !battleEnded && (!multiplayer || multiplayerOnceReady) &&
        !busy && !_inputFault && !_openingInFlight;
    const auto &multi = _live.multiplayer;
    NSMutableArray *onceBlockers = [NSMutableArray array];
    if (!_frame.ready) [onceBlockers addObject:@"frame-not-ready"];
    if (!_frame.enabled) [onceBlockers addObject:@"feature-disabled"];
    if (battleEnded) [onceBlockers addObject:@"battle-ended"];
    if (busy) [onceBlockers addObject:@"busy"];
    if (_inputFault) [onceBlockers addObject:@"input-fault"];
    if (_openingInFlight) [onceBlockers addObject:@"opening-in-flight"];
    if (multiplayer) {
        if (!liveReadable) [onceBlockers addObject:@"live-unreadable"];
        if (!previewReady) [onceBlockers addObject:@"board-not-ready"];
        if (!leaderReady) [onceBlockers addObject:@"leaders-unavailable"];
        if (!timeReady) [onceBlockers addObject:@"time-unavailable"];
    }
    const double uptime = NSProcessInfo.processInfo.systemUptime;
    const double observationAge = _observationUptime > 0 ? std::max(0.0, uptime - _observationUptime) * 1000 : -1;
    const bool observationCurrent = liveReadable && _frame.ready && _frame.enabled && !_inputFault && ![self executing] &&
        !_oneShot && observationAge >= 0 && observationAge <= 1500 &&
        _observationState.observation.status == PHOrbObservationReady && ObservationContextMatches(_observationState, _live);
    NSDictionary *observation = BattleObservationTrace(_observationState, observationCurrent, observationAge, _observationDurationMS,
        _enemyActionObservation);
    NSDictionary *onceReadiness = @{@"canOnce":@(canOnce), @"blockers":onceBlockers,
        @"frameReady":@(_frame.ready), @"frameEnabled":@(_frame.enabled), @"monitoring":@(_monitoring),
        @"readinessMonitoring":@(_readinessMonitoring),
        @"readSucceeded":@(_hasLive), @"liveReadable":@(liveReadable), @"present":@(_live.present),
        @"boardRead":@(HasBoardObservation(_live)), @"previewReady":@(previewReady),
        @"settled":@(_live.settled), @"ordinary":@(_live.ordinary), @"boardTransition":@(_live.boardTransition),
        @"dragActive":@(_live.dragged != 0), @"inputQuiet":@(PHInputIsQuiet(&_live.input)),
        @"remainingFrames":@(_live.remainingFrames), @"fps":@(_live.fps),
        @"leaderReady":@(!multiplayer || leaderReady), @"timeReady":@(timeReady),
        @"leaderRead":LeaderReadTrace(_live),
        @"busy":@(busy), @"inputFault":@(_inputFault), @"openingInFlight":@(_openingInFlight),
        @"sessionPhase":SessionPhaseText(_phase), @"nativePhase":@(_live.phase), @"generation":@(_generation),
        @"completed":@(_completed), @"readReason":_liveReadReason,
        @"uptimeMS":@(uint64_t(uptime * 1000)),
        @"readAgeMS":@(_lastReadUptime > 0 ? std::max(0.0, uptime - _lastReadUptime) * 1000 : -1),
        @"multiplayer":MultiplayerTrace(_live)};
    // Retain changes seen by the overlay even if diagnostics are copied after
    // leaving the match. Clock ticks and route swaps do not fill the history.
    NSDictionary *readinessKey = @{@"blockers":onceBlockers, @"sessionPhase":SessionPhaseText(_phase),
        @"nativePhase":@(_live.phase), @"generation":@(_generation), @"kind":@(multi.kind),
        @"turn":@(multi.turnSerial), @"seat":@(multi.localSeat), @"view":@(multi.viewTeam),
        @"dungeon":@(_live.identity.dungeon), @"floor":@(_live.identity.floor), @"wave":@(_live.wave),
        @"readReason":_liveReadReason, @"clockFlags":@(multi.clockFlags),
        @"leaderReadStatus":@(PHOrbLeaderReadStatusName(_live.leaderRead.status)),
        @"leaderReadPass":@(_live.leaderRead.pass), @"leaderReadSlot":@(_live.leaderRead.visibleSlot),
        @"networkFlags":@(multi.networkFlags), @"syncState":@(multi.syncState),
        @"syncStep":@(multi.syncStep), @"disconnectKnown":@(multi.disconnectKnown),
        @"continuationOwnerKnown":@(multi.continuationOwnerKnown), @"continuationDialog":@(multi.continuationDialog),
        @"continuationOwnerFlags":@(multi.continuationOwnerFlags),
        @"recoverySource":@(multi.recoverySource), @"recoveryFlags":@(multi.recoveryFlags),
        @"disconnectSeat":@(multi.disconnectSeat), @"disconnectStatus":@(multi.disconnectStatus),
        @"connectionState":@(multi.connectionState), @"localStatus":@(multi.localStatus),
        @"countdownReady":@(multi.countdownReady), @"finished":@(multi.finished),
        @"resolvePending":@(multi.resolvePending)};
    if (multiplayer && ![_onceReadinessKey isEqual:readinessKey]) {
        if (_onceReadinessHistory.count >= 16) [_onceReadinessHistory removeObjectAtIndex:0];
        [_onceReadinessHistory addObject:onceReadiness];
    }
    _onceReadinessKey = multiplayer ? readinessKey : nil;
    NSString *multiplayerMessage = @"";
    if (multiplayer) {
        if (_inputFault) multiplayerMessage = @"輸入狀態異常，請檢查診斷";
        else if (_auto.eight()) multiplayerMessage = _message;
        else if (busy) multiplayerMessage = (_phase == PHOrbSessionPhase::Releasing) ?
            @"正在放開珠子，等待遊戲確認" : @"本次轉珠執行中，可按停止";
        else if (!_frame.ready) multiplayerMessage = @"遊戲校驗或前景尚未就緒";
        else if (!_frame.enabled) multiplayerMessage = @"轉珠功能未啟用，請檢查設定";
        else if (!liveReadable || !multi.known) multiplayerMessage = @"多人資料未確認，暫不能轉一次";
        else if (_openingInFlight) multiplayerMessage = @"等待已啟動的技能結束";
        else if (!PHOrbCanPreview(&_live)) multiplayerMessage = MultiplayerWaitMessage(_live);
        else if (!leaderReady) multiplayerMessage = liveLeaderPlan[@"message"] ?: @"本機隊長／助人資料尚未確認";
        else multiplayerMessage = canOnce ? (multi.kind == PHOrbMultiplayerEight ?
            @"可轉一次，或按 Auto 連續處理本場" : @"可轉一次；完成後停止") : MultiplayerWaitMessage(_live);
        if (liveReadable && multi.known && multi.teamCount >= 2 && multi.teamCount <= 8 && multi.localSeat < multi.teamCount) {
            NSString *clock = multi.clockKnown && std::isfinite(multi.remainingSeconds) &&
                multi.remainingSeconds >= 0 && multi.remainingSeconds <= 86400 ?
                [NSString stringWithFormat:@"%.1f 秒", multi.remainingSeconds] : @"時間未確認";
            if (multi.kind == PHOrbMultiplayerThreeSolo)
                multiplayerMessage = [multiplayerMessage stringByAppendingFormat:@"\n%@ · 本機 %u／%u · 轉珠 %.1f 秒",
                    MultiplayerModeName(multi.kind), multi.localSeat + 1, multi.teamCount, TimeAvailable(_live)];
            else multiplayerMessage = [multiplayerMessage stringByAppendingFormat:@"\n%@ · 本機 %u／%u · 回合 %u · 剩餘 %@",
                MultiplayerModeName(multi.kind), multi.localSeat + 1, multi.teamCount, multi.turnSerial, clock];
        }
    }
    NSString *traceRelation = !_searchTrace.count ? @"none" : !liveReadable ? @"current-unreadable" :
        !PHBattleSameIdentity(&_traceOrigin.identity, &_live.identity) ? @"captured-in-earlier-battle" :
        !PHOrbSameTurn(&_traceOrigin, &_live) ? @"captured-in-different-turn-context" :
        ([_searchTrace[@"observedTurn"] unsignedIntegerValue] && _observedTurn &&
            [_searchTrace[@"observedTurn"] unsignedIntegerValue] != _observedTurn) ? @"captured-in-earlier-turn" :
        !HasBoardObservation(_live) ? @"current-board-unreadable" :
        SamePosition(_traceOrigin, _live) ? @"captured-at-current-position" : @"captured-at-earlier-position";
    const bool decisionCurrent = liveReadable && _decisionTurn &&
        PHBattleSameIdentity(&_decisionState.identity, &_live.identity) && PHOrbSameLeaders(&_decisionState, &_live);
    BOOL canExecute = !multiplayer && preview && _route.goalsMet && _route.hazardsSafe && _frame.enabled && !_inputFault && _hasLive && PHOrbCanPreview(&_live) &&
        SamePosition(_live, _preview) && HasRouteTime(_live, _route) && [self hasThornBudget:&_live];
    return @{@"phase":SessionPhaseText(_phase), @"message":_message, @"generation":@(_generation),
        @"canRead":@(_frame.ready && !_auto.active() && !_oneShot && ![self executing] && !_inputFault && !_openingInFlight), @"canExecute":@(canExecute && !_auto.active() && !_oneShot),
        @"canStart":@(_frame.ready && _frame.enabled && !battleEnded && (!multiplayer ||
            (PHOrbEightMatchReady(&_live) && PHOrbCanPreview(&_live) && !_live.moves && timeReady)) &&
            !_auto.active() && !_oneShot && ![self executing] && !_inputFault),
        @"canOnce":@(canOnce),
        @"onceReadiness":onceReadiness, @"onceReadinessHistory":[_onceReadinessHistory copy],
        @"multiplayer":MultiplayerTrace(_live), @"multiplayerActive":@(multiplayer), @"multiplayerMessage":multiplayerMessage,
        @"liveLeaderPlan":liveLeaderPlan,
        @"once":@(_oneShot),
        @"busy":@(busy), @"canStop":@(busy || _readQueued || preview || (_phase == PHOrbSessionPhase::Solving)),
        @"automatic":@(_auto.active()), @"autoTurn":@(_auto.turn), @"autoMoves":@(_auto.moves), @"autoSkills":@(_auto.skills),
        @"eightAutoAvailable":@(PHOrbEightMatchReady(&_live)),
        @"battleEnded":@(battleEnded),
        @"eightAutomatic":@(_auto.eight()), @"eightSkillPending":@(_eightSkillPending),
        @"watchPlayerState":@(_watchPlayerState),
        @"eightTeam":EightTeamTrace(_live.eight.valid ? _live : _eightLastObserved),
        @"eightProgress":[self eightProgressTrace],
        @"rules":_skillRules, @"automationLog":[_automationLog copy], @"enemyEffects":@(_live.automation.enemyEffects),
        @"ruleDecisions":[_ruleDecisions copy], @"decisionTurn":@(_decisionTurn),
        @"decisionObservedTurn":@(_decisionObservedTurn),
        @"ruleTiming":@{@"observedTurn":@(_observedTurn), @"profileID":_historyTimingProfile[@"id"] ?: @"",
            @"ruleLastUseTurns":ruleUses, @"groupLastUseTurns":groupUses},
        @"ruleDecisionDetails":decisionDetails, @"decisionPhase":@(_decisionState.phase),
        @"executionElapsedMS":@(unsigned(_executionElapsed * 1000)),
        @"executionLimitMS":@(unsigned(RouteExecutionLimit(_preview,_route) * 1000)),
        @"decisionSkillWindow":@(_decisionState.automation.window), @"decisionSkillBusy":@(_decisionState.automation.busy),
        @"decisionSkillWindowReason":PHOrbSkillWindowDescription(&_decisionState.automation),
        @"decisionContextCurrent":@(decisionCurrent),
        @"decisionLeaderNumbers":@[@(_decisionState.leaderNumbers[0]), @(_decisionState.leaderNumbers[1])],
        @"decisionDungeon":@(_decisionState.identity.dungeon), @"decisionFloor":@(_decisionState.identity.floor),
        @"decisionWave":@(_decisionState.wave),
        @"playerStatus":PHOrbSurvivalDescription(&_live), @"confirmedSkillRules":@(_confirmedRules),
        @"battleObservation":observation,
        @"dungeonReport":[self dungeonReportSummary],
        @"battleObservationDescription":multiplayer ? @"" : BattleObservationDescription(_observationState.observation, observationCurrent,
            _enemyActionObservation),
        @"obstacles":PHOrbObstacleDescription(&_live), @"usedSkillGroups":@(_usedGroups),
        @"activeProfile":_activeProfile,
        @"opening":@{@"profile":_openingProfile[@"id"] ?: @"", @"completed":@(_openingStep),
            @"count":@([_openingProfile[@"opening"] count]), @"inFlight":@(_openingInFlight),
            @"puzzlePending":@(_openingPuzzlePending), @"steps":[_openingLog copy]},
        @"searchTrace":_searchTrace, @"lastFailedSearch":_lastFailedSearch, @"executionHistory":[_executionHistory copy],
        @"lastFishingSuccess":_lastFishingSuccess ?: @{},
        @"lastAssistRecoverySuccess":_lastAssistRecoverySuccess ?: @{},
        @"liveReadValid":@(liveReadable), @"searchTraceRelation":traceRelation,
        @"boardTransition":@(_live.boardTransition), @"boardTransitionFrames":@(_boardTransitionFrames),
        @"resizeProgress":@(_live.resizeProgress), @"resizeTarget":@(_live.resizeTarget), @"lastReadIssue":_lastReadIssue,
        @"profileContext":@{@"known":@(_hasLive && _live.valid && _live.present && !_live.multiplayerDisabled),
            @"dungeon":@(_live.identity.dungeon), @"floor":@(_live.identity.floor)},
        @"playerBypass":@(_live.automation.playerBypass), @"skillWindow":@(_live.automation.window),
        @"enabled":@(_frame.ready && _frame.enabled), @"board":BoardJSON(_route.count ? _preview.board : _live.board), @"path":path,
        @"quality":_quality, @"searchSteps":@(_solveOptions.steps), @"searchBeam":@(_solveOptions.beam),
        @"thornComboPriority":@(_thornComboPriority),
        @"allowPoisonWithHeartFour":@(_allowPoisonWithHeartFour),
        @"diagonal":@(_diagonal), @"routeDiagonal":@(_route.diagonal),
        @"assistRecoveryL":@(_assistRecoveryL), @"assistRecoveryTrace":_assistTrace,
        @"assistRecoveryStatus":_goalPlan[@"assistRecoveryMessage"] ?: @"",
        @"enemyCounterStatus":_goalPlan[@"enemyCounterMessage"] ?: @"",
        @"recovery":_recovery, @"recoveryActive":_goalPlan[@"recoveryActive"] ?: @NO,
        @"recoveryStatus":_goalPlan[@"recoveryMessage"] ?: @"", @"recoveryTurn":@(_recoveryTurn),
        @"goalMode":_goalMode, @"goals":_manualGoals, @"goalPlan":_goalPlan,
        @"plannedGoalCount":_searchTrace[@"goalCount"] ?: @0, @"goalEvaluationAvailable":@(_route.count > 1),
        @"goalCount":@(_route.goalCount), @"matchedGoals":@(_route.matchedGoals), @"goalsMet":@(_route.goalsMet),
        @"hazardsSafe":@(_route.hazardsSafe), @"poisonErased":@(_route.poisonErased), @"unmatchedBombs":@(_route.unmatchedBombs),
        @"mortalPoisonErased":@(_route.mortalPoisonErased),
        @"thornContacts":@(_route.thornContacts), @"thornDamage":@(_route.thornDamage), @"heartFours":@(_route.heartFours),
        @"thornHPKnown":@(_live.thorns.known), @"thornHP":@(_live.thorns.hp), @"thornMaximumHP":@(_live.thorns.maximumHP),
        @"pendingOrbDamage":@(_live.thorns.pendingDamage),
        @"spinnerMask":@(_live.board.spinnerMask), @"spinnerColors":@(_live.board.spinnerColors),
        @"spinnerTurns":@(_live.spinnerTurns), @"spinnerPeriodFrames":@(_live.spinnerPeriodFrames),
        @"spinnerCountdownKnown":@(_live.spinnerCountdownKnown), @"spinnerCountdown":@(_live.spinnerCountdown),
        @"spinnerReadStatus":PHOrbSpinnerReadDescription(&_live),
        @"spinnerRawCount":@(_live.spinnerRawCount), @"spinnerRawPeriod":@(_live.spinnerRawPeriod),
        @"spinnerRawColors":@(_live.spinnerRawColors), @"spinnerStoredCells":@(_live.spinnerStoredCells),
        @"spinnerRawRows":@[@(_live.spinnerRawRows[0]), @(_live.spinnerRawRows[1]), @(_live.spinnerRawRows[2]),
            @(_live.spinnerRawRows[3]), @(_live.spinnerRawRows[4]), @(_live.spinnerRawRows[5])],
        @"spinnerVariants":@(PHOrbSpinnerVariants(&_live.board)), @"blockedMask":@(PHOrbBlockedCells(&_live.board)),
        @"tapeMask":@(_live.board.tapeMask), @"cloudMask":@(_live.board.cloudMask), @"fixedStart":@(_live.board.fixedStart),
        @"unmatchableColors":@(_live.board.unmatchableColors), @"unmatchableTurns":@(_live.unmatchableTurns),
        @"unmatchableKnown":@(_live.unmatchableKnown),
        @"tapeRowTurns":@(_live.tapeRowTurns), @"tapeColumnTurns":@(_live.tapeColumnTurns), @"cloudTurns":@(_live.cloudTurns),
        @"minErase":@(_live.minErase), @"minMatch":@(_live.board.minMatch),
        @"goalHits":goalHits,
        @"leadersValid":@(_live.leadersValid), @"leaderNumbers":@[@(_live.leaderNumbers[0]), @(_live.leaderNumbers[1])],
        @"leaderContext":LeaderTrace(_live),
        @"searchBudgetMS":@(_solveOptions.milliseconds),
        @"combos":@(_route.combos), @"steps":@(_route.count ? _route.count - 1 : 0), @"confirmed":@(_confirmed),
        @"elapsedMS":@(_route.elapsedMS), @"evaluated":@(_route.evaluated), @"deadline":@(_route.deadlineReached),
        @"evaluationCalls":@(_route.evaluationCalls), @"evaluationCacheHits":@(_route.evaluationCacheHits),
        @"evaluationBreakdownKnown":@(_route.count && [_searchTrace[@"evaluationBreakdownKnown"] boolValue]),
        @"searchPasses":@(_route.searchPasses), @"backtracks":@(_route.backtracks), @"exploredDepth":@(_route.exploredDepth),
        @"completed":@(_completed), @"cancelled":@(_cancelled), @"inputFrames":@(_sentFrames), @"inputFault":@(_inputFault),
        @"startSent":@(_startSent), @"startAccepted":@(_startSent && !_awaitingStart),
        @"multiplayerDisabled":@(_live.multiplayerDisabled), @"nativePhase":@(_live.phase), @"wave":@(_live.wave),
        @"moves":@(_live.moves), @"remainingFrames":@(_live.remainingFrames), @"fps":@(_live.fps),
        @"settled":@(_live.settled), @"ordinary":@(_live.ordinary), @"deviceVerified":@NO};
}
