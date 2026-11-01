// Retained reports are historical export data. Nothing reads these fields to
// decide whether to cast a skill, relax goals or submit input.
- (void)queueDungeonCapture {
    _monitoring = true;
    _dungeonCaptureAt = 0;
}
- (void)resetDungeonReport:(const PHOrbSnapshot *)state {
    _dungeonIdentity = state->identity; _dungeonBound = true;
    _dungeonFloors = @[]; _dungeonPages = [NSMutableDictionary dictionary];
    _dungeonSamples = [NSMutableArray array]; _dungeonSampleKey = nil;
    _dungeonDefinitionSets = [NSMutableDictionary dictionary];
    _dungeonTableSHA = @""; _dungeonMetadata = @[];
    _dungeonCursor = _dungeonRefreshCursor = _dungeonDropped = 0; _dungeonCaptureAt = 0;
    _dungeonStatus = @"capturing";
}
- (void)captureDungeonPage:(const PHOrbSnapshot *)state time:(NSTimeInterval)time {
    if (![_engine respondsToSelector:@selector(observeDungeon:waveIndex:)]) return;
    if (!_dungeonBound || !PHBattleSameIdentity(&_dungeonIdentity,&state->identity)) [self resetDungeonReport:state];
    const bool finished = _dungeonFloors.count && _dungeonCursor >= _dungeonFloors.count;
    const double interval = finished ? 8.0 : 0.75;
    if (_dungeonCaptureAt > 0 && time >= _dungeonCaptureAt && time - _dungeonCaptureAt < interval) return;
    _dungeonCaptureAt = time;
    const NSUInteger requested = finished ? _dungeonRefreshCursor++ % _dungeonFloors.count : _dungeonCursor;
    NSDictionary *page = [_engine observeDungeon:state waveIndex:requested];
    if (![page[@"status"] isEqual:@"ready"]) { _dungeonStatus = page[@"status"] ?: @"unavailable"; return; }
    NSArray *floors = page[@"floors"], *metadata = page[@"actionTableMetadataRaw"];
    NSString *digest = page[@"spawnTableSHA256"];
    if (![floors isKindOfClass:NSArray.class] || !floors.count || floors.count > 100 ||
        ![digest isKindOfClass:NSString.class] || !digest.length ||
        ![metadata isKindOfClass:NSArray.class] || ![page[@"floorDefinitions"] isKindOfClass:NSDictionary.class] ||
        requested >= floors.count || [page[@"waveIndex"] unsignedIntegerValue] != requested ||
        [page[@"floorDefinitions"][@"wave"] unsignedIntegerValue] != requested) { _dungeonStatus = @"unavailable"; return; }
    if (_dungeonTableSHA.length && (![_dungeonTableSHA isEqual:digest] || ![_dungeonMetadata isEqual:metadata])) {
        [_dungeonPages removeAllObjects]; _dungeonCursor = _dungeonRefreshCursor = 0;
        // Historical samples remain labeled with their original table digest.
    }
    _dungeonFloors = [floors copy]; _dungeonTableSHA = [digest copy]; _dungeonMetadata = [metadata copy];
    NSMutableDictionary *captured = [page[@"floorDefinitions"] mutableCopy];
    captured[@"capturedUptimeMS"] = @(uint64_t(NSProcessInfo.processInfo.systemUptime * 1000));
    _dungeonPages[@(requested)] = [captured copy];
    while (_dungeonPages[@(_dungeonCursor)] && _dungeonCursor < _dungeonFloors.count) ++_dungeonCursor;
    _dungeonStatus = _dungeonCursor == _dungeonFloors.count ? @"captured" : @"capturing";
}
- (void)retainDungeonSample:(const PHOrbSnapshot *)state {
    if (state->observation.status != PHOrbObservationReady || !_dungeonBound ||
        !PHBattleSameIdentity(&_dungeonIdentity,&state->identity)) return;
    NSMutableDictionary *observation = [BattleObservationTrace(*state,true,0,_observationDurationMS,nil) mutableCopy];
    [observation removeObjectForKey:@"enemyActions"]; [observation removeObjectForKey:@"ageMS"];
    [observation removeObjectForKey:@"readDurationMS"]; [observation removeObjectForKey:@"current"];
    NSMutableArray *enemies = [NSMutableArray array];
    NSUInteger droppedDefinitions = 0;
    for (NSDictionary *enemy in _enemyActionObservation[@"enemies"]) {
        NSMutableDictionary *compact = [enemy mutableCopy];
        for (NSString *key in @[@"dispatchAnalysis",@"selectorAnalysis",@"damageAnalysis",@"threatEvents",@"rowsRawHex",@"definitionHeaderHex",@"rows"])
            [compact removeObjectForKey:key];
        if ([enemy[@"definitionSHA256"] length]) {
            NSDictionary *set = @{@"catalogID":enemy[@"catalogID"] ?: @0,
                @"definitionHeaderHex":enemy[@"definitionHeaderHex"] ?: @"", @"rowsRawHex":enemy[@"rowsRawHex"] ?: @"",
                @"actionTableMetadataRaw":_enemyActionObservation[@"actionTableMetadataRaw"] ?: @[],
                @"definitions":_enemyActionObservation[@"definitions"] ?: @[]};
            NSString *digest = PHOrbDungeonRecordKey(set);
            if (digest) {
                if (!_dungeonDefinitionSets[digest] && _dungeonDefinitionSets.count < 256) _dungeonDefinitionSets[digest] = set;
                compact[@"definitionSetID"] = digest;
                compact[@"definitionsRetained"] = @(_dungeonDefinitionSets[digest] != nil);
                if (!_dungeonDefinitionSets[digest]) ++droppedDefinitions;
            }
        }
        [enemies addObject:[compact copy]];
    }
    NSDictionary *key = @{@"wave":@(state->wave), @"board":BoardTrace(state->board), @"observation":observation,
        @"enemies":enemies, @"tableSHA256":_dungeonTableSHA ?: @"", @"autoTurn":@(_autoTurn),
        @"enemyActionStatus":_enemyActionObservation[@"status"] ?: @"unavailable",
        @"sessionPhase":_phase, @"message":_message ?: @""};
    if ([_dungeonSampleKey isEqual:key]) return;
    _dungeonSampleKey = key;
    _dungeonDropped += droppedDefinitions;
    NSMutableDictionary *sample = [key mutableCopy];
    sample[@"capturedUptimeMS"] = @(uint64_t(NSProcessInfo.processInfo.systemUptime * 1000));
    sample[@"current"] = @NO; sample[@"source"] = @"observed-stable-player-window";
    if (_dungeonSamples.count == 128) { [_dungeonSamples removeObjectAtIndex:0]; ++_dungeonDropped; }
    [_dungeonSamples addObject:[sample copy]];
}
- (NSDictionary *)dungeonReportSummary {
    bool complete = _dungeonFloors.count && _dungeonPages.count == _dungeonFloors.count;
    for (NSDictionary *floor in _dungeonFloors) complete &= [floor[@"layoutKnown"] boolValue];
    for (NSDictionary *page in _dungeonPages.allValues) complete &= [page[@"rawCaptureComplete"] boolValue];
    return @{@"available":@(_dungeonBound && (_dungeonFloors.count || _dungeonSamples.count)),
        @"status":_dungeonStatus ?: @"not-requested", @"dungeon":@(_dungeonIdentity.dungeon), @"floor":@(_dungeonIdentity.floor),
        @"totalFloors":@(_dungeonFloors.count), @"capturedFloors":@(_dungeonPages.count),
        @"allFloorsCaptured":@(_dungeonFloors.count && _dungeonPages.count == _dungeonFloors.count),
        @"allDefinitionsCaptured":@(complete), @"samples":@(_dungeonSamples.count), @"droppedRecords":@(_dungeonDropped),
        @"current":@NO, @"enemyForecastKnown":@NO, @"survivalKnown":@NO};
}
- (NSDictionary *)dungeonReport {
    NSMutableArray *pages = [NSMutableArray array];
    for (NSUInteger index = 0; index < _dungeonFloors.count; ++index) {
        NSDictionary *page = _dungeonPages[@(index)];
        [pages addObject:page ?: @{@"wave":@(index), @"rawCaptureComplete":@NO, @"status":@"not-captured"}];
    }
    NSMutableDictionary *report = [[self dungeonReportSummary] mutableCopy];
    report[@"schema"] = @1; report[@"kind"] = @"PADCheaper-dungeon-report";
    report[@"target"] = @"jp.gungho.pad-ios-23.6.0-106878";
    report[@"source"] = @"loaded-entry-spawn-table-and-observed-player-windows";
    report[@"spawnTableSHA256"] = _dungeonTableSHA ?: @"";
    report[@"actionTableMetadataRaw"] = _dungeonMetadata ?: @[];
    report[@"floors"] = _dungeonFloors ?: @[]; report[@"floorDefinitions"] = pages;
    report[@"observations"] = [_dungeonSamples copy] ?: @[];
    report[@"observedDefinitions"] = [_dungeonDefinitionSets copy] ?: @{};
    report[@"randomAlternativesKnown"] = @NO; report[@"preemptiveSelectionKnown"] = @NO;
    report[@"historicalOnly"] = @YES;
    report[@"captureLimits"] = @{@"floors":@100, @"definitionsPerFloor":@192, @"observations":@128,
        @"observedDefinitionSets":@256, @"actionReadBytesPerPass":@131072, @"actionReadCallsPerPass":@256,
        @"futureAIStateKnown":@NO};
    if (_dungeonBound && PHBattleSameIdentity(&_dungeonIdentity,&_traceOrigin.identity)) {
        report[@"lastSearch"] = _searchTrace ?: @{};
        report[@"lastStopReason"] = _message ?: @"";
    }
    return [report copy];
}
