static void AssistProfileTests(void) {
    NSDictionary *plan = ReadPlanFixture();
    NSDictionary *profile = PHOrbProfileFromPlan(plan,NULL);
    Check(profile && [profile[@"settings"][@"assistRecoveryL"] isEqual:@YES], "Uryu full plan explicitly opts into L recovery");
    if (!profile) return;
    Check([PHOrbProfileFromPlan(PHOrbPlanFromProfile(profile),NULL) isEqual:profile],
        "full-plan export and import preserve the independent policy");
    NSDictionary *portable = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),PHOrbExportProfileData(profile,NULL),NULL);
    Check([portable[@"profiles"][0][@"settings"][@"assistRecoveryL"] isEqual:@YES],
        "portable profile data preserves the conditional L setting");
    PHOrbSnapshot original = {0}; original.valid = original.automation.valid = true;
    for (unsigned i = 0; i < 6; ++i) {
        PHOrbSkillMember *member = &original.automation.members[i]; member->active = true; member->assistState = 172;
        member->number = [profile[@"team"][i] unsignedIntValue];
        PHBattleCatalogIDForNumber([profile[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue], &member->assistCatalogID);
        member->sourceCatalogIDs[1] = member->assistCatalogID;
    }
    Check(PHOrbProfileMatchesOpeningTeam(profile,&original,0), "equipped Uryu still binds without a mechanism override");
    for (unsigned condition = 0; condition < 10; ++condition) {
        PHOrbSnapshot state = original; PHOrbSkillMember *member = &state.automation.members[0];
        member->assistCatalogID = 0;
        PHOrbAssistRecoveryState *evidence = &state.assistRecovery;
        evidence->valid = true; evidence->turns = 3; evidence->retainedAssists = 1;
        evidence->sourceAssists[0] = member->sourceCatalogIDs[1]; evidence->assistStates[0] = member->assistState;
        evidence->numbers[0] = member->number;
        if (condition == 1) evidence->valid = false;
        if (condition == 2) evidence->turns = 0;
        if (condition == 3) evidence->dungeonFlags = 8;
        if (condition == 4) member->assistState = 0;
        if (condition == 5) ++member->sourceCatalogIDs[1];
        if (condition == 6) ++evidence->assistStates[0];
        if (condition == 7) evidence->temporaryTurns[0] = 1;
        if (condition == 8) evidence->retainedAssists = 0;
        if (condition == 9) ++evidence->numbers[0];
        Check(PHOrbProfileMatchesOpeningTeam(profile,&state,0) == (condition == 0),
            "missing assist needs fresh positive duration, exact retained source, protected state and current identity");
        if (!condition) {
            NSMutableDictionary *disabled = [profile mutableCopy], *settings = [profile[@"settings"] mutableCopy];
            settings[@"assistRecoveryL"] = @NO; disabled[@"settings"] = settings;
            Check(!PHOrbProfileMatchesOpeningTeam(disabled,&state,0), "disabled policy does not loosen equipment matching");
            member->temporaryKnown = true; member->temporaryID = 4264; member->temporaryTurns = 2;
            member->identityNumber = member->number; member->identityAssist = 0;
            evidence->temporaryTurns[0] = 2;
            NSMutableDictionary *noOpening = [profile mutableCopy]; noOpening[@"opening"] = @[];
            Check(!PHOrbProfileMatchesRunningTeam(noOpening,&state),
                "temporary-form normalization cannot fabricate a missing retained assist receipt");
        }
    }
    NSDictionary *library = Library(@[profile],profile[@"id"],NO);
    NSMutableDictionary *settings = [profile[@"settings"] mutableCopy]; settings[@"assistRecoveryL"] = @NO;
    NSDictionary *updated = PHOrbProfilesUpdatingSettings(library,settings);
    Check([PHOrbProfileNamed(updated,profile[@"id"])[@"settings"][@"assistRecoveryL"] isEqual:@NO] &&
        [PHOrbProfileNamed(library,profile[@"id"])[@"settings"][@"assistRecoveryL"] isEqual:@YES],
        "editing the selected policy yields a new coherent library without mutating the source");
    Check([PHOrbProfileFromPlan(ReadCielPlanFixture(@"kenpachi-1.team-plan.json"),NULL)[@"settings"][@"assistRecoveryL"] isEqual:@NO],
        "single-Kenpachi CIEL retains its existing policy and consumed-assist semantics");
    NSData *data = [NSData dataWithContentsOfFile:@"ios/templates/ishida-uryu-gamewith-574233/post-opening.orbProfiles.json"];
    NSDictionary *post = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    Check(post && PHOrbProfilesValid(post), "Uryu post-opening library retains a valid schema");
    for (NSDictionary *record in post[@"profiles"]) Check([record[@"settings"][@"assistRecoveryL"] isEqual:@YES],
        "all current Uryu post-opening variants explicitly opt in");
}
