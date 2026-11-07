static NSMutableDictionary *FormationProfileMutableCopy(NSDictionary *value) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:value options:0 error:NULL];
    return [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL];
}

static NSDictionary *FormationProfileSelected(NSDictionary *config) {
    NSDictionary *profiles = PHConfigurationOrbProfiles(config);
    return PHOrbProfileNamed(profiles, profiles[@"selected"]);
}

static void TestFormationProfilePersistence(void) {
    NSData *data = [NSData dataWithContentsOfFile:@"ios/templates/ishida-uryu-gamewith-574233/team-plan.json"];
    NSDictionary *plan = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    NSMutableDictionary *baseline = [PHDefaultConfiguration() mutableCopy];
    baseline[@"formations"] = PHFormationLibraryFromTeam(nil);
    NSDictionary *imported = PHOrbProfilesImportingPlan(PHOrbDefaultProfiles(), plan, NULL);
    NSDictionary *settings = PHOrbProfileNamed(imported, imported[@"selected"])[@"settings"];
    NSString *reason = nil;
    NSDictionary *saved = PHConfigurationSavingOrbProfiles(baseline, imported, settings, PHOrbDefaultProfiles(), &reason);
    NSString *teamID = FormationProfileSelected(saved)[@"formationID"];
    Check(saved && !reason && [teamID isEqual:saved[@"formations"][@"selected"]] &&
        [saved[@"formations"][@"teams"] count] == 2 && [baseline[@"formations"][@"teams"] count] == 1,
        "initial full-plan transaction binds one new team without mutating the prior configuration");
    if (!saved) return;

    NSDictionary *baseProfiles = PHConfigurationOrbProfiles(saved);
    NSMutableDictionary *team = FormationProfileMutableCopy(FormationProfileSelected(saved)[@"formation"]);
    team[@"slots"][2][@"body"][@"latents"] = [@[@48,@11] mutableCopy];
    team[@"slots"][1][@"body"][@"level"] = @110;
    team[@"slots"][1][@"body"][@"plus"] = [@[@99,@99,@99] mutableCopy];
    team[@"badge"] = @89;
    NSDictionary *edited = PHConfigurationSavingFormations(saved,
        PHFormationLibraryUpdatingTeam(saved[@"formations"], team), &reason);
    Check(edited && [FormationProfileSelected(edited)[@"formation"] isEqual:team] &&
        [FormationProfileSelected(edited)[@"formationID"] isEqual:teamID] &&
        [FormationProfileSelected(saved)[@"formation"][@"slots"][2][@"body"][@"latents"] isEqual:@[@48]] &&
        !FormationProfileSelected(saved)[@"formation"][@"badge"],
        "formation save atomically updates linked latents, level, plus and badge without aliasing the old snapshot");
    if (!edited) return;
    NSDictionary *reselected = PHConfigurationSavingOrbProfiles(edited, baseProfiles, settings, baseProfiles, &reason);
    Check(reselected && [FormationProfileSelected(reselected)[@"formation"] isEqual:team] &&
        [reselected[@"formations"][@"teams"] count] == 2 && [reselected[@"formations"][@"selected"] isEqual:teamID],
        "saving an older open strategy draft cannot restore imported latents or create a duplicate team");
    if (!reselected) return;
    NSDictionary *reloaded = FormationProfileMutableCopy(reselected);
    NSData *exported = PHOrbExportProfileData(FormationProfileSelected(reloaded), &reason);
    NSDictionary *package = [NSJSONSerialization JSONObjectWithData:exported options:0 error:NULL];
    Check(PHConfigurationIsValid(reloaded) && [reloaded isEqual:reselected] &&
        [package[@"formation"] isEqual:team] && !package[@"formationID"] &&
        PHOrbProfileFromPlan(package, NULL),
        "restart and portable export retain edited loadout without leaking a device-local formation ID");
    NSMutableDictionary *withoutBadge = FormationProfileMutableCopy(team); [withoutBadge removeObjectForKey:@"badge"];
    NSDictionary *clearedBadge = PHConfigurationSavingFormations(reloaded,
        PHFormationLibraryUpdatingTeam(reloaded[@"formations"], withoutBadge), NULL);
    Check(clearedBadge && !FormationProfileSelected(clearedBadge)[@"formation"][@"badge"] &&
        [FormationProfileSelected(reloaded)[@"formation"][@"badge"] isEqual:@89],
        "removing a badge override updates linked plans without changing the previous saved selection");
    NSMutableDictionary *defaultBadge = FormationProfileMutableCopy(team); defaultBadge[@"badge"] = @0;
    NSDictionary *zeroBadge = PHConfigurationSavingFormations(clearedBadge,
        PHFormationLibraryUpdatingTeam(clearedBadge[@"formations"], defaultBadge), NULL);
    NSDictionary *zeroPackage = PHOrbPlanFromProfile(FormationProfileSelected(zeroBadge));
    Check([zeroPackage[@"formation"][@"badge"] isEqual:@0] && PHOrbProfileFromPlan(zeroPackage,NULL),
        "explicit no-badge survives linked-plan portable export distinctly from absent selection");

    NSMutableDictionary *otherSelection = [PHConfigurationOrbProfiles(reloaded) mutableCopy]; otherSelection[@"selected"] = @"";
    NSDictionary *away = PHConfigurationSavingOrbProfiles(reloaded, otherSelection, settings, nil, NULL);
    NSDictionary *back = PHConfigurationSavingOrbProfiles(away, PHConfigurationOrbProfiles(reloaded), settings, nil, NULL);
    Check([back isEqual:reloaded], "switching away and back preserves the bound ID and edited formation");
    NSMutableDictionary *concurrent = [PHConfigurationOrbProfiles(edited) mutableCopy]; concurrent[@"autoSelect"] = @YES;
    NSDictionary *changed = PHConfigurationSavingOrbProfiles(edited, concurrent, settings, nil, NULL);
    Check(!PHConfigurationSavingOrbProfiles(changed, baseProfiles, settings, baseProfiles, &reason) &&
        [reason containsString:@"其他操作"], "a stale strategy editor cannot overwrite a concurrent non-loadout setting change");
    NSDictionary *reimported = PHConfigurationSavingOrbProfiles(edited, imported, settings, baseProfiles, NULL);
    Check(reimported && [FormationProfileSelected(reimported)[@"formation"] isEqual:plan[@"formation"]] &&
        [PHFormationLibraryTeam(reimported[@"formations"], teamID) isEqual:team],
        "explicit delete-and-reimport can restore the reviewed package while retaining the edited team record");

    NSMutableDictionary *copiedProfiles = FormationProfileMutableCopy(PHConfigurationOrbProfiles(edited));
    NSMutableDictionary *copy = FormationProfileMutableCopy(FormationProfileSelected(edited));
    copy[@"id"] = @"independent-copy"; [copy removeObjectForKey:@"formationID"];
    [copiedProfiles[@"profiles"] addObject:copy]; copiedProfiles[@"selected"] = copy[@"id"];
    NSDictionary *copied = PHConfigurationSavingOrbProfiles(edited, copiedProfiles, settings, nil, NULL);
    NSString *copyID = FormationProfileSelected(copied)[@"formationID"];
    Check(copied && ![copyID isEqual:teamID] && [copied[@"formations"][@"teams"] count] == 3,
        "copying a full strategy creates an independent loadout even when title and cards match");
    if (!copied) return;
    NSMutableDictionary *copyTeam = FormationProfileMutableCopy(team); copyTeam[@"slots"][2][@"body"][@"latents"] = [@[@48] mutableCopy];
    copyTeam[@"badge"] = @129;
    NSDictionary *copyEdited = PHConfigurationSavingFormations(copied,
        PHFormationLibraryUpdatingTeam(copied[@"formations"], copyTeam), NULL);
    Check([FormationProfileSelected(copyEdited)[@"formation"] isEqual:copyTeam] &&
        [PHOrbProfileNamed(PHConfigurationOrbProfiles(copyEdited), plan[@"id"])[@"formation"] isEqual:team],
        "editing an independent copy's latents and PAD Pass selection does not change the original strategy");
    NSDictionary *passPackage = PHOrbPlanFromProfile(FormationProfileSelected(copyEdited));
    Check([passPackage[@"formation"][@"badge"] isEqual:@129] && PHOrbProfileFromPlan(passPackage,NULL),
        "full-plan sharing preserves synthetic PAD Pass selection without converting it to a native ID");

    NSMutableDictionary *legacy = [baseline mutableCopy];
    legacy[@"formations"] = PHFormationLibraryFromTeam(plan[@"formation"]);
    legacy[@"orbProfiles"] = imported; legacy[@"orbs"] = settings;
    NSDictionary *migrated = PHConfigurationSavingFormations(legacy,
        PHFormationLibraryUpdatingTeam(legacy[@"formations"], team), NULL);
    Check([FormationProfileSelected(migrated)[@"formationID"] isEqual:@"legacy"] &&
        [FormationProfileSelected(migrated)[@"formation"] isEqual:team],
        "editing an unambiguously matching legacy team migrates the link before its snapshot changes");
    legacy[@"formations"] = @{@"schema":@1, @"selected":@"edited", @"teams":@[
        @{@"id":@"original", @"team":plan[@"formation"]}, @{@"id":@"edited", @"team":team}]};
    Check(!PHConfigurationSavingOrbProfiles(legacy, imported, settings, nil, &reason) &&
        [reason containsString:@"使用的隊伍"] && [legacy[@"formations"][@"teams"] count] == 2,
        "legacy original-plus-edited duplicates require explicit selection and are never merged or deleted");
    NSMutableDictionary *rebound = FormationProfileMutableCopy(imported);
    rebound[@"profiles"][0][@"formationID"] = @"edited";
    NSDictionary *repaired = PHConfigurationSavingOrbProfiles(legacy, rebound, settings, nil, NULL);
    Check([FormationProfileSelected(repaired)[@"formation"] isEqual:team] &&
        [repaired[@"formations"][@"selected"] isEqual:@"edited"] && [repaired[@"formations"][@"teams"] count] == 2,
        "explicitly selecting an existing edited duplicate repairs the binding without creating another team");

    NSMutableDictionary *incompatible = FormationProfileMutableCopy(team);
    incompatible[@"slots"][0][@"assist"][@"id"] = @0;
    NSDictionary *changedCards = PHConfigurationSavingFormations(edited,
        PHFormationLibraryUpdatingTeam(edited[@"formations"], incompatible), NULL);
    Check(changedCards && [FormationProfileSelected(changedCards)[@"formation"] isEqual:team] &&
        !PHConfigurationSavingOrbProfiles(changedCards, PHConfigurationOrbProfiles(changedCards), settings, nil, &reason) &&
        [reason containsString:@"本體／武裝"],
        "changing an equipped identity saves the team but cannot silently rewrite or reselect the old opening");
    NSMutableDictionary *inactiveOld = [baseProfiles mutableCopy]; inactiveOld[@"selected"] = @"";
    NSDictionary *inactiveSaved = PHConfigurationSavingOrbProfiles(changedCards, inactiveOld, settings, baseProfiles, NULL);
    Check([PHOrbProfileNamed(PHConfigurationOrbProfiles(inactiveSaved), plan[@"id"])[@"formation"] isEqual:team],
        "an inactive incompatible link keeps its latest recoverable snapshot when an older editor is saved");
    NSDictionary *deleted = PHConfigurationSavingFormations(edited, baseline[@"formations"], NULL);
    Check(deleted && [FormationProfileSelected(deleted)[@"formationID"] isEqual:teamID] &&
        !PHConfigurationSavingOrbProfiles(deleted, PHConfigurationOrbProfiles(deleted), settings, nil, &reason) &&
        [reason containsString:@"已刪除"], "deleting a linked team does not recreate it when its strategy is reselected");

    NSMutableDictionary *shared = FormationProfileMutableCopy(PHConfigurationOrbProfiles(copied));
    shared[@"profiles"][1][@"formationID"] = teamID;
    NSDictionary *sharing = PHConfigurationSavingOrbProfiles(copied, shared, settings, nil, NULL);
    NSDictionary *sharedEdited = PHConfigurationSavingFormations(sharing,
        PHFormationLibraryUpdatingTeam(sharing[@"formations"], copyTeam), NULL);
    Check([PHOrbProfileNamed(PHConfigurationOrbProfiles(sharedEdited), plan[@"id"])[@"formation"] isEqual:copyTeam] &&
        [FormationProfileSelected(sharedEdited)[@"formation"] isEqual:copyTeam],
        "explicitly sharing one formation synchronizes all compatible linked strategies");

    NSMutableArray *fullTeams = [NSMutableArray arrayWithObject:edited[@"formations"][@"teams"][1]];
    for (NSUInteger i = 1; i < PHFormationLibraryCapacity; ++i)
        [fullTeams addObject:@{@"id":[NSString stringWithFormat:@"full-%lu", (unsigned long)i], @"team":baseline[@"formations"][@"teams"][0][@"team"]}];
    NSDictionary *full = PHConfigurationSavingFormations(edited,
        @{@"schema":@1, @"selected":teamID, @"teams":fullTeams}, NULL);
    Check(PHConfigurationSavingOrbProfiles(full, PHConfigurationOrbProfiles(full), settings, nil, NULL) != nil,
        "a full 64-team library can still reselect and edit its existing linked strategy");
    Check(!PHConfigurationSavingOrbProfiles(full, copiedProfiles, settings, nil, &reason) && [reason containsString:@"已滿"],
        "copying into a full library refuses the transaction without discarding an existing team");
    NSMutableDictionary *unselectedCopy = FormationProfileMutableCopy(copiedProfiles); unselectedCopy[@"selected"] = @"";
    NSDictionary *savedCopy = PHConfigurationSavingOrbProfiles(edited, unselectedCopy, settings, nil, NULL);
    Check([PHOrbProfileNamed(PHConfigurationOrbProfiles(savedCopy), @"independent-copy")[@"formationID"] length] > 0 &&
        [savedCopy[@"formations"][@"selected"] isEqual:teamID],
        "a new unselected full-plan copy also receives its own durable team without changing current selection");
    for (id badID in @[@"", @YES, @"bad id", NSNull.null]) {
        NSMutableDictionary *bad = FormationProfileMutableCopy(imported); bad[@"profiles"][0][@"formationID"] = badID;
        Check(!PHOrbProfilesValid(bad), "local formation links require a nonempty valid identifier");
    }
    NSMutableDictionary *global = [PHOrbDefaultProfiles() mutableCopy]; global[@"eightAutoStart"] = @YES;
    Check([PHOrbProfilesImportingPlan(global, plan, NULL)[@"eightAutoStart"] isEqual:@YES],
        "full-plan import preserves the independent eight-player countdown preference");
    NSData *eightData = [NSData dataWithContentsOfFile:@"ios/templates/eight-player-thanks-2026/activity-profile.json"];
    NSDictionary *eightLibrary = PHOrbProfilesImportingData(PHConfigurationOrbProfiles(edited), eightData, NULL);
    NSDictionary *eightConfig = PHConfigurationSavingOrbProfiles(edited, eightLibrary, settings, nil, NULL);
    Check(eightConfig && [eightConfig[@"formations"] isEqual:edited[@"formations"]] &&
        !FormationProfileSelected(eightConfig)[@"formationID"],
        "selecting an eight-player strategy never selects or replaces a formation");
    NSMutableDictionary *badEight = FormationProfileMutableCopy(eightLibrary);
    [badEight[@"profiles"] lastObject][@"formationID"] = teamID;
    Check(!PHOrbProfilesValid(badEight), "eight-player activity records cannot acquire a team-replacement binding");
}
