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

static void MagnardFullPlanTests(void) {
    NSString *directory = @"ios/templates/magnard-gamewith-589197/";
    NSData *data = [NSData dataWithContentsOfFile:[directory stringByAppendingString:@"team-plan.json"]];
    NSDictionary *plan = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    NSDictionary *profile = PHOrbProfileFromPlan(plan,NULL);
    Check(profile && PHOrbProfileNeedsOpeningReceipts(profile) && [profile[@"opening"] count] == 7 &&
        [profile[@"team"] isEqual:@[@14359,@14407,@14422,@14389,@14383,@14359]],
        "Magnard full plan starts with the base team and requires all seven opening receipts");
    if (!profile) return;
    NSString *reason = @"previous import failure";
    NSDictionary *imported = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),data,&reason);
    Check([imported[@"profiles"][0] isEqual:profile] && !reason &&
        [imported[@"selected"] isEqual:profile[@"id"]],
        "the UI data import accepts the complete Magnard plan and clears any previous failure reason");
    Check([PHOrbProfileFromPlan(PHOrbPlanFromProfile(profile),NULL) isEqual:profile],
        "native body binding survives complete-plan import/export without becoming a wildcard");
    NSData *legacyData = [NSData dataWithContentsOfFile:[directory stringByAppendingString:@"post-opening.json"]];
    NSDictionary *legacy = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),legacyData,NULL);
    NSDictionary *library = PHOrbProfilesImportingPlan(legacy,plan,NULL);
    Check([library[@"profiles"] count] == 2 && [library[@"selected"] isEqual:profile[@"id"]] &&
        [profile[@"settings"] isEqual:legacy[@"profiles"][0][@"settings"]],
        "full plan has a new ID and preserves the reviewed post-opening rotation and board goals");
    NSArray *slots = @[@5,@1,@3,@4,@2,@1,@6];
    NSArray *skills = @[@67052,@67000,@67034,@67016,@67069,@0,@67000];
    for (NSUInteger i = 0; i < 7; ++i) {
        NSDictionary *step = profile[@"opening"][i];
        Check([step[@"slot"] isEqual:slots[i]] && [step[@"skill"] isEqual:skills[i]] &&
            [step[@"source"] isEqual:@"body"] && [step[@"bindBodySkill"] boolValue] == (i == 5) &&
            ![step[@"consumeAssist"] boolValue] && ![step[@"puzzleAfter"] boolValue],
            "opening uses the observed transform IDs, explicitly binds only step six, and has no intervening puzzle or assist");
    }
    for (NSDictionary *changes in @[@{@"bindBodySkill":@NO}, @{@"bindBodySkill":@1},
        @{@"bindBodySkill":@"true"}, @{@"skill":@67000}, @{@"source":@"assist"},
        @{@"source":@"body_or_assist"}, @{@"afterMonster":@14360}, @{@"consumeAssist":@YES}]) {
        NSMutableDictionary *bad = [plan mutableCopy]; NSMutableArray *opening = [plan[@"opening"] mutableCopy];
        NSMutableDictionary *step = [opening[5] mutableCopy]; [step addEntriesFromDictionary:changes];
        opening[5] = step; bad[@"opening"] = opening;
        Check(!PHOrbProfileFromPlan(bad,&reason) && [reason containsString:@"開場第 6 步"],
            "invalid native binding is rejected with the sixth opening step identified");
    }
    for (NSDictionary *changes in @[@{@"schema":@2}, @{@"target":@"foreign"}, @{@"formation":NSNull.null}]) {
        NSMutableDictionary *invalid = [plan mutableCopy]; [invalid addEntriesFromDictionary:changes];
        NSData *encoded = [NSJSONSerialization dataWithJSONObject:invalid options:0 error:NULL];
        Check(!PHOrbProfilesImportingData(PHOrbDefaultProfiles(),encoded,&reason) &&
            [reason containsString:[changes.allKeys firstObject]],
            "file import identifies schema, game target or formation independently");
    }
    NSMutableDictionary *bad = [plan mutableCopy]; NSMutableArray *opening = [plan[@"opening"] mutableCopy];
    NSMutableDictionary *step = [opening[0] mutableCopy]; step[@"skill"] = @0; opening[0] = step; bad[@"opening"] = opening;
    Check(!PHOrbProfileFromPlan(bad,NULL), "ordinary opening steps still require an explicit nonzero native skill ID");
    PHOrbSnapshot state = {0}; state.valid = state.present = state.automation.valid = true;
    for (unsigned i = 0; i < 6; ++i) {
        state.automation.members[i].active = true;
        state.automation.members[i].number = [profile[@"team"][i] unsignedIntValue];
        PHBattleCatalogIDForNumber([profile[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue],
            &state.automation.members[i].assistCatalogID);
    }
    Check([PHOrbResolveProfile(library,&state)[@"ok"] boolValue], "full Magnard plan resolves before the first transform");
    for (NSUInteger i = 0; i < 7; ++i) {
        NSDictionary *current = profile[@"opening"][i];
        if ([current[@"afterMonster"] unsignedIntValue])
            state.automation.members[[current[@"slot"] unsignedIntValue]-1].number = [current[@"afterMonster"] unsignedIntValue];
        Check(PHOrbProfileMatchesOpeningTeam(profile,&state,i+1) && !PHOrbProfileManualOpeningProgress(profile,&state) &&
            ![PHOrbResolveProfile(library,&state)[@"ok"] boolValue],
            "observed forms match an explicit receipt cursor but cannot infer completion or skip the non-transform sixth step");
    }
    Check([PHOrbResolveProfile(legacy,&state)[@"ok"] boolValue], "the separately selected manual-opening profile retains its existing behavior");
}
