// Cap-stop diagnostics must use the settled native turn that reached the cap,
// preserve unknown facts, and never issue a speculative extra action.
static void FixedLimitDiagnosticSessionTests() {
    for (unsigned scenario=0;scenario<7;++scenario) {
        FixedTestEngine *engine=[[FixedTestEngine alloc] initWithWidth:5 height:4];
        NSMutableDictionary *profile=MutableFixedSessionProfile(engine);
        NSMutableDictionary *variant=profile[@"fixedPlan"][@"floors"][0][@"variants"][0];
        NSMutableDictionary *stage=variant[@"stages"][0];
        stage[@"actions"]=@[];
        if (scenario==3 || scenario==6) stage[@"repeat"][@"until"]=@{@"kind":@"enemy_hp_at_most",@"percent":@10};
        if (scenario==4) {
            stage[@"repeat"][@"until"]=@{@"kind":@"skill_ready",@"slot":@2,@"monster":@101,@"skill":@700};
            engine->unavailableSkills[1]=YES;
        }
        if (scenario==5) stage[@"repeat"][@"until"]=@{@"kind":@"puzzles"};
        if (scenario>=3) {
            // Nonterminal predicates require a later floor-change stage in
            // the real profile schema, even when this fixture stops at cap.
            NSMutableDictionary *next=[stage mutableCopy]; next[@"id"]=@"next";
            next[@"repeat"]=@{@"minimumPuzzles":@1,@"maximumPuzzles":@1,@"until":@{@"kind":@"floor_change"}};
            variant[@"stages"]=@[stage,next];
        }
        const uint8_t cells[]={0,0,0,1,2, 1,2,3,4,5, 2,3,4,5,1, 3,4,5,1,2};
        auto &board=engine->live.board;
        std::copy(std::begin(cells),std::end(cells),board.types);
        board.fixedStart=1; board.tapeMask=((UINT64_C(1)<<20)-1)&~UINT64_C(3);
        for (unsigned i=0;i<20;++i) board.flags[i]=(board.tapeMask&(UINT64_C(1)<<i)) ? PHOrbFlagTape : 0;
        PHOrbSession *session=StartFixedTest(engine,profile);
        Check(UntilPhase(session,engine,@"turnWaiting"),"fixed cap fixture completes one acknowledged native puzzle");
        for (unsigned i=0;i<3;++i) Tick(session,engine,Frame());
        const NSUInteger stages=engine->stages, releases=engine->releases;
        auto &enemy=engine->live.fixedPlan.enemies[0];
        enemy.hp=UINT64_C(135342484374); enemy.maximumHP=UINT64_C(340000000000);
        engine->live.automation.enemyEffects=PHOrbEnemyAttributeAbsorb|PHOrbEnemyDamageVoid;
        engine->live.automation.playerBypass=PHOrbEnemyAttributeAbsorb;
        if (!scenario) {
            engine->live.fixedPlan.count=2;
            auto &part=engine->live.fixedPlan.enemies[1];
            part=enemy; part.flags=0x100u; part.hp=1;
        }
        if (scenario==1 || scenario==6) enemy.hpKnown=false;
        if (scenario==2) engine->live.automation.enemiesKnown=false;
        engine->live.phase=2; engine->live.moves=0;
        for (unsigned i=0;i<100;++i) {
            Tick(session,engine,Frame());
            if ([session.snapshot[@"phase"] isEqual:@"stopped"] ||
                [session.snapshot[@"fixedPlanProgress"][@"stage"] isEqual:@"next"]) break;
        }
        NSDictionary *snapshot=session.snapshot, *progress=snapshot[@"fixedPlanProgress"], *stop=progress[@"lastStop"];
        if (scenario==5) {
            Check([progress[@"stage"] isEqual:@"next"] && !stop.count && [snapshot[@"automatic"] boolValue],
                "a satisfied completion predicate advances before checking the same puzzle cap");
        } else if (scenario==6) {
            Check([snapshot[@"phase"] isEqual:@"stopped"] && !stop.count &&
                [snapshot[@"message"] containsString:@"完成條件未知"],
                "an unknown completion predicate retains its separate stop instead of inventing cap evidence");
        } else {
            Check([snapshot[@"phase"] isEqual:@"stopped"] && [stop[@"code"] isEqual:@"stage-puzzle-limit"] &&
                [stop[@"confirmedPuzzles"] isEqual:@1] && [stop[@"maximumPuzzles"] isEqual:@1] &&
                ![stop[@"completionSatisfied"] boolValue] && ![stop[@"continuationAllowed"] boolValue] &&
                ![stop[@"survivalKnown"] boolValue] && [stop[@"reason"] isEqual:snapshot[@"message"]] &&
                engine->stages==stages && engine->releases==releases && engine->skillOrder.empty() && !engine->targetCalls &&
                [NSJSONSerialization isValidJSONObject:stop],
                "the native cap stops without an extra puzzle, target or skill and exports serializable frozen evidence");
            NSDictionary *body=[stop[@"enemies"] firstObject];
            if (!scenario) Check([stop[@"enemies"] count]==1 && [stop[@"livingBodies"] isEqual:@1] &&
                [body[@"hp"] unsignedLongLongValue]==UINT64_C(135342484374) &&
                [body[@"remainingPercent"] doubleValue]>39.80 && [body[@"remainingPercent"] doubleValue]<39.82 &&
                [stop[@"enemyEffects"] isEqual:@6] && [stop[@"playerBypass"] isEqual:@2] &&
                [stop[@"uncoveredEffects"] isEqual:@4] && [snapshot[@"message"] containsString:@"敵人仍存活"] &&
                [snapshot[@"message"] containsString:@"39.81%"] && [snapshot[@"message"] containsString:@"傷害無效"],
                "the cap reports fresh 64-bit boss HP and uncovered effects while excluding parts");
            if (scenario==1 || scenario==2) Check(![body[@"hpKnown"] boolValue] &&
                body[@"hp"]==NSNull.null && !body[@"remainingPercent"] && stop[@"livingBodies"]==NSNull.null &&
                ![snapshot[@"message"] containsString:@"39.81%"] && ![snapshot[@"message"] containsString:@"敵人仍存活"],
                "unknown body HP does not become zero, a ratio or an asserted survivor");
            if (scenario==2) Check(![stop[@"effectsKnown"] boolValue] && stop[@"enemyEffects"]==NSNull.null &&
                stop[@"playerBypass"]==NSNull.null && stop[@"uncoveredEffects"]==NSNull.null &&
                [snapshot[@"message"] containsString:@"狀態未知"],
                "unknown aggregate effect evidence is retained as unknown rather than no active effects");
            if (scenario==3) Check([stop[@"until"][@"percent"] isEqual:@10] &&
                [snapshot[@"message"] containsString:@"HP 尚未達到指定門檻"],
                "an unmet HP checkpoint is distinguished from a floor-change condition");
            if (scenario==4) Check([stop[@"readinessKnownSlots"] isEqual:@2] && [stop[@"readySlots"] isEqual:@0] &&
                [snapshot[@"message"] containsString:@"指定技能尚未就緒"],
                "only the actually probed skill-ready predicate contributes readiness evidence");
            else Check([stop[@"readinessKnownSlots"] isEqual:@0],
                "ordinary cap diagnostics do not call native skill getters to infer follow-up readiness");
            engine->live.fixedPlan.enemies[0].hp=1; engine->live.automation.enemyEffects=0;
            for (unsigned i=0;i<15;++i) Tick(session,engine,Frame());
            Check([session.snapshot[@"fixedPlanProgress"][@"lastStop"] isEqual:stop],
                "later idle reads cannot rewrite the native evidence captured at the stage cap");
            [session queueAutomatic];
            for (unsigned i=0;i<30;++i) Tick(session,engine,Frame());
            Check([session.snapshot[@"phase"] isEqual:@"stopped"] && engine->stages==stages &&
                [session.snapshot[@"fixedPlanProgress"][@"lastStop"] isEqual:stop],
                "the consumed-run guard prevents a restart from replaying the capped puzzle or erasing its evidence");
        }
        [session queueStop:@"fixed cap diagnostic cleanup"];
    }
}
