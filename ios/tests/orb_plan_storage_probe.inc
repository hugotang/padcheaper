static void PHProbePlanStorage(UIWindow *overlay) {
    // The probe intentionally loads the dylib at runtime. Resolve helpers
    // from that exact handle instead of introducing a static link to it.
    __typeof__(&PHConfigurationOrbProfiles) configProfiles = (__typeof__(&PHConfigurationOrbProfiles))dlsym(PHProbeDylib, "PHConfigurationOrbProfiles");
    __typeof__(&PHOrbProfilesImportingPlan) importPlan = (__typeof__(&PHOrbProfilesImportingPlan))dlsym(PHProbeDylib, "PHOrbProfilesImportingPlan");
    __typeof__(&PHOrbProfileNamed) profileNamed = (__typeof__(&PHOrbProfileNamed))dlsym(PHProbeDylib, "PHOrbProfileNamed");
    __typeof__(&PHFormationReadLibrary) readLibrary = (__typeof__(&PHFormationReadLibrary))dlsym(PHProbeDylib, "PHFormationReadLibrary");
    PHProbeCheck(configProfiles && importPlan && profileNamed && readLibrary, @"Plan helpers resolve from the loaded test dylib");
    if (!configProfiles || !importPlan || !profileNamed || !readLibrary) return;
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
    NSDictionary *library = importPlan(configProfiles(baseline), plan, NULL);
    NSDictionary *profile = profileNamed(library, library[@"selected"]);
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
        [failed[@"orbState"][@"profiles"] isEqual:configProfiles(baseline)] &&
        [failed[@"orbState"][@"profileMessage"] length] > 0,
        @"Failed full-plan disk write publishes neither the new formation nor the opening/strategy");
    BOOL repaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:backup toPath:path error:NULL];
    PHProbeCheck(repaired, @"Plan fixture restores the previous canonical file after the failed commit");
    if (!repaired) return;
    action(@"retry", nil);
    action(@"orbsProfiles", payload);
    NSDictionary *committed = snapshot(), *disk = PHProbeJSON(@"config.json"), *team = nil;
    readLibrary(disk[@"formations"], &team, NULL);
    PHProbeCheck([disk[@"orbProfiles"] isEqual:library] && [disk[@"orbs"] isEqual:profile[@"settings"]] &&
        [disk[@"formations"][@"teams"] count] == [baseline[@"formations"][@"teams"] count] + 1 &&
        [team isEqual:plan[@"formation"]] && [committed[@"formationDraft"] isEqual:team] &&
        [committed[@"orbState"][@"profiles"] isEqual:library],
        @"A successful full-plan commit saves all four parts and publishes the selected automatic entry team together");
    NSString *thornBackup = [path stringByAppendingFormat:@".thorn-probe-%@", NSUUID.UUID.UUIDString];
    BOOL thornMoved = [files moveItemAtPath:path toPath:thornBackup error:NULL];
    BOOL thornBlocked = thornMoved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(thornBlocked, @"Thorn setting test blocks the real canonical file after selecting a plan");
    if (thornBlocked) {
        action(@"orbsThornPriority", @YES);
        NSDictionary *thornFailed = snapshot();
        PHProbeCheck([thornFailed[@"config"] isEqual:disk] &&
            [thornFailed[@"orbState"][@"thornComboPriority"] isEqual:@NO] &&
            [thornFailed[@"orbState"][@"thornPolicyMessage"] length] > 0,
            @"Failed thorn preference save leaves both active settings and the selected plan unchanged");
        BOOL thornRepaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:thornBackup toPath:path error:NULL];
        PHProbeCheck(thornRepaired, @"Thorn fixture restores the canonical file after failed write");
        if (!thornRepaired) return;
        action(@"retry", nil); action(@"orbsThornPriority", @YES);
        NSDictionary *thornCommitted = snapshot(), *thornDisk = PHProbeJSON(@"config.json");
        NSDictionary *thornProfile = profileNamed(configProfiles(thornDisk), library[@"selected"]);
        PHProbeCheck([thornDisk[@"orbs"][@"thornComboPriority"] isEqual:@YES] &&
            [thornProfile[@"settings"][@"thornComboPriority"] isEqual:@YES] &&
            [thornCommitted[@"orbState"][@"thornComboPriority"] isEqual:@YES] &&
            ![thornCommitted[@"orbState"][@"thornPolicyMessage"] length],
            @"Successful thorn preference save updates the selected full plan and published settings atomically");
    } else if (thornMoved) [files moveItemAtPath:thornBackup toPath:path error:NULL];
    NSDictionary *beforePoison = PHProbeJSON(@"config.json");
    NSString *poisonBackup = [path stringByAppendingFormat:@".poison-probe-%@", NSUUID.UUID.UUIDString];
    BOOL poisonMoved = [files moveItemAtPath:path toPath:poisonBackup error:NULL];
    BOOL poisonBlocked = poisonMoved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(poisonBlocked, @"Poison policy test blocks the real canonical file for a selected plan");
    if (poisonBlocked) {
        action(@"orbsPoisonWithHeartFour", @YES);
        NSDictionary *poisonFailed = snapshot();
        PHProbeCheck([poisonFailed[@"config"] isEqual:beforePoison] &&
            [poisonFailed[@"orbState"][@"allowPoisonWithHeartFour"] isEqual:@NO] &&
            [poisonFailed[@"orbState"][@"poisonPolicyMessage"] length] > 0,
            @"Failed poison policy save keeps selected plan and active policy unchanged");
        BOOL poisonRepaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:poisonBackup toPath:path error:NULL];
        PHProbeCheck(poisonRepaired, @"Poison fixture restores the canonical file after a failed write");
        if (!poisonRepaired) return;
        action(@"retry", nil); action(@"orbsPoisonWithHeartFour", @YES);
        NSDictionary *poisonCommitted = snapshot(), *poisonDisk = PHProbeJSON(@"config.json");
        NSDictionary *poisonProfile = profileNamed(configProfiles(poisonDisk), library[@"selected"]);
        PHProbeCheck([poisonDisk[@"orbs"][@"allowPoisonWithHeartFour"] isEqual:@YES] &&
            [poisonProfile[@"settings"][@"allowPoisonWithHeartFour"] isEqual:@YES] &&
            [poisonCommitted[@"orbState"][@"allowPoisonWithHeartFour"] isEqual:@YES] &&
            ![poisonCommitted[@"orbState"][@"poisonPolicyMessage"] length],
            @"Successful poison policy save atomically updates the selected plan and active settings");
        action(@"orbsQuality", @"thorough"); action(@"orbsThornPriority", @NO);
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"allowPoisonWithHeartFour"] isEqual:@YES],
            @"Changing quality or thorn preference preserves the selected team's poison policy");
    } else if (poisonMoved) [files moveItemAtPath:poisonBackup toPath:path error:NULL];
    BOOL restored = [original writeToFile:path options:NSDataWritingAtomic error:NULL];
    NSDictionary *reloaded = snapshot();
    PHProbeCheck(restored && [reloaded[@"config"] isEqual:baseline] &&
        [reloaded[@"formationLibrary"] isEqual:baseline[@"formations"]] &&
        [reloaded[@"formationDraft"] isEqual:initial[@"formationDraft"]] &&
        [reloaded[@"orbState"][@"profiles"] isEqual:configProfiles(baseline)],
        @"External canonical reload updates both formations and strategies, and restores the probe baseline");
}
