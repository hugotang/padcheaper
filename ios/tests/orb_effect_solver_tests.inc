// Independent effect oracle: whole-line runs, disjoint-set components, then
// column reconstruction. No production matching, phase or replay helpers.
struct EffectOracleResult { unsigned combos = 0, erased = 0, poison = 0, bombs = 0; };
static EffectOracleResult EffectOracle(const PHOrbBoard &board) {
    EffectOracleResult result;
    const unsigned w = board.width, h = board.height, n = w * h, run = board.minErase ? board.minErase : 3;
    std::vector<int> cells(board.types, board.types + n);
    bool first = true;
    for (;;) {
        std::set<unsigned> marked;
        for (unsigned axis = 0; axis < 2; ++axis) {
            const unsigned lines = axis ? w : h, length = axis ? h : w;
            for (unsigned line = 0; line < lines; ++line) {
                auto at = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
                for (unsigned begin = 0; begin < length;) {
                    unsigned end = begin + 1;
                    while (end < length && cells[at(end)] == cells[at(begin)]) ++end;
                    int color = cells[at(begin)];
                    if (color >= 0 && !(board.unmatchableColors & (1u << color)) && end - begin >= run)
                        for (unsigned p = begin; p < end; ++p) marked.insert(at(p));
                    begin = end;
                }
            }
        }
        std::vector<unsigned> parent(n), sizes(n);
        std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](unsigned a) { while (parent[a] != a) a = parent[a]; return a; };
        for (unsigned a : marked) for (unsigned b : marked)
            if (a < b && cells[a] == cells[b] && ((a / w == b / w && b == a + 1) || b == a + w)) parent[root(b)] = root(a);
        for (unsigned cell : marked) ++sizes[root(cell)];
        for (auto it = marked.begin(); it != marked.end();)
            if (sizes[root(*it)] < board.minMatch) it = marked.erase(it); else ++it;
        if (first) {
            for (unsigned i = 0; i < n; ++i) result.bombs += cells[i] == 9 && !marked.count(i);
            first = false;
        }
        if (marked.empty()) return result;
        std::set<unsigned> groups;
        for (unsigned i : marked) {
            groups.insert(root(i)); result.poison += cells[i] == 7 || cells[i] == 8; cells[i] = -1;
        }
        result.combos += unsigned(groups.size()); result.erased += unsigned(marked.size());
        for (unsigned x = 0; x < w; ++x) {
            std::vector<int> survivors;
            for (unsigned y = 0; y < h; ++y) if (cells[y * w + x] >= 0) survivors.push_back(cells[y * w + x]);
            survivors.insert(survivors.begin(), h - survivors.size(), -1);
            for (unsigned y = 0; y < h; ++y) cells[y * w + x] = survivors[y];
        }
    }
}

static void TapeExcept(PHOrbBoard &board, std::initializer_list<unsigned> open) {
    board.tapeMask = (UINT64_C(1) << (board.width * board.height)) - 1;
    for (unsigned cell : open) board.tapeMask &= ~(UINT64_C(1) << cell);
    for (unsigned i = 0; i < board.width * board.height; ++i)
        board.flags[i] = (board.flags[i] & ~PHOrbFlagTape) | ((board.tapeMask & (UINT64_C(1) << i)) ? PHOrbFlagTape : 0);
}

