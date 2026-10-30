static void PoisonHeartFourSolverTests() {
    PHOrbEvaluation evaluation{};
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.steps = 2; options.allowPoisonWithHeartFour = true;
    auto board = ShapeBoard(6, 5, {});
    board.types[0] = board.types[1] = board.types[2] = PHOrbPoison;
    for (unsigned i : {24u, 25u, 26u, 27u}) board.types[i] = 5;
    const auto oracle = EffectOracle(board);
    Check(oracle.poison == 3 && FourOracle(board, 5) == 1 &&
        PHOrbEvaluate(board, {}, evaluation) && !evaluation.hazardsSafe && evaluation.poisonErased == 3 &&
        PHOrbEvaluate(board, {}, evaluation, true) && evaluation.hazardsSafe && evaluation.heartFours == 1 &&
        evaluation.combos == oracle.combos && !evaluation.mortalPoisonErased,
        "normal poison is opt-in only with an independently verified first-wave heart four");
    TapeExcept(board, {28, 29}); board.fixedStart = 29;
    Check(PHOrbSolve(board, options, cancel, route) == PHOrbSolveStatus::Solved && route.poisonErased == 3 &&
        route.heartFours == 1 && route.hazardsSafe && route.count == 2,
        "an accepted normal poison route can be solved without changing tape or fixed start");
    uint32_t damage = 0; uint8_t contacts = 0;
    Check(PHOrbEvaluateRoute(board, route, options, evaluation, damage, contacts) && evaluation.hazardsSafe &&
        evaluation.poisonErased == route.poisonErased && !evaluation.mortalPoisonErased,
        "route replay applies the captured normal poison policy");
    options.allowPoisonWithHeartFour = false;
    Check(PHOrbEvaluateRoute(board, route, options, evaluation, damage, contacts) && !evaluation.hazardsSafe,
        "the same route cannot inherit another strategy's permission to consume poison");
    options.allowPoisonWithHeartFour = true;
    board.flags[29] = PHOrbFlagThorn; board.thornDamage[29] = 4; options.thorns = {40, 1000, 0, true};
    Check(PHOrbEvaluateRoute(board, route, options, evaluation, damage, contacts) && !evaluation.hazardsSafe && damage == 40,
        "accepting poison never credits healing to the thorn HP budget");
    options.thorns.hp = 41;
    Check(PHOrbEvaluateRoute(board, route, options, evaluation, damage, contacts) && evaluation.hazardsSafe,
        "normal poison plus hearts still permits a strictly survivable thorn contact");
    board = ShapeBoard(6, 5, {});
    for (unsigned i : {24u, 25u, 26u, 27u}) board.types[i] = 5;
    board.types[0] = board.types[1] = board.types[2] = PHOrbMortalPoison;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && !evaluation.hazardsSafe && evaluation.mortalPoisonErased == 3,
        "the normal poison option does not enable mortal poison even with hearts");
    board.types[0] = board.types[1] = board.types[2] = PHOrbPoison;
    board.types[28] = 5;
    Check(FourOracle(board, 5) == 0 && PHOrbEvaluate(board, {}, evaluation, true) && !evaluation.hazardsSafe,
        "five connected hearts cannot authorize normal poison");
    board.types[28] = 1; board.types[26] = 1;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && !evaluation.hazardsSafe && !evaluation.heartFours,
        "missing first-wave hearts never authorize poison");
    auto falling = ShapeBoard(6, 5, {26, 27, 28});
    for (unsigned i : {24u, 25u, 20u, 21u}) falling.types[i] = 5;
    falling.types[0] = falling.types[1] = falling.types[2] = PHOrbPoison;
    Check(FourOracle(falling, 5) == 0 && PHOrbEvaluate(falling, {}, evaluation, true) &&
        evaluation.poisonErased == 3 && !evaluation.hazardsSafe && !evaluation.heartFours,
        "a heart four created only by gravity cannot authorize poison");
    auto cascadingPoison = ShapeBoard(6, 5, {26, 27, 28});
    cascadingPoison.types[24] = cascadingPoison.types[25] = cascadingPoison.types[20] = PHOrbPoison;
    for (unsigned i : {0u, 1u, 2u, 3u}) cascadingPoison.types[i] = 5;
    Check(PHOrbEvaluate(cascadingPoison, {}, evaluation, true) && evaluation.hazardsSafe && evaluation.poisonErased == 3,
        "deterministic cascade poison is accepted only behind the same first-wave heart gate");
    cascadingPoison.types[24] = cascadingPoison.types[25] = cascadingPoison.types[20] = PHOrbBomb;
    Check(PHOrbEvaluate(cascadingPoison, {}, evaluation, true) && !evaluation.hazardsSafe && evaluation.unmatchedBombs == 3,
        "hearts and poison permission never postpone bomb removal to a later cascade");
    board = ShapeBoard(6, 5, {});
    for (unsigned i : {24u, 25u, 26u, 27u}) board.types[i] = 5;
    board.types[0] = board.types[1] = board.types[2] = PHOrbPoison;
    board.types[5] = PHOrbBomb;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && !evaluation.hazardsSafe && evaluation.unmatchedBombs == 1,
        "normal poison permission does not allow a lone bomb to remain");
    board.types[5] = 2; board.types[6] = board.types[7] = board.types[8] = PHOrbBomb;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && evaluation.hazardsSafe && !evaluation.unmatchedBombs,
        "a first-wave bomb clear can coexist with accepted normal poison");
    board.types[6] = board.types[7] = board.types[8] = 1;
    board.minMatch = 5;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && evaluation.poisonErased == 0 && !evaluation.heartFours,
        "native group minimums apply to both poison and hearts");
    board.minMatch = 0; board.unmatchableColors = 1u << 5;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && !evaluation.hazardsSafe && !evaluation.heartFours,
        "unmatchable hearts cannot authorize poison");
    board.unmatchableColors = 0;
    board.minErase = 5;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && !evaluation.heartFours && !evaluation.poisonErased,
        "a higher native run minimum prevents both the poison triple and the heart four");
    board.minErase = 3;
    board.spinnerMask = UINT64_C(1) << 28; board.spinnerColors = 63;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && !evaluation.hazardsSafe && !evaluation.heartFours,
        "every spinner phase must retain an exact heart four when any phase consumes poison");
    board.spinnerMask = UINT64_C(1) << 19;
    Check(PHOrbEvaluate(board, {}, evaluation, true) && evaluation.hazardsSafe && evaluation.heartFours == 1,
        "an unmarked side spinner retains the first-wave four and accepted poison in every phase");
    auto correlated = ShapeBoard(6, 5, {});
    correlated.types[0] = correlated.types[1] = correlated.types[2] = correlated.types[28] = PHOrbPoison;
    for (unsigned i : {24u, 25u, 26u, 27u}) correlated.types[i] = 5;
    correlated.spinnerMask = (UINT64_C(1) << 2) | (UINT64_C(1) << 28);
    correlated.spinnerColors = (1u << 5) | (1u << PHOrbPoison);
    Check(PHOrbEvaluate(correlated, {}, evaluation, true) && evaluation.poisonErased == 3 &&
        !evaluation.heartFours && !evaluation.hazardsSafe,
        "poison plus hearts in one phase does not waive the heart requirement in poison-free phases");
    board.spinnerMask = 0; board.spinnerColors = 0;
    board.types[0] = board.types[1] = 1;
    Check(PHOrbEvaluate(board, {}, evaluation) && evaluation.hazardsSafe && !evaluation.poisonErased,
        "a lone normal poison left on the board never behaves as an exploding bomb");

    auto choice = ShapeBoard(6, 5, {});
    choice.types[0] = choice.types[1] = choice.types[3] = PHOrbPoison;
    for (unsigned i : {24u, 25u, 26u, 27u}) choice.types[i] = 5;
    TapeExcept(choice, {2, 3, 4}); choice.fixedStart = 4;
    options.steps = 1; options.thorns = {}; options.allowPoisonWithHeartFour = false;
    PHOrbRoute strict{}, accepting{}; PHOrbBoard replay{};
    Check(PHOrbSolve(choice, options, cancel, strict) == PHOrbSolveStatus::Solved && !strict.poisonErased,
        "a one-swap strict choice retains the lower-combo poison-free route");
    options.allowPoisonWithHeartFour = true;
    Check(PHOrbSolve(choice, options, cancel, accepting) == PHOrbSolveStatus::Solved && accepting.hazardsSafe &&
        accepting.poisonErased == 3 && accepting.combos > strict.combos && accepting.cells[1] == 2 &&
        PHOrbReplay(&choice, &accepting, 1, &replay) && FourOracle(replay, 5) == 1 &&
        EffectOracle(replay).combos == accepting.combos,
        "accepted normal poison ranks extra Combo ahead of avoiding poison on an exhaustive one-swap choice");

    // Mixed hazard phases exercise normal/mortal distinction and first-wave
    // heart counts using independent whole-run, union and gravity oracles.
    for (unsigned seed = 1; seed <= 18; ++seed) {
        auto mixed = Board(seed % 3 == 0 ? 7 : seed % 3 == 1 ? 6 : 5,
                           seed % 3 == 0 ? 6 : seed % 3 == 1 ? 5 : 4, seed);
        const unsigned n = mixed.width * mixed.height;
        for (unsigned i = 0; i < n; ++i) mixed.types[i] = uint8_t((mixed.types[i] + i + seed) % 10);
        for (unsigned i = 0; i < 4; ++i) mixed.types[i] = 5;
        mixed.spinnerMask = UINT64_C(1) << (n - 1); mixed.spinnerColors = 0x3ff;
        mixed.minMatch = uint8_t(seed % 6); mixed.minErase = uint8_t(3 + seed % 3);
        unsigned poison = 0, mortal = 0, bombs = 0, hearts = n;
        for (unsigned phase = 0; phase < 10; ++phase) {
            auto variant = mixed;
            variant.types[n - 1] = uint8_t((mixed.types[n - 1] + phase) % 10);
            const auto effects = EffectOracle(variant);
            poison = std::max(poison, effects.poison); mortal = std::max(mortal, effects.mortal);
            bombs = std::max(bombs, effects.bombs); hearts = std::min(hearts, FourOracle(variant, 5));
        }
        Check(PHOrbEvaluate(mixed, {}, evaluation, true) && evaluation.poisonErased == poison &&
            evaluation.mortalPoisonErased == mortal && evaluation.unmatchedBombs == bombs && evaluation.heartFours == hearts &&
            evaluation.hazardsSafe == (!mortal && !bombs && (!poison || hearts)),
            "mixed hazard phases follow independent first-wave hearts, poison and mortal-poison counts");
    }
}
