// One explicit match intent, fresh native gesture leases and skill receipts.
// Ordinary solo opening, formation and recovery rules never enter this path.
- (void)rememberEightBlock:(NSDictionary *)decision {
    _eightLastBlocked = @{@"wave":@(_live.wave),@"turn":@(_autoTurn),@"decision":decision ?: @{},
        @"board":BoardTrace(_live.board),@"team":EightTeamTrace(_live),@"multiplayer":MultiplayerTrace(_live),
        @"enemyCombo":@{@"valid":@(_live.enemyCombo.valid),@"turns":@(_live.enemyCombo.turns[0]),
            @"threshold":@(_live.enemyCombo.thresholds[0])}};
    if (!_eightFirstBlocked) _eightFirstBlocked = _eightLastBlocked;
}
- (void)rememberEightSkill:(const PHOrbSnapshot *)state {
    if (state->eight.valid) _eightLastObserved = *state;
    if (_eightSkillPending && _eightSkillStarted && PHOrbEightSkillAcknowledged(&_eightSkillBefore,state,unsigned(_eightSlot))) {
        if (!_eightSkillAcknowledged) { _eightSkillAcknowledged = true; _eightSkillAfter = *state; }
        if (!_eightSkillSettled && PHOrbCanPreview(state) && !state->automation.busy) {
            _eightSkillSettled = true; _eightSkillAfter = *state;
        }
    }
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
        @"sequenceStep":@(_eightSequenceStep),@"sequenceCount":@(_eightSequence.count),@"recoveryCursor":@(_eightSequenceCursor),
        @"attemptedSlots":@(_eightAttemptedSlots),@"boardSubmitted":@(_eightBoardSubmitted),
        @"guideEnabled":@(PHOrbEightGuideEnabled(_activeProfile[@"activity"])),@"guidePrepared":@(_eightGuidePrepared),
        @"guideSequence":@(_eightSequenceGuide),@"floorSkillMask":@(_live.wave < _eightFloorSkillMask.size() ? _eightFloorSkillMask[_live.wave] : 0),
        @"firstPuzzle":_eightFirstPuzzle ?: @{},
        @"firstBlocked":_eightFirstBlocked ?: @{},@"lastBlocked":_eightLastBlocked ?: @{},@"preparation":_eightPreparationTrace ?: @{},
        @"preparationUsed":@(_eightPreparationUsed),@"preparationAwaiting":@(_eightPreparationAwaiting),
        @"lastStop":_eightLastStop ?: @{}};
}

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
        _eightSkillAcknowledged = _eightSkillSettled = false; _eightSkillBefore = _eightSkillAfter = _eightLastObserved = {};
        _eightDiscardSequence = _eightPausedResolve = false;
        _eightSequence = nil; _eightSequenceCursor = _eightSequenceStep = 0; _eightAttemptedSlots = 0;
        _eightGuidePrepared = _eightSequenceGuide = false; _eightFloorSkillMask = {}; _eightFloorSubmitted = {};
        _eightFirstPuzzle = nil;
        _eightFirstBlocked = _eightLastBlocked = _eightPreparation = _eightPreparationTrace = nil;
        _eightPreparing = _eightPreparationUsed = _eightPreparationAwaiting = false;
        _eightPreparationSearchMS = 0;
        _eightResumes = 0; _eightLastStop = nil;
        _autoTurn = 1; _autoMoves = _autoSkills = 0;
        [_automationLog removeAllObjects];
    } else if (_eightSkillPending && !_eightSkillStarted) {
        [self queueStop:@"本場上次技能啟動結果不明，不會重複施放；請手動完成本場"]; return;
    }
    const bool sameProfile = [_eightHistoryProfile isEqual:selected ?: @{}];
    _eightPreparing = false; // A resumed preparation always needs a new board check and route.
    // Keep a pending skill's original cursor until its receipt is consumed.
    // A new profile/board can discard the remainder only after that accounting.
    if (![_engine armEightPlayerAuto:state]) {
        [self queueStop:@"8 人本場操作身分未確認，未啟動 Auto"]; return;
    }
    _eightResuming = sameMatch; _eightDiscardSequence |= sameMatch && !sameProfile;
    _eightHistoryProfile = selected ?: @{};
    if (sameMatch) ++_eightResumes;
    [self cancelWorker]; [self clearRoute];
    _automatic = _eightAutomatic = _autoBound = _monitoring = true;
    _eightTeamBound = _hasStable = _readQueued = false;
    _autoOrigin = *state; _autoStartAt = _autoPhaseAt = _now;
    _activeProfile = selected ?: @{};
    _skillWindowWaitAt = 0; _goalPlan = @{}; _assistTrace = @{}; _assistAwaitingResult = false;
    if (selected) [self loadSettings:PHOrbCanonicalSettings(selected[@"settings"])];
    _watchPlayerState = false;
    _phase = _eightSkillPending ? @"eightSkillWaiting" : _eightBoardSubmitted ? @"turnWaiting" : @"autoWaiting";
    _message = sameMatch ? @"8 人同場接續中；保留已確認進度並重讀目前盤面" : @"8 人本場 Auto 準備中；點停止可立即中斷";
    [self logAutomation:sameMatch ? @"8 人同場接續：保留回合、開技與停止紀錄，重新讀盤" :
        selected ? [NSString stringWithFormat:@"8 人活動：%@",selected[@"name"]] : @"8 人通用盤面 Auto；使用目前盤面目標"];
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
    [self rememberEightSkill:state];
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
        _eightResuming = _eightDiscardSequence = _eightPausedResolve = false;
        _eightHistory = _live;
        _eightSequence = nil; _eightSequenceCursor = _eightSequenceStep = 0; _eightAttemptedSlots = 0;
        _eightGuidePrepared = _eightSequenceGuide = false;
        _eightPreparing = false;
        [self waitForAutomationBoard]; return;
    }
    if (_eightSkillPending) {
        if (!_eightSkillStarted || !_eightSkillAcknowledged) {
            [self queueStop:@"8 人技能生效紀錄未確認，保留進度避免重複施放"]; return;
        }
        _eightSkillPending = false;
        if (_eightSequenceStep < _eightSequence.count) ++_eightSequenceStep;
        // A verified effect changes the inventory used to skip earlier rescue
        // branches (for example Ney -> Kagutsuchi). Revisit those branches
        // while preserving both the ordered sequence and per-turn attempt mask.
        _eightSequenceCursor = 0;
        ++_autoSkills;
        if (_eightSkillBefore.wave < _eightFloorSkillMask.size())
            _eightFloorSkillMask[_eightSkillBefore.wave] |= uint8_t(1u << _eightSlot);
        [self logAutomation:[NSString stringWithFormat:@"B%u：位置 %lu 技能已確認完成",_eightSkillBefore.wave + 1,(unsigned long)_eightSlot + 1]];
        if (!_eightPausedResolve && _live.wave == _eightSkillBefore.wave && _live.multiplayer.turnSerial == _eightSkillBefore.multiplayer.turnSerial &&
            _eightSkillSettled && PHOrbSamePlanBoard(&_eightSkillAfter.board,&_live.board) &&
            PHOrbEightSkillAcknowledged(&_eightSkillBefore,&_live,unsigned(_eightSlot))) _eightHistory = _live;
        else _eightResuming = _eightDiscardSequence = true;
        [self waitForAutomationBoard]; return;
    }
    if (_eightResuming) {
        const bool newTurn = _eightPausedResolve || _eightHistory.wave != _live.wave ||
            _eightHistory.multiplayer.turnSerial != _live.multiplayer.turnSerial;
        if (newTurn || _eightDiscardSequence || !PHOrbSamePlanBoard(&_eightHistory.board,&_live.board) ||
            _eightHistory.automation.skillUses != _live.automation.skillUses) {
            _eightSequence = nil; _eightSequenceCursor = _eightSequenceStep = 0;
            _eightGuidePrepared = _eightSequenceGuide = false;
            if (newTurn) {
                ++_autoTurn; _eightAttemptedSlots = 0;
                if (_eightHistory.wave < _eightFloorSubmitted.size()) _eightFloorSubmitted[_eightHistory.wave] = true;
            }
            [self logAutomation:@"接續時盤面、回合或策略已改變，依目前盤面重新規劃"];
        }
        _eightResuming = _eightDiscardSequence = _eightPausedResolve = false;
    }
    _eightHistory = _live;
    if (_eightPreparationAwaiting) {
        unsigned minimum = 0;
        if (!PHOrbEnemyComboRequirement(&_live.enemyCombo,&minimum) || minimum) {
            [self queueStop:@"準備回合後吸收尚未確認消失，已停止；本場不再重複準備"]; return;
        }
        _eightPreparationAwaiting = _eightPreparing = false;
        _eightSequence = nil; _eightSequenceStep = 0; _eightGuidePrepared = true;
        NSMutableDictionary *trace = [_eightPreparationTrace mutableCopy];
        trace[@"status"] = @"next-turn-confirmed"; trace[@"nextBoard"] = BoardTrace(_live.board);
        trace[@"nextTeam"] = EightTeamTrace(_live); _eightPreparationTrace = [trace copy];
        [self logAutomation:@"準備回合已確認，吸收已消失；依現盤與實際 CD 選擇轉色輸出"];
        if (PHOrbEightGuideEnabled(_activeProfile[@"activity"])) {
            if (_live.wave != [_eightPreparationTrace[@"wave"] unsignedIntValue] ||
                _live.wave >= [_activeProfile[@"activity"][@"floors"] count] || ![_eightPreparation[@"sequence"] count]) {
                [self queueStop:@"準備回合後樓層或攻略已改變，請依目前盤面重新開始"]; return;
            }
            NSMutableDictionary *trial = [_activeProfile[@"activity"] mutableCopy];
            NSMutableArray *floors = [trial[@"floors"] mutableCopy];
            NSMutableDictionary *floor = [floors[_live.wave] mutableCopy];
            floor[@"sequences"] = @[_eightPreparation[@"sequence"]]; floors[_live.wave] = floor; trial[@"floors"] = floors;
            NSUInteger cursor = 0;
            _eightSequence = PHOrbEightSkillSequence(trial,&_live,_eightAttemptedSlots,&cursor);
            _eightSequenceCursor = 0;
            _eightSequenceGuide = false;
            if (!_eightSequence.count) {
                [self queueStop:@"準備回合後轉色技能或珠數未達條件，已停止；本場不再重複準備"]; return;
            }
        }
    }
    if (!_eightGuidePrepared && PHOrbEightGuideEnabled(_activeProfile[@"activity"])) {
        _eightGuidePrepared = true;
        if (_live.wave >= _eightFloorSubmitted.size()) { [self queueStop:@"已超出攻略樓層上限"]; return; }
        if (!_eightFloorSubmitted[_live.wave]) {
            NSDictionary *activity = _activeProfile[@"activity"];
            NSDictionary *preparation = PHOrbEightPreparationDecision(activity,&_live,_eightAttemptedSlots);
            if (!_eightPreparationUsed && [activity[@"guide"][@"preparation"][@"preferBeforeOutput"] boolValue] &&
                [preparation[@"available"] boolValue] && [preparation[@"preferable"] boolValue]) {
                const unsigned budget = std::min(OptionsForQuality(_quality).milliseconds,
                    [activity[@"floors"][_live.wave][@"searchMS"] unsignedIntValue]);
                _eightPreparationSearchMS = std::min(150u,budget / 3);
                [self beginEightPreparation:preparation priority:YES]; return;
            }
            NSDictionary *decision = PHOrbEightGuideDecision(_activeProfile[@"activity"],&_live,
                _eightFloorSkillMask[_live.wave],_eightAttemptedSlots);
            if (![decision[@"available"] boolValue]) {
                [self rememberEightBlock:decision];
                if (!_eightPreparationUsed && !_eightFloorSubmitted[_live.wave] && [preparation[@"available"] boolValue]) {
                    // Before a guide delay, still try the unchanged board and
                    // all currently ready recovery skills with normal goals.
                    _eightSequence = nil; _eightSequenceStep = 0;
                    [self beginSolve]; return;
                }
                _eightGuidePrepared = false; [self queueStop:decision[@"message"]]; return;
            }
            _eightSequence = decision[@"sequence"]; _eightSequenceStep = 0; _eightSequenceGuide = _eightSequence.count > 0;
            if (_eightSequenceGuide) [self logAutomation:[NSString stringWithFormat:
                @"B%u：攻略開技 %@，完成後再求路",_live.wave + 1,[_eightSequence componentsJoinedByString:@"→"]]];
        }
    }
    if (_eightSequenceStep < _eightSequence.count) {
        const NSUInteger slot = [_eightSequence[_eightSequenceStep] unsignedIntegerValue] - 1;
        const auto &m = _live.automation.members[slot];
        if (_live.automation.skillUses >= 255) {
            [self queueStop:@"8 人技能總計數已達上限，無法確認新的施放，已停止"]; return;
        }
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
        if (_eightSequenceGuide) _eightGuidePrepared = false;
        [self logAutomation:reason ?: @"遊戲判定此技能不可用，重讀盤面後重新求路"];
        [self waitForAutomationBoard]; return;
    }
    _eightSkillPending = true; _eightSkillStarted = result == PHOrbSkillStarted;
    _eightSkillAcknowledged = _eightSkillSettled = false; _eightSkillAfter = {};
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
        options->activity.linkRewardLimit = [floor[@"linkRewardLimit"] unsignedIntValue];
        if (floor[@"attack"]) {
            NSDictionary *attack = floor[@"attack"];
            for (unsigned c = 0; c < 6; ++c) {
                options->activity.attackWeights[c] = [attack[@"weights"][c] unsignedIntValue];
                options->activity.attackRowPercent[c] = [attack[@"rowPercent"][c] unsignedIntValue];
            }
            options->activity.attackLinkMultiplier = [attack[@"linkMultiplier"] unsignedIntValue];
            options->activity.attackLinkComboBonus = [attack[@"linkComboBonus"] unsignedIntValue];
            if (state->automation.awakeningRestricted) {
                for (unsigned c = 0; c < 6; ++c) options->activity.attackRowPercent[c] = 0;
                options->activity.attackLinkMultiplier = 1;
            }
        }
        // Older cup profiles without the new key already describe B1 as
        // combo-only (zero row/link weights). Preserve that intent on upgrade.
        options->activity.comboPriority = floor[@"comboPriority"] ? [floor[@"comboPriority"] boolValue] :
            state->wave == 0 && options->activity.comboWeight > 0 && !options->activity.rowWeight && !options->activity.linkWeight;
        if (_eightPreparing) {
            options->steps = MIN(options->steps,12u); options->milliseconds = MIN(options->milliseconds,_eightPreparationSearchMS);
            options->activity.preparation = true; options->activity.comboPriority = false;
            options->activity.comboWeight = options->activity.rowWeight = options->activity.linkWeight = 0;
            options->activity.primaryColor = [_eightPreparation[@"primaryColor"] unsignedIntValue];
            options->activity.primaryMinimum = [_eightPreparation[@"primaryMinimum"] unsignedIntValue];
            for (unsigned c = 0; c < 6; ++c) {
                options->activity.conversion[c] = [_eightPreparation[@"conversion"][c] unsignedIntValue];
                options->activity.attackWeights[c] = (options->activity.colors & (1u << c)) ?
                    (c == options->activity.primaryColor ? 300 : 100) : 0;
            }
            PHOrbGoalSet goals{}; goals.count = 1; goals.items[0] = {PHOrbGoalCombo,0,1,0};
            return @{@"available":@YES,@"goals":PHOrbGoalsJSON(&goals),@"enemyComboFirstWave":@YES,
                @"leaderAddedCombos":@0,@"message":_eightPreparation[@"message"],@"warning":@"本杯攻略的一回合準備分支；不是傷害或存活預測。",@"leaders":@[]};
        }
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

