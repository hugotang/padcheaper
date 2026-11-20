// Fixed-plan state-machine fixtures. Written separately from the real solver
// contracts so native acknowledgement/identity behavior is observable.
#include "orb_coral_preflight_fixture.inc"
@interface FixedTestEngine : FakeOrbEngine {
@public
    BOOL unknownFixed, unknownReady, ignoreTarget, swapOnTransform;
    NSUInteger targetCalls, lastTargetSlot;
    NSUInteger bypassAfterSkillSlotPlusOne, clearUnmatchableAfterSkillSlotPlusOne;
    BOOL unknownUnmatchable;
    NSUInteger unmatchableReads;
}
@end
@implementation FixedTestEngine
- (void)nativeInputFrame {
    const bool swap=swapOnTransform && skillFrames==1 && activeSkill==4 && transformAfterSkill[4];
    const unsigned uses=live.automation.skillUses;
    [super nativeInputFrame];
    if (swap) std::swap(live.automation.members[0],live.automation.members[4]);
    if (live.automation.skillUses==uses+1) {
        if (bypassAfterSkillSlotPlusOne==activeSkill+1)
            live.automation.playerBypass=PHOrbEnemyDamageAbsorb|PHOrbEnemyAttributeAbsorb|PHOrbEnemyDamageVoid;
        if (clearUnmatchableAfterSkillSlotPlusOne==activeSkill+1) {
            live.unmatchableTurns=0; live.board.unmatchableColors=0;
        }
    }
}
- (BOOL)readFixedPlan:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (unknownFixed) { if (reason) *reason=@"fixture unknown fixed evidence"; return NO; }
    state->fixedPlan=live.fixedPlan; return state->fixedPlan.valid;
}
- (BOOL)readUnmatchableRecovery:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++unmatchableReads; state->unmatchableRecovery={};
    if (unknownUnmatchable || staged || !frame.ready || !frame.enabled || live.phase!=2 ||
        live.automation.busy || live.boardTransition || (!live.settled && !live.dragged) ||
        live.multiplayer.kind!=PHOrbMultiplayerNone || !PHInputIsQuiet(&live.input)) return NO;
    state->unmatchableRecovery=live.unmatchableRecovery;
    return state->unmatchableRecovery.valid;
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
static void CoralPreflightSessionTests() {
    for (unsigned automatic=0;automatic<2;++automatic) {
        FixedTestEngine *e=[FixedTestEngine new]; FixedSessionProfile(e);
        NSDictionary *old=CoralPreflightProfile(5), *current=CoralPreflightProfile(7);
        e->live.identity.dungeon=7801; e->live.identity.floor=1;
        for (unsigned i=0;i<6;++i) {
            NSDictionary *member=old[@"fixedPlan"][@"members"][i]; auto &native=e->live.automation.members[i];
            native.number=native.identityNumber=[member[@"body"] unsignedIntValue];
            PHBattleCatalogIDForNumber(native.number,&native.sourceCatalogIDs[0]);
            native.sourceCatalogIDs[1]=native.assistCatalogID=0; native.assistState=0;
            if (member[@"assist"]!=NSNull.null && [member[@"assist"] unsignedIntValue]) {
                PHBattleCatalogIDForNumber([member[@"assist"] unsignedIntValue],&native.sourceCatalogIDs[1]);
                native.assistCatalogID=native.sourceCatalogIDs[1]; native.assistState=172;
            }
        }
        e->chosenSource[1]=PHOrbSkillAssist; e->chosenSkill[1]=66752;
        PHOrbSession *s=[[PHOrbSession alloc] initWithEngine:e];
        NSDictionary *library=@{@"schema":@1,@"selected":old[@"id"],@"autoSelect":@(automatic),@"profiles":@[old]};
        Check([s configureProfiles:library],"an outdated Coral plan stays loadable for review and export");
        Tick(s,e,Frame()); [s queueAutomatic]; AdvanceFixedToSolveOrStop(s,e);
        NSDictionary *snapshot=s.snapshot, *progress=snapshot[@"fixedPlanProgress"], *stop=progress[@"lastStop"];
        Check([snapshot[@"phase"] isEqual:@"stopped"] && [stop[@"code"] isEqual:@"plan-update-required"] &&
            [stop[@"scope"] isEqual:@"before-first-action"] && [progress[@"profile"] isEqual:old[@"id"]] &&
            [stop[@"reason"] isEqual:snapshot[@"message"]] && ![progress[@"consumed"] boolValue] &&
            ![progress[@"preflight"][@"allowed"] boolValue] && ![snapshot[@"searchTrace"] count] &&
            e->skillOrder.empty() && !e->targetCalls && !e->stages && !e->releases,
            "explicit and automatically matched old Coral plans stop before any skill, target, solver or gesture");
        for (unsigned i=0;i<12;++i) Tick(s,e,Frame());
        Check([s.snapshot[@"fixedPlanProgress"][@"lastStop"] isEqual:stop],
            "idle reads preserve the startup refusal and its selected profile content");
        Check([s configureProfiles:@{@"schema":@1,@"selected":current[@"id"],@"autoSelect":@NO,@"profiles":@[old,current]}],
            "the user can select a corrected plan without altering the saved old record");
        [s queueAutomatic];
        for (unsigned i=0;i<180 && e->skillOrder.empty() && ![s.snapshot[@"phase"] isEqual:@"stopped"];++i)
            Tick(s,e,Frame(),0.02,0,false);
        Check(e->skillOrder.size()==1 && e->skillOrder[0]==1 &&
            [s.snapshot[@"fixedPlanProgress"][@"preflight"][@"allowed"] boolValue] &&
            [s.snapshot[@"fixedPlanProgress"][@"consumed"] boolValue],
            "correcting an untouched B1 plan permits only the original first prescribed skill");
        [s queueStop:@"preflight consumed fixture"];
        NSDictionary *consumedStop=s.snapshot[@"fixedPlanProgress"][@"lastStop"];
        [s configureProfiles:library]; [s queueAutomatic];
        for (unsigned i=0;i<30;++i) Tick(s,e,Frame());
        Check(e->skillOrder.size()==1 && !e->targetCalls && !e->stages &&
            [s.snapshot[@"message"] containsString:@"已有執行紀錄"] &&
            [s.snapshot[@"fixedPlanProgress"][@"lastStop"] isEqual:consumedStop],
            "switching back to an old plan never replaces a consumed-run refusal with a restartable preflight");
        [s queueStop:@"preflight cleanup"];
    }
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
static void FixedAbsorptionTargetSessionTests() {
    for (unsigned scenario=0;scenario<13;++scenario) {
        FixedTestEngine *e=[FixedTestEngine new]; NSMutableDictionary *p=MutableFixedSessionProfile(e);
        NSMutableDictionary *s=p[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        NSDictionary *target=@{@"kind":@"target",@"target":@"attribute_absorb",@"monster":@0,@"color":@2,
            @"optional":@NO,@"frequency":@"each"};
        s[@"requireEnemyState"]=@[@{@"monster":@42},@{@"monster":@43}];
        s[@"actions"]=@[target,s[@"actions"][0]];
        auto &f=e->live.fixedPlan;
        f.count=2; f.aliveMask=3; f.enemies[1]=f.enemies[0]; f.enemies[1].catalogID=43;
        for (unsigned i=0;i<2;++i) {
            f.enemies[i].attributeAbsorbKnown=true; f.enemies[i].attributeAbsorbTurns=10;
            f.enemies[i].attributeAbsorbColors=i ? 4 : 1;
        }
        e->live.automation.activeEnemies=2; e->live.automation.enemyEffects=PHOrbEnemyAttributeAbsorb;
        e->live.automation.playerBypass=PHOrbEnemyAttributeAbsorb;
        if (scenario==1) f.enemies[1].attributeAbsorbColors=1; // Known absence, not unknown.
        if (scenario==2) f.enemies[0].attributeAbsorbKnown=false;
        if (scenario==3) f.target=1; // Already selected, no toggle tap.
        if (scenario==4 || scenario==5) f.enemies[0].attributeAbsorbColors=4;
        if (scenario==4) f.target=1; // Both match: retain the selected one.
        if (scenario==6) { f.enemies[1].hp=0; f.enemies[1].attributeAbsorbKnown=false; f.aliveMask=1; e->live.automation.activeEnemies=1; }
        if (scenario==7) e->ignoreTarget=YES;
        if (scenario==9) f.enemies[1].catalogID=44; // Extra/different roster.
        if (scenario==12) { f.enemies[0].hp=0; f.aliveMask=2; e->live.automation.activeEnemies=1; }
        PHOrbSession *session=StartFixedTest(e,p);
        for (unsigned i=0;i<240 && !e->targetCalls && ![session.snapshot[@"phase"] isEqual:@"solving"] &&
            ![session.snapshot[@"phase"] isEqual:@"stopped"];++i) Tick(session,e,Frame(),0.02,0,false);
        if (scenario==8) f.enemies[1].attributeAbsorbColors=1; // Changed before acknowledgement.
        AdvanceFixedToSolveOrStop(session,e);
        const bool rejected=scenario==2 || scenario==7 || scenario==8 || scenario==9;
        const unsigned taps=scenario==0 || scenario==5 || scenario==7 || scenario==8 || scenario==10 || scenario==11 ? 1 : 0;
        Check(e->targetCalls==taps && e->skillOrder.size()==(rejected ? 0u : 1u),
            "fresh conditional target checks run before skills; unknown, changed, mismatched and missing acknowledgements never advance");
        if (taps) Check(e->lastTargetSlot==(scenario==5 ? 0u : 1u),
            "wood absorption is selected by current colour, even while bypassed, with deterministic equivalent-target handling");
        if (rejected) Check([session.snapshot[@"phase"] isEqual:@"stopped"],"failed target evidence stops the fixed plan");
        if (scenario==10 || scenario==11) {
            Check([session.snapshot[@"phase"] isEqual:@"solving"] && !e->stages,"target fixture reaches planning before input");
            if (scenario==10) f.enemies[1].attributeAbsorbColors=1;
            else f.target=0;
            Tick(session,e,Frame(),0.02,0,false);
            Check([session.snapshot[@"phase"] isEqual:@"stopped"] && !e->stages,
                "changed per-enemy absorption or target revokes the planned attack even if aggregate bypass still matches");
        }
        [session queueStop:@"absorption target test cleanup"];
    }
}
static void FixedSessionTests(void) {
    CoralPreflightSessionTests();
    FixedPrivateTargetSessionTests();
    FixedAbsorptionTargetSessionTests();
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
