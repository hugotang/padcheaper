// Run start/stop and countdown policy. Native release cleanup stays in the session.
- (void)stopForLifecycle:(NSString *)reason {
    const bool intent = _auto.active() || _oneShot || PHOrbSessionPendingIntent(_phase, _readQueued, _releaseComplete);
    if (intent) [self queueStop:reason];
    // An aborted gesture may still await release. Keep that state and its
    // original error; the next native frame will complete the release.
}
- (void)queueStop:(NSString *)reason {
    const bool wasAutomatic = _auto.active();
    const bool wasEightAutomatic = _auto.eight();
    const bool pendingStart = [self executing] && _awaitingStart && _startSent;
    const bool skillRunning = (_phase == PHOrbSessionPhase::SkillWaiting) || _openingInFlight ||
        (_eightSkillPending && _eightSkillStarted && _hasLive && _live.phase == 22);
    const bool cancelledIntent = _auto.active() || PHOrbSessionPendingIntent(_phase, _readQueued, _releaseComplete);
    [self cancelWorker];
    [_engine armMultiplayerOnce:NO];
    [_engine armEightPlayerAuto:nullptr];
    [self openSpinnerCell:-1]; // A release is not a down step, so it still works.
    _auto.stop(); _oneShot = _oneShotBound = false;
    _skillWindowWaitAt = 0;
    _readQueued = false; _hasStable = false;
    _message = reason ?: @"已停止";
    if (skillRunning) _message = [_message stringByAppendingString:@"；已啟動的技能由遊戲完成"];
    if (wasEightAutomatic) _eightLastStop = @{@"reason":_message,@"sessionPhase":SessionPhaseText(_phase),
        @"autoTurn":@(_auto.turn),@"autoMoves":@(_auto.moves),@"autoSkills":@(_auto.skills),
        @"wave":@(_live.wave),@"nativePhase":@(_live.phase),@"battleEnded":@(PHOrbBattleEnded(&_live)),
        @"connectionState":@(_live.multiplayer.connectionState),@"localStatus":@(_live.multiplayer.localStatus),
        @"remainingSeconds":@(_live.multiplayer.remainingSeconds),
        @"skillPending":@(_eightSkillPending),@"skillAcknowledged":@(_eightSkillAcknowledged)};
    if (_eightPrep.active && !_eightPrep.used) _eightPlan.guidePrepared = false;
    _eightPrep.active = false;
    if (_dungeonBound && cancelledIntent &&
        ((_hasLive && PHBattleSameIdentity(&_dungeonIdentity,&_live.identity)) ||
         (wasAutomatic && _auto.bound && PHBattleSameIdentity(&_dungeonIdentity,&_auto.origin.identity)))) {
        _dungeonLastStop = @{@"reason":_message, @"sessionPhase":SessionPhaseText(_phase), @"autoTurn":@(_auto.turn),
            @"waveKnown":@(_hasLive && PHBattleSameIdentity(&_dungeonIdentity,&_live.identity)),
            @"wave":@(_live.wave), @"capturedUptimeMS":@(uint64_t(NSProcessInfo.processInfo.systemUptime * 1000))};
    }
    if (_recordingExecution) {
        [self recordExecution:@"cancelled" state:_hasLive ? &_live : nullptr evaluation:nil];
        NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
        if (!record[@"reason"]) record[@"reason"] = _message;
        record[@"assistRecovery"] = _assistTrace;
        _executionHistory[_executionHistory.count - 1] = [record copy];
    }
    if (wasAutomatic) [self logAutomation:[NSString stringWithFormat:@"連續模式停止：%@", _message]];
    _releaseComplete = false;
    if (_assistAwaitingResult) {
        NSMutableDictionary *trace = [_assistTrace mutableCopy];
        if ([trace[@"status"] isEqual:@"awaiting-next-turn"]) trace[@"status"] = @"interrupted";
        trace[@"actualConfirmed"] = @NO; trace[@"reason"] = _message;
        _assistTrace = [trace copy]; _assistAwaitingResult = false;
        if (_executionHistory.count) {
            NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
            record[@"assistRecovery"] = _assistTrace; _executionHistory[_executionHistory.count - 1] = [record copy];
        }
    }
    if (_ownedOrb || _staged || pendingStart) _phase = PHOrbSessionPhase::Releasing;
    else { _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false; }
    if (cancelledIntent) ++_cancelled;
}

- (void)queueAutomatic {
    if (_auto.active() || _oneShot || [self executing] || _inputFault || !_frame.ready || !_frame.enabled) return;
    _monitoring = true;
    PHOrbSnapshot current{}; NSString *reason = nil;
    const BOOL read = [_engine read:&current frame:_frame reason:&reason];
    _hasLive = read; _live = current;
    _liveReadReason = [reason copy] ?: @""; _lastReadUptime = NSProcessInfo.processInfo.systemUptime;
    [_engine armMultiplayerOnce:NO];
    if (read && PHOrbBattleEnded(&current)) { [self queueStop:BattleEndMessage(current)]; return; }
    if (read && current.multiplayer.kind == PHOrbMultiplayerEight) {
        [self startEightAutomatic:&current]; return;
    }
    if (current.multiplayer.kind != PHOrbMultiplayerNone || current.multiplayerDisabled || (current.present &&
        !PHBattleIsSinglePlayer(current.identity.entryMode, current.identity.multiplayer))) {
        [self queueStop:@"多人只支援轉一次；連續轉珠、開場與技能規則限單人"]; return;
    }
    [self cancelWorker];
    _auto.beginSolo(_now); _monitoring = true; _hasStable = _readQueued = false;
    _unavailableRules = 0; [self clearRoute]; _goalPlan = @{};
    _assistTrace = @{}; _assistAwaitingResult = false; _assistCheckedAt = 0;
    _skillWindowWaitAt = 0;
    _boardTransitionFrames = 0; _lastReadIssue = @{};
    _activeProfile = @{};
    [_automationLog removeAllObjects];
    [_ruleDecisions removeAllObjects]; [_unavailableReasons removeAllObjects];
    [_skillAttemptContexts removeAllObjects];
    [_decisionContexts removeAllObjects];
    _decisionRules = @[]; _decisionState = {}; _decisionTurn = _decisionObservedTurn = _recoveryTurn = 0;
    _phase = PHOrbSessionPhase::AutoWaiting; _message = @"連續模式準備中；點浮動「停止」可隨時中斷";
}
- (void)logAutomation:(NSString *)message {
    [_automationLog addObject:message];
    if (_automationLog.count > 12) [_automationLog removeObjectAtIndex:0];
}
- (void)waitForAutomationBoard {
    _phase = PHOrbSessionPhase::AutoWaiting; _auto.phaseAt = _now; _hasStable = false;
    _message = [NSString stringWithFormat:@"連續第 %lu 回合 · 等待盤面穩定", (unsigned long)_auto.turn];
}

- (BOOL)tryCountdownAutomatic:(const PHOrbSnapshot *)state read:(BOOL)read active:(BOOL)active {
    // User setting (2026-11-04): start 8P Auto by itself once the countdown
    // finishes. At most once per match (clock identity); a manual stop is not
    // undone in the same match, and nothing ever queues or rematches.
    if ([_profileLibrary[@"eightAutoStart"] boolValue] && read && !active && !_auto.active() && !_oneShot &&
        !_inputFault && _frame.ready && _frame.enabled && _eightAutoStart.claimCountdown(*state)) {
        [self logAutomation:@"8 人倒數完成：依設定自動開始本場 Auto"];
        [self queueAutomatic]; return YES;
    }
    return NO;
}
