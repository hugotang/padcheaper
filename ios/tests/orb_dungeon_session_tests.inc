static NSArray *DungeonPages(NSString *digest, bool secondComplete = true) {
    NSArray *floors = @[@{@"wave":@0,@"floorNumber":@1,@"layoutKnown":@YES,@"spawns":@[@{@"catalogID":@400,@"level":@10}]},
        @{@"wave":@1,@"floorNumber":@2,@"layoutKnown":@YES,@"spawns":@[@{@"catalogID":@401,@"level":@11}]}];
    NSMutableArray *pages = [NSMutableArray array];
    for (unsigned wave = 0; wave < 2; ++wave) [pages addObject:@{@"status":@"ready", @"waveIndex":@(wave),
        @"floors":floors,@"spawnTableSHA256":digest,@"actionTableMetadataRaw":@[@4,@17,@512,@0],
        @"floorDefinitions":@{@"wave":@(wave),@"rawCaptureComplete":@(!wave || secondComplete),
            @"catalogs":@[@{@"catalogID":@(400 + wave),@"level":@(10 + wave)}],@"definitions":@[]}}];
    return pages;
}
static void DungeonTicks(PHOrbSession *session, FakeOrbEngine *engine, unsigned count) {
    for (unsigned i = 0; i < count; ++i) Tick(session,engine,Frame(),0.81);
}
static void DungeonSessionTests() {
    FakeOrbEngine *engine = [FakeOrbEngine new]; SeedObservation(engine);
    engine->dungeonFixtures = DungeonPages(@"first");
    engine->enemyActionFixture = @{@"status":@"ready",@"actionTableMetadataRaw":@[@4,@17,@512,@0],
        @"enemies":@[@{@"catalogID":@400,@"definitionSHA256":@"rows-same",@"definitionHeaderHex":@"01",@"rowsRawHex":@"02"}],
        @"definitions":@[@{@"actionID":@3,@"parameters":@[@4]}]};
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    DungeonTicks(session,engine,2);
    Check(!engine->reads && !engine->dungeonReads,"unused dungeon recorder does not start polling");
    [session queueDungeonCapture]; DungeonTicks(session,engine,1);
    NSDictionary *report = session.dungeonReport;
    Check([report[@"floors"] count] == 2 && [report[@"capturedFloors"] intValue] == 1 &&
        ![report[@"allFloorsCaptured"] boolValue] && [report[@"floorDefinitions"][1][@"status"] isEqual:@"not-captured"],
        "first capture publishes all loaded spawns and explicitly marks uncollected definitions");
    DungeonTicks(session,engine,2); report = session.dungeonReport;
    Check([report[@"allDefinitionsCaptured"] boolValue] && [report[@"capturedFloors"] intValue] == 2 &&
        [report[@"observations"] count] == 1 && [report[@"observedDefinitions"] count] == 1 && engine->live.wave == 0 &&
        !engine->stages && !engine->skillAttempts && ![session.snapshot[@"busy"] boolValue],
        "read-only capture gets both floors from the first floor and deduplicates unchanged observations without solving");
    NSMutableDictionary *changed = [engine->enemyActionFixture mutableCopy];
    changed[@"definitions"] = @[@{@"actionID":@3,@"parameters":@[@9]}]; engine->enemyActionFixture = changed;
    DungeonTicks(session,engine,1); report = session.dungeonReport;
    Check([report[@"observedDefinitions"] count] == 2 && [report[@"observations"] count] == 2,
        "same row digest with changed referenced action bytes retains a separate definition set");
    engine->live.phase = 22; const NSUInteger before = engine->dungeonReads;
    DungeonTicks(session,engine,2);
    Check(engine->dungeonReads == before && [session.dungeonReport[@"allDefinitionsCaptured"] boolValue],
        "skill transitions retain historical reports without reading live enemy definitions");
    engine->live.phase = 2; engine->live.wave = 1; DungeonTicks(session,engine,1);
    report = session.dungeonReport;
    Check([report[@"observations"] count] == 3 && [[report[@"observations"] lastObject][@"wave"] intValue] == 1 &&
        ![report[@"current"] boolValue] && ![report[@"survivalKnown"] boolValue],"wave samples stay labeled historical and do not authorize survival");
    engine->live.present = engine->live.valid = false; DungeonTicks(session,engine,1);
    Check([session.dungeonReport isEqual:report],"leaving the dungeon retains the entire latest report for export");
    NSDictionary *exported = PHOrbExportDungeonReport(report,@"fixture",@"req-1",
        [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]);
    NSData *data = [NSData dataWithContentsOfFile:exported[@"filePath"]];
    NSDictionary *json = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    Check([exported[@"ok"] boolValue] && [exported[@"requestID"] isEqual:@"req-1"] &&
        [json[@"floorDefinitions"] count] == 2 && [json[@"dylibVersion"] isEqual:@"fixture"] &&
        !json[@"player"] && !json[@"entryHash"] && !json[@"impl"] && !json[@"endpoint"],
        "export creates an actual readable JSON file with all floors and no account or pointer fields");
    if (exported[@"directoryPath"]) [NSFileManager.defaultManager removeItemAtPath:exported[@"directoryPath"] error:NULL];
    Check(![PHOrbExportDungeonReport(@{},@"fixture",@"empty",nil)[@"ok"] boolValue] &&
        ![PHOrbExportDungeonReport(report,@"fixture",@"bad",[NSURL URLWithString:@"https://invalid.example"])[@"ok"] boolValue],
        "missing report or invalid export directory is a visible failure without a fabricated file");
    engine->live.present = engine->live.valid = true; engine->live.wave = 0; ++engine->live.identity.entryHash[0];
    engine->dungeonFixtures = DungeonPages(@"second",false); [session queueDungeonCapture]; DungeonTicks(session,engine,1);
    report = session.dungeonReport;
    Check([report[@"spawnTableSHA256"] isEqual:@"second"] && [report[@"capturedFloors"] intValue] == 1 &&
        [report[@"observations"] count] == 1,"new entry replaces both old floors and old observations");
    DungeonTicks(session,engine,1); report = session.dungeonReport;
    Check([report[@"allFloorsCaptured"] boolValue] && ![report[@"allDefinitionsCaptured"] boolValue],
        "all pages present does not hide missing action definitions");
    engine->dungeonFixtures = DungeonPages(@"third"); [session queueDungeonCapture]; DungeonTicks(session,engine,1);
    Check([session.dungeonReport[@"capturedFloors"] intValue] == 1 && ![session.dungeonReport[@"allFloorsCaptured"] boolValue],
        "a changed spawn table invalidates previously accumulated floor pages");
    for (unsigned i = 0; i < 132; ++i) {
        engine->live.observation.hp = 100 + i; engine->live.automation.hp = 100 + i; DungeonTicks(session,engine,1);
    }
    report = session.dungeonReport;
    Check([report[@"observations"] count] == 128 && [report[@"droppedRecords"] intValue] > 0,
        "historical observations are bounded and visibly count dropped older samples");
    PHBattleFrame other = Frame(); ++other.epoch; Tick(session,engine,other,0.81);
    Check(![session.dungeonReport[@"available"] boolValue] && ![session.dungeonReport[@"floors"] count],
        "an account epoch change clears the retained report before export");
    for (auto kind : {PHOrbMultiplayerThree,PHOrbMultiplayerFour,PHOrbMultiplayerEight,PHOrbMultiplayerThreeSolo}) {
        engine = [FakeOrbEngine new]; SeedObservation(engine); engine->dungeonFixtures = DungeonPages(@"multi");
        engine->live.multiplayer.kind = kind; engine->live.identity.multiplayer = kind == PHOrbMultiplayerThreeSolo ? 2 : 1;
        session = [[PHOrbSession alloc] initWithEngine:engine]; [session queueDungeonCapture]; DungeonTicks(session,engine,3);
        Check(!engine->dungeonReads && ![session.dungeonReport[@"available"] boolValue] && !engine->stages && !engine->skillAttempts,
            "dungeon recorder does not read or operate any multiplayer mode");
    }
    engine = [FakeOrbEngine new]; SetAssistBoard(engine,0); SeedObservation(engine);
    engine->dungeonFixtures = DungeonPages(@"stop-reason");
    engine->live.enemyCombo.turns[0] = 3; engine->live.enemyCombo.thresholds[0] = 14;
    session = [[PHOrbSession alloc] initWithEngine:engine];
    [session queueDungeonCapture]; DungeonTicks(session,engine,3); [session queueAutomatic];
    Check(UntilPhase(session,engine,@"stopped") && [session.dungeonReport[@"lastStopReason"] containsString:@"Combo"],
        "an automatic rejection is recorded independently of later UI messages");
    NSDictionary *stop = session.dungeonReport[@"lastStop"];
    Check([session configureQuality:@"balanced"] && [session.dungeonReport[@"lastStop"] isEqual:stop],
        "reloading idle settings cannot replace the retained automatic stop reason");
    engine->live.present = engine->live.valid = false; DungeonTicks(session,engine,1);
    Check([session.dungeonReport[@"lastStop"] isEqual:stop], "leaving after a failure keeps its original stop reason");
    engine->live.present = engine->live.valid = true; ++engine->live.identity.entryHash[0];
    DungeonTicks(session,engine,1);
    Check(![session.dungeonReport[@"lastStop"] count], "a new entry cannot inherit the previous entry's stop reason");
}
