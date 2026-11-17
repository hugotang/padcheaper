// Keep the released layout fixed with one movable equal-colour pair. The
// assertions concern Session rejection/diagnostics, not a beam-search result.
static void FixedConstraintDiagnosticSessionTests() {
    for (unsigned scenario=0;scenario<5;++scenario) {
        const bool wide=scenario!=3;
        FixedTestEngine *engine=[[FixedTestEngine alloc] initWithWidth:(wide ? 7 : 5) height:(wide ? 6 : 4)];
        NSMutableDictionary *profile=MutableFixedSessionProfile(engine);
        NSMutableDictionary *stage=profile[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        stage[@"actions"]=@[];
        stage[@"board"][@"policy"]=scenario<2 || scenario==4 ? @"six_colors_fours" : scenario==2 ? @"six_colors_no_fours" : @"one_combo";
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
        } else {
            // Every colour has enough inventory, but the only legal equal-
            // colour exchange leaves a layout with no matched group at all.
            for (unsigned y=0;y<6;++y) for (unsigned x=0;x<7;++x)
                board.types[y*7+x]=uint8_t((x+y)%6);
            board.types[1]=board.types[0];
        }
        const unsigned first=scenario==0 ? 7 : 0, size=board.width*board.height;
        board.fixedStart=uint8_t(first+1);
        board.tapeMask=((UINT64_C(1)<<size)-1) & ~((UINT64_C(1)<<first)|(UINT64_C(1)<<(first+1)));
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
        [session queueStop:@"fixed diagnostic cleanup"];
    }
}
