// Written regressions, pending an explicitly authorized build/test run.
// The recorded fixture contains only anonymous orb types and a failed path.
// No successful path or board-specific seed is supplied to the solver.
struct ColorOracleResult {
    unsigned combos = 0, erased = 0, firstCombos = 0;
    unsigned colors = 0, firstColors = 0;
    std::vector<unsigned> waveColors;
};

static unsigned ColorCount(unsigned mask) {
    unsigned count = 0;
    while (mask) { count += mask & 1u; mask >>= 1; }
    return count;
}

// Independent ordinary-board replay: coordinate adjacency and endpoint swaps.
// Spinner fishing and thorn policies need their dedicated effect oracles.
static bool ColorReplay(const PHOrbBoard &board, const PHOrbRoute &route, PHOrbBoard &released) {
    const unsigned w = board.width, h = board.height, n = w * h;
    if (!w || !h || n > PHOrbMaxCells || route.count < 2 || route.count > PHOrbMaxSteps + 1 ||
        board.spinnerMask || route.fishing || route.cells[0] >= n ||
        (board.fixedStart && unsigned(route.cells[0]) + 1 != board.fixedStart)) return false;
    for (unsigned i = 0; i < n; ++i)
        if ((board.flags[i] & PHOrbFlagThorn) || board.thornDamage[i]) return false;
    released = board;
    for (unsigned step = 0; step < route.count; ++step) {
        const unsigned b = route.cells[step];
        if (b >= n || (board.tapeMask & (UINT64_C(1) << b))) return false;
        if (!step) continue;
        const unsigned a = route.cells[step - 1];
        const int dx = std::abs(int(a % w) - int(b % w));
        const int dy = std::abs(int(a / w) - int(b / w));
        if (route.diagonal ? (dx > 1 || dy > 1 || !(dx || dy)) : dx + dy != 1) return false;
        std::swap(released.types[a], released.types[b]);
        std::swap(released.tokens[a], released.tokens[b]);
    }
    return true;
}

// Whole-line scans, orthogonal components, minimum connected removal, then
// column reconstruction. No production evaluator, matcher or gravity helper.
static ColorOracleResult ColorOracle(const PHOrbBoard &board) {
    ColorOracleResult result;
    const unsigned w = board.width, h = board.height, n = w * h;
    const unsigned run = board.minErase ? board.minErase : 3;
    std::vector<int> cells(board.types, board.types + n);
    for (;;) {
        std::set<unsigned> marked;
        for (unsigned axis = 0; axis < 2; ++axis) {
            const unsigned lines = axis ? w : h, length = axis ? h : w;
            for (unsigned line = 0; line < lines; ++line) {
                auto at = [&](unsigned position) { return axis ? position * w + line : line * w + position; };
                for (unsigned begin = 0; begin < length;) {
                    unsigned end = begin + 1;
                    while (end < length && cells[at(begin)] == cells[at(end)]) ++end;
                    const int color = cells[at(begin)];
                    if (color >= 0 && !(board.unmatchableColors & (1u << color)) && end - begin >= run)
                        for (unsigned position = begin; position < end; ++position) marked.insert(at(position));
                    begin = end;
                }
            }
        }
        std::vector<unsigned> parent(n), sizes(n);
        std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](unsigned cell) { while (parent[cell] != cell) cell = parent[cell]; return cell; };
        for (unsigned a : marked) for (unsigned b : marked)
            if (a < b && cells[a] == cells[b] &&
                ((a / w == b / w && b == a + 1) || b == a + w)) parent[root(b)] = root(a);
        for (unsigned cell : marked) ++sizes[root(cell)];
        for (auto it = marked.begin(); it != marked.end();)
            if (sizes[root(*it)] < board.minMatch) it = marked.erase(it); else ++it;
        if (marked.empty()) return result;
        std::set<unsigned> groups;
        unsigned colors = 0;
        for (unsigned cell : marked) {
            groups.insert(root(cell)); colors |= 1u << cells[cell]; cells[cell] = -1;
        }
        if (result.waveColors.empty()) {
            result.firstCombos = unsigned(groups.size()); result.firstColors = colors;
        }
        result.waveColors.push_back(colors); result.colors |= colors;
        result.combos += unsigned(groups.size()); result.erased += unsigned(marked.size());
        for (unsigned x = 0; x < w; ++x) {
            std::vector<int> survivors;
            for (unsigned y = 0; y < h; ++y) if (cells[y * w + x] >= 0) survivors.push_back(cells[y * w + x]);
            survivors.insert(survivors.begin(), h - survivors.size(), -1);
            for (unsigned y = 0; y < h; ++y) cells[y * w + x] = survivors[y];
        }
    }
}

