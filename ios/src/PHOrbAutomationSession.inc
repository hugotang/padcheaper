// Single-player Auto: bind, observe, choose, then dispatch on the native frame.
// Engine arming, source validation and gesture ownership remain explicit.
- (void)resetRuleTiming {
    _observedTurn = 0;
    _historyPuzzleStarted = false;
    std::fill(std::begin(_ruleLastUseTurns), std::end(_ruleLastUseTurns), 0);
    std::fill(std::begin(_groupLastUseTurns), std::end(_groupLastUseTurns), 0);
}
- (PHOrbRuleMatch)triggerRule:(NSUInteger)index state:(const PHOrbSnapshot *)state reason:(NSString **)reason {
    NSDictionary *rule = _skillRules[index];
    const NSUInteger group = [rule[@"group"] unsignedIntegerValue];
    NSUInteger last = group ? _groupLastUseTurns[group - 1] : _ruleLastUseTurns[index];
    // A same-turn attempt without a completion receipt must still expose a
    // required rescue. Per-turn availability stops it; the new interval must
    // not hide it merely because its uncertain start reserved the clock.
    const bool attempted = (_usedRules & (1u << index)) || (group && (_usedGroups & (1u << (group - 1))));
    if (attempted && last == _observedTurn && [rule[@"required"] boolValue]) last = 0;
    return PHOrbTriggerSkillRuleWithTiming(rule, state, _auto.turn, _observedTurn, last, reason);
}
- (BOOL)hasPendingSkillWindow:(const PHOrbSnapshot *)state {
    if (_openingInFlight || [_activeProfile[@"opening"] count] > _openingStep) return YES;
    if (_openingPuzzlePending && _activeProfile[@"formation"]) return NO;
    for (NSUInteger i = 0; i < _skillRules.count; ++i) {
        NSDictionary *rule = _skillRules[i];
        NSUInteger slot = [rule[@"slot"] unsignedIntegerValue] - 1, group = [rule[@"group"] unsignedIntegerValue];
        if (![rule[@"enabled"] boolValue] || ((_usedRules | _confirmedRules | _unavailableRules) & (1u << i)) ||
            (_usedSlots & (1u << slot)) || (group && ((_usedGroups | _confirmedGroups) & (1u << (group - 1)))) ||
            ([rule[@"once"] boolValue] && [_onceRules containsObject:rule])) continue;
        if ([rule[@"monster"] unsignedIntValue] && [rule[@"monster"] unsignedIntValue] != state->automation.members[slot].number &&
            !state->automation.members[slot].temporaryTurns) continue;
        return YES;
    }
    return NO;
}
- (void)deferSkillWindow:(NSString *)reason {
    [self waitForAutomationBoard];
    if (!_skillWindowWaitAt) _skillWindowWaitAt = _now;
    if (_now - _skillWindowWaitAt >= SkillWindowTimeout) {
        [self queueStop:[NSString stringWithFormat:@"%@；等待 3 秒後仍未就緒，已停止，未施放或轉珠", reason]];
    } else {
        _message = [NSString stringWithFormat:@"%@；此次未施放，等待重查", reason];
    }
}
- (BOOL)waitForSkillWindow {
    if (![self hasPendingSkillWindow:&_live]) { _skillWindowWaitAt = 0; return NO; }
    const auto &state = _live.automation;
    // Known lasting restrictions retain optional-rule behavior. A temporary
    // closed window is neither a native refusal nor permission to start a drag.
    const bool persistent = PersistentSkillWindow(state);
    if (!state.window && persistent && [_activeProfile[@"opening"] count] > _openingStep) {
        [self queueStop:[NSString stringWithFormat:@"開場第 %lu 步未就緒：%@；未施放或跳到下一步",
            (unsigned long)_openingStep + 1, PHOrbSkillWindowDescription(&state)]];
        return YES;
    }
    if (persistent) { _skillWindowWaitAt = 0; return NO; }
    if (state.window) return NO; // Require a stable decision/cast before clearing the deferral timer.
    if (!_skillWindowWaitAt) _skillWindowWaitAt = _now;
    if (_now - _skillWindowWaitAt >= SkillWindowTimeout) {
        [self queueStop:[NSString stringWithFormat:@"%@；等待 3 秒後仍未就緒，已停止，未施放或轉珠",
            PHOrbSkillWindowDescription(&state)]];
    } else {
        _hasStable = false;
        _message = [NSString stringWithFormat:@"%@；等待重新確認，未施放或轉珠", PHOrbSkillWindowDescription(&state)];
    }
    return YES;
}
// Keep once-per-battle and per-orb-turn history across a manual stop/start.
// Transient read failures, phase 22 and skill-cleared floors do not reset it.
- (void)observeRuleHistory:(const PHOrbSnapshot *)state read:(BOOL)read {
    if (!read) { _historyPuzzleStarted = false; return; }
    if (!state->present) {
        _openingProfile = nil; _openingStep = 0; _temporaryMask = 0; _openingManual = false;
        _openingInFlight = _openingAcknowledged = _openingStarted = _openingPuzzlePending = false;
        [_openingLog removeAllObjects];
        _historyBound = _historyResolve = false; [_onceRules removeAllObjects];
        _historyRules = nil; _historyTimingProfile = nil;
        [self resetRuleTiming];
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        [_unavailableReasons removeAllObjects];
        std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
        return;
    }
    if (!state->valid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer)) {
        _historyPuzzleStarted = false; return;
    }
    if (!_historyBound || !PHBattleSameIdentity(&_historyIdentity, &state->identity)) {
        _openingProfile = nil; _openingStep = 0; _temporaryMask = 0; _openingManual = false;
        _openingInFlight = _openingAcknowledged = _openingStarted = _openingPuzzlePending = false;
        [_openingLog removeAllObjects];
        _historyIdentity = state->identity; _historyBound = true; _historyResolve = false;
        _historyRules = nil; _historyTimingProfile = nil;
        [self resetRuleTiming]; _observedTurn = 1;
        [_onceRules removeAllObjects];
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        [_unavailableReasons removeAllObjects];
        std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
    }
    const bool timed = AcceptedSkillTiming(_historyRules);
    if (state->phase == 22 && !_historyResolve) _historyPuzzleStarted = false;
    if (timed && state->phase == 2 && state->dragged) _historyPuzzleStarted = true;
    // A zero-swap turn needs an observed drag or an explicitly pending opening
    // puzzle. A phase/floor transition alone must not advance skill intervals.
    if (ResolvingOrbs(state->phase) && (state->moves > 0 || _openingPuzzlePending || (timed && _historyPuzzleStarted)))
        _historyResolve = true;
    if (_historyResolve && state->phase == 2 && state->moves == 0 && state->settled && !state->dragged &&
        !state->boardTransition && (!(timed || _openingPuzzlePending) || (state->automation.valid && !state->automation.busy))) {
        // Count this observed resolve/idle pair once. Phase 22, a floor clear,
        // elapsed time or a stop/start never advances the timing clock.
        if (_observedTurn && _observedTurn < NSUIntegerMax) ++_observedTurn;
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        _openingPuzzlePending = false;
        [_unavailableReasons removeAllObjects]; _historyResolve = false;
    }
    if (!_historyResolve && state->phase == 2 && !state->moves && state->settled && !state->dragged && !state->boardTransition)
        _historyPuzzleStarted = false;
}
// Called on every observed native frame while armed, even during animation.
// A skill's phase 22 is never evidence that an orb turn has completed.
- (BOOL)observeAutomation:(PHOrbSnapshot *)state read:(BOOL)read reason:(NSString *)reason {
    if (read && PHOrbBattleEnded(state)) {
        [self queueStop:BattleEndMessage(*state)];
        _ownedOrb = 0; _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false;
        return NO;
    }
    if (_auto.eight()) return [self observeEightAutomation:state read:read reason:reason];
    [self observeRuleHistory:state read:read];
    if (!_frame.ready || !_frame.enabled || _now < _auto.phaseAt) {
        [self queueStop:@"總開關、前景或登入狀態已改變，已停止連續模式"]; return NO;
    }
    if (!read || !state->present || !state->valid || !state->automation.valid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) || state->identity.sourceMode != 0 || state->wave >= 999) {
        [self recordReadIssue:state reason:reason ?: @"戰鬥資料無法確認" multiplayer:NO];
        [self queueStop:reason ?: @"關卡已結束或戰鬥資料無法確認，已停止連續模式"]; return NO;
    }
    if (!_auto.bound) {
        NSDictionary *retained = _openingProfile ? PHOrbProfileNamed(_profileLibrary, _openingProfile[@"id"]) : nil;
        const bool resume = retained && [retained[@"formation"] isEqual:_openingProfile[@"formation"]] &&
            [retained[@"opening"] isEqual:_openingProfile[@"opening"]] &&
            [retained[@"dungeon"] isEqual:_openingProfile[@"dungeon"]] && [retained[@"floor"] isEqual:_openingProfile[@"floor"]] &&
            ([_profileLibrary[@"autoSelect"] boolValue] || [_profileLibrary[@"selected"] isEqual:retained[@"id"]]);
        NSDictionary *resolved = resume ? @{@"ok":@YES, @"profile":retained, @"message":retained[@"name"]} :
            PHOrbResolveProfile(_profileLibrary, state);
        const bool recoverBinding = resume ? [retained[@"settings"][@"assistRecoveryL"] boolValue] :
            [resolved[@"ok"] boolValue] ? ([resolved[@"profile"] count] ?
                [resolved[@"profile"][@"settings"][@"assistRecoveryL"] boolValue] : _assistRecoveryL) : [self bindingNeedsAssistRecovery];
        const bool assistWindow = state->phase == 2 && state->settled && !state->boardTransition &&
            !state->automation.busy && !state->dragged;
        if (recoverBinding && !assistWindow && !(resume && _openingInFlight)) {
            if (!PHInputIsQuiet(&state->input) || state->dragged) {
                [self queueStop:@"偵測到手動操作，已停止連續模式"]; return NO;
            }
            if (_now - _auto.phaseAt > 12) {
                [self queueStop:@"尚未取得穩定的單人隊伍及武裝狀態，已停止"]; return NO;
            }
            _message = @"等待盤面穩定後核對隊伍及武裝狀態…"; return YES;
        }
        if (recoverBinding && assistWindow) {
            if (![self captureAssistRecovery:state]) return NO;
            if (!resume) resolved = PHOrbResolveProfile(_profileLibrary, state);
        }
        if (![resolved[@"ok"] boolValue]) { [self queueStop:resolved[@"message"]]; return NO; }
        _activeProfile = resolved[@"profile"];
        if (_activeProfile[@"formation"]) {
            if (!resume && _openingInFlight) {
                [self queueStop:@"前一次開場技能尚未確認完成，不能切換流程或跳過此步"]; return NO;
            }
            if (!resume && [_activeProfile[@"opening"] count] && _openingProfile && _openingStep) {
                [self queueStop:@"本場已有不同的開場紀錄，不能以另一份流程重播開場；請使用不含開場的策略或下一場再開始"]; return NO;
            }
            // A player who transformed manually (enemy CD delay) starts Auto
            // after that transform; the visible form is the evidence.
            const NSUInteger manualStep = !resume && [_activeProfile[@"opening"] count] &&
                !PHOrbProfileMatchesOpeningTeam(_activeProfile, state, 0) ?
                PHOrbProfileManualOpeningProgress(_activeProfile, state) : 0;
            if (!resume) {
                if (!manualStep && [_activeProfile[@"opening"] count] &&
                    (state->wave || state->moves || state->phase != 2 || state->automation.skillUses)) {
                    [self queueStop:@"開場只可在首層、尚未轉珠及施放技能時開始；中途加入請使用不含開場的策略"]; return NO;
                }
                _openingProfile = _activeProfile; _openingStep = 0; _openingManual = false;
                _openingInFlight = _openingAcknowledged = _openingStarted = _openingPuzzlePending = false;
                [_openingLog removeAllObjects];
                if (manualStep) [self adoptOpeningFrom:manualStep];
            }
            if (resume && !_openingInFlight) [self adoptManualOpeningProgress:state];
            const bool openingDone = resume && _openingStep == [_openingProfile[@"opening"] count];
            if (!_openingInFlight && !(openingDone ? PHOrbProfileMatchesRunningTeam(_openingProfile, state) :
                PHOrbProfileMatchesOpeningTeam(_openingProfile, state, _openingStep))) {
                [self queueStop:@"目前本體／武裝與已確認的開場進度不符，未重播技能"]; return NO;
            }
            if (_openingInFlight && !_openingStarted) {
                [self queueStop:@"前一次開場技能啟動結果不明，本場不能重試或跳過此步；請手動完成本場"]; return NO;
            }
            if (_openingInFlight) _phase = PHOrbSessionPhase::OpeningWaiting;
        } else if (_openingInFlight) {
            [self queueStop:@"前一次開場技能尚未確認完成，請等待原生技能結束；不會啟動另一份策略"]; return NO;
        }
        if (_activeProfile.count) [self loadSettings:PHOrbCanonicalSettings(_activeProfile[@"settings"])];
        // Idle config reloads can restore different common settings after an
        // automatically selected profile stops. Only rules chosen for a new
        // run may reset its cursor; reloading the UI must not restart a cycle.
        NSDictionary *timingProfile = SkillTimingProfile(_activeProfile);
        const bool rulesChanged = ![_historyRules isEqual:_skillRules];
        const bool timingChanged = rulesChanged || ![_historyTimingProfile isEqual:timingProfile];
        if (rulesChanged || (timingChanged && AcceptedSkillTiming(_skillRules))) {
            _usedRules = _unavailableRules = _confirmedRules = _confirmedGroups = 0;
            [_unavailableReasons removeAllObjects];
            std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
        }
        if (timingChanged) {
            [self resetRuleTiming]; _observedTurn = 1; _historyResolve = false;
            _historyRules = _skillRules; _historyTimingProfile = timingProfile;
        }
        [self logAutomation:[NSString stringWithFormat:@"本次策略：%@", resolved[@"message"]]];
        _auto.bind(*state);
    }
    if (!PHBattleSameIdentity(&_auto.origin.identity, &state->identity) || _auto.origin.inputManager != state->inputManager) {
        [self queueStop:@"已離開本次戰鬥，連續模式停止"]; return NO;
    }
    if (!PHInputIsQuiet(&state->input) || (state->dragged && ![self executing])) {
        [self queueStop:@"偵測到手動操作，已停止連續模式"]; return NO;
    }
    if (_watchPlayerState && state->phase == 2 &&
        ((_phase == PHOrbSessionPhase::Solving) || (_phase == PHOrbSessionPhase::Preview) || (_phase == PHOrbSessionPhase::Arming) || (_phase == PHOrbSessionPhase::Running)) &&
        !PHOrbSamePlayerState(&_preview, state)) {
        // A change in the native frame accepting our first touch still needs
        // an owned-orb release; it must not bypass the new rescue condition.
        if (_staged && _awaitingStart && _route.count && state->dragged == _preview.board.tokens[_route.cells[0]])
            _ownedOrb = state->dragged;
        [self queueStop:@"求路後 HP 或我方異常已改變，請重新開始判斷自救規則"]; return NO;
    }
    if ((_phase == PHOrbSessionPhase::SkillWaiting) && state->automation.skillUses == _skillBefore.automation.skillUses + 1)
        _skillAcknowledged = true; // Preserve acknowledgement if a skill clears the floor and the counter later resets.
    if (_openingInFlight && _openingStarted && state->automation.skillUses == _openingBefore.automation.skillUses + 1)
        _openingAcknowledged = true;
    if (state->boardTransition) {
        ++_boardTransitionFrames; _hasStable = false;
        [self recordReadIssue:state reason:@"等待原生改盤動畫完成" multiplayer:NO];
        _message = _openingInFlight ? [NSString stringWithFormat:@"開場第 %lu 步：等待原生改盤動畫完成…", (unsigned long)_openingStep + 1] :
            @"等待原生改盤動畫完成…";
    }
    const bool beforeTouch = (_phase == PHOrbSessionPhase::Solving) || (_phase == PHOrbSessionPhase::Preview) ||
        ((_phase == PHOrbSessionPhase::Arming) && !_startSent && !state->dragged);
    // Only an outstanding preview is polled, at a bounded rate. Actual input
    // still gets an unthrottled read after native coordinate conversion.
    if (_preview.assistRecovery.valid && ((_phase == PHOrbSessionPhase::Solving) || (_phase == PHOrbSessionPhase::Preview)) &&
        (_now < _assistCheckedAt || _now - _assistCheckedAt >= 0.25) && ![self validateAssistRecovery:state]) return NO;
    if (_preview.enemyCombo.valid && ((_phase == PHOrbSessionPhase::Solving) || (_phase == PHOrbSessionPhase::Preview)) &&
        (_now < _enemyComboCheckedAt || _now - _enemyComboCheckedAt >= 0.25) && ![self validateEnemyCombo:state]) return NO;
    if (!state->boardTransition && state->phase == 2 &&
        !state->automation.window && beforeTouch && [self hasPendingSkillWindow:state] &&
        !PersistentSkillWindow(state->automation)) {
        [self cancelWorker]; [self clearRoute];
        [self waitForAutomationBoard];
        _message = @"轉珠前技能窗口已關閉，重新確認後再判斷技能";
    }
    return YES;
}
// A delayed opening skill (enemy CD delay) stops Auto before the step. If the
// player then casts the form-changing step manually, the recorded cursor no
// longer matches the team. Advance only to the first later step whose own
// afterMonster form is now visible; steps without form evidence stay pending.
- (BOOL)adoptManualOpeningProgress:(const PHOrbSnapshot *)state {
    NSArray *opening = _openingProfile[@"opening"];
    if (_openingStep >= opening.count || PHOrbProfileMatchesOpeningTeam(_openingProfile, state, _openingStep) ||
        state->automation.skillUses <= _openingBefore.automation.skillUses) return NO;
    for (NSUInteger k = _openingStep + 1; k <= opening.count; ++k) {
        if (![opening[k - 1][@"afterMonster"] unsignedIntValue]) continue;
        if (!PHOrbProfileMatchesOpeningTeam(_openingProfile, state, k)) return NO;
        [self adoptOpeningFrom:k]; return YES;
    }
    return NO;
}
- (void)adoptOpeningFrom:(NSUInteger)k {
    NSArray *opening = _openingProfile[@"opening"];
    const NSUInteger from = _openingStep;
    for (NSUInteger i = from; i < k; ++i)
        [_openingLog addObject:@{@"step":@(i + 1), @"slot":opening[i][@"slot"], @"source":opening[i][@"source"],
            @"skill":opening[i][@"skill"], @"status":@"manual"}];
    _openingStep = k; _openingManual = true;
    [self logAutomation:[NSString stringWithFormat:@"偵測到手動完成開場第 %lu–%lu 步（形態已變更），從第 %lu 步接續；之後未就緒的開場技能改為略過",
        (unsigned long)from + 1, (unsigned long)k, (unsigned long)k + 1]];
    if (k == opening.count && [opening[k - 1][@"puzzleAfter"] boolValue]) _openingPuzzlePending = true;
}
- (BOOL)finishOpeningIfConfirmed:(const PHOrbSnapshot *)state {
    if (!_openingInFlight || !_openingStarted || !_openingAcknowledged || !state->valid || !state->automation.valid ||
        state->boardTransition || state->phase != 2 || !state->settled ||
        state->automation.busy || state->dragged ||
        (state->wave == _openingBefore.wave && state->automation.skillUses != _openingBefore.automation.skillUses + 1) ||
        !PHOrbProfileMatchesOpeningTeam(_openingProfile, state, _openingStep + 1)) return NO;
    NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"confirmed";
    record[@"usesAfter"] = @(state->automation.skillUses);
    _openingLog[_openingLog.count - 1] = [record copy];
    ++_openingStep; _openingInFlight = false;
    if (_openingStep == [_openingProfile[@"opening"] count] &&
        [_openingProfile[@"opening"][_openingStep - 1][@"puzzleAfter"] boolValue]) {
        _openingPuzzlePending = true;
        [self logAutomation:@"開場完成：先轉此盤，確認下一回合後才接技能循環"];
    }
    [self logAutomation:[NSString stringWithFormat:@"開場 %lu／%lu 已確認完成", (unsigned long)_openingStep,
        (unsigned long)[_openingProfile[@"opening"] count]]];
    return YES;
}
- (void)advanceAutomation {
    if (_auto.eight()) { [self advanceEightAutomation]; return; }
    if (!_auto.active() || !_auto.bound || [self executing] || (_phase == PHOrbSessionPhase::Solving) || (_phase == PHOrbSessionPhase::Preview) ||
        (_phase == PHOrbSessionPhase::SkillArming) || (_phase == PHOrbSessionPhase::OpeningArming) || _now - _auto.startedAt < 0.35) return;
    if ((_phase == PHOrbSessionPhase::AutoWaiting) && _live.phase == 2 && !_live.boardTransition &&
        _live.settled && [self waitForSkillWindow]) return;
    if (_phase == PHOrbSessionPhase::TurnWaiting) {
        _auto.sawResolve |= ResolvingOrbs(_live.phase);
        if (_now - _auto.phaseAt > TurnTimeout) { [self queueStop:@"等候下一回合逾時，請檢查遊戲畫面"]; return; }
        if (!_auto.sawResolve || _live.boardTransition || _live.phase != 2 || _live.moves != 0 || _live.automation.busy || !_live.settled) {
            _hasStable = false; return;
        }
    } else if (_phase == PHOrbSessionPhase::OpeningWaiting) {
        if (_now - _auto.phaseAt > SkillTimeout) { [self queueStop:@"開場技能等候逾時；保留進度，不會重複施放"]; return; }
        if (_live.boardTransition || _live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
        if (!_openingAcknowledged) {
            _openingStarted = false;
            NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"unconfirmed";
            _openingLog[_openingLog.count - 1] = [record copy];
            [self queueStop:@"開場技能使用次數未確認；已停止，不能重播此步"]; return;
        }
    } else if (_phase == PHOrbSessionPhase::SkillWaiting) {
        if (_now - _auto.phaseAt > SkillTimeout) { [self queueStop:@"等待技能完成逾時，已停止；請查看遊戲是否仍有動畫或選擇畫面"]; return; }
        if (_live.boardTransition || _live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
        if (!_skillAcknowledged) {
            [self queueStop:@"技能使用次數未確認，已停止以免重複施放"]; return;
        }
    } else if (_phase == PHOrbSessionPhase::AutoWaiting) {
        if (_now - _auto.phaseAt > 12) {
            [self queueStop:@"等待 12 秒仍未取得穩定的單人操作與技能狀態，已停止；請查看遊戲畫面"]; return;
        }
        if (_live.boardTransition || _live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
    } else return;
    if (!_hasStable || !PHOrbSameAutomation(&_live, &_stable)) {
        _stable = _live; _stableAt = _now; _hasStable = true; return;
    }
    if (_now - _stableAt < AutomationSettle) return;
    // Only stable, active decisions request mechanism evidence. Skills run
    // first; the final goal capture below always reads again after them.
    if (_assistRecoveryL && ![self captureAssistRecovery:&_live]) return;
    if (_phase == PHOrbSessionPhase::OpeningWaiting) {
        if (![self finishOpeningIfConfirmed:&_live]) {
            [self queueStop:@"開場技能完成後的次數、角色或武裝不符合預期；保留進度，未執行下一步"]; return;
        }
        ++_auto.skills; [self waitForAutomationBoard]; return;
    }
    if (_phase == PHOrbSessionPhase::TurnWaiting) {
        if (![self confirmAssistRecoveryResult:&_live]) return;
        ++_auto.turn;
        [self waitForAutomationBoard]; return;
    }
    if (_phase == PHOrbSessionPhase::SkillWaiting) {
        ++_auto.skills; _unavailableRules = 0; // Haste may make a formerly unavailable member ready.
        [_unavailableReasons removeAllObjects];
        _confirmedRules |= 1u << _skillRule;
        _ruleDecisions[_skillRule] = @"本回合已確認完成";
        NSUInteger group = [_skillRules[_skillRule][@"group"] unsignedIntegerValue];
        if (group) _confirmedGroups |= 1u << (group - 1);
        [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 技能完成", (unsigned long)_auto.turn, (unsigned long)_skillRule + 1]];
        [self waitForAutomationBoard]; return;
    }
    if (_activeProfile[@"formation"]) {
        const bool openingDone = _openingStep == [_activeProfile[@"opening"] count];
        if (!(openingDone ? PHOrbProfileMatchesRunningTeam(_activeProfile, &_live) :
              PHOrbProfileMatchesOpeningTeam(_activeProfile, &_live, _openingStep))) {
            [self queueStop:@"隊伍形態或武裝與本次流程不符，已停止"]; return;
        }
        if (_openingStep < [_activeProfile[@"opening"] count]) {
            _openingBefore = _live; _phase = PHOrbSessionPhase::OpeningArming;
            _message = [NSString stringWithFormat:@"開場 %lu／%lu：準備使用位置 %@ %@技能",
                (unsigned long)_openingStep + 1, (unsigned long)[_activeProfile[@"opening"] count],
                _activeProfile[@"opening"][_openingStep][@"slot"],
                [_activeProfile[@"opening"][_openingStep][@"source"] isEqual:@"body"] ? @"本體" : @"武裝"];
            return;
        }
    }
    if (_openingPuzzlePending && _activeProfile[@"formation"]) {
        // The last opening skill may leave a CD1 supplier uncharged. Keep
        // this choice across stop/start; only a confirmed orb turn clears it.
        if (!PHOrbCanPreview(&_live)) {
            [self queueStop:@"開場完成後盤面或操作條件尚未確認，未轉珠或接續技能循環"]; return;
        }
        [self beginSolve]; return;
    }
    uint8_t temporaryMask = 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const auto &member = _live.automation.members[slot];
        if (member.temporaryTurns) {
            temporaryMask |= uint8_t(1u << slot);
            if (!(_temporaryMask & (1u << slot))) [self logAutomation:[NSString stringWithFormat:
                @"位置 %u 暫時變身：#%u → #%u；保留流程，略過原形態技能", slot + 1, member.identityNumber, member.number]];
        } else if (_temporaryMask & (1u << slot)) [self logAutomation:[NSString stringWithFormat:
            @"位置 %u 暫時變身已結束，恢復原技能循環", slot + 1]];
    }
    _temporaryMask = temporaryMask;
    if (_decisionTurn != _auto.turn) [_skillAttemptContexts removeAllObjects];
    [_ruleDecisions removeAllObjects]; _decisionTurn = _auto.turn; _decisionObservedTurn = _observedTurn;
    _decisionRules = _skillRules; _decisionState = _live;
    [_decisionContexts removeAllObjects];
    for (NSUInteger i = 0; i < _skillRules.count; ++i) {
        [_ruleDecisions addObject:@"尚未判斷"];
        NSUInteger slot = [_skillRules[i][@"slot"] unsignedIntegerValue] - 1;
        NSDictionary *attempt = ((_unavailableRules | _usedRules) & (1u << i)) ? _skillAttemptContexts[@(i)] : nil;
        [_decisionContexts addObject:attempt ?: SkillDecisionContext(_live, slot)];
    }
    uint16_t unmetRequired = 0;
    for (NSNumber *index in PHOrbSkillRuleOrder(_skillRules, _groupNext)) {
        NSUInteger i = index.unsignedIntegerValue;
        NSDictionary *rule = _skillRules[i];
        NSUInteger slot = [rule[@"slot"] unsignedIntegerValue] - 1;
        NSUInteger group = [rule[@"group"] unsignedIntegerValue];
        if (![rule[@"enabled"] boolValue]) { _ruleDecisions[i] = @"已停用"; continue; }
        if ((_confirmedRules & (1u << i)) || (group && (_confirmedGroups & (1u << (group - 1))))) {
            _ruleDecisions[i] = group ? @"同組本回合已有技能確認完成" : @"本回合已確認完成";
            continue;
        }
        const PHOrbSkillMember *member = &_live.automation.members[slot];
        NSString *unavailable = nil;
        if (_usedRules & (1u << i)) unavailable = @"本回合已嘗試，但未確認完成";
        else if (_usedSlots & (1u << slot)) unavailable = @"此位置本回合已使用，不能重複施放";
        else if (group && (_usedGroups & (1u << (group - 1)))) unavailable = @"同組本回合已嘗試，但未確認完成";
        else if ([rule[@"once"] boolValue] && [_onceRules containsObject:rule]) unavailable = @"本場一次的使用紀錄已消耗";
        else if (PHOrbSkillReservedUntil(_skillRules, slot) > _live.wave + 1)
            unavailable = [NSString stringWithFormat:@"此位置保留至第 %lu 層", (unsigned long)PHOrbSkillReservedUntil(_skillRules, slot)];
        else if (_unavailableRules & (1u << i)) unavailable = _unavailableReasons[@(i)] ?: @"遊戲判定技能不可用";
        else if (!member->active) unavailable = @"此位置沒有可使用的隊員";
        else if (member->temporaryTurns && (!member->temporaryKnown ||
                 [rule[@"monster"] unsignedIntValue] != member->number))
            unavailable = [NSString stringWithFormat:@"暫時變身為 #%u，原形態技能暫停", member->number];
        else if (member->bound) unavailable = @"施放者被綁定";
        else if (member->state) unavailable = @"施放者尚未回到待命狀態";
        else if (!_live.automation.window) unavailable = PHOrbSkillWindowDescription(&_live.automation);
        // Optional rules retain the previous short circuit: unavailable
        // actors do not make unrelated, unknown condition data mandatory.
        if (unavailable && ![rule[@"required"] boolValue]) {
            _ruleDecisions[i] = [NSString stringWithFormat:@"略過 · %@", unavailable]; continue;
        }
        NSString *trigger = nil;
        PHOrbRuleMatch match = [self triggerRule:i state:&_live reason:&trigger];
        _ruleDecisions[i] = trigger ?: @"條件無法確認";
        if (match == PHOrbRuleUnknown) {
            [self queueStop:[NSString stringWithFormat:@"規則 %lu：%@，已停止", (unsigned long)i + 1, _ruleDecisions[i]]]; return;
        }
        if (match != PHOrbRuleYes) continue;
        if ([rule[@"required"] boolValue]) unmetRequired |= 1u << i;
        if (unavailable) {
            _ruleDecisions[i] = [NSString stringWithFormat:@"%@ · %@", trigger, unavailable]; continue;
        }
        if ([rule[@"once"] boolValue] && _onceRules.count >= 128) {
            [self queueStop:@"本場一次性技能紀錄已達上限，請離開本場後再開始"]; return;
        }
        _skillRule = i; _skillBefore = _live; _phase = PHOrbSessionPhase::SkillArming;
        _ruleDecisions[i] = [NSString stringWithFormat:@"觸發 · %@", trigger];
        [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 觸發 · %@",
            (unsigned long)_auto.turn, (unsigned long)i + 1, trigger]];
        _message = [NSString stringWithFormat:@"連續第 %lu 回合 · 規則 %lu：準備使用位置 %lu 技能",
            (unsigned long)_auto.turn, (unsigned long)i + 1, (unsigned long)slot + 1];
        return;
    }
    // Defer this check until other eligible skills and grouped alternatives
    // had their chance to heal, cleanse or haste, then reevaluate the trigger.
    if (unmetRequired) {
        for (NSUInteger i = 0; i < _skillRules.count; ++i) if (unmetRequired & (1u << i)) {
            [self queueStop:[NSString stringWithFormat:@"必要規則 %lu 未完成：%@；已停止轉珠",
                (unsigned long)i + 1, _ruleDecisions[i]]]; return;
        }
    }
    if (!PHOrbCanPreview(&_live)) {
        NSString *obstacles = PHOrbObstacleDescription(&_live);
        [self queueStop:[NSString stringWithFormat:@"技能規則處理後仍無法轉珠：%@。請解除效果或手動處理後再開始。",
            obstacles.length ? obstacles : @"盤面或操作條件尚未確認"]]; return;
    }
    [self beginSolve];
}

- (void)beforeAutomationFrame:(PHBattleFrame)frame time:(NSTimeInterval)time {
    PHOrbSnapshot current{}; NSString *reason = nil;
    BOOL read = [_engine readAutomation:&current frame:frame reason:&reason];
    _hasLive = read; _live = current;
    _liveReadReason = [reason copy] ?: @""; _lastReadUptime = NSProcessInfo.processInfo.systemUptime;
    if (![self observeAutomation:&current read:read reason:reason]) return;
    if (_auto.eight() && (_phase == PHOrbSessionPhase::EightSkillArming)) {
        [self castEightSkill:&current frame:frame]; return;
    }
    _live.assistRecovery = current.assistRecovery;
    if (_phase == PHOrbSessionPhase::OpeningArming) {
        if (_assistRecoveryL && ![self captureAssistRecovery:&current]) return;
        NSDictionary *step = _activeProfile[@"opening"][_openingStep];
        const NSUInteger slot = [step[@"slot"] unsignedIntegerValue] - 1;
        if (current.phase == 2 && !current.automation.window &&
            PHOrbSameAutomationExceptWindow(&_openingBefore, &current) && !current.dragged && PHInputIsQuiet(&current.input)) {
            [self deferSkillWindow:PHOrbSkillWindowDescription(&current.automation)];
            return;
        }
        if (!PHOrbSameAutomation(&_openingBefore, &current) ||
            !PHOrbProfileMatchesOpeningTeam(_activeProfile, &current, _openingStep)) {
            [self queueStop:@"開場施放前盤面、隊員或武裝已改變，未施放"]; return;
        }
        PHOrbSkillResult result = [_engine useSkill:slot request:PHOrbRequestForRule(step)
            expected:&_openingBefore frame:frame reason:&reason];
        if (result == PHOrbSkillWindowClosed) {
            [self deferSkillWindow:reason ?: @"技能窗口尚未開放；開場此步未施放"];
            return;
        }
        _skillWindowWaitAt = 0;
        if (result == PHOrbSkillUnavailable) {
            if (_openingManual) {
                // After a manual handoff the player may already have used
                // this skill, or an enemy delay holds it; skip, never retry.
                [_openingLog addObject:@{@"step":@(_openingStep + 1), @"slot":step[@"slot"], @"source":step[@"source"],
                    @"skill":step[@"skill"], @"status":@"skipped"}];
                [self logAutomation:[NSString stringWithFormat:@"開場第 %lu 步未就緒（%@），手動接續後略過",
                    (unsigned long)_openingStep + 1, reason ?: @"遊戲判定技能不可用"]];
                if (++_openingStep == [_activeProfile[@"opening"] count] && [step[@"puzzleAfter"] boolValue])
                    _openingPuzzlePending = true;
                [self waitForAutomationBoard]; return;
            }
            [self queueStop:[NSString stringWithFormat:@"開場第 %lu 步未就緒：%@；未跳到下一步",
                (unsigned long)_openingStep + 1, reason ?: @"遊戲判定技能不可用"]]; return;
        }
        _openingInFlight = true; _openingAcknowledged = false; _openingStarted = result == PHOrbSkillStarted;
        [_openingLog addObject:@{@"step":@(_openingStep + 1), @"slot":step[@"slot"], @"source":step[@"source"],
            @"skill":step[@"skill"], @"usesBefore":@(_openingBefore.automation.skillUses),
            @"consumeAssist":@([step[@"consumeAssist"] boolValue]), @"puzzleAfter":@([step[@"puzzleAfter"] boolValue]),
            @"status":result == PHOrbSkillStarted ? @"started" : @"uncertain"}];
        if (result != PHOrbSkillStarted) {
            [self queueStop:reason ?: @"開場啟動未確認；保留此步，不會重複施放"]; return;
        }
        _phase = PHOrbSessionPhase::OpeningWaiting; _auto.phaseAt = time; _hasStable = false;
        _message = [NSString stringWithFormat:@"開場第 %lu 步已啟動，等待完成…", (unsigned long)_openingStep + 1];
        return;
    }
    if (_phase == PHOrbSessionPhase::SkillArming) {
        NSUInteger slot = [_skillRules[_skillRule][@"slot"] unsignedIntegerValue] - 1;
        if (current.phase == 2 && !current.automation.window &&
            PHOrbSameAutomationExceptWindow(&_skillBefore, &current) && !current.dragged && PHInputIsQuiet(&current.input)) {
            _decisionState = current;
            _decisionContexts[_skillRule] = SkillDecisionContext(current, slot);
            _ruleDecisions[_skillRule] = @"技能窗口尚未開放，此次未施放，等待重查";
            [self deferSkillWindow:PHOrbSkillWindowDescription(&current.automation)];
            return;
        }
        // Recheck color-count conditions on the frame that starts a skill.
        // Spinner-dependent thresholds are unknown, never guessed from a
        // previously sampled color. The native engine rechecks state again.
        if (!PHOrbSameAutomation(&_skillBefore, &current)) {
            [self queueStop:@"技能施放前盤面、隊員或戰鬥狀態已改變，已停止"]; return;
        }
        PHOrbRuleMatch trigger = [self triggerRule:_skillRule state:&current reason:&reason];
        if (trigger != PHOrbRuleYes) {
            if (trigger == PHOrbRuleUnknown) [self queueStop:reason ?: @"技能觸發條件無法確認"];
            else [self waitForAutomationBoard];
            return;
        }
        // The engine rereads once more immediately before its native call.
        _skillAttemptContexts[@(_skillRule)] = SkillDecisionContext(current, slot, true);
        _decisionContexts[_skillRule] = _skillAttemptContexts[@(_skillRule)];
        PHOrbSkillResult result = [_engine useSkill:slot request:PHOrbRequestForRule(_skillRules[_skillRule])
            expected:&_skillBefore frame:frame reason:&reason];
        NSDictionary *rule = _skillRules[_skillRule];
        if (result == PHOrbSkillWindowClosed) {
            NSMutableDictionary *attempt = [_skillAttemptContexts[@(_skillRule)] mutableCopy];
            attempt[@"nativeAttempt"] = @NO;
            attempt[@"skillWindow"] = @NO;
            attempt[@"skillWindowBlocksKnown"] = @NO;
            attempt[@"skillWindowBlocks"] = @0;
            attempt[@"skillWindowReason"] = reason ?: @"技能窗口尚未開放";
            _skillAttemptContexts[@(_skillRule)] = [attempt copy];
            _decisionContexts[_skillRule] = [attempt copy];
            _ruleDecisions[_skillRule] = @"技能窗口尚未開放，此次未呼叫原生施放，等待重查";
            [self deferSkillWindow:reason ?: @"技能窗口尚未開放"];
            return;
        }
        _skillWindowWaitAt = 0;
        // Rejection can include an uncertain native start. A once rule
        // must not be retried by stopping and immediately starting again.
        if (result != PHOrbSkillUnavailable && [rule[@"once"] boolValue]) [_onceRules addObject:rule];
        NSUInteger group = [rule[@"group"] unsignedIntegerValue];
        if (result != PHOrbSkillUnavailable) {
            _usedRules |= 1u << _skillRule; _usedSlots |= 1u << slot;
            _ruleLastUseTurns[_skillRule] = _observedTurn;
            if (group) _groupLastUseTurns[group - 1] = _observedTurn;
            if (group) _usedGroups |= 1u << (group - 1);
        }
        if (result == PHOrbSkillRejected) {
            _ruleDecisions[_skillRule] = [NSString stringWithFormat:@"停止 · %@", reason ?: @"技能啟動校驗失敗"];
            [self queueStop:reason ?: @"技能啟動校驗失敗，已停止"]; return;
        }
        if (result == PHOrbSkillUnavailable) {
            _unavailableRules |= 1u << _skillRule;
            _unavailableReasons[@(_skillRule)] = reason ?: @"技能不可用";
            _ruleDecisions[_skillRule] = [NSString stringWithFormat:@"略過 · %@", reason ?: @"技能不可用"];
            [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 略過 · %@",
                (unsigned long)_auto.turn, (unsigned long)_skillRule + 1, reason ?: @"技能不可用"]];
            [self waitForAutomationBoard];
        } else {
            _ruleDecisions[_skillRule] = @"技能已啟動，等待遊戲完成";
            if (group) {
                NSUInteger next = (_skillRule + 1) % _skillRules.count;
                BOOL timed = NO;
                for (NSDictionary *candidate in _skillRules) if ([candidate[@"enabled"] boolValue] &&
                    [candidate[@"group"] unsignedIntegerValue] == group &&
                    [candidate[@"intervalMode"] isEqual:@"accepted"]) timed = YES;
                if (timed) for (NSUInteger step = 1; step < _skillRules.count; ++step) {
                    const NSUInteger index = (_skillRule + step) % _skillRules.count;
                    NSDictionary *candidate = _skillRules[index];
                    if ([candidate[@"group"] unsignedIntegerValue] == group &&
                        [candidate[@"slot"] unsignedIntegerValue] != slot + 1) { next = index; break; }
                }
                _groupNext[group - 1] = (uint8_t)next;
            }
            _skillAcknowledged = false;
            _phase = PHOrbSessionPhase::SkillWaiting; _auto.phaseAt = time; _hasStable = false;
            _message = [NSString stringWithFormat:@"位置 %lu 技能已啟動，等待遊戲完成…", (unsigned long)slot + 1];
        }
    }
    return;
}
