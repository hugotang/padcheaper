static void OrbEffectRulesTests(void) {
    PHOrbSnapshot state = {0};
    state.valid = state.ordinary = state.settled = true; state.phase = 2;
    state.board.width = 6; state.board.height = 5;
    state.automation.valid = state.automation.boardKnown = true;
    state.automation.members[0].active = true;
    NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy];
    NSArray *names = @[@"jammer", @"poison", @"mortal_poison", @"bomb", @"cloud", @"tape", @"blind", @"super_blind",
        @"nail", @"thorn", @"fixed_start", @"combo_orb", @"match_rule"];
    const uint32_t flags[] = {PHOrbObstacleJammer, PHOrbObstaclePoison, PHOrbObstacleMortalPoison, PHOrbObstacleBomb,
        PHOrbObstacleCloud, PHOrbObstacleTape, PHOrbObstacleBlind, PHOrbObstacleSuperBlind, PHOrbObstacleNail,
        PHOrbObstacleThorn, PHOrbObstacleFixedStart, PHOrbObstacleCombo, PHOrbObstacleMatchRule};
    for (NSUInteger i = 0; i < names.count; ++i) {
        rule[@"obstacle"] = names[i]; state.obstacles = flags[i];
        Check(PHOrbSkillRulesValid(@[rule]) && PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleYes &&
            [PHOrbObstacleDescription(&state) containsString:PHOrbObstacleConditionTitle(names[i])],
            "each new condition retains its own full-width bit, title and trigger");
        state.obstacles = 0;
        Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleNo, "cleared effects do not trigger stale remedy rules");
        state.automation.boardKnown = false;
        Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleUnknown, "missing effect data never means absent");
        state.automation.boardKnown = true;
        NSData *data = [NSJSONSerialization dataWithJSONObject:@[rule] options:0 error:NULL];
        Check(PHOrbSkillRulesValid([NSJSONSerialization JSONObjectWithData:data options:0 error:NULL]),
            "new obstacle condition names survive settings persistence");
    }
    state.board.types[0] = PHOrbJammer; state.board.types[1] = PHOrbPoison;
    state.board.types[2] = PHOrbMortalPoison; state.board.types[3] = PHOrbBomb;
    NSString *description = PHOrbObstacleDescription(&state);
    Check([description containsString:@"邪魔"] && [description containsString:@"猛毒"] &&
        [description containsString:@"爆彈"] && ![description containsString:@"基本六色以外"],
        "known hazard types are described explicitly without a redundant unknown-orb label");
    state.board.types[4] = 10;
    Check([PHOrbObstacleDescription(&state) containsString:@"基本六色以外"], "mixed known hazards never hide an unknown type");

    rule = [PHOrbDefaultSkillRule() mutableCopy]; rule[@"color"] = @5;
    for (unsigned i = 0; i < 30; ++i) state.board.types[i] = 1;
    for (unsigned i = 24; i < 28; ++i) state.board.types[i] = 5;
    state.board.spinnerMask = UINT64_C(1) << 29; state.board.spinnerColors = 33; state.board.types[29] = 5;
    rule[@"below"] = @6;
    Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleYes, "shortage true in every spinner phase can trigger");
    rule[@"below"] = @5;
    Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleUnknown, "a threshold crossing is unknown even when this sampled color is sufficient");
    rule[@"below"] = @4;
    Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleNo, "a guaranteed sufficient minimum skips the skill");
    state.board.spinnerMask |= UINT64_C(1) << 28; state.board.types[28] = 0;
    rule[@"below"] = @6;
    Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleYes, "opposite spinner phases preserve the exact shared inventory");
    rule[@"below"] = @5;
    Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleNo, "shared-phase counting does not invent independent color losses");
    state.board.flags[29] = PHOrbFlagLocked; rule[@"below"] = @6;
    Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleUnknown, "locked spinners contribute a fixed color while unlocked cells still rotate");
    state.board.spinnerColors = 0x400;
    Check(PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleUnknown, "unsupported cycles cannot supply a guessed inventory");
    state.board.spinnerMask = state.board.spinnerColors = 0;
    rule = [PHOrbDefaultSkillRule() mutableCopy]; rule[@"obstacle"] = @"unmatchable";
    state.unmatchableKnown = true; state.board.unmatchableColors = 9;
    for (unsigned mask = 0; mask < 64; ++mask) {
        rule[@"unmatchableColors"] = @(mask);
        Check(PHOrbSkillRulesValid(@[rule]) && PHOrbTriggerSkillRule(rule,&state,1,NULL) ==
              (!mask || (mask & 9) ? PHOrbRuleYes : PHOrbRuleNo),
              "selected unmatchable colors use any intersection, including the reported fire plus light case");
        NSData *data = [NSJSONSerialization dataWithJSONObject:@[rule] options:0 error:NULL];
        NSArray *saved = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        Check(PHOrbSkillRulesValid(saved) && [saved[0][@"unmatchableColors"] isEqual:@(mask)],
              "every selected color combination survives JSON persistence");
    }
    for (id mask in @[@YES,@-1,@64,@1.5,@"8"]) {
        rule[@"unmatchableColors"] = mask;
        Check(!PHOrbSkillRulesValid(@[rule]),"unmatchable selections reject booleans, out-of-range values and coercion");
    }
    rule[@"unmatchableColors"] = @8; state.unmatchableKnown = false;
    Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleUnknown,
          "missing unmatchable data never authorizes a color-specific skill");
    state.unmatchableKnown = true; state.ordinary = false; state.spinnerTurns = 1;
    state.spinnerReadStatus = PHOrbSpinnerCountMismatch;
    Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleYes &&
          [PHOrbObstacleDescription(&state) containsString:@"數量與保存位置不符"],
          "known unmatchable color still permits a remedy when independent spinner validation fails");
    rule[@"status"] = @"awoken_bind"; state.automation.statusKnown = true; state.automation.playerEffects = 0;
    Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleNo,"status plus unmatchable in one rule remains AND");
    state.automation.playerEffects = PHOrbPlayerAwokenBind;
    Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleYes,"both status and color requirements can trigger together");
    rule[@"obstacle"] = @"thorn";
    Check(!PHOrbSkillRulesValid(@[rule]),"color masks cannot accidentally attach to an unrelated obstacle condition");
}
