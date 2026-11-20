// Template-specific import/timing regressions. These do not model new native
// skill effects or prove the manual opening, cooldowns, damage or shield uptime.
static void MagnardProfileTests(void) {
    NSString *directory = @"ios/templates/magnard-gamewith-589197/";
    NSData *data = [NSData dataWithContentsOfFile:[directory stringByAppendingString:@"post-opening.json"]];
    NSString *reason = nil;
    NSDictionary *library = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),data,&reason);
    NSDictionary *profile = [library[@"profiles"] count] == 1 ? library[@"profiles"][0] : nil;
    Check(profile && !reason && ![library[@"autoSelect"] boolValue] &&
        !profile[@"opening"] && !profile[@"formation"] && !profile[@"fixedPlan"] &&
        [profile[@"team"] isEqual:@[@14360,@14408,@14423,@14390,@14384,@14360]],
        "Magnard imports as a transformed-team ordinary profile without fabricating an automatic opening");
    if (!profile) return;
    NSData *shared = PHOrbExportProfileData(profile,NULL);
    NSDictionary *again = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),shared,NULL);
    Check(shared && [again[@"profiles"][0] isEqual:profile],
        "Magnard's canonical profile survives import and export without losing timing or source restrictions");
    Check(!PHOrbProfilesImportingData(library,data,NULL),"Magnard duplicate import cannot overwrite a saved strategy");
    NSData *reference = [NSData dataWithContentsOfFile:[directory stringByAppendingString:@"strategy.reference.json"]];
    Check(reference && !PHOrbProfilesImportingData(PHOrbDefaultProfiles(),reference,NULL),
        "Magnard's manual opening and formation reference cannot be imported as executable instructions");

    NSDictionary *settings = profile[@"settings"];
    NSArray *rules = settings[@"rules"];
    PHOrbGoalSet goals = {0};
    Check(PHOrbReadGoals(settings[@"goals"],&goals) && goals.count == 3 &&
        goals.items[0].kind == PHOrbGoalFour && goals.items[0].colors == 1 && goals.items[0].count == 1 &&
        goals.items[1].kind == PHOrbGoalFour && goals.items[1].colors == 2 && goals.items[1].count == 1 &&
        goals.items[2].kind == PHOrbGoalFour && goals.items[2].colors == 32 && goals.items[2].count == 1 &&
        [settings[@"assistRecoveryL"] boolValue] && ![settings[@"allowPoisonWithHeartFour"] boolValue],
        "Magnard requires three distinct first-wave straight fours while retaining conditional assist recovery and strict poison policy");
    Check(rules.count == 10,"Magnard fits the ordinary twelve-rule limit");
    if (rules.count != 10) return;
    for (NSDictionary *rule in rules) {
        PHOrbSkillRequest request = PHOrbRequestForRule(rule);
        Check(request.source == PHOrbSkillBody && !request.skill &&
            [rule[@"monster"] isEqual:profile[@"team"][[rule[@"slot"] unsignedIntegerValue]-1]],
            "the unknown skill ID uses the existing body wildcard but never an unrestricted actor or automatic assist source");
    }
    Check([rules[0][@"status"] isEqual:@"awoken_bind"] && [rules[1][@"obstacle"] isEqual:@"unmatchable"] &&
        [rules[0][@"group"] isEqual:rules[1][@"group"]] && [rules[0][@"required"] boolValue] &&
        [rules[1][@"required"] boolValue] && [rules[9][@"slot"] isEqual:@5] && ![rules[9][@"required"] boolValue],
        "Chianti has separate required cleanse predicates and Ripple supply follows every board-changing skill");

    PHOrbSnapshot state = {0};
    state.valid = state.automation.valid = state.automation.enemiesKnown = true;
    for (unsigned i = 0; i < 6; ++i) {
        state.automation.members[i].active = true;
        state.automation.members[i].number = [profile[@"team"][i] unsignedIntValue];
    }
    for (unsigned i = 2; i <= 3; ++i) {
        Check([rules[i][@"every"] isEqual:@2] && [rules[i][@"startAfter"] isEqual:@2] &&
            [rules[i][@"intervalMode"] isEqual:@"accepted"] && [rules[i][@"required"] boolValue] &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,1,0,NULL) == PHOrbRuleNo &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,2,0,NULL) == PHOrbRuleNo &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,3,0,NULL) == PHOrbRuleYes &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,4,3,NULL) == PHOrbRuleNo &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,5,3,NULL) == PHOrbRuleYes,
            "both Magnards preserve the opening's first two puzzles and use observed accepted intervals after restart");
    }
    uint8_t next[PHOrbMaxSkillGroups] = {0};
    NSArray *order = PHOrbSkillRuleOrder(rules,next);
    Check([order isEqual:@[@0,@1,@2,@3,@4,@5,@6,@7,@8,@9]],
        "the initial order cleanses, selects leader before helper, then buffs, bypasses and supplies");
    next[0] = 3;
    order = PHOrbSkillRuleOrder(rules,next);
    Check([order isEqual:@[@0,@1,@3,@2,@4,@5,@6,@7,@8,@9]],
        "after the leader accepts a skill the shared group prefers the helper without moving supply ahead of cleanse");
    for (unsigned effect = 0; effect < 3; ++effect) {
        state.automation.enemyEffects = (uint8_t)(1u << effect);
        state.automation.playerBypass = 0;
        for (unsigned i = 0; i < 3; ++i) Check(
            PHOrbTriggerSkillRuleWithTiming(rules[5+i],&state,1,1,0,NULL) == (i == effect ? PHOrbRuleYes : PHOrbRuleNo),
            "each Volken rescue independently handles its own enemy effect");
        state.automation.playerBypass = (uint8_t)(1u << effect);
        Check(PHOrbTriggerSkillRuleWithTiming(rules[5+effect],&state,1,1,0,NULL) == PHOrbRuleNo,
            "confirmed native team bypass suppresses the corresponding required rescue");
    }
}
