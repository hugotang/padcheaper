// Explicit B2 policy. The native read supplies providers; the solver predicts
// removal timing. Neither is an observed latent-recovery receipt.
- (BOOL)fixedUnmatchableRecoveryEnabled {
    return _auto.active() && _fixedWave==1 && !_fixedStall &&
        [_fixedStage[@"board"][@"policy"] isEqual:@"six_colors_fours"] &&
        [_fixedStage[@"board"][@"unmatchableRecovery"] boolValue];
}
- (BOOL)unmatchableBoardSupported:(const PHOrbSnapshot *)state {
    const unsigned mask=state->board.unmatchableColors;
    if (![self fixedUnmatchableRecoveryEnabled] || state->wave!=1 ||
        !state->unmatchableKnown || state->unmatchableTurns!=1 || !mask || mask>32 || (mask&(mask-1)) ||
        !PHBattleIsSinglePlayer(state->identity.entryMode,state->identity.multiplayer) ||
        state->multiplayer.kind!=PHOrbMultiplayerNone || state->multiplayerDisabled ||
        !PHOrbBoardValid(&state->board) || (state->board.spinnerColors&~63u) ||
        (state->board.minErase && state->board.minErase!=3) || state->board.minMatch>3) return NO;
    for (unsigned i=0;i<unsigned(state->board.width)*state->board.height;++i)
        if (state->board.types[i]>=6 || (state->board.flags[i]&(PHOrbFlagThorn|PHOrbFlagNail|PHOrbFlagCombo))) return NO;
    return YES;
}
- (NSDictionary *)unmatchableRecoveryTrace:(const PHOrbSnapshot *)state status:(NSString *)status {
    const auto &r=state->unmatchableRecovery;
    NSMutableArray *members=[NSMutableArray array], *amounts=[NSMutableArray array], *providers=[NSMutableArray array];
    for (unsigned c=0;c<5;++c) { [amounts addObject:@(r.perColor[c])]; [providers addObject:@(r.providers[c])]; }
    for (unsigned i=0;i<6;++i) [members addObject:@{@"slot":@(i+1),@"number":@(r.numbers[i]),
        @"colors":@(r.colors[i]),@"effectiveTPA":@(r.awakenings[i]),@"latent41Count":@(r.latents[i]),@"blockers":@(r.blockers[i])}];
    return @{@"status":status,@"known":@(r.valid),@"beforeTurns":@(state->unmatchableTurns),
        @"beforeColors":@(state->board.unmatchableColors),@"fresh":@(r.fresh),@"perColor":amounts,@"providers":providers,
        @"members":members,@"unknownMembers":@(r.unknownMembers),@"generation":@(_generation),@"turn":@(_auto.turn),
        @"scope":@"b2-one-color-one-turn-first-native-group-then-existing-orb-cascade",
        @"actualConfirmed":@NO,@"latentOnlyAmountConfirmed":@NO,@"nativeQueueCapacityConfirmed":@NO,
        @"survivalKnown":@NO,@"fishingSupported":@NO};
}
- (BOOL)captureUnmatchableRecovery:(PHOrbSnapshot *)state {
    state->unmatchableRecovery={}; NSString *reason=nil;
    if (![_engine respondsToSelector:@selector(readUnmatchableRecovery:frame:reason:)] ||
        ![_engine readUnmatchableRecovery:state frame:_frame reason:&reason] || !state->unmatchableRecovery.valid) {
        NSMutableDictionary *trace=[(_unmatchableTrace ?: @{}) mutableCopy];
        trace[@"status"]=@"unavailable"; trace[@"known"]=@NO; trace[@"actualConfirmed"]=@NO;
        trace[@"reason"]=reason ?: @"缺少可核對的原生不可消回復資料"; _unmatchableTrace=[trace copy];
        [self queueStop:trace[@"reason"]]; return NO;
    }
    _unmatchableCheckedAt=_now; return YES;
}
- (BOOL)prepareUnmatchableRecovery:(PHOrbSnapshot *)state {
    state->unmatchableRecovery={}; _unmatchableTrace=@{};
    if (![self fixedUnmatchableRecoveryEnabled] || !(state->board.unmatchableColors&63u)) return YES;
    // This guide branch belongs to Bartholomew. No card name, saved plan or
    // earlier floor observation substitutes for this fresh exact enemy list.
    const auto &f=state->fixedPlan;
    if (![self unmatchableBoardSupported:state] || !f.valid || f.count!=1 || f.partsMask ||
        !f.enemies[0].identityKnown || f.enemies[0].catalogID!=200974 ||
        !f.enemies[0].hpKnown || !f.enemies[0].hp || (f.enemies[0].flags&0x100u)) {
        [self queueStop:@"不可消回復只支援已確認的 B2 巴索羅繆、單色一回合及普通珠盤面"]; return NO;
    }
    if (![self captureUnmatchableRecovery:state]) return NO;
    unsigned colors=0;
    for (unsigned c : {0u,2u}) if (!(state->board.unmatchableColors&(1u<<c)) &&
        state->unmatchableRecovery.perColor[c]>=state->unmatchableTurns && state->unmatchableRecovery.providers[c]) colors|=1u<<c;
    NSMutableDictionary *trace=[[self unmatchableRecoveryTrace:state status:colors ? @"planned" : @"no-provider"] mutableCopy];
    trace[@"eligibleColors"]=@(colors); _unmatchableTrace=[trace copy];
    if (!colors) { [self queueStop:@"未確認可用的火／木二體攻擊與不可消回復潛覺提供者，未開始轉珠"]; return NO; }
    return YES;
}
- (BOOL)validateUnmatchableRecovery:(PHOrbSnapshot *)state {
    if (!_preview.unmatchableRecovery.valid) return YES;
    if (![self captureUnmatchableRecovery:state]) return NO;
    if (![self unmatchableBoardSupported:state] ||
        state->board.unmatchableColors!=_preview.board.unmatchableColors ||
        !PHOrbSameAssistRecovery(&_preview.unmatchableRecovery,&state->unmatchableRecovery)) {
        NSMutableDictionary *trace=[_unmatchableTrace mutableCopy];
        trace[@"status"]=@"changed-before-release"; trace[@"actualConfirmed"]=@NO;
        trace[@"observedMechanism"]=[self unmatchableRecoveryTrace:state status:@"changed"]; _unmatchableTrace=[trace copy];
        [self queueStop:@"不可消回合、二體攻擊或回復提供者已改變，已撤銷路線"]; return NO;
    }
    return YES;
}
- (BOOL)confirmUnmatchableRecoveryResult:(const PHOrbSnapshot *)state {
    if (!_solveOptions.fixedBoard.unmatchableRecoveryColors) return YES;
    NSDictionary *last=_executionHistory.lastObject;
    const bool context=last[@"fixedPlanAfterTurn"] && [last[@"releaseConfirmed"] boolValue] &&
        [last[@"generation"] isEqual:@(_generation)] && [last[@"turn"] isEqual:@(_auto.turn)] &&
        PHBattleSameIdentity(&_preview.identity,&state->identity) && _preview.inputManager==state->inputManager;
    const bool clear=context && state->unmatchableKnown && !state->unmatchableTurns && !state->board.unmatchableColors;
    NSMutableDictionary *trace=[_unmatchableTrace mutableCopy];
    trace[@"status"]=clear ? @"observed-clear" : @"unconfirmed-after-turn"; trace[@"actualConfirmed"]=@(clear);
    trace[@"observedTurns"]=state->unmatchableKnown ? @(state->unmatchableTurns) : (id)NSNull.null;
    trace[@"observedColors"]=state->unmatchableKnown ? @(state->board.unmatchableColors) : (id)NSNull.null;
    trace[@"waveChanged"]=@(_preview.wave!=state->wave);
    // The duration can also expire normally or be reapplied by the enemy.
    // A clear after-turn sample is not proof of a latent-only decrement.
    trace[@"latentOnlyAmountConfirmed"]=@NO; _unmatchableTrace=[trace copy];
    if (context) {
        NSMutableDictionary *record=[last mutableCopy]; record[@"unmatchableRecovery"]=_unmatchableTrace;
        _executionHistory[_executionHistory.count-1]=[record copy];
    }
    if (!clear) { [self queueStop:@"轉珠後尚未確認不可消已解除，可能殘留或再次施加；已停止固定計畫"]; return NO; }
    return YES;
}