static PHOrbBoard RecordedColorBoard() {
    const uint8_t types[] = {4,3,4,1,0,1, 0,5,2,5,5,3, 2,3,3,3,2,0,
                             5,1,0,5,0,0, 1,0,0,4,2,1};
    PHOrbBoard board{}; board.width = 6; board.height = 5;
    std::copy(std::begin(types), std::end(types), board.types);
    for (unsigned i = 0; i < 30; ++i) board.tokens[i] = i + 1;
    return board;
}

static void CheckColorRoute(const PHOrbBoard &board, const PHOrbSolveOptions &options,
                           PHOrbSolveStatus status, const PHOrbRoute &route) {
    Check(route.count >= 2 && route.count <= options.steps + 1 && route.diagonal == options.diagonal,
        "color search retains a bounded gesture and its requested movement policy");
    PHOrbBoard replay{}, nativeReplay{};
    const bool replayed = ColorReplay(board, route, replay);
    Check(replayed, "color search obeys independent adjacency, tape and fixed-start checks");
    if (!replayed) return;
    const auto score = ColorOracle(replay);
    const unsigned colors = options.goals.bonusFirstWave ? score.firstColors : score.colors;
    unsigned matched = 0;
    for (unsigned g = 0; g < options.goals.count; ++g) {
        const unsigned hits = ColorCount(colors & options.goals.items[g].colors);
        Check(route.goalHits[g] == hits, "color route hits count distinct requested colors, including only authorized hearts");
        matched += hits >= options.goals.items[g].count;
    }
    Check(route.combos == score.combos && route.erased == score.erased &&
        route.goalCount == options.goals.count && route.matchedGoals == matched &&
        route.goalsMet == (matched == options.goals.count) && route.hazardsSafe &&
        (status == PHOrbSolveStatus::Solved) == route.goalsMet,
        "color route status and score agree with independent cascades and every original goal");
    PHOrbEvaluation evaluation{}; uint32_t damage = 0; uint8_t contacts = 0;
    Check(PHOrbReplay(&board, &route, route.count - 1, &nativeReplay) &&
        !std::memcmp(replay.types, nativeReplay.types, board.width * board.height) &&
        PHOrbEvaluateRoute(board, route, options, evaluation, damage, contacts) &&
        evaluation.goalsMet == route.goalsMet && evaluation.firstWaveCombos == score.firstCombos &&
        evaluation.combos == score.combos && evaluation.erased == score.erased && !damage && !contacts,
        "color search independently replays identically to the uncached release validation");
}

