// Pending execution. Regressions exercise public search and independently
// replay/score every returned route; a written fixture is not a success claim.
static PHOrbBoard RecordedColorSpinnerBoard() {
    const uint8_t types[] = {4,3,0,2,3,3, 1,2,3,0,0,3, 2,1,0,1,0,4,
        0,5,4,1,5,0, 3,5,1,3,2,2};
    const uint32_t enhancements[] = {0,0,1031127695,1031127695,0,0,
        0,0,0,1031127695,1031127695,0, 1031127695,0,1031127695,0,0,0,
        1031127695,1031127695,1031127695,0,0,1031127695, 0,0,0,1031127695,1031127695,0};
    auto board = Board(6,5,1);
    std::copy(std::begin(types),std::end(types),board.types);
    std::copy(std::begin(enhancements),std::end(enhancements),board.enhancement);
    board.flags[16] = PHOrbFlagCombo; board.minErase = 3;
    board.spinnerMask = UINT64_C(1) << 28; board.spinnerColors = 63;
    return board;
}

static PHOrbGoalSet ColorSpinnerGoals() {
    auto goals = Goals(PHOrbGoalColors,31,5);
    goals.items[goals.count++] = {PHOrbGoalMatch,32,1,3}; return goals;
}

static bool ColorSpinnerIndependentReplay(const PHOrbBoard &board, const PHOrbRoute &route, PHOrbBoard &released) {
    const unsigned width = board.width, cells = width * board.height;
    if (route.count < 2 || route.count > PHOrbMaxSteps + 1 || route.fishingExchangeCount ||
        (board.fixedStart && unsigned(route.cells[0]) + 1 != board.fixedStart)) return false;
    if (route.fishing && (route.fishCell >= cells || route.fishColor >= 6 ||
        !(board.spinnerMask & (UINT64_C(1) << route.fishCell)) ||
        (board.flags[route.fishCell] & PHOrbFlagLocked) || !(board.spinnerColors & (1u << route.fishColor)))) return false;
    bool took = false;
    released = board;
    for (unsigned step = 0; step < route.count; ++step) {
        const unsigned target = route.cells[step];
        if (target >= cells || (board.tapeMask & (UINT64_C(1) << target))) return false;
        if (!step) {
            if (board.spinnerMask & (UINT64_C(1) << target)) return false;
            continue;
        }
        const unsigned previous = route.cells[step-1];
        const int dx = std::abs(int(previous % width) - int(target % width));
        const int dy = std::abs(int(previous / width) - int(target / width));
        if (route.diagonal ? (dx > 1 || dy > 1 || !(dx || dy)) : dx + dy != 1) return false;
        if (board.spinnerMask & (UINT64_C(1) << target)) {
            if (!route.fishing || took || target != route.fishCell ||
                !(board.spinnerColors & (1u << released.types[previous]))) return false;
            took = true; released.types[target] = route.fishColor;
        } else if (route.fishing && previous == route.fishCell &&
            !(board.spinnerColors & (1u << released.types[target]))) return false;
        std::swap(released.types[previous],released.types[target]);
        std::swap(released.tokens[previous],released.tokens[target]);
        std::swap(released.flags[previous],released.flags[target]);
        std::swap(released.enhancement[previous],released.enhancement[target]);
        std::swap(released.blindTurns[previous],released.blindTurns[target]);
        std::swap(released.thornDamage[previous],released.thornDamage[target]);
        released.flags[previous] &= ~PHOrbFlagBlindTouch;
        released.flags[target] &= ~PHOrbFlagBlindTouch;
    }
    return took == route.fishing;
}

static void CheckColorSpinnerRoute(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    PHOrbSolveStatus status, const PHOrbRoute &route) {
    PHOrbBoard released{};
    const bool valid = ColorSpinnerIndependentReplay(board,route,released);
    Check(valid,"multi-colour routes obey independent fixed-start, adjacency and declared single-take replay");
    if (!valid) return;
    PHOrbBoard productionReplay{};
    Check(PHOrbReplay(&board,&route,route.count-1,&productionReplay) &&
        !std::memcmp(released.types,productionReplay.types,sizeof(released.types)) &&
        !std::memcmp(released.tokens,productionReplay.tokens,sizeof(released.tokens)) &&
        !std::memcmp(released.flags,productionReplay.flags,sizeof(released.flags)) &&
        !std::memcmp(released.enhancement,productionReplay.enhancement,sizeof(released.enhancement)) &&
        !std::memcmp(released.blindTurns,productionReplay.blindTurns,sizeof(released.blindTurns)) &&
        !std::memcmp(released.thornDamage,productionReplay.thornDamage,sizeof(released.thornDamage)),
        "independent replay preserves recorded enhancement, Combo flags and token metadata at the same destinations");
    std::vector<unsigned> table;
    for (unsigned color = 0; color < 6; ++color) if (board.spinnerColors & (1u << color)) table.push_back(color);
    const unsigned variants = table.empty() ? 1 : unsigned(table.size());
    unsigned combos = PHOrbMaxCells, erased = PHOrbMaxCells, first = PHOrbMaxCells, attackHits = 6;
    bool heart = true;
    for (unsigned phase = 0; phase < variants; ++phase) {
        auto one = released; one.spinnerMask = 0; one.spinnerColors = 0;
        for (unsigned cell = 0; cell < board.width * board.height; ++cell)
            if ((board.spinnerMask & (UINT64_C(1) << cell)) && !(board.flags[cell] & PHOrbFlagLocked)) {
                const auto initial = std::find(table.begin(),table.end(),released.types[cell]);
                Check(initial != table.end(),"independent shared-phase source belongs to the recorded table");
                if (initial == table.end()) return;
                one.types[cell] = uint8_t(table[(size_t(initial-table.begin()) + phase) % table.size()]);
            }
        const auto oracle = ColorOracle(one);
        const unsigned colors = options.goals.bonusFirstWave ? oracle.firstColors : oracle.colors;
        combos = std::min(combos,oracle.combos); erased = std::min(erased,oracle.erased);
        first = std::min(first,oracle.firstCombos); attackHits = std::min(attackHits,ColorCount(colors & options.goals.items[0].colors));
        heart &= (colors & options.goals.items[1].colors) != 0;
    }
    const bool met = attackHits >= options.goals.items[0].count && heart;
    Check(route.combos == combos && route.erased == erased && route.goalHits[0] == attackHits &&
        (route.goalHits[1] != 0) == heart && route.goalsMet == met && route.hazardsSafe &&
        (status == PHOrbSolveStatus::Solved) == met,
        "every returned colour/heart result agrees with independently enumerated shared spinner phases");
    PHOrbEvaluation exact{}; uint32_t damage = 0; uint8_t contacts = 0;
    Check(PHOrbEvaluateRoute(board,route,options,exact,damage,contacts) &&
        exact.goalsMet == met && exact.firstWaveCombos == first && exact.combos == combos &&
        exact.goalHits[0] == attackHits && (exact.goalHits[1] != 0) == heart && !damage && !contacts,
        "uncached public release checks retain the original goals and all shared phases");
}

