static void InventoryDiagnosticTests() {
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    auto board = ShapeBoard(6,5,{});
    const uint8_t types[] = {5,2,3,0,3,1,4,0,3,5,3,5,5,5,3,0,3,5,3,4,3,2,3,5,5,4,4,1,4,5};
    std::copy(std::begin(types),std::end(types),board.types);
    board.spinnerMask = UINT64_C(1) << 16; board.spinnerColors = 63;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.goals.count = 3; options.goals.comboFirstWave = true;
    options.goals.items[0] = {PHOrbGoalT,8,1,0};
    options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    options.goals.items[2] = {PHOrbGoalCombo,0,13,0};
    auto issue = PHOrbInspectInventory(board,options.goals);
    Check(issue.kind == PHOrbInventoryKind::GoalCapacity && issue.goal == 2 && issue.required == 13 &&
        issue.available == 8 && issue.variants == 6 &&
        PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::InsufficientOrbs && !route.count && !route.evaluated,
        "recorded Uryu failure is a Combo capacity check, not insufficient light or hearts");
    options.goals.count = 2;
    Check(PHOrbInspectInventory(board,options.goals).kind == PHOrbInventoryKind::None,
        "the same recorded board has enough orbs for the original T and heart four (not a route proof)");

    board = ShapeBoard(6,5,{0,1,2}); options.goals = Goals(PHOrbGoalMatch,1,1,3);
    board.unmatchableColors = 1;
    issue = PHOrbInspectInventory(board,options.goals);
    Check(issue.kind == PHOrbInventoryKind::GoalCapacity && issue.available == 0 &&
        PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::InsufficientOrbs,
        "three existing red orbs cannot supply a match while red is unmatchable");
    board.unmatchableColors = 0; board.minMatch = 4; options.goals = Goals(PHOrbGoalExact,1,1,3);
    issue = PHOrbInspectInventory(board,options.goals);
    Check(issue.kind == PHOrbInventoryKind::MatchRule && issue.available == 3 && issue.required == 4 &&
        PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::InsufficientOrbs,
        "an exact three conflicts with minimum four even when all three orbs exist");

    board = ShapeBoard(6,5,{0,1,2,3,4}); options.goals = Goals(PHOrbGoalT);
    board.spinnerMask = 1; board.spinnerColors = 3;
    issue = PHOrbInspectInventory(board,options.goals);
    Check(issue.kind == PHOrbInventoryKind::ShapeOrbs && issue.phase == 1 && issue.variants == 2 &&
        issue.color == 0 && issue.available == 4 && issue.required == 5,
        "spinner inventory failure identifies a failing phase instead of claiming every phase lacks red");
    board.flags[0] = PHOrbFlagLocked;
    Check(PHOrbInspectInventory(board,options.goals).kind == PHOrbInventoryKind::None,
        "locking the fifth red removes the phase-dependent inventory rejection");
    board.width = 0;
    Check(PHOrbInspectInventory(board,options.goals).kind == PHOrbInventoryKind::Invalid,
        "unreadable input is not reported as a known orb shortage");
}

