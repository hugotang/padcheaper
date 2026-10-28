static PHOrbSnapshot RecoverySizedBoard(unsigned width, unsigned height, unsigned hearts) {
    PHOrbSnapshot state = {0};
    state.present = state.valid = state.ordinary = state.settled = true;
    state.phase = 2; state.minErase = 3; state.freeMovement = 1; state.remainingFrames = 300; state.fps = 60;
    state.input.valid = true; state.board.width = (uint8_t)width; state.board.height = (uint8_t)height;
    state.automation.valid = state.automation.hpKnown = state.automation.boardKnown = true;
    state.automation.hp = 400; state.automation.maximumHP = 1000;
    for (unsigned i = 0; i < width * height; ++i) state.board.types[i] = (uint8_t)(i < hearts ? 5 : i % 5);
    return state;
}
static PHOrbSnapshot RecoveryBoard(void) { return RecoverySizedBoard(6,5,9); }
static NSDictionary *RecoveryBase(NSArray *goals) {
    return @{@"available":@YES, @"goals":goals, @"message":PHOrbGoalsDescription(goals), @"warning":@"原有提醒", @"leaders":@[@"隊長"]};
}
static void RecoveryPlanningTests(void) {
    Check(PHOrbRecoveryValid(PHOrbDefaultRecovery()), "old settings acquire a valid disabled recovery plan");
    NSDictionary *settings = @{@"hpAtMost":@40, @"groups":@1, @"shape":@"match"};
    for (id invalid in @[@YES, @[], @{}, @{@"hpAtMost":@40}])
        Check(!PHOrbRecoveryValid(invalid), "partial or non-object recovery settings are refused");
    for (NSDictionary *change in @[@{@"hpAtMost":@YES}, @{@"hpAtMost":@-1}, @{@"hpAtMost":@101}, @{@"hpAtMost":@40.5},
        @{@"hpAtMost":@"40"}, @{@"groups":@0}, @{@"groups":@4}, @{@"groups":@YES}, @{@"shape":@"square"}, @{@"extra":@1}]) {
        NSMutableDictionary *bad = [settings mutableCopy]; [bad addEntriesFromDictionary:change];
        Check(!PHOrbRecoveryValid(bad), "threshold, group, shape and key validation never silently changes intent");
    }
    NSDictionary *base = RecoveryBase(@[Goal(@"combo",0,5,0)]);
    NSDictionary *plan = PHOrbPlanWithRecovery(base,PHOrbDefaultRecovery(),NULL,YES);
    Check([plan[@"available"] boolValue] && ![plan[@"recoveryActive"] boolValue] && [plan[@"goals"] isEqual:base[@"goals"]],
        "disabled recovery needs no HP data and retains the original conjunction");
    plan = PHOrbPlanWithRecovery(base,settings,NULL,NO);
    Check([plan[@"available"] boolValue] && [plan[@"goals"] isEqual:base[@"goals"]] && [plan[@"recoveryMessage"] containsString:@"手動"],
        "manual previews do not silently acquire HP reads or conditional goals");
    PHOrbSnapshot state = RecoveryBoard();
    plan = PHOrbPlanWithRecovery(base,settings,&state,YES);
    Check([plan[@"available"] boolValue] && [plan[@"recoveryActive"] boolValue] &&
        [plan[@"goals"] isEqual:@[Goal(@"combo",0,5,0),Goal(@"match",32,1,3)]] && [plan[@"baseGoals"] isEqual:base[@"goals"]] &&
        [plan[@"warning"] containsString:@"原有提醒"] && [plan[@"warning"] containsString:@"回復量"],
        "inclusive low-HP threshold adds a heart goal while retaining base goals and limitations");
    state.automation.hp = 401; plan = PHOrbPlanWithRecovery(base,settings,&state,YES);
    Check([plan[@"available"] boolValue] && ![plan[@"recoveryActive"] boolValue] && [plan[@"goals"] isEqual:base[@"goals"]],
        "one HP above the threshold retains the original goals");
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        state = RecoveryBoard();
        if (invalid == 0) state.automation.hpKnown = false;
        if (invalid == 1) state.automation.maximumHP = 0;
        if (invalid == 2) state.automation.hp = 1001;
        if (invalid == 3) state.identity.multiplayer = 1;
        if (invalid == 4) state.identity.entryMode = 7;
        Check(![PHOrbPlanWithRecovery(base,settings,&state,YES)[@"available"] boolValue],
            "unknown HP, inconsistent maximum and multiplayer cannot authorize conditional recovery");
    }
    state = RecoveryBoard(); state.automation.hp = state.automation.maximumHP = INT32_MAX;
    NSMutableDictionary *wide = [settings mutableCopy]; wide[@"hpAtMost"] = @99;
    Check(![PHOrbPlanWithRecovery(base,wide,&state,YES)[@"recoveryActive"] boolValue], "HP products use wide arithmetic at the signed maximum");
    wide[@"hpAtMost"] = @100;
    Check([PHOrbPlanWithRecovery(base,wide,&state,YES)[@"recoveryActive"] boolValue], "100 percent still includes maximum supported full HP");

    NSDictionary *two = @{@"hpAtMost":@40, @"groups":@2, @"shape":@"four"};
    const unsigned widths[] = {6,7,5}, heights[] = {5,6,4};
    for (unsigned size = 0; size < 3; ++size) {
        state = RecoverySizedBoard(widths[size],heights[size],7);
        Check(![PHOrbPlanWithRecovery(base,two,&state,YES)[@"available"] boolValue], "two heart fours require eight real orbs on each supported geometry");
        state.board.types[widths[size] * heights[size] - 1] = 5;
        state.board.flags[widths[size] * heights[size] - 1] = PHOrbFlagLocked;
        plan = PHOrbPlanWithRecovery(base,two,&state,YES);
        Check([plan[@"available"] boolValue] && [plan[@"goals"] containsObject:Goal(@"four",32,2,0)],
            "the final cell and locked heart count without changing ordinary board restrictions");
        state.ordinary = false; state.obstacles = PHOrbObstacleSpinner;
        Check(![PHOrbPlanWithRecovery(base,two,&state,YES)[@"available"] boolValue], "recovery cannot relax an unsupported board or spinner restriction");
    }
    state = RecoveryBoard();
    NSArray *six = @[Goal(@"match",32,3,3), Goal(@"combo",0,5,0), Goal(@"colors",31,3,0),
        Goal(@"l",1,1,0), Goal(@"cross",2,1,0), Goal(@"column",4,1,0)];
    base = RecoveryBase(six); plan = PHOrbPlanWithRecovery(base,settings,&state,YES);
    Check([plan[@"available"] boolValue] && [plan[@"goals"] isEqual:six] && [base[@"goals"] isEqual:six],
        "an identical heart goal merges at the stronger group count, even with six existing goals, without mutating settings");
    NSMutableArray *different = [six mutableCopy]; different[0] = Goal(@"match",1,1,3);
    plan = PHOrbPlanWithRecovery(RecoveryBase(different),settings,&state,YES);
    Check(![plan[@"available"] boolValue] && [plan[@"message"] containsString:@"6 項"], "a seventh distinct goal stops instead of dropping a base requirement");
    NSDictionary *unsupported = @{@"available":@NO, @"goals":@[], @"message":@"未知隊長條件", @"unsupported":@[@"unknown"]};
    plan = PHOrbPlanWithRecovery(unsupported,settings,&state,YES);
    Check(![plan[@"available"] boolValue] && [plan[@"message"] isEqual:unsupported[@"message"]] &&
        [plan[@"unsupported"] isEqual:unsupported[@"unsupported"]], "heart recovery never turns unsupported leader conditions into an allowed route");
}
