// Schema and pure policy cases only; running these awaits explicit build/test.
static NSMutableDictionary *FixedFixture(void) {
    NSMutableArray *members = [NSMutableArray array];
    for (unsigned i = 0; i < 6; ++i) [members addObject:@{@"body":@100,@"assist":@0,
        @"forms":@[@{@"number":@100,@"skills":@[@700]},@{@"number":@101,@"skills":@[@701]}],@"assistSkills":@[]}];
    NSDictionary *action = @{@"kind":@"skill",@"slot":@1,@"source":@"body",@"monster":@100,@"skill":@700,
        @"afterMonster":@101,@"optional":@NO,@"frequency":@"once",@"when":@"always"};
    NSDictionary *stage = @{@"id":@"attack",@"actions":@[action],
        @"board":@{@"policy":@"six_colors_fours",@"assistRecoveryL":@YES,@"maxGestureMS":@4500},
        @"repeat":@{@"minimumPuzzles":@1,@"maximumPuzzles":@30,@"until":@{@"kind":@"floor_change"}}};
    NSDictionary *plan = @{@"schema":@1,@"members":members,@"floors":@[@{@"number":@1,
        @"variants":@[@{@"kind":@"any",@"stages":@[stage]}]}]};
    NSDictionary *profile = @{@"id":@"fixed-fixture",@"name":@"固定隊伍",@"team":@[@100,@100,@100,@100,@100,@100],
        @"dungeon":@0,@"floor":@0,@"source":@"https://example.invalid/fixed",@"fixedPlan":plan,
        @"settings":@{@"quality":@"thorough",@"mode":@"combo",@"goals":@[],@"rules":@[]}};
    NSData *data = [NSJSONSerialization dataWithJSONObject:@{@"format":@"padcheaper-fixed-plan",@"schema":@1,
        @"target":@(PHTargetID),@"profile":profile} options:0 error:NULL];
    return [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL];
}
static void FixedProfilesTests(void) {
    NSData *coralData=[NSData dataWithContentsOfFile:@"ios/templates/coral-fixed-gamewith-585607/fixed-plan.json"];
    NSDictionary *coralPackage=coralData ? [NSJSONSerialization JSONObjectWithData:coralData options:0 error:NULL] : nil;
    NSDictionary *coral=PHOrbFixedPlanFromPackage(coralPackage,NULL);
    Check(coral && [coral[@"fixedPlan"][@"floors"] count]==15 &&
        [coral[@"fixedPlan"][@"leaderSwap"] isEqual:@5],"actual Coral package imports all fifteen floors and declared swap");
    NSMutableDictionary *package = FixedFixture(); NSString *reason = nil;
    NSDictionary *profile = PHOrbFixedPlanFromPackage(package,&reason);
    NSData *exported = PHOrbExportProfileData(profile,&reason);
    NSDictionary *library = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),exported,&reason);
    Check(profile && library && !reason && [library[@"selected"] isEqual:@"fixed-fixture"] &&
        ![library[@"autoSelect"] boolValue] && !library[@"profiles"][0][@"formation"],
        "fixed plans import without formation replacement or automatic start");
    Check([PHOrbExportProfileData(library[@"profiles"][0],NULL) isEqual:exported],
        "fixed plan stages and native identities roundtrip canonically");
    Check(!PHOrbProfilesImportingData(library,exported,&reason) && [reason containsString:@"ID"],
        "fixed plan duplicate import preserves the original record");
    NSMutableDictionary *ordinary = [package mutableCopy]; ordinary[@"format"] = @"padcheaper-orb-profile";
    Check(!PHOrbProfilesImportingData(PHOrbDefaultProfiles(),[NSJSONSerialization dataWithJSONObject:ordinary options:0 error:NULL],NULL),
        "ordinary format cannot smuggle fixed execution stages");
    NSMutableDictionary *optionalTarget=FixedFixture();
    optionalTarget[@"profile"][@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0][@"actions"]=
        @[@{@"kind":@"target",@"target":@"part",@"monster":@0,@"optional":@YES,@"frequency":@"once"}];
    Check(!PHOrbFixedPlanFromPackage(optionalTarget,NULL),"unsupported optional targets cannot silently skip a required enemy selection");
    for (NSString *mutation in @[@"unknown-key",@"source",@"skill",@"form",@"gesture",@"repeat",@"missing-floor",@"mixed-variants",@"boolean",@"profile-team",@"rules",@"no-last-exit"]) {
        NSMutableDictionary *bad = FixedFixture(), *p = bad[@"profile"], *plan = p[@"fixedPlan"];
        NSMutableDictionary *stage = plan[@"floors"][0][@"variants"][0][@"stages"][0], *action = stage[@"actions"][0];
        if ([mutation isEqual:@"unknown-key"]) plan[@"timerEdit"] = @YES;
        if ([mutation isEqual:@"source"]) action[@"source"] = @"auto";
        if ([mutation isEqual:@"skill"]) action[@"skill"] = @702;
        if ([mutation isEqual:@"form"]) action[@"afterMonster"] = @102;
        if ([mutation isEqual:@"gesture"]) stage[@"board"][@"maxGestureMS"] = @4501;
        if ([mutation isEqual:@"repeat"]) stage[@"repeat"][@"maximumPuzzles"] = @0;
        if ([mutation isEqual:@"missing-floor"]) plan[@"floors"][0][@"number"] = @2;
        if ([mutation isEqual:@"mixed-variants"]) plan[@"floors"][0][@"variants"] = @[
            plan[@"floors"][0][@"variants"][0],@{@"kind":@"parts",@"stages":@[stage]}];
        if ([mutation isEqual:@"boolean"]) action[@"optional"] = @1;
        if ([mutation isEqual:@"profile-team"]) p[@"team"][0] = @101;
        if ([mutation isEqual:@"rules"]) p[@"settings"][@"rules"] = @[PHOrbDefaultSkillRule()];
        if ([mutation isEqual:@"no-last-exit"]) stage[@"repeat"][@"until"] = @{@"kind":@"puzzles"};
        Check(!PHOrbFixedPlanFromPackage(bad,NULL),"fixed plans reject undeclared sources, unsafe bounds, shape keys and conflicting execution models");
    }
    PHOrbSnapshot state = {0}; state.valid = state.present = state.automation.valid = true;
    state.identity.entryMode = 1; state.automation.enemiesKnown = true;
    for (unsigned i = 0; i < 6; ++i) {
        state.automation.members[i].active = state.automation.members[i].temporaryKnown = true;
        state.automation.members[i].number = 100; state.automation.members[i].sourceCatalogIDs[0] = 100;
    }
    Check(PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"fixed plan matches its six declared native sources");
    state.automation.members[0].number = 101;
    Check(PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"declared native transform remains bound to original fixed source");
    state.automation.members[0].sourceCatalogIDs[0] = 101;
    Check(!PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"same displayed form cannot hide a changed native source");
    state.automation.members[0].sourceCatalogIDs[0] = 100;
    state.automation.members[0].temporaryTurns = 1;
    Check(!PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"temporary replacements cannot authorize fixed-plan actions");
    PHOrbFixedPlanEvidence evidence = {0};
    NSDictionary *repeat = @{@"minimumPuzzles":@2,@"maximumPuzzles":@8,@"until":@{@"kind":@"effects_clear",@"effects":@[@"awoken_bind",@"assist_void"]}};
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&evidence)==PHOrbFixedPlanNo,
        "stage never advances before required confirmed puzzle count");
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,2,0,&evidence)==PHOrbFixedPlanUnknown,
        "missing native effect evidence cannot finish a stage");
    evidence.statusKnown = evidence.assistVoidKnown = true;
    evidence.assistVoidTurns = 1;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,2,0,&evidence)==PHOrbFixedPlanNo,
        "one remaining native effect retains the current stage");
    evidence.assistVoidTurns = 0;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,2,0,&evidence)==PHOrbFixedPlanYes,
        "all fresh native effects cleared can advance after confirmed puzzles");
    repeat = @{@"minimumPuzzles":@0,@"maximumPuzzles":@8,@"until":@{@"kind":@"effects_clear",@"effects":@[@"damage_absorb"]}};
    evidence.enemiesKnown = true; evidence.enemyEffects = evidence.playerBypass = PHOrbEnemyDamageAbsorb;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,0,&evidence)==PHOrbFixedPlanYes,
        "confirmed native bypass can finish a condition-only stage before its puzzle");
    repeat = @{@"minimumPuzzles":@1,@"maximumPuzzles":@8,@"until":@{@"kind":@"enemy_hp_at_most",@"percent":@50}};
    evidence.enemiesKnown = true; evidence.aliveMask = evidence.hpKnownMask = 1;
    evidence.enemyMaximumHP[0] = UINT64_MAX; evidence.enemyHP[0] = UINT64_MAX/2;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&evidence)==PHOrbFixedPlanYes,
        "native HP boundary calculation cannot overflow at maximum uint64 HP");
    ++evidence.enemyHP[0];
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&evidence)==PHOrbFixedPlanNo,
        "one native HP above the threshold does not advance");
    repeat=@{@"minimumPuzzles":@0,@"maximumPuzzles":@8,@"until":@{@"kind":@"skill_ready",@"slot":@5,@"monster":@13828,@"skill":@63192}};
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,12,&evidence)==PHOrbFixedPlanUnknown,"cooldown wait needs native readiness evidence");
    evidence.readinessKnownSlots=16;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,12,&evidence)==PHOrbFixedPlanNo,"known unready remains in B13 stall");
    evidence.readySlots=16;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,12,&evidence)==PHOrbFixedPlanYes,"ready Solomon leaves B13 without an extra puzzle");
    NSDictionary *attack=@{@"kind":@"skill",@"when":@"attack_ready"};
    evidence.enemyEffects=PHOrbEnemyAttributeAbsorb; evidence.playerBypass=0;
    Check(PHOrbFixedPlanActionCondition(attack,&evidence)==PHOrbFixedPlanNo,"absorption stall preserves final Riku skill");
    evidence.playerBypass=PHOrbEnemyAttributeAbsorb;
    Check(PHOrbFixedPlanActionCondition(attack,&evidence)==PHOrbFixedPlanYes,"fresh bypass permits final attack buff");
}
