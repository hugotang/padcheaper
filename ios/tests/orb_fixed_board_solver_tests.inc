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
}
