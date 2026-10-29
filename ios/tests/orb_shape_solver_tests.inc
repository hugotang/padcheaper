// First-wave oracle for exact fours: scan complete runs, then union only
// marked neighbours. Unmarked same-colour neighbours never enter a group.
// This does not use production templates, marking or shape helpers.
static unsigned FourOracle(const PHOrbBoard &board, unsigned color) {
    const unsigned w = board.width, h = board.height, n = w * h;
    const unsigned minimum = board.minErase ? board.minErase : 3;
    if (board.unmatchableColors & (1u << color)) return 0;
    std::set<unsigned> marked;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const unsigned lines = axis ? w : h, length = axis ? h : w;
        for (unsigned line = 0; line < lines; ++line) {
            auto at = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
            for (unsigned begin = 0; begin < length;) {
                unsigned end = begin + 1;
                while (end < length && board.types[at(end)] == board.types[at(begin)]) ++end;
                if (board.types[at(begin)] == color && end - begin >= minimum)
                    for (unsigned p = begin; p < end; ++p) marked.insert(at(p));
                begin = end;
            }
        }
    }
    std::vector<unsigned> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    auto root = [&](unsigned a) { while (parent[a] != a) a = parent[a]; return a; };
    for (unsigned a : marked) for (unsigned b : marked)
        if (a < b && ((a / w == b / w && b == a + 1) || b == a + w)) parent[root(b)] = root(a);
    unsigned fours = 0;
    for (unsigned i = 0; i < n; ++i) {
        std::vector<unsigned> group;
        for (unsigned cell : marked) if (root(cell) == i) group.push_back(cell);
        if (group.size() != 4 || group.size() < board.minMatch) continue;
        bool row = true, column = true;
        for (unsigned cell : group) {
            row &= cell / w == group[0] / w;
            column &= cell % w == group[0] % w;
        }
        fours += row || column;
    }
    return fours;
}

static PHOrbGoalSet LightTHeartFourGoals() {
    auto goals = Goals(PHOrbGoalT, 1u << 3);
    goals.items[goals.count++] = {PHOrbGoalFour, 1u << 5, 1, 0};
    goals.items[goals.count++] = {PHOrbGoalCombo, 0, 1, 0};
    return goals;
}

static PHOrbBoard LightTHeartFourBoard(unsigned w, unsigned h) {
    auto board = Board(w, h, 1);
    const uint8_t background[] = {0, 1, 2, 4};
    for (unsigned i = 0; i < w * h; ++i) board.types[i] = background[(i % w + 2 * (i / w)) % 4];
    for (unsigned i : {0u, 1u, 2u, w + 1, 2 * w + 1}) board.types[i] = 3;
    for (unsigned x = 0; x < 4; ++x) board.types[(h - 1) * w + x] = 5;
    board.types[(h - 2) * w + 2] = 5; // A side neighbour, not part of a run.
    return board;
}

