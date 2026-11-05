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