static void ColorSpinnerSolverTests() {
    const auto recorded = RecordedColorSpinnerBoard();
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.diagonal = true; options.goals = ColorSpinnerGoals();
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    // Original, cyclic relabeling and a 180-degree rotation exercise the
    // actual failed layout without giving the solver a successful path.
    for (unsigned variant = 0; variant < 3; ++variant) {
        auto board = recorded;
        for (unsigned i = 0; i < 30; ++i) {
            const unsigned source = variant == 2 ? 29-i : i;
            board.types[i] = recorded.types[source]; board.flags[i] = recorded.flags[source];
            board.tokens[i] = recorded.tokens[source]; board.enhancement[i] = recorded.enhancement[source];
            board.blindTurns[i] = recorded.blindTurns[source]; board.thornDamage[i] = recorded.thornDamage[source];
            if (variant == 1 && board.types[i] < 5) board.types[i] = uint8_t((board.types[i] + 1) % 5);
        }
        if (variant == 2) board.spinnerMask = UINT64_C(1) << 1;
        const auto original = board;
        Check(!std::strcmp(PHOrbColorRecoveryEligibilityReason(board,options),"eligible") &&
            PHOrbInspectInventory(board,options).kind == PHOrbInventoryKind::None,
            "recorded five-colour/heart spinner variants are structurally eligible and have sufficient necessary inventory");
        const auto status = PHOrbSolve(board,options,cancel,route);
        Check(status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet ||
            (status == PHOrbSolveStatus::NoRoute && route.deadlineReached),
            "recorded spinner variants return a checked route or explicit search incompleteness");
        Check(!std::memcmp(&board,&original,sizeof(board)) && route.searchPasses <= 4 &&
            route.backtracks <= options.backtrackLimit && route.exploredDepth <= options.steps,
            "spinner-aware colour recovery shares the original bounded passes, frontiers and input snapshot");
        if (!route.deadlineReached && status == PHOrbSolveStatus::GoalsUnmet)
            Check(route.searchPasses > 1,"a completed unsuccessful primary beam spends remaining budget on different colour guidance");
        if (route.count) CheckColorSpinnerRoute(board,options,status,route);
        std::printf("Recorded colour-heart spinner variant %u: status=%u colors=%u/5 heart=%u steps=%u elapsed=%u ms deadline=%u passes=%u\n",
            variant,unsigned(status),route.goalHits[0],route.goalHits[1],route.count ? route.count-1 : 0,
            route.elapsedMS,route.deadlineReached,route.searchPasses);
    }

    // Small deterministic graphs cover all geometries, restricted tables,
    // multiple sources and both movement policies without wall-time assertions.
    for (auto dimensions : {std::pair<unsigned,unsigned>{6,5},{7,6},{5,4}})
        for (bool diagonal : {false,true}) for (unsigned sources : {1u,3u}) for (unsigned table : {3u,37u,63u}) {
            auto board = Board(dimensions.first,dimensions.second,1);
            const unsigned cells = board.width * board.height;
            for (unsigned i = 0; i < cells; ++i) board.types[i] = uint8_t(i % 6);
            board.spinnerMask = UINT64_C(1) << (cells-1);
            if (sources == 3) board.spinnerMask |= (UINT64_C(1) << (cells-2)) | (UINT64_C(1) << (cells-3));
            board.spinnerColors = uint16_t(table);
            for (unsigned i = 0; i < cells; ++i) if (board.spinnerMask & (UINT64_C(1) << i)) board.types[i] = 0;
            TapeExcept(board,{0,1}); board.fixedStart = 1;
            auto shortOptions = options; shortOptions.diagonal = diagonal; shortOptions.steps = 1;
            // A two-colour request plus one heart still activates the same
            // policy while staying feasible by count on the 5x4 graph.
            shortOptions.goals.items[0] = {PHOrbGoalColors,3,2,0};
            const auto status = PHOrbSolve(board,shortOptions,cancel,route);
            Check(status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet ||
                status == PHOrbSolveStatus::InsufficientOrbs,
                "small colour graphs may fail exact goals or necessary inventory without opening a spinner");
            if (route.count) CheckColorSpinnerRoute(board,shortOptions,status,route);
        }
    cancel.store(true); route.count = 2;
    Check(PHOrbSolve(recorded,options,cancel,route) == PHOrbSolveStatus::Cancelled && !route.count,
        "cancellation cannot publish a prior colour-spinner route or start its recovery passes");
}
