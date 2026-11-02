// One explicit match intent, fresh native gesture leases and skill receipts.
// Ordinary solo opening, formation and recovery rules never enter this path.
- (void)startEightAutomatic:(const PHOrbSnapshot *)state {
    if (!PHOrbEightMatchReady(state) || !PHOrbCanPreview(state) || state->moves ||
        TimeAvailable(*state) <= ArmDelay + TimeMargin + StepDuration(*state)) {
        [self queueStop:@"請在 8 人倒數完成、本機盤面可操作時按 Auto"]; return;
    }
    NSDictionary *selected = PHOrbProfileNamed(_profileLibrary,_profileLibrary[@"selected"]);
    if (selected && !selected[@"activity"]) {
        [self queueStop:@"8 人請選擇活動策略或通用盤面設定，再按 Auto"]; return;
    }
    const bool sameMatch = PHOrbSameEightMatch(&_eightHistory,state);
    if (!sameMatch) {
        _eightHistory = *state; _eightSkillPending = _eightSkillStarted = _eightBoardSubmitted = _sawResolve = false;
        _eightSequence = nil; _eightSequenceCursor = _eightSequenceStep = 0; _eightAttemptedSlots = 0;
    } else if (_eightSkillPending && !_eightSkillStarted) {
        [self queueStop:@"本場上次技能啟動結果不明，不會重複施放；請手動完成本場"]; return;
    }
    const bool sameProfile = [_eightHistoryProfile isEqual:selected ?: @{}];
    if (!sameProfile) { _eightSequence = nil; _eightSequenceCursor = _eightSequenceStep = 0; }
    if (sameMatch && !_eightSkillPending && !_eightBoardSubmitted &&
        (_eightHistory.wave != state->wave || _eightHistory.multiplayer.turnSerial != state->multiplayer.turnSerial)) {
        _eightSequence = nil; _eightSequenceCursor = _eightSequenceStep = 0; _eightAttemptedSlots = 0;
    }
    _eightHistoryProfile = selected ?: @{};
    if (![_engine armEightPlayerAuto:state]) {
        [self queueStop:@"8 人本場操作身分未確認，未啟動 Auto"]; return;
    }
    [self cancelWorker]; [self clearRoute];
    _automatic = _eightAutomatic = _autoBound = _monitoring = true;
    _eightTeamBound = _hasStable = _readQueued = false;
    _autoOrigin = *state; _autoStartAt = _autoPhaseAt = _now;
    _autoTurn = 1; _autoMoves = _autoSkills = 0;
    _activeProfile = selected ?: @{};
    _skillWindowWaitAt = 0; _goalPlan = @{}; _assistTrace = @{}; _assistAwaitingResult = false;
    [_automationLog removeAllObjects];
    if (selected) [self loadSettings:PHOrbCanonicalSettings(selected[@"settings"])];
    _watchPlayerState = false;
    _phase = _eightSkillPending ? @"eightSkillWaiting" : _eightBoardSubmitted ? @"turnWaiting" : @"autoWaiting";
    _message = @"8 人本場 Auto 準備中；點停止可立即中斷";
    [self logAutomation:selected ? [NSString stringWithFormat:@"8 人活動：%@",selected[@"name"]] :
        @"8 人通用盤面 Auto；使用目前盤面目標"];
}

