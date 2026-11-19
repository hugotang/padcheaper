// Keep the released layout fixed with one movable equal-colour pair. The
// assertions concern Session rejection/diagnostics, not a beam-search result.
static void FixedConstraintDiagnosticSessionTests() {
    for (unsigned scenario=0;scenario<6;++scenario) {
        const bool wide=scenario!=3;
        FixedTestEngine *engine=[[FixedTestEngine alloc] initWithWidth:(wide ? 7 : 5) height:(wide ? 6 : 4)];
        NSMutableDictionary *profile=MutableFixedSessionProfile(engine);
        NSMutableDictionary *stage=profile[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        stage[@"actions"]=@[];
        stage[@"board"][@"policy"]=scenario<2 || scenario>=4 ? @"six_colors_fours" : scenario==2 ? @"six_colors_no_fours" : @"one_combo";
        auto &board=engine->live.board;
        if (!scenario) {
            // Sanitized released layout from the B2 report: fire joins only
            // after existing-orb cascades, while generic goals are 2/2.
            const uint8_t layout[]={4,2,5,3,0,2,1, 2,2,2,2,0,3,2, 3,4,5,2,1,3,2,
                3,4,5,1,1,1,5, 3,4,5,1,1,5,3, 1,2,5,2,0,1,3};
            std::copy(std::begin(layout),std::end(layout),board.types);
            board.spinnerMask=UINT64_C(1)<<31; board.spinnerColors=63;
            engine->live.fps=30; engine->live.remainingFrames=128;
            engine->live.spinnerCountdownKnown=engine->live.orbMotionKnown=true;
            engine->live.spinnerCountdown=engine->live.spinnerPeriodFrames=30;
        } else if (scenario==1) {
            // Six horizontal triples plus unused heart tokens: inventory
            // permits a heart four, but this fixed layout has none.
            for (unsigned y=0;y<6;++y) for (unsigned x=0;x<7;++x)
                board.types[y*7+x]=uint8_t(x<3 ? y : (y+x-2)%6);
        } else if (scenario==2) {
            for (unsigned y=0;y<6;++y) for (unsigned x=0;x<7;++x)
                board.types[y*7+x]=uint8_t(x<4 ? y : (y+x-3)%6);
        } else if (scenario==3) {
            const uint8_t layout[]={0,0,0,0,2, 1,2,3,4,5, 2,3,4,5,1, 3,4,5,1,2};
            std::copy(std::begin(layout),std::end(layout),board.types);
        } else if (scenario==5) {
            // Exact released B4 finish: six colours, heart four, three fours.
            const uint8_t layout[]={0,5,0,3,4,5,2,3,1,5,3,4,2,0,0,1,4,2,4,5,3,
                0,1,0,2,0,5,2,0,1,4,2,1,5,2,3,3,3,2,1,5,4};
            std::copy(std::begin(layout),std::end(layout),board.types);
            stage[@"board"][@"requireSixColorFours"]=@YES;
        } else {
            // Every colour has enough inventory, but the only legal equal-
            // colour exchange leaves a layout with no matched group at all.
            for (unsigned y=0;y<6;++y) for (unsigned x=0;x<7;++x)
                board.types[y*7+x]=uint8_t((x+y)%6);
            board.types[1]=board.types[0];
        }
        const unsigned first=scenario==0 ? 7 : scenario==5 ? 15 : 0, size=board.width*board.height;
        const unsigned second=first+(scenario==5 ? 7 : 1);
        board.fixedStart=uint8_t(first+1);
        board.tapeMask=((UINT64_C(1)<<size)-1) & ~((UINT64_C(1)<<first)|(UINT64_C(1)<<second));
        for (unsigned i=0;i<size;++i) board.flags[i]=board.tapeMask&(UINT64_C(1)<<i) ? PHOrbFlagTape : 0;
        PHOrbSession *session=StartFixedTest(engine,profile);
        for (unsigned i=0;i<4000;++i) {
            Tick(session,engine,Frame());
            if ([session.snapshot[@"searchTrace"][@"fixedConstraints"][@"valid"] boolValue] ||
                [session.snapshot[@"phase"] isEqual:@"fault"]) break;
        }
        NSDictionary *snapshot=session.snapshot, *trace=snapshot[@"searchTrace"], *fixed=trace[@"fixedConstraints"];
        NSString *message=snapshot[@"message"];
        Check([fixed[@"applicable"] boolValue] && ![fixed[@"satisfied"] boolValue] &&
            [trace[@"status"] isEqual:@"goals-unmet"] && [snapshot[@"phase"] isEqual:@"stopped"] &&
            !engine->stages && ![snapshot[@"canExecute"] boolValue] && [message isEqual:trace[@"reason"]] &&
            [NSJSONSerialization isValidJSONObject:fixed],
            "fixed constraint diagnostics preserve refusal, retained reason and serializable evidence without input");
        if (!scenario) Check([fixed[@"missingFirstColors"] isEqual:@1] &&
            [fixed[@"missingFirstColorNames"] isEqual:@"火"] &&
            [trace[@"evaluation"][@"goalHits"] isEqual:@[@6,@1]] &&
            [trace[@"colorPhases"][@"missingInAnyFailedPhase"] isEqual:@0] &&
            [message containsString:@"盤面目標已達成 2／2"] && [message containsString:@"首批缺少火"] &&
            ![message containsString:@"僅達成 2／2"],
            "a cascade-only fire match reports the separate first-removal deficit without contradicting generic 2/2 goals");
        if (scenario==1) Check([fixed[@"missingFirstColors"] isEqual:@0] &&
            [fixed[@"firstWaveHeartFoursMinimum"] isEqual:@0] && [message containsString:@"要求首批心四連"],
            "six first-removal colors do not conceal the missing heart-four requirement");
        if (scenario==2) Check([fixed[@"resolvedFoursMaximum"] isEqual:@6] &&
            [message containsString:@"禁止四顆消除"] && [message containsString:@"最多有 6 組"],
            "stall rejection names forbidden four-orb groups including existing-orb cascades");
        if (scenario==3) Check([fixed[@"exactRemovalOrbs"] isEqual:@3] &&
            [fixed[@"erasedMinimum"] isEqual:@4] && [message containsString:@"只允許一組 3 顆"] &&
            ![message containsString:@"僅達成 0／0"],
            "an exact single-combo rejection explains its group size without inventing zero missing goals");
        if (scenario==4) Check([fixed[@"missingFirstColors"] isEqual:@63] &&
            [fixed[@"missingFirstColorNames"] isEqual:@"火／水／木／光／闇／心"] &&
            [fixed[@"deficits"][0][@"missingColorNames"] isEqual:@"火／水／木／光／闇／心"] &&
            [message containsString:@"首批缺少火／水／木／光／闇／心"] && ![message containsString:@"首批缺少任意珠色"],
            "all six missing first-removal colours are listed explicitly instead of the generic any-colour label");
        if (scenario==5) Check([fixed[@"requireSixColorFours"] boolValue] &&
            [fixed[@"missingFirstFourColors"] unsignedIntValue] &&
            [message containsString:@"首批六色各一組四顆"] && [trace[@"evaluation"][@"firstWaveFours"] isEqual:@3],
            "Session refuses a weak B4 attack and names its missing four colours before staging input");
        [session queueStop:@"fixed diagnostic cleanup"];
    }
}

static void FixedUnmatchableDiagnosticSessionTests() {
    for (unsigned scenario=0;scenario<9;++scenario) {
        FixedTestEngine *e=[[FixedTestEngine alloc] initWithWidth:7 height:6];
        NSMutableDictionary *p=MutableFixedSessionProfile(e);
        NSMutableDictionary *stage=p[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        stage[@"id"]=@"finish";
        stage[@"board"][@"policy"]=scenario==1 ? @"six_colors_no_fours" : scenario==2 ? @"six_colors" :
            scenario==6 ? @"one_combo" : @"six_colors_fours";
        // Sanitized 0.5.56 B2 inventory: six dark tokens are present, but dark
        // is unmatchable. A spinner cannot repair the fixed six-colour policy.
        const uint8_t cells[]={1,2,3,1,1,1,4, 2,5,0,4,2,2,4, 1,3,3,0,0,3,5,
            4,0,1,2,3,0,0,3,5,1,2,3,2,5, 2,4,4,5,1,0,0};
        std::copy(std::begin(cells),std::end(cells),e->live.board.types);
        e->live.board.unmatchableColors=scenario==3 ? 63 : scenario==4 || scenario==7 ? 0 :
            scenario==8 ? (1u<<PHOrbJammer) : 16;
        e->live.unmatchableKnown=scenario!=4;
        e->live.unmatchableTurns=scenario==7 ? 0 : 1;
        if (!scenario) {
            e->live.board.spinnerMask=UINT64_C(1)<<39; e->live.board.spinnerColors=63;
            e->live.spinnerCountdownKnown=e->live.orbMotionKnown=true;
            e->live.spinnerCountdown=18; e->live.spinnerPeriodFrames=30;
            e->live.fps=30; e->live.remainingFrames=810;
        }
        auto &enemy=e->live.fixedPlan.enemies[0];
        enemy.catalogID=200974; enemy.hp=UINT64_C(139369497786); enemy.maximumHP=UINT64_C(330000000000);
        if (scenario==5) e->clearUnmatchableAfterSkillSlotPlusOne=1;
        PHOrbSession *s=StartFixedTest(e,p); AdvanceFixedToSolveOrStop(s,e);
        NSDictionary *snapshot=s.snapshot, *stop=snapshot[@"fixedPlanProgress"][@"lastStop"];
        if (scenario<5) {
            Check([snapshot[@"phase"] isEqual:@"stopped"] && !e->stages && !e->releases && e->skillOrder.size()==1 &&
                ![snapshot[@"searchTrace"] count] && [stop[@"scope"] isEqual:@"fixed-board-preconditions"] &&
                [stop[@"requiredFirstColors"] isEqual:@63] && [stop[@"stage"] isEqual:@"finish"] &&
                [stop[@"reason"] isEqual:snapshot[@"message"]] && [stop[@"consumed"] boolValue] &&
                ![stop[@"continuationAllowed"] boolValue] && ![stop[@"latentUnmatchableRecoverySupported"] boolValue] &&
                [NSJSONSerialization isValidJSONObject:stop],
                "unmatchable fixed colours stop after prescribed skills, before solver/fishing/input, with frozen diagnostics");
            if (scenario==4) Check([stop[@"code"] isEqual:@"unmatchable-state-unknown"] &&
                ![stop[@"unmatchableKnown"] boolValue] && stop[@"unmatchableColors"]==NSNull.null &&
                stop[@"unmatchableTurns"]==NSNull.null && stop[@"blockedColors"]==NSNull.null &&
                stop[@"blockedColorNames"]==NSNull.null,
                "unknown unmatchable evidence never becomes a clear mask or a guessed blocked colour");
            else Check([stop[@"code"] isEqual:@"required-colors-unmatchable"] &&
                [stop[@"blockedColors"] isEqual:(scenario==3 ? @63 : @16)] &&
                [stop[@"blockedColorNames"] isEqual:(scenario==3 ? @"火／水／木／光／闇／心" : @"闇")] &&
                [stop[@"unmatchableTurns"] isEqual:@1] && [snapshot[@"message"] containsString:@"轉轉取色無法解除不可消"] &&
                [stop[@"enemies"][0][@"hp"] unsignedLongLongValue]==UINT64_C(139369497786),
                "the blocker names all unmatchable required colours and retains native 64-bit enemy HP");
            e->live.board.unmatchableColors=0; e->live.unmatchableTurns=0;
            e->live.fixedPlan.enemies[0].hp=1;
            for (unsigned i=0;i<15;++i) Tick(s,e,Frame());
            [s queueAutomatic];
            for (unsigned i=0;i<30;++i) Tick(s,e,Frame());
            Check([s.snapshot[@"fixedPlanProgress"][@"lastStop"] isEqual:stop] && e->skillOrder.size()==1 && !e->stages,
                "later reads and a refused consumed-run restart cannot erase the blocker or replay its skill");
        } else Check([snapshot[@"phase"] isEqual:@"solving"] && !stop.count && e->skillOrder.size()==1 && !e->stages,
            "native cleansing, a clear board, unrelated special colours and non-six-colour policies are not hard-blocked");
        [s queueStop:@"unmatchable diagnostic cleanup"];
    }
    for (unsigned held=0;held<2;++held) {
        FixedTestEngine *e=[[FixedTestEngine alloc] initWithWidth:7 height:6];
        NSMutableDictionary *p=MutableFixedSessionProfile(e);
        NSMutableDictionary *stage=p[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        stage[@"actions"]=@[]; stage[@"board"][@"policy"]=@"six_colors_fours";
        // Six exact four-orb groups. Only the equal-colour pair
        // may move, keeping the revocation case independent of search quality.
        auto &board=e->live.board;
        for (unsigned y=0;y<6;++y) for (unsigned x=0;x<7;++x)
            board.types[y*7+x]=uint8_t(x<4 ? y : (y+x-3)%6);
        board.fixedStart=1; board.tapeMask=((UINT64_C(1)<<42)-1)&~UINT64_C(3);
        for (unsigned i=0;i<42;++i) board.flags[i]=(board.tapeMask&(UINT64_C(1)<<i)) ? PHOrbFlagTape : 0;
        PHOrbSession *s=StartFixedTest(e,p);
        if (held) Check(UntilPhase(s,e,@"running"),"unmatchable revocation fixture owns a held gesture");
        else AdvanceFixedToSolveOrStop(s,e);
        const NSUInteger sent=e->stages;
        e->live.unmatchableTurns=1; board.unmatchableColors=16;
        for (unsigned i=0;i<100 && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i) Tick(s,e,Frame());
        Check([s.snapshot[@"phase"] isEqual:@"stopped"] &&
            [s.snapshot[@"fixedPlanProgress"][@"lastStop"][@"code"] isEqual:@"required-colors-unmatchable"] &&
            e->stages==sent+(held ? 1u : 0u) && !e->live.dragged,
            "unmatchable appearing during search or a held gesture revokes the route without further movement");
        [s queueStop:@"unmatchable revocation cleanup"];
    }
}
