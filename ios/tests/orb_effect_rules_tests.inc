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
}