static void OrbEffectSolverTests() {
    PHOrbEvaluation result{};
    std::atomic_bool cancel{false}; PHOrbRoute route{}; PHOrbBoard replay{};
    auto jammer = ShapeBoard(6, 5, {});
    jammer.types[24] = jammer.types[25] = jammer.types[26] = PHOrbJammer;
    Check(PHOrbEvaluate(jammer, Goals(PHOrbGoalColors, 1), result) && result.hazardsSafe && result.combos >= 1 && !result.goalsMet,
        "jammer matches count as combos without supplying an attack color");
    for (unsigned poison : {unsigned(PHOrbPoison), unsigned(PHOrbMortalPoison)}) {
        auto board = jammer;
        board.types[24] = board.types[25] = board.types[26] = uint8_t(poison);
        Check(PHOrbEvaluate(board, {}, result) && !result.hazardsSafe && result.poisonErased == 3,
            "poison and mortal poison matches never authorize a route");
        board.unmatchableColors = uint16_t(1u << poison);
        Check(PHOrbEvaluate(board, {}, result) && result.hazardsSafe && result.poisonErased == 0,
            "an unmatchable poison color stays on the board in every wave");
    }
    auto cascade = ShapeBoard(6, 5, {26, 27, 28});
    cascade.types[24] = cascade.types[25] = cascade.types[20] = PHOrbPoison;
    Check(PHOrbEvaluate(cascade, {}, result) && result.poisonErased == 3 && !result.hazardsSafe,
        "a poison match created by deterministic falling is included in the hazard gate");
    cascade.types[24] = cascade.types[25] = cascade.types[20] = PHOrbBomb;
    Check(PHOrbEvaluate(cascade, {}, result) && result.unmatchedBombs == 3 && !result.hazardsSafe,
        "bombs that would match only after falling are unsafe before the first removal wave");
    auto bombs = jammer; bombs.types[24] = bombs.types[25] = bombs.types[26] = PHOrbBomb;
    bombs.flags[24] = PHOrbFlagLocked;
    Check(PHOrbEvaluate(bombs, {}, result) && result.hazardsSafe && result.unmatchedBombs == 0,
        "locked bombs can be safely erased together in the first wave");
    bombs.unmatchableColors = 1u << PHOrbBomb;
    Check(PHOrbEvaluate(bombs, {}, result) && !result.hazardsSafe && result.unmatchedBombs == 3,
        "unmatchable bombs cannot be mistaken for a safe bomb match");
    bombs.unmatchableColors = 0; bombs.minErase = 4;
    Check(PHOrbEvaluate(bombs, {}, result) && result.unmatchedBombs == 3, "minimum straight-run rules also apply to bombs");
    bombs.minErase = 3; bombs.minMatch = 4;
    Check(PHOrbEvaluate(bombs, {}, result) && result.unmatchedBombs == 3, "connected-group restrictions also apply to bombs");

    auto unsafe = ShapeBoard(6, 5, {}); unsafe.types[29] = PHOrbBomb; TapeExcept(unsafe, {0, 1});
    Check(PHOrbSolve(unsafe, {2, 16, 1000}, cancel, route) == PHOrbSolveStatus::Unsafe &&
        !route.hazardsSafe && route.unmatchedBombs == 1 && route.count == 2,
        "a lone bomb exposes a non-executable partial route rather than a successful solve");
    TapeExcept(unsafe, {});
    Check(PHOrbSolve(unsafe, {2, 16, 1000}, cancel, route) == PHOrbSolveStatus::NoRoute && !route.count,
        "a fully sealed board yields no route");

    for (auto dimensions : {std::pair<unsigned, unsigned>{6,5}, {7,6}, {5,4}}) {
        auto board = ShapeBoard(dimensions.first, dimensions.second, {});
        const unsigned n = board.width * board.height;
        board.fixedStart = uint8_t(n); board.spinnerMask = 1; board.spinnerColors = 63;
        board.tapeMask = UINT64_C(1) << 1; board.flags[1] = PHOrbFlagTape;
        board.flags[2] = PHOrbFlagThorn; board.thornDamage[2] = 4;
        board.cloudMask = UINT64_C(1) << (n - 1);
        Check(PHOrbSolve(board, {2, 16, 1000}, cancel, route) == PHOrbSolveStatus::Solved && route.cells[0] == n - 1 &&
            PHOrbReplay(&board, &route, route.count - 1, &replay), "all geometries respect fixed start and avoid spinner, tape and thorn cells");
        for (unsigned i = 0; i < route.count; ++i) Check(route.cells[i] > 2, "every step stays outside the blocked area");
        PHOrbRoute bad{}; bad.count = 2; bad.cells[0] = 3; bad.cells[1] = 4;
        Check(!PHOrbReplay(&board, &bad, 1, &replay), "replay independently rejects the wrong fixed start");
        board.fixedStart = 0; bad.cells[0] = 3; bad.cells[1] = 2;
        Check(PHOrbReplay(&board, &bad, 1, &replay) && replay.thornDamage[3] == 4,
            "replay tracks a thorn moving with its token; route validation separately checks damage");
        bad.cells[0] = uint8_t(board.width + 1); bad.cells[1] = 1;
        Check(!PHOrbReplay(&board, &bad, 1, &replay), "replay rejects entering tape even along an adjacent edge");
        bad.cells[0] = uint8_t(board.width); bad.cells[1] = 0;
        Check(!PHOrbReplay(&board, &bad, 1, &replay), "replay rejects entering a rotating cell");
    }

    auto spinner = ShapeBoard(6, 5, {0, 1, 2, 7, 13}); // Red T, spinner on its border.
    spinner.spinnerMask = UINT64_C(1) << 3; spinner.spinnerColors = 63;
    Check(PHOrbEvaluate(spinner, Goals(PHOrbGoalT), result) && !result.goalsMet,
        "an adjacent spinner can invalidate an exact T at a different release color");
    spinner.flags[3] = PHOrbFlagLocked;
    Check(PHOrbEvaluate(spinner, Goals(PHOrbGoalT), result) && result.goalsMet && PHOrbSpinnerVariants(&spinner) == 1,
        "a locked spinner keeps its actual non-red color while still being excluded from the path");
    spinner.flags[3] = 0;
    spinner.spinnerMask |= UINT64_C(1) << 29; spinner.spinnerColors = 7;
    spinner.types[3] = 0; spinner.types[29] = 1;
    auto advanced = spinner; advanced.types[3] = 1; advanced.types[29] = 2;
    Check(!PHOrbSameBoard(&spinner, &advanced) && PHOrbSamePlanBoard(&spinner, &advanced),
        "multiple spinner colors may advance together while token identity stays exact");
    advanced.types[29] = 1;
    Check(!PHOrbSamePlanBoard(&spinner, &advanced), "one independently changing spinner invalidates the common-phase assumption");
    advanced = spinner; ++advanced.tokens[3];
    Check(!PHOrbSamePlanBoard(&spinner, &advanced), "spinner tolerance never hides a replacement token");
    spinner.spinnerColors = 0x400;
    Check(!PHOrbSpinnerSupported(&spinner), "unknown native color ten is not enabled by a custom spinner mask");
    spinner.spinnerColors = 1u | (1u << PHOrbPoison); spinner.types[3] = spinner.types[29] = 0;
    float plus = 0.06f; std::memcpy(&spinner.enhancement[3], &plus, sizeof(plus));
    Check(!PHOrbSpinnerSupported(&spinner), "hazard-color spinners wait until clearing of enhancement has settled");
    spinner.enhancement[3] = 0; spinner.flags[3] = PHOrbFlagNail;
    Check(!PHOrbSpinnerSupported(&spinner), "hazard-color spinner metadata cannot silently discard a nail marker");
    spinner.flags[3] = 0;
    Check(PHOrbSpinnerSupported(&spinner), "settled custom hazard-color spinners are evaluated with the hazard policy");

    // Independent shared-phase lower bounds, including hazards and masks.
    const unsigned widths[] = {6, 7, 5}, heights[] = {5, 6, 4};
    for (unsigned seed = 1; seed <= 12; ++seed) {
        const unsigned geometry = (seed - 1) % 3;
        auto board = Board(widths[geometry], heights[geometry], seed);
        const unsigned n = board.width * board.height;
        for (unsigned i = 0; i < n; ++i) board.types[i] = uint8_t((board.types[i] + i + seed) % 10);
        board.spinnerMask = 1 | (UINT64_C(1) << (n - 1)); board.spinnerColors = 0x3ff;
        board.minErase = uint8_t(3 + seed % 3); board.minMatch = uint8_t(seed % 7);
        board.unmatchableColors = uint16_t(seed % 2 ? 0 : 1u << 5);
        EffectOracleResult worst{n, n, 0, 0};
        for (unsigned phase = 0; phase < 10; ++phase) {
            auto variant = board;
            variant.types[0] = uint8_t((board.types[0] + phase) % 10);
            variant.types[n - 1] = uint8_t((board.types[n - 1] + phase) % 10);
            auto oracle = EffectOracle(variant);
            worst.combos = std::min(worst.combos, oracle.combos); worst.erased = std::min(worst.erased, oracle.erased);
            worst.poison = std::max(worst.poison, oracle.poison); worst.bombs = std::max(worst.bombs, oracle.bombs);
        }
        Check(PHOrbEvaluate(board, {}, result) && result.combos == worst.combos && result.erased == worst.erased &&
            result.poisonErased == worst.poison && result.unmatchedBombs == worst.bombs &&
            result.hazardsSafe == (!worst.poison && !worst.bombs), "mixed-effect evaluation agrees with independent runs, groups and cascades in every spinner phase");
    }
    auto four = ShapeBoard(6, 5, {0, 1, 2, 3}); four.minErase = 4;
    Check(PHOrbEvaluate(four, Goals(PHOrbGoalFour), result) && result.goalsMet, "four-in-a-row remains possible with a four-orb minimum");
    four.minErase = 5;
    Check(PHOrbEvaluate(four, Goals(PHOrbGoalFour), result) && !result.goalsMet, "a higher native run minimum disables a four-orb goal");
    four.minErase = 3; four.unmatchableColors = 1;
    Check(PHOrbSolve(four, {2, 16, 1000, Goals(PHOrbGoalFour)}, cancel, route) == PHOrbSolveStatus::InsufficientOrbs,
        "unmatchable colors never satisfy the pre-search inventory gate");
    auto longRun = ShapeBoard(7, 6, {35, 36, 37, 38, 39, 40, 41});
    for (unsigned minimum : {6u, 7u}) {
        longRun.minErase = uint8_t(minimum); longRun.minMatch = 7;
        Check(PHOrbEvaluate(longRun, Goals(PHOrbGoalRow), result) && result.goalsMet && result.erased == 7,
            "six- and seven-orb minimums accept a complete native seven-cell row");
    }
    longRun.minErase = 7; longRun.minMatch = 8;
    Check(PHOrbEvaluate(longRun, Goals(PHOrbGoalRow), result) && !result.goalsMet && !result.erased,
        "a legal seven-cell run still fails a larger connected-group threshold");

    auto decorated = ShapeBoard(6, 5, {0, 1, 2});
    decorated.flags[0] = PHOrbFlagNail | PHOrbFlagCombo | PHOrbFlagBlind;
    decorated.flags[1] = PHOrbFlagBlindTouch; decorated.blindTurns[0] = 5;
    float weak = -0.5f; std::memcpy(&decorated.enhancement[0], &weak, sizeof(weak));
    Check(PHOrbEvaluate(decorated, {}, result) && result.hazardsSafe && result.combos >= 1,
        "weak, blind, nail and combo metadata preserves ordinary removal scoring without promising damage or healing");
    auto visible = decorated; visible.flags[1] &= ~PHOrbFlagBlindTouch;
    Check(!PHOrbSameBoard(&decorated, &visible) && PHOrbSamePlanBoard(&decorated, &visible),
        "native touch clears normal darkness without invalidating token-aware route verification");
    decorated.flags[2] = PHOrbFlagBlindTouch;
    PHOrbRoute swap{}; swap.count = 2; swap.cells[0] = 0; swap.cells[1] = 1;
    Check(PHOrbReplay(&decorated, &swap, 0, &replay) && PHOrbSameBoard(&decorated, &replay),
        "the initial replay frame retains the unhandled darkness");
    Check(PHOrbReplay(&decorated, &swap, 1, &replay) && !(replay.flags[0] & PHOrbFlagBlindTouch) &&
        (replay.flags[1] & (PHOrbFlagBlind | PHOrbFlagNail | PHOrbFlagCombo)) ==
            (PHOrbFlagBlind | PHOrbFlagNail | PHOrbFlagCombo) &&
        replay.blindTurns[1] == 5 && replay.enhancement[1] == decorated.enhancement[0] &&
        replay.tokens[1] == decorated.tokens[0] && (replay.flags[2] & PHOrbFlagBlindTouch) &&
        (decorated.flags[1] & PHOrbFlagBlindTouch),
        "replay clears touched ordinary darkness, swaps persistent metadata and preserves untouched cells and its source");
    decorated.fixedStart = 1; decorated.flags[2] |= PHOrbFlagStartDim;
    visible = decorated; visible.flags[2] &= ~PHOrbFlagStartDim;
    Check(PHOrbSamePlanBoard(&decorated, &visible), "native fixed-start dimming can clear after acceptance");
    visible.fixedStart = 2;
    Check(!PHOrbSamePlanBoard(&decorated, &visible), "a changed fixed-start rule invalidates the previous plan");
    PHOrbSnapshot state{}; state.board = decorated; state.board.flags[29] = PHOrbFlagThorn;
    auto flags = PHOrbBoardObstacles(&state);
    Check((flags & (PHOrbObstacleNail | PHOrbObstacleThorn | PHOrbObstacleCombo | PHOrbObstacleFixedStart |
        PHOrbObstacleWeakened | PHOrbObstacleBlind | PHOrbObstacleSuperBlind)) ==
        (PHOrbObstacleNail | PHOrbObstacleThorn | PHOrbObstacleCombo | PHOrbObstacleFixedStart |
        PHOrbObstacleWeakened | PHOrbObstacleBlind | PHOrbObstacleSuperBlind), "high obstacle bits and simultaneous decorations are never truncated");

    auto bounded = Board(7, 6, 731);
    bounded.spinnerMask = 1 | (UINT64_C(1) << 41); bounded.spinnerColors = 0x3ff;
    auto started = std::chrono::steady_clock::now();
    auto status = PHOrbSolve(bounded, {48, 512, 1, Goals(PHOrbGoalCombo, 0, 1)}, cancel, route);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    Check((status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet ||
        status == PHOrbSolveStatus::Unsafe || status == PHOrbSolveStatus::NoRoute) && elapsed < 500,
        "a ten-phase spinner search observes a short deadline without completing a full batch of candidates");
    if (route.count) Check(PHOrbReplay(&bounded, &route, route.count - 1, &replay) &&
        PHOrbEvaluate(replay, Goals(PHOrbGoalCombo, 0, 1), result) && result.hazardsSafe == route.hazardsSafe &&
        result.goalsMet == route.goalsMet && result.poisonErased == route.poisonErased && result.unmatchedBombs == route.unmatchedBombs,
        "deadline-limited spinner candidates retain the complete all-phase hazard and goal checks");
    cancel = true;
    Check(PHOrbSolve(bounded, {48, 512, 1000}, cancel, route) == PHOrbSolveStatus::Cancelled && !route.count,
        "cancelled spinner planning cannot return an executable candidate");
}
