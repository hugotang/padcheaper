static void FailureMessageSessionTests() {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        FakeOrbEngine *engine = [FakeOrbEngine new];
        auto &board = engine->live.board;
        for (unsigned i = 0; i < 30; ++i) board.types[i] = i < (scenario == 2 ? 5u : 3u) ? 0 : 1;
        if (scenario == 0) board.unmatchableColors = 1;
        if (scenario == 1) board.minMatch = 4;
        if (scenario == 2) { board.spinnerMask = 1; board.spinnerColors = 3; }
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        NSDictionary *goal = scenario == 2 ? @{@"kind":@"t",@"colors":@1,@"count":@1} :
            @{@"kind":scenario == 1 ? @"exact" : @"match",@"colors":@1,@"count":@1,@"length":@3};
        Check([session configureSettings:@{@"mode":@"manual",@"quality":@"fast",@"goals":@[goal],@"rules":@[]}],
            "failure diagnostic settings accepted");
        Tick(session,engine,Frame()); [session queueRead];
        // UntilPhase is an Auto helper: it returns immediately when automatic
        // is false. Manual read/solve must instead wait for its own terminal phase.
        for (unsigned i = 0; i < 2000 && ![session.snapshot[@"phase"] isEqual:@"stopped"] &&
            ![session.snapshot[@"phase"] isEqual:@"fault"] && ![session.snapshot[@"phase"] isEqual:@"blocked"]; ++i)
            Tick(session,engine,Frame());
        const bool ok = [session.snapshot[@"phase"] isEqual:@"stopped"] && !engine->stages && ![session.snapshot[@"canExecute"] boolValue] &&
            [session.snapshot[@"message"] containsString:scenario == 0 ? @"不可消：火" :
                scenario == 1 ? @"最少消珠規則衝突" : @"至少一種色序未通過"] &&
            [session.snapshot[@"message"] isEqual:session.snapshot[@"searchTrace"][@"reason"]];
        if (!ok) std::fprintf(stderr,"Diagnostic scenario %u: phase=%s message=%s reason=%s\n",scenario,
            [session.snapshot[@"phase"] UTF8String], [session.snapshot[@"message"] UTF8String],
            [session.snapshot[@"searchTrace"][@"reason"] UTF8String] ?: "(none)");
        Check(ok,
            "unmatchable, minimum-match and spinner-phase failures retain their specific reason without starting a gesture");
        [session queueStop:@"end manual diagnostic fixture"];
    }
    FakeOrbEngine *engine = [FakeOrbEngine new]; SetEffectBoard(engine,false);
    auto &board = engine->live.board;
    // One spinner overlaps tape, while the route crosses the other. A blocked
    // taped spinner does not mean that locked spinners remain impassable.
    board.spinnerMask = (UINT64_C(1) << 29) | 1; board.spinnerColors = 63;
    for (unsigned i = 0; i < 30; ++i) board.flags[i] |= PHOrbFlagLocked;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session,engine) && [session.snapshot[@"path"] isEqual:@[@28,@29]] &&
        [session.snapshot[@"message"] containsString:@"可穿過轉轉格"] && ![session.snapshot[@"message"] containsString:@"避開轉轉"],
        "a fully locked route that actually crosses a spinner is described accurately");
    [session queueStop:@"end diagnostic tests"];
}
