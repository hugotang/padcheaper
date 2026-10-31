// Presentation only. Current observations never authorize a skill or a route.
id ObservationValue(bool known, id value) { return known ? value : NSNull.null; }
NSString *ObservationStatusName(PHOrbObservationStatus status) {
    switch (status) {
        case PHOrbObservationNotRequested: return @"not-requested";
        case PHOrbObservationReady: return @"ready";
        case PHOrbObservationUnavailable: return @"unavailable";
        case PHOrbObservationTransition: return @"transition";
        case PHOrbObservationMultiplayer: return @"multiplayer";
        case PHOrbObservationChanged: return @"changed";
    }
    return @"unknown";
}

bool ObservationWindow(const PHOrbSnapshot &state) {
    return state.valid && state.present && !state.multiplayerDisabled && state.multiplayer.kind == PHOrbMultiplayerNone &&
        PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer) && !state.identity.sourceMode &&
        state.phase == 2 && state.settled && !state.boardTransition && !state.dragged && PHInputIsQuiet(&state.input);
}

bool ObservationContextMatches(const PHOrbSnapshot &captured, const PHOrbSnapshot &live) {
    if (!ObservationWindow(live) || !PHOrbSameTurn(&captured, &live) || !PHOrbSameLeaders(&captured, &live) ||
        captured.moves != live.moves || !PHOrbSamePlanBoard(&captured.board, &live.board)) return false;
    if (captured.automation.valid && live.automation.valid && !PHOrbSameAutomation(&captured, &live)) return false;
    if (captured.observation.status == PHOrbObservationReady && live.automation.valid) {
        if (live.automation.busy || (captured.observation.hpKnown && live.automation.hpKnown &&
            (captured.observation.hp != live.automation.hp || captured.observation.maximumHP != live.automation.maximumHP))) return false;
        for (unsigned slot = 0; slot < 6; ++slot) {
            const auto &observed = captured.observation.members[slot]; const auto &member = live.automation.members[slot];
            if (observed.active != member.active || (observed.active &&
                (observed.number != member.number || observed.charge != member.charge || observed.bound != member.bound ||
                observed.activeAssist != member.assistCatalogID || observed.sourceAssist != member.sourceCatalogIDs[1] ||
                observed.stage[0] != member.skillStage[0] || observed.stage[1] != member.skillStage[1]))) return false;
        }
    }
    return true;
}

NSString *ObservationColors(uint8_t colors) {
    NSArray *names = @[@"火", @"水", @"木", @"光", @"暗"];
    NSMutableArray *selected = [NSMutableArray array];
    for (unsigned i = 0; i < 5; ++i) if (colors & (1u << i)) [selected addObject:names[i]];
    return [selected componentsJoinedByString:@"／"];
}

