// Geometry-only regression: four hearts already exist, but only wood tokens
// are reachable through the ordinary input graph. A passing inventory cannot
// establish a reachable four-heart layout. These are written test cases, not
// results from an executed search or device run.
static PHOrbBoard FishingGeometryBoard(bool remoteHearts = true) {
    auto board = ShapeBoard(6,5,{});
    const uint64_t reachable = (UINT64_C(1) << 7) | (UINT64_C(1) << 8) |
        (UINT64_C(1) << 14) | (UINT64_C(1) << 15);
    board.tapeMask = ((UINT64_C(1) << 30) - 1) & ~reachable;
    board.spinnerMask = UINT64_C(1) << 14; board.spinnerColors = 36;
    board.fixedStart = 8; board.minErase = 3;
    for (unsigned i = 0; i < 30; ++i) {
        board.types[i] = i == 9 || i == 10 || (remoteHearts && (i == 0 || i == 29)) ? 5 : 2;
        board.flags[i] = board.tapeMask & (UINT64_C(1) << i) ? PHOrbFlagTape : 0;
    }
    return board;
}
static PHOrbSolveOptions FishingGeometryOptions() {
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.fishCell = 14; options.diagonal = true; options.steps = 8;
    options.milliseconds = 1000; options.goals = Goals(PHOrbGoalFour,32);
    return options;
}
static PHOrbMultiFishingRoute FishingGeometryWitness() {
    PHOrbMultiFishingRoute route{};
    route.count = 5; route.fishCell = 14; route.diagonal = true; route.exchangeCount = 2;
    const uint8_t path[] = {7,14,8,14,15};
    std::copy(std::begin(path),std::end(path),route.cells);
    route.exchanges[0] = {1,2,5,2}; route.exchanges[1] = {3,4,5,2};
    route.plan.goals = FishingGeometryOptions().goals;
    route.plan.take[5] = route.plan.leave[2] = route.plan.exchangeCount = 2;
    return route;
}
static bool FishingGeometryHeartFour(const PHOrbBoard &board) {
    // Existing independent full-line/flood-fill oracle, shared with the
    // assist witness tests. No production template or evaluator is used.
    for (const auto &group : AssistFishingFirstGroups(board)) {
        if (group.size() != 4 || board.types[*group.begin()] != 5) continue;
        bool row = true, column = true;
        for (unsigned cell : group) {
            row &= cell / board.width == *group.begin() / board.width;
            column &= cell % board.width == *group.begin() % board.width;
        }
        if (row || column) return true;
    }
    return false;
}
static void FishingGeometryWitnessTests() {
    const auto board = FishingGeometryBoard(); const auto options = FishingGeometryOptions();
    Check(PHOrbBoardValid(&board) && std::count(board.types,board.types + 30,uint8_t(5)) == 4,
        "geometry fixture already has the four fixed hearts required by inventory");
    // Every ordinary destination (source 14 is blocked) is wood. Swapping
    // these tokens any number of times cannot change their color layout; all
    // four hearts are taped. This is a graph invariant, not a timeout result.
    const uint64_t ordinaryCells = ((UINT64_C(1) << 30) - 1) & ~(board.tapeMask | board.spinnerMask);
    Check(ordinaryCells == ((UINT64_C(1) << 7) | (UINT64_C(1) << 8) | (UINT64_C(1) << 15)),
        "ordinary movement can reach only the three wood cells outside the spinner");
    for (unsigned cell = 0; cell < 30; ++cell) {
        if (ordinaryCells & (UINT64_C(1) << cell)) Check(board.types[cell] == 2,"ordinary swaps preserve the all-wood component");
        if (board.types[cell] == 5) Check((board.tapeMask & (UINT64_C(1) << cell)) != 0,"every existing heart is immovable");
    }
    for (unsigned phase : {2u,5u}) {
        auto concrete = board; concrete.types[14] = uint8_t(phase); concrete.spinnerMask = concrete.spinnerColors = 0;
        Check(!FishingGeometryHeartFour(concrete),"no ordinary path can make a first-wave heart-four in either source phase");
        // One extracted heart can only be placed in this same component.
        // All such placements fail when the final source is wood, so no
        // one-take route can meet the required all-phase guarantee.
        if (phase == 2) for (unsigned destination : {7u,8u,15u}) {
            auto one = concrete; one.types[destination] = 5;
            Check(!FishingGeometryHeartFour(one),"one extra heart has no all-phase four-heart placement in this graph");
        }
    }

    const auto witness = FishingGeometryWitness(); PHOrbBoard replay{}; PHOrbEvaluation evaluated{};
    Check(PHOrbReplayMultiFishing(board,witness,witness.count - 1,replay) &&
        PHOrbEvaluateMultiFishing(board,witness,options,nullptr,evaluated) && evaluated.goalsMet && evaluated.hazardsSafe,
        "two extra geometry takes create a valid route despite zero initial inventory shortage");
    // Independent color/token replay of the explicit four moves.
    auto manual = board;
    for (unsigned step = 1; step < witness.count; ++step) {
        const unsigned from = witness.cells[step - 1], to = witness.cells[step];
        if (to == 14) manual.types[to] = 5;
        if (from == 14) Check(manual.types[to] == 2,"hand witness pays each heart with an existing wood donor");
        std::swap(manual.types[from],manual.types[to]); std::swap(manual.tokens[from],manual.tokens[to]);
    }
    Check(std::equal(manual.types,manual.types + 30,replay.types) && std::equal(manual.tokens,manual.tokens + 30,replay.tokens) &&
        replay.tokens[15] == board.tokens[7] && replay.tokens[0] == board.tokens[0] && replay.tokens[29] == board.tokens[29],
        "production replay agrees with independent swaps and leaves remote hearts and the held identity intact");
    for (unsigned phase : {2u,5u}) {
        auto concrete = manual; concrete.types[14] = uint8_t(phase); concrete.spinnerMask = concrete.spinnerColors = 0;
        const auto groups = AssistFishingFirstGroups(concrete);
        Check(FishingGeometryHeartFour(concrete) &&
            std::find(groups.begin(),groups.end(),std::set<unsigned>{7,8,9,10}) != groups.end(),
            "the full marked group is exactly 7/8/9/10 in both release colors, not a local four-cell subset");
    }
}
static bool FishingGeometryHasHeartPlan(const PHOrbMultiFishingInventory &inventory, unsigned count) {
    for (unsigned p = 0; p < inventory.count; ++p) {
        const auto &plan = inventory.plans[p];
        bool match = plan.exchangeCount == count && plan.take[5] == count && plan.leave[2] == count;
        for (unsigned color = 0; color < 6; ++color) {
            if (color != 5) match &= plan.take[color] == 0;
            if (color != 2) match &= plan.leave[color] == 0;
        }
        if (match) return true;
    }
    return false;
}
static void FishingGeometryCheckPlans(const PHOrbMultiFishingInventory &inventory, const PHOrbBoard &board,
    unsigned limit) {
    Check(inventory.valid && inventory.count <= PHOrbAssistFishingMaxPlans,
        "geometry planning preserves its public validity and retained-plan cap");
    Check(inventory.visitedStates <= PHOrbAssistFishingMaxInventoryStates &&
        inventory.geometryVisitedStates <= PHOrbMultiFishingMaxGeometryStates &&
        inventory.geometryVisitedStates <= inventory.visitedStates &&
        inventory.geometrySelectedPlans <= PHOrbMultiFishingMaxGeometryPlans,
        "geometry planning stays within the shared state cap and its smaller state/slot allowance");
    for (unsigned p = 0; p < inventory.count; ++p) {
        const auto &plan = inventory.plans[p];
        unsigned takes = 0, leaves = 0;
        for (unsigned color = 0; color < 6; ++color) {
            takes += plan.take[color]; leaves += plan.leave[color];
            Check(!(plan.take[color] && plan.leave[color]) && plan.leave[color] <= inventory.fixed[color] &&
                (!(plan.take[color] || plan.leave[color]) || (board.spinnerColors & (1u << color))),
                "geometry plans keep real in-table donors and never take and sacrifice the same color");
        }
        Check(takes == leaves && takes == plan.exchangeCount && takes <= limit &&
            plan.goals.count == 1 && plan.goals.items[0].kind == PHOrbGoalFour && plan.goals.items[0].colors == 32 &&
            plan.goals.items[0].count == 1 && !plan.predictedTurns && !plan.providers,
            "extra geometry exchanges conserve orb count, respect the cap and preserve the complete ordinary goal");
    }
}
static void FishingGeometryCheckBaselinePrefix(const PHOrbMultiFishingInventory &baseline,
    const PHOrbMultiFishingInventory &extended) {
    Check(baseline.valid && extended.valid && baseline.count && extended.count >= baseline.count &&
        extended.minimumExchanges == baseline.minimumExchanges && extended.count <= PHOrbAssistFishingMaxPlans &&
        extended.visitedStates <= PHOrbAssistFishingMaxInventoryStates &&
        extended.geometryVisitedStates <= PHOrbMultiFishingMaxGeometryStates &&
        extended.geometryVisitedStates <= extended.visitedStates &&
        extended.geometrySelectedPlans <= PHOrbMultiFishingMaxGeometryPlans &&
        extended.count - baseline.count == extended.geometrySelectedPlans,
        "five/six-take baselines retain their minimum and slots inside the original total state and plan caps");
    for (unsigned p = 0; p < extended.count; ++p)
        Check(extended.plans[p].exchangeCount <= PHOrbMultiFishingMaxExchanges &&
            extended.plans[p].geometryExtraExchanges <= PHOrbMultiFishingMaxGeometryExtra,
            "geometry extensions never raise the native six-exchange route cap");
    for (unsigned p = 0; p < baseline.count && p < extended.count; ++p) {
        const auto &before = baseline.plans[p], &after = extended.plans[p];
        Check(after.exchangeCount == before.exchangeCount && !after.geometryExtraExchanges &&
            after.predictedTurns == before.predictedTurns && after.providers == before.providers &&
            after.comboUpper == before.comboUpper &&
            std::equal(std::begin(before.take),std::end(before.take),after.take) &&
            std::equal(std::begin(before.leave),std::end(before.leave),after.leave) &&
            std::equal(std::begin(before.lCounts),std::end(before.lCounts),after.lCounts) &&
            after.goals.count == before.goals.count && after.goals.comboFirstWave == before.goals.comboFirstWave &&
            after.goals.bonusFirstWave == before.goals.bonusFirstWave,
            "each original recovery inventory remains in the same prefix position with its full allocation");
        for (unsigned g = 0; g < before.goals.count && g < after.goals.count; ++g) {
            const auto &a = before.goals.items[g], &b = after.goals.items[g];
            Check(a.kind == b.kind && a.colors == b.colors && a.count == b.count && a.length == b.length,
                "geometry expansion preserves every goal of an existing five/six-exchange recovery plan");
        }
    }
}
static void FishingGeometryTests() {
    FishingGeometryWitnessTests();
    const auto board = FishingGeometryBoard(); const auto options = FishingGeometryOptions();
    const auto baseline = PHOrbPlanMultiFishing(board,options,nullptr,6);
    FishingGeometryCheckPlans(baseline,board,6);
    Check(baseline.enumerationComplete && baseline.minimumExchanges == 0 && baseline.count == 1 &&
        FishingGeometryHasHeartPlan(baseline,0) && !baseline.plans[0].geometryExtraExchanges,
        "omitting the geometry option retains the previous single zero-exchange inventory plan");

    const auto extraOne = PHOrbPlanMultiFishing(board,options,nullptr,6,1);
    const auto extraTwo = PHOrbPlanMultiFishing(board,options,nullptr,6,2);
    FishingGeometryCheckPlans(extraOne,board,6); FishingGeometryCheckPlans(extraTwo,board,6);
    Check(extraOne.minimumExchanges == 0 && FishingGeometryHasHeartPlan(extraOne,0) &&
        FishingGeometryHasHeartPlan(extraOne,1) && !FishingGeometryHasHeartPlan(extraOne,2),
        "one extra exchange retains baseline and explores only one wood-to-heart improvement");
    Check(extraTwo.minimumExchanges == 0 && FishingGeometryHasHeartPlan(extraTwo,0) &&
        FishingGeometryHasHeartPlan(extraTwo,1) && FishingGeometryHasHeartPlan(extraTwo,2) &&
        extraTwo.plans[0].exchangeCount == 0 && !extraTwo.plans[0].geometryExtraExchanges,
        "two extra exchanges keep the original baseline first and retain the necessary two-heart geometry plan");
    for (unsigned p = 0; p < extraTwo.count; ++p)
        Check(extraTwo.plans[p].geometryExtraExchanges == extraTwo.plans[p].exchangeCount &&
            extraTwo.plans[p].geometryExtraExchanges <= 2,
            "zero-shortage geometry provenance records only the extra one/two exchanges");

    auto fullTable = board; fullTable.spinnerColors = 63;
    const auto fullGeometry = PHOrbPlanMultiFishing(fullTable,options,nullptr,6,2);
    FishingGeometryCheckPlans(fullGeometry,fullTable,6);
    Check(fullGeometry.minimumExchanges == 0 && FishingGeometryHasHeartPlan(fullGeometry,0) &&
        FishingGeometryHasHeartPlan(fullGeometry,2),
        "six-color geometry ranking retains the needed two-heart plan despite many one-take and off-goal alternatives");

    for (unsigned limit : {0u,1u,2u}) {
        const auto capped = PHOrbPlanMultiFishing(board,options,nullptr,limit,2);
        FishingGeometryCheckPlans(capped,board,limit);
        Check(FishingGeometryHasHeartPlan(capped,0) &&
            (FishingGeometryHasHeartPlan(capped,2) == (limit == 2)),
            "the total exchange cap bounds geometry expansion without removing its zero baseline");
    }
    Check(!PHOrbPlanMultiFishing(board,options,nullptr,6,3).valid,
        "geometry depth above the supported two-extra limit is rejected");

    auto restricted = board; restricted.spinnerColors = 5; // Fire/wood only.
    const auto absent = PHOrbPlanMultiFishing(restricted,options,nullptr,6,2);
    FishingGeometryCheckPlans(absent,restricted,6);
    Check(absent.minimumExchanges == 0 && FishingGeometryHasHeartPlan(absent,0),
        "a table without heart still retains the already-sufficient inventory baseline");
    for (unsigned p = 0; p < absent.count; ++p)
        Check(!absent.plans[p].take[5],"geometry expansion cannot request heart outside the native color table");
    PHOrbBoard replay{};
    Check(!PHOrbReplayMultiFishing(restricted,FishingGeometryWitness(),4,replay),
        "the same geometry witness is invalid when the spinner cannot provide its heart takes");

    const auto shortageBoard = FishingGeometryBoard(false);
    const auto shortageBase = PHOrbPlanMultiFishing(shortageBoard,options,nullptr,6);
    const auto shortageExtra = PHOrbPlanMultiFishing(shortageBoard,options,nullptr,6,2);
    FishingGeometryCheckPlans(shortageExtra,shortageBoard,6);
    Check(shortageBase.minimumExchanges == 2 && shortageExtra.minimumExchanges == 2 &&
        shortageBase.count && shortageExtra.count >= shortageBase.count &&
        FishingGeometryHasHeartPlan(shortageExtra,2) && shortageExtra.plans[0].exchangeCount == 2 &&
        !shortageExtra.plans[0].geometryExtraExchanges,
        "extra geometry work preserves the original two-heart shortage solution and discovered minimum");
    for (unsigned p = 0; p < shortageBase.count && p < shortageExtra.count; ++p) {
        const auto &before = shortageBase.plans[p], &after = shortageExtra.plans[p];
        Check(after.exchangeCount == before.exchangeCount && !after.geometryExtraExchanges &&
            std::equal(std::begin(before.take),std::end(before.take),after.take) &&
            std::equal(std::begin(before.leave),std::end(before.leave),after.leave),
            "baseline selection remains the same prefix before optional geometry alternatives");
    }
    for (unsigned p = 0; p < shortageExtra.count; ++p)
        Check(shortageExtra.plans[p].exchangeCount == 2 + shortageExtra.plans[p].geometryExtraExchanges &&
            shortageExtra.plans[p].geometryExtraExchanges <= 2,
            "geometry allowance is relative to its passing shortage baseline and still respects the total cap");

    const auto recoveryOptions = AssistFishingOptions(); const auto recovery = AssistFishingEvidence();
    for (unsigned minimum : {5u,6u}) {
        auto established = AssistFishingFiveBoard();
        TapeExcept(established,{7,13,14,15,19,20,21});
        if (minimum == 6) { established.types[24] = 2; established.spinnerColors = 44; }
        const auto originalPlans = PHOrbPlanMultiFishing(established,recoveryOptions,&recovery,6);
        const auto withGeometry = PHOrbPlanMultiFishing(established,recoveryOptions,&recovery,6,2);
        Check(originalPlans.minimumExchanges == minimum,
            "established recovery fixture retains its five/six-exchange minimum");
        FishingGeometryCheckBaselinePrefix(originalPlans,withGeometry);
        if (minimum == 6) Check(withGeometry.count == originalPlans.count && !withGeometry.geometrySelectedPlans,
            "a six-exchange baseline cannot consume any extra exchange allowance beyond the original route cap");
    }

    // Written bounded solver cases use the same tiny graph as the independent
    // impossibility invariant and four-step witness above. Their execution is
    // deferred until the next explicit build/test request.
    std::atomic_bool cancel{false}; PHOrbMultiFishingResult result{};
    const PHOrbTimedFishingTiming timing{600,4,12,24,10};
    for (unsigned extra : {0u,1u}) {
        const auto status = PHOrbSolveMultiFishing(board,options,nullptr,timing,6,cancel,result,extra);
        Check(status != PHOrbSolveStatus::Solved && !result.evaluation.goalsMet,
            "zero/one extra take cannot claim a solution contradicted by the independent geometry proof");
    }
    const auto solved = PHOrbSolveMultiFishing(board,options,nullptr,timing,6,cancel,result,2);
    const bool replayed = result.route.count && PHOrbReplayMultiFishing(board,result.route,result.route.count - 1,replay);
    Check(solved == PHOrbSolveStatus::Solved &&
        result.evaluation.goalsMet && result.evaluation.hazardsSafe && result.route.exchangeCount == 2 &&
        result.route.plan.geometryExtraExchanges == 2 && result.reservation.valid && result.reservation.fits &&
        replayed,
        "two optional geometry exchanges find and replay a fully funded all-phase route");
    if (replayed) {
        for (unsigned phase : {2u,5u}) {
            auto concrete = replay; concrete.types[14] = uint8_t(phase); concrete.spinnerMask = concrete.spinnerColors = 0;
            Check(FishingGeometryHeartFour(concrete),"the searched geometry route passes the independent terminal-group oracle");
        }
    }
    Check(PHOrbSolveMultiFishing(board,options,nullptr,timing,6,cancel,result,3) == PHOrbSolveStatus::Invalid &&
        !result.route.count,"invalid geometry depth cannot publish a stale or executable solver result");
    result.route = FishingGeometryWitness(); result.evaluation.goalsMet = true;
    cancel.store(true,std::memory_order_relaxed);
    Check(PHOrbSolveMultiFishing(board,options,nullptr,timing,6,cancel,result,2) == PHOrbSolveStatus::Cancelled &&
        !result.route.count && !result.evaluation.goalsMet && !result.inventory.visitedStates &&
        !result.inventory.geometryVisitedStates,
        "cancellation clears any prior route before baseline or geometry search can authorize a new result");
}
