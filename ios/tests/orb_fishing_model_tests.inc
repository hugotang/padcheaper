// C-compatible model cases, included by the private C++ suite. No solver is
// invoked: these exercise full-list replay, live one-step prediction and the
// narrowly scoped selected-spinner comparison directly.
static PHOrbBoard FishingModelBoard(void) {
    PHOrbBoard board; memset(&board,0,sizeof(board)); board.width = 6; board.height = 5;
    for (unsigned i = 0; i < 30; ++i) {
        board.types[i] = (uint8_t)(i % 6); board.tokens[i] = 100 + i;
        board.x[i] = (int16_t)(i % 6); board.y[i] = (int16_t)(i / 6);
    }
    board.spinnerMask = UINT64_C(1) << 7; board.spinnerColors = 63;
    board.types[6] = 3; board.types[7] = 1; board.types[8] = 2; board.types[13] = 4;
    board.types[20] = PHOrbMortalPoison;
    board.enhancement[7] = 1031127695u; board.enhancement[8] = 3204448256u;
    return board;
}

static PHOrbRoute FishingModelRoute(void) {
    PHOrbRoute route; memset(&route,0,sizeof(route)); route.fishing = true; route.fishCell = 7; route.fishColor = 5;
    const uint8_t cells[] = {6,7,8,7,13};
    route.count = (uint8_t)sizeof(cells); memcpy(route.cells,cells,sizeof(cells));
    route.fishingExchangeCount = 2;
    route.fishingExchanges[0].entry = 1; route.fishingExchanges[0].exit = 2;
    route.fishingExchanges[0].take = 5; route.fishingExchanges[0].leave = 2;
    route.fishingExchanges[1].entry = 3; route.fishingExchanges[1].exit = 4;
    route.fishingExchanges[1].take = 3; route.fishingExchanges[1].leave = 4;
    return route;
}