NSDictionary *BattleObservationTrace(const PHOrbSnapshot &captured, bool current, double ageMS, double durationMS) {
    const auto &observation = captured.observation;
    NSMutableDictionary *trace = [@{@"status":ObservationStatusName(observation.status), @"current":@(current),
        @"ageMS":@(ageMS), @"readDurationMS":@(durationMS), @"diagnosticOnly":@YES,
        @"enemyForecastKnown":@NO, @"shieldReductionKnown":@NO, @"healingKnown":@NO,
        @"survivalKnown":@NO, @"lRecoveryAmountKnown":@NO,
        @"chargeWindowIsUsability":@NO, @"chargeUnitsAreTurns":@NO,
        @"unsupported":@[@"enemy-action-branches", @"next-hit-damage", @"effective-shield", @"healing-order",
            @"l-recovery-timing", @"future-skill-deadlines"]} mutableCopy];
    // Do not display stale facts with a small-print freshness warning. The
    // previous observation is discarded from the public trace immediately.
    if (!current) return trace;
    [trace addEntriesFromDictionary:@{@"dungeon":@(captured.identity.dungeon), @"floor":@(captured.identity.floor),
        @"wave":@(captured.wave), @"phase":@(captured.phase), @"hpKnown":@(observation.hpKnown),
        @"hp":ObservationValue(observation.hpKnown, @(observation.hp)),
        @"maximumHP":ObservationValue(observation.hpKnown, @(observation.maximumHP)),
        @"rulesKnown":@(observation.rulesKnown),
        @"assistProhibited":ObservationValue(observation.rulesKnown, @((observation.dungeonFlags & 8u) != 0)),
        @"assistVoidKnown":@(observation.assistVoidKnown),
        @"assistVoidTurns":ObservationValue(observation.assistVoidKnown, @(observation.assistVoidTurns)),
        @"assistVoidFresh":ObservationValue(observation.assistVoidKnown, @(observation.assistVoidFresh)),
        @"cooldownAdjustmentKnown":@(observation.cooldownAdjustmentKnown),
        @"dungeonCooldownAdjustment":ObservationValue(observation.cooldownAdjustmentKnown, @(observation.dungeonCooldownAdjustment)),
        @"enemiesKnown":@(observation.enemiesKnown),
        @"enemyCount":ObservationValue(observation.enemiesKnown, @(observation.enemyCount)),
        @"activeEnemies":ObservationValue(observation.enemiesKnown, @(observation.activeEnemies))}];
    NSMutableArray *members = [NSMutableArray array], *enemies = [NSMutableArray array];
    NSArray *windows = @[@"unknown", @"no-positive-cooldown", @"below-body", @"body", @"assist"];
    for (unsigned slot = 0; slot < 6; ++slot) {
        const auto &member = observation.members[slot];
        const unsigned window = unsigned(member.chargeWindow);
        [members addObject:@{@"slot":@(slot + 1), @"number":@(member.number), @"active":@(member.active),
            @"charge":@(member.charge), @"bound":@(member.bound), @"temporaryForm":@(member.temporaryForm),
            @"sourceAssistCatalogID":@(member.sourceAssist), @"activeAssistCatalogID":@(member.activeAssist),
            @"assistRetained":@(member.assistRetained), @"assistVariantUnsupported":@(member.assistVariantUnsupported),
            @"skillIDs":@[ObservationValue(member.skillKnown[0], @(member.skillIDs[0])),
                ObservationValue(member.skillKnown[1], @(member.skillIDs[1]))],
            @"rootSkillIDs":@[ObservationValue(member.skillKnown[0], @(member.rootSkillIDs[0])),
                ObservationValue(member.skillKnown[1], @(member.rootSkillIDs[1]))],
            @"stage":@[@(member.stage[0]), @(member.stage[1])],
            @"cachedStage":@[@(member.cachedStage[0]), @(member.cachedStage[1])],
            @"stageCacheMatches":@(member.stage[0] == member.cachedStage[0] && member.stage[1] == member.cachedStage[1]),
            @"catalogCooldown":@[ObservationValue(member.cooldownKnown[0], @(member.catalogCooldown[0])),
                ObservationValue(member.cooldownKnown[1], @(member.catalogCooldown[1]))],
            @"thresholdsKnown":@(member.thresholdsKnown),
            @"bodyCooldown":ObservationValue(member.thresholdsKnown, @(member.bodyCooldown)),
            @"totalCooldown":ObservationValue(member.thresholdsKnown, @(member.totalCooldown)),
            @"chargeToBody":ObservationValue(member.thresholdsKnown, @(member.chargeToBody)),
            @"chargeToAssist":ObservationValue(member.thresholdsKnown && member.activeAssist, @(member.chargeToAssist)),
            @"chargeWindow":window < windows.count ? windows[window] : @"unknown",
            @"sourceFamilyEqual":ObservationValue(member.familyKnown, @(member.sourceFamilyEqual)),
            @"recoveryLatents":ObservationValue(member.latentKnown, @(member.assistRecoveryLatents)),
            @"effectiveLAwakenings":ObservationValue(member.lAwakeningsKnown, @(member.lAwakenings)),
            @"lEligibilityKnown":@(member.lEligibilityKnown), @"lEligible":ObservationValue(member.lEligibilityKnown, @(!member.lBlockers)),
            @"lEligibleColors":ObservationValue(member.lEligibilityKnown, @(member.lColors)),
            @"lBlockers":@(member.lBlockers)}];
    }
    for (unsigned slot = 0; slot < observation.enemyCount && slot < 8; ++slot) {
        const auto &enemy = observation.enemies[slot];
        [enemies addObject:@{@"slot":@(slot + 1), @"parts":@(enemy.parts), @"hpKnown":@(enemy.hpKnown),
            @"hp":ObservationValue(enemy.hpKnown, @(enemy.hp)), @"effectsKnown":@(enemy.effectsKnown),
            @"actionBusy":ObservationValue(enemy.effectsKnown, @(enemy.actionBusy)),
            @"effects":ObservationValue(enemy.effectsKnown, @(enemy.effects)),
            @"effectTurns":ObservationValue(enemy.effectsKnown, @[@(enemy.effectTurns[0]), @(enemy.effectTurns[1]), @(enemy.effectTurns[2]), @(enemy.effectTurns[3])]),
            @"effectMasks":ObservationValue(enemy.effectsKnown, @[@(enemy.effectMasks[0]), @(enemy.effectMasks[1]), @(enemy.effectMasks[2]), @(enemy.effectMasks[3])])}];
    }
    trace[@"members"] = members; trace[@"enemies"] = enemies;
    return trace;
}

