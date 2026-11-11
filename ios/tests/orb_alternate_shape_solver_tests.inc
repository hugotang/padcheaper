// Known-solution failures from the shape simulation dataset. Witness paths
// stay in the research JSON and are never used as solver seeds.
static unsigned AlternateShapeOracle(const PHOrbBoard &board, const PHOrbGoal &goal) {
    const unsigned w = board.width, h = board.height;
    using Point = std::pair<unsigned,unsigned>;
    std::vector<Point> shape;
    if (goal.kind == PHOrbGoalL) shape = {{0,0},{0,1},{0,2},{1,2},{2,2}};
    if (goal.kind == PHOrbGoalT) shape = {{0,0},{1,0},{2,0},{1,1},{1,2}};
    if (goal.kind == PHOrbGoalCross) shape = {{1,0},{0,1},{1,1},{2,1},{1,2}};
    if (goal.kind == PHOrbGoalSquare) for (unsigned y = 0; y < 3; ++y)
        for (unsigned x = 0; x < 3; ++x) shape.push_back({x,y});
    if (shape.empty()) return 0;
    std::set<std::vector<unsigned>> counted;
    for (unsigned color = 0; color < 6; ++color) if (goal.colors & (1u << color))
        for (unsigned rotation = 0; rotation < 4; ++rotation)
            for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x) {
                std::vector<unsigned> cells;
                bool same = true;
                for (auto point : shape) {
                    for (unsigned r = 0; r < rotation; ++r) point = {2-point.second,point.first};
                    const unsigned cell = (y+point.second)*w+x+point.first;
                    cells.push_back(cell); same &= board.types[cell] == color;
                }
                if (!same || FirstWaveGroupSize(board,cells[0]) != cells.size()) continue;
                std::sort(cells.begin(),cells.end()); counted.insert(cells);
            }
    return unsigned(counted.size());
}

static void AlternateShapeSolverTests() {
    struct Fixture { const char *name; unsigned w,h; const char *cells; PHOrbGoalSet goals; };
    const Fixture fixtures[] = {
        {"Square-03-v0",6,5,"204500025212424005053001024200",{1, {{PHOrbGoalSquare,1,1,0}},false,false}},
        {"Square-03-v1",6,5,"002420100350500424212520005402",{1, {{PHOrbGoalSquare,1,1,0}},false,false}},
        {"Square-03-v2",6,5,"420122241434040221215223240422",{1, {{PHOrbGoalSquare,4,1,0}},false,false}},
        {"DoubleT-09-v1",7,6,"531251330001450002212122151333150041214001",{1, {{PHOrbGoalT,1,2,0}},false,false}},
        {"LCross-10-v0",7,6,"003341401044330253131111115431235431431055",{2, {{PHOrbGoalL,1,1,0},{PHOrbGoalCross,32,1,0}},false,false}},
        {"LCross-10-v1",7,6,"550134134532134511111131352033440104143300",{2, {{PHOrbGoalL,1,1,0},{PHOrbGoalCross,32,1,0}},false,false}},
        {"LCross-10-v2",7,6,"225503023200552415353333331053451053053211",{2, {{PHOrbGoalL,4,1,0},{PHOrbGoalCross,2,1,0}},false,false}},
    };
    std::atomic_bool cancel{false};
    for (const auto &fixture : fixtures) {
        auto board = Board(fixture.w,fixture.h,1);
        for (unsigned i = 0; i < fixture.w*fixture.h; ++i) board.types[i] = uint8_t(fixture.cells[i]-'0');
        const auto original = board;
        auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
        options.goals = fixture.goals; options.beam = 128; options.milliseconds = 1000;
        PHOrbRoute route{}; PHOrbBoard replay{};
        const auto status = PHOrbSolve(board,options,cancel,route);
        Check(status == PHOrbSolveStatus::Solved && route.goalsMet && route.hazardsSafe,
            "alternate shape recovery solves the recorded narrow-beam miss");
        Check(std::memcmp(&board,&original,sizeof(board)) == 0 && route.count <= options.steps+1 &&
            route.backtracks <= options.backtrackLimit && route.squareRecoveryPasses <= 2 &&
            route.searchPasses <= 1+options.recoveryPasses+route.backtracks+route.squareRecoveryPasses &&
            route.exploredDepth <= options.steps && !route.diagonal,
            "alternate shape recovery preserves source, movement and search bounds");
        if (route.count >= 2 && PHOrbReplay(&board,&route,route.count-1,&replay)) {
            const auto score = Oracle(replay);
            Check(score.first == route.combos && score.second == route.erased,
                "alternate shape route agrees with the independent cascade oracle");
            for (unsigned g = 0; g < options.goals.count; ++g) {
                const unsigned hits = AlternateShapeOracle(replay,options.goals.items[g]);
                Check(hits == route.goalHits[g] && hits >= options.goals.items[g].count,
                    "alternate guidance never substitutes for independently verified first-wave shapes");
            }
        } else Check(false,"alternate shape route must replay legally");
        std::printf("Alternate shape %s: status=%u passes=%u backtracks=%u elapsed=%u ms\n",
            fixture.name,unsigned(status),route.searchPasses,route.backtracks,route.elapsedMS);
    }
    auto board = Board(fixtures[0].w,fixtures[0].h,1);
    for (unsigned i = 0; i < unsigned(board.width)*board.height; ++i) board.types[i] = uint8_t(fixtures[0].cells[i]-'0');
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = fixtures[0].goals; options.beam = 128; options.milliseconds = 1;
    PHOrbRoute route{};
    const auto start = std::chrono::steady_clock::now();
    const auto status = PHOrbSolve(board,options,cancel,route);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    Check(elapsed < 500 && route.count <= options.steps+1 && route.squareRecoveryPasses <= 2 &&
        route.searchPasses <= 4+route.squareRecoveryPasses &&
        (status == PHOrbSolveStatus::NoRoute || status == PHOrbSolveStatus::GoalsUnmet || status == PHOrbSolveStatus::Solved),
        "alternate shape scoring respects a short deadline and retains bounded output");
    cancel = true;
    Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Cancelled && !route.count,
        "cancelled alternate shape search returns no executable route");
}
