// Fixed-plan board contracts: independent layouts, strict first removal and
// cascade constraints. Cases are authored here; execution awaits build/test.
static PHOrbBoard FixedBoardFixture(unsigned width, unsigned height, std::initializer_list<unsigned> cells) {
    PHOrbBoard board{}; board.width = uint8_t(width); board.height = uint8_t(height);
    unsigned i = 0;
    for (unsigned color : cells) { board.types[i] = uint8_t(color); board.tokens[i] = i + 1; ++i; }
    return board;
}
static void FixedBoardSolverTests() {
    PHOrbEvaluation value{};
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.fixedBoard.mode = PHOrbFixedBoardMode::OneCombo;
    auto single = FixedBoardFixture(5,4,{
        0,0,0,1,2,
        1,2,3,4,5,
        2,3,4,5,1,
        3,4,5,1,2});
    Check(PHOrbEvaluateWithOptions(single,options,value) && value.goalsMet &&
        value.fixedBoardSatisfied && value.firstWaveCombos == 1 && value.erased == 3,
        "one_combo accepts one actual three-orb group without a deterministic cascade");
    auto four = single; four.types[3] = 0;
    Check(PHOrbEvaluateWithOptions(four,options,value) && !value.goalsMet && !value.fixedBoardSatisfied,
        "one_combo rejects a four-orb group even though it is still one Combo");
    auto cascade = FixedBoardFixture(5,4,{
        1,2,3,4,5,
        2,3,4,5,4,
        3,4,1,2,3,
        1,1,0,0,0});
    // Removing the red triple lowers the upper 1 into the two lower 1s.
    Check(PHOrbEvaluateWithOptions(cascade,options,value) && value.firstWaveCombos == 1 &&
        value.combos > 1 && !value.fixedBoardSatisfied,
        "one_combo rejects a second Combo caused by existing-orb cascades");
    auto six = FixedBoardFixture(5,4,{
        0,0,0,1,2,
        3,4,5,1,2,
        3,4,5,1,2,
        3,4,5,0,1});
    options.fixedBoard = {PHOrbFixedBoardMode::SixColorStall,63,false};
    Check(PHOrbEvaluateWithOptions(six,options,value) && value.goalsMet &&
        value.firstWaveColors == 63 && value.firstWaveFours == 0 && value.resolvedFours == 0,
        "six-color stall accepts six native three-orb groups on a 5x4 board");
    auto missing = six; missing.types[7] = 4;
    Check(PHOrbEvaluateWithOptions(missing,options,value) && !value.goalsMet,
        "six-color stall cannot satisfy its shield with a missing first-wave heart color");
    PHOrbBoard attack{}; attack.width = 7; attack.height = 6;
    for (unsigned y = 0; y < 6; ++y) for (unsigned x = 0; x < 7; ++x) {
        const unsigned i = y * 7 + x;
        attack.types[i] = uint8_t(x < 4 ? y : (y + x - 3) % 6); attack.tokens[i] = i + 1;
    }
    Check(PHOrbEvaluateWithOptions(attack,options,value) && !value.goalsMet &&
        value.firstWaveColors == 63 && value.resolvedFours == 6,
        "six-color stall rejects four-orb firepower in any basic color including heart");
    options.fixedBoard = {PHOrbFixedBoardMode::FourAttack,63,true};
    Check(PHOrbEvaluateWithOptions(attack,options,value) && value.goalsMet &&
        value.firstWaveFours == 6 && value.heartFours == 1,
        "attack counts six exact four groups without requiring a separate saved goal for each color");
    Check(PHOrbEvaluateWithOptions(six,options,value) && !value.goalsMet && value.firstWaveColors == 63,
        "attack keeps the heart-four requirement on reduced boards instead of silently using three hearts");
    options.fixedBoard = {PHOrbFixedBoardMode::Normal,63,false};
    Check(PHOrbEvaluateWithOptions(six,options,value) && value.goalsMet,
        "six_colors supports the short expert-multicolor turn without a four-group requirement");
    options.fixedBoard.mode = PHOrbFixedBoardMode::OneCombo; options.fixedBoard.firstColors = 0;
    single.spinnerMask = UINT64_C(1) << 3; single.spinnerColors = 3;
    Check(PHOrbEvaluateWithOptions(single,options,value) && !value.goalsMet,
        "one_combo checks every common spinner phase and rejects the phase extending its triple to four");
    options.fixedBoard = {PHOrbFixedBoardMode::Normal,63,false};
    six.unmatchableColors = 32;
    Check(PHOrbEvaluateWithOptions(six,options,value) && !value.goalsMet,
        "fixed color contract honors native unmatchable colors");
    auto loneL = FixedBoardFixture(5,4,{
        0,1,2,3,4,
        0,2,3,4,5,
        0,0,0,5,1,
        2,3,4,1,2});
    options.goals = Goals(PHOrbGoalL,13,1,0);
    options.fixedBoard = {PHOrbFixedBoardMode::LOnly,0,false};
    Check(PHOrbEvaluateWithOptions(loneL,options,value) && value.goalsMet && value.erased == 5 &&
        value.firstWaveCombos == 1,
        "L-only recovery turn retains one fire/wood/light L without extra damage groups");
    loneL.types[15] = loneL.types[16] = loneL.types[17] = 2;
    Check(PHOrbEvaluateWithOptions(loneL,options,value) && !value.goalsMet,
        "L-only rejects an additional match even when its required L is present");
    options.fixedBoard.mode = static_cast<PHOrbFixedBoardMode>(255);
    Check(!PHOrbEvaluateWithOptions(six,options,value), "unknown fixed puzzle policy is rejected");

    // Freeze the B5 released layout with one equal-color swap. It is already
    // feasible but cannot gain a fourth group. Extra quality search must run
    // within the existing pass budget and retain the original valid fallback.
    auto finish=FixedBoardFixture(7,6,{
        2,0,3,4,1,1,2, 2,4,4,4,4,2,3, 2,4,5,3,3,3,4,
        2,1,5,5,0,2,2, 0,1,5,3,1,3,2, 2,1,5,0,0,0,2});
    finish.spinnerMask=UINT64_C(1)<<32; finish.spinnerColors=63;
    finish.fixedStart=5;
    finish.tapeMask=((UINT64_C(1)<<42)-1) & ~((UINT64_C(1)<<4)|(UINT64_C(1)<<5));
    for (unsigned i=0;i<42;++i) finish.flags[i]=(finish.tapeMask&(UINT64_C(1)<<i)) ? PHOrbFlagTape : 0;
    options=PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.steps=12; options.beam=16; options.milliseconds=2000;
    options.goals=Goals(PHOrbGoalColors,63,6,0); options.goals.items[options.goals.count++]={PHOrbGoalFour,32,1,0};
    options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true};
    options.recoveryPasses=0; options.backtrackLimit=0;
    std::atomic_bool cancel{false}; PHOrbRoute baseline{}, improved{};
    Check(PHOrbSolve(finish,options,cancel,baseline)==PHOrbSolveStatus::Solved && baseline.searchPasses==1,
        "a frozen valid B5 attack establishes a baseline route without optional passes");
    options.recoveryPasses=1; options.backtrackLimit=2;
    Check(PHOrbSolve(finish,options,cancel,improved)==PHOrbSolveStatus::Solved && improved.searchPasses==2 &&
        improved.backtracks==0 && improved.goalCount==2 && improved.goalsMet && improved.hazardsSafe,
        "a valid attack still uses its one existing quality recovery without publishing the temporary extra goal");
    uint32_t damage=0; uint8_t contacts=0;
    Check(PHOrbEvaluateRoute(finish,improved,options,value,damage,contacts) && value.goalsMet &&
        value.fixedBoardSatisfied && value.firstWaveColors==63 && value.firstWaveFours==3 && value.heartFours==1 &&
        improved.count==baseline.count && !damage && !contacts,
        "an unsuccessful quality improvement retains the exact feasible first-wave-six and heart-four route");
    options.fixedBoard={}; PHOrbRoute ordinary{};
    Check(PHOrbSolve(finish,options,cancel,ordinary)==PHOrbSolveStatus::Solved && ordinary.searchPasses==1,
        "ordinary solved six-color and heart-four search keeps its previous stopping behavior");

    // The separate six-four layout reaches its exact inventory ceiling, so
    // quality recovery must not spend another pass on an impossible seventh.
    attack.fixedStart=1;
    attack.tapeMask=((UINT64_C(1)<<42)-1) & ~UINT64_C(3);
    for (unsigned i=0;i<42;++i) attack.flags[i]=(attack.tapeMask&(UINT64_C(1)<<i)) ? PHOrbFlagTape : 0;
    options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true};
    PHOrbRoute ceiling{};
    Check(PHOrbSolve(attack,options,cancel,ceiling)==PHOrbSolveStatus::Solved && ceiling.searchPasses==1 &&
        PHOrbEvaluateRoute(attack,ceiling,options,value,damage,contacts) && value.firstWaveFours==6,
        "a valid attack at its inventory ceiling does not schedule redundant quality recovery");
    cancel=true;
    Check(PHOrbSolve(finish,options,cancel,ceiling)==PHOrbSolveStatus::Cancelled && !ceiling.count,
        "cancelled attack quality search publishes no retained or temporary-goal route");

    // Actual B5 search input, not the frozen released board above. Under the
    // former two-thirds primary slice this feasible route could be abandoned
    // for first-colour recovery before it was discovered on a slower host.
    // Use the existing solo ceiling for a host-independent regression; the
    // separate recorded-budget probe checks the observed 800 ms configuration.
    auto liveFinish=FixedBoardFixture(7,6,{
        2,0,3,4,1,1,2, 4,5,4,2,4,2,3, 5,1,5,0,4,3,4,
        2,4,3,2,3,2,0, 0,0,2,5,1,2,2, 2,1,1,5,3,0,3});
    liveFinish.spinnerMask=UINT64_C(1)<<32; liveFinish.spinnerColors=63;
    liveFinish.minErase=3;
    options=PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.milliseconds=PHOrbSoloRetryMilliseconds;
    options.diagonal=options.allowPoisonWithHeartFour=options.allowThornFishing=true;
    options.allowThornsWithHeartMatch=options.thornComboPriority=true;
    options.goals=Goals(PHOrbGoalColors,63,6,0); options.goals.items[options.goals.count++]={PHOrbGoalFour,32,1,0};
    options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true};
    PHOrbRoute witness{}; witness.diagonal=true;
    const uint8_t witnessCells[]={24,16,10,18,25,17,23,16,8,14,22,30,29,37,31,30,38,31,23,16,24,25,31,24,
        25,26,33,27,26,25,24,31,39,33,41,34,26,25,24,16,22,15,16,8,7,15,14};
    witness.count=uint8_t(sizeof(witnessCells));
    std::copy(std::begin(witnessCells),std::end(witnessCells),witness.cells);
    Check(PHOrbEvaluateRoute(liveFinish,witness,options,value,damage,contacts) && value.goalsMet &&
        value.fixedBoardSatisfied && value.firstWaveFours==3 && value.firstWaveColors==63 && value.heartFours==1,
        "the recorded B5 ordinary path is an independently retained feasible three-four witness");
    cancel=false; PHOrbRoute retainedAttack{};
    Check(PHOrbSolve(liveFinish,options,cancel,retainedAttack)==PHOrbSolveStatus::Solved &&
        PHOrbEvaluateRoute(liveFinish,retainedAttack,options,value,damage,contacts) &&
        value.goalsMet && value.hazardsSafe && value.fixedBoardSatisfied &&
        value.firstWaveFours>=3 && value.firstWaveColors==63 && value.heartFours>=1 &&
        retainedAttack.goalCount==2 && !retainedAttack.fishing && retainedAttack.count<=49,
        "fixed attack refinement preserves a feasible baseline on the actual B5 board with the original release contract");

    auto novemberFinish=FixedBoardFixture(7,6,{
        5,1,4,5,5,2,0, 3,0,4,2,2,5,4, 2,5,1,5,0,2,1,
        3,2,4,1,1,3,0, 2,4,5,3,0,1,3, 0,0,1,1,5,3,1});
    novemberFinish.spinnerMask=UINT64_C(1)<<8; novemberFinish.spinnerColors=63; novemberFinish.minErase=3;
    PHOrbRoute devicePrefix{}; devicePrefix.diagonal=true;
    const uint8_t deviceCells[]={39,33,26,32,31,25,18,10,4,12,18,24,30,29,21,22,15,16,23,30,31,32,40,39,38,37,36,35,28,21,14,22,15,7};
    devicePrefix.count=sizeof(deviceCells);
    std::copy(std::begin(deviceCells),std::end(deviceCells),devicePrefix.cells);
    Check(PHOrbEvaluateRoute(novemberFinish,devicePrefix,options,value,damage,contacts) && value.goalsMet &&
        value.fixedBoardSatisfied && value.hazardsSafe && value.firstWaveFours==4 && value.firstWaveCombos==8,
        "0.5.52 B5 records an ordinary 33-step four-group baseline, not a failed drag");
    options.steps=34;
    PHOrbRoute extension{};
    Check(PHOrbSolveFixedAttackSuffix(novemberFinish,options,devicePrefix,cancel,extension)==PHOrbSolveStatus::Solved &&
        extension.count==35 && std::equal(std::begin(deviceCells),std::end(deviceCells),extension.cells) &&
        PHOrbEvaluateRoute(novemberFinish,extension,options,value,damage,contacts) && value.goalsMet &&
        value.hazardsSafe && value.firstWaveFours>=4 && !damage && !contacts,
        "the logged ordinary drag can continue with one remaining swap without starting over or losing its contract");
    options.steps=48;
    auto b5Quality=options; b5Quality.goals.items[b5Quality.goals.count++]={PHOrbGoalFour,63,5,0};
    devicePrefix.count=uint8_t(1+PHOrbAttackPrefixSteps(devicePrefix.count-1,options.steps));
    Check(PHOrbSolveFixedAttackSuffix(novemberFinish,b5Quality,devicePrefix,cancel,extension)==PHOrbSolveStatus::Solved &&
        devicePrefix.count==33 && extension.count>devicePrefix.count && extension.count<=options.steps+1 && !extension.fishing &&
        std::equal(deviceCells,deviceCells+devicePrefix.count,extension.cells) &&
        PHOrbEvaluateRoute(novemberFinish,extension,options,value,damage,contacts) && value.goalsMet &&
        value.hazardsSafe && value.fixedBoardSatisfied && value.firstWaveColors==63 && value.heartFours>=1 &&
        value.firstWaveFours>=5 && !damage && !contacts,
        "the B5 witness has a legal suffix that adds a fifth first-wave four across all spinner phases");

    auto b7Finish=FixedBoardFixture(6,5,{
        5,1,0,1,5,4, 0,1,0,4,5,4, 2,0,5,3,1,3,
        2,5,4,3,0,5, 1,2,3,8,2,3});
    b7Finish.spinnerMask=UINT64_C(1)<<6; b7Finish.spinnerColors=63; b7Finish.minErase=3;
    b7Finish.enhancement[2]=b7Finish.enhancement[3]=UINT32_C(3204448256);
    for (unsigned cell : {6u,12u,16u,20u,24u}) b7Finish.enhancement[cell]=UINT32_C(1031127695);
    const uint8_t b7Cells[]={16,23,22,15,10,9,2,7,13,19,24,25,20,21,22,29,23,17,10,4};
    PHOrbRoute b7Prefix{}; b7Prefix.diagonal=true; b7Prefix.count=sizeof(b7Cells);
    std::copy(std::begin(b7Cells),std::end(b7Cells),b7Prefix.cells);
    PHOrbEvaluation b7Baseline{};
    Check(PHOrbEvaluateRoute(b7Finish,b7Prefix,options,b7Baseline,damage,contacts) &&
        b7Baseline.goalsMet && b7Baseline.hazardsSafe && b7Baseline.firstWaveFours==3 && b7Baseline.firstWaveCombos==6,
        "the recorded B7 finish meets six colors and heart four with three first-wave fours");
    auto b7Quality=options;
    Check(PHOrbAttackQualityGoals(options.goals,b7Baseline.firstWaveFours,30/4,b7Quality.goals),
        "B7 finish is eligible for a stronger ordinary attack");
    b7Prefix.count=uint8_t(1+PHOrbAttackPrefixSteps(b7Prefix.count-1,options.steps));
    Check(b7Prefix.count==19 && PHOrbAttackPrefixSteps(0,48)==0 && PHOrbAttackPrefixSteps(1,48)==1 &&
        PHOrbAttackPrefixSteps(48,48)==32 && PHOrbAttackPrefixSteps(1,1)==0,
        "a full approach can revise its last swap while one-swap starts and the gesture reserve stay bounded");
    Check(PHOrbSolveFixedAttackSuffix(b7Finish,b7Quality,b7Prefix,cancel,extension)==PHOrbSolveStatus::Solved &&
        extension.count>b7Prefix.count && extension.count<=options.steps+1 && !extension.fishing &&
        std::equal(b7Cells,b7Cells+b7Prefix.count,extension.cells) &&
        PHOrbEvaluateRoute(b7Finish,extension,options,value,damage,contacts) && PHOrbPreferAttackQuality(value,b7Baseline) &&
        value.firstWaveColors==63 && value.heartFours>=1 && !value.mortalPoisonErased && !damage && !contacts,
        "the B7 approach can finish with at least four guaranteed first-wave fours under its original release goals");

    auto fullB5=FixedBoardFixture(7,6,{
        0,3,4,2,0,3,2, 1,4,5,5,0,5,4, 1,1,1,4,4,2,0,
        1,1,0,4,4,3,5, 3,0,0,0,1,1,1, 2,1,5,0,2,5,3});
    fullB5.spinnerMask=UINT64_C(1)<<8; fullB5.spinnerColors=63; fullB5.minErase=3;
    for (unsigned cell : {19u,24u,31u}) fullB5.enhancement[cell]=UINT32_C(3204448256);
    for (unsigned cell : {0u,2u,6u,8u,11u,15u,16u,28u,29u,30u,32u,35u,36u,37u,38u,39u,40u})
        fullB5.enhancement[cell]=UINT32_C(1031127695);
    const uint8_t finishCells[]={11,12,19,13,5,12,4,3,11,17,9,16,24,32,40,41,33,27,26,34,40,39,31,37,29,
        36,30,22,23,17,25,24,23,30,24,25,26,20,27,34,26,25,24,23,29,28,21,15,9};
    PHOrbRoute fullDrag{}; fullDrag.count=sizeof(finishCells); fullDrag.diagonal=true;
    std::copy(std::begin(finishCells),std::end(finishCells),fullDrag.cells);
    PHOrbEvaluation fullScore{};
    Check(PHOrbEvaluateRoute(fullB5,fullDrag,options,fullScore,damage,contacts) && fullScore.goalsMet &&
        fullScore.firstWaveFours==5 && fullScore.firstWaveCombos==7,
        "B5 finish uses all 48 moves and guarantees five fours and seven combos");
    auto extraGoal=options;
    Check(PHOrbAttackQualityGoals(options.goals,5,10,extraGoal.goals),"the extra B5 search requests six fours");
    auto approach=fullDrag; approach.count=uint8_t(1+PHOrbAttackPrefixSteps(fullDrag.count-1,options.steps));
    auto status=PHOrbSolveFixedAttackSuffix(fullB5,extraGoal,approach,cancel,extension);
    Check(PHOrbHasSearchCandidate(status,extension) && PHOrbEvaluateRoute(fullB5,extension,options,value,damage,contacts) &&
        (PHOrbPreferAttackQuality(value,fullScore) || PHOrbPreferExtraCombo(fullB5,fullDrag,fullScore,extension,value)),
        "a full-length B5 finish can improve its original contract within the gesture limit");
    const uint8_t bonusCells[]={11,12,19,13,5,12,4,3,11,17,9,16,24,32,40,41,33,27,26,34,40,39,31,37,29,
        36,30,22,23,17,25,24,23,30,24,23,29,28,21,15,9,3,11,12,20,13};
    PHOrbRoute bonusPath{}; bonusPath.count=sizeof(bonusCells); bonusPath.diagonal=true;
    std::copy(std::begin(bonusCells),std::end(bonusCells),bonusPath.cells);
    Check(PHOrbEvaluateRoute(fullB5,bonusPath,options,value,damage,contacts) && value.firstWaveCombos==8 &&
        PHOrbPreferExtraCombo(fullB5,fullDrag,fullScore,bonusPath,value) &&
        !PHOrbAddsOrdinaryAttackCombo(fullB5,bonusPath,fullDrag) && !PHOrbAddsOrdinaryAttackCombo(fullB5,fullDrag,fullDrag),
        "all six phases preserve the same seven original token groups and gain a basic-color combo");
    PHOrbEvaluation provisional{};
    Check(PHOrbEvaluateRoute(fullB5,bonusPath,extraGoal,provisional,damage,contacts) && !provisional.goalsMet &&
        provisional.firstWaveFours==5 && provisional.goalHits[2]==5 && value.goalsMet && value.matchedGoals==2,
        "the five-four candidate misses the provisional sixth four and still meets both original goals");
    auto originalContract=bonusPath;
    PHOrbApplyAttackQualityEvaluation(originalContract,value,options.goals);
    Check(originalContract.goalsMet && originalContract.goalCount==2 && originalContract.goalHits[2]==0 && !provisional.goalsMet,
        "normalization reports the original goals and preserves the provisional search outcome separately");
    auto rejectedScore=value; ++rejectedScore.mortalPoisonErased;
    Check(!PHOrbPreferExtraCombo(fullB5,fullDrag,fullScore,bonusPath,rejectedScore),"a combo cannot increase mortal poison exposure");
    auto speculative=bonusPath; speculative.fishing=true;
    Check(!PHOrbAddsOrdinaryAttackCombo(fullB5,fullDrag,speculative),"an ordinary combo tie cannot introduce a spinner take");
    const uint8_t exchangedCells[]={11,12,19,13,5,12,4,3,11,17,9,16,24,32,40,41,33,27,26,34,40,39,31,37,29,
        23,17,25,24,23,22,29,28,36,30,24,23,15,9,3,11,12,20,13};
    speculative=bonusPath; speculative.count=sizeof(exchangedCells);
    std::copy(std::begin(exchangedCells),std::end(exchangedCells),speculative.cells);
    Check(PHOrbEvaluateRoute(fullB5,speculative,options,value,damage,contacts) && value.firstWaveCombos==8 &&
        value.firstWaveFours==5 && !PHOrbPreferExtraCombo(fullB5,fullDrag,fullScore,speculative,value),
        "equal counts cannot replace original matched tokens on a board with weakened orbs");

    for (unsigned width : {5u, 6u, 7u}) {
        PHOrbBoard sample{};
        sample.width = uint8_t(width);
        sample.height = uint8_t(width - 1);
        const unsigned bottom = width * (sample.height - 1);
        for (unsigned cell = 0; cell < width * sample.height; ++cell)
            sample.types[cell] = uint8_t(1 + (cell / width + cell % width) % 2);
        sample.types[0] = sample.types[1] = sample.types[2] = 4;
        sample.types[bottom] = sample.types[bottom + 1] = sample.types[bottom + 3] = 0;
        sample.types[bottom + 2] = 3;
        sample.spinnerMask = UINT64_C(1) << (width - 1);
        sample.spinnerColors = (1u << PHOrbTypes) - 1;
        PHOrbRoute unchanged{}, extra{};
        unchanged.count = 3;
        unchanged.cells[0] = unchanged.cells[2] = uint8_t(width);
        unchanged.cells[1] = uint8_t(width + 1);
        extra.count = 2;
        extra.cells[0] = uint8_t(bottom + 2);
        extra.cells[1] = uint8_t(bottom + 3);
        Check(PHOrbSpinnerVariants(&sample) == PHOrbTypes && PHOrbAddsOrdinaryAttackCombo(sample, unchanged, extra),
            "every supported size and all ten possible spinner colors retain the matched tokens");
        sample.spinnerMask = UINT64_C(1) << bottom;
        Check(!PHOrbAddsOrdinaryAttackCombo(sample, unchanged, extra), "a gain in only the observed spinner phase is rejected");
        sample.spinnerMask = sample.spinnerColors = 0;
        sample.types[bottom] = sample.types[bottom + 1] = sample.types[bottom + 3] = PHOrbJammer;
        Check(!PHOrbAddsOrdinaryAttackCombo(sample, unchanged, extra), "a new jammer match is not a basic-color combo");
    }

    auto suffixBoard=FixedBoardFixture(7,6,{
        0,0,0,2,2,3,4, 1,1,1,1,0,2,3, 2,2,2,2,3,4,5,
        3,3,3,3,4,5,0, 4,4,4,4,5,0,1, 5,5,5,5,0,1,2});
    suffixBoard.fixedStart=7; suffixBoard.spinnerMask=UINT64_C(1)<<41; suffixBoard.spinnerColors=63;
    suffixBoard.tapeMask=((UINT64_C(1)<<42)-1)&~((UINT64_C(1)<<6)|(UINT64_C(1)<<5)|
        (UINT64_C(1)<<4)|(UINT64_C(1)<<3)|(UINT64_C(1)<<11));
    for (unsigned cell=0;cell<42;++cell)
        if (suffixBoard.tapeMask&(UINT64_C(1)<<cell)) suffixBoard.flags[cell]=PHOrbFlagTape;
    PHOrbRoute prefix{}; prefix.count=3; prefix.diagonal=true;
    prefix.cells[0]=6; prefix.cells[1]=5; prefix.cells[2]=4;
    options.steps=4;
    Check(PHOrbEvaluateRoute(suffixBoard,prefix,options,value,damage,contacts) && value.goalsMet &&
        value.firstWaveColors==63 && value.heartFours==1 && value.firstWaveFours==5,
        "the two-step prefix already satisfies the original fixed contract in every spinner phase");
    auto trial=options; trial.goals.items[trial.goals.count++]={PHOrbGoalFour,63,6,0};
    Check(PHOrbSolveFixedAttackSuffix(suffixBoard,trial,prefix,cancel,extension)==PHOrbSolveStatus::Solved && extension.count==5 &&
        std::equal(prefix.cells,prefix.cells+prefix.count,extension.cells) && extension.cells[3]==3 && extension.cells[4]==11 &&
        !extension.fishing && PHOrbEvaluateRoute(suffixBoard,extension,options,value,damage,contacts) &&
        value.goalsMet && value.fixedBoardSatisfied && value.hazardsSafe && value.firstWaveFours==6 && value.heartFours==1,
        "two more swaps improve the retained attack without another pickup, crossing a spinner or extending the four-step limit");
    trial.steps=3;
    Check(PHOrbSolveFixedAttackSuffix(suffixBoard,trial,prefix,cancel,extension)==PHOrbSolveStatus::GoalsUnmet && extension.count==4,
        "the suffix gets only the remaining step, not a fresh full gesture");
    trial.steps=2;
    Check(PHOrbSolveFixedAttackSuffix(suffixBoard,trial,prefix,cancel,extension)==PHOrbSolveStatus::Invalid && !extension.count,
        "a prefix with no steps remaining cannot start a refinement");
    trial.steps=4;
    auto invalid=prefix; invalid.cells[1]=41;
    Check(PHOrbSolveFixedAttackSuffix(suffixBoard,trial,invalid,cancel,extension)==PHOrbSolveStatus::Invalid && !extension.count,
        "an invalid prefix must replay from the actual board before any continuation");
    suffixBoard.flags[4]|=PHOrbFlagThorn; suffixBoard.thornDamage[4]=10;
    trial.thorns={11,100,0,true};
    Check(PHOrbSolveFixedAttackSuffix(suffixBoard,trial,prefix,cancel,extension)==PHOrbSolveStatus::Solved && extension.thornContacts==1 &&
        extension.thornDamage==10 && PHOrbEvaluateRoute(suffixBoard,extension,trial,value,damage,contacts) &&
        value.hazardsSafe && damage==10 && contacts==1,
        "prefix replay carries the displaced thorn and charges its contact exactly once over the complete route");
    trial.thorns.pendingDamage=1;
    Check(PHOrbSolveFixedAttackSuffix(suffixBoard,trial,prefix,cancel,extension)==PHOrbSolveStatus::Invalid && !extension.count,
        "pending damage and prefix contacts cannot exhaust HP even if the final layout has a heart four");
}
