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
    PHOrbEvaluation result{}; unsigned sixCases = 0, checked = 0;
    for (auto size : {std::pair<unsigned,unsigned>{6,5}, {7,6}, {5,4}}) {
        const unsigned w = size.first, h = size.second;
        for (unsigned rotation = 0; rotation < 4; ++rotation)
            for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x) {
                std::vector<unsigned> cells;
                for (auto local : {std::pair<unsigned,unsigned>{0,0}, {1,0}, {2,0}, {1,1}, {1,2}}) {
                    unsigned px = local.first, py = local.second;
                    for (unsigned r = 0; r < rotation; ++r) { unsigned old = px; px = 2 - py; py = old; }
                    cells.push_back((y + py) * w + x + px);
                }
                auto board = ShapeBoard(w,h,cells);
                Check(FirstWaveGroupSize(board,cells[0]) == 5 && PHOrbEvaluate(board,Goals(PHOrbGoalT),result) && result.goalsMet,
                    "every translated and rotated isolated T has exactly five actual first-wave removals");
                for (unsigned extra = 0; extra < w * h; ++extra) {
                    if (std::find(cells.begin(),cells.end(),extra) != cells.end()) continue;
                    bool adjacent = false;
                    for (unsigned cell : cells) adjacent |= (cell / w == extra / w && (cell + 1 == extra || extra + 1 == cell)) ||
                        cell + w == extra || extra + w == cell;
                    if (!adjacent) continue;
                    auto merged = board; merged.types[extra] = 0;
                    unsigned removed = FirstWaveGroupSize(merged,cells[0]);
                    Check(PHOrbEvaluate(merged,Goals(PHOrbGoalT),result) && !result.goalsMet,
                        "an extra same-color T neighbour never authorizes the conservative exact-five goal");
                    sixCases += removed == 6; ++checked;
                    auto spinner = board; spinner.spinnerMask = UINT64_C(1) << extra; spinner.spinnerColors = 63;
                    for (unsigned color = 0; color < 6; ++color) {
                        spinner.types[extra] = uint8_t(color);
                        Check(PHOrbEvaluate(spinner,Goals(PHOrbGoalT),result) && !result.goalsMet,
                            "all sampled spinner phases reject a possible sixth first-wave orb");
                    }
                    spinner.flags[extra] = PHOrbFlagLocked; spinner.types[extra] = 1;
                    Check(PHOrbEvaluate(spinner,Goals(PHOrbGoalT),result) && result.goalsMet,
                        "a locked nonmatching border spinner cannot add a sixth T orb");
                }
            }
    }
    Check(sixCases > 100 && checked > sixCases, "boundary coverage includes both marked sixth orbs and unmarked neighbours");
    std::printf("T boundary fixtures: %u adjacent layouts, %u actual six-orb first-wave groups\n",checked,sixCases);
}
