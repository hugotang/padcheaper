// Exercise the shipped fixed plan through the loaded dylib and real config.
static void PHProbeFixedPlanStorage(UIWindow *overlay) {
    __typeof__(&PHConfigurationOrbProfiles) profiles = (__typeof__(&PHConfigurationOrbProfiles))dlsym(PHProbeDylib,"PHConfigurationOrbProfiles");
    __typeof__(&PHOrbProfilesImportingData) importing = (__typeof__(&PHOrbProfilesImportingData))dlsym(PHProbeDylib,"PHOrbProfilesImportingData");
    __typeof__(&PHOrbProfileNamed) named = (__typeof__(&PHOrbProfileNamed))dlsym(PHProbeDylib,"PHOrbProfileNamed");
    __typeof__(&PHOrbExportProfileData) exporting = (__typeof__(&PHOrbExportProfileData))dlsym(PHProbeDylib,"PHOrbExportProfileData");
    PHProbeCheck(profiles && importing && named && exporting,@"Fixed-plan helpers resolve from the actual GUI dylib");
    if (!profiles || !importing || !named || !exporting) return;
    PHOverlaySnapshot snapshot=[overlay.rootViewController valueForKey:@"snapshot"];
    PHOverlayAction action=[overlay.rootViewController valueForKey:@"action"];
    NSDictionary *initial=snapshot(), *baseline=initial[@"config"];
    NSString *path=initial[@"configPath"];
    NSData *original=[NSData dataWithContentsOfFile:path];
    NSData *data=[NSData dataWithContentsOfFile:[NSBundle.mainBundle pathForResource:@"coral-fixed-plan" ofType:@"json"]];
    NSDictionary *library=data ? importing(profiles(baseline),data,NULL) : nil;
    NSDictionary *profile=library ? named(library,library[@"selected"]) : nil;
    PHProbeCheck(original && profile && [profile[@"fixedPlan"][@"floors"] count]==15 &&
        [profile[@"id"] isEqual:@"coral-fixed-585607-v90-r5"] && [profile[@"dungeon"] isEqual:@7801] &&
        [profile[@"floor"] isEqual:@1] &&
        [profile[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0][@"actions"][0][@"skill"] isEqual:@66752] &&
        [profile[@"fixedPlan"][@"floors"][1][@"variants"][0][@"stages"][1][@"board"][@"unmatchableRecovery"] isEqual:@YES] &&
        [profile[@"fixedPlan"][@"floors"][6][@"variants"][0][@"stages"][0][@"actions"][0][@"monster"] isEqual:@210524] &&
        [profile[@"fixedPlan"][@"floors"][6][@"variants"][0][@"stages"][1][@"requireEnemyState"][1][@"maximumPercent"] isEqual:@50] &&
        [profile[@"fixedPlan"][@"floors"][12][@"variants"][0][@"stages"][0][@"repeat"][@"until"][@"skill"] isEqual:@66740] &&
        [profile[@"fixedPlan"][@"floors"][13][@"variants"][0][@"stages"][2][@"repeat"][@"until"][@"skill"] isEqual:@66718] &&
        [profile[@"fixedPlan"][@"floors"][14][@"variants"][0][@"stages"][0][@"requireEffectsClear"] isEqual:@[@"unmatchable"]] &&
        [[profile[@"fixedPlan"][@"floors"][14][@"variants"][0][@"stages"] lastObject][@"repeat"][@"maximumPuzzles"] isEqual:@2],
        @"Bundled Coral revision imports native identity, B7 survivor guard, reserved skills and B15 recovery with a bounded finish");
    if (!original || !profile) return;
    @try {
        action(@"orbsProfiles",@{@"library":library,@"settings":profile[@"settings"],@"requestID":@"coral-probe"});
        NSDictionary *saved=snapshot(), *disk=PHProbeJSON(@"config.json");
        NSDictionary *roundtrip=named(profiles(disk),library[@"selected"]);
        PHProbeCheck([saved[@"orbProfileSaveResult"][@"ok"] isEqual:@YES] &&
            [roundtrip[@"fixedPlan"] isEqual:profile[@"fixedPlan"]] &&
            [roundtrip[@"id"] isEqual:@"coral-fixed-585607-v90-r5"] && [roundtrip[@"dungeon"] isEqual:@7801] &&
            [roundtrip[@"floor"] isEqual:@1] && !roundtrip[@"formation"] &&
            [saved[@"formationLibrary"] isEqual:initial[@"formationLibrary"]] &&
            [saved[@"formationDraft"] isEqual:initial[@"formationDraft"]],
            @"Fixed-plan commit preserves every stage without selecting or creating a replacement team");
        NSData *exported=exporting(roundtrip,NULL);
        NSDictionary *package=exported ? [NSJSONSerialization JSONObjectWithData:exported options:0 error:NULL] : nil;
        PHProbeCheck([package[@"format"] isEqual:@"padcheaper-fixed-plan"] &&
            [package[@"profile"] isEqual:roundtrip] && !importing(profiles(disk),exported,NULL),
            @"Fixed-plan export and duplicate rejection preserve the saved source and floor order");
        PHOrbProfileEditor *editor=[[NSClassFromString(@"PHOrbProfileEditor") alloc] initWithLibrary:profiles(disk)
            settings:roundtrip[@"settings"] formations:saved[@"formationLibrary"] context:@{} save:^NSString *(NSDictionary *a,NSDictionary *b) { return nil; }];
        [editor loadViewIfNeeded];
        for (NSString *section in @[@"management",@"details"])
            [(UIButton *)PHProbeFind(editor.view,[@"padcheaper.orbsProfileSection-" stringByAppendingString:section]) sendActionsForControlEvents:UIControlEventTouchUpInside];
        UILabel *summary=(UILabel *)PHProbeFind(editor.view,@"padcheaper.orbsFixedPlanSummary");
        PHProbeCheck([summary.text containsString:@"B15"] && PHProbeFind(editor.view,@"padcheaper.orbsFixedPlanExport"),
            @"Fixed plan editor shows the final floor and exposes its dedicated export");
        NSDictionary *before=[[editor valueForKey:@"selected"] copy];
        [(UIButton *)PHProbeFind(editor.view,@"padcheaper.orbsProfileAnyTeam") sendActionsForControlEvents:UIControlEventTouchUpInside];
        PHProbeCheck([before isEqual:[editor valueForKey:@"selected"]],@"Fixed plan cannot discard its six-member binding in the editor");
    } @finally {
        BOOL restored=[original writeToFile:path options:NSDataWritingAtomic error:NULL];
        NSDictionary *again=snapshot();
        PHProbeCheck(restored && [profiles(again[@"config"]) isEqual:profiles(baseline)] &&
            [again[@"formationLibrary"] isEqual:initial[@"formationLibrary"]],@"Fixed-plan probe restores the prior configuration");
    }
}
