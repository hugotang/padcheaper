static PHOrbSnapshot AssistBoard(unsigned light, unsigned dark, unsigned turns) {
    PHOrbSnapshot state = RecoverySizedBoard(6, 5, 0);
    for (unsigned i = 0; i < 30; ++i) state.board.types[i] = i < light ? 3 : i < light + dark ? 4 : 5;
    state.assistRecovery.valid = true; state.assistRecovery.turns = turns;
    state.assistRecovery.perColor[3] = 2; state.assistRecovery.perColor[4] = 2;
    state.assistRecovery.providers[3] = state.assistRecovery.providers[4] = 1;
    return state;
}
static void AssistPlanningTests(void) {
    NSDictionary *base = RecoveryBase(@[Goal(@"t",8,1,0), Goal(@"four",32,1,0)]);
    PHOrbSnapshot state = AssistBoard(10, 5, 2);
    NSDictionary *plan = PHOrbPlanWithAssistRecovery(base, NO, NULL, YES);
    Check([plan[@"available"] boolValue] && [plan[@"goals"] isEqual:base[@"goals"]],
        "disabled assist policy does not request native evidence or change a goal");
    plan = PHOrbPlanWithAssistRecovery(base, YES, NULL, NO);
    Check([plan[@"available"] boolValue] && ![plan[@"assistRecoveryActive"] boolValue] &&
        [plan[@"goals"] isEqual:base[@"goals"]], "manual and one-shot plans retain their captured goals");
    plan = PHOrbPlanWithAssistRecovery(base, YES, &state, YES);
    Check([plan[@"available"] boolValue] && [plan[@"assistRecoveryCandidates"] count] == 2 &&
        [plan[@"assistRecoveryBaseGoals"] isEqual:base[@"goals"]] &&
        [plan[@"goals"] containsObject:Goal(@"t",8,1,0)] && [plan[@"goals"] containsObject:Goal(@"four",32,1,0)],
        "light or dark L can recover the full duration while preserving T and heart-four requirements");
    for (NSDictionary *candidate in plan[@"assistRecoveryCandidates"])
        Check([candidate[@"predictedTurns"] unsignedIntValue] >= 2 && [candidate[@"groups"] unsignedIntValue] == 1,
            "every published candidate requests the full predicted recovery rather than a partial turn");
    state = AssistBoard(9, 5, 2); plan = PHOrbPlanWithAssistRecovery(base, YES, &state, YES);
    Check([plan[@"available"] boolValue] && [plan[@"assistRecoveryCandidates"] count] == 1 &&
        [plan[@"goals"] containsObject:Goal(@"l",16,1,0)],
        "nine light orbs cannot form both T and L, but an eligible dark L remains available");
    state.assistRecovery.perColor[4] = 0; plan = PHOrbPlanWithAssistRecovery(base, YES, &state, YES);
    Check(![plan[@"available"] boolValue] && [plan[@"message"] containsString:@"10 顆"],
        "insufficient joint T/L inventory stops without weakening the T");

    state = AssistBoard(10, 0, 4);
    plan = PHOrbPlanWithAssistRecovery(RecoveryBase(@[]), YES, &state, YES);
    Check([plan[@"available"] boolValue] && [plan[@"goals"] containsObject:Goal(@"l",8,2,0)] &&
        [plan[@"assistRecoveryCandidates"][0][@"predictedTurns"] unsignedIntValue] == 4,
        "two separate same-color L groups add their per-group recovery");
    state.assistRecovery.turns = 5;
    Check(![PHOrbPlanWithAssistRecovery(RecoveryBase(@[]), YES, &state, YES)[@"available"] boolValue],
        "four predicted turns do not authorize a five-turn void");
    state = AssistBoard(5, 5, 4);
    plan = PHOrbPlanWithAssistRecovery(RecoveryBase(@[]), YES, &state, YES);
    Check([plan[@"available"] boolValue] && [plan[@"goals"] containsObject:Goal(@"l",8,1,0)] &&
        [plan[@"goals"] containsObject:Goal(@"l",16,1,0)], "distinct colors can supply the required combined recovery");
    NSDictionary *existing = RecoveryBase(@[Goal(@"l",8,1,0), Goal(@"l",16,1,0)]);
    plan = PHOrbPlanWithAssistRecovery(existing, YES, &state, YES);
    Check([plan[@"assistRecoveryCandidates"] count] == 1 && [plan[@"goals"] isEqual:existing[@"goals"]] &&
        [plan[@"assistRecoveryCandidates"][0][@"addedGroups"] unsignedIntValue] == 0,
        "existing L groups are reused and identical merged candidates are not searched twice");
    state.assistRecovery.turns = 1;
    plan = PHOrbPlanWithAssistRecovery(existing, YES, &state, YES);
    Check([plan[@"assistRecoveryCandidates"] count] == 1 &&
        [plan[@"assistRecoveryCandidates"][0][@"predictedTurns"] unsignedIntValue] == 4,
        "mandatory existing groups remain in the prediction even when only one turn needs recovery");
    state = AssistBoard(10, 0, 2); state.assistRecovery.perColor[4] = 0;
    NSDictionary *six = RecoveryBase(@[Goal(@"combo",0,1,0), Goal(@"colors",63,1,0), Goal(@"match",8,1,3),
        Goal(@"exact",8,1,5), Goal(@"row",32,1,0), Goal(@"t",8,1,0)]);
    plan = PHOrbPlanWithAssistRecovery(six, YES, &state, YES);
    Check(![plan[@"available"] boolValue] && [plan[@"message"] containsString:@"6 項"] &&
        [plan[@"goals"] isEqual:six[@"goals"]], "goal overflow never drops a leader or saved requirement");

    state = AssistBoard(10, 0, 2); state.board.spinnerMask = UINT64_C(1) << 9; state.board.spinnerColors = 63;
    Check(![PHOrbPlanWithAssistRecovery(base, YES, &state, YES)[@"available"] boolValue],
        "a spinner cannot lend its currently observed tenth light orb to every phase");
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        state = AssistBoard(10, 0, 2);
        if (scenario == 0) state.assistRecovery.valid = false;
        if (scenario == 1) state.assistRecovery.dungeonFlags = 8;
        if (scenario == 2) state.board.minErase = 4;
        if (scenario == 3) state.board.minMatch = 6;
        if (scenario == 4) state.board.unmatchableColors = 8;
        if (scenario == 5) state.multiplayer.kind = PHOrbMultiplayerFour;
        if (scenario == 6) state.multiplayer.kind = PHOrbMultiplayerThreeSolo;
        Check(![PHOrbPlanWithAssistRecovery(base, YES, &state, YES)[@"available"] boolValue],
            "unknown evidence, prohibited assists, incompatible erasure and multiplayer remain blocked");
    }
    state = AssistBoard(10, 0, 0); plan = PHOrbPlanWithAssistRecovery(base, YES, &state, YES);
    Check([plan[@"available"] boolValue] && ![plan[@"assistRecoveryActive"] boolValue] &&
        [plan[@"goals"] isEqual:base[@"goals"]], "cleared duration removes only the temporary recovery goals");
    Check(![PHOrbPlanWithAssistRecovery(@{@"available":@NO,@"message":@"leader unavailable"}, YES, &state, YES)[@"available"] boolValue],
        "assist recovery cannot bypass an unavailable base leader plan");
    state = RecoverySizedBoard(7,6,0); state.assistRecovery.valid = true; state.assistRecovery.turns = 4;
    for (unsigned i = 0; i < 42; ++i) state.board.types[i] = i < 30 ? i / 10 : i < 40 ? 3 + (i - 30) / 5 : 5;
    for (unsigned color = 0; color < 5; ++color) {
        state.assistRecovery.perColor[color] = 1; state.assistRecovery.providers[color] = 1;
    }
    plan = PHOrbPlanWithAssistRecovery(RecoveryBase(@[]), YES, &state, YES);
    Check([plan[@"available"] boolValue] && [plan[@"assistRecoveryCandidates"] count] == 12,
        "many minimal allocations are bounded to twelve choices under one later search deadline");
    state.assistRecovery.unknownMembers = 2;
    Check([PHOrbPlanWithAssistRecovery(RecoveryBase(@[]), YES, &state, YES)[@"available"] boolValue],
        "an unknown unrelated member cannot erase sufficient recovery from already verified providers");
}
