// Fixed-plan state-machine fixtures. Written separately from the real solver
// contracts so native acknowledgement/identity behavior is observable.
@interface FixedTestEngine : FakeOrbEngine {
@public
    BOOL unknownFixed, unknownReady, ignoreTarget, swapOnTransform;
    NSUInteger targetCalls, lastTargetSlot;
}
@end
@implementation FixedTestEngine
- (void)nativeInputFrame {
    const bool swap=swapOnTransform && skillFrames==1 && activeSkill==4 && transformAfterSkill[4];
    [super nativeInputFrame];
    if (swap) std::swap(live.automation.members[0],live.automation.members[4]);
}
- (BOOL)readFixedPlan:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (unknownFixed) { if (reason) *reason=@"fixture unknown fixed evidence"; return NO; }
    state->fixedPlan=live.fixedPlan; return state->fixedPlan.valid;
}
- (PHOrbSkillResult)fixedSkillReadiness:(NSUInteger)slot request:(PHOrbSkillRequest)request expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (unknownReady || !PHOrbSameAutomation(expected,&live)) return PHOrbSkillRejected;
    if (!live.automation.window) return PHOrbSkillWindowClosed;
    if (request.source!=chosenSource[slot] || request.skill!=chosenSkill[slot]) return PHOrbSkillRejected;
    return unavailableSkills[slot] ? PHOrbSkillUnavailable : PHOrbSkillStarted;
}
- (BOOL)stageFixedPlanTarget:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++targetCalls; lastTargetSlot=slot; if (!ignoreTarget) live.fixedPlan.target=int8_t(slot); return YES;
}
@end