- (void)beginEightPreparation:(NSDictionary *)preparation priority:(BOOL)priority {
    _eightPreparation = preparation; _eightPreparing = true;
    _eightSequence = nil; _eightSequenceStep = 0; _eightSequenceGuide = false;
    _eightPreparationTrace = @{@"status":@"planning",@"turn":@(_autoTurn),@"wave":@(_live.wave),
        @"priority":@(priority),@"plan":preparation,@"board":BoardTrace(_live.board),@"team":EightTeamTrace(_live)};
    [self logAutomation:priority ? @"B6：先留珠消一組三珠，等下一回合雙轉色技就緒再輸出" : preparation[@"message"]];
    [self beginSolve];
}

- (BOOL)recoverEightSolve {
    if (!_eightAutomatic || !_activeProfile[@"activity"] || !PHOrbSameAutomation(&_preview,&_live) ||
        !PHOrbSameEnemyCombo(&_preview.enemyCombo,&_live.enemyCombo) || _eightPreparing) return NO;
    NSArray *sequence = PHOrbEightSkillSequence(_activeProfile[@"activity"],&_live,_eightAttemptedSlots,&_eightSequenceCursor);
    if (!sequence.count) {
        if (_eightPreparationUsed || !_eightPreparationSearchMS || _live.wave >= _eightFloorSubmitted.size() || _eightFloorSubmitted[_live.wave]) return NO;
        NSDictionary *preparation = PHOrbEightPreparationDecision(_activeProfile[@"activity"],&_live,_eightAttemptedSlots);
        if (![preparation[@"available"] boolValue]) return NO;
        [self rememberEightBlock:@{@"message":@"現盤與當前技能未取得輸出路徑",@"search":_searchTrace ?: @{}}];
        [self beginEightPreparation:preparation priority:NO]; return YES;
    }
    // Only one search budget per observed board; a new search follows an
    // actual skill effect, with a bounded six-slot attempt mask per turn.
    _eightSequence = sequence; _eightSequenceStep = 0;
    _eightSequenceGuide = false;
    [self clearRoute]; [self waitForAutomationBoard];
    [self logAutomation:[NSString stringWithFormat:@"B%u：本次未找到達標路徑，依活動策略開技後重讀",_live.wave + 1]];
    return YES;
}
