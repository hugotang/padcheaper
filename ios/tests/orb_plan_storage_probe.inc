static void PHProbePlanStorage(UIWindow *overlay) {
    // The probe intentionally loads the dylib at runtime. Resolve helpers
    // from that exact handle instead of introducing a static link to it.
    __typeof__(&PHConfigurationOrbProfiles) configProfiles = (__typeof__(&PHConfigurationOrbProfiles))dlsym(PHProbeDylib, "PHConfigurationOrbProfiles");
    __typeof__(&PHOrbProfilesImportingPlan) importPlan = (__typeof__(&PHOrbProfilesImportingPlan))dlsym(PHProbeDylib, "PHOrbProfilesImportingPlan");
    __typeof__(&PHOrbProfileNamed) profileNamed = (__typeof__(&PHOrbProfileNamed))dlsym(PHProbeDylib, "PHOrbProfileNamed");
    __typeof__(&PHFormationReadLibrary) readLibrary = (__typeof__(&PHFormationReadLibrary))dlsym(PHProbeDylib, "PHFormationReadLibrary");
    __typeof__(&PHFormationLibraryUpdatingTeam) updateTeam = (__typeof__(&PHFormationLibraryUpdatingTeam))dlsym(PHProbeDylib, "PHFormationLibraryUpdatingTeam");
    __typeof__(&PHOrbExportProfileData) exportProfile = (__typeof__(&PHOrbExportProfileData))dlsym(PHProbeDylib, "PHOrbExportProfileData");
    PHProbeCheck(configProfiles && importPlan && profileNamed && readLibrary && updateTeam && exportProfile, @"Plan helpers resolve from the loaded test dylib");
    if (!configProfiles || !importPlan || !profileNamed || !readLibrary || !updateTeam || !exportProfile) return;
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
    NSDictionary *payload = @{@"library":library, @"settings":profile[@"settings"], @"requestID":@"plan-storage-import"};
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
        [failed[@"orbState"][@"profileMessage"] length] > 0 &&
        [failed[@"orbProfileSaveResult"][@"ok"] isEqual:@NO],
        @"Failed full-plan disk write publishes neither the new formation nor the opening/strategy");
    BOOL repaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:backup toPath:path error:NULL];
    PHProbeCheck(repaired, @"Plan fixture restores the previous canonical file after the failed commit");
    if (!repaired) return;
    action(@"retry", nil);
    action(@"orbsProfiles", payload);
    NSDictionary *committed = snapshot(), *disk = PHProbeJSON(@"config.json"), *team = nil;
    readLibrary(disk[@"formations"], &team, NULL);
    NSDictionary *linkedProfile = profileNamed(configProfiles(disk), library[@"selected"]);
    NSMutableDictionary *portableProfile = [linkedProfile mutableCopy]; [portableProfile removeObjectForKey:@"formationID"];
    PHProbeCheck([portableProfile isEqual:profile] &&
        [linkedProfile[@"formationID"] isEqual:disk[@"formations"][@"selected"]] && [disk[@"orbs"] isEqual:profile[@"settings"]] &&
        [disk[@"formations"][@"teams"] count] == [baseline[@"formations"][@"teams"] count] + 1 &&
        [team isEqual:plan[@"formation"]] && [committed[@"formationDraft"] isEqual:team] &&
        [committed[@"orbState"][@"profiles"] isEqual:disk[@"orbProfiles"]] &&
        [committed[@"orbProfileSaveResult"][@"requestID"] isEqual:payload[@"requestID"]] &&
        [committed[@"orbProfileSaveResult"][@"ok"] isEqual:@YES],
        @"A successful full-plan commit saves all four parts and publishes the selected automatic entry team together");
    NSData *teamData = [NSJSONSerialization dataWithJSONObject:team options:0 error:NULL];
    NSMutableDictionary *editedTeam = [NSJSONSerialization JSONObjectWithData:teamData options:NSJSONReadingMutableContainers error:NULL];
    editedTeam[@"slots"][2][@"body"][@"latents"] = [@[@48,@11] mutableCopy];
    NSDictionary *editedLibrary = updateTeam(disk[@"formations"], editedTeam);
    NSString *latentBackup = [path stringByAppendingFormat:@".latent-probe-%@", NSUUID.UUID.UUIDString];
    BOOL latentMoved = [files moveItemAtPath:path toPath:latentBackup error:NULL];
    BOOL latentBlocked = latentMoved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(latentBlocked, @"Latent persistence fixture blocks the canonical file");
    if (latentBlocked) {
        action(@"formationLibrarySave", @{@"requestID":@"latent-failed", @"library":editedLibrary});
        NSDictionary *failedLatents = snapshot();
        PHProbeCheck([failedLatents[@"formationSaveResult"][@"ok"] isEqual:@NO] &&
            [failedLatents[@"config"] isEqual:disk] && [failedLatents[@"formationLibrary"] isEqual:disk[@"formations"]],
            @"Failed latent save publishes neither the edited team nor its strategy snapshot");
        BOOL latentRepaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:latentBackup toPath:path error:NULL];
        PHProbeCheck(latentRepaired, @"Latent fixture restores the canonical file");
        if (!latentRepaired) return;
    } else if (latentMoved) [files moveItemAtPath:latentBackup toPath:path error:NULL];
    action(@"formationLibrarySave", @{@"requestID":@"latent-saved", @"library":editedLibrary});
    NSDictionary *latentDisk = PHProbeJSON(@"config.json"), *latentState = snapshot();
    PHProbeCheck([latentState[@"formationSaveResult"][@"ok"] isEqual:@YES] &&
        [profileNamed(configProfiles(latentDisk), library[@"selected"])[@"formation"] isEqual:editedTeam] &&
        [latentState[@"orbState"][@"profiles"] isEqual:configProfiles(latentDisk)],
        @"Real team save persists and publishes the edited latent in the full strategy atomically");
    action(@"orbsProfiles", @{@"requestID":@"latent-reselect", @"library":configProfiles(disk),
        @"settings":profile[@"settings"], @"baseProfiles":configProfiles(disk)});
    NSDictionary *reselectedDisk = PHProbeJSON(@"config.json"), *reselectedState = snapshot();
    NSDictionary *reselectedProfile = profileNamed(configProfiles(reselectedDisk), library[@"selected"]);
    NSData *exported = exportProfile(reselectedProfile, NULL);
    NSDictionary *exportedPlan = exported ? [NSJSONSerialization JSONObjectWithData:exported options:0 error:NULL] : nil;
    PHProbeCheck([reselectedState[@"orbProfileSaveResult"][@"ok"] isEqual:@YES] &&
        [reselectedState[@"formationDraft"] isEqual:editedTeam] && [exportedPlan[@"formation"] isEqual:editedTeam] &&
        [reselectedDisk[@"formations"][@"teams"] count] == [disk[@"formations"][@"teams"] count] &&
        [reselectedDisk[@"formations"][@"selected"] isEqual:disk[@"formations"][@"selected"]],
        @"Reselecting an older strategy draft retains edited latents, export and team count after the canonical reload");
    disk = reselectedDisk;
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
    NSDictionary *beforeDiagonal = PHProbeJSON(@"config.json");
    NSString *diagonalBackup = [path stringByAppendingFormat:@".diagonal-probe-%@",NSUUID.UUID.UUIDString];
    BOOL diagonalMoved = [files moveItemAtPath:path toPath:diagonalBackup error:NULL];
    BOOL diagonalBlocked = diagonalMoved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(diagonalBlocked,@"Diagonal save test blocks the canonical file of a selected plan");
    if (diagonalBlocked) {
        action(@"orbsDiagonal",@YES);
        NSDictionary *failedDiagonal = snapshot();
        PHProbeCheck([failedDiagonal[@"config"] isEqual:beforeDiagonal] &&
            [failedDiagonal[@"orbState"][@"diagonal"] isEqual:@NO] &&
            [failedDiagonal[@"orbState"][@"diagonalMessage"] length] > 0,
            @"A failed diagonal save publishes neither the new working policy nor the selected profile");
        BOOL diagonalRepaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:diagonalBackup toPath:path error:NULL];
        PHProbeCheck(diagonalRepaired,@"Diagonal fixture restores the canonical file after failed save");
        if (!diagonalRepaired) return;
        action(@"retry",nil); action(@"orbsDiagonal",@YES);
        NSDictionary *diagonalCommitted = snapshot(), *diagonalDisk = PHProbeJSON(@"config.json");
        NSDictionary *diagonalProfile = profileNamed(configProfiles(diagonalDisk),library[@"selected"]);
        PHProbeCheck([diagonalDisk[@"orbs"][@"diagonal"] isEqual:@YES] &&
            [diagonalProfile[@"settings"][@"diagonal"] isEqual:@YES] &&
            [diagonalCommitted[@"orbState"][@"diagonal"] isEqual:@YES] && ![diagonalCommitted[@"orbState"][@"diagonalMessage"] length],
            @"A successful diagonal save atomically updates the working settings and selected team plan");
        action(@"orbsQuality",@"balanced"); action(@"orbsPoisonWithHeartFour",@NO);
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"diagonal"] isEqual:@YES],
            @"Changing quality or poison policy preserves diagonal permission in the selected plan");
    } else if (diagonalMoved) [files moveItemAtPath:diagonalBackup toPath:path error:NULL];
    NSDictionary *beforeAssist = PHProbeJSON(@"config.json");
    NSNumber *oldAssist = beforeAssist[@"orbs"][@"assistRecoveryL"] ?: @NO;
    // Objective-C C promotes !BOOL to int; boxing that expression creates
    // numeric 0/1, which the JSON-Boolean contract correctly rejects.
    NSNumber *newAssist = oldAssist.boolValue ? @NO : @YES;
    NSString *assistBackup = [path stringByAppendingFormat:@".assist-probe-%@",NSUUID.UUID.UUIDString];
    BOOL assistMoved = [files moveItemAtPath:path toPath:assistBackup error:NULL];
    BOOL assistBlocked = assistMoved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(assistBlocked,@"Assist policy fixture blocks the selected plan's canonical file");
    if (assistBlocked) {
        action(@"orbsAssistRecoveryL",newAssist);
        NSDictionary *failedAssist = snapshot();
        PHProbeCheck([failedAssist[@"config"] isEqual:beforeAssist] &&
            [failedAssist[@"orbState"][@"assistRecoveryL"] isEqual:oldAssist] &&
            ![failedAssist[@"storageReady"] boolValue] &&
            [failedAssist[@"orbState"][@"assistRecoveryMessage"] length] > 0,
            @"Real failed assist disk write marks storage unavailable and preserves settings and the selected profile");
        BOOL repairedAssist = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:assistBackup toPath:path error:NULL];
        PHProbeCheck(repairedAssist,@"Assist fixture restores its original canonical file");
        if (!repairedAssist) return;
        action(@"retry",nil); action(@"orbsAssistRecoveryL",newAssist);
        NSDictionary *assistDisk = PHProbeJSON(@"config.json"), *assistCommitted = snapshot();
        NSDictionary *assistProfile = profileNamed(configProfiles(assistDisk),library[@"selected"]);
        PHProbeCheck([assistDisk[@"orbs"][@"assistRecoveryL"] isEqual:newAssist] &&
            [assistProfile[@"settings"][@"assistRecoveryL"] isEqual:newAssist] &&
            [assistCommitted[@"orbState"][@"assistRecoveryL"] isEqual:newAssist] &&
            ![assistCommitted[@"orbState"][@"assistRecoveryMessage"] length],
            @"Successful assist policy save publishes one coherent common setting and selected plan");
        action(@"orbsQuality",@"fast");
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"assistRecoveryL"] isEqual:newAssist],
            @"Quality changes preserve the independent assist policy");
        action(@"orbsAssistRecoveryL",oldAssist);
        NSDictionary *assistRestoredDisk = PHProbeJSON(@"config.json"), *assistRestoredState = snapshot();
        NSDictionary *assistRestoredProfile = profileNamed(configProfiles(assistRestoredDisk),library[@"selected"]);
        PHProbeCheck([assistRestoredDisk[@"orbs"][@"assistRecoveryL"] isEqual:oldAssist] &&
            [assistRestoredProfile[@"settings"][@"assistRecoveryL"] isEqual:oldAssist] &&
            [assistRestoredState[@"orbState"][@"assistRecoveryL"] isEqual:oldAssist] &&
            ![assistRestoredState[@"orbState"][@"assistRecoveryMessage"] length],
            @"Assist policy can be saved in both Boolean directions after storage recovery");
    } else if (assistMoved) [files moveItemAtPath:assistBackup toPath:path error:NULL];
    NSDictionary *beforeAuto = PHProbeJSON(@"config.json");
    NSNumber *oldAuto = configProfiles(beforeAuto)[@"eightAutoStart"] ?: @NO;
    NSNumber *newAuto = oldAuto.boolValue ? @NO : @YES;
    NSString *autoBackup = [path stringByAppendingFormat:@".auto-probe-%@", NSUUID.UUID.UUIDString];
    BOOL autoMoved = [files moveItemAtPath:path toPath:autoBackup error:NULL];
    BOOL autoBlocked = autoMoved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(autoBlocked, @"Countdown preference fixture blocks the canonical file");
    if (autoBlocked) {
        action(@"orbsEightAutoStart", newAuto);
        NSDictionary *failedAuto = snapshot();
        PHProbeCheck([failedAuto[@"config"] isEqual:beforeAuto] &&
            [failedAuto[@"orbState"][@"profiles"] isEqual:configProfiles(beforeAuto)] &&
            [failedAuto[@"orbState"][@"autoStartMessage"] length] > 0,
            @"Failed countdown save retains the old flag and every strategy/formation setting");
        BOOL repairedAuto = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:autoBackup toPath:path error:NULL];
        PHProbeCheck(repairedAuto, @"Countdown fixture restores the canonical file");
        if (!repairedAuto) return;
        action(@"retry", nil); action(@"orbsEightAutoStart", newAuto);
        NSMutableDictionary *expectedAuto = [beforeAuto mutableCopy];
        NSMutableDictionary *expectedProfiles = [configProfiles(beforeAuto) mutableCopy];
        expectedProfiles[@"eightAutoStart"] = newAuto; expectedAuto[@"orbProfiles"] = expectedProfiles;
        NSDictionary *savedAuto = snapshot();
        PHProbeCheck([PHProbeJSON(@"config.json") isEqual:expectedAuto] &&
            [savedAuto[@"orbState"][@"profiles"] isEqual:expectedProfiles] &&
            [savedAuto[@"formationLibrary"] isEqual:beforeAuto[@"formations"]] &&
            ![savedAuto[@"orbState"][@"autoStartMessage"] length],
            @"Global countdown preference writes only its flag, leaving the selected plan and formation unchanged");
        action(@"orbsEightAutoStart", @1);
        PHProbeCheck([PHProbeJSON(@"config.json") isEqual:expectedAuto] &&
            [snapshot()[@"orbState"][@"autoStartMessage"] length], @"Numeric one cannot masquerade as a JSON Boolean");
        action(@"orbsEightAutoStart", oldAuto);
        expectedProfiles[@"eightAutoStart"] = oldAuto;
        PHProbeCheck([PHProbeJSON(@"config.json") isEqual:expectedAuto], @"Countdown preference saves in both directions");
    } else if (autoMoved) [files moveItemAtPath:autoBackup toPath:path error:NULL];
    BOOL restored = [original writeToFile:path options:NSDataWritingAtomic error:NULL];
    NSDictionary *reloaded = snapshot();
    PHProbeCheck(restored && [reloaded[@"config"] isEqual:baseline] &&
        [reloaded[@"formationLibrary"] isEqual:baseline[@"formations"]] &&
        [reloaded[@"formationDraft"] isEqual:initial[@"formationDraft"]] &&
        [reloaded[@"orbState"][@"profiles"] isEqual:configProfiles(baseline)],
        @"External canonical reload updates both formations and strategies, and restores the probe baseline");
}
