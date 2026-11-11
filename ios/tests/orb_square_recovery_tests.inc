// Newly recovered seeded boards; retained witnesses are verification only.
// The solver receives the board and goals, never a preselected route.
static void SquareRecoveryTests() {
    struct Fixture { unsigned w,h; const char *board; bool heart; std::vector<uint8_t> witness; };
    const Fixture cases[] = {
        {6,5,"344153305345323014303222222235",false,{5,4,3,2,8,14,13,12,6,0,1,7,8,9,15,21,27,28,22,21,27,26,25,19,20,21,15,14}},
        {6,5,"241303330331031234145505403252",true,{8,7,1,2,8,14,13,19,20,26,27,21,15,14,20,26,25,19,20,26,27,28,22,23,17,11,5,4,10,11,17,23,29,28,22,21,20,19,13,7,6,12,13,7,8,14,13}},
        {7,6,"435145054155344543503131443023130534414514",false,{1,2,9,16,23,22,29,30,23,24,25,26,19,12,11,18,19,20,27,26,33,34,27,26,25,32,31,30,23,24,25,18,17,24,23,22,15,16,17,18,25,32,31,24,23,30,37,38,31}},
    };
    for (const auto &item : cases) {
        auto board = Board(item.w,item.h,1);
        for (unsigned i = 0; i < item.w*item.h; ++i) board.types[i] = uint8_t(item.board[i]-'0');
        const auto before = board;
        auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
        options.milliseconds = PHOrbMaxSearchMilliseconds;
        options.goals.count = 1; options.goals.items[0] = {PHOrbGoalSquare,8,1,0};
        if (item.heart) options.goals.items[options.goals.count++] = {PHOrbGoalFour,32,1,0};
        PHOrbRoute witness{}; witness.count = uint8_t(item.witness.size());
        std::copy(item.witness.begin(),item.witness.end(),witness.cells);
        PHOrbBoard replay{}; PHOrbEvaluation evaluation{}; uint32_t damage = 0; uint8_t contacts = 0;
        Check(PHOrbEvaluateRoute(board,witness,options,evaluation,damage,contacts) && evaluation.goalsMet,
            "retained square witness establishes a route within the unchanged step bound");
        std::atomic_bool cancel{false}; PHOrbRoute route{};
        const auto status = PHOrbSolve(board,options,cancel,route);
        Check(status == PHOrbSolveStatus::Solved && route.squareRecoveryPasses > 0 && route.squareRecoveryPasses <= 2 &&
            route.searchPasses <= 4 + route.squareRecoveryPasses && route.backtracks <= options.backtrackLimit &&
            route.count <= options.steps+1 && !std::memcmp(&before,&board,sizeof(board)),
            "square-only fallback recovers the recorded ordinary miss within bounded passes and moves");
        if (route.count >= 2 && PHOrbReplay(&board,&route,route.count-1,&replay)) {
            const auto score = Oracle(replay);
            Check(AlternateShapeOracle(replay,options.goals.items[0]) >= 1 &&
                (!item.heart || FourOracle(replay,5) >= 1) && score.first == route.combos && score.second == route.erased,
                "recovered square and heart-four route agrees with independent first-wave and cascade oracles");
        } else Check(false,"recovered square route replays");
        auto disabled = options; disabled.recoveryPasses = 0;
        PHOrbSolve(board,disabled,cancel,route);
        Check(route.squareRecoveryPasses == 0,"disabling recovery also disables the square fallback");
        if (&item == &cases[0]) {
            auto limited = options; limited.steps = 1; limited.beam = 32;
            PHOrbSolve(board,limited,cancel,route);
            Check(!route.goalsMet && route.squareRecoveryPasses == 2,
                "one-step unmet square supplies a control for excluded-mode checks");
            for (unsigned mode = 0; mode < 3; ++mode) {
                auto excluded = board; auto limits = limited;
                if (mode == 0) { excluded.spinnerMask = UINT64_C(1) << 29; excluded.spinnerColors = 63; }
                if (mode == 1) { excluded.flags[29] |= PHOrbFlagThorn; excluded.thornDamage[29] = 4; }
                if (mode == 2) limits.activity.firstWaveOnly = true;
                const auto excludedStatus = PHOrbSolve(excluded,limits,cancel,route);
                Check(excludedStatus != PHOrbSolveStatus::Invalid && excludedStatus != PHOrbSolveStatus::Failed &&
                    !route.goalsMet && route.squareRecoveryPasses == 0,
                    "spinner, thorn and activity searches retain their existing recovery scope");
            }
        }
        cancel = true;
        Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Cancelled && !route.count && !route.squareRecoveryPasses,
            "cancelled square search publishes neither route nor fallback work");
    }
}
