// Hand-constructed removal sequence. No skyfall or search result is used as
// the expected answer: rows 1..5 remove first, then row 0 falls intact.
static PHOrbBoard UnmatchableCascadeFixture() {
    auto board=FixedBoardFixture(7,6,{
        4,4,4,4,0,1,2,
        1,1,1,1,2,3,4,
        3,3,3,3,4,5,0,
        5,5,5,5,0,1,2,
        2,2,2,2,3,4,5,
        0,0,0,0,1,2,3});
    board.unmatchableColors=16; return board;
}
static void UnmatchableSolverTests() {
    auto board=UnmatchableCascadeFixture();
    auto options=PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true,5};
    options.goals=Goals(PHOrbGoalColors,63,6,0);
    options.goals.items[options.goals.count++]={PHOrbGoalFour,32,1,0};
    options.goals.items[options.goals.count++]={PHOrbGoalFour,5,1,0};
    PHOrbEvaluation value{};
    Check(PHOrbEvaluateWithOptions(board,options,value) && value.goalsMet && value.fixedBoardSatisfied &&
        value.unmatchableRecovered && value.firstWaveColors==47 && value.recoveredColors==16 &&
        value.firstWaveCombos==5 && value.combos==6 && value.firstWaveFours==5 &&
        value.heartFours==1 && !value.recoveryHeartFours,
        "first native fire four recovers dark for the later existing-orb cascade without claiming first-wave six colors");
    Check(PHOrbInspectInventory(board,options).kind==PHOrbInventoryKind::None,
        "necessary inventory check permits the scoped recovery without treating it as an executable solution");
    auto wood=board;
    for (auto &c : wood.types) { if (!c) c=2; else if (c==2) c=0; }
    options.fixedBoard.unmatchableRecoveryColors=4;
    Check(PHOrbEvaluateWithOptions(wood,options,value) && value.goalsMet && value.recoveredColors==16,
        "a verified wood provider may supply the same first-group recovery");
    options.fixedBoard.unmatchableRecoveryColors=5;
    auto heart=board; heart.unmatchableColors=32;
    for (auto &c : heart.types) { if (c==4) c=5; else if (c==5) c=4; }
    Check(PHOrbEvaluateWithOptions(heart,options,value) && value.goalsMet && value.firstWaveColors==31 &&
        value.recoveredColors==32 && !value.heartFours && !value.heartMatches && value.recoveryHeartFours==1,
        "a recovered heart four satisfies the fixed attack only, leaving first-wave healing counters at zero");
    for (unsigned scenario=0;scenario<6;++scenario) {
        auto changed=board; auto checked=options;
        if (!scenario) checked.fixedBoard.unmatchableRecoveryColors=0;
        if (scenario==1) checked.fixedBoard.unmatchableRecoveryColors=4;
        if (scenario==2) for (auto &c : changed.types) { if (!c) c=1; else if (c==1) c=0; }
        if (scenario==3) { changed.types[1]=1; changed.types[2]=2; changed.types[3]=3; }
        if (scenario==4) changed.types[28]=0; // An unmarked attached orb makes the native component non-isolated.
        if (scenario==5) { changed.spinnerMask=(UINT64_C(1)<<35)|(UINT64_C(1)<<4); changed.spinnerColors=63; }
        Check(PHOrbEvaluateWithOptions(changed,checked,value) && !value.goalsMet && !value.fixedBoardSatisfied,
            "missing policy/provider, wrong first group, absent cascade, attached orb or one failing spinner phase cannot authorize recovery");
        if (scenario==3) Check(value.unmatchableRecovered && !value.recoveredColors,
            "clearing the modeled mask is insufficient unless the formerly blocked color actually matches later");
    }
    for (unsigned scenario=0;scenario<10;++scenario) {
        auto changed=heart; auto checked=options;
        if (!scenario) changed.unmatchableColors=48;
        if (scenario==1) changed.unmatchableColors=0;
        if (scenario==2) changed.types[4]=6; // Poison/other special orbs are outside this branch.
        if (scenario==3) changed.flags[4]=PHOrbFlagThorn;
        if (scenario==4) changed.flags[4]=PHOrbFlagNail;
        if (scenario==5) changed.flags[4]=PHOrbFlagCombo;
        if (scenario==6) changed.minErase=4;
        if (scenario==7) checked.activity.firstWaveOnly=true;
        if (scenario==8) checked.goals.bonusFirstWave=true;
        if (scenario==9) { changed.spinnerMask=UINT64_C(1)<<4; changed.spinnerColors=127; }
        checked.allowPoisonWithHeartFour=checked.allowThornsWithHeartMatch=true;
        Check(!PHOrbEvaluateWithOptions(changed,checked,value),
            "unsupported effects and first-wave-only bonus contracts fail closed; recovered hearts never relax hazard gates");
    }
    board.spinnerMask=UINT64_C(1)<<4; board.spinnerColors=63; options.fishCell=4;
    Check(!PHOrbEvaluateWithOptions(board,options,value) && !PHOrbEvaluateMultiFishingBoard(board,4,options,value),
        "single and multi fishing public replay APIs reject the new recovery policy");
}
