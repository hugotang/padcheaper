static void EnemyComboPlanningTests(void) {
    NSDictionary *base = RecoveryBase(@[Goal(@"t",8,1,0), Goal(@"four",32,1,0)]);
    PHOrbSnapshot state = AssistBoard(10, 5, 2);
    state.automation.enemiesKnown = true;
    state.enemyCombo = (PHOrbEnemyComboState){0}; state.enemyCombo.valid = true;
    state.enemyCombo.enemyCount = 2; state.enemyCombo.aliveMask = 3;
    state.enemyCombo.hp[0] = state.enemyCombo.hp[1] = 1000;
    state.enemyCombo.turns[0] = state.enemyCombo.turns[1] = 3;
    state.enemyCombo.thresholds[0] = 4; state.enemyCombo.thresholds[1] = 7;
    NSDictionary *plan = PHOrbPlanWithEnemyCombo(base, &state, YES);
    Check([plan[@"available"] boolValue] && [plan[@"enemyComboRequired"] isEqual:@8] && [plan[@"enemyComboFirstWave"] boolValue] &&
        [plan[@"goals"] containsObject:Goal(@"combo",0,8,0)] &&
        [plan[@"goals"] containsObject:Goal(@"t",8,1,0)] && [plan[@"goals"] containsObject:Goal(@"four",32,1,0)] &&
        [base[@"goals"] count] == 2, "enemy counter adds max threshold plus one without mutating base shape or heart goals");
    NSDictionary *assist = PHOrbPlanWithAssistRecovery(plan, YES, &state, YES);
    Check([assist[@"available"] boolValue], "enemy Combo counter combines with assist-L planning");
    for (NSDictionary *candidate in assist[@"assistRecoveryCandidates"])
        Check([candidate[@"goals"] containsObject:Goal(@"combo",0,8,0)], "every L allocation retains the enemy Combo requirement");
    Check([assist[@"enemyComboFirstWave"] boolValue], "assist allocation preserves the internal first-removal policy");
    state.enemyCombo.adjustmentTurns = 1; state.enemyCombo.adjustment = -1;
    state.enemyCombo.subtractTurns = 1; state.enemyCombo.subtract = 2;
    plan = PHOrbPlanWithEnemyCombo(base,&state,YES);
    Check([plan[@"available"] boolValue] && [plan[@"goals"] containsObject:Goal(@"combo",0,11,0)],
        "planning reserves three Combo debits on top of the strongest enemy threshold");
    state.enemyCombo.adjustmentTurns = state.enemyCombo.subtractTurns = 0;
    plan = PHOrbPlanWithEnemyCombo(RecoveryBase(@[Goal(@"combo",0,10,0)]), &state, YES);
    Check([plan[@"goals"] isEqual:@[Goal(@"combo",0,10,0)]], "a stronger saved Combo goal is preserved");
    state.enemyCombo.bypassTurns = 0x401;
    plan = PHOrbPlanWithEnemyCombo(base, &state, YES);
    Check([plan[@"available"] boolValue] && ![plan[@"enemyComboActive"] boolValue] && ![plan[@"enemyComboFirstWave"] boolValue] && [plan[@"goals"] isEqual:base[@"goals"]],
        "currently active Combo bypass removes only the temporary enemy requirement");
    state.enemyCombo.bypassDelay = 1;
    plan = PHOrbPlanWithEnemyCombo(base, &state, YES);
    Check([plan[@"enemyComboRequired"] isEqual:@8], "a queued bypass grants no present Combo credit");
    state.enemyCombo.thresholds[1] = 14;
    plan = PHOrbPlanWithEnemyCombo(base, &state, YES);
    Check(![plan[@"available"] boolValue] && [plan[@"enemyCounterBlocker"] isEqual:@"combo-limit"],
        "unverifiable Combo counts stop without skyfall or leader bonus assumptions");
    state.enemyCombo.thresholds[1] = 7;
    NSMutableArray *full = [NSMutableArray array];
    for (unsigned color = 0; color < 6; ++color) [full addObject:Goal(@"match",1u << color,1,3)];
    plan = PHOrbPlanWithEnemyCombo(RecoveryBase(full), &state, YES);
    Check(![plan[@"available"] boolValue] && [plan[@"enemyCounterBlocker"] isEqual:@"goal-limit"] &&
        [plan[@"goals"] isEqual:full], "goal overflow does not delete an existing requirement");
    state.enemyCombo.upperTurns[0] = 1;
    plan = PHOrbPlanWithEnemyCombo(base,&state,YES);
    Check(![plan[@"available"] boolValue], "upper-Combo absorption cannot be solved by raising a minimum goal");
    state.enemyCombo.upperTurns[0] = 0;
    state.enemyCombo.valid = false;
    plan = PHOrbPlanWithEnemyCombo(base, &state, YES);
    Check(![plan[@"available"] boolValue], "unknown fresh enemy evidence blocks continuous planning");
    plan = PHOrbPlanWithEnemyCombo(base, NULL, NO);
    Check([plan[@"available"] boolValue] && [plan[@"goals"] isEqual:base[@"goals"]],
        "manual previews and one-shot multiplayer do not enable continuous countermeasures");
}
