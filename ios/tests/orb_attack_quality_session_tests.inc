// Policy boundaries plus real replay and public Session regressions. These do
// not depend on a wall-clock timeout or on a particular beam search winner.
static void AttackQualitySessionTests() {
    PHOrbSolveOptions options; options.milliseconds=800;
    options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true};
    options.goals.count=2; options.goals.items[0]={PHOrbGoalColors,63,6,0};
    options.goals.items[1]={PHOrbGoalFour,32,1,0};
    PHOrbRoute baseline{}; baseline.count=2; baseline.goalsMet=baseline.hazardsSafe=true;
    Check(PHOrbAttackQualityBudget(true,false,false,options,PHOrbSolveStatus::Solved,baseline,496)==800 &&
        PHOrbAttackQualityBudget(true,false,false,options,PHOrbSolveStatus::Solved,baseline,1800)==200 &&
        !PHOrbAttackQualityBudget(true,false,false,options,PHOrbSolveStatus::Solved,baseline,2000),
        "successful fixed attacks get one bounded shared refinement stage within the solo ceiling");
    for (unsigned scenario=0;scenario<7;++scenario) {
        auto trial=options; auto route=baseline;
        if (scenario==3) trial.fixedBoard.mode=PHOrbFixedBoardMode::SixColorStall;
        if (scenario==4) route.fishing=true;
        if (scenario==5) route.hazardsSafe=false;
        Check(!PHOrbAttackQualityBudget(scenario!=0,scenario==1,scenario==2,trial,
            scenario==6 ? PHOrbSolveStatus::GoalsUnmet : PHOrbSolveStatus::Solved,route,496),
            "multiplayer, recovery, cancellation, stalls, fishing, hazards and failed baselines cannot enter quality fishing");
    }
    PHOrbGoalSet stronger{};
    Check(PHOrbAttackQualityGoals(options.goals,3,10,stronger) && stronger.count==3 &&
        stronger.items[2].kind==PHOrbGoalFour && stronger.items[2].colors==63 && stronger.items[2].count==4 &&
        options.goals.count==2 && stronger.items[0].count==6 && stronger.items[1].colors==32,
        "a B5 three-four baseline requests a fourth group without changing the original six colors or heart goal");
    auto full=stronger;
    Check(PHOrbAttackQualityGoals(stronger,4,10,full) && full.count==3 && full.items[2].count==5,
        "a later improvement raises the same temporary clause without accumulating duplicate goals");
    full=options.goals; full.count=PHOrbMaxGoals;
    Check(!PHOrbAttackQualityGoals(full,3,10,stronger) && !PHOrbAttackQualityGoals(options.goals,10,10,stronger),
        "goal capacity and the board bound suppress refinement without dropping original goals");
    PHOrbEvaluation kept{}; kept.goalsMet=kept.hazardsSafe=kept.fixedBoardSatisfied=true;
    kept.firstWaveFours=3; kept.combos=7;
    for (unsigned scenario=0;scenario<6;++scenario) {
        auto candidate=kept; candidate.firstWaveFours=4;
        if (scenario==1) candidate.goalsMet=false;
        if (scenario==2) candidate.hazardsSafe=false;
        if (scenario==3) candidate.fixedBoardSatisfied=false;
        if (scenario==4) { candidate.firstWaveFours=3; candidate.combos=12; }
        if (scenario==5) candidate.firstWaveFours=2;
        Check(PHOrbPreferAttackQuality(candidate,kept)==(scenario==0),
            "more safe first-wave fours are required; incomplete, unsafe, fixed-invalid, tied or weaker results keep the baseline");
    }

    // Six guaranteed four-lines, and two isolated spinners. The selected
    // source still has independent phases when its route makes zero takes.
    PHOrbBoard board{}; board.width=7; board.height=6; board.spinnerColors=63;
    board.spinnerMask=(UINT64_C(1)<<6)|(UINT64_C(1)<<41);
    for (unsigned y=0;y<6;++y) for (unsigned x=0;x<7;++x)
        board.types[y*7+x]=uint8_t(x<4 ? y : (y+x-3)%6);
    PHOrbMultiFishingRoute path; path.count=2; path.cells[0]=0; path.cells[1]=1; path.fishCell=6;
    Check(PHOrbAttackQualityGoals(options.goals,5,10,path.plan.goals),"replay fixture has a temporary six-four goal");
    auto multiOptions=options; multiOptions.fishCell=6; multiOptions.goals=path.plan.goals;
    PHOrbEvaluation searched{}, normalized{};
    Check(PHOrbEvaluateMultiFishing(board,path,multiOptions,nullptr,searched) && searched.goalsMet &&
        searched.firstWaveFours==6,"zero-take multi candidate passes its stronger contract under independent phases");
    auto original=multiOptions; original.goals=options.goals;
    Check(!PHOrbEvaluateMultiFishing(board,path,original,nullptr,normalized),
        "multi replay refuses mismatched temporary plan goals instead of silently mixing contracts");
    path.plan.goals=original.goals;
    Check(PHOrbEvaluateMultiFishing(board,path,original,nullptr,normalized) && normalized.goalsMet &&
        normalized.hazardsSafe && normalized.fixedBoardSatisfied && normalized.firstWaveFours==6 &&
        normalized.matchedGoals==2 && normalized.goalHits[2]==0,
        "normalized multi replay preserves the original contract and phase scope without leaking the temporary goal");
    PHOrbRoute route{}; route.count=2; route.cells[0]=0; route.cells[1]=1; route.goalCount=3;
    route.goalHits[2]=6; route.evaluated=123; route.fishCell=6;
    PHOrbApplyAttackQualityEvaluation(route,normalized,original.goals);
    Check(route.goalCount==2 && route.goalHits[2]==0 && route.matchedGoals==2 && route.goalsMet &&
        route.evaluated==123 && route.cells[1]==1 && route.fishCell==6,
        "normalization removes temporary goal results while preserving route identity and search statistics");

    // Only the equal-colour starting pair is reachable. Ordinary search is
    // solved, but a stronger fishing request cannot improve it. This observes
    // the public selection fallback without injecting a private route.
    FixedTestEngine *engine=[[FixedTestEngine alloc] initWithWidth:7 height:6];
    NSMutableDictionary *profile=MutableFixedSessionProfile(engine);
    NSMutableDictionary *stage=profile[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
    stage[@"actions"]=@[]; stage[@"board"][@"policy"]=@"six_colors_fours";
    profile[@"settings"][@"quality"]=@"thorough";
    auto &live=engine->live;
    std::copy(std::begin(board.types),std::end(board.types),live.board.types);
    live.board.spinnerColors=board.spinnerColors; live.board.spinnerMask=UINT64_C(1)<<6; live.board.fixedStart=1;
    live.board.tapeMask=((UINT64_C(1)<<42)-1) & ~(UINT64_C(3)|(UINT64_C(1)<<6)|(UINT64_C(1)<<41));
    for (unsigned i=0;i<42;++i) live.board.flags[i]=live.board.tapeMask&(UINT64_C(1)<<i) ? PHOrbFlagTape : 0;
    live.spinnerTurns=5; live.spinnerPeriodFrames=live.spinnerCountdown=30;
    live.spinnerCountdownKnown=live.orbMotionKnown=true; live.remainingFrames=810; live.fps=30;
    PHOrbSession *session=StartFixedTest(engine,profile);
    for (unsigned i=0;i<4000;++i) {
        Tick(session,engine,Frame());
        if ([session.snapshot[@"searchTrace"][@"fixedConstraints"][@"valid"] boolValue] ||
            [session.snapshot[@"phase"] isEqual:@"fault"]) break;
    }
    NSDictionary *trace=session.snapshot[@"searchTrace"], *quality=trace[@"attackQuality"];
    Check([trace[@"status"] isEqual:@"solved"] && [quality[@"started"] boolValue] && [quality[@"ordinary"] boolValue] &&
        ![quality[@"selected"] boolValue] &&
        [trace[@"selectedAttemptIndex"] isEqual:@0] && ![trace[@"multiFishingSelected"] boolValue] &&
        [trace[@"goalCount"] isEqual:@2] && [trace[@"evaluation"][@"goalsMet"] boolValue] && !engine->stages,
        "an eligible spinner cannot displace ordinary refinement or its complete baseline on failure");
    NSArray *attempts=trace[@"candidateAttempts"];
    NSMutableSet *singleColors=[NSMutableSet set];
    for (NSDictionary *attempt in attempts) if ([attempt[@"fishingRequested"] boolValue] && ![attempt[@"multiFishing"] boolValue])
        [singleColors addObject:attempt[@"fishColor"]];
    Check(attempts.count==5 && [attempts[1][@"ordinaryQuality"] boolValue] && [attempts[1][@"prefixSteps"] isEqual:@1] &&
        ![attempts[1][@"fishingRequested"] boolValue] && [attempts[1][@"budgetMS"] unsignedIntValue]>[quality[@"budgetMS"] unsignedIntValue]/2 &&
        [quality[@"singleFishingPlan"][@"fishCell"] isEqual:@6] && [quality[@"singleFishingPlan"][@"selectedCount"] isEqual:@2] &&
        singleColors.count==2 && [attempts[4][@"multiFishing"] boolValue] && [quality[@"multiAttempts"] isEqual:@1] &&
        [trace[@"path"] isEqual:attempts[0][@"path"]],
        "one-spinner quality tries the retained prefix first, then both single colors and the independent source in the shared budget");
    CheckSearchAttemptTrace(trace);
    [session queueStop:@"attack quality cleanup"];

    // The same immovable matches without spinners exercise the ordinary
    // fallback. Only one stronger attempt may spend the whole extra budget.
    engine=[[FixedTestEngine alloc] initWithWidth:7 height:6];
    profile=MutableFixedSessionProfile(engine);
    stage=profile[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
    stage[@"actions"]=@[]; stage[@"board"][@"policy"]=@"six_colors_fours";
    profile[@"settings"][@"quality"]=@"thorough";
    auto &plain=engine->live.board;
    std::copy(std::begin(board.types),std::end(board.types),plain.types);
    plain.fixedStart=1; plain.tapeMask=((UINT64_C(1)<<42)-1)&~UINT64_C(3);
    for (unsigned i=0;i<42;++i) plain.flags[i]=(plain.tapeMask&(UINT64_C(1)<<i)) ? PHOrbFlagTape : 0;
    session=StartFixedTest(engine,profile);
    for (unsigned i=0;i<4000;++i) {
        Tick(session,engine,Frame());
        if ([session.snapshot[@"searchTrace"][@"fixedConstraints"][@"valid"] boolValue] ||
            [session.snapshot[@"phase"] isEqual:@"fault"]) break;
    }
    trace=session.snapshot[@"searchTrace"]; quality=trace[@"attackQuality"];
    Check([trace[@"status"] isEqual:@"solved"] && [quality[@"ordinary"] boolValue] &&
        [quality[@"attempts"] isEqual:@1] && ![quality[@"selected"] boolValue] &&
        [trace[@"selectedAttemptIndex"] isEqual:@0] && [trace[@"goalCount"] isEqual:@2] &&
        [trace[@"evaluation"][@"goalsMet"] boolValue] && !engine->stages,
        "ordinary quality runs once and retains the executable original route and goals when no improvement exists");
    attempts=trace[@"candidateAttempts"];
    Check(attempts.count==2 && [attempts[1][@"ordinaryQuality"] boolValue] &&
        [attempts[1][@"prefixSteps"] isEqual:@0] &&
        [attempts[1][@"budgetMS"] unsignedIntValue]>[quality[@"budgetMS"] unsignedIntValue]/2,
        "ordinary refinement receives the unsplit extra budget rather than two repeated half-depth searches");
    CheckSearchAttemptTrace(trace);
    [session queueStop:@"ordinary attack quality cleanup"];
}
