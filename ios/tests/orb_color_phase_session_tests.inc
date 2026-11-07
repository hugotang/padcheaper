static void ColorPhaseDiagnosticSessionTests() {
    // Keep only an equal-colour adjacent pair movable: preview returns the
    // same known failed layout, independent of search beam or route length.
    FakeOrbEngine *engine = [FakeOrbEngine new];
    const uint8_t failed[] = {4,2,2,2,0,3, 4,3,3,1,0,3, 0,0,0,3,0,3,
        1,1,1,1,0,4, 3,5,5,5,2,2};
    auto &board = engine->live.board;
    std::copy(std::begin(failed),std::end(failed),board.types);
    board.spinnerMask = UINT64_C(1) << 28; board.spinnerColors = 63;
    board.fixedStart = 19;
    board.tapeMask = ((UINT64_C(1) << 30) - 1) & ~((UINT64_C(1) << 18) | (UINT64_C(1) << 19));
    for (unsigned i = 0; i < 30; ++i) board.flags[i] = board.tapeMask & (UINT64_C(1) << i) ? PHOrbFlagTape : 0;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"rules":@[],
        @"goals":@[@{@"kind":@"colors",@"colors":@31,@"count":@5},
            @{@"kind":@"match",@"colors":@32,@"count":@1,@"length":@3}]}],"missing-colour diagnostic fixture accepts original goals");
    Check(Preview(session,engine),"known unmet layout remains available as a checked preview");
    NSDictionary *trace = session.snapshot[@"searchTrace"];
    NSDictionary *phases = trace[@"colorPhases"];
    Check([phases[@"valid"] boolValue] && [phases[@"phaseCount"] isEqual:@6] &&
        [phases[@"missingInAnyFailedPhase"] isEqual:@16] &&
        [phases[@"missingInAnyFailedPhaseNames"] isEqual:@"闇"] &&
        ![session.snapshot[@"canExecute"] boolValue] && !engine->stages,
        "six-phase diagnostics name the missing dark colour without authorizing a failed preview");
    for (NSDictionary *phase in phases[@"phases"]) {
        NSArray *deficits = phase[@"deficits"];
        Check(deficits.count == 1 && [deficits[0][@"goalIndex"] isEqual:@0] &&
            [deficits[0][@"additionalColorsNeeded"] isEqual:@1] &&
            [deficits[0][@"absentAllowedColors"] isEqual:@16],
            "each failed shared phase preserves the original colour count and exact absent alternatives");
    }
    Check([NSJSONSerialization isValidJSONObject:trace],"phase diagnostics remain JSON serializable");

    // The same layout already contains four attack colours. Requiring any
    // four-of-five must not list absent dark as an unmet mandatory colour.
    Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"rules":@[],
        @"goals":@[@{@"kind":@"colors",@"colors":@31,@"count":@4}]}] && Preview(session,engine),
        "optional-colour diagnostic fixture produces a preview");
    phases = session.snapshot[@"searchTrace"][@"colorPhases"];
    Check([phases[@"valid"] boolValue] && [phases[@"missingInAnyFailedPhase"] isEqual:@0],
        "a satisfied subset does not demand every allowed colour");
    for (NSDictionary *phase in phases[@"phases"])
        Check([phase[@"deficits"] count] == 0,"satisfied phase has no invented missing-colour clause");
}
