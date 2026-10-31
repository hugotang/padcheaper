static void AssistSolverTests() {
    PHOrbGoalSet goals = Goals(PHOrbGoalT, 1);
    goals.items[goals.count++] = {PHOrbGoalL, 1, 1, 0};
    PHOrbSolveOptions options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast); options.goals = goals;
    std::atomic_bool cancel{false}; PHOrbRoute route{}; PHOrbEvaluation evaluation{};
    auto shortBoard = ShapeBoard(6,5,{0,1,2,6,7,8,12,13,14});
    Check(PHOrbRequiredShapeOrbs(&goals,0) == 10 && !PHOrbShapeInventoryAvailable(&shortBoard,&goals) &&
        PHOrbSolve(shortBoard,options,cancel,route) == PHOrbSolveStatus::InsufficientOrbs && !route.evaluated,
        "T and L of one color need ten distinct orbs before search can start");
    // Coordinate witness: T at the upper-left, L with its corner at bottom-right.
    auto witness = ShapeBoard(6,5,{0,1,2,7,13,17,23,27,28,29});
    Check(PHOrbEvaluate(witness,goals,evaluation) && evaluation.goalsMet &&
        evaluation.goalHits[0] == 1 && evaluation.goalHits[1] == 1,
        "ten-orb T/L witness has two independent first-wave matches");
    TapeExcept(witness,{3,4}); witness.fixedStart = 4; options.steps = 1;
    Check(PHOrbSolve(witness,options,cancel,route) == PHOrbSolveStatus::Solved && route.goalsMet,
        "a legal separated T/L layout is not rejected by the new geometric bound");
    auto dense = ShapeBoard(6,5,{}); std::fill(dense.types,dense.types + 30,0); dense.types[0] = 1;
    Check(PHOrbSolve(dense,options,cancel,route) == PHOrbSolveStatus::IncompatibleGoals && !route.evaluated,
        "one separator cannot split two same-color exact shapes regardless of route");
    for (auto dimensions : {std::pair<unsigned,unsigned>{5,4},{6,5},{7,6}}) {
        const unsigned w = dimensions.first, h = dimensions.second;
        for (unsigned rotation = 0; rotation < 4; ++rotation) for (unsigned side = 0; side < 4; ++side) {
            const unsigned x = side & 1 ? w - 3 : 0, y = side & 2 ? h - 3 : 0;
            std::vector<unsigned> cells;
            for (auto point : {std::pair<unsigned,unsigned>{0,0},{0,1},{0,2},{1,2},{2,2}}) {
                unsigned px = point.first, py = point.second;
                for (unsigned r = 0; r < rotation; ++r) { unsigned old = px; px = 2 - py; py = old; }
                cells.push_back((y + py) * w + x + px);
            }
            Check(PHOrbEvaluate(ShapeBoard(w,h,cells),Goals(PHOrbGoalL),evaluation) && evaluation.goalsMet,
                "all four L directions remain recognized at every fitting board corner");
        }
    }
    auto two = ShapeBoard(6,5,{0,6,12,13,14,17,23,27,28,29});
    Check(PHOrbEvaluate(two,Goals(PHOrbGoalL,1,2),evaluation) && evaluation.goalsMet && evaluation.goalHits[0] == 2,
        "two separated L groups are counted twice rather than as one color-presence flag");
    witness = ShapeBoard(6,5,{0,1,2,7,13,17,23,27,28,29});
    witness.spinnerMask = UINT64_C(1) << 29; witness.spinnerColors = 63; options.steps = 1;
    Check(PHOrbSolve(witness,options,cancel,route) == PHOrbSolveStatus::InsufficientOrbs,
        "the joint inventory bound applies to every shared spinner phase");
    auto overlapping = Goals(PHOrbGoalMatch,1,1,5);
    overlapping.items[overlapping.count++] = {PHOrbGoalL,1,1,0};
    Check(PHOrbRequiredShapeOrbs(&overlapping,0) == 5,
        "a generic match may reuse an L and is not charged twice by the exact-shape bound");
}
