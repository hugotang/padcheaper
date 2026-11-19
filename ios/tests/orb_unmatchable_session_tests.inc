static PHOrbSession *StartB2RecoveryTest(FixedTestEngine *e, unsigned failure=0) {
    auto profile=MutableFixedSessionProfile(e);
    NSMutableDictionary *first=profile[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
    first[@"actions"]=@[]; first[@"board"][@"policy"]=@"six_colors_fours";
    NSDictionary *finish=@{@"id":@"finish",@"actions":@[],
        @"board":@{@"policy":@"six_colors_fours",@"assistRecoveryL":@NO,@"maxGestureMS":@0,@"unmatchableRecovery":@YES},
        @"repeat":@{@"minimumPuzzles":@1,@"maximumPuzzles":@1,@"until":@{@"kind":@"floor_change"}}};
    [profile[@"fixedPlan"][@"floors"] addObject:@{@"number":@2,@"variants":@[@{@"kind":@"any",@"stages":@[finish]}]}];
    const uint8_t cells[]={4,4,4,4,0,1,2, 1,1,1,1,2,3,4, 3,3,3,3,4,5,0,
        5,5,5,5,0,1,2, 2,2,2,2,3,4,5, 0,0,0,0,1,2,3};
    auto &board=e->live.board; std::copy(std::begin(cells),std::end(cells),board.types);
    // Equal-color swap only. The release layout and native scan order stay
    // fixed, so this fixture does not depend on finding a route by chance.
    board.fixedStart=36; board.tapeMask=((UINT64_C(1)<<42)-1)&~(UINT64_C(3)<<35);
    for (unsigned i=0;i<42;++i) board.flags[i]=board.tapeMask&(UINT64_C(1)<<i) ? PHOrbFlagTape : 0;
    auto s=StartFixedTest(e,profile);
    Check(UntilPhase(s,e,@"turnWaiting"),"B2 recovery fixture first confirms its B1 release");
    FixedSettledTurn(s,e); e->live.wave=1; board.unmatchableColors=16; e->live.unmatchableTurns=1;
    e->live.fixedPlan.enemies[0].catalogID=200974;
    auto &r=e->live.unmatchableRecovery; r.valid=true; r.turns=1;
    r.perColor[0]=r.perColor[2]=1; r.providers[0]=r.providers[2]=1;
    r.colors[0]=5; r.awakenings[0]=r.latents[0]=1; r.numbers[0]=100;
    if (failure==1) e->unknownUnmatchable=YES;
    if (failure==2) { r.perColor[0]=r.perColor[2]=0; r.providers[0]=r.providers[2]=0; r.latents[0]=0; }
    if (failure==3) e->live.unmatchableTurns=2;
    if (failure==4) board.unmatchableColors=48;
    if (failure==5) e->live.fixedPlan.enemies[0].catalogID=200975;
    if (failure==6) { e->live.unmatchableKnown=false; board.unmatchableColors=0; }
    if (failure==7) board.flags[4]|=PHOrbFlagNail;
    if (failure==8) { board.unmatchableColors=0; e->live.unmatchableTurns=0; }
    AdvanceFixedToSolveOrStop(s,e); return s;
}
static void UnmatchableSessionTests() {
    for (unsigned failure=0;failure<9;++failure) {
        auto e=[[FixedTestEngine alloc] initWithWidth:7 height:6]; auto s=StartB2RecoveryTest(e,failure);
        if (failure && failure!=8) Check([s.snapshot[@"phase"] isEqual:@"stopped"] && e->releases==1 && e->skillOrder.empty(),
            "B2 unknown/absent provider, unsupported turn/mask/enemy/board stop without issuing another skill or puzzle");
        else {
            Check([s.snapshot[@"phase"] isEqual:@"solving"] && e->releases==1,
                "B2 scoped recovery or a genuinely clear board may begin ordinary solving");
            if (!failure) Check([s.snapshot[@"unmatchableRecoveryTrace"][@"eligibleColors"] isEqual:@5] && e->unmatchableReads,
                "planning retains the exact fresh eligible fire/wood sources");
            else Check(!e->unmatchableReads && ![s.snapshot[@"unmatchableRecoveryTrace"] count],
                "a clear B2 board uses the original contract without borrowing provider evidence");
        }
        [s queueStop:@"B2 precondition cleanup"];
    }
    for (unsigned held=0;held<2;++held) {
        auto e=[[FixedTestEngine alloc] initWithWidth:7 height:6]; auto s=StartB2RecoveryTest(e);
        if (held) { Check(UntilPhase(s,e,@"running"),"B2 recovery route reaches input"); UntilDragging(s,e); }
        const NSUInteger sent=e->stages;
        ++e->live.unmatchableRecovery.awakenings[0];
        for (unsigned i=0;i<100 && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i) Tick(s,e,Frame());
        Check([s.snapshot[@"phase"] isEqual:@"stopped"] && !e->live.dragged && e->stages<=sent+(held ? 1u : 0u) &&
            [s.snapshot[@"unmatchableRecoveryTrace"][@"status"] isEqual:@"changed-before-release"],
            "a provider change during search or held input revokes movement while permitting owned cleanup");
        [s queueStop:@"B2 revocation cleanup"];
    }
    for (unsigned after=0;after<3;++after) {
        auto e=[[FixedTestEngine alloc] initWithWidth:7 height:6]; auto s=StartB2RecoveryTest(e);
        Check(UntilPhase(s,e,@"turnWaiting"),"B2 recovery confirms native release");
        NSArray *history=s.snapshot[@"executionHistory"];
        NSDictionary *evaluation=[history lastObject][@"releaseEvaluation"];
        Check([evaluation[@"unmatchableRecoveryPredicted"] boolValue] && [evaluation[@"recoveredColors"] isEqual:@16] &&
            [evaluation[@"firstWaveColors"] isEqual:@47] && ![evaluation[@"nativeUnmatchableRecoveryConfirmed"] boolValue],
            "release replay distinguishes a predicted recovery and five first-wave colors from a native receipt");
        FixedSettledTurn(s,e);
        if (!after) { e->live.board.unmatchableColors=0; e->live.unmatchableTurns=0; }
        if (after==2) e->live.unmatchableKnown=false;
        AdvanceFixedToSolveOrStop(s,e);
        NSDictionary *trace=s.snapshot[@"unmatchableRecoveryTrace"];
        Check([trace[@"actualConfirmed"] boolValue]==(after==0) && ![trace[@"latentOnlyAmountConfirmed"] boolValue] &&
            [s.snapshot[@"phase"] isEqual:@"stopped"] && e->releases==2,
            "a fresh clear is required after the turn; the original one-puzzle cap and no-retry guard remain in force");
        NSDictionary *last=[s.snapshot[@"executionHistory"] lastObject];
        Check([last[@"unmatchableRecovery"] isEqual:trace] && last[@"fixedPlanAfterTurn"],
            "the after-turn evidence belongs to the same released puzzle and is retained even on stop");
        e->live.unmatchableKnown=true; e->live.unmatchableTurns=0; e->live.board.unmatchableColors=0;
        [s queueAutomatic]; for (unsigned i=0;i<30;++i) Tick(s,e,Frame());
        Check(e->releases==2 && [[s.snapshot[@"executionHistory"] lastObject] isEqual:last],
            "idle reads and consumed-run restart cannot rewrite the outcome or submit another puzzle");
        [s queueStop:@"B2 receipt cleanup"];
    }
}
