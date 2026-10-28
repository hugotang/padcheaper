static void FinishSurvivalAttempt(PHOrbSession *session, FakeOrbEngine *engine, unsigned frames = 150) {
    for (unsigned i = 0; i < frames && [[session snapshot][@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
}
static void SurvivalSessionTests() {
    NSMutableDictionary *rescue = [PHOrbDefaultSkillRule() mutableCopy];
    rescue[@"hpAtMost"] = @40; rescue[@"required"] = @YES;
    FakeOrbEngine *engine = [FakeOrbEngine new]; engine->live.automation.hp = 400; engine->healWithSkill = YES;
    PHOrbSession *session = AutomaticSession(engine,@[rescue]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 &&
        engine->live.automation.hp == 1000 && [[session snapshot][@"confirmedSkillRules"] unsignedIntegerValue] == 1,
        "required low-HP rescue completes and is freshly observed before ordinary dragging");
    [session queueStop:@"end healed fixture"];
    engine = [FakeOrbEngine new]; session = AutomaticSession(engine,@[rescue]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty() &&
        [[[session snapshot][@"ruleDecisions"] componentsJoinedByString:@" "] containsString:@"高於"],
        "healthy turns do not consume the rescue skill and explain the skipped condition");
    [session queueStop:@"end healthy fixture"];

    for (unsigned unavailable = 0; unavailable < 5; ++unavailable) {
        engine = [FakeOrbEngine new]; engine->live.automation.hp = 100;
        NSMutableDictionary *rule = [rescue mutableCopy];
        if (unavailable == 0) engine->unavailableSkills[0] = YES;
        if (unavailable == 1) engine->live.automation.members[0].bound = true;
        if (unavailable == 2) engine->live.automation.members[0].overridden = true;
        if (unavailable == 3) rule[@"reserveUntil"] = @5;
        if (unavailable == 4) { engine->live.automation.window = false; engine->live.automation.playerEffects = PHOrbPlayerSkillSeal; }
        session = AutomaticSession(engine,@[rule]); FinishSurvivalAttempt(session,engine);
        Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && engine->skillOrder.empty() &&
            [[session snapshot][@"message"] containsString:@"必要規則"],
            "required unavailable, bound, manual-selected, reserved or sealed rescue cannot silently fall through to dragging");
    }
    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100; engine->unavailableSkills[0] = YES;
    NSMutableDictionary *optional = [rescue mutableCopy]; optional[@"required"] = @NO;
    session = AutomaticSession(engine,@[optional]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty(), "legacy optional unavailable skills still allow an otherwise eligible route");
    [session queueStop:@"end optional fixture"];
    for (unsigned unavailable = 0; unavailable < 3; ++unavailable) for (BOOL required : {NO, YES}) {
        engine = [FakeOrbEngine new]; engine->live.automation.enemiesKnown = false;
        NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy];
        rule[@"enemy"] = @"damage_absorb"; rule[@"required"] = @(required);
        if (unavailable == 0) engine->live.automation.members[0].bound = true;
        if (unavailable == 1) rule[@"reserveUntil"] = @5;
        if (unavailable == 2) engine->live.automation.members[0].active = false;
        session = AutomaticSession(engine,@[rule]);
        if (required) {
            FinishSurvivalAttempt(session,engine);
            Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && engine->skillOrder.empty() &&
                [[session snapshot][@"message"] containsString:@"敵人效果無法確認"],
                "unavailable required rules still stop when their triggering condition cannot be established");
        } else {
            Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty() &&
                [[[session snapshot][@"ruleDecisions"] componentsJoinedByString:@" "] containsString:@"略過"],
                "bound, reserved or absent optional actors retain legacy skipping even with unknown enemy conditions");
            [session queueStop:@"end optional unknown-condition fixture"];
        }
    }

    NSMutableDictionary *haste = [PHOrbDefaultSkillRule() mutableCopy]; haste[@"slot"] = @2;
    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100; engine->unavailableSkills[0] = YES; engine->hasteFirst = YES;
    session = AutomaticSession(engine,@[rescue,haste]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 2 &&
        engine->skillOrder[0] == 1 && engine->skillOrder[1] == 0,
        "another skill may make a required skill ready before the final required-rule check");
    [session queueStop:@"end haste rescue fixture"];
    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100; engine->unavailableSkills[0] = YES; engine->healWithSkill = YES;
    session = AutomaticSession(engine,@[rescue,haste]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && engine->skillOrder[0] == 1,
        "another heal can remove the low-HP condition without consuming an unavailable required skill");
    [session queueStop:@"end alternative healing fixture"];

    NSMutableDictionary *a = [rescue mutableCopy], *b = [rescue mutableCopy];
    a[@"group"] = @1; b[@"group"] = @1; b[@"slot"] = @2;
    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100; engine->unavailableSkills[0] = YES;
    session = AutomaticSession(engine,@[a,b]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && engine->skillOrder[0] == 1,
        "one acknowledged alternative satisfies a required group without spending both members");
    [session queueStop:@"end required group fixture"];
    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100;
    engine->unavailableSkills[0] = engine->unavailableSkills[1] = YES;
    session = AutomaticSession(engine,@[a,b]); FinishSurvivalAttempt(session,engine);
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.empty() && !engine->stages,
        "an exhausted required group cannot authorize a route just because every alternative was visited");

    for (unsigned unknown = 0; unknown < 2; ++unknown) {
        engine = [FakeOrbEngine new]; engine->live.automation.hp = 100;
        NSMutableDictionary *rule = [rescue mutableCopy];
        if (!unknown) engine->live.automation.hpKnown = false;
        else { rule[@"status"] = @"awoken_bind"; engine->live.automation.statusKnown = false; }
        session = AutomaticSession(engine,@[rule]); FinishSurvivalAttempt(session,engine);
        Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.empty() && !engine->stages,
            "unknown HP or requested player effects stop instead of pretending the rescue was unnecessary");
    }
    NSMutableDictionary *cleanse = [PHOrbDefaultSkillRule() mutableCopy]; cleanse[@"status"] = @"awoken_bind"; cleanse[@"required"] = @YES;
    engine = [FakeOrbEngine new]; engine->live.automation.playerEffects = PHOrbPlayerAwokenBind; engine->clearPlayerStatusWithSkill = YES;
    session = AutomaticSession(engine,@[cleanse]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && !engine->live.automation.playerEffects,
        "temporary awakening bind can request a confirmed cleanse followed by a new snapshot");
    [session queueStop:@"end temporary bind fixture"];
    cleanse[@"status"] = @"member_bind";
    engine = [FakeOrbEngine new]; engine->live.automation.playerEffects = PHOrbPlayerMemberBind;
    engine->live.automation.members[2].bound = true; engine->clearPlayerStatusWithSkill = YES;
    session = AutomaticSession(engine,@[cleanse]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1 && !engine->live.automation.members[2].bound,
        "a bound teammate can trigger the unbound caster's rescue rule");
    [session queueStop:@"end teammate bind fixture"]; cleanse[@"status"] = @"awoken_bind";
    engine = [FakeOrbEngine new]; engine->live.automation.awakeningRestricted = true;
    session = AutomaticSession(engine,@[cleanse]);
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty(), "fixed dungeon restriction alone does not waste a cleanse skill");
    [session queueStop:@"end fixed restriction fixture"];

    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100; engine->rejectStartedSkill = YES;
    session = AutomaticSession(engine,@[rescue]); FinishSurvivalAttempt(session,engine);
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    engine->rejectStartedSkill = NO; [session queueAutomatic]; FinishSurvivalAttempt(session,engine);
    Check(engine->skillOrder.size() == 1 && !engine->stages && ![[session snapshot][@"automatic"] boolValue] &&
        [[session snapshot][@"message"] containsString:@"未確認完成"],
        "uncertain native activation remains spent but cannot satisfy a required rule after stop and restart");

    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100;
    session = AutomaticSession(engine,@[rescue]);
    Check(UntilPhase(session,engine,@"skillWaiting"), "required stop-before-ack fixture starts");
    [session queueStop:@"stop before completion acknowledgement"];
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    [session queueAutomatic]; FinishSurvivalAttempt(session,engine);
    Check(engine->skillOrder.size() == 1 && !engine->stages && ![[session snapshot][@"automatic"] boolValue],
        "a manual stop before confirmation cannot turn an attempted cast into confirmed required success");

    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100;
    session = AutomaticSession(engine,@[rescue]);
    Check(UntilPhase(session,engine,@"skillWaiting") && UntilPhase(session,engine,@"autoWaiting"),
        "confirmed rescue fixture reaches the next stable-board wait");
    [session queueStop:@"pause after confirmation"];
    for (unsigned i = 0; i < 30; ++i) Tick(session,engine,Frame());
    [session queueAutomatic];
    Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == 1,
        "a genuinely confirmed required skill survives a same-turn pause without being repeated");
    [session queueStop:@"end confirmed pause fixture"];
    NSMutableDictionary *once = [rescue mutableCopy]; once[@"once"] = @YES;
    engine = [FakeOrbEngine new]; engine->live.automation.hp = 100; session = AutomaticSession(engine,@[once]);
    Check(UntilPhase(session,engine,@"turnWaiting"), "once-required skill completes the first orb turn");
    engine->live.phase = 2; engine->live.moves = 0; FinishSurvivalAttempt(session,engine);
    Check(![[session snapshot][@"automatic"] boolValue] && engine->skillOrder.size() == 1 && engine->releases == 1 &&
        [[session snapshot][@"message"] containsString:@"本場一次"],
        "prior-turn confirmation cannot satisfy a once-only rescue that is needed again on a later turn");

    engine = [FakeOrbEngine new]; session = AutomaticSession(engine,@[rescue]);
    Check(UntilPhase(session,engine,@"solving"), "HP change fixture reaches the solver without needing rescue");
    engine->live.automation.hp = 100; Tick(session,engine,Frame());
    Check(![[session snapshot][@"automatic"] boolValue] && !engine->stages && engine->skillOrder.empty() &&
        [[session snapshot][@"message"] containsString:@"HP"], "a later HP drop cancels a route chosen before rescue became necessary");
    for (unsigned during = 0; during < 2; ++during) {
        engine = [FakeOrbEngine new]; engine->dropHPOnStart = during == 0; engine->dropHPOnMove = during == 1;
        session = AutomaticSession(engine,@[rescue]);
        for (unsigned i = 0; i < 2000 && !engine->releases; ++i) Tick(session,engine,Frame());
        Check(![[session snapshot][@"automatic"] boolValue] && engine->releases == 1 && !engine->live.dragged &&
            engine->live.moves == during, "HP changing in the start or swap frame releases the owned orb and schedules no additional swap");
    }
}
