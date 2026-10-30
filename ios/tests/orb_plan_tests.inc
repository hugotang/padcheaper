static NSMutableDictionary *ReadPlanFixture(void) {
    NSData *data = [NSData dataWithContentsOfFile:@"ios/templates/ishida-uryu-gamewith-574233/team-plan.json"];
    return data ? [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL] : nil;
}
static void PlanTests(void) {
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