NSString *BattleObservationDescription(const PHOrbBattleObservation &observation, bool current) {
    if (!current) {
        if (observation.status == PHOrbObservationNotRequested || observation.status == PHOrbObservationMultiplayer) return @"";
        return @"戰況診斷：等待新的穩定單人資料；尚不能判斷耐久。";
    }
    NSMutableArray *lines = [NSMutableArray arrayWithObject:observation.enemiesKnown ?
        [NSString stringWithFormat:@"戰況診斷：%u 個存活敵人。下一擊／減傷／回血尚未解析。", observation.activeEnemies] :
        @"戰況診斷：敵人資料不完整。下一擊／減傷／回血尚未解析。"];
    if (observation.assistVoidKnown) [lines addObject:observation.assistVoidTurns > 0 ?
        [NSString stringWithFormat:@"暫時武裝無效：%d 回合", observation.assistVoidTurns] : @"目前沒有暫時武裝無效。"];
    else [lines addObject:@"暫時武裝無效：未能確認。"];
    if (observation.rulesKnown && (observation.dungeonFlags & 8u)) [lines addObject:@"本關卡禁止武裝技能。"];
    NSMutableArray *providers = [NSMutableArray array];
    unsigned unknown = 0;
    for (unsigned i = 0; i < 6; ++i) {
        const auto &member = observation.members[i];
        if (member.lEligibilityKnown && !member.lBlockers && member.lColors) [providers addObject:
            [NSString stringWithFormat:@"位置 %u：%@ L（有效 L %u 個）", i + 1, ObservationColors(member.lColors), member.lAwakenings]];
        else if (!member.lEligibilityKnown && member.active) ++unknown;
    }
    if (providers.count) [lines addObject:[NSString stringWithFormat:@"解武裝資格：%@；解除量及時點待核對。", [providers componentsJoinedByString:@"；"]]];
    else [lines addObject:unknown ? @"解武裝 L 資格有未確認資料。" : @"目前沒有有效的解武裝 L 提供者。"];
    if (providers.count && unknown) [lines addObject:@"另有隊員的 L 資格未能確認。"];
    for (unsigned i = 0; i < 6; ++i) {
        const auto &member = observation.members[i];
        if (!member.active) continue;
        NSString *charge = nil;
        if (!member.thresholdsKnown) charge = [NSString stringWithFormat:@"充能 %u，CD 未能完整確認", member.charge];
        else if (member.activeAssist && !(observation.dungeonFlags & 8u)) charge =
            [NSString stringWithFormat:@"充能 %u／本體 %u／武裝合計 %u；%@", member.charge, member.bodyCooldown, member.totalCooldown,
                member.chargeWindow == PHOrbChargeAssist ? @"已達武裝充能門檻" :
                [NSString stringWithFormat:@"距武裝還差 %u 充能", member.chargeToAssist]];
        else charge = [NSString stringWithFormat:@"充能 %u／本體 %u；%@", member.charge, member.bodyCooldown,
            !member.sourceAssist ? @"沒有來源武裝" : !member.assistRetained ? @"武裝已消耗／移除" :
            member.temporaryForm ? @"暫時變身中" : @"目前武裝未生效或受限制"];
        [lines addObject:[NSString stringWithFormat:@"位置 %u：%@", i + 1, charge]];
    }
    [lines addObject:@"充能差不是回合預測；能否施放仍由遊戲判定。尚未啟用自動 L＋心四連耐久。"];
    return [lines componentsJoinedByString:@"\n"];
}
