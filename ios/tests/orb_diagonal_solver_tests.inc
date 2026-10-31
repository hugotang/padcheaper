static void DiagonalSolverTests() {
    std::atomic_bool cancel{false};
    for (auto dimensions : {std::pair<unsigned, unsigned>{6,5}, {7,6}, {5,4}}) {
        const unsigned w = dimensions.first, h = dimensions.second, n = w * h;
        for (unsigned a = 0; a < n; ++a) for (unsigned b = 0; b < n; ++b) {
            const int dx = int(a % w) - int(b % w), dy = int(a / w) - int(b / w);
            const bool eight = std::abs(dx) <= 1 && std::abs(dy) <= 1 && (dx || dy);
            Check(PHOrbMoveAdjacent(a,b,w,h,true) == eight &&
                PHOrbMoveAdjacent(a,b,w,h,false) == (std::abs(dx) + std::abs(dy) == 1),
                "every neighbor pair follows independent coordinate bounds without wrapping rows");
        }
        Check(!PHOrbMoveAdjacent(0,n,w,h,true) && !PHOrbMoveAdjacent(0,0,w,h,true) &&
            !PHOrbMoveAdjacent(0,1,0,h,true), "diagonal moves reject out-of-board, empty geometry and self swaps");
        auto board = ShapeBoard(w,h,{});
        const unsigned a = n - w - 2, b = n - 1;
        TapeExcept(board,{a,b}); board.fixedStart = uint8_t(a + 1);
        PHOrbRoute route{};
        PHOrbSolveOptions options; options.steps = 1; options.beam = 16; options.milliseconds = 100;
        Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::NoRoute && !route.count,
            "default movement cannot traverse a pair isolated by orthogonal tape");
        options.diagonal = true;
        Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Solved && route.diagonal &&
            route.count == 2 && route.cells[0] == a && route.cells[1] == b,
            "diagonal movement reaches the only legal neighbor with its fixed start and captured policy");
        PHOrbBoard replay{};
        Check(PHOrbReplay(&board,&route,1,&replay) && replay.tokens[b] == board.tokens[a] &&
            replay.tokens[a] == board.tokens[b], "a diagonal step swaps the endpoints directly");
        const auto score = Oracle(replay);
        Check(score.first == route.combos && score.second == route.erased,
            "diagonal routes still use the independent horizontal/vertical run and component oracle");
        PHOrbEvaluation result{}; uint32_t damage = 0; uint8_t contacts = 0;
        PHOrbSolveOptions oldPolicy = options; oldPolicy.diagonal = false;
        Check(!PHOrbEvaluateRoute(board,route,oldPolicy,result,damage,contacts),
            "a diagonal route cannot be revalidated under an orthogonal-only policy");
        cancel = true;
        Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Cancelled && !route.count,
            "eight-way search retains cancellation before publishing a route");
        cancel = false;
    }
    auto cornerGroups = ShapeBoard(6,5,{0,1,2,9,10,11});
    PHOrbRoute oneDiagonal{}; oneDiagonal.count = 2; oneDiagonal.cells[0] = 24; oneDiagonal.cells[1] = 19;
    oneDiagonal.diagonal = true;
    PHOrbSolveOptions options; options.diagonal = true;
    PHOrbEvaluation result{}; uint32_t damage = 0; uint8_t contacts = 0;
    Check(PHOrbEvaluateRoute(cornerGroups,oneDiagonal,options,result,damage,contacts) && result.combos == 2 && result.erased == 6,
        "two same-color matches touching only at a corner remain separate combos");
    auto hearts = ShapeBoard(6,5,{});
    for (unsigned i = 0; i < 4; ++i) hearts.types[i] = 5;
    hearts.flags[19] = PHOrbFlagThorn; hearts.thornDamage[19] = 4;
    options.thorns = {1000,1000,0,true};
    Check(PHOrbEvaluateRoute(hearts,oneDiagonal,options,result,damage,contacts) && result.hazardsSafe &&
        contacts == 1 && damage == 40 && result.heartFours == 1,
        "diagonal entry counts the destination thorn token against live HP and first-wave hearts");
    PHOrbRoute repeat = oneDiagonal; repeat.count = 4; repeat.cells[2] = 24; repeat.cells[3] = 19;
    Check(PHOrbEvaluateRoute(hearts,repeat,options,result,damage,contacts) && contacts == 3 && damage == 120,
        "revisiting a moving thorn token charges every diagonal contact");
    options.thorns.hp = 120;
    Check(PHOrbEvaluateRoute(hearts,repeat,options,result,damage,contacts) && !result.hazardsSafe,
        "diagonal policy never spends future heart healing or permits damage equal to current HP");
}
