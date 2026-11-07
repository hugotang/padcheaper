// Sanitized 0.5.40 regression fixtures. No successful route is seeded.
// Written only; execute with the next authorized full build/test.
static PHOrbBoard FourSpinnerThornBoard() {
    auto board = Board(6,5,1);
    const uint8_t types[] = {0,5,0,3,5,4,3,4,2,4,2,3,1,2,4,2,1,1,2,1,5,2,3,4,0,5,3,2,2,5};
    std::copy(std::begin(types),std::end(types),board.types);
    const uint32_t flags[] = {0,524288,0,0,0,524288,0,0,0,0,0,524288,0,524288,0,524288,0,524288,0,0,524288,0,0,524288,0,0,0,0,0,0};
    std::copy(std::begin(flags),std::end(flags),board.flags);
    const uint32_t enhancementBits[] = {1031127695,0,0,0,0,0,0,1031127695,1031127695,0,0,0,0,0,0,1031127695,0,1031127695,1031127695,0,1031127695,1031127695,1031127695,0,0,0,0,0,0,0};
    std::copy(std::begin(enhancementBits),std::end(enhancementBits),board.enhancement);
    const uint8_t blindTurns[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    std::copy(std::begin(blindTurns),std::end(blindTurns),board.blindTurns);
    const uint8_t thornPercent[] = {0,4,0,0,0,4,0,0,0,0,0,4,0,4,0,4,0,4,0,0,4,0,0,4,0,0,0,0,0,0};
    std::copy(std::begin(thornPercent),std::end(thornPercent),board.thornDamage);
    board.minErase = 3; board.spinnerColors = 63; board.spinnerMask = UINT64_C(4719744);
    return board;
}

static PHOrbBoard SingleSpinnerBlindBoard() {
    auto board = Board(6,5,1);
    const uint8_t types[] = {1,4,3,1,4,4,3,0,5,5,1,2,2,3,1,5,2,1,5,4,2,1,3,0,5,3,3,0,3,1};
    std::copy(std::begin(types),std::end(types),board.types);
    const uint32_t flags[] = {0,0,0,0,4096,0,0,0,4096,0,0,0,4096,0,0,4096,0,4096,0,0,0,0,0,4096,0,0,4096,4096,0,0};
    std::copy(std::begin(flags),std::end(flags),board.flags);
    const uint32_t enhancementBits[] = {1031127695,0,0,0,0,0,0,1031127695,0,0,1031127695,0,0,1031127695,0,0,1031127695,0,1031127695,1031127695,0,1031127695,1031127695,0,1031127695,0,0,0,0,0};
    std::copy(std::begin(enhancementBits),std::end(enhancementBits),board.enhancement);
    const uint8_t blindTurns[] = {0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,3,0,1,0,0,0,0,0,1,0,0,3,3,0,0};
    std::copy(std::begin(blindTurns),std::end(blindTurns),board.blindTurns);
    const uint8_t thornPercent[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    std::copy(std::begin(thornPercent),std::end(thornPercent),board.thornDamage);
    board.minErase = 3; board.spinnerColors = 63; board.spinnerMask = UINT64_C(512);
    return board;
}

static void ReportedColorSpinnerSolverTests() {
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = ColorSpinnerGoals(); options.diagonal = true;
    // Thorn HP remains unknown; never borrow unrelated main-panel HP.
    options.thorns = {};
    for (unsigned fixture = 0; fixture < 2; ++fixture) for (unsigned variant = 0; variant < 3; ++variant) {
        const auto original = fixture ? SingleSpinnerBlindBoard() : FourSpinnerThornBoard();
        auto board = original;
        if (variant == 1) {
            for (unsigned i = 0; i < 30; ++i) if (board.types[i] < 5) board.types[i] = uint8_t((board.types[i] + 1) % 5);
        } else if (variant == 2) {
            board.spinnerMask = 0;
            for (unsigned i = 0; i < 30; ++i) {
                const unsigned source = 29-i;
                board.types[i] = original.types[source]; board.flags[i] = original.flags[source];
                board.tokens[i] = original.tokens[source]; board.enhancement[i] = original.enhancement[source];
                board.blindTurns[i] = original.blindTurns[source]; board.thornDamage[i] = original.thornDamage[source];
                if (original.spinnerMask & (UINT64_C(1) << source)) board.spinnerMask |= UINT64_C(1) << i;
            }
        }
        const auto before = board;
        const auto status = PHOrbSolve(board,options,cancel,route);
        Check(status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet ||
            (status == PHOrbSolveStatus::NoRoute && route.deadlineReached),
            "reported spinner layouts produce a checked candidate or explicit search incompleteness");
        Check(!std::memcmp(&before,&board,sizeof(board)) && route.count <= options.steps + 1 &&
            route.searchPasses <= 4 && route.backtracks <= options.backtrackLimit,
            "reported layouts retain immutable metadata and original search limits");
        if (route.count) {
            CheckColorSpinnerRoute(board,options,status,route);
            PHOrbBoard moved = board; unsigned contacts = 0;
            for (unsigned step = 1; step < route.count; ++step) {
                const unsigned from = route.cells[step-1], to = route.cells[step];
                contacts += (moved.flags[to] & PHOrbFlagThorn) != 0;
                std::swap(moved.flags[from],moved.flags[to]);
            }
            Check(!contacts && !route.thornContacts && !route.thornDamage,
                "reported unknown-HP thorn board never contacts an unheld thorn even when starting on one");
        }
        std::printf("Reported colour spinner fixture %u variant %u: status=%u colours=%u/5 heart=%u steps=%u ms=%u passes=%u calls=%u hits=%u\n",
            fixture,variant,unsigned(status),route.goalHits[0],route.goalHits[1],route.count ? route.count-1 : 0,
            route.elapsedMS,route.searchPasses,route.evaluationCalls,route.evaluationCacheHits);
    }
    // Match the reported online stage sizes without granting any of these
    // calls extra recovery time. Session tests cover the shared deadline;
    // this fixture independently checks every candidate from each raw call.
    const auto single = SingleSpinnerBlindBoard();
    for (int take : {-1,0,2}) {
        auto stage = options; stage.milliseconds = take < 0 ? 400 : 133;
        stage.fishCell = take < 0 ? -1 : 9; stage.fishColor = take < 0 ? 0u : unsigned(take);
        const auto status = PHOrbSolve(single,stage,cancel,route);
        Check(status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet ||
            (status == PHOrbSolveStatus::NoRoute && route.deadlineReached),
            "recorded ordinary/fire/wood slices return a checkable candidate or explicit deadline");
        Check(route.count <= stage.steps + 1 && route.searchPasses <= (take < 0 ? 4u : 1u) &&
            (!route.fishing || (route.fishCell == 9 && route.fishColor == unsigned(take))) &&
            !route.fishingExchangeCount && !route.thornContacts,
            "a short single-take attempt keeps its requested source/color and cannot inherit ordinary recovery passes");
        if (route.count) CheckColorSpinnerRoute(single,stage,status,route);
        std::printf("Reported single spinner stage take=%d: status=%u colours=%u/5 heart=%u fishing=%u steps=%u ms=%u calls=%u hits=%u\n",
            take,unsigned(status),route.goalHits[0],route.goalHits[1],route.fishing,route.count ? route.count-1 : 0,
            route.elapsedMS,route.evaluationCalls,route.evaluationCacheHits);
    }
}
