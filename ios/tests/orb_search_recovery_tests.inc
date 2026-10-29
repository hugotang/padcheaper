static unsigned ConservativeTOracle(const PHOrbBoard &board, unsigned color) {
    unsigned hits = 0;
    const unsigned w = board.width, h = board.height;
    for (unsigned rotation = 0; rotation < 4; ++rotation)
        for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x) {
            std::set<unsigned> cells;
            for (auto local : {std::pair<unsigned,unsigned>{0,0}, {1,0}, {2,0}, {1,1}, {1,2}}) {
                unsigned px = local.first, py = local.second;
                for (unsigned r = 0; r < rotation; ++r) { unsigned old = px; px = 2 - py; py = old; }
                cells.insert((y + py) * w + x + px);
            }
            bool valid = true;
            for (unsigned i : cells) {
                valid &= board.types[i] == color;
                for (unsigned j = 0; j < w * h; ++j)
                    if (((i / w == j / w && (i + 1 == j || j + 1 == i)) || i + w == j || j + w == i) &&
                        !cells.count(j) && board.types[j] == color) valid = false;
            }
            hits += valid && FirstWaveGroupSize(board,*cells.begin()) == 5;
        }
    return hits;
}

static void SearchRecoveryTests() {
    std::atomic_bool cancel{false}; PHOrbRoute route{}; PHOrbBoard replay{};
    const uint8_t cells[] = {1,0,4,3,0,5,5,1,5,3,5,3,1,0,0,5,5,1,1,3,0,3,3,5,4,5,5,4,3,3,4,0,1,4,3,2,1,2,2,0,3,2};
    auto board = Board(7,6,1); std::copy(std::begin(cells),std::end(cells),board.types);
    board.spinnerMask = (UINT64_C(1) << 11) | (UINT64_C(1) << 21); board.spinnerColors = 63;
    auto goals = LightTHeartFourGoals(); goals.count = 2;
    // The retained route is an existence witness even on a throttled test
    // host; it never becomes a special-case seed in the shipping solver.
    const uint8_t witness[] = {33,34,27,26,19,18,25,24,17,16,23,30,29,22,23,24,31,30,29,36,37,38,39,32,31,30,23,22,15,8,9,10,17,24,23,22,29,30,23,16,9,10,3,4,5,6,13,12};
    route.count = sizeof(witness); std::copy(std::begin(witness),std::end(witness),route.cells);
    PHOrbEvaluation evaluated{};
    Check(PHOrbReplay(&board,&route,route.count - 1,&replay) && PHOrbEvaluate(replay,goals,evaluated) && evaluated.goalsMet,
        "retained device spinner fixture has a valid witness under unchanged exact goals");
    for (auto quality : {PHOrbSearchQuality::Fast, PHOrbSearchQuality::Balanced, PHOrbSearchQuality::Thorough}) {
        auto options = PHOrbOptionsForQuality(quality); options.goals = goals;
        const auto status = PHOrbSolve(board,options,cancel,route);
        Check((status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet) &&
            PHOrbReplay(&board,&route,route.count - 1,&replay), "real spinner board returns a legal bounded result at every quality");
        if (quality != PHOrbSearchQuality::Fast && !route.deadlineReached)
            Check(status == PHOrbSolveStatus::Solved, "balanced and thorough find the real two-spinner T/four solution without deadline pressure");
        unsigned minT = 42, minFour = 42, minCombo = 42;
        for (unsigned phase = 0; phase < 6; ++phase) {
            auto variant = replay;
            for (unsigned i : {11u,21u}) variant.types[i] = uint8_t((board.types[i] + phase) % 6);
            minT = std::min(minT,ConservativeTOracle(variant,3));
            minFour = std::min(minFour,FourOracle(variant,5));
            minCombo = std::min(minCombo,Oracle(variant).first);
        }
        Check(route.goalHits[0] == minT && route.goalHits[1] == minFour && route.combos == minCombo &&
            route.goalsMet == (minT > 0 && minFour > 0), "all six spinner phases match independent T/four/cascade oracles");
        Check(route.searchPasses >= 1 && route.searchPasses <= 4 && route.backtracks <= options.backtrackLimit &&
            route.exploredDepth <= options.steps && route.count <= options.steps + 1,
            "multiple searches share bounded passes, saved frontiers and gesture length");
        std::printf("Device spinner fixture quality %u: goals %u/2, %u steps, %u combos, %u ms, passes %u, backtracks %u\n",
            unsigned(quality),route.matchedGoals,unsigned(route.count - 1),route.combos,route.elapsedMS,route.searchPasses,route.backtracks);
    }
    // A custom cycle excluding red must not ban an adjacent red T, and
    // locked/single-color target spinners remain useful parts of shapes.
    for (unsigned mode = 0; mode < 3; ++mode) {
        auto near = ShapeBoard(6,5,{0,1,2,7,13});
        near.spinnerMask = UINT64_C(1) << (mode ? 0 : 3);
        near.spinnerColors = mode == 2 ? 1 : mode == 1 ? 63 : 6;
        near.types[mode ? 0 : 3] = mode ? 0 : 1;
        if (mode == 1) near.flags[0] = PHOrbFlagLocked;
        std::swap(near.types[13],near.types[14]); near.fixedStart = 15;
        PHOrbSolveOptions options{1,32,1000}; options.goals = Goals(PHOrbGoalT);
        Check(PHOrbSolve(near,options,cancel,route) == PHOrbSolveStatus::Solved && route.count == 2,
            "stable-color, locked and target-excluding spinner cycles retain valid shape guidance");
    }
    for (unsigned seed : {36u,55u}) {
        const auto ordinary = Board(6,5,seed);
        PHOrbSolveOptions primary{24,32,1000}; primary.goals = goals; primary.recoveryPasses = 0;
        PHOrbRoute first{}, second{}, third{};
        const auto firstStatus = PHOrbSolve(ordinary,primary,cancel,first);
        auto recovery = primary; recovery.recoveryPasses = 1;
        const auto secondStatus = PHOrbSolve(ordinary,recovery,cancel,second);
        recovery.backtrackLimit = 2;
        const auto thirdStatus = PHOrbSolve(ordinary,recovery,cancel,third);
        Check(firstStatus == PHOrbSolveStatus::GoalsUnmet && !first.deadlineReached,
            "bounded primary beam reproduces a missed solution");
        if (seed == 55) Check(secondStatus == PHOrbSolveStatus::Solved && second.searchPasses == 2,
            "missing-goal recovery finds a real solution discarded by the primary score");
        else Check(secondStatus == PHOrbSolveStatus::GoalsUnmet && thirdStatus == PHOrbSolveStatus::Solved &&
            third.backtracks > 0 && third.searchPasses > 2,
            "bounded revisit of discarded frontiers rescues a board missed by both whole-board searches");
        Check(third.goalsMet && PHOrbReplay(&ordinary,&third,third.count - 1,&replay) &&
            ConservativeTOracle(replay,3) >= 1 && FourOracle(replay,5) >= 1 && Oracle(replay).first == third.combos,
            "recovery and backtracking witnesses satisfy independent first-wave and gravity oracles");
    }
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough); options.goals = goals; options.milliseconds = 1;
    const auto start = std::chrono::steady_clock::now();
    PHOrbSolve(board,options,cancel,route);
    Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300) && route.searchPasses <= 4,
        "recovery and backtracking cannot restart the one-millisecond global deadline");
    cancel = true;
    Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Cancelled && !route.count,
        "cancelled recovery search cannot return an executable route");
    cancel = false; options.recoveryPasses = 2;
    Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Invalid, "unbounded recovery counts are rejected");
}