- (BOOL)observeEightAutomation:(PHOrbSnapshot *)state read:(BOOL)read reason:(NSString *)reason {
    if (!_frame.ready || !_frame.enabled || _now < _autoPhaseAt || !read || !state->present || !state->valid ||
        !PHOrbSameEightMatch(&_autoOrigin,state)) {
        [self queueStop:reason ?: @"已離開本場或 8 人資料無法確認，Auto 停止"]; return NO;
    }
    if (state->multiplayer.finished || (state->multiplayer.clockKnown && state->multiplayer.remainingSeconds <= 0) || state->wave >= 999) {
        [self queueStop:@"8 人本場已結束，Auto 停止"]; return NO;
    }
    if (!PHOrbEightMatchReady(state)) { [self queueStop:@"8 人連線、本機席位或時計已改變，Auto 停止"]; return NO; }
    if (!PHInputIsQuiet(&state->input) || (state->dragged && ![self executing])) {
        [self queueStop:@"偵測到手動操作，已停止 8 人 Auto"]; return NO;
    }
    if (_eightBoardSubmitted && (ResolvingOrbs(state->phase) || state->multiplayer.resolvePending)) _sawResolve = true;
    if (state->eight.valid) {
        _eightLastObserved = *state; // Retain complete evidence for diagnostics after a stop.
        if (!_eightTeamBound) {
            if (_activeProfile[@"activity"] && !PHOrbEightActivityMatches(_activeProfile,state,&reason)) {
                [self queueStop:reason]; return NO;
            }
            if (_eightHistory.eight.valid && !PHOrbSameEightTeam(&_eightHistory.eight,&state->eight)) {
                [self queueStop:@"本場固定隊伍的原生版本已改變，Auto 停止"]; return NO;
            }
            _autoOrigin.eight = state->eight; _eightHistory.eight = state->eight; _eightTeamBound = true;
        } else if (!PHOrbSameEightTeam(&_autoOrigin.eight,&state->eight)) {
            [self queueStop:@"8 人固定隊伍或技能版本已改變，Auto 停止"]; return NO;
        }
    }
    if (_eightSkillPending && state->automation.valid) {
        const auto &before = _eightSkillBefore.automation.members[_eightSlot];
        const auto &after = state->automation.members[_eightSlot];
        if (after.skillUses[0] == unsigned(before.skillUses[0]) + 1 &&
            state->automation.skillUses == unsigned(_eightSkillBefore.automation.skillUses) + 1) _skillAcknowledged = true;
    }
    const bool beforeTouch = [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        ([_phase isEqual:@"arming"] && !_startSent);
    if (beforeTouch && (!PHOrbCanPreview(state) || !PHOrbSameAutomation(&_preview,state) ||
        !PHOrbSameEnemyCombo(&_preview.enemyCombo,&state->enemyCombo))) {
        [self queueStop:@"8 人求路期間盤面、技能或敵方狀態已改變，已停止"]; return NO;
    }
    return YES;
}

- (BOOL)eightNewBoard:(const PHOrbSnapshot *)state {
    return _eightBoardSubmitted && PHOrbCanPreview(state) && state->moves == 0 &&
        (_sawResolve || state->wave != _eightSubmission.wave ||
            state->multiplayer.turnSerial != _eightSubmission.multiplayer.turnSerial) &&
        (_eightSubmission.moves > 0 || !PHOrbSamePlanBoard(&_eightSubmission.board,&state->board) ||
            state->wave != _eightSubmission.wave || state->multiplayer.turnSerial != _eightSubmission.multiplayer.turnSerial);
}

- (void)advanceEightAutomation {
    if (!_automatic || !_eightAutomatic || [self executing] || [_phase isEqual:@"solving"] ||
        [_phase isEqual:@"preview"] || [_phase isEqual:@"eightSkillArming"] || _now - _autoStartAt < ArmDelay) return;
    if (_now - _autoPhaseAt > ([_phase isEqual:@"eightSkillWaiting"] ? SkillTimeout : TurnTimeout)) {
        [self queueStop:@"8 人等待原生技能或下一盤逾時，已停止"]; return;
    }
    if (!_eightTeamBound || !_live.eight.valid || !_live.automation.valid || !PHOrbCanPreview(&_live) || _live.automation.busy) {
        _hasStable = false; return;
    }
    if (!_live.enemyCombo.valid) { [self queueStop:@"8 人當前 Combo 吸收資料未確認，已停止"]; return; }
    if (_eightBoardSubmitted && ![self eightNewBoard:&_live]) { _hasStable = false; return; }
    if (!_hasStable || !PHOrbSameAutomation(&_stable,&_live) || !PHOrbSameEnemyCombo(&_stable.enemyCombo,&_live.enemyCombo)) {
        _stable = _live; _stableAt = _now; _hasStable = true; return;
    }
    if (_now - _stableAt < AutomationSettle) return;
    if (_activeProfile[@"activity"]) {
        NSString *reason = nil;
        if (!PHOrbEightActivityMatches(_activeProfile,&_live,&reason)) { [self queueStop:reason]; return; }
    }
    if (_eightBoardSubmitted) {
        ++_autoTurn; _eightBoardSubmitted = _sawResolve = false;
        _eightHistory = _live;
        _eightSequence = nil; _eightSequenceCursor = _eightSequenceStep = 0; _eightAttemptedSlots = 0;
        [self waitForAutomationBoard]; return;
    }
    if (_eightSkillPending) {
        if (!_eightSkillStarted || !_skillAcknowledged) {
            [self queueStop:@"8 人技能使用次數未確認，保留紀錄避免重複施放"]; return;
        }
        _eightSkillPending = false;
        if (_eightSequenceStep < _eightSequence.count) ++_eightSequenceStep;
        ++_autoSkills;
        [self logAutomation:[NSString stringWithFormat:@"B%u：位置 %lu 技能已確認完成",_live.wave + 1,(unsigned long)_eightSlot + 1]];
        [self waitForAutomationBoard]; return;
    }
    if (_eightSequenceStep < _eightSequence.count) {
        const NSUInteger slot = [_eightSequence[_eightSequenceStep] unsignedIntegerValue] - 1;
        const auto &m = _live.automation.members[slot];
        if (!_live.automation.window) {
            if (!_skillWindowWaitAt) _skillWindowWaitAt = _now;
            if (_now - _skillWindowWaitAt > SkillWindowTimeout) [self queueStop:PHOrbSkillWindowDescription(&_live.automation)];
            return;
        }
        if (!_live.eight.members[slot].skillKnown || m.bound || m.state || m.charge < _live.eight.members[slot].cooldown) {
            [self queueStop:@"8 人技能序列的下一格未就緒，未跳過此步"]; return;
        }
        _eightSlot = slot; _eightSkillBefore = _live; _phase = @"eightSkillArming";
        _message = [NSString stringWithFormat:@"B%u：準備使用位置 %lu 技能",_live.wave + 1,(unsigned long)slot + 1]; return;
    }
    [self beginSolve];
}

- (void)castEightSkill:(const PHOrbSnapshot *)state frame:(PHBattleFrame)frame {
    if (!PHOrbSameAutomation(&_eightSkillBefore,state) || !PHOrbSameEnemyCombo(&_eightSkillBefore.enemyCombo,&state->enemyCombo)) {
        [self queueStop:@"8 人技能施放前盤面或狀態已改變，未施放"]; return;
    }
    NSString *reason = nil;
    PHOrbSkillRequest request{PHOrbSkillBody,state->eight.members[_eightSlot].activeSkill};
    const PHOrbSkillResult result = [_engine useSkill:_eightSlot request:request expected:state frame:frame reason:&reason];
    if (result == PHOrbSkillWindowClosed) {
        [self waitForAutomationBoard];
        if (!_skillWindowWaitAt) _skillWindowWaitAt = _now;
        if (_now - _skillWindowWaitAt > SkillWindowTimeout) [self queueStop:reason ?: @"8 人技能窗口等候逾時"];
        return;
    }
    _eightAttemptedSlots |= uint8_t(1u << _eightSlot); _skillWindowWaitAt = 0;
    if (result == PHOrbSkillUnavailable) {
        _eightSequence = nil; _eightSequenceStep = 0;
        [self logAutomation:reason ?: @"遊戲判定此技能不可用，重讀盤面後重新求路"];
        [self waitForAutomationBoard]; return;
    }
    _eightSkillPending = true; _eightSkillStarted = result == PHOrbSkillStarted; _skillAcknowledged = false;
    if (!_eightSkillStarted) { [self queueStop:reason ?: @"8 人技能啟動未確認，本場不會重試此意圖"]; return; }
    _phase = @"eightSkillWaiting"; _hasStable = false; _autoPhaseAt = _now;
    _message = [NSString stringWithFormat:@"B%u：位置 %lu 技能已啟動，等待生效",state->wave + 1,(unsigned long)_eightSlot + 1];
}

- (NSDictionary *)planEightBoard:(const PHOrbSnapshot *)state options:(PHOrbSolveOptions *)options {
    options->activity.firstWaveOnly = true;
    if (_activeProfile[@"activity"]) {
        NSDictionary *a = _activeProfile[@"activity"];
        if (state->wave >= [a[@"floors"] count]) return @{@"available":@NO,@"message":@"已超出活動設定樓層"};
        NSDictionary *floor = a[@"floors"][state->wave], *weights = floor[@"weights"];
        options->milliseconds = MIN(options->milliseconds,[floor[@"searchMS"] unsignedIntValue]);
        options->activity.colors = [a[@"leader"][@"colors"] unsignedIntValue];
        options->activity.linkLength = [a[@"leader"][@"comboLength"] unsignedIntValue];
        options->activity.comboWeight = [weights[@"combo"] unsignedIntValue];
        options->activity.rowWeight = [weights[@"row"] unsignedIntValue];
        options->activity.linkWeight = [weights[@"link"] unsignedIntValue];
        options->activity.stepCost = [weights[@"step"] unsignedIntValue];
        return PHOrbEightPlan(a,state);
    }
    // Generic 8P Auto uses only the user's board goals and current absorption;
    // it never imports ordinary solo skill rules or assumes leader bonuses.
    NSDictionary *base = [_goalMode isEqual:@"leaders"] ? PHOrbPlanForLeaders(state) :
        @{@"available":@YES,@"goals":[_goalMode isEqual:@"manual"] ? _manualGoals : @[],@"leaders":@[],@"warning":@"",@"message":@"8 人通用盤面"};
    PHOrbGoalSet goals{}; unsigned minimum = 0;
    if (![base[@"available"] boolValue]) return base;
    if (!PHOrbReadGoals(base[@"goals"],&goals) || !PHOrbEnemyComboRequirement(&state->enemyCombo,&minimum) || minimum > 14)
        return @{@"available":@NO,@"message":@"8 人 Combo 吸收狀態未確認或門檻超出支援"};
    unsigned index = goals.count;
    for (unsigned i = 0; i < goals.count; ++i) if (goals.items[i].kind == PHOrbGoalCombo) { index = i; break; }
    if (minimum && index == PHOrbMaxGoals) return @{@"available":@NO,@"message":@"8 人吸收對策超過目標上限"};
    if (minimum && index == goals.count) goals.items[goals.count++] = {PHOrbGoalCombo,0,uint8_t(minimum),0};
    else if (minimum) goals.items[index].count = uint8_t(std::max(minimum,unsigned(goals.items[index].count)));
    NSMutableDictionary *plan = [base mutableCopy]; plan[@"goals"] = PHOrbGoalsJSON(&goals); plan[@"enemyComboFirstWave"] = @YES;
    plan[@"message"] = PHOrbGoalsDescription(plan[@"goals"]);
    return [plan copy];
}

- (BOOL)recoverEightSolve {
    if (!_eightAutomatic || !_activeProfile[@"activity"] || !PHOrbSameAutomation(&_preview,&_live) ||
        !PHOrbSameEnemyCombo(&_preview.enemyCombo,&_live.enemyCombo)) return NO;
    NSArray *sequence = PHOrbEightSkillSequence(_activeProfile[@"activity"],&_live,_eightAttemptedSlots,&_eightSequenceCursor);
    if (!sequence.count) return NO;
    // Only one search budget per observed board; a new search follows an
    // actual skill effect, with a bounded six-slot attempt mask per turn.
    _eightSequence = sequence; _eightSequenceStep = 0;
    [self clearRoute]; [self waitForAutomationBoard];
    [self logAutomation:[NSString stringWithFormat:@"B%u：本次未找到達標路徑，依活動策略開技後重讀",_live.wave + 1]];
    return YES;
}
