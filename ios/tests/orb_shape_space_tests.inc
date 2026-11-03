static void ShapeSpaceTests() {
    auto board = ShapeBoard(5,4,{});
    const uint8_t cells[] = {5,3,3,3,3, 5,3,3,3,3, 3,5,5,5,3, 3,5,3,5,5};
    std::copy(std::begin(cells),std::end(cells),board.types);
    board.spinnerMask = UINT64_C(1) << 19; board.spinnerColors = 63;
    board.flags[14] = PHOrbFlagThorn; board.thornDamage[14] = 4;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = LightTHeartFourGoals(); options.goals.count = 2;
    options.diagonal = options.thornComboPriority = options.allowPoisonWithHeartFour = true;
    options.thorns = {465209,697814,0,true};
    PHOrbRoute route{}; std::atomic_bool cancel{false};
    // The T border is no longer a separator requirement (native T uses the
    // actual removed group), so this board must reach the beam search.
    Check(PHOrbSolve(board,options,cancel,route) != PHOrbSolveStatus::IncompatibleGoals,
        "a dense light board is not rejected by the retired conservative T/four separator bound");
    // The lower bound must not reject a legal boundary case. This witness
    // uses eight fixed hearts; the bottom-right spinner cannot touch its T.
    auto witness = ShapeBoard(5,4,{});
    std::fill(witness.types,witness.types + 20,3);
    for (unsigned cell : {1u,2u,3u,4u,8u,11u,12u,15u}) witness.types[cell] = 5;
    witness.types[19] = 0; witness.spinnerMask = UINT64_C(1) << 19; witness.spinnerColors = 63;
    TapeExcept(witness,{6,7}); witness.fixedStart = 7; options.steps = 1; options.thorns = {};
    Check(PHOrbSolve(witness,options,cancel,route) == PHOrbSolveStatus::Solved && route.goalsMet,
        "eight-separator T/four witness survives every spinner phase and remains executable under the lower bound");
    PHOrbBoard replay{}; PHOrbEvaluation evaluation{};
    Check(PHOrbReplay(&witness,&route,route.count - 1,&replay), "separator boundary witness returns a legal swap");
    for (unsigned phase = 0; phase < 6; ++phase) {
        auto variant = replay; variant.types[19] = uint8_t(phase);
        Check(PHOrbEvaluate(variant,options.goals,evaluation) && evaluation.goalsMet && FourOracle(variant,5) >= 1,
            "independent all-phase shape checks validate the minimum-separator witness");
    }
    options.goals.items[1] = {PHOrbGoalMatch,32,1,4}; options.steps = 1;
    Check(PHOrbSolve(board,options,cancel,route) != PHOrbSolveStatus::IncompatibleGoals,
        "the exact-four separator bound does not reject a different at-least-four goal");
}
