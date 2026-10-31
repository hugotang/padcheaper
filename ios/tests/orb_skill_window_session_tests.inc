// Source cases only until the next explicitly authorized build/test batch.
static void SkillWindowSessionTests() {
    NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy];
    rule[@"slot"] = @2; rule[@"once"] = @YES; rule[@"group"] = @1;
    for (unsigned point = 0; point < 3; ++point) {
        FakeOrbEngine *engine = [FakeOrbEngine new];
        PHOrbSession *session = AutomaticSession(engine, @[rule]);
        if (point != 0) Check(UntilPhase(session, engine, @"skillArming"), "window closes after a stable rule was armed");
        if (point == 2) engine->closeWindowInUse = YES;
        else {
            engine->live.automation.window = false;
            engine->live.automation.windowBlocks = PHOrbSkillWindowInput;
        }
        for (unsigned i = 0; i < 40; ++i) Tick(session, engine, Frame());
        Check([session.snapshot[@"automatic"] boolValue] && !engine->skillAttempts && !engine->stages &&
            ![session.snapshot[@"usedSkillGroups"] unsignedIntegerValue],
            "temporary closure before scanning, before dispatch or inside dispatch neither casts, drags nor consumes the group");
        engine->live.automation.window = true; engine->live.automation.windowBlocks = 0;
        Check(UntilPhase(session, engine, @"skillWaiting") && engine->skillAttempts == 1 &&
            engine->skillOrder.size() == 1 && engine->skillOrder[0] == 1,
            "a reopened window allows the same ready once/group rule in the same orb turn");
        NSDictionary *detail = [session.snapshot[@"ruleDecisionDetails"] firstObject];
        Check([detail[@"skillWindow"] boolValue] && [detail[@"skillWindowBlocksKnown"] boolValue] &&
            ![detail[@"skillWindowBlocks"] unsignedIntValue] && [detail[@"nativeAttempt"] boolValue],
            "a successful retry captures its own reopened-window context");
        [session queueStop:@"end window closure fixture"];
    }
    FakeOrbEngine *engine = [FakeOrbEngine new];
    engine->live.automation.window = false; engine->live.automation.windowBlocks = PHOrbSkillWindowInput;
    PHOrbSession *session = AutomaticSession(engine, @[rule]);
    for (unsigned i = 0; i < 190 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session, engine, Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && [session.snapshot[@"message"] containsString:@"3 秒"] &&
        !engine->skillAttempts && !engine->stages, "an unready window has a bounded timeout with zero automatic input");
    engine = [FakeOrbEngine new]; engine->live.automation.window = false;
    engine->live.automation.windowBlocks = PHOrbSkillWindowInput; engine->live.automation.busy = true;
    session = AutomaticSession(engine, @[rule]);
    for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
    Check([session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->skillAttempts,
        "busy temporary input gate is also deferred without any automatic action");
    engine->live.automation.window = true; engine->live.automation.busy = false; engine->live.automation.windowBlocks = 0;
    Check(UntilPhase(session,engine,@"skillWaiting") && engine->skillAttempts == 1,
        "a busy input gate can settle and reopen before the bounded timeout");
    [session queueStop:@"end busy input gate fixture"];

    for (unsigned cancellation = 0; cancellation < 3; ++cancellation) {
        engine = [FakeOrbEngine new]; engine->live.automation.window = false;
        engine->live.automation.windowBlocks = PHOrbSkillWindowInput;
        session = AutomaticSession(engine, @[rule]);
        for (unsigned i = 0; i < 35; ++i) Tick(session, engine, Frame());
        if (cancellation == 0) engine->live.input.rawCount = 1;
        if (cancellation == 1) engine->live.identity.multiplayer = 1;
        if (cancellation == 2) engine->live.present = false;
        Tick(session, engine, Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->skillAttempts && !engine->stages,
            "manual input, multiplayer and scene exit cancel a deferred skill without input");
    }
    for (bool required : {false, true}) {
        engine = [FakeOrbEngine new]; engine->live.automation.window = false;
        engine->live.automation.windowBlocks = PHOrbSkillWindowSeal;
        engine->live.automation.playerEffects = PHOrbPlayerSkillSeal;
        NSMutableDictionary *sealed = [PHOrbDefaultSkillRule() mutableCopy]; sealed[@"required"] = @(required);
        session = AutomaticSession(engine, @[sealed]);
        if (required) {
            for (unsigned i = 0; i < 100 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session, engine, Frame());
            Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages &&
                [session.snapshot[@"message"] containsString:@"必要規則"], "known skill seal never bypasses a necessary rule");
        } else Check(UntilPhase(session, engine, @"turnWaiting"), "optional sealed skill retains the existing orb-turn behavior");
        Check(!engine->skillAttempts, "known seal never invokes native skill availability");
        [session queueStop:@"end known seal fixture"];
    }
    engine = [FakeOrbEngine new]; session = AutomaticSession(engine, @[rule]);
    Check(UntilPhase(session, engine, @"skillArming"), "window race with changed HP is armed");
    engine->live.automation.window = false; engine->live.automation.windowBlocks = PHOrbSkillWindowInput;
    engine->live.automation.hp = 500; Tick(session, engine, Frame());
    Check(![session.snapshot[@"automatic"] boolValue] && !engine->skillAttempts && !engine->stages,
        "window deferral does not normalize away a changed player state");
    for (bool persistent : {false,true}) {
        engine = [FakeOrbEngine new]; NSDictionary *profile = PlanSessionProfile();
        engine->live.automation.window = false;
        engine->live.automation.windowBlocks = persistent ? PHOrbSkillWindowSeal : PHOrbSkillWindowInput;
        session = PlanSession(engine,profile);
        for (unsigned i = 0; i < 40 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
        Check(!engine->skillAttempts && !engine->stages && ![session.snapshot[@"opening"][@"completed"] unsignedIntegerValue] &&
            ![session.snapshot[@"opening"][@"inFlight"] boolValue], "closed opening window cannot consume, skip or guess the first step");
        if (persistent) Check(![session.snapshot[@"automatic"] boolValue], "sealed opening stops without repeatedly arming the first skill");
        else {
            engine->live.automation.window = true; engine->live.automation.windowBlocks = 0;
            Check(UntilPhase(session,engine,@"openingWaiting") && engine->skillOrder.size() == 1,
                "reopened opening window starts the same first step once");
            [session queueStop:@"end opening window fixture"];
        }
    }
    for (bool busy : {false,true}) {
        engine = [FakeOrbEngine new];
        NSMutableDictionary *future = [PHOrbDefaultSkillRule() mutableCopy]; future[@"floor"] = @2;
        session = AutomaticSession(engine,@[future]);
        Check(UntilSolveStarts(session,engine), "a nontriggered rule allows search before window changes");
        engine->live.automation.window = false; engine->live.automation.windowBlocks = PHOrbSkillWindowInput;
        engine->live.automation.busy = busy;
        Tick(session,engine,Frame());
        Check([session.snapshot[@"automatic"] boolValue] && [session.snapshot[@"phase"] isEqual:@"autoWaiting"] &&
            !engine->skillAttempts && !engine->stages, "window closure cancels the pre-touch solver and returns to rules without publishing an old route");
        engine->live.automation.window = true; engine->live.automation.windowBlocks = 0;
        engine->live.automation.busy = false;
        Check(UntilPhase(session,engine,@"arming"), "reopened window can publish a fresh automatic route");
        engine->live.automation.window = false; engine->live.automation.windowBlocks = PHOrbSkillWindowInput;
        engine->live.automation.busy = busy;
        Tick(session,engine,Frame());
        Check([session.snapshot[@"automatic"] boolValue] && [session.snapshot[@"phase"] isEqual:@"autoWaiting"] && !engine->stages,
            "window closure after search still waits for rule reevaluation before the first touch");
        [session queueStop:@"end pre-touch window fixture"];
    }
    engine = [FakeOrbEngine new]; engine->live.leadersValid = true;
    engine->live.leaderNumbers[0] = 14038; engine->live.leaderNumbers[1] = 14065;
    session = AutomaticSession(engine,@[rule]);
    Check(UntilPhase(session,engine,@"skillArming") && [session.snapshot[@"decisionContextCurrent"] boolValue],
        "readable original leaders identify the captured skill decision");
    [session queueStop:@"end captured leader fixture"];
    engine->live.leaderNumbers[0] = engine->live.leaderNumbers[1] = 12832;
    for (unsigned i = 0; i < 7; ++i) Tick(session,engine,Frame());
    Check(![session.snapshot[@"decisionContextCurrent"] boolValue] &&
        [session.snapshot[@"decisionLeaderNumbers"] isEqual:@[@14038,@14065]] && [session.snapshot[@"ruleDecisions"] count],
        "new visible leaders label and retain the old team's decision instead of attributing it to the new team");
    // Native refusal and haste retry are covered by the existing selection and automation cases.
}
