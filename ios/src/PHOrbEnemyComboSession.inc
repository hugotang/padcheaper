// Current enemy state is action evidence, independent of optional diagnostics.
- (BOOL)captureEnemyCombo:(PHOrbSnapshot *)state {
    state->enemyCombo = {};
    NSString *reason = nil; unsigned minimum = 0;
    if (![_engine respondsToSelector:@selector(readEnemyCombo:frame:reason:)] ||
        ![_engine readEnemyCombo:state frame:_frame reason:&reason] ||
        !PHOrbEnemyComboRequirement(&state->enemyCombo, &minimum)) {
        if (!reason && state->enemyCombo.valid) for (unsigned slot = 0; slot < state->enemyCombo.enemyCount; ++slot) {
            if ((state->enemyCombo.aliveMask & (1u << slot)) && int16_t(state->enemyCombo.upperTurns[slot]) > 0)
                reason = @"敵人有「Combo 以上吸收」；尚未能限制天降及加算後的最高 Combo，已停止";
        }
        [self queueStop:reason ?: @"敵人對策：Combo 吸收及減 Combo 資料未確認，已停止"]; return NO;
    }
    _enemyComboCheckedAt = _now;
    return YES;
}
- (BOOL)validateEnemyCombo:(PHOrbSnapshot *)state {
    if (!_preview.enemyCombo.valid) return YES;
    if (![self captureEnemyCombo:state]) return NO;
    if (!PHOrbSameEnemyCombo(&_preview.enemyCombo, &state->enemyCombo)) {
        [self queueStop:@"敵人 Combo 吸收或解吸收狀態已改變，請重新開始"]; return NO;
    }
    return YES;
}
