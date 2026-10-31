static void SkillTimingRuleTests(void) {
    NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy];
    PHOrbSnapshot state = {0}; state.valid = state.settled = state.automation.valid = true;
    state.automation.members[0].active = true;
    rule[@"intervalMode"] = @"accepted"; rule[@"startAfter"] = @2; rule[@"every"] = @2;
    NSString *reason = nil;
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,1,0,&reason) == PHOrbRuleNo &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,1,2,0,NULL) == PHOrbRuleNo &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,1,3,0,NULL) == PHOrbRuleYes,
        "accepted timing waits for two observed puzzles even when the displayed run restarts at one");
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,3,3,NULL) == PHOrbRuleNo &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,1,4,3,NULL) == PHOrbRuleNo &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,1,5,3,NULL) == PHOrbRuleYes,
        "a use on turn three permits the next use on turn five, not on the next restart");
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,6,3,NULL) == PHOrbRuleYes &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,1,7,6,NULL) == PHOrbRuleNo &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,1,8,6,NULL) == PHOrbRuleYes,
        "missing a ready turn does not impose odd-turn parity; the eventual accepted use starts the next interval");
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,0,0,NULL) == PHOrbRuleUnknown &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,1,3,4,NULL) == PHOrbRuleUnknown &&
        PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleUnknown,
        "unknown or future history cannot authorize an accepted-timing rule");
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,NSUIntegerMax,NSUIntegerMax - 2,NULL) == PHOrbRuleYes,
        "large accepted-turn values do not overflow interval arithmetic");
    rule[@"intervalMode"] = @"run"; rule[@"startAfter"] = @0;
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,8,8,NULL) == PHOrbRuleYes &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,2,9,0,NULL) == PHOrbRuleNo &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,3,0,0,NULL) == PHOrbRuleYes,
        "legacy intervals remain relative to each run and ignore retained accepted history");
    rule[@"startAfter"] = @2;
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,2,0,0,NULL) == PHOrbRuleNo &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,3,0,0,NULL) == PHOrbRuleYes &&
        PHOrbTriggerSkillRuleWithTiming(rule,&state,4,0,0,NULL) == PHOrbRuleNo,
        "run timing can delay its first eligible turn without changing the interval");
    [rule removeObjectsForKeys:@[@"intervalMode",@"startAfter"]];
    Check(PHOrbSkillRulesValid(@[rule]) && [PHOrbNormalizedSkillRule(rule)[@"intervalMode"] isEqual:@"run"] &&
        [PHOrbNormalizedSkillRule(rule)[@"startAfter"] isEqual:@0], "old config files acquire the original timing defaults");
    for (NSDictionary *change in @[@{@"intervalMode":@"clock"},@{@"intervalMode":@1},@{@"intervalMode":NSNull.null},
        @{@"startAfter":@-1},@{@"startAfter":@1000},@{@"startAfter":@YES},@{@"startAfter":@1.5},@{@"startAfter":@"2"}]) {
        NSMutableDictionary *bad = [rule mutableCopy]; [bad addEntriesFromDictionary:change];
        Check(!PHOrbSkillRulesValid(@[bad]), "timing fields reject invalid modes, Boolean delays and out-of-range values");
    }
    rule[@"intervalMode"] = @"accepted"; rule[@"startAfter"] = @0; rule[@"every"] = @1;
    rule[@"required"] = @YES; rule[@"enemy"] = @"damage_absorb";
    state.automation.enemiesKnown = true; state.automation.enemyEffects = PHOrbEnemyDamageAbsorb;
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,1,0,NULL) == PHOrbRuleYes,
        "a separate required rescue can trigger during the ordinary group's warmup");
    state.automation.playerBypass = PHOrbEnemyDamageAbsorb;
    Check(PHOrbTriggerSkillRuleWithTiming(rule,&state,1,1,0,NULL) == PHOrbRuleNo,
        "retained timing does not bypass the existing active-effect condition");
    NSMutableArray *group = [NSMutableArray array];
    for (NSNumber *slot in @[@1,@2,@1,@2,@1,@2]) {
        NSMutableDictionary *row = [PHOrbDefaultSkillRule() mutableCopy];
        row[@"slot"] = slot; row[@"group"] = @1; row[@"intervalMode"] = @"accepted";
        [group addObject:row];
    }
    uint8_t next[PHOrbMaxSkillGroups] = {0}; next[0] = 3;
    Check([PHOrbSkillRuleOrder(group,next) isEqual:@[@1,@3,@5,@0,@2,@4]],
        "after a rescue use, timed groups try every condition for the other position before the previous caster");
    for (NSMutableDictionary *row in group) row[@"intervalMode"] = @"run";
    Check([PHOrbSkillRuleOrder(group,next) isEqual:@[@3,@4,@5,@0,@1,@2]],
        "legacy groups retain their existing row order");
}
