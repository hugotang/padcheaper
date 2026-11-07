// Construct known isolated groups from coordinates, without production shape
// masks or a copy of its matching algorithm. This matrix complements the
// exhaustive neighbour/translation cases in TBoundarySolverTests.
static PHOrbBoard NativeRuleBoard(unsigned width, unsigned height, unsigned color,
    const std::vector<std::pair<unsigned,unsigned>> &positions) {
    auto board = Board(width,height,1);
    std::vector<unsigned> background;
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) if (c != color) background.push_back(c);
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
        board.types[y * width + x] = uint8_t(background[(x + 2 * y) % background.size()]);
    for (auto cell : positions) board.types[cell.second * width + cell.first] = uint8_t(color);
    return board;
}

static void NativeRuleMatrixTests() {
    struct Fixture {
        PHOrbGoalKind kind;
        unsigned longestRun, rotations;
        std::vector<std::pair<unsigned,unsigned>> cells;
    };
    PHOrbEvaluation result{};
    for (auto dimensions : {std::pair<unsigned,unsigned>{5,4},{6,5},{7,6}}) {
        const unsigned w = dimensions.first, h = dimensions.second;
        std::vector<Fixture> fixtures{
            {PHOrbGoalL,3,4,{{0,0},{0,1},{0,2},{1,2},{2,2}}},
            {PHOrbGoalT,3,4,{{0,0},{1,0},{2,0},{1,1},{1,2}}},
            {PHOrbGoalCross,3,1,{{1,0},{0,1},{1,1},{2,1},{1,2}}},
            {PHOrbGoalSquare,3,1,{{0,0},{1,0},{2,0},{0,1},{1,1},{2,1},{0,2},{1,2},{2,2}}},
            {PHOrbGoalFour,4,1,{{0,0},{1,0},{2,0},{3,0}}},
            {PHOrbGoalFour,4,1,{{0,0},{0,1},{0,2},{0,3}}},
            {PHOrbGoalRow,w,1,{}}, {PHOrbGoalColumn,h,1,{}}};
        for (unsigned x = 0; x < w; ++x) fixtures[6].cells.emplace_back(x,0);
        for (unsigned y = 0; y < h; ++y) fixtures[7].cells.emplace_back(0,y);
        for (const auto &fixture : fixtures) for (unsigned rotation = 0; rotation < fixture.rotations; ++rotation) {
            auto positions = fixture.cells;
            for (auto &cell : positions) for (unsigned r = 0; r < rotation; ++r) {
                const unsigned x = cell.first; cell.first = 2 - cell.second; cell.second = x;
            }
            const unsigned size = unsigned(positions.size());
            for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
                const auto initial = NativeRuleBoard(w,h,color,positions);
                auto goals = Goals(fixture.kind,1u << color);
                goals.items[goals.count++] = {PHOrbGoalExact,uint8_t(1u << color),1,uint8_t(size)};
                goals.items[goals.count++] = {PHOrbGoalMatch,uint8_t(1u << color),1,uint8_t(size)};
                goals.items[goals.count++] = {PHOrbGoalColors,uint8_t(1u << color),1,0};
                // In each constructed group every cell lies on its longest
                // run. No other orb has this colour, even after gravity.
                for (unsigned minimumRun : {0u,3u,4u,5u,6u,7u})
                    for (unsigned minimumGroup : {0u,size,size + 1}) {
                        auto board = initial;
                        board.minErase = uint8_t(minimumRun); board.minMatch = uint8_t(minimumGroup);
                        const bool removable = (minimumRun ? minimumRun : 3) <= fixture.longestRun && minimumGroup <= size;
                        Check(PHOrbEvaluate(board,goals,result) && result.goalsMet == removable,
                            "all native shape families obey run/group minima on every board size and ordinary colour");
                        for (unsigned g = 0; g < goals.count; ++g) Check(result.goalHits[g] == unsigned(removable),
                            "shape, exact size, minimum size and colour presence agree for a constructed isolated group");
                        Check(result.heartFours == unsigned(removable && color == 5 && size == 4 && fixture.longestRun == 4),
                            "only a removable first-wave straight heart four supplies heart-four recovery evidence");
                        // Native-awakening mode has the same result for an
                        // isolated full column, since no side branch exists.
                        if (fixture.kind == PHOrbGoalColumn) {
                            auto bonus = goals; bonus.bonusFirstWave = true;
                            Check(PHOrbEvaluate(board,bonus,result) && result.goalsMet == removable,
                                "an isolated full column also satisfies the stricter native bonus condition");
                        }
                    }
                auto unmatchable = initial; unmatchable.unmatchableColors = uint16_t(1u << color);
                Check(PHOrbEvaluate(unmatchable,goals,result) && !result.goalsMet && result.matchedGoals == 0,
                    "unmatchable colours disable every constructed shape without changing its layout");
                auto shortGroup = goals; shortGroup.items[1].length = uint8_t(size + 1);
                Check(PHOrbEvaluate(initial,shortGroup,result) && !result.goalsMet && !result.goalHits[1] &&
                    result.goalHits[0] == 1 && result.goalHits[2] == 1 && result.goalHits[3] == 1,
                    "an exact-length clause remains independent of the same group's shape and minimum-length clauses");
            }
        }
        // PAD's native column flag requires a one-column removed group. The
        // user-facing ordinary Column goal intentionally means a full column
        // within a group; bonusFirstWave requests the native stricter rule.
        for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
            std::vector<std::pair<unsigned,unsigned>> column, row;
            for (unsigned y = 0; y < h; ++y) column.emplace_back(0,y);
            column.emplace_back(1,2); column.emplace_back(2,2);
            auto board = NativeRuleBoard(w,h,color,column);
            auto goals = Goals(PHOrbGoalColumn,1u << color);
            Check(PHOrbEvaluate(board,goals,result) && result.goalHits[0] == 1,
                "a full column with a marked side branch remains one ordinary column group");
            goals.bonusFirstWave = true;
            Check(PHOrbEvaluate(board,goals,result) && !result.goalHits[0],
                "a marked side branch prevents the strict native column bonus");
            for (unsigned x = 0; x < w; ++x) row.emplace_back(x,0);
            row.emplace_back(2,1); row.emplace_back(2,2);
            board = NativeRuleBoard(w,h,color,row); goals = Goals(PHOrbGoalRow,1u << color);
            goals.bonusFirstWave = true;
            Check(PHOrbEvaluate(board,goals,result) && result.goalHits[0] == 1,
                "native full-row recognition still accepts a removed group with a marked branch");
        }
    }
}
