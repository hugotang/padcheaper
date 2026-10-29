static void PHProbePlanStorage(UIWindow *overlay) {
    PHOverlaySnapshot snapshot = [overlay.rootViewController valueForKey:@"snapshot"];
    PHOverlayAction action = [overlay.rootViewController valueForKey:@"action"];
    NSDictionary *initial = snapshot();
    NSMutableDictionary *baseline = [initial[@"config"] mutableCopy];
    baseline[@"formations"] = initial[@"formationLibrary"];
    NSString *path = initial[@"configPath"];
    NSData *original = [NSJSONSerialization dataWithJSONObject:baseline options:0 error:NULL];
    BOOL seeded = original && [original writeToFile:path options:NSDataWritingAtomic error:NULL];
    PHProbeCheck(seeded, @"Plan storage fixture captures the existing configuration and formation library");
    if (!seeded) return;
    snapshot();
    NSString *planPath = [NSBundle.mainBundle pathForResource:@"team-plan" ofType:@"json"];
    NSData *data = [NSData dataWithContentsOfFile:planPath];
    NSDictionary *plan = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    NSDictionary *library = PHOrbProfilesImportingPlan(PHConfigurationOrbProfiles(baseline), plan, NULL);
    NSDictionary *profile = PHOrbProfileNamed(library, library[@"selected"]);
    PHProbeCheck(profile != nil, @"Real storage test loads the executable Uryu plan");
    if (!profile) return;
    NSDictionary *payload = @{@"library":library, @"settings":profile[@"settings"]};
    NSFileManager *files = NSFileManager.defaultManager;
    NSString *backup = [path stringByAppendingFormat:@".probe-%@", NSUUID.UUID.UUIDString];
    BOOL moved = [files moveItemAtPath:path toPath:backup error:NULL];
    BOOL blocked = moved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(blocked, @"Full-plan transaction fixture blocks the real canonical file");
    if (!blocked) {
        if (moved) [files moveItemAtPath:backup toPath:path error:NULL];
        return;
    }
    action(@"orbsProfiles", payload);
    NSDictionary *failed = snapshot();
    PHProbeCheck([failed[@"config"] isEqual:baseline] &&
        [failed[@"formationLibrary"] isEqual:baseline[@"formations"]] &&
        [failed[@"orbState"][@"profiles"] isEqual:PHConfigurationOrbProfiles(baseline)] &&
        [failed[@"orbState"][@"profileMessage"] length] > 0,
        @"Failed full-plan disk write publishes neither the new formation nor the opening/strategy");
    BOOL repaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:backup toPath:path error:NULL];
    PHProbeCheck(repaired, @"Plan fixture restores the previous canonical file after the failed commit");
    if (!repaired) return;
    action(@"retry", nil);
    action(@"orbsProfiles", payload);
    NSDictionary *committed = snapshot(), *disk = PHProbeJSON(@"config.json"), *team = nil;
    PHFormationReadLibrary(disk[@"formations"], &team, NULL);
    PHProbeCheck([disk[@"orbProfiles"] isEqual:library] && [disk[@"orbs"] isEqual:profile[@"settings"]] &&
        [disk[@"formations"][@"teams"] count] == [baseline[@"formations"][@"teams"] count] + 1 &&
        [team isEqual:plan[@"formation"]] && [committed[@"formationDraft"] isEqual:team] &&
        [committed[@"orbState"][@"profiles"] isEqual:library],
        @"A successful full-plan commit saves all four parts and publishes the selected automatic entry team together");
    BOOL restored = [original writeToFile:path options:NSDataWritingAtomic error:NULL];
    NSDictionary *reloaded = snapshot();
    PHProbeCheck(restored && [reloaded[@"config"] isEqual:baseline] &&
        [reloaded[@"formationLibrary"] isEqual:baseline[@"formations"]] &&
        [reloaded[@"formationDraft"] isEqual:initial[@"formationDraft"]] &&
        [reloaded[@"orbState"][@"profiles"] isEqual:PHConfigurationOrbProfiles(baseline)],
        @"External canonical reload updates both formations and strategies, and restores the probe baseline");
}
