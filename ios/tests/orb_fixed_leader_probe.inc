// Run only inside an authorized GUI build. These are value-only local 4P
// fixtures; the real planner and embedded data come from the loaded dylib.
// This does not exercise native memory discovery or multiplayer input.
static NSDictionary *PHProbeFourLeaderPlan(uint16_t number, uint32_t skill, BOOL readable) {
    __typeof__(&PHOrbPlanForLeaders) planForLeaders =
        (__typeof__(&PHOrbPlanForLeaders))dlsym(PHProbeDylib, "PHOrbPlanForLeaders");
    PHProbeCheck(planForLeaders != NULL, @"Fixed leader planner resolves from the loaded test dylib");
    if (!planForLeaders) return @{};
    PHOrbSnapshot state = {0};
    state.present = state.valid = true; state.leadersValid = readable; state.phase = 2;
    state.identity.entryMode = 11; state.identity.multiplayer = 1;
    PHOrbMultiplayerState *multi = &state.multiplayer;
    multi->kind = PHOrbMultiplayerFour; multi->known = multi->clockKnown = true;
    multi->localSeat = multi->viewTeam = 3; multi->teamCount = 4;
    multi->localStatus = 3; multi->networkFlags = 1; multi->connectionState = 6;
    multi->countdownReady = 1; multi->clockFlags = 4; multi->clockStart = 100; multi->remainingSeconds = 60;
    state.leaderNumbers[0] = number;
    PHOrbLeaderContext *leader = &state.leaderContext;
    leader->known = leader->extraHelper = true; leader->teamBank = 8000;
    leader->sourceCatalogIDs[0] = number >= 9900 ? number + 100 : number;
    leader->catalogIDs[0] = leader->parameterIDs[0] = 9000000u + leader->sourceCatalogIDs[0];
    leader->parameters[0] = 9000; leader->skillIDs[0] = skill; leader->activeFlags[0] = 1;
    leader->catalogIDs[1] = leader->parameterIDs[1] = 10000000;
    leader->sourceSlots[1] = 5; leader->sourceCatalogIDs[1] = (uint16_t)10000000u;
    leader->extraFlags[1] = 2; leader->extraRows = 12000; leader->parameters[1] = leader->extraRows + 8;
    leader->extraCount = 1; leader->extraSelection = leader->extraRecord = 73; leader->extraSkill = 36861;
    state.leaderRead.status = readable ? PHOrbLeaderReadReady : PHOrbLeaderReadParameter;
    state.leaderRead.pass = readable ? 3 : 1;
    return planForLeaders(&state);
}

static NSArray *PHProbeFixedFaganGoals(void) {
    return @[@{@"kind":@"colors",@"colors":@31,@"count":@3,@"length":@0},
             @{@"kind":@"exact",@"colors":@63,@"count":@1,@"length":@5}];
}
