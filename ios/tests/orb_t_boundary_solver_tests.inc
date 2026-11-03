// Independent first-wave union oracle. Geometry is constructed from a stem
// and crossbar, not the production bit-pattern constants.
static unsigned FirstWaveGroupSize(const PHOrbBoard &board, unsigned at) {
    const unsigned w = board.width, h = board.height, n = w * h;
    std::vector<bool> marked(n); std::vector<unsigned> parent(n);
    std::iota(parent.begin(), parent.end(), 0);
    for (unsigned axis = 0; axis < 2; ++axis) {
        const unsigned lines = axis ? w : h, length = axis ? h : w;
        for (unsigned line = 0; line < lines; ++line) {
            auto cell = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
            for (unsigned start = 0; start < length;) {
                unsigned end = start + 1;
                while (end < length && board.types[cell(start)] == board.types[cell(end)]) ++end;
                if (end - start >= 3) for (unsigned p = start; p < end; ++p) marked[cell(p)] = true;
                start = end;
            }
        }
    }
    auto root = [&](unsigned c) { while (parent[c] != c) c = parent[c]; return c; };
    for (unsigned a = 0; a < n; ++a) for (unsigned b = a + 1; b < n; ++b)
        if (marked[a] && marked[b] && board.types[a] == board.types[b] &&
            ((a / w == b / w && b == a + 1) || b == a + w)) parent[root(b)] = root(a);
    if (!marked[at]) return 0;
    unsigned size = 0;
    for (unsigned i = 0; i < n; ++i) size += marked[i] && root(i) == root(at);
    return size;
}

static void TBoundarySolverTests() {
    // PAD 23.6.0 0x1005F7954 judges every exact shape on the actual removed
    // group, so each shape is built from coordinates and checked against the
    // independent first-wave oracle with every adjacent same-color extra.
    // Any same-color orb beside a 3x3 extends one of its rows/columns, so the
    // square has no unmarked-neighbour case.
    struct Shape { PHOrbGoalKind kind; std::vector<std::pair<unsigned,unsigned>> cells; };
    const std::vector<Shape> shapes{
        {PHOrbGoalT, {{0,0},{1,0},{2,0},{1,1},{1,2}}},
        {PHOrbGoalL, {{0,0},{0,1},{0,2},{1,2},{2,2}}},
        {PHOrbGoalCross, {{1,0},{0,1},{1,1},{2,1},{1,2}}},
        {PHOrbGoalSquare, {{0,0},{1,0},{2,0},{0,1},{1,1},{2,1},{0,2},{1,2},{2,2}}}};
    PHOrbEvaluation result{};
    for (const auto &shape : shapes) {
        const unsigned size = unsigned(shape.cells.size());
        unsigned largerCases = 0, unmarked = 0, checked = 0;
        for (auto dimensions : {std::pair<unsigned,unsigned>{6,5}, {7,6}, {5,4}}) {
            const unsigned w = dimensions.first, h = dimensions.second;
            for (unsigned rotation = 0; rotation < 4; ++rotation)
                for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x) {
                    std::vector<unsigned> cells;
                    for (auto local : shape.cells) {
                        unsigned px = local.first, py = local.second;
                        for (unsigned r = 0; r < rotation; ++r) { unsigned old = px; px = 2 - py; py = old; }
                        cells.push_back((y + py) * w + x + px);
                    }
                    auto board = ShapeBoard(w,h,cells);
                    Check(FirstWaveGroupSize(board,cells[0]) == size && PHOrbEvaluate(board,Goals(shape.kind),result) &&
                        result.goalsMet, "every translated and rotated isolated exact shape is recognized");
                    for (unsigned extra = 0; extra < w * h; ++extra) {
                        if (std::find(cells.begin(),cells.end(),extra) != cells.end()) continue;
                        bool adjacent = false;
                        for (unsigned cell : cells) adjacent |= (cell / w == extra / w && (cell + 1 == extra || extra + 1 == cell)) ||
                            cell + w == extra || extra + w == cell;
                        if (!adjacent) continue;
                        auto merged = board; merged.types[extra] = 0;
                        const unsigned removed = FirstWaveGroupSize(merged,cells[0]);
                        Check(PHOrbEvaluate(merged,Goals(shape.kind),result) && result.goalsMet == (removed == size),
                            "a same-color neighbour voids an exact shape only when it is actually removed with it");
                        largerCases += removed > size; unmarked += removed == size; ++checked;
                        auto spinner = board; spinner.spinnerMask = UINT64_C(1) << extra; spinner.spinnerColors = 63;
                        spinner.types[extra] = 0;
                        Check(PHOrbEvaluate(spinner,Goals(shape.kind),result) && result.goalsMet == (removed == size),
                            "a border spinner voids an exact shape only when one phase joins the removed group");
                        spinner.flags[extra] = PHOrbFlagLocked; spinner.types[extra] = 1;
                        Check(PHOrbEvaluate(spinner,Goals(shape.kind),result) && result.goalsMet,
                            "a locked nonmatching border spinner cannot join an exact shape");
                    }
                }
        }
        Check(largerCases > 0 && (unmarked > 0 || shape.kind == PHOrbGoalSquare) && checked == largerCases + unmarked,
            "boundary coverage includes both joined and accepted unmarked neighbours for every exact shape");
        std::printf("exact shape %u boundary fixtures: %u adjacent layouts, %u joined, %u unmarked\n",
            unsigned(shape.kind),checked,largerCases,unmarked);
    }
}