static void ColorSolverTests() {
    const auto recorded = RecordedColorBoard();
    const uint8_t previousPath[] = {11,4,9,16,21,28,27,20,15,8,1,2,3,9,15,21,22,28,21,15};
    PHOrbRoute previous{}; previous.diagonal = true; previous.count = sizeof(previousPath);
    std::copy(std::begin(previousPath), std::end(previousPath), previous.cells);
    PHOrbBoard released{};
    const bool replayed = ColorReplay(recorded, previous, released);
    Check(replayed, "anonymous recorded nineteen-move path has legal eight-way swaps");
    if (replayed) {
        const auto old = ColorOracle(released);
        Check(old.combos == 6 && old.erased == 21 && old.firstCombos == 4 && old.colors == 0x2f && old.firstColors == 0x29 &&
            old.waveColors == std::vector<unsigned>({0x29, 0x06}) &&
            ColorCount(old.colors & 31) == 4 && (31u & ~old.colors) == 16,
            "recorded six-combo path clears only four attack colors, with dark absent even after cascades");
        PHOrbEvaluation evaluation{};
        auto five = Goals(PHOrbGoalColors, 31, 5);
        Check(PHOrbEvaluate(released, five, evaluation) && !evaluation.goalsMet && evaluation.goalHits[0] == 4 &&
            evaluation.combos == old.combos && evaluation.firstWaveCombos == old.firstCombos,
            "heart removal cannot supply the fifth attack color in the recorded miss");
        for (const auto &selection : std::vector<std::pair<unsigned,unsigned>>{{15,4}, {48,1}, {48,2}, {63,5}, {63,6}}) {
            auto goals = Goals(PHOrbGoalColors, selection.first, selection.second);
            const unsigned hits = ColorCount(old.colors & selection.first);
            Check(PHOrbEvaluate(released, goals, evaluation) && evaluation.goalHits[0] == hits &&
                evaluation.goalsMet == (hits >= selection.second),
                "subset masks, partial counts and explicit heart inclusion use the requested color set");
        }
        auto cascade = Goals(PHOrbGoalColors, 15, 4);
        cascade.comboFirstWave = true;
        Check(PHOrbEvaluate(released, cascade, evaluation) && evaluation.goalsMet && evaluation.goalHits[0] == 4,
            "a first-wave combo restriction alone does not remove later-wave color matches");
        cascade.bonusFirstWave = true;
        Check(PHOrbEvaluate(released, cascade, evaluation) && !evaluation.goalsMet && evaluation.goalHits[0] == 2,
            "first-wave color triggers exclude the water and wood supplied by this cascade");
    }

    std::atomic_bool cancel{false}; PHOrbRoute route{};
    auto thorough = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    thorough.steps = 48; thorough.milliseconds = 800; thorough.diagonal = true;
    thorough.goals = Goals(PHOrbGoalColors, 31, 5);
    unsigned inventory[6]{};
    for (unsigned i = 0; i < 30; ++i) ++inventory[recorded.types[i]];
    Check(inventory[4] == 3 && PHOrbInspectInventory(recorded, thorough.goals).kind == PHOrbInventoryKind::None,
        "recorded scarce dark inventory passes only a necessary capacity check, not a route proof");
    // Acceptance targets, still pending execution. Relabeling attack colors
    // and reflecting positions prevent a dark-specific or coordinate-specific
    // recovery from satisfying only the single recorded case.
    for (unsigned variant = 0; variant < 3; ++variant) {
        auto board = recorded;
        for (unsigned i = 0; i < 30; ++i) {
            const unsigned source = variant == 2 ? (i / 6) * 6 + 5 - i % 6 : i;
            const unsigned color = recorded.types[source];
            board.types[i] = uint8_t(variant == 1 && color < 5 ? (color + 1) % 5 : color);
        }
        const auto original = board;
        const auto status = PHOrbSolve(board, thorough, cancel, route);
        Check(status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet ||
            (status == PHOrbSolveStatus::NoRoute && route.deadlineReached),
            "recorded color variants return an exact success or an explicitly incomplete search");
        if (!route.deadlineReached)
            Check(status == PHOrbSolveStatus::Solved, "thorough search recovers each anonymous five-color miss without deadline pressure");
        Check(!std::memcmp(&board, &original, sizeof(board)) && route.searchPasses <= 4 &&
            route.backtracks <= thorough.backtrackLimit && route.exploredDepth <= thorough.steps &&
            route.count <= thorough.steps + 1,
            "ordinary color recovery shares bounded passes, frontiers and depth without mutating its input");
        if (route.count) CheckColorRoute(board, thorough, status, route);
        else Check(status == PHOrbSolveStatus::NoRoute && route.deadlineReached,
            "a recorded-board success must contain a route for independent five-color replay");
        std::printf("Anonymous five-color variant %u: status=%u hits=%u/5 steps=%u elapsed=%u ms deadline=%u passes=%u backtracks=%u\n",
            variant, unsigned(status), route.goalHits[0], route.count ? route.count - 1 : 0,
            route.elapsedMS, route.deadlineReached, route.searchPasses, route.backtracks);
    }

    // Six vertical triples are unaffected by the only open bottom-row swap.
    // This hand-constructed board covers both movement policies and goals
    // needing fewer colors than their masks permit, without a search witness.
    auto six = recorded;
    for (unsigned i = 0; i < 30; ++i)
        six.types[i] = uint8_t(i < 18 ? i % 6 : (i % 6 + i / 6 - 2) % 6);
    TapeExcept(six, {24,25}); six.fixedStart = 25;
    for (bool diagonal : {false, true})
        for (const auto &selection : std::vector<std::pair<unsigned,unsigned>>{{1,1}, {3,2}, {21,2}, {48,1}, {31,5}, {63,6}}) {
            auto options = thorough; options.steps = 1; options.diagonal = diagonal;
            options.goals = Goals(PHOrbGoalColors, selection.first, selection.second);
            const auto status = PHOrbSolve(six, options, cancel, route);
            Check(status == PHOrbSolveStatus::Solved && route.count == 2 && route.cells[0] == 24 && route.cells[1] == 25,
                "one permitted fixed-start swap preserves requested subsets and all six colors including hearts");
            if (route.count) CheckColorRoute(six, options, status, route);
        }
    auto firstWave = thorough; firstWave.steps = 1; firstWave.goals = Goals(PHOrbGoalColors, 63, 6);
    firstWave.goals.bonusFirstWave = true;
    auto status = PHOrbSolve(six, firstWave, cancel, route);
    Check(status == PHOrbSolveStatus::Solved && route.count == 2,
        "six first-wave colors also satisfy explicit first-wave triggers");
    if (route.count) CheckColorRoute(six, firstWave, status, route);
    auto conjunction = firstWave; conjunction.goals.bonusFirstWave = false;
    conjunction.goals = Goals(PHOrbGoalColors, 31, 5);
    conjunction.goals.items[conjunction.goals.count++] = {PHOrbGoalColors, 48, 2, 0};
    status = PHOrbSolve(six, conjunction, cancel, route);
    Check(status == PHOrbSolveStatus::Solved && route.count == 2 && route.matchedGoals == 2,
        "separate color-mask goals remain a conjunction even when they share dark");
    if (route.count) CheckColorRoute(six, conjunction, status, route);

    auto diagonalOnly = ShapeBoard(6,5,{1,2,7});
    TapeExcept(diagonalOnly, {0,7}); diagonalOnly.fixedStart = 8;
    auto oneColor = thorough; oneColor.steps = 1; oneColor.diagonal = false;
    oneColor.goals = Goals(PHOrbGoalColors, 1, 1);
    Check(PHOrbSolve(diagonalOnly, oneColor, cancel, route) == PHOrbSolveStatus::NoRoute && !route.count,
        "a color goal cannot cross a diagonal-only opening under four-way movement");
    oneColor.diagonal = true;
    status = PHOrbSolve(diagonalOnly, oneColor, cancel, route);
    Check(status == PHOrbSolveStatus::Solved && route.count == 2 && route.cells[0] == 7 && route.cells[1] == 0,
        "eight-way color search completes the triple using the sole legal fixed-start move");
    if (route.count) CheckColorRoute(diagonalOnly, oneColor, status, route);

    auto blocked = recorded;
    for (unsigned i = 0; i < 30; ++i) blocked.types[i] = uint8_t((i % 6 + 2 * (i / 6)) % 6);
    TapeExcept(blocked, {0,1}); blocked.fixedStart = 1;
    status = PHOrbSolve(blocked, thorough, cancel, route);
    Check(status == PHOrbSolveStatus::GoalsUnmet && route.count >= 2 && !route.goalsMet && route.searchPasses <= 4,
        "sufficient inventory with unreachable color supplies remains an unmet preview after bounded recovery");
    if (route.count) CheckColorRoute(blocked, thorough, status, route);
    TapeExcept(blocked, {0});
    Check(PHOrbSolve(blocked, thorough, cancel, route) == PHOrbSolveStatus::NoRoute && !route.count,
        "an isolated fixed start cannot publish a color route");
    TapeExcept(blocked, {});
    Check(PHOrbSolve(blocked, thorough, cancel, route) == PHOrbSolveStatus::NoRoute && !route.count,
        "a taped fixed start cannot be bypassed by color recovery");

    for (unsigned restriction = 0; restriction < 4; ++restriction) {
        auto scarce = recorded;
        if (!restriction) scarce.types[27] = 0; // Exactly two dark orbs remain.
        else if (restriction == 1) scarce.unmatchableColors = 16;
        else if (restriction == 2) scarce.minErase = 4;
        else scarce.minMatch = 4;
        Check(PHOrbInspectInventory(scarce, thorough.goals).kind == PHOrbInventoryKind::GoalCapacity &&
            PHOrbSolve(scarce, thorough, cancel, route) == PHOrbSolveStatus::InsufficientOrbs && !route.count && !route.evaluated,
            "scarcity, unmatchable dark and larger match minima reject five colors before searching");
    }
    six.unmatchableColors = 16;
    auto fourColors = thorough; fourColors.steps = 1; fourColors.goals = Goals(PHOrbGoalColors, 31, 4);
    status = PHOrbSolve(six, fourColors, cancel, route);
    Check(status == PHOrbSolveStatus::Solved && route.count == 2 && route.goalHits[0] == 4,
        "unmatchable dark does not prevent a request for four other allowed colors");
    if (route.count) CheckColorRoute(six, fourColors, status, route);
    fourColors.goals = Goals(PHOrbGoalColors, 48, 1);
    status = PHOrbSolve(six, fourColors, cancel, route);
    Check(status == PHOrbSolveStatus::Solved && route.count == 2 && route.goalHits[0] == 1,
        "a partial mask can choose matchable hearts without requiring its unmatchable dark alternative");
    if (route.count) CheckColorRoute(six, fourColors, status, route);
    fourColors.goals = Goals(PHOrbGoalColors, 63, 6);
    Check(PHOrbSolve(six, fourColors, cancel, route) == PHOrbSolveStatus::InsufficientOrbs && !route.count,
        "requesting all six colors still requires the unmatchable color");

    auto limited = thorough; limited.milliseconds = 1;
    const auto start = std::chrono::steady_clock::now();
    status = PHOrbSolve(recorded, limited, cancel, route);
    Check(std::chrono::steady_clock::now() - start < std::chrono::milliseconds(300) &&
        route.searchPasses <= 4 && route.backtracks <= limited.backtrackLimit &&
        route.exploredDepth <= limited.steps && route.count <= limited.steps + 1,
        "color recovery and saved frontiers share the original one-millisecond deadline and gesture limit");
    Check(status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet || status == PHOrbSolveStatus::NoRoute,
        "a tight color deadline returns only a validated candidate or no route");
    if (route.count) CheckColorRoute(recorded, limited, status, route);
    else Check(status == PHOrbSolveStatus::NoRoute, "a color search without a candidate cannot report success");
    cancel.store(true); route.count = 2;
    Check(PHOrbSolve(recorded, thorough, cancel, route) == PHOrbSolveStatus::Cancelled && !route.count && !route.evaluated,
        "a cancelled color search clears any previous executable route before recovery");
    cancel.store(false); limited.recoveryPasses = 2;
    Check(PHOrbSolve(recorded, limited, cancel, route) == PHOrbSolveStatus::Invalid && !route.count,
        "color recovery cannot exceed the existing recovery-pass limit");
    limited.recoveryPasses = 1; limited.backtrackLimit = 3;
    Check(PHOrbSolve(recorded, limited, cancel, route) == PHOrbSolveStatus::Invalid && !route.count,
        "color recovery cannot exceed the existing saved-frontier limit");
}
