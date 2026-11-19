// Explicit single-player fixed-team execution. A stopped run with any issued
// action is not replayed in the same battle; ambiguous receipts remain consumed.
- (BOOL)bindFixedPlan:(const PHOrbSnapshot *)state {
    NSDictionary *plan=_activeProfile[@"fixedPlan"];
    if (_fixedConsumed && PHBattleSameIdentity(&_fixedOrigin.identity,&state->identity)) {
        [self queueStop:@"本場固定計畫已有執行紀錄；停止後不重播技能或猜測回合，請手動完成本場"]; return NO;
    }
    if (state->wave || state->moves || state->phase!=2 || state->automation.skillUses ||
        !PHOrbFixedPlanMatchesTeamOrder(plan,state,NO) ||
        ![_engine respondsToSelector:@selector(readFixedPlan:frame:reason:)] ||
        ![_engine respondsToSelector:@selector(fixedSkillReadiness:request:expected:frame:reason:)] ||
        ![_engine respondsToSelector:@selector(stageFixedPlanTarget:expected:frame:reason:)]) {
        [self queueStop:@"固定計畫只可在 B1 未操作的相符原生隊伍開始；所需原生功能必須可用"]; return NO;
    }
    for (unsigned i=0;i<6;++i) if (state->automation.members[i].number != [plan[@"members"][i][@"body"] unsignedIntValue]) {
        [self queueStop:@"固定計畫開場形態不符，未開始"]; return NO;
    }
    for (unsigned i=0;i<6;++i) if (plan[@"members"][i][@"assist"] != NSNull.null &&
        [plan[@"members"][i][@"assist"] unsignedIntValue] &&
        !PHBattleAssistIsRetained(state->automation.members[i].assistState)) {
        [self queueStop:@"固定計畫開場所需武裝已消耗，未重播"]; return NO;
    }
    _fixedProfile=_activeProfile; _fixedOrigin=_fixedTeam=*state; _fixedConsumed=false;
    _fixedWave=state->wave; _fixedStageIndex=_fixedAction=_fixedPuzzles=_fixedLoopCursor=_fixedOnce=0;
    _fixedPuzzleSkills=0;
    _fixedStage=nil; _fixedVariant=nil; _fixedPending=nil; _fixedLastStop=nil; _fixedLoops=[NSMutableArray array];
    _unmatchableTrace=@{};
    _fixedPartsGone=_fixedAcknowledged=_fixedStall=false;
    return YES;
}
- (BOOL)fixedTeamMatches:(const PHOrbSnapshot *)state {
    for (unsigned i=0;i<6;++i) {
        const auto &a=_fixedTeam.automation.members[i], &b=state->automation.members[i];
        if (!b.active || !b.temporaryKnown || b.temporaryTurns || b.number!=a.number ||
            b.sourceCatalogIDs[0]!=a.sourceCatalogIDs[0] || b.sourceCatalogIDs[1]!=a.sourceCatalogIDs[1] ||
            PHBattleAssistIsRetained(b.assistState)!=PHBattleAssistIsRetained(a.assistState)) return NO;
    }
    return YES;
}
- (BOOL)observeFixedPlan:(PHOrbSnapshot *)state {
    if (_phase==PHOrbSessionPhase::FixedSkillWaiting &&
        state->automation.skillUses==_fixedBefore.automation.skillUses+1) _fixedAcknowledged=true;
    if (_fixedVariant && [_fixedVariant isEqual:@"parts"] && state->wave==_fixedWave && state->automation.enemiesKnown) {
        bool known=true;
        for (unsigned i=0;i<8;++i) if (_fixedOrigin.automation.partsMask&(1u<<i)) {
            const auto &p=state->automation.parts[i];
            known &= p.known && (p.mode<0 || !p.hp);
        }
        _fixedPartsGone=known;
    }
    if (state->wave>_fixedWave+1 || state->wave<_fixedWave) {
        [self queueStop:@"固定計畫樓層不連續，未猜測進度"]; return NO;
    }
    if (_phase!=PHOrbSessionPhase::FixedSkillWaiting && _phase!=PHOrbSessionPhase::TurnWaiting &&
        _phase!=PHOrbSessionPhase::Releasing &&
        state->wave==_fixedWave && state->automation.skillUses!=_fixedTeam.automation.skillUses) {
        [self queueStop:@"固定計畫以外的原生技能次數已改變，未猜測進度"]; return NO;
    }
    if ((_phase==PHOrbSessionPhase::Solving || _phase==PHOrbSessionPhase::Preview || [self executing]) &&
        ![self fixedTeamMatches:state]) {
        [self queueStop:@"固定計畫轉珠期間的隊員／武裝身分已改變"]; return NO;
    }
    // Revoke a retry if coverage changes after planning, including while a
    // gesture is held. Once release has completed, effects may legitimately
    // expire as the game resolves the turn; that is not an input failure.
    if ((_phase==PHOrbSessionPhase::Solving || _phase==PHOrbSessionPhase::Preview ||
        _phase==PHOrbSessionPhase::Arming || _phase==PHOrbSessionPhase::Running) &&
        ![self fixedBoardConditionsAllowed:state]) return NO;
    if (_preview.fixedPlan.valid && (_phase==PHOrbSessionPhase::Solving || _phase==PHOrbSessionPhase::Preview ||
        _phase==PHOrbSessionPhase::Arming || _phase==PHOrbSessionPhase::Running) && !_staged) {
        NSString *reason=nil;
        if (![_engine readFixedPlan:state frame:_frame reason:&reason] ||
            !PHOrbSameFixedPlan(&_preview.fixedPlan,&state->fixedPlan)) {
            [self queueStop:reason ?: @"固定計畫求路後的敵人 HP／目標已改變"]; return NO;
        }
        if (![self fixedEnemyStateAllowed:state]) return NO;
    }
    return YES;
}
- (BOOL)fixedEnemyStateAllowed:(const PHOrbSnapshot *)state {
    if (!_fixedStage[@"requireEnemyState"]) return YES;
    PHOrbFixedPlanEvidence e=[self fixedEvidence:state];
    if (state->wave!=_fixedWave || PHOrbFixedPlanEnemyState(_fixedStage[@"requireEnemyState"],&e)!=PHOrbFixedPlanYes) {
        [self queueStop:@"固定計畫所需敵人身分／存活／HP 狀態未確認，未繼續行動"]; return NO;
    }
    return YES;
}
- (BOOL)fixedBoardConditionsAllowed:(const PHOrbSnapshot *)state {
    if (![self fixedRetryAttackAllowed:state]) return NO;
    // Run after the prescribed skills have acknowledged, so a real cleanse can
    // still repair the board. Only the explicit B2 policy may predict a latent
    // recovery; it separately captures providers before solving and input.
    NSString *policy=_fixedStall ? @"six_colors_no_fours" : _fixedStage[@"board"][@"policy"];
    const unsigned blocked=state->board.unmatchableColors & 63u;
    const bool recovery=blocked && [self unmatchableBoardSupported:state];
    if ([policy hasPrefix:@"six_colors"] && (!state->unmatchableKnown || (blocked && !recovery))) {
        NSArray *colorNames=@[@"火",@"水",@"木",@"光",@"闇",@"心"];
        NSMutableArray *names=[NSMutableArray array];
        for (unsigned c=0;c<6;++c) if (blocked&(1u<<c)) [names addObject:colorNames[c]];
        NSString *colors=[names componentsJoinedByString:@"／"];
        NSString *detail=state->unmatchableKnown ? [NSString stringWithFormat:
            @"%@目前不可消，與首消六色要求衝突。轉轉取色無法解除不可消；此步不符合已支援的四連潛覺回復條件，已停止，請手動處理本場",colors] :
            @"不可消狀態尚未確認，無法檢查首消六色要求，已停止固定計畫";
        NSString *message=[NSString stringWithFormat:@"固定計畫 B%lu · %@：%@",
            (unsigned long)_fixedWave+1,_fixedStage[@"id"],detail];
        NSMutableDictionary *stop=[FixedEffectTrace(*state,_fixedStage[@"id"],@"fixed-board-preconditions") mutableCopy];
        [stop addEntriesFromDictionary:@{@"reason":message,
            @"code":state->unmatchableKnown ? @"required-colors-unmatchable" : @"unmatchable-state-unknown",
            @"profile":_fixedProfile[@"id"] ?: @"",@"policy":policy,@"requiredFirstColors":@63,
            @"unmatchableKnown":@(state->unmatchableKnown),
            @"unmatchableColors":state->unmatchableKnown ? @(state->board.unmatchableColors) : (id)NSNull.null,
            @"unmatchableTurns":state->unmatchableKnown ? @(state->unmatchableTurns) : (id)NSNull.null,
            @"blockedColors":state->unmatchableKnown ? @(blocked) : (id)NSNull.null,
            @"blockedColorNames":state->unmatchableKnown ? colors : (id)NSNull.null,
            @"latentUnmatchableRecoverySupported":@(recovery),@"recoveryRequested":@([self fixedUnmatchableRecoveryEnabled]),@"action":@(_fixedAction),
            @"confirmedPuzzles":@(_fixedPuzzles),@"pending":_fixedPending ?: @{},
            @"sessionPhase":SessionPhaseText(_phase),@"autoTurn":@(_auto.turn),@"autoSkills":@(_auto.skills),
            @"consumed":@(_fixedConsumed),@"continuationAllowed":@NO,@"survivalKnown":@NO,
            @"capturedUptimeMS":@(uint64_t(NSProcessInfo.processInfo.systemUptime*1000))}];
        _fixedLastStop=[stop copy];
        [self queueStop:message]; return NO;
    }
    PHOrbFixedPlanEvidence e=[self fixedEvidence:state];
    // Effects are independently captured native fields; they remain available
    // during a staged input frame when another fixed-plan read is forbidden.
    e.enemiesKnown=EffectMasksKnown(*state);
    for (NSString *effect in _fixedStage[@"requireEffectsClear"]) {
        if (PHOrbFixedPlanActionCondition(@{@"kind":@"skill",@"when":effect},&e)!=PHOrbFixedPlanNo) {
            [self queueStop:[NSString stringWithFormat:@"固定計畫轉珠前所需效果 %@ 尚未解除／覆蓋或讀取未知",effect]]; return NO;
        }
    }
    return YES;
}
- (BOOL)fixedRetryAttackAllowed:(const PHOrbSnapshot *)state {
    if (!_fixedPuzzles || ![_fixedStage[@"retryRequiresBypass"] boolValue]) return YES;
    const auto &e=state->automation;
    if (!e.valid || !e.enemiesKnown || !e.statusKnown) {
        [self queueStop:@"固定計畫補盤的吸收／貫通效果未知，未繼續攻擊"]; return NO;
    }
    if (e.enemyEffects & ~e.playerBypass & (PHOrbEnemyDamageAbsorb|PHOrbEnemyAttributeAbsorb|PHOrbEnemyDamageVoid)) {
        [self queueStop:@"固定計畫補盤仍有未覆蓋的傷害吸收／屬性吸收／傷害無效，未繼續攻擊"]; return NO;
    }
    return YES;
}
- (PHOrbFixedPlanEvidence)fixedEvidence:(const PHOrbSnapshot *)state {
    PHOrbFixedPlanEvidence e{}; e.wave=state->wave;
    e.enemiesKnown=state->fixedPlan.valid && state->automation.enemiesKnown;
    e.partsKnown=state->fixedPlan.valid; e.partsMask=state->fixedPlan.partsMask;
    e.enemyEffects=state->automation.enemyEffects; e.playerBypass=state->automation.playerBypass;
    e.playerEffects=state->automation.playerEffects; e.statusKnown=state->automation.statusKnown;
    e.assistVoidKnown=state->assistRecovery.valid; e.assistVoidTurns=state->assistRecovery.turns;
    e.unmatchableKnown=state->unmatchableKnown; e.unmatchableTurns=state->unmatchableTurns;
    e.spinnerKnown=state->valid; e.spinnerTurns=state->spinnerTurns;
    e.enemyCount=state->fixedPlan.count;
    for (unsigned i=0;i<std::min(unsigned(state->fixedPlan.count),8u);++i) {
        const auto &n=state->fixedPlan.enemies[i];
        e.enemyIDs[i]=n.catalogID;
        if (n.identityKnown) e.identityKnownMask|=1u<<i;
        if (n.flags&0x100u) continue; // Boss thresholds refer to the body, never its parts.
        e.bodyMask|=1u<<i;
        if (n.hp) e.aliveMask|=1u<<i;
        if (n.hpKnown) e.hpKnownMask|=1u<<i;
        e.enemyHP[i]=n.hp; e.enemyMaximumHP[i]=n.maximumHP;
    }
    return e;
}
- (void)armFixedAction:(NSDictionary *)action loop:(BOOL)loop {
    _fixedPending=action; _fixedPendingLoop=loop; _fixedBefore=_live;
    _phase=PHOrbSessionPhase::FixedActionArming;
    _message=[NSString stringWithFormat:@"固定計畫 B%lu · %@ · %@",(unsigned long)_fixedWave+1,_fixedStage[@"id"],action[@"kind"]];
}
- (void)finishFixedAction {
    if (_fixedPendingLoop) ++_fixedLoopCursor;
    else { if ([_fixedPending[@"frequency"] isEqual:@"once"]) _fixedOnce|=1u<<_fixedAction; ++_fixedAction; }
    _fixedPending=nil; [self waitForAutomationBoard];
}
- (NSString *)fixedLimitStop:(const PHOrbFixedPlanEvidence *)evidence {
    // Called only after this tick's native fixed-plan read and an unsatisfied
    // completion predicate. Freeze the evidence before later idle reads can
    // change it; diagnostic observations cannot authorize an extra turn.
    NSDictionary *repeat=_fixedStage[@"repeat"], *until=repeat[@"until"];
    NSString *kind=until[@"kind"];
    const NSUInteger maximum=[repeat[@"maximumPuzzles"] unsignedIntegerValue];
    NSMutableArray *enemies=[NSMutableArray array], *hpDescriptions=[NSMutableArray array];
    unsigned alive=0; bool bodyHPKnown=evidence->enemiesKnown;
    for (unsigned i=0;i<std::min(unsigned(_live.fixedPlan.count),8u);++i) {
        const auto &enemy=_live.fixedPlan.enemies[i];
        if (enemy.flags&0x100u) continue;
        const bool hpKnown=evidence->enemiesKnown && enemy.hpKnown;
        const bool ratioKnown=hpKnown && enemy.maximumHP && enemy.hp<=enemy.maximumHP;
        bodyHPKnown &= hpKnown;
        if (hpKnown && enemy.hp) ++alive;
        NSMutableDictionary *entry=[@{@"slot":@(i+1),@"identityKnown":@(enemy.identityKnown),
            @"catalogID":enemy.identityKnown ? @(enemy.catalogID) : (id)NSNull.null,
            @"hpKnown":@(hpKnown),@"hp":hpKnown ? @(enemy.hp) : (id)NSNull.null,
            @"maximumHP":hpKnown ? @(enemy.maximumHP) : (id)NSNull.null,
            @"alive":hpKnown ? @(enemy.hp>0) : (id)NSNull.null,@"ratioKnown":@(ratioKnown)} mutableCopy];
        if (ratioKnown) {
            const double percent=100.0*double(enemy.hp)/double(enemy.maximumHP);
            entry[@"remainingPercent"]=@(percent);
            if (enemy.hp) [hpDescriptions addObject:[NSString stringWithFormat:@"敵人 %u HP %llu／%llu（%.2f%%）",
                i+1,(unsigned long long)enemy.hp,(unsigned long long)enemy.maximumHP,percent]];
        } else if (hpKnown && enemy.hp) [hpDescriptions addObject:[NSString stringWithFormat:@"敵人 %u HP %llu（比例未知）",
            i+1,(unsigned long long)enemy.hp]];
        [enemies addObject:[entry copy]];
    }
    if (!enemies.count) bodyHPKnown=false;
    NSString *condition=[kind isEqual:@"floor_change"] ?
        (bodyHPKnown && alive ? @"敵人仍存活、尚未換層" : @"尚未確認換層") :
        [kind isEqual:@"enemy_hp_at_most"] ? @"敵人 HP 尚未達到指定門檻" :
        [kind isEqual:@"skill_ready"] ? @"指定技能尚未就緒" :
        [kind isEqual:@"parts_absent"] ? @"尚未確認部位全數消失" : @"步驟完成條件尚未達成";
    const bool effectsKnown=EffectMasksKnown(_live) && evidence->enemiesKnown;
    const unsigned uncovered=effectsKnown ? evidence->enemyEffects & ~evidence->playerBypass & 15u : 0;
    NSMutableArray *blockers=[NSMutableArray array];
    if (uncovered&PHOrbEnemyDamageAbsorb) [blockers addObject:@"傷害吸收"];
    if (uncovered&PHOrbEnemyAttributeAbsorb) [blockers addObject:@"屬性吸收"];
    if (uncovered&PHOrbEnemyDamageVoid) [blockers addObject:@"傷害無效"];
    if (uncovered&PHOrbEnemyComboAbsorb) [blockers addObject:@"Combo 吸收"];
    NSMutableString *message=[NSMutableString stringWithFormat:@"固定計畫 B%lu · %@ 已轉 %lu／%lu 盤，%@",
        (unsigned long)_fixedWave+1,_fixedStage[@"id"],(unsigned long)_fixedPuzzles,(unsigned long)maximum,condition];
    if (hpDescriptions.count) [message appendFormat:@"；%@",[hpDescriptions componentsJoinedByString:@"；"]];
    if (blockers.count) [message appendFormat:@"；目前仍有未覆蓋的%@",[blockers componentsJoinedByString:@"／"]];
    if (!effectsKnown) [message appendString:@"；目前吸收／無效對策狀態未知"];
    [message appendString:@"。已達此步轉珠上限，停止固定計畫"];
    _fixedLastStop=@{@"reason":message,@"code":@"stage-puzzle-limit",@"scope":@"fresh-native-stage-cap",
        @"profile":_fixedProfile[@"id"] ?: @"",@"floor":@(_fixedWave+1),@"stage":_fixedStage[@"id"] ?: @"",
        @"confirmedPuzzles":@(_fixedPuzzles),@"maximumPuzzles":@(maximum),@"until":until,
        @"completionSatisfied":@NO,@"continuationAllowed":@NO,@"survivalKnown":@NO,
        @"enemiesKnown":@(evidence->enemiesKnown),@"bodyHPKnown":@(bodyHPKnown),
        @"livingBodies":bodyHPKnown ? @(alive) : (id)NSNull.null,@"enemies":[enemies copy],
        @"effectsKnown":@(effectsKnown),@"enemyEffects":effectsKnown ? @(evidence->enemyEffects) : (id)NSNull.null,
        @"playerBypass":effectsKnown ? @(evidence->playerBypass) : (id)NSNull.null,
        @"uncoveredEffects":effectsKnown ? @(uncovered) : (id)NSNull.null,
        @"uncoveredEffectNames":[blockers copy],@"readinessKnownSlots":@(evidence->readinessKnownSlots),
        @"readySlots":@(evidence->readySlots),@"capturedUptimeMS":@(uint64_t(NSProcessInfo.processInfo.systemUptime*1000))};
    return message;
}
- (void)recordFixedStop {
    // A specific stage-cap or board-precondition stop may already have frozen
    // richer native evidence for this message.
    NSString *known=_fixedLastStop[@"reason"];
    if (known.length && [_message hasPrefix:known]) return;
    _fixedLastStop=@{@"reason":_message ?: @"",@"code":@"stopped",@"scope":@"session-stop",
        @"profile":_fixedProfile[@"id"] ?: @"",@"floor":@(_fixedWave+1),@"stage":_fixedStage[@"id"] ?: @"",
        @"action":@(_fixedAction),@"confirmedPuzzles":@(_fixedPuzzles),@"pending":_fixedPending ?: @{},
        @"sessionPhase":SessionPhaseText(_phase),@"autoTurn":@(_auto.turn),@"autoSkills":@(_auto.skills),
        @"consumed":@(_fixedConsumed),@"nativeKnown":@(_hasLive),
        @"nativePhase":_hasLive ? @(_live.phase) : (id)NSNull.null,@"wave":_hasLive ? @(_live.wave) : (id)NSNull.null,
        @"continuationAllowed":@NO,@"survivalKnown":@NO,
        @"capturedUptimeMS":@(uint64_t(NSProcessInfo.processInfo.systemUptime*1000))};
}
- (void)advanceFixedPlan {
    if (!_auto.active() || [self executing] || _phase==PHOrbSessionPhase::Solving || _phase==PHOrbSessionPhase::Preview ||
        _phase==PHOrbSessionPhase::FixedActionArming || _now-_auto.startedAt<0.35) return;
    const bool puzzle=_phase==PHOrbSessionPhase::TurnWaiting;
    const bool skill=_phase==PHOrbSessionPhase::FixedSkillWaiting;
    const bool target=_phase==PHOrbSessionPhase::FixedTargetWaiting;
    if (!puzzle && !skill && !target && _phase!=PHOrbSessionPhase::AutoWaiting) return;
    if (puzzle) _auto.sawResolve|=ResolvingOrbs(_live.phase);
    if (_now-_auto.phaseAt>(puzzle ? TurnTimeout : skill ? SkillTimeout : 12)) {
        [self queueStop:@"固定計畫等待原生回覆逾時；未重試或跳步"]; return;
    }
    if (_live.phase!=2 || !_live.settled || _live.boardTransition || _live.automation.busy ||
        _live.dragged || (puzzle && (!_auto.sawResolve || _live.moves))) { _hasStable=false; return; }
    if (!_hasStable || !PHOrbSameAutomation(&_stable,&_live)) { _stable=_live; _stableAt=_now; _hasStable=true; return; }
    if (_now-_stableAt<AutomationSettle) return;
    if (skill) {
        if (!_fixedAcknowledged || _live.wave!=_fixedBefore.wave ||
            _live.automation.skillUses!=_fixedBefore.automation.skillUses+1) {
            [self queueStop:@"固定計畫技能未取得唯一完成回覆，停止以免重播"]; return;
        }
        PHOrbSnapshot expected=_fixedTeam;
        const unsigned after=[_fixedPending[@"afterMonster"] unsignedIntValue];
        if (after) expected.automation.members[_fixedSlot].number=uint16_t(after);
        const unsigned swap=[_fixedProfile[@"fixedPlan"][@"leaderSwap"] unsignedIntValue];
        if (after && swap && _fixedSlot==swap-1) std::swap(expected.automation.members[0],expected.automation.members[_fixedSlot]);
        if ([_fixedPending[@"consumeAssist"] boolValue]) {
            if (_live.automation.members[_fixedSlot].assistCatalogID || PHBattleAssistIsRetained(_live.automation.members[_fixedSlot].assistState)) {
                [self queueStop:@"固定計畫武裝消耗未確認，未前進"]; return;
            }
            expected.automation.members[_fixedSlot].assistState=_live.automation.members[_fixedSlot].assistState;
        }
        _fixedTeam=expected;
        if (![self fixedTeamMatches:&_live]) { [self queueStop:@"固定計畫技能完成後的形態／換位／武裝與預期不符"]; return; }
        _fixedTeam=_live;
        if (!_fixedPendingLoop) _fixedPuzzleSkills|=1u<<_fixedAction;
        if ([_fixedPending[@"loop"] boolValue]) {
            NSMutableDictionary *loop=[_fixedPending mutableCopy]; loop[@"optional"]=@YES; loop[@"frequency"]=@"each";
            [loop removeObjectForKey:@"requireReady"];
            if (![_fixedLoops containsObject:loop]) [_fixedLoops addObject:[loop copy]];
        }
        ++_auto.skills; [self logAutomation:[NSString stringWithFormat:@"固定計畫 B%lu · %@：位置 %lu 技能 %@ 已確認",
            (unsigned long)_fixedWave+1,_fixedStage[@"id"],(unsigned long)_fixedSlot+1,_fixedPending[@"skill"]]];
        [self finishFixedAction]; return;
    }
    if (![self fixedTeamMatches:&_live]) { [self queueStop:@"固定計畫隊員來源／形態／武裝已改變"]; return; }
    NSString *reason=nil;
    if (![_engine readFixedPlan:&_live frame:_frame reason:&reason]) { [self queueStop:reason]; return; }
    if (target) {
        if (!_live.fixedPlan.targetKnown || _live.fixedPlan.target!=int(_fixedSlot)) {
            [self queueStop:@"固定計畫指定敵人未獲原生確認；未繼續攻擊"]; return;
        }
        [self finishFixedAction]; return;
    }
    if (puzzle) {
        if (![self confirmAssistRecoveryResult:&_live]) return;
        [self recordFixedTurnResult:&_live];
        if (![self confirmUnmatchableRecoveryResult:&_live]) return;
        _fixedTeam=_live;
        ++_auto.turn; ++_fixedPuzzles; _fixedAction=_fixedLoopCursor=_fixedPuzzleSkills=0;
        [self waitForAutomationBoard]; return;
    }
    if (_live.wave!=_fixedWave || !_fixedStage) {
        if (_fixedStage && ![@[@"floor_change",@"enemy_state_or_floor_change"] containsObject:_fixedStage[@"repeat"][@"until"][@"kind"]]) {
            [self queueStop:@"固定計畫在準備步驟提前換層，未省略餘下技能"]; return;
        }
        if (_fixedStage && [_fixedVariant isEqual:@"parts"] && !_fixedPartsGone) {
            [self queueStop:@"亂入樓層離開前未確認部位破壞，停止固定計畫"]; return;
        }
        NSDictionary *variant=PHOrbFixedPlanVariantForFloor(_fixedProfile[@"fixedPlan"],_live.wave,&_live);
        if (!variant) { [self queueStop:@"固定計畫樓層或亂入分支無法確認"]; return; }
        _fixedWave=_live.wave; _fixedVariant=variant[@"kind"]; _fixedStageIndex=0;
        _fixedStage=variant[@"stages"][0]; _fixedAction=_fixedPuzzles=_fixedOnce=_fixedLoopCursor=_fixedPuzzleSkills=0;
        _fixedPartsGone=false; _fixedOrigin.automation.partsMask=_live.automation.partsMask;
    }
    // Capture predicates after selecting the stage, including its first tick.
    BOOL assistFact=[_fixedStage[@"repeat"][@"until"][@"effects"] containsObject:@"assist_void"];
    assistFact |= [_fixedStage[@"requireEffectsClear"] containsObject:@"assist_void"];
    for (NSDictionary *a in _fixedStage[@"actions"]) assistFact |= [a[@"when"] isEqual:@"assist_void"];
    for (NSDictionary *a in _fixedLoops) assistFact |= [a[@"when"] isEqual:@"assist_void"];
    if (assistFact && ![self captureAssistRecovery:&_live]) return;
    PHOrbFixedPlanEvidence e=[self fixedEvidence:&_live];
    NSDictionary *until=_fixedStage[@"repeat"][@"until"];
    if ([until[@"kind"] isEqual:@"skill_ready"]) {
        unsigned slot=[until[@"slot"] unsignedIntValue]-1;
        if (_live.automation.members[slot].number!=[until[@"monster"] unsignedIntValue]) {
            [self queueStop:@"存技目標的形態不符"]; return;
        }
        PHOrbSkillResult r=[_engine fixedSkillReadiness:slot request:PHOrbSkillRequest{PHOrbSkillBody,[until[@"skill"] unsignedIntValue]}
            expected:&_live frame:_frame reason:&reason];
        if (r==PHOrbSkillWindowClosed) { [self deferSkillWindow:reason]; return; }
        if (r!=PHOrbSkillStarted && r!=PHOrbSkillUnavailable) { [self queueStop:reason ?: @"存技狀態未知"]; return; }
        e.readinessKnownSlots|=1u<<slot; if (r==PHOrbSkillStarted) e.readySlots|=1u<<slot;
    }
    PHOrbFixedPlanMatch done=PHOrbFixedPlanRepeatSatisfied(_fixedStage[@"repeat"],_fixedPuzzles,_fixedWave,&e);
    if (done==PHOrbFixedPlanUnknown) { [self queueStop:@"固定計畫步驟完成條件未知，未跳步"]; return; }
    if (done==PHOrbFixedPlanYes) {
        NSDictionary *next=PHOrbFixedPlanStage(_fixedProfile[@"fixedPlan"],_fixedWave,_fixedVariant,++_fixedStageIndex);
        if (!next) { [self queueStop:@"固定計畫缺少下一步，已停止"]; return; }
        _fixedStage=next; _fixedAction=_fixedPuzzles=_fixedOnce=_fixedLoopCursor=_fixedPuzzleSkills=0;
        [self waitForAutomationBoard]; return;
    }
    if (_fixedPuzzles>=[_fixedStage[@"repeat"][@"maximumPuzzles"] unsignedIntegerValue]) {
        [self queueStop:[self fixedLimitStop:&e]]; return;
    }
    if (![self fixedEnemyStateAllowed:&_live]) return;
    // Activated loops remain tied to native readiness, including after haste.
    while (_fixedLoopCursor<_fixedLoops.count) {
        NSDictionary *loop=_fixedLoops[_fixedLoopCursor]; bool explicitStep=false;
        if ([_fixedStage[@"holdLoops"] containsObject:loop[@"slot"]]) { ++_fixedLoopCursor; continue; }
        for (NSDictionary *a in _fixedStage[@"actions"]) if ([a[@"kind"] isEqual:@"skill"] && [a[@"slot"] isEqual:loop[@"slot"]]) explicitStep=true;
        if (explicitStep) { ++_fixedLoopCursor; continue; }
        PHOrbFixedPlanMatch condition=PHOrbFixedPlanActionCondition(loop,&e);
        if (condition==PHOrbFixedPlanUnknown) { [self queueStop:@"固定計畫循環技能條件未知"]; return; }
        if (condition==PHOrbFixedPlanNo) { ++_fixedLoopCursor; continue; }
        [self armFixedAction:loop loop:YES]; return;
    }
    while (_fixedAction<[_fixedStage[@"actions"] count]) {
        NSDictionary *a=_fixedStage[@"actions"][_fixedAction];
        if ((_fixedOnce&(1u<<_fixedAction)) && [a[@"frequency"] isEqual:@"once"]) { ++_fixedAction; continue; }
        PHOrbFixedPlanMatch condition=PHOrbFixedPlanActionCondition(a,&e);
        if (condition==PHOrbFixedPlanUnknown) { [self queueStop:@"固定計畫技能觸發條件未知"]; return; }
        if (condition==PHOrbFixedPlanNo) { ++_fixedAction; continue; }
        [self armFixedAction:a loop:NO]; return;
    }
    _fixedStall=[_fixedStage[@"absorbStall"] boolValue] &&
        (e.enemyEffects & ~e.playerBypass & (PHOrbEnemyDamageAbsorb|PHOrbEnemyAttributeAbsorb));
    // This is evaluated after the current puzzle's skills have acknowledged.
    // A previous turn's two-turn bypass, or a successful six-colour search,
    // is not evidence that a retry can damage the remaining enemies.
    if (![self fixedBoardConditionsAllowed:&_live]) return;
    _assistRecoveryL=[_fixedStage[@"board"][@"assistRecoveryL"] boolValue];
    if (!PHOrbCanPreview(&_live)) { [self queueStop:PHOrbObstacleDescription(&_live)]; return; }
    [self beginSolve];
}
- (void)castFixedAction:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame {
    NSString *reason=nil;
    if (!PHOrbSameAutomation(&_fixedBefore,state) || ![self fixedTeamMatches:state] ||
        ![_engine readFixedPlan:state frame:frame reason:&reason]) {
        [self queueStop:reason ?: @"固定計畫行動前狀態已改變"]; return;
    }
    if ([_fixedPending[@"when"] isEqual:@"assist_void"] && ![self captureAssistRecovery:state]) return;
    if (![self fixedEnemyStateAllowed:state]) return;
    PHOrbFixedPlanEvidence e=[self fixedEvidence:state];
    if (PHOrbFixedPlanActionCondition(_fixedPending,&e)!=PHOrbFixedPlanYes) {
        [self queueStop:@"固定計畫行動前條件已改變"]; return;
    }
    if ([_fixedPending[@"kind"] isEqual:@"target"]) {
        int chosen=-1; unsigned id=[_fixedPending[@"monster"] unsignedIntValue];
        for (unsigned i=0;i<state->fixedPlan.count;++i) {
            const auto &n=state->fixedPlan.enemies[i];
            if (!n.identityKnown || !n.hpKnown || !n.hp || bool(n.flags&0x100u)!=[_fixedPending[@"target"] isEqual:@"part"] || (id && n.catalogID!=id)) continue;
            if (chosen>=0) { [self queueStop:@"指定目標不唯一，未任意選敵人"]; return; }
            chosen=int(i);
        }
        if (chosen<0) { [self queueStop:@"找不到固定計畫指定的存活目標"]; return; }
        if (state->fixedPlan.target==chosen) { [self finishFixedAction]; return; }
        _fixedConsumed=true; _fixedSlot=NSUInteger(chosen);
        if (![_engine stageFixedPlanTarget:_fixedSlot expected:state frame:frame reason:&reason]) {
            [self queueStop:reason ?: @"原生目標點選窗口未確認"]; return;
        }
        _phase=PHOrbSessionPhase::FixedTargetWaiting; _auto.phaseAt=_now; _hasStable=false; return;
    }
    NSArray *slots=[@[_fixedPending[@"slot"]] arrayByAddingObjectsFromArray:_fixedPending[@"alternatives"] ?: @[]];
    for (NSNumber *number in slots) {
        const NSUInteger slot=number.unsignedIntegerValue-1;
        if (state->automation.members[slot].number!=[_fixedPending[@"monster"] unsignedIntValue]) {
            [self queueStop:@"固定計畫指定位置的本體形態不符"]; return;
        }
        PHOrbSkillResult ready=[_engine fixedSkillReadiness:slot request:PHOrbFixedPlanActionRequest(_fixedPending)
            expected:state frame:frame reason:&reason];
        if (ready==PHOrbSkillWindowClosed) { [self deferSkillWindow:reason]; return; }
        if (ready==PHOrbSkillUnavailable) continue;
        if (ready!=PHOrbSkillStarted) { [self queueStop:reason ?: @"固定計畫技能來源未知"]; return; }
        _fixedBefore=*state; _fixedSlot=slot; _fixedAcknowledged=false; _fixedConsumed=true;
        PHOrbSkillResult result=[_engine useSkill:slot request:PHOrbFixedPlanActionRequest(_fixedPending)
            expected:state frame:frame reason:&reason];
        if (result!=PHOrbSkillStarted) { [self queueStop:reason ?: @"固定計畫技能啟動未確認，不重試"]; return; }
        _phase=PHOrbSessionPhase::FixedSkillWaiting; _auto.phaseAt=_now; _hasStable=false; return;
    }
    BOOL loopActive=NO;
    if ([_fixedPending[@"loop"] boolValue]) for (NSDictionary *loop in _fixedLoops)
        if ([loop[@"slot"] isEqual:_fixedPending[@"slot"]] && [loop[@"skill"] isEqual:_fixedPending[@"skill"]]) loopActive=YES;
    // Only a duplicate can be omitted, after the same declared skill was
    // actually acknowledged in this puzzle. Old turns and skipped actions
    // never supply this receipt; the first required action cannot be skipped.
    BOOL retrySkip=NO;
    if ([_fixedPending[@"optionalAfterFirst"] boolValue] && _fixedPuzzles && !_fixedPendingLoop)
        for (NSUInteger i=0;i<_fixedAction;++i)
            if ((_fixedPuzzleSkills&(1u<<i)) && PHOrbFixedPlanSameSkill(_fixedStage[@"actions"][i],_fixedPending)) retrySkip=YES;
    if (retrySkip) [self logAutomation:[NSString stringWithFormat:@"固定計畫 B%lu · %@：補盤時技能 %@ 未就緒，略過",
        (unsigned long)_fixedWave+1,_fixedStage[@"id"],_fixedPending[@"skill"]]];
    if ([_fixedPending[@"optional"] boolValue] || (loopActive && ![_fixedPending[@"requireReady"] boolValue]) || retrySkip) {
        [self finishFixedAction]; return;
    }
    [self queueStop:reason ?: @"固定計畫必要技能尚未就緒，未轉珠"]; return;
}
- (NSDictionary *)fixedBoardPlan:(PHOrbSolveOptions *)options {
    NSDictionary *board=_fixedStage[@"board"]; NSString *policy=_fixedStall ? @"six_colors_no_fours" : board[@"policy"];
    auto &p=options->fixedBoard;
    p.mode=[policy isEqual:@"one_combo"] ? PHOrbFixedBoardMode::OneCombo : [policy isEqual:@"six_colors_no_fours"] ? PHOrbFixedBoardMode::SixColorStall :
        [policy isEqual:@"six_colors_fours"] ? PHOrbFixedBoardMode::FourAttack : [policy isEqual:@"l_only"] ? PHOrbFixedBoardMode::LOnly : PHOrbFixedBoardMode::Normal;
    p.firstColors=[policy hasPrefix:@"six_colors"] ? 63 : 0;
    p.requireHeartFour=[policy isEqual:@"six_colors_fours"];
    if ([self fixedUnmatchableRecoveryEnabled] && _preview.unmatchableRecovery.valid)
        p.unmatchableRecoveryColors=[_unmatchableTrace[@"eligibleColors"] unsignedIntValue];
    NSMutableArray *goals=[NSMutableArray array];
    if (p.firstColors) [goals addObject:@{@"kind":@"colors",@"colors":@63,@"count":@6,@"length":@0}];
    if (p.requireHeartFour) [goals addObject:@{@"kind":@"four",@"colors":@32,@"count":@1,@"length":@0}];
    if (p.unmatchableRecoveryColors) [goals addObject:@{@"kind":@"four",@"colors":@(p.unmatchableRecoveryColors),@"count":@1,@"length":@0}];
    if (board[@"lColors"] && !_fixedStall) [goals addObject:@{@"kind":@"l",@"colors":board[@"lColors"],@"count":@1,@"length":@0}];
    return @{@"available":@YES,@"goals":goals,@"message":[NSString stringWithFormat:@"固定計畫 B%lu · %@ · %@",(unsigned long)_fixedWave+1,_fixedStage[@"id"],policy],
        @"warning":@"",@"leaders":@[]};
}
- (BOOL)fixedAttackBoard {
    return !_fixedStall && [@[@"six_colors_fours",@"six_colors"] containsObject:_fixedStage[@"board"][@"policy"]];
}
- (NSDictionary *)fixedProgressTrace {
    return @{@"profile":_fixedProfile[@"id"] ?: @"",@"floor":@(_fixedWave+1),@"variant":_fixedVariant ?: @"",
        @"stage":_fixedStage[@"id"] ?: @"",@"action":@(_fixedAction),@"confirmedPuzzles":@(_fixedPuzzles),
        @"board":_fixedStage[@"board"] ?: @{},@"repeat":_fixedStage[@"repeat"] ?: @{},
        @"pending":_fixedPending ?: @{},@"activeLoops":_fixedLoops ?: @[],@"lastStop":_fixedLastStop ?: @{},
        @"consumed":@(_fixedConsumed),@"receipt":@(_fixedAcknowledged),@"partsGone":@(_fixedPartsGone),@"absorbStall":@(_fixedStall)};
}