static void OrbShapeSolverTests() {
    PHOrbEvaluation result{};
    PHOrbRoute route{}; PHOrbBoard replay{};
    std::atomic_bool cancel{false};
    const auto goals = LightTHeartFourGoals();
    for (auto dimensions : {std::pair<unsigned, unsigned>{6, 5}, {7, 6}, {5, 4}}) {
        const unsigned w = dimensions.first, h = dimensions.second;
        for (bool vertical : {false, true}) {
            std::vector<unsigned> positions;
            for (unsigned k = 0; k < 4; ++k) positions.push_back(vertical ? k * w : k);
            positions.push_back(w + 1);
            auto board = ShapeBoard(w, h, positions);
            Check(FourOracle(board, 0) == 1 && PHOrbEvaluate(board, Goals(PHOrbGoalFour), result) &&
                result.goalsMet && result.goalHits[0] == 1,
                "an unmarked side neighbour preserves a horizontal or vertical exact four on every geometry");
            board.minErase = 4;
            Check(FourOracle(board, 0) == 1 && PHOrbEvaluate(board, Goals(PHOrbGoalFour), result) && result.goalsMet,
                "unmarked-neighbour handling respects the native four-orb run minimum");
            board.minMatch = 5;
            Check(FourOracle(board, 0) == 0 && PHOrbEvaluate(board, Goals(PHOrbGoalFour), result) && !result.goalsMet,
                "an unmarked neighbour cannot satisfy a five-orb connected-match minimum");
        }

        auto target = LightTHeartFourBoard(w, h);
        Check(FourOracle(target, 5) == 1 && PHOrbEvaluate(target, goals, result) && result.goalsMet &&
            result.goalHits[0] == 1 && result.goalHits[1] == 1 && result.hazardsSafe,
            "light T and heart four can coexist with an unmarked extra heart");
        const unsigned a = (h - 2) * w + 3, b = (h - 1) * w + 3;
        auto near = target; std::swap(near.types[a], near.types[b]);
        for (auto quality : {PHOrbSearchQuality::Fast, PHOrbSearchQuality::Balanced, PHOrbSearchQuality::Thorough}) {
            auto options = PHOrbOptionsForQuality(quality);
            options.steps = 1; options.milliseconds = 1000; options.goals = goals;
            Check(PHOrbSolve(near, options, cancel, route) == PHOrbSolveStatus::Solved && route.goalsMet &&
                route.count == 2 && PHOrbReplay(&near, &route, 1, &replay) && FourOracle(replay, 5) == 1 &&
                PHOrbEvaluate(replay, goals, result) && result.goalsMet,
                "each quality can complete both light-T/heart-four goals with one legal swap");
        }
        near.fixedStart = uint8_t(a + 1); TapeExcept(near, {a, b});
        const auto before = near;
        Check(PHOrbSolve(near, {3, 32, 1000, goals}, cancel, route) == PHOrbSolveStatus::Solved &&
            route.cells[0] == a && route.cells[1] == b && PHOrbReplay(&near, &route, route.count - 1, &replay) &&
            FourOracle(replay, 5) == 1 && std::memcmp(&near, &before, sizeof(near)) == 0,
            "joint-shape planning respects fixed start, sealed cells, replay and immutable input");
    }

    for (const auto &positions : std::vector<std::vector<unsigned>>{
        {0, 1, 2, 3, 4}, {0, 1, 2, 3, 7, 13}, {0, 1, 2, 3, 6, 7, 8}}) {
        auto board = ShapeBoard(6, 5, positions);
        Check(FourOracle(board, 0) == 0 && PHOrbEvaluate(board, Goals(PHOrbGoalFour), result) && !result.goalsMet,
            "a longer run or another marked run touching the four remains a merged non-four group");
    }
    auto pairs = ShapeBoard(6, 5, {0, 1, 2, 3, 7, 20, 24, 25, 26, 27});
    Check(FourOracle(pairs, 0) == 2 && PHOrbEvaluate(pairs, Goals(PHOrbGoalFour, 1, 2), result) &&
        result.goalsMet && result.goalHits[0] == 2, "two fours with unmarked neighbours count as two separate groups");
    for (unsigned i : {24u, 25u, 26u, 27u}) pairs.types[i] = 5;
    Check(PHOrbEvaluate(pairs, Goals(PHOrbGoalFour, 33, 2), result) && result.goalsMet && result.goalHits[0] == 2,
        "multiple requested groups may use different allowed colours");

    auto falling = ShapeBoard(6, 5, {26, 27, 28});
    for (unsigned i : {24u, 25u, 20u, 21u}) falling.types[i] = 5;
    Check(FourOracle(falling, 5) == 0 && PHOrbEvaluate(falling, Goals(PHOrbGoalFour, 32), result) && !result.goalsMet,
        "a four that exists only after falling still cannot satisfy a first-wave four goal");
    Check(PHOrbEvaluate(falling, Goals(PHOrbGoalMatch, 32, 1, 4), result) && result.goalsMet,
        "at-least-four remains a different goal and can be supplied by deterministic falling");

    auto spinner = ShapeBoard(6, 5, {24, 25, 26, 27});
    spinner.spinnerMask = UINT64_C(1) << 19; spinner.spinnerColors = 63;
    Check(PHOrbEvaluate(spinner, Goals(PHOrbGoalFour), result) && result.goalsMet,
        "an unmarked side spinner does not invalidate the four in any shared phase");
    spinner.spinnerMask = UINT64_C(1) << 28;
    Check(PHOrbEvaluate(spinner, Goals(PHOrbGoalFour), result) && !result.goalsMet,
        "an end spinner invalidates the goal in the phase that turns the four into five");
    spinner.spinnerMask = 0; spinner.spinnerColors = 0; spinner.unmatchableColors = 1;
    Check(PHOrbEvaluate(spinner, Goals(PHOrbGoalFour), result) && !result.goalsMet,
        "relaxing neighbour rejection never enables an unmatchable colour");
    spinner.unmatchableColors = 0;
    spinner.types[0] = spinner.types[1] = spinner.types[2] = PHOrbPoison;
    Check(PHOrbEvaluate(spinner, Goals(PHOrbGoalFour), result) && result.goalsMet && !result.hazardsSafe,
        "fulfilling a four never bypasses the poison gate");

    // Three adjacent swaps scramble both shapes. Reversing this known path
    // is a witness; a small, exhaustive-depth solve must recover a valid pair.
    auto scrambled = LightTHeartFourBoard(6, 5);
    const unsigned witness[] = {13, 19, 25, 26};
    for (unsigned i = 1; i < 4; ++i) std::swap(scrambled.types[witness[i - 1]], scrambled.types[witness[i]]);
    Check(PHOrbSolve(scrambled, {3, 512, 1000, goals}, cancel, route) == PHOrbSolveStatus::Solved &&
        route.count <= 4 && PHOrbReplay(&scrambled, &route, route.count - 1, &replay) && FourOracle(replay, 5) == 1 &&
        PHOrbEvaluate(replay, goals, result) && result.goalsMet,
        "a bounded continuous path can repair both shape goals together");

    // IMG_0407 transcription, not a native snapshot. Report quality results
    // when tests are explicitly run; never assert that this board is solved
    // without evidence. Even a partial result must replay and score exactly.
    auto sample = Board(7, 6, 1);
    const uint8_t cells[] = {
        3, 1, 0, 0, 3, 4, 5,
        3, 5, 5, 0, 3, 3, 5,
        5, 3, 0, 5, 1, 5, 2,
        3, 4, 5, 5, 3, 5, 4,
        1, 3, 5, 0, 3, 1, 2,
        0, 5, 2, 0, 3, 5, 4
    };
    std::copy(std::begin(cells), std::end(cells), sample.types);
    // Captured from the first authorized 0.5.3 desktop run. Keep a known
    // witness independent of future search ordering and machine deadlines.
    const uint8_t solvedPath[] = {32, 25, 24, 17, 16, 15, 14, 7, 0, 1, 8, 15, 14, 21, 22, 15, 14, 7, 8, 1, 2, 3, 10};
    PHOrbRoute witnessRoute{}; witnessRoute.count = uint8_t(std::size(solvedPath));
    std::copy(std::begin(solvedPath), std::end(solvedPath), witnessRoute.cells);
    Check(PHOrbReplay(&sample, &witnessRoute, witnessRoute.count - 1, &replay) && FourOracle(replay, 5) == 1 &&
        PHOrbEvaluate(replay, goals, result) && result.goalsMet && result.hazardsSafe && result.combos == 6 && result.erased == 21 &&
        Oracle(replay) == std::make_pair(6u, 21u),
        "the screenshot has a captured 22-step light-T/heart-four witness with six independently verified combos");
    const auto originalSample = sample;
    for (unsigned beam : {1u, 3u, 4u, 16u}) {
        auto status = PHOrbSolve(sample, {6, beam, 1000, goals}, cancel, route);
        Check((status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet) &&
            route.count >= 2 && route.count <= 7 && PHOrbReplay(&sample, &route, route.count - 1, &replay) &&
            PHOrbEvaluate(replay, goals, result) && result.goalsMet == route.goalsMet &&
            result.hazardsSafe == route.hazardsSafe &&
            std::equal(std::begin(result.goalHits), std::end(result.goalHits), route.goalHits) &&
            std::memcmp(&sample, &originalSample, sizeof(sample)) == 0,
            "small beams with and without reserved candidates preserve route bounds and exact replay scores");
    }
    auto rotating = LightTHeartFourBoard(7, 6);
    rotating.spinnerMask = UINT64_C(1) << 41; rotating.spinnerColors = 0x3ff;
    const auto started = std::chrono::steady_clock::now();
    auto limited = PHOrbSolve(rotating, {48, 512, 1, goals}, cancel, route);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    Check((limited == PHOrbSolveStatus::Solved || limited == PHOrbSolveStatus::GoalsUnmet ||
        limited == PHOrbSolveStatus::Unsafe || limited == PHOrbSolveStatus::NoRoute) && elapsed < 500,
        "joint shape guidance with ten spinner phases still observes a short deadline");
    if (route.count) Check(PHOrbReplay(&rotating, &route, route.count - 1, &replay) &&
        PHOrbEvaluate(replay, goals, result) && result.goalsMet == route.goalsMet &&
        result.hazardsSafe == route.hazardsSafe && result.poisonErased == route.poisonErased &&
        result.unmatchedBombs == route.unmatchedBombs &&
        std::equal(std::begin(result.goalHits), std::end(result.goalHits), route.goalHits),
        "a deadline result uses complete all-phase goal and hazard checks even when guidance is skipped for replay");
    cancel = true;
    Check(PHOrbSolve(sample, {48, 512, 1000, goals}, cancel, route) == PHOrbSolveStatus::Cancelled && !route.count,
        "cancelling a joint-shape search leaves no executable route");
    cancel = false;
    for (bool exact : {false, true}) for (auto quality :
        {PHOrbSearchQuality::Fast, PHOrbSearchQuality::Balanced, PHOrbSearchQuality::Thorough}) {
        auto options = PHOrbOptionsForQuality(quality); options.goals = goals;
        if (!exact) options.goals.items[1] = {PHOrbGoalMatch, 32, 1, 4};
        auto status = PHOrbSolve(sample, options, cancel, route);
        Check(status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet || status == PHOrbSolveStatus::NoRoute,
            "the screenshot yields a bounded result without invented solvability");
        if (route.count) {
            Check(route.count <= options.steps + 1 && PHOrbReplay(&sample, &route, route.count - 1, &replay) &&
                PHOrbEvaluate(replay, options.goals, result) && result.goalsMet == route.goalsMet &&
                (status == PHOrbSolveStatus::Solved) == route.goalsMet &&
                result.matchedGoals == route.matchedGoals && route.goalCount == options.goals.count &&
                std::equal(std::begin(result.goalHits), std::end(result.goalHits), route.goalHits) &&
                result.hazardsSafe && route.hazardsSafe,
                "screenshot routes retain per-goal replay and hazard verification");
            const auto oracle = Oracle(replay);
            Check(route.combos == oracle.first && route.erased == oracle.second &&
                (!exact || route.goalHits[1] == FourOracle(replay, 5)),
                "screenshot routes agree with independent cascade and exact-four oracles");
        }
        std::printf("IMG_0407 %s quality=%u status=%u goals=%u/%u T=%u heart=%u steps=%u ms=%u deadline=%u evaluated=%u\n",
            exact ? "four" : "match>=4", unsigned(quality), unsigned(status), unsigned(route.matchedGoals),
            unsigned(route.goalCount), unsigned(route.goalHits[0]), unsigned(route.goalHits[1]),
            route.count ? unsigned(route.count - 1) : 0, unsigned(route.elapsedMS), unsigned(route.deadlineReached),
            unsigned(route.evaluated));
    }
}