static void FishingModelTests(void) {
    PHOrbBoard board = FishingModelBoard(), replay = board;
    PHOrbRoute route = FishingModelRoute();
    Check(PHOrbReplay(&board,&route,4,&replay) && replay.types[6] == 5 && replay.types[8] == 3 &&
        replay.types[7] == 4 && replay.types[13] == 3 && replay.types[20] == PHOrbMortalPoison,
        "explicit exchange list supports heart then light without discarding nonbasic board tokens");
    Check(replay.tokens[6] == board.tokens[7] && replay.tokens[8] == board.tokens[8] &&
        replay.tokens[7] == board.tokens[13] && replay.tokens[13] == board.tokens[6] &&
        replay.enhancement[6] == board.enhancement[7] && replay.enhancement[8] == board.enhancement[8],
        "multi-take replay preserves complete synthetic native identities and enhancement fields");
    Check(PHOrbReplay(&board,&route,1,&replay) && replay.types[6] == 5 && replay.types[7] == 3 &&
        replay.tokens[7] == board.tokens[6], "full-list validated prefix stops at the requested entry");
    Check(PHOrbReplay(&board,&route,0,&replay) && PHOrbSameBoard(&board,&replay),
        "zero-step prefix still validates the complete exchange sequence");
    PHOrbBoard inPlace = board;
    Check(PHOrbReplay(&inPlace,&route,4,&inPlace) && inPlace.types[6] == 5,
        "explicit full replay supports aliased input and output");
    PHOrbRoute bad = route; bad.fishingExchanges[1].leave = 2;
    replay = board;
    Check(!PHOrbReplay(&board,&bad,0,&replay) && PHOrbSameBoard(&board,&replay),
        "wrong future donor invalidates even a zero-step prefix without altering output");
    bad = route; bad.fishingExchangeCount = PHOrbRouteMaxFishingExchanges + 1;
    Check(!PHOrbReplay(&board,&bad,1,&replay),"exchange array count is bounded before access");
    bad = route; bad.fishingExchangeCount = 1;
    Check(!PHOrbReplay(&board,&bad,1,&replay),"unlisted second spinner entry is rejected before prefix publication");
    bad = route; bad.fishingExchanges[1].entry = 2;
    Check(!PHOrbReplay(&board,&bad,1,&replay),"overlapping entry and exit declarations are rejected");
    bad = route; bad.fishingExchanges[1].exit = 5;
    Check(!PHOrbReplay(&board,&bad,1,&replay),"out-of-range exit rejects an otherwise valid first exchange");
    bad = route; bad.cells[4] = 0;
    Check(!PHOrbReplay(&board,&bad,1,&replay),"future nonadjacent move rejects an earlier prefix");
    bad = route; bad.fishing = false;
    Check(!PHOrbReplay(&board,&bad,1,&replay),"exchange list requires explicit fishing route identity");
    PHOrbBoard changed = board; changed.spinnerMask |= UINT64_C(1) << 8;
    Check(!PHOrbReplay(&changed,&route,1,&replay),"a route cannot enter another spinner even as an exit donor");
    changed = board; changed.spinnerMask |= (UINT64_C(1) << 23) | (UINT64_C(1) << 25);
    Check(PHOrbReplay(&changed,&route,4,&replay),"untouched extra spinners do not prohibit selected-cell fishing");
    changed = board; changed.tapeMask = UINT64_C(1) << 8; changed.flags[8] = PHOrbFlagTape;
    Check(!PHOrbReplay(&changed,&route,1,&replay),"taped future exit rejects the full route");
    changed = board; changed.flags[20] = PHOrbFlagLocked;
    Check(!PHOrbReplay(&changed,&route,1,&replay),"locked boards stay outside multi-take model");
    changed = board; changed.flags[20] = PHOrbFlagThorn;
    Check(!PHOrbReplay(&changed,&route,1,&replay),"thorn boards stay outside multi-take model");
    changed = board; changed.types[6] = PHOrbMortalPoison;
    Check(!PHOrbReplay(&changed,&route,1,&replay),"nonbasic held orb cannot enter the selected spinner");
    changed = board; changed.types[8] = PHOrbMortalPoison; changed.enhancement[8] = 0;
    Check(!PHOrbReplay(&changed,&route,1,&replay),"nonbasic orb cannot be sacrificed as an exit donor");
    changed = board; changed.spinnerColors = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
    Check(!PHOrbReplay(&changed,&route,1,&replay),"missing heart in a restricted spinner table rejects the planned take");
    changed = board; changed.spinnerColors = (1u << 1) | (1u << 2) | (1u << 3) | (1u << 5);
    Check(!PHOrbReplay(&changed,&route,1,&replay),"missing donor in the spinner table rejects its future exchange");
    changed = board; changed.fixedStart = 1;
    Check(!PHOrbReplay(&changed,&route,1,&replay),"full replay enforces original fixed start");

    changed = board; changed.types[7] = 5;
    Check(PHOrbReplayFishingStep(&changed,&route,1,&replay) && replay.types[6] == 5 && replay.types[7] == 3,
        "live entry step consumes only the actually observed declared take");
    PHOrbBoard beforeExit = replay;
    Check(PHOrbReplayFishingStep(&beforeExit,&route,2,&replay) && replay.types[7] == 2,
        "live exit step verifies and deposits the declared donor");
    changed = replay; changed.types[7] = 3;
    Check(PHOrbReplayFishingStep(&changed,&route,3,&replay) && replay.types[8] == 3,
        "live second entry uses the new acknowledged board rather than an old prefix");
    changed = board;
    Check(!PHOrbReplayFishingStep(&changed,&route,1,&replay),"live entry never invents an unobserved take color");
    changed = beforeExit; changed.types[8] = 4;
    Check(!PHOrbReplayFishingStep(&changed,&route,2,&replay),"live exit rejects a changed donor color");
    changed = beforeExit; changed.fixedStart = 1;
    Check(PHOrbReplayFishingStep(&changed,&route,2,&replay),"live incremental replay does not reapply the old fixed-start gate");
    Check(!PHOrbReplayFishingStep(&board,&route,0,&replay) && !PHOrbReplayFishingStep(&board,&route,5,&replay),
        "live step index excludes zero and beyond route end");

    PHOrbBoard phases = board;
    phases.spinnerMask |= (UINT64_C(1) << 23) | (UINT64_C(1) << 25);
    phases.types[23] = 0; phases.types[25] = 3;
    PHOrbBoard advanced = phases;
    advanced.types[23] = 1; advanced.types[25] = 4; advanced.types[7] = 5;
    Check(!PHOrbSamePlanBoard(&phases,&advanced) &&
        PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[6]),
        "selected spinner can change relative phase while untouched spinners advance together");
    advanced.types[25] = 5;
    Check(!PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[6]),
        "the independent target exception cannot hide an unrelated spinner phase drift");
    advanced = phases; advanced.types[7] = 5;
    Check(!PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[7]),
        "selected spinner color must remain exact while the held token occupies it");
    advanced = phases; advanced.types[23] = 1; advanced.types[25] = 4;
    Check(PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[7]),
        "other spinners keep cycling while the selected spinner contains the held token");
    advanced = phases; advanced.types[6] = 5;
    Check(!PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[6]),"nonspinner color mismatch remains a hard failure");
    advanced = phases; advanced.tokens[8]++;
    Check(!PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[6]),"same-color donor replacement is rejected by token identity");
    advanced = phases; advanced.enhancement[7] = 0;
    Check(!PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[6]),"selected spinner metadata cannot drift with its color");
    advanced = phases; advanced.flags[7] = PHOrbFlagEffectFresh;
    Check(!PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[6]),"unrelated effect metadata change is not hidden");
    PHOrbBoard visual = phases;
    visual.flags[6] = PHOrbFlagBlindTouch;
    visual.flags[7] = PHOrbFlagBlindTouch | PHOrbFlagBlind | PHOrbFlagNail | PHOrbFlagCombo;
    visual.blindTurns[7] = 3;
    advanced = visual;
    advanced.flags[6] &= ~PHOrbFlagBlindTouch; advanced.flags[7] &= ~PHOrbFlagBlindTouch;
    Check(!PHOrbSameBoard(&visual,&advanced) && PHOrbSamePlanBoard(&visual,&advanced) &&
        PHOrbSameFishingPlanBoard(&visual,&advanced,7,visual.tokens[6]),
        "native ordinary darkness clearing uses the existing visual exception on target and ordinary cells");
    advanced.flags[7] &= ~PHOrbFlagBlind;
    Check(!PHOrbSameFishingPlanBoard(&visual,&advanced,7,visual.tokens[6]),
        "ordinary-darkness visual tolerance cannot hide super-darkness flag loss");
    advanced = visual; advanced.blindTurns[7] = 2;
    Check(!PHOrbSameFishingPlanBoard(&visual,&advanced,7,visual.tokens[6]),
        "ordinary-darkness visual tolerance keeps super-darkness duration exact");
    advanced = visual; advanced.tokens[6]++;
    Check(!PHOrbSameFishingPlanBoard(&visual,&advanced,7,visual.tokens[6]),
        "visual tolerance cannot hide a held-token replacement");
    visual = phases; visual.fixedStart = 7;
    visual.flags[5] = visual.flags[7] = PHOrbFlagStartDim;
    advanced = visual; advanced.flags[5] = advanced.flags[7] = 0;
    Check(PHOrbSamePlanBoard(&visual,&advanced) &&
        PHOrbSameFishingPlanBoard(&visual,&advanced,7,visual.tokens[6]),
        "accepted fixed-start dim clearing preserves the same selected coordinate during multi-take input");
    advanced.fixedStart = 8;
    Check(!PHOrbSameFishingPlanBoard(&visual,&advanced,7,visual.tokens[6]),
        "fixed-start dim tolerance cannot hide a changed start coordinate");
    visual.fixedStart = advanced.fixedStart = 0;
    Check(!PHOrbSameFishingPlanBoard(&visual,&advanced,7,visual.tokens[6]),
        "start dimming without a fixed-start rule remains invalid input");
    advanced = phases; advanced.spinnerColors = 31;
    Check(!PHOrbSameFishingPlanBoard(&phases,&advanced,7,phases.tokens[6]),"changed spinner color table invalidates the accepted route");
    Check(!PHOrbSameFishingPlanBoard(&phases,&phases,30,0) &&
        !PHOrbSameFishingPlanBoard(&phases,&phases,6,0),"selected cell must remain a valid current spinner");

    PHOrbRoute legacy; memset(&legacy,0,sizeof(legacy)); legacy.fishing = true; legacy.fishCell = 7; legacy.fishColor = 5;
    legacy.count = 3; legacy.cells[0] = 6; legacy.cells[1] = 7; legacy.cells[2] = 8;
    Check(PHOrbReplay(&board,&legacy,2,&replay) && replay.types[6] == 5,
        "zero-list legacy single-take routes retain their existing contract");
    legacy.cells[legacy.count++] = 7;
    Check(!PHOrbReplay(&board,&legacy,3,&replay),"legacy route still cannot silently become multi-take");
}
