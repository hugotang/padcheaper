// Included inside PHOrbSession's implementation. Evidence is never borrowed
// from the throttled diagnostic observation and is not saved in a profile.
- (NSDictionary *)assistRecoveryTrace:(const PHOrbAssistRecoveryState *)state status:(NSString *)status {
    NSMutableArray *amounts = [NSMutableArray array], *providers = [NSMutableArray array], *members = [NSMutableArray array];
    for (unsigned color = 0; color < 5; ++color) {
        [amounts addObject:@(state->perColor[color])]; [providers addObject:@(state->providers[color])];
    }
    for (unsigned slot = 0; slot < 6; ++slot) [members addObject:@{@"slot":@(slot + 1),
        @"number":@(state->numbers[slot]), @"colors":@(state->colors[slot]), @"effectiveL":@(state->awakenings[slot]),
        @"latentCount":@(state->latents[slot]), @"blockers":@(state->blockers[slot])}];
    return @{@"status":status, @"known":@(state->valid), @"beforeTurns":@(state->turns), @"fresh":@(state->fresh),
        @"perColor":amounts, @"providers":providers, @"members":members, @"unknownMembers":@(state->unknownMembers),
        @"turn":@(_autoTurn), @"actualConfirmed":@NO, @"survivalKnown":@NO, @"healingKnown":@NO,
        @"nativeQueueCapacityConfirmed":@NO};
}
- (BOOL)bindingNeedsAssistRecovery {
    if ([_profileLibrary[@"autoSelect"] boolValue]) {
        for (NSDictionary *profile in _profileLibrary[@"profiles"])
            if ([profile[@"settings"][@"assistRecoveryL"] boolValue]) return YES;
    }
    NSDictionary *selected = PHOrbProfileNamed(_profileLibrary, _profileLibrary[@"selected"]);
    return selected ? [selected[@"settings"][@"assistRecoveryL"] boolValue] : _assistRecoveryL;
}
- (BOOL)captureAssistRecovery:(PHOrbSnapshot *)state {
    state->assistRecovery = {};
    NSString *reason = nil;
    if (![_engine respondsToSelector:@selector(readAssistRecovery:frame:reason:)] ||
        ![_engine readAssistRecovery:state frame:_frame reason:&reason] || !state->assistRecovery.valid) {
        NSMutableDictionary *trace = [_assistTrace mutableCopy];
        trace[@"status"] = @"unavailable"; trace[@"known"] = @NO; trace[@"actualConfirmed"] = @NO;
        trace[@"reason"] = reason ?: @"缺少可核對的原生資料"; _assistTrace = [trace copy];
        [self queueStop:reason ?: @"武裝回復 L：缺少可核對的原生資料，已停止"]; return NO;
    }
    _assistCheckedAt = _now;
    return YES;
}
- (BOOL)validateAssistRecovery:(PHOrbSnapshot *)state {
    if (!_preview.assistRecovery.valid) return YES;
    if (![self captureAssistRecovery:state]) return NO;
    if (!PHOrbSameAssistRecovery(&_preview.assistRecovery, &state->assistRecovery)) {
        NSMutableDictionary *trace = [_assistTrace mutableCopy];
        trace[@"status"] = @"changed-before-release"; trace[@"actualConfirmed"] = @NO;
        trace[@"observedMechanism"] = [self assistRecoveryTrace:&state->assistRecovery status:@"changed"];
        _assistTrace = [trace copy];
        [self queueStop:@"武裝無效、有效 L 或提供隊員已改變，請重新開始"]; return NO;
    }
    return YES;
}
- (BOOL)confirmAssistRecoveryResult:(const PHOrbSnapshot *)state {
    if (!_assistAwaitingResult) return YES;
    _assistAwaitingResult = false;
    NSMutableDictionary *trace = [_assistTrace mutableCopy];
    const bool context = PHBattleSameIdentity(&_assistReleased.identity, &state->identity) &&
        _assistReleased.inputManager == state->inputManager;
    const bool confirmed = context && state->assistRecovery.valid && !state->assistRecovery.turns;
    trace[@"status"] = confirmed ? @"observed-clear" : @"unconfirmed-after-turn";
    trace[@"actualConfirmed"] = @(confirmed);
    trace[@"observedTurns"] = state->assistRecovery.valid ? @(state->assistRecovery.turns) : NSNull.null;
    trace[@"waveChanged"] = @(_assistReleased.wave != state->wave);
    // A normal duration tick, a later enemy reapply, or other native effects
    // cannot be separated by this sample. Never label the delta as L-only.
    trace[@"lOnlyAmountConfirmed"] = @NO; _assistTrace = [trace copy];
    if (_executionHistory.count) {
        NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
        record[@"assistRecovery"] = _assistTrace; _executionHistory[_executionHistory.count - 1] = [record copy];
    }
    if (!confirmed) {
        [self queueStop:@"轉珠後仍未確認武裝無效已解除，可能有殘留或再次施加；已停止，請檢查戰況"]; return NO;
    }
    [self logAutomation:@"已重新讀取確認武裝無效為 0；下一回合撤下臨時 L，重判隊伍、技能及原有目標"];
    return YES;
}