static void FishingInventoryTests() {
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    auto board = ShapeBoard(6,5,{});
    // Anonymous final_board: 29 fixed orbs are [3,2,1,10,2,11]. The spinner
    // remains variable at release; the requested first-wave nine is unchanged.
    const uint8_t types[] = {1,3,3,5,5,3, 3,3,5,0,3,5, 4,5,0,3,3,3,
                             5,3,0,5,3,5, 4,1,5,2,5,5};
    std::copy(std::begin(types), std::end(types), board.types);
    board.spinnerMask = UINT64_C(1) << 6; board.spinnerColors = 63;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.diagonal = true; options.goals.count = 3; options.goals.comboFirstWave = true;
    options.goals.items[0] = {PHOrbGoalT,8,1,0};
    options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    options.goals.items[2] = {PHOrbGoalCombo,0,9,0};
    const auto ordinary = PHOrbInspectInventory(board, options);
    Check(ordinary.kind == PHOrbInventoryKind::GoalCapacity && ordinary.goal == 2 &&
        ordinary.required == 9 && ordinary.available == 7,
        "final board ordinary rejection is its nine-Combo requirement, not a missing T or heart four");
    options.fishCell = 6;
    const unsigned expectedUpper[] = {7,8,7,7,8,8};
    for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
        options.fishColor = color;
        const auto issue = PHOrbInspectInventory(board, options);
        Check(issue.kind == PHOrbInventoryKind::GoalCapacity && issue.goal == 2 && issue.required == 9 &&
            issue.available == expectedUpper[color] && issue.variants == 6 &&
            PHOrbSolve(board, options, cancel, route) == PHOrbSolveStatus::InsufficientOrbs &&
            !route.count && !route.evaluated && !route.searchPasses,
            "every one-take color on final board stays below nine without entering beam search");
    }
    auto plan = PHOrbPlanFishing(board, options);
    Check(plan.valid && plan.count == 6 && !plan.selectedCount && options.goals.items[2].count == 9,
        "preflight records all six colors and never weakens final board's nine-Combo requirement");
    unsigned colors = 0;
    for (unsigned i = 0; i < plan.count; ++i) {
        const auto &candidate = plan.candidates[i]; colors |= 1u << candidate.color;
        Check(candidate.comboUpper == expectedUpper[candidate.color] &&
            candidate.inventory.kind == PHOrbInventoryKind::GoalCapacity,
            "color diagnostics retain each all-phase capacity instead of reporting a search attempt");
    }
    Check(colors == 63, "preflight includes off-shape water and dark, as well as light and heart");

    // Separate fixtures at eight exercise selection only, not a claim that
    // the recorded nine is solvable or that the shape layouts fit.
    for (bool shapes : {false, true}) {
        auto trial = options;
        if (shapes) trial.goals.items[2].count = 8;
        else { trial.goals = Goals(PHOrbGoalCombo,0,8); trial.goals.comboFirstWave = true; }
        plan = PHOrbPlanFishing(board, trial);
        Check(plan.valid && plan.count == 6 && plan.selectedCount == 2 &&
            plan.candidates[0].color == 1 && plan.candidates[1].color == 4 &&
            plan.candidates[0].inventory.kind == PHOrbInventoryKind::None &&
            plan.candidates[1].inventory.kind == PHOrbInventoryKind::None,
            "pure Combo and mixed shape/Combo goals rank water/dark completions before the two-color cap");
    }
    options.goals.items[2].count = 8;
    board.spinnerColors = 40; // light/heart only: do not invent water/dark access.
    plan = PHOrbPlanFishing(board, options);
    Check(plan.valid && plan.count == 2 && plan.selectedCount == 1 && plan.candidates[0].color == 5,
        "a restricted spinner table allows only its own colors even when another color would help Combo");
    options.fishColor = 1;
    Check(PHOrbInspectInventory(board, options).kind == PHOrbInventoryKind::Invalid,
        "fishing diagnostics validate the requested color just like search");
    options.fishCell = 42;
    Check(!PHOrbPlanFishing(board, options).valid &&
        PHOrbInspectInventory(board, options).kind == PHOrbInventoryKind::Invalid,
        "invalid fish cells are rejected before bit shifts or array access");
    options.fishCell = 6; options.fishColor = 3; board.flags[6] = PHOrbFlagLocked;
    Check(!PHOrbPlanFishing(board, options).valid, "a locked spinner is not a fishing candidate");
    board.flags[6] = 0; board.spinnerMask |= UINT64_C(1) << 7;
    Check(!PHOrbPlanFishing(board, options).valid, "preflight retains the single unlocked spinner restriction");
    board.flags[7] = PHOrbFlagLocked;
    Check(PHOrbPlanFishing(board, options).valid, "other locked spinners stay fixed and do not forbid the single unlocked target");

    // Fixed counts [3,3,3,3,3,14]. The former '+1' bound counted 31 orbs
    // and admitted ten Combo. For each phase a different sacrifice could
    // preserve ten, but one fixed sacrifice cannot do so for all phases.
    board = ShapeBoard(6,5,{}); board.spinnerMask = UINT64_C(1) << 29; board.spinnerColors = 63;
    for (unsigned i = 0; i < 30; ++i) board.types[i] = uint8_t(i < 15 ? i / 3 : 5);
    options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.goals = Goals(PHOrbGoalCombo,0,10); options.goals.comboFirstWave = true;
    options.fishCell = 29; options.fishColor = 5;
    auto issue = PHOrbInspectInventory(board, options);
    Check(issue.kind == PHOrbInventoryKind::GoalCapacity && issue.required == 10 && issue.available == 9 &&
        PHOrbSolve(board, options, cancel, route) == PHOrbSolveStatus::InsufficientOrbs && !route.evaluated,
        "one exchange neither creates a thirty-first orb nor changes its sacrificed color between phases");

    // Taking fire would remove an essential water; no-take already has a
    // water match. Fishing options must still admit the ordinary route.
    for (unsigned i = 0; i < 30; ++i) board.types[i] = i < 3 ? 1 : PHOrbJammer;
    board.types[29] = 0; board.spinnerColors = 3;
    options.goals = Goals(PHOrbGoalMatch,2,1,3); options.fishColor = 0;
    Check(PHOrbInspectInventory(board, options).kind == PHOrbInventoryKind::None,
        "optional fishing does not reject an ordinary feasible inventory when every take loses a required orb");
    board.types[0] = board.types[1] = 0; board.types[2] = board.types[3] = board.types[4] = 1;
    options.goals = Goals(PHOrbGoalMatch,1,1,3);
    options.goals.items[options.goals.count++] = {PHOrbGoalMatch,2,1,3};
    Check(PHOrbInspectInventory(board, options).kind == PHOrbInventoryKind::FishingExchange &&
        PHOrbSolve(board, options, cancel, route) == PHOrbSolveStatus::InsufficientOrbs && !route.count,
        "a sacrifice that repairs red but breaks water cannot satisfy the two goals with a single exchange");
}
