// Synthetic native receipts exercise the new state transitions. These cases
// are intentionally separate from claims about damage, survival or a clear.
static void FixedStateBoard(FixedTestEngine *engine) {
    const uint8_t cells[]={0,0,0,1,2, 1,2,3,4,5, 2,3,4,5,1, 3,4,5,1,2};
    auto &board=engine->live.board;
    std::copy(std::begin(cells),std::end(cells),board.types);
    board.fixedStart=1; board.tapeMask=((UINT64_C(1)<<20)-1)&~UINT64_C(3);
    for (unsigned i=0;i<20;++i) board.flags[i]=(board.tapeMask&(UINT64_C(1)<<i)) ? PHOrbFlagTape : 0;
}
static void FixedSettledTurn(PHOrbSession *s, FixedTestEngine *e) {
    for (unsigned i=0;i<3;++i) Tick(s,e,Frame());
    e->live.phase=2; e->live.moves=0;
}
static void FixedEnemyStateSessionTests() {
    NSArray *survivor=@[@{@"monster":@200038,@"alive":@NO},@{@"monster":@210524,@"alive":@YES,@"maximumPercent":@50}];
    for (unsigned scenario=0;scenario<9;++scenario) {
        FixedTestEngine *e=[[FixedTestEngine alloc] initWithWidth:5 height:4];
        NSMutableDictionary *p=MutableFixedSessionProfile(e), *plan=p[@"fixedPlan"];
        NSMutableDictionary *variant=plan[@"floors"][0][@"variants"][0], *first=variant[@"stages"][0];
        first[@"actions"]=@[];
        first[@"requireEnemyState"]=@[@{@"monster":@200038},@{@"monster":@210524}];
        first[@"repeat"]=@{@"minimumPuzzles":@1,@"maximumPuzzles":@2,
            @"until":@{@"kind":@"enemy_state_or_floor_change",@"enemies":survivor}};
        NSMutableDictionary *finish=[first mutableCopy]; finish[@"id"]=@"survivor";
        finish[@"requireEnemyState"]=survivor;
        finish[@"requireEffectsClear"]=@[@"damage_absorb",@"attribute_absorb",@"damage_void"];
        finish[@"repeat"]=@{@"minimumPuzzles":@1,@"maximumPuzzles":@1,@"until":@{@"kind":@"floor_change"}};
        variant[@"stages"]=@[first,finish];
        NSMutableDictionary *next=[finish mutableCopy]; next[@"id"]=@"next-floor";
        [next removeObjectForKey:@"requireEnemyState"];
        [plan[@"floors"] addObject:@{@"number":@2,@"variants":@[@{@"kind":@"any",@"stages":@[next]}]}];
        auto &fixed=e->live.fixedPlan;
        fixed.count=2; fixed.aliveMask=3; fixed.enemies[1]=fixed.enemies[0];
        fixed.enemies[0].catalogID=200038; fixed.enemies[1].catalogID=210524;
        e->live.automation.activeEnemies=2; FixedStateBoard(e);
        if (scenario==3) fixed.enemies[1].catalogID=10524;
        if (scenario==4) fixed.enemies[1].hpKnown=false;
        if (scenario==5) fixed.enemies[0].catalogID=210524;
        PHOrbSession *s=StartFixedTest(e,p);
        if (scenario>=3 && scenario<=5) {
            AdvanceFixedToSolveOrStop(s,e);
            Check([s.snapshot[@"phase"] isEqual:@"stopped"] && !e->stages && e->skillOrder.empty(),
                "an unknown or different exact enemy roster stops before any fixed-plan action");
        } else if (scenario>=7) {
            if (scenario==7) AdvanceFixedToSolveOrStop(s,e);
            else Check(UntilPhase(s,e,@"running"),"state guard fixture owns a held gesture");
            const NSUInteger sent=e->stages;
            fixed.enemies[1].catalogID=10524;
            for (unsigned i=0;i<100 && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i) Tick(s,e,Frame());
            Check([s.snapshot[@"phase"] isEqual:@"stopped"] && e->stages==sent+(scenario==8 ? 1u : 0u) && !e->live.dragged,
                "an enemy identity change revokes a planned or held gesture without another movement");
        } else {
            Check(UntilPhase(s,e,@"turnWaiting"),"state branch fixture confirms its first puzzle release");
            FixedSettledTurn(s,e);
            if (scenario==6) {
                e->live.wave=1;
                AdvanceFixedToSolveOrStop(s,e);
                Check([s.snapshot[@"fixedPlanProgress"][@"stage"] isEqual:@"next-floor"] &&
                    [s.snapshot[@"fixedPlanProgress"][@"floor"] isEqual:@2],
                    "a real floor advance after the original attack bypasses the survivor stage");
            } else {
                if (scenario==0) { fixed.enemies[0].hp=0; fixed.enemies[1].hp=500; fixed.aliveMask=2; }
                AdvanceFixedToSolveOrStop(s,e);
                if (scenario!=0) {
                    Check([s.snapshot[@"fixedPlanProgress"][@"stage"] isEqual:@"opening"],
                        "two living enemies remain in the original bounded attack phase");
                    Check(UntilPhase(s,e,@"turnWaiting"),"state branch confirms its second original attack");
                    FixedSettledTurn(s,e);
                    fixed.enemies[0].hp=0; fixed.enemies[1].hp=scenario==2 ? 501 : 500; fixed.aliveMask=2;
                    AdvanceFixedToSolveOrStop(s,e);
                }
                if (scenario==2) Check([s.snapshot[@"phase"] isEqual:@"stopped"] &&
                    [s.snapshot[@"fixedPlanProgress"][@"lastStop"][@"code"] isEqual:@"stage-puzzle-limit"],
                    "a survivor above half HP cannot authorize a third attack");
                else {
                    Check([s.snapshot[@"fixedPlanProgress"][@"stage"] isEqual:@"survivor"],
                        "fresh exact survivor evidence advances after either one or two original attacks");
                    NSArray *history=s.snapshot[@"executionHistory"];
                    NSDictionary *result=[history lastObject][@"fixedPlanAfterTurn"];
                    Check([result[@"enemies"][0][@"hp"] isEqual:@0] && [result[@"enemies"][1][@"hp"] isEqual:@500],
                        "the state authorizing a survivor board is also retained on the completed puzzle record");
                    Check(UntilPhase(s,e,@"turnWaiting"),"the one authorized survivor board completes");
                    FixedSettledTurn(s,e); AdvanceFixedToSolveOrStop(s,e);
                    Check([s.snapshot[@"phase"] isEqual:@"stopped"] &&
                        [s.snapshot[@"fixedPlanProgress"][@"lastStop"][@"maximumPuzzles"] isEqual:@1] &&
                        [s.snapshot[@"executionHistory"] count]==(scenario ? 3u : 2u) &&
                        [s.snapshot[@"executionHistory"][[history count]-1][@"fixedPlanAfterTurn"] isEqual:result],
                        "an uncleared survivor gets no fourth attack and later turns cannot rewrite its preceding result");
                }
            }
        }
        [s queueStop:@"fixed state fixture cleanup"];
    }
}
static void FixedEffectPreparationSessionTests() {
    for (unsigned scenario=0;scenario<5;++scenario) {
        FixedTestEngine *e=[FixedTestEngine new]; NSMutableDictionary *p=MutableFixedSessionProfile(e);
        NSMutableDictionary *v=p[@"fixedPlan"][@"floors"][0][@"variants"][0], *prep=v[@"stages"][0];
        NSMutableDictionary *a=[prep[@"actions"][0] mutableCopy];
        a[@"slot"]=@5; a[@"monster"]=@104; a[@"when"]=@"damage_absorb"; a[@"optional"]=@YES; a[@"frequency"]=@"each";
        prep[@"actions"]=@[a];
        prep[@"repeat"]=@{@"minimumPuzzles":@0,@"maximumPuzzles":@8,
            @"until":@{@"kind":@"effects_clear",@"effects":@[@"damage_absorb"]}};
        NSMutableDictionary *finish=[prep mutableCopy]; finish[@"id"]=@"finish"; finish[@"actions"]=@[];
        finish[@"requireEffectsClear"]=@[@"damage_absorb"];
        finish[@"repeat"]=@{@"minimumPuzzles":@1,@"maximumPuzzles":@1,@"until":@{@"kind":@"floor_change"}};
        v[@"stages"]=@[prep,finish]; e->live.automation.enemyEffects=PHOrbEnemyDamageAbsorb;
        if (scenario==0) e->live.automation.playerBypass=PHOrbEnemyDamageAbsorb;
        if (scenario==1) e->bypassAfterSkillSlotPlusOne=5;
        if (scenario==2) e->unavailableSkills[4]=YES;
        if (scenario==4) e->unknownReady=YES;
        PHOrbSession *s=StartFixedTest(e,p); AdvanceFixedToSolveOrStop(s,e);
        if (scenario<2) Check([s.snapshot[@"fixedPlanProgress"][@"stage"] isEqual:@"finish"] &&
            e->skillOrder.size()==(scenario ? 1u : 0u) && !e->stages,
            "covered bypass skips preparation; expired bypass needs the actual native skill effect before advancing without a stall board");
        if (scenario==2 || scenario==3) Check([s.snapshot[@"fixedPlanProgress"][@"stage"] isEqual:@"opening"] &&
            e->skillOrder.size()==(scenario==3 ? 1u : 0u),
            "unready Solomon or a skill receipt without resulting bypass cannot finish preparation");
        if (scenario==4) Check([s.snapshot[@"phase"] isEqual:@"stopped"] && e->skillOrder.empty() && !e->stages,
            "unknown skill readiness cannot masquerade as an optional unready skill");
        [s queueStop:@"effect preparation cleanup"];
    }
    for (unsigned scenario=0;scenario<5;++scenario) {
        FixedTestEngine *e=[FixedTestEngine new]; NSMutableDictionary *p=MutableFixedSessionProfile(e);
        NSMutableDictionary *stage=p[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        stage[@"actions"][0][@"when"]=@"unmatchable";
        stage[@"requireEffectsClear"]=@[@"unmatchable"];
        e->live.unmatchableTurns=2; e->live.board.unmatchableColors=26;
        if (scenario==0 || scenario==4) e->clearUnmatchableAfterSkillSlotPlusOne=1;
        if (scenario==1) e->unavailableSkills[0]=YES;
        if (scenario==2) e->live.unmatchableKnown=false;
        PHOrbSession *s=StartFixedTest(e,p); AdvanceFixedToSolveOrStop(s,e);
        if (scenario==0) Check([s.snapshot[@"phase"] isEqual:@"solving"] && e->skillOrder.size()==1,
            "opening recovery permits planning only after native unmatchable has actually cleared");
        else if (scenario==4) {
            e->live.unmatchableTurns=1;
            for (unsigned i=0;i<20 && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i) Tick(s,e,Frame());
            Check([s.snapshot[@"phase"] isEqual:@"stopped"] && !e->stages,
                "unmatchable returning during planning revokes the opening board");
        } else Check([s.snapshot[@"phase"] isEqual:@"stopped"] && !e->stages &&
            e->skillOrder.size()==(scenario==3 ? 1u : 0u),
            "an unready recovery skill, unknown effect or uncleared native result blocks all opening input");
        [s queueStop:@"opening recovery cleanup"];
    }
}
static void FixedThresholdAndLoopSessionTests() {
    for (unsigned scenario=0;scenario<3;++scenario) {
        FixedTestEngine *e=[FixedTestEngine new]; NSMutableDictionary *p=MutableFixedSessionProfile(e);
        NSMutableDictionary *v=p[@"fixedPlan"][@"floors"][0][@"variants"][0], *threshold=v[@"stages"][0];
        threshold[@"repeat"]=@{@"minimumPuzzles":@0,@"maximumPuzzles":@2,@"until":@{@"kind":@"enemy_hp_at_most",@"percent":@50}};
        NSMutableDictionary *cleanse=[threshold mutableCopy]; cleanse[@"id"]=@"cleanse";
        cleanse[@"actions"]=@[]; cleanse[@"repeat"]=@{@"minimumPuzzles":@1,@"maximumPuzzles":@2,@"until":@{@"kind":@"floor_change"}};
        v[@"stages"]=@[threshold,cleanse];
        e->live.fixedPlan.enemies[0].hp=scenario ? 501 : 500;
        if (scenario==2) e->live.fixedPlan.enemies[0].hpKnown=false;
        PHOrbSession *s=StartFixedTest(e,p); AdvanceFixedToSolveOrStop(s,e);
        Check(e->skillOrder.size()==(scenario==1 ? 1u : 0u) &&
            (scenario==2 ? [s.snapshot[@"phase"] isEqual:@"stopped"] :
             [s.snapshot[@"fixedPlanProgress"][@"stage"] isEqual:(scenario ? @"opening" : @"cleanse")]),
            "the early HP checkpoint skips attacks at half HP, preserves them above half, and stops on unknown HP");
        [s queueStop:@"threshold cleanup"];
    }
    for (unsigned scenario=0;scenario<3;++scenario) {
        FixedTestEngine *e=[[FixedTestEngine alloc] initWithWidth:5 height:4];
        NSMutableDictionary *p=MutableFixedSessionProfile(e), *v=p[@"fixedPlan"][@"floors"][0][@"variants"][0];
        NSMutableDictionary *first=v[@"stages"][0], *a=first[@"actions"][0];
        a[@"loop"]=@YES; first[@"repeat"][@"until"]=@{@"kind":@"puzzles"};
        NSMutableDictionary *next=[first mutableCopy]; next[@"id"]=@"reserve-or-resize";
        next[@"repeat"]=@{@"minimumPuzzles":@1,@"maximumPuzzles":@1,@"until":@{@"kind":@"floor_change"}};
        if (scenario==0) { next[@"actions"]=@[]; next[@"holdLoops"]=@[@1]; }
        else { NSMutableDictionary *required=[a mutableCopy]; required[@"requireReady"]=@YES; next[@"actions"]=@[required]; }
        v[@"stages"]=@[first,next]; FixedStateBoard(e);
        PHOrbSession *s=StartFixedTest(e,p);
        Check(UntilPhase(s,e,@"turnWaiting"),"loop fixture activates and acknowledges its original loop");
        FixedSettledTurn(s,e); e->unavailableSkills[0]=scenario==1;
        AdvanceFixedToSolveOrStop(s,e);
        Check(e->skillOrder.size()==(scenario==2 ? 2u : 1u) &&
            [s.snapshot[@"phase"] isEqual:(scenario==1 ? @"stopped" : @"solving")] &&
            [s.snapshot[@"fixedPlanProgress"][@"activeLoops"] count]==1,
            "holding a ready loop preserves it, mandatory resize cannot be skipped, and a ready explicit loop cast still runs");
        [s queueStop:@"loop reservation cleanup"];
    }
}
