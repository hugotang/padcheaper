static NSMutableDictionary *ReadPlanFixture(void) {
    NSData *data = [NSData dataWithContentsOfFile:@"ios/templates/ishida-uryu-gamewith-574233/team-plan.json"];
    return data ? [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL] : nil;
}
static NSMutableDictionary *ReadCielPlanFixture(NSString *name) {
    NSString *path = [@"ios/templates/halloween-ciel-gamewith-579537/" stringByAppendingString:name];
    NSData *data = [NSData dataWithContentsOfFile:path];
    return data ? [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL] : nil;
}
static void UryuFirstPuzzlePlanTests(void) {
    for (NSString *name in @[@"team-plan.json",@"team-plan-heart-l.json"]) {
        NSData *data = [NSData dataWithContentsOfFile:[@"ios/templates/ishida-uryu-gamewith-574233/" stringByAppendingString:name]];
        NSDictionary *plan = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
        NSDictionary *profile = PHOrbProfileFromPlan(plan,NULL);
        Check(profile && [PHOrbProfileFromPlan(PHOrbPlanFromProfile(profile),NULL) isEqual:profile],
            "both Uryu goal variants round-trip with delayed normal Byakuya rules and immediate supply alternatives");
        if (!profile) continue;
        const unsigned hearts = [name isEqual:@"team-plan.json"] ? 4 : 5;
        NSArray *rules = profile[@"settings"][@"rules"];
        Check(rules.count == 11 && [profile[@"opening"] count] == 6 && ![[profile[@"opening"] lastObject][@"puzzleAfter"] boolValue],
            "Uryu retains all six opening steps and still evaluates skills/rescues on the first puzzle");
        if (rules.count != 11) continue;
        Check([rules[0][@"slot"] isEqual:@2] && [rules[7][@"slot"] isEqual:@4] &&
            [rules[8][@"required"] boolValue] && [rules[9][@"required"] boolValue] && [rules[10][@"atLeast"] isEqual:@9],
            "Yoruichi remains before Uryu and both required Unohana rescues remain independent");
        for (NSUInteger i = 1; i <= 6; ++i) Check([rules[i][@"group"] isEqual:@1] &&
            [rules[i][@"intervalMode"] isEqual:@"accepted"] && [rules[i][@"every"] isEqual:@1] &&
            [rules[i][@"startAfter"] isEqual:(i <= 2 ? @1 : @0)] &&
            [rules[i][@"slot"] unsignedIntValue] == (i % 2 ? 5 : 6),
            "normal and shortage alternatives share one Byakuya group without delaying first-puzzle rescue");
        PHOrbSnapshot state = {0}; state.valid = state.settled = state.automation.valid = state.automation.boardKnown = true;
        state.board.width = 6; state.board.height = 5;
        for (unsigned slot = 0; slot < 6; ++slot) {
            state.automation.members[slot].active = true;
            state.automation.members[slot].number = slot >= 4 ? 14065 : 100 + slot;
        }
        for (unsigned i = 0; i < 30; ++i) state.board.types[i] = i < 9 ? 3 : i < 9 + hearts ? 5 : 0;
        for (NSUInteger i = 1; i <= 6; ++i)
            Check(PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,1,0,NULL) == PHOrbRuleNo,
                "sufficient first-puzzle light and goal-specific hearts preserve both Byakuya skills");
        state.board.types[8] = 0;
        Check(PHOrbTriggerSkillRuleWithTiming(rules[3],&state,1,1,0,NULL) == PHOrbRuleYes &&
            PHOrbTriggerSkillRuleWithTiming(rules[4],&state,1,1,0,NULL) == PHOrbRuleYes,
            "first-puzzle light shortage may request either ready group member");
        state.board.types[8] = 3; state.board.types[9 + hearts - 1] = 0;
        Check(PHOrbTriggerSkillRuleWithTiming(rules[5],&state,1,1,0,NULL) == PHOrbRuleYes &&
            PHOrbTriggerSkillRuleWithTiming(rules[6],&state,1,1,0,NULL) == PHOrbRuleYes,
            "heart shortage is an independent OR alternative rather than requiring light shortage too");
        state.board.types[9 + hearts - 1] = 5;
        Check(PHOrbTriggerSkillRuleWithTiming(rules[1],&state,1,2,0,NULL) == PHOrbRuleYes &&
            PHOrbTriggerSkillRuleWithTiming(rules[2],&state,1,3,2,NULL) == PHOrbRuleYes,
            "normal supply starts on observed turn two and continues despite displayed-run restarts");
        state.board.spinnerMask = UINT64_C(1) << 8; state.board.spinnerColors = 63;
        Check(PHOrbTriggerSkillRuleWithTiming(rules[3],&state,1,1,0,NULL) == PHOrbRuleUnknown,
            "a spinner-dependent ninth light is not treated as guaranteed first-puzzle supply");
    }
}
static void ConsumingAssistPlanTests(void) {
    for (NSString *name in @[@"recommended.team-plan.json", @"kenpachi-1.team-plan.json", @"kenpachi-2.team-plan.json"]) {
        NSMutableDictionary *plan = ReadCielPlanFixture(name);
        NSDictionary *profile = PHOrbProfileFromPlan(plan, NULL);
        Check(profile && [PHOrbProfileFromPlan(PHOrbPlanFromProfile(profile), NULL) isEqual:profile],
            "CIEL article plans retain their consumed assists and first-puzzle policy on roundtrip");
        if (!profile) continue;
        if ([name isEqual:@"kenpachi-2.team-plan.json"]) {
            NSArray *rules = profile[@"settings"][@"rules"];
            for (NSUInteger i = 4; i < rules.count; ++i) Check(
                [rules[i][@"intervalMode"] isEqual:@"accepted"] &&
                [rules[i][@"startAfter"] isEqual:(i < 6 ? @2 : @0)] &&
                [rules[i][@"every"] isEqual:(i < 6 ? @2 : @1)],
                "full Kenpachi plan preserves two warmup turns while leaving same-group rescue active immediately");
        }
        PHOrbSnapshot state = {0}; state.valid = state.automation.valid = true;
        for (unsigned i = 0; i < 6; ++i) {
            PHOrbSkillMember *m = &state.automation.members[i]; m->active = true; m->assistState = 172;
            m->number = [plan[@"formation"][@"slots"][i][@"body"][@"id"] unsignedIntValue];
            PHBattleCatalogIDForNumber([plan[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue], &m->assistCatalogID);
            m->sourceCatalogIDs[1] = m->assistCatalogID;
        }
        Check(PHOrbProfileMatchesOpeningTeam(profile, &state, 0), "CIEL opening matches the initial equipped team");
        for (NSUInteger i = 0; i < [plan[@"opening"] count]; ++i) {
            NSDictionary *step = plan[@"opening"][i];
            PHOrbSkillMember *m = &state.automation.members[[step[@"slot"] unsignedIntegerValue] - 1];
            if ([step[@"afterMonster"] unsignedIntValue]) m->number = [step[@"afterMonster"] unsignedIntValue];
            if ([step[@"consumeAssist"] boolValue]) {
                uint16_t assist = m->assistCatalogID;
                m->assistCatalogID = 0;
                Check(!PHOrbProfileMatchesOpeningTeam(profile, &state, i + 1), "a merely missing active assist is not a consumption receipt");
                m->assistState = 0;
                Check(!PHOrbProfileMatchesOpeningTeam(profile, &state, i), "consumption cannot match an earlier unconfirmed step");
                ++m->sourceCatalogIDs[1];
                Check(!PHOrbProfileMatchesOpeningTeam(profile, &state, i + 1), "consumption still binds the exact source weapon");
                m->sourceCatalogIDs[1] = assist;
            }
            Check(PHOrbProfileMatchesOpeningTeam(profile, &state, i + 1), "each CIEL opening transition matches only its confirmed progress");
        }
        Check(PHOrbProfileMatchesRunningTeam(profile, &state), "consumed article assists remain absent during the loop");
        for (unsigned slot = 0; slot < 6; ++slot) {
            PHOrbSkillMember *m = &state.automation.members[slot];
            if (m->assistCatalogID) continue;
            m->identityNumber = m->number; m->identityAssist = m->sourceCatalogIDs[1];
            m->number = 4264; m->temporaryKnown = true; m->temporaryTurns = 2;
            PHBattleCatalogIDForNumber(4264, &m->temporaryID);
            Check(PHOrbProfileMatchesRunningTeam(profile, &state), "native temporary replacement preserves the consumed source identity");
            ++m->identityAssist;
            Check(!PHOrbProfileMatchesRunningTeam(profile, &state), "temporary replacement cannot hide a changed consumed source");
            --m->identityAssist; m->number = m->identityNumber; m->temporaryTurns = 0;
        }
    }
    NSMutableDictionary *plan = ReadCielPlanFixture(@"recommended.team-plan.json");
    for (id invalid in @[@1, @"true", NSNull.null]) {
        NSMutableDictionary *bad = ReadCielPlanFixture(@"recommended.team-plan.json");
        bad[@"opening"][8][@"consumeAssist"] = invalid;
        Check(!PHOrbProfileFromPlan(bad, NULL), "consumption flags require JSON Boolean values");
        bad = ReadCielPlanFixture(@"recommended.team-plan.json"); bad[@"opening"][9][@"puzzleAfter"] = invalid;
        Check(!PHOrbProfileFromPlan(bad, NULL), "first-puzzle flags require JSON Boolean values");
    }
    NSMutableDictionary *bad = ReadCielPlanFixture(@"recommended.team-plan.json"); bad[@"opening"][0][@"puzzleAfter"] = @YES;
    Check(!PHOrbProfileFromPlan(bad, NULL), "first puzzle may follow only the final opening step");
    bad = ReadCielPlanFixture(@"recommended.team-plan.json"); bad[@"opening"][9][@"consumeAssist"] = @YES;
    Check(!PHOrbProfileFromPlan(bad, NULL), "body skills cannot declare assist consumption");
    bad = ReadCielPlanFixture(@"recommended.team-plan.json"); bad[@"opening"][8][@"afterMonster"] = @14189;
    Check(!PHOrbProfileFromPlan(bad, NULL), "assist consumption cannot declare a simultaneous body transformation");
    Check([PHOrbPlanDescription(plan) containsString:@"確認武裝消耗"] && [PHOrbPlanDescription(plan) containsString:@"先轉此盤"],
        "import preview exposes both the consumed weapon and first-puzzle policy");
    // A later body use on the consumed slot requires assist zero; retaining
    // the old ID would wrongly imply the original weapon is still active.
    NSMutableDictionary *last = [[plan[@"opening"] lastObject] mutableCopy]; [last removeObjectForKey:@"puzzleAfter"];
    NSMutableArray *steps = [plan[@"opening"] mutableCopy]; steps[steps.count - 1] = last;
    [steps addObject:@{@"slot":@4,@"source":@"body",@"monster":@14190,@"assist":@0,@"skill":@65757,@"afterMonster":@0}];
    plan[@"opening"] = steps;
    Check(PHOrbProfileFromPlan(plan, NULL) != nil, "later opening body steps may name the now-consumed assist as zero");
    steps[steps.count - 1] = @{@"slot":@4,@"source":@"assist",@"monster":@14190,@"assist":@14191,@"skill":@65884,@"afterMonster":@0};
    plan[@"opening"] = steps;
    Check(!PHOrbProfileFromPlan(plan, NULL), "an opening cannot reuse a consumed assist");
    for (NSString *name in @[@"recommended.post-opening.json", @"kenpachi-1.post-opening.json", @"kenpachi-2.post-opening.json"]) {
        NSString *path = [@"ios/templates/halloween-ciel-gamewith-579537/" stringByAppendingString:name];
        NSData *data = [NSData dataWithContentsOfFile:path];
        NSDictionary *library = PHOrbProfilesImportingData(PHOrbDefaultProfiles(), data, NULL);
        Check(library && !library[@"profiles"][0][@"formation"] && !library[@"profiles"][0][@"opening"],
            "manual-opening compatibility files import as ordinary profiles without replaying weapons");
        NSDictionary *portable = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
        for (NSDictionary *rule in portable[@"profile"][@"settings"][@"rules"])
            Check(!rule[@"intervalMode"] && !rule[@"startAfter"], "0.5.11 compatibility files retain their supported rule keys");
    }
}
static void SingleKenpachiPlanTests(void) {
    NSDictionary *plan = ReadCielPlanFixture(@"kenpachi-1.team-plan.json");
    NSDictionary *profile = PHOrbProfileFromPlan(plan, NULL);
    Check(profile && [plan[@"id"] isEqual:@"halloween-ciel-gw579537-kenpachi-1"] &&
        [profile[@"team"] isEqual:@[@14184,@14200,@14175,@14042,@14061,@14189]],
        "single Kenpachi has a separate strategy identity and the evolved Unohana member");
    if (!profile) return;
    NSArray *steps = profile[@"opening"], *rules = profile[@"settings"][@"rules"];
    Check(steps.count == 7 && rules.count == 9, "single Kenpachi keeps seven opening rows and nine loop/rescue rules");
    if (steps.count != 7 || rules.count != 9) return;
    Check([steps[4][@"slot"] isEqual:@6] && [steps[4][@"consumeAssist"] boolValue] &&
        [steps[6][@"slot"] isEqual:@5] && [steps[6][@"skill"] isEqual:@63375] &&
        [steps[6][@"consumeAssist"] boolValue] && ![steps[6][@"puzzleAfter"] boolValue],
        "single Kenpachi consumes Euphyllis then Unohana weapons and enters the required supply loop on the first puzzle");
    for (NSUInteger i = 1; i <= 3; ++i)
        Check([rules[i][@"required"] boolValue] && [rules[i][@"color"] isEqual:@-1] &&
            [rules[i][@"slot"] unsignedIntValue] == (i == 1 ? 6 : i - 1),
            "all three CIEL suppliers are mandatory even when the transformation already made enough orbs");
    Check([rules[4][@"slot"] isEqual:@5] && [rules[4][@"skill"] isEqual:@64730] && ![rules[4][@"required"] boolValue] &&
        [rules[5][@"required"] boolValue] && [rules[5][@"status"] isEqual:@"awoken_bind"] &&
        [rules[6][@"required"] boolValue] && [rules[6][@"unmatchableColors"] isEqual:@48] &&
        [rules[4][@"group"] isEqual:@0] && [rules[5][@"group"] isEqual:@0] && [rules[6][@"group"] isEqual:@0],
        "optional Unohana healing does not confirm away residual awoken-bind or dark/heart-unmatchable rescue");
    for (NSUInteger i = 7; i < 9; ++i)
        Check([rules[i][@"slot"] isEqual:@4] && [rules[i][@"monster"] isEqual:@14043] &&
            [rules[i][@"required"] boolValue] && ![rules[i][@"enemy"] isEqual:@"none"],
            "the only Kenpachi is reserved for absorption rather than copied into a fixed two-member loop");
    NSData *postData = [NSData dataWithContentsOfFile:@"ios/templates/halloween-ciel-gamewith-579537/kenpachi-1.post-opening.json"];
    NSDictionary *post = postData ? [NSJSONSerialization JSONObjectWithData:postData options:0 error:NULL] : nil;
    Check([post[@"profile"][@"team"] isEqual:@[@14184,@14200,@14175,@14043,@14061,@14190]] &&
        [post[@"profile"][@"settings"][@"rules"] isEqual:plan[@"rotation"]],
        "manual-opening single Kenpachi profile binds transformed members and the same supplier/rescue order");
}
static void PlanTests(void) {
    UryuFirstPuzzlePlanTests();
    ConsumingAssistPlanTests();
    SingleKenpachiPlanTests();
    NSMutableDictionary *plan = ReadPlanFixture(); NSString *reason = nil;
    NSDictionary *profile = PHOrbProfileFromPlan(plan, &reason);
    Check(profile && !reason && [profile[@"opening"] count] == 6, "complete Uryu template validates all six opening steps");
    if (!profile) return;
    NSDictionary *roundtrip = PHOrbProfileFromPlan(PHOrbPlanFromProfile(profile), &reason);
    Check([profile isEqual:roundtrip], "plan export/import preserves formation, opening, rotation and goals");
    NSMutableDictionary *thornPlan = ReadPlanFixture(); thornPlan[@"board"][@"thornComboPriority"] = @YES;
    NSDictionary *thornProfile = PHOrbProfileFromPlan(thornPlan, &reason);
    Check(thornProfile && [PHOrbPlanFromProfile(thornProfile)[@"board"][@"thornComboPriority"] isEqual:@YES] &&
        [PHOrbProfileFromPlan(PHOrbPlanFromProfile(thornProfile), NULL) isEqual:thornProfile],
        "full team-plan roundtrip preserves the optional thorn Combo preference");
    thornPlan[@"board"][@"thornComboPriority"] = @1;
    Check(!PHOrbProfileFromPlan(thornPlan, NULL), "plan import rejects numeric or malformed thorn preference");
    thornPlan[@"board"][@"thornComboPriority"] = @NO; thornPlan[@"board"][@"unknown"] = @YES;
    Check(!PHOrbProfileFromPlan(thornPlan, NULL), "adding a known optional board key does not allow unknown settings");
    NSMutableDictionary *poisonPlan = ReadPlanFixture(); poisonPlan[@"board"][@"allowPoisonWithHeartFour"] = @YES;
    NSDictionary *poisonProfile = PHOrbProfileFromPlan(poisonPlan, &reason);
    Check(poisonProfile && [PHOrbPlanFromProfile(poisonProfile)[@"board"][@"allowPoisonWithHeartFour"] isEqual:@YES] &&
        [PHOrbProfileFromPlan(PHOrbPlanFromProfile(poisonProfile), NULL) isEqual:poisonProfile],
        "full team plans retain the optional normal-poison-with-hearts policy on roundtrip");
    poisonPlan[@"board"][@"allowPoisonWithHeartFour"] = @1;
    Check(!PHOrbProfileFromPlan(poisonPlan, NULL), "plan import rejects numeric poison permission");
    NSMutableDictionary *diagonalPlan = ReadPlanFixture(); diagonalPlan[@"board"][@"diagonal"] = @YES;
    NSDictionary *diagonalProfile = PHOrbProfileFromPlan(diagonalPlan, &reason);
    Check(diagonalProfile && [PHOrbPlanFromProfile(diagonalProfile)[@"board"][@"diagonal"] isEqual:@YES] &&
        [PHOrbProfileFromPlan(PHOrbPlanFromProfile(diagonalProfile), NULL) isEqual:diagonalProfile] &&
        [PHOrbPlanDescription(diagonalPlan) containsString:@"斜向轉珠：開啟"],
        "complete team-plan import/export and review summary retain the optional diagonal gesture policy");
    diagonalPlan[@"board"][@"diagonal"] = @1;
    Check(!PHOrbProfileFromPlan(diagonalPlan, NULL), "team plans reject numeric diagonal permission");
    NSDictionary *library = PHOrbProfilesImportingPlan(PHOrbDefaultProfiles(),plan,&reason);
    Check(PHOrbProfilesValid(library) && [library[@"selected"] isEqual:plan[@"id"]] && ![library[@"autoSelect"] boolValue],
        "import selects an explicitly reviewed plan without automatic start");
    Check(!PHOrbProfilesImportingPlan(library,plan,&reason) && [reason containsString:@"ID"], "duplicate import cannot overwrite an existing strategy");
    NSDictionary *formations = PHFormationLibraryFromTeam(nil);
    NSDictionary *selected = PHOrbFormationsSelectingProfile(formations,profile);
    NSDictionary *selectedTeam = nil;
    Check(PHFormationReadLibrary(selected,&selectedTeam,NULL) && [selected[@"teams"] count] == 2 &&
        [selectedTeam isEqual:plan[@"formation"]] && [formations[@"teams"] count] == 1,
        "plan selection adds a team without mutating or replacing an existing team");
    Check([PHOrbFormationsSelectingProfile(selected,profile) isEqual:selected], "selecting the same plan reuses its saved team");
    for (NSDictionary *change in @[@{@"format":@"other"}, @{@"schema":@2}, @{@"target":@"unknown"},
        @{@"opening":@[@{@"action":@"tap"}]}, @{@"unknown":@YES}, @{@"rotation":@[@{@"action":@"use"}]}]) {
        NSMutableDictionary *bad = ReadPlanFixture(); [bad addEntriesFromDictionary:change];
        Check(!PHOrbProfileFromPlan(bad,NULL), "unsupported format, version, action or conditions reject the entire import");
    }
    NSMutableDictionary *bad = ReadPlanFixture(); bad[@"opening"][1][@"monster"] = @14038;
    Check(!PHOrbProfileFromPlan(bad,NULL), "opening precondition must match the formation and preceding transformations");
    bad = ReadPlanFixture(); bad[@"opening"][0][@"assist"] = @0;
    Check(!PHOrbProfileFromPlan(bad,NULL), "assist steps require the exact equipped assist");
    PHOrbSnapshot state = {0}; state.present = state.valid = state.automation.valid = true;
    for (unsigned i = 0; i < 6; ++i) {
        state.automation.members[i].active = true;
        state.automation.members[i].number = [plan[@"formation"][@"slots"][i][@"body"][@"id"] unsignedIntValue];
        PHBattleCatalogIDForNumber([plan[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue], &state.automation.members[i].assistCatalogID);
    }
    Check(PHOrbProfileMatchesOpeningTeam(profile,&state,0), "opening binds six bodies and assists");
    state.automation.members[0].number = 14038;
    Check(!PHOrbProfileMatchesOpeningTeam(profile,&state,0) && PHOrbProfileMatchesOpeningTeam(profile,&state,2),
        "transformation only matches the confirmed opening progress, never a fresh opening");
    ++state.automation.members[5].assistCatalogID;
    Check(!PHOrbProfileMatchesOpeningTeam(profile,&state,2), "same body with a different assist cannot resume a plan");
    --state.automation.members[5].assistCatalogID;
    PHOrbSkillMember *member = &state.automation.members[0];
    member->identityNumber = 14038; member->identityAssist = member->assistCatalogID;
    member->number = 4264; member->temporaryKnown = true; member->temporaryTurns = 3;
    PHBattleCatalogIDForNumber(4264, &member->temporaryID); member->assistCatalogID = 0;
    Check(PHOrbProfileMatchesRunningTeam(profile,&state) && !PHOrbProfileMatchesOpeningTeam(profile,&state,2),
        "native temporary form retains final plan identity while the opening remains strict");
    member->temporaryKnown = false;
    Check(!PHOrbProfileMatchesRunningTeam(profile,&state), "unverified temporary identity cannot bypass the team gate");
    member->temporaryKnown = true; member->identityNumber = 14037;
    Check(!PHOrbProfileMatchesRunningTeam(profile,&state), "temporary identity must match the completed transformation");
    member->identityNumber = 14038; ++member->identityAssist;
    Check(!PHOrbProfileMatchesRunningTeam(profile,&state), "temporary form cannot hide an actual change of underlying assist");
    --member->identityAssist; member->temporaryTurns = 0;
    Check(!PHOrbProfileMatchesRunningTeam(profile,&state), "a persistent unexpected substitution still stops the plan");
    member->number = 14038; member->assistCatalogID = member->identityAssist;
    Check(PHOrbProfileMatchesRunningTeam(profile,&state), "restored original form resumes the same completed plan");
}
