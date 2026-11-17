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
}