static NSMutableDictionary *FixedSessionProfile(FixedTestEngine *engine) {
    NSMutableArray *members=[NSMutableArray array], *team=[NSMutableArray array];
    for (unsigned i=0;i<6;++i) {
        unsigned n=100+i; [team addObject:@(n)];
        [members addObject:@{@"body":@(n),@"assist":@0,@"forms":@[@{@"number":@(n),@"skills":@[@700]}],@"assistSkills":@[]}];
        auto &m=engine->live.automation.members[i];
        m.sourceCatalogIDs[0]=n; m.temporaryKnown=true; m.identityNumber=n;
        engine->chosenSource[i]=PHOrbSkillBody; engine->chosenSkill[i]=700;
    }
    engine->live.fixedPlan.valid=engine->live.fixedPlan.targetKnown=true;
    engine->live.fixedPlan.count=engine->live.fixedPlan.aliveMask=1; engine->live.fixedPlan.target=-1;
    auto &enemy=engine->live.fixedPlan.enemies[0]; enemy.hp=enemy.maximumHP=1000;
    enemy.hpKnown=enemy.identityKnown=enemy.targetable=true; enemy.catalogID=42;
    NSDictionary *action=@{@"kind":@"skill",@"slot":@1,@"source":@"body",@"monster":@100,@"skill":@700,
        @"afterMonster":@0,@"optional":@NO,@"frequency":@"once",@"when":@"always"};
    NSDictionary *stage=@{@"id":@"opening",@"actions":@[action],
        @"board":@{@"policy":@"one_combo",@"assistRecoveryL":@NO,@"maxGestureMS":@0},
        @"repeat":@{@"minimumPuzzles":@1,@"maximumPuzzles":@1,@"until":@{@"kind":@"floor_change"}}};
    return [@{@"id":@"fixed-session",@"name":@"固定測試",@"team":team,@"dungeon":@0,@"floor":@0,@"source":@"fixture",
        @"settings":@{@"quality":@"fast",@"mode":@"combo",@"goals":@[],@"rules":@[]},
        @"fixedPlan":@{@"schema":@1,@"members":members,@"floors":@[@{@"number":@1,
            @"variants":@[@{@"kind":@"any",@"stages":@[stage]}]}]}} mutableCopy];
}
static PHOrbSession *StartFixedTest(FixedTestEngine *engine, NSDictionary *profile) {
    PHOrbSession *s=[[PHOrbSession alloc] initWithEngine:engine];
    Check([s configureProfiles:@{@"schema":@1,@"selected":profile[@"id"],@"autoSelect":@NO,@"profiles":@[profile]}],"fixed session fixture imports");
    Tick(s,engine,Frame()); [s queueAutomatic]; return s;
}
static NSMutableDictionary *MutableFixedSessionProfile(FixedTestEngine *engine) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:FixedSessionProfile(engine) options:0 error:NULL];
    return [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL];
}
static void AdvanceFixedToSolveOrStop(PHOrbSession *s, FixedTestEngine *e) {
    for (unsigned i=0;i<240 && ![s.snapshot[@"phase"] isEqual:@"solving"] && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i)
        Tick(s,e,Frame(),0.02,0,false);
}
static void FixedPrivateTargetSessionTests() {
    for (unsigned scenario=0;scenario<9;++scenario) {
        FixedTestEngine *e=[FixedTestEngine new]; NSMutableDictionary *profile=MutableFixedSessionProfile(e);
        NSMutableDictionary *stage=profile[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        NSDictionary *skill=stage[@"actions"][0];
        stage[@"actions"]=@[@{@"kind":@"target",@"target":@"enemy",@"monster":scenario==1 ? @10524 : @210524,
            @"optional":@NO,@"frequency":@"once"},skill];
        auto &fixed=e->live.fixedPlan;
        fixed.count=2; fixed.aliveMask=3;
        fixed.enemies[1]=fixed.enemies[0];
        fixed.enemies[0].catalogID=200038; fixed.enemies[1].catalogID=210524;
        e->live.automation.activeEnemies=2;
        if (scenario==2) fixed.enemies[0].catalogID=210524;
        if (scenario==3) { fixed.enemies[1].hp=0; fixed.aliveMask=1; }
        if (scenario==4) { fixed.enemies[1].flags=0x100u; fixed.partsMask=2; }
        if (scenario==5) fixed.enemies[1].identityKnown=false;
        if (scenario==6) fixed.enemies[1].hpKnown=false;
        if (scenario==7) e->ignoreTarget=YES;
        if (scenario==8) fixed.enemies[1].catalogID=110524;
        PHOrbSession *s=StartFixedTest(e,profile);
        for (unsigned i=0;i<240 && !e->targetCalls && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i)
            Tick(s,e,Frame(),0.02,0,false);
        const bool matched=scenario==0 || scenario==7;
        Check(e->targetCalls==(matched ? 1u : 0u) && e->skillOrder.empty(),
            "only one living ordinary enemy with the exact private ID can stage a target before any skill");
        if (matched) Check(e->lastTargetSlot==1,"the exact private target selects slot two beside an unrelated private enemy");
        AdvanceFixedToSolveOrStop(s,e);
        if (!scenario) {
            Check(e->targetCalls==1 && fixed.target==1 && e->skillOrder.size()==1 && e->skillOrder[0]==0 &&
                [s.snapshot[@"fixedPlanProgress"][@"action"] unsignedIntegerValue]==2,
                "native target acknowledgement advances exactly once before the following prescribed skill");
        } else {
            Check([s.snapshot[@"phase"] isEqual:@"stopped"] && e->skillOrder.empty() &&
                [s.snapshot[@"fixedPlanProgress"][@"action"] unsignedIntegerValue]==0 && e->targetCalls==(matched ? 1u : 0u),
                "public IDs, other private variants, duplicate, dead, part, unknown and unacknowledged targets never advance");
        }
        [s queueStop:@"test cleanup"];
    }
}
static void FixedSessionTests(void) {
    FixedPrivateTargetSessionTests();
    for (unsigned scenario=0;scenario<6;++scenario) {
        FixedTestEngine *e=[FixedTestEngine new]; NSDictionary *profile=FixedSessionProfile(e);
        e->unknownFixed=scenario==1; e->unknownReady=scenario==2; e->unavailableSkills[0]=scenario==3;
        e->failSkillAck=scenario==4;
        if (scenario==5) e->live.wave=1;
        PHOrbSession *s=StartFixedTest(e,profile);
        for (unsigned i=0;i<180 && ![s.snapshot[@"phase"] isEqual:@"solving"] && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i)
            Tick(s,e,Frame(),0.02,0,false);
        Check(e->skillOrder.size()==((scenario==0 || scenario==4) ? 1u : 0u),"only confirmed fixed evidence permits the prescribed skill");
        if (!scenario) Check([s.snapshot[@"fixedPlanProgress"][@"action"] unsignedIntegerValue]==1,"fixed skill receipt advances exactly one action");
        if (scenario==2) {
            NSDictionary *progress=s.snapshot[@"fixedPlanProgress"];
            Check([s.snapshot[@"phase"] isEqual:@"stopped"] && [progress[@"floor"] isEqual:@1] &&
                [progress[@"action"] isEqual:@0] && [progress[@"pending"][@"skill"] isEqual:@700] &&
                ![progress[@"consumed"] boolValue] && [NSJSONSerialization isValidJSONObject:progress],
                "a rejected first readiness probe retains the exact pending fixed action for exported diagnostics");
        }
        [s queueStop:@"test pause"]; [s queueAutomatic];
        for (unsigned i=0;i<30;++i) Tick(s,e,Frame(),0.02,0,false);
        if (!scenario || scenario==4) Check(e->skillOrder.size()==1,"stopped or unacknowledged fixed action cannot replay in this battle");
        [s queueStop:@"test cleanup"];
    }
    for (unsigned scenario=0;scenario<5;++scenario) {
        FixedTestEngine *e=[FixedTestEngine new]; NSMutableDictionary *profile=MutableFixedSessionProfile(e);
        NSMutableDictionary *plan=profile[@"fixedPlan"], *stage=plan[@"floors"][0][@"variants"][0][@"stages"][0];
        NSMutableDictionary *action=stage[@"actions"][0];
        if (scenario==0) {
            // An unready primary Atori must use the declared ready alternative.
            plan[@"members"][5]=plan[@"members"][0]; profile[@"team"][5]=@100;
            e->live.automation.members[5]=e->live.automation.members[0];
            action[@"alternatives"]=@[@6]; e->unavailableSkills[0]=YES;
        } else if (scenario==1) {
            plan[@"leaderSwap"]=@5;
            plan[@"members"][4][@"forms"]=@[@{@"number":@104,@"skills":@[@700]},@{@"number":@106,@"skills":@[@701]}];
            action[@"slot"]=@5; action[@"monster"]=@104; action[@"afterMonster"]=@106;
            e->transformAfterSkill[4]=106; e->swapOnTransform=YES;
        } else if (scenario==2 || scenario==3) {
            NSDictionary *target=@{@"kind":@"target",@"target":@"enemy",@"monster":@42,@"optional":@NO,@"frequency":@"once"};
            stage[@"actions"]=@[target,action]; e->ignoreTarget=scenario==3;
        } else {
            action[@"when"]=@"assist_void"; e->live.assistRecovery.valid=true; e->live.assistRecovery.turns=1;
            stage[@"board"][@"maxGestureMS"]=@4500;
        }
        PHOrbSession *s=StartFixedTest(e,profile); AdvanceFixedToSolveOrStop(s,e);
        const NSUInteger expectedSlot=scenario==0 ? 5 : scenario==1 ? 4 : 0;
        Check(e->skillOrder.size()==(scenario==3 ? 0u : 1u) &&
            (scenario==3 || e->skillOrder[0]==expectedSlot),"fixed alternatives, leader-swap receipt and target acknowledgement preserve action order");
        if (scenario==2 || scenario==3) Check(e->targetCalls==1,"target tap is never replayed to guess a missing acknowledgement");
        if (scenario==4) Check(e->assistReads && [s.snapshot[@"executionLimitMS"] unsignedIntValue]>0 &&
            [s.snapshot[@"executionLimitMS"] unsignedIntValue]<=4500,
            "first-stage assist predicate captures evidence and exposes the bounded gesture clock");
        [s queueStop:@"test cleanup"];
    }
}
