// Written invariants for colour/Match recovery and shared spinner phases.
// These tests do not assert that heuristic search solves the recorded board.
static PHOrbBoard ColorSpinnerGuidanceBoard(unsigned width = 6, unsigned height = 5) {
    PHOrbBoard board{}; board.width = uint8_t(width); board.height = uint8_t(height);
    for (unsigned i = 0; i < width * height; ++i) {
        board.types[i] = uint8_t((i % width + 2 * (i / width)) % 6);
        board.tokens[i] = i + 1;
    }
    return board;
}

static PHOrbGoalSet ColorHeartGoals() {
    auto goals = Goal(PHOrbGoalColors,31,5);
    goals.items[goals.count++] = {PHOrbGoalMatch,32,1,3}; return goals;
}

static void ColorRecoveryPolicyTests() {
    auto board = ColorSpinnerGuidanceBoard(); PHOrbSolveOptions options;
    options.goals = ColorHeartGoals();
    for (unsigned spinners : {0u,1u,3u,4u}) {
        board.spinnerMask = spinners ? UINT64_C(1) << 28 : 0;
        if (spinners >= 3) board.spinnerMask |= (UINT64_C(1) << 2) | (UINT64_C(1) << 20);
        if (spinners == 4) board.spinnerMask |= UINT64_C(1) << 9;
        board.spinnerColors = spinners ? 63 : 0;
        Check(!std::strcmp(PHOrbColorRecoveryEligibilityReason(board,options),"eligible") &&
            MakeContext(board,options.goals).colorRecovery,
            "ordinary five-colour plus one minimum heart match allows fixed-orb recovery with zero/one/multiple spinners");
    }
    for (unsigned guard = 0; guard < 9; ++guard) {
        auto guarded = options; auto restricted = board;
        const char *reason = "unsupported-goal";
        if (guard == 0) guarded.goals.items[1].length = 4;
        if (guard == 1) guarded.goals.items[1].count = 2;
        if (guard == 2) guarded.goals.items[1].colors = 48;
        if (guard == 3) guarded.goals.items[1].kind = PHOrbGoalExact;
        if (guard == 4) { restricted.minMatch = 4; reason = "match-rule"; }
        if (guard == 5) { guarded.activity.firstWaveOnly = true; reason = "activity"; }
        if (guard == 6) { guarded.fishCell = 28; reason = "fishing"; }
        if (guard == 7) { guarded.goals.items[0].count = 1; reason = "no-multicolor-goal"; }
        if (guard == 8) { guarded.goals.items[1].colors = 0; reason = "invalid-input"; }
        Check(!std::strcmp(PHOrbColorRecoveryEligibilityReason(restricted,guarded),reason),
            "larger/repeated/multicolour Match, Exact, match rules, activity and fishing cannot be silently reduced to ordinary colour recovery");
        if (guard != 8) Check(!MakeContext(restricted,guarded.goals,true,false,false,guarded.activity,guarded.fishCell).colorRecovery,
            "diagnostic recovery exclusions agree with the actual search context");
    }
    board.minErase = board.minMatch = 4; options.goals.items[1].length = 4;
    Check(!std::strcmp(PHOrbColorRecoveryEligibilityReason(board,options),"eligible"),
        "a native four-orb minimum can guide an equivalent one-group length-four Match without lowering its exact goal");
}

static void CheckColorPhaseAggregation(const PHOrbBoard &board, const PHOrbGoalSet &goals) {
    auto primary = MakeContext(board,goals); auto guided = primary; guided.colorGuidance = true;
    const auto cells = BoardCells(board);
    const auto score = Evaluate(cells,guided), exact = Evaluate(cells,primary);
    Check(SameEvaluation(score.result,exact.result),"colour guidance cannot change any exact shared-phase release field");
    unsigned common = 63;
    auto staticContext = primary; staticContext.variants = 1; staticContext.dynamicSpinners = 0;
    for (unsigned phase = 0; phase < primary.variants; ++phase) {
        auto one = cells;
        for (unsigned i = 0; i < board.width * board.height; ++i)
            if (primary.dynamicSpinners & (UINT64_C(1) << i)) one[i] = primary.spinnerStates[phase][i];
        common &= EvaluateStatic(one,staticContext).matchedColors;
    }
    Check(score.matchedColors == common,"matched-colour hints use the intersection of all shared phases, not the first phase");
    // Rephase every source by the same tick. Exact aggregates and hints must
    // be independent of which point of the cycle was captured first.
    auto shifted = board;
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (primary.dynamicSpinners & (UINT64_C(1) << i)) shifted.types[i] = primary.spinnerStates[1 % primary.variants][i];
    auto rephased = MakeContext(shifted,goals); rephased.colorGuidance = true;
    const auto other = Evaluate(BoardCells(shifted),rephased);
    Check(SameScore(score,other),"all colour progress, missing colours and chosen repair layouts are invariant under a shared phase shift");
}

static void ColorSpinnerCacheTests() {
    // Anonymous four-spinner layout. The cache stores context-only colour
    // evaluations; held-token restrictions and route damage belong to nodes.
    const uint8_t types[] = {0,5,0,3,5,4, 3,4,2,4,2,3, 1,2,4,2,1,1,
        2,1,5,2,3,4, 0,5,3,2,2,5};
    auto board = ColorSpinnerGuidanceBoard();
    std::copy(std::begin(types),std::end(types),board.types);
    for (unsigned cell : {7u,10u,19u,22u}) board.spinnerMask |= UINT64_C(1) << cell;
    board.spinnerColors = 63;
    for (unsigned cell : {1u,5u,11u,13u,15u,17u,20u,23u}) {
        board.flags[cell] = PHOrbFlagThorn; board.thornDamage[cell] = 4;
    }
    const auto primary = MakeContext(board,ColorHeartGoals(),true,false,true);
    Check(primary.variants == 6 && !primary.shapeGoals && primary.colorRecovery,
        "the four-spinner colour/heart fixture exercises caching without a shape goal");
    const auto cells = BoardCells(board);
    auto changed = cells; std::swap(changed[0],changed[1]);
    Check(changed != cells,"the cache miss fixture moves two different fixed colours");
    const auto exact = Evaluate(cells,primary);
    Check(UseEvaluationCache(primary),"multiphase colour recovery uses the bounded evaluation cache");
    EvaluationCache cache(UseEvaluationCache(primary));
    for (unsigned pass = 0; pass < 4; ++pass) {
        auto context = primary;
        if (pass == 1 || pass == 2) context.colorGuidance = true;
        if (pass == 2) context.colorFocus = 1;
        const auto expected = Evaluate(cells,context), moved = Evaluate(changed,context);
        Check(SameEvaluation(expected.result,exact.result),
            "primary, balanced and water-focused guidance retain exact six-phase release fields");
        const unsigned calls = cache.calls, hits = cache.hits;
        cache.nextPass();
        Check(SameScore(cache.score(cells,context),expected) && cache.calls == calls + 1,
            "each colour search pass evaluates its own context instead of inheriting cached guidance");
        Check(SameScore(cache.score(cells,context),expected) && cache.calls == calls + 1 && cache.hits == hits + 1,
            "repeated four-spinner colour layouts reuse the complete score within the current pass");
        Check(SameScore(cache.score(changed,context),moved) && cache.calls == calls + 2,
            "a changed fixed-colour layout gets a new six-phase evaluation");
        Check(SameScore(cache.score(changed,context),moved) && cache.calls == calls + 2 && cache.hits == hits + 2,
            "the moved colour layout also reuses only its exact current-pass result");
    }
    Check(cache.calls == 8 && cache.hits == 8,
        "colour-only multiphase caching performs real reuse across separately evaluated passes");
    CheckColorPhaseAggregation(board,ColorHeartGoals());
}

static void SingleFishingColorCacheTests() {
    auto board = ColorSpinnerGuidanceBoard();
    board.spinnerMask = UINT64_C(1) << 28; board.spinnerColors = 63;
    const auto goals = ColorHeartGoals();
    auto ordinary = MakeContext(board,goals); ordinary.variants = 1;
    Check(!UseEvaluationCache(ordinary),"single-phase colour search keeps its existing cache policy");
    const auto single = MakeContext(board,goals,true,false,true,{},28,0);
    Check(!single.colorRecovery && !single.shapeGoals && UseEvaluationCache(single),
        "single-take multicolour search caches shared-phase scores without enabling ordinary recovery");
    const auto combo = MakeContext(board,Goal(PHOrbGoalCombo,0,5),true,false,true,{},28,0);
    const auto oneColor = MakeContext(board,Goal(PHOrbGoalColors,31,1),true,false,true,{},28,0);
    Check(!UseEvaluationCache(combo) && !UseEvaluationCache(oneColor),
        "the new single-take cache scope excludes Combo-only and one-colour requests");
    EvaluationCache cache(UseEvaluationCache(single));
    for (unsigned take = 0; take < PHOrbBasicTypes; ++take) {
        const auto context = MakeContext(board,goals,true,false,true,{},28,take);
        auto cells = BoardCells(board); cells[28] = uint8_t(take);
        auto entered = cells; std::swap(entered[27],entered[28]);
        const auto expected = Evaluate(cells,context), afterEntry = Evaluate(entered,context);
        const unsigned calls = cache.calls, hits = cache.hits;
        cache.nextPass();
        Check(SameScore(cache.score(cells,context),expected) && cache.calls == calls + 1,
            "a fresh single-take context evaluates its requested colour without inheriting another pass");
        Check(SameScore(cache.score(cells,context),expected) && cache.calls == calls + 1 && cache.hits == hits + 1,
            "single-take repeated colour layouts reuse all exact and guidance fields");
        // With equal colours the entry leaves evaluator input unchanged;
        // fished state still changes which successors the search may take.
        const unsigned changed = entered != cells;
        Check(SameScore(cache.score(entered,context),afterEntry) && cache.calls == calls + 1 + changed &&
            cache.hits == hits + 2 - changed,
            "single-take entry reuses a score only when its complete colour layout is equal");
        Node pending; pending.cells = entered; pending.path[0] = 27;
        Node taken = pending; taken.fished = true;
        Check(Hash(pending,30) != Hash(taken,30),
            "evaluation reuse cannot merge pending and consumed single-take movement states");
    }
}

static void ColorSpinnerGuidanceTests() {
    ColorRecoveryPolicyTests();
    ColorSpinnerCacheTests();
    SingleFishingColorCacheTests();
    for (auto dimensions : {std::pair<unsigned,unsigned>{6,5},{7,6},{5,4}}) {
        auto board = ColorSpinnerGuidanceBoard(dimensions.first,dimensions.second);
        for (unsigned count : {1u,3u,4u}) for (unsigned table : {3u,37u,63u}) {
            board.spinnerMask = 1;
            if (count >= 3) board.spinnerMask |= (UINT64_C(1) << (board.width + 1)) |
                (UINT64_C(1) << (board.width * board.height - 1));
            if (count == 4) board.spinnerMask |= UINT64_C(1) << (2 * board.width + 2);
            board.spinnerColors = uint16_t(table);
            for (unsigned i = 0; i < board.width * board.height; ++i)
                if (board.spinnerMask & (UINT64_C(1) << i)) board.types[i] = i ? 0 : uint8_t(table == 37 ? 5 : 1);
            CheckColorPhaseAggregation(board,ColorHeartGoals());
        }
    }

    // Fire/water alternatives share one spinner: fire completes the row,
    // water completes the column. Wood is fixed. Each phase has two colours
    // even though only wood belongs to the intersection. Hearts are absent.
    auto board = ColorSpinnerGuidanceBoard();
    for (unsigned i = 0; i < 30; ++i) board.types[i] = uint8_t(3 + (i % 6 + i / 6) % 2);
    board.types[0] = board.types[1] = board.types[2] = 0;
    board.types[8] = board.types[14] = 1;
    board.types[24] = board.types[25] = board.types[26] = 2;
    board.spinnerMask = UINT64_C(1) << 2; board.spinnerColors = 3;
    auto goals = Goal(PHOrbGoalColors,7,2); goals.bonusFirstWave = true;
    goals.items[goals.count++] = {PHOrbGoalMatch,32,1,3};
    auto context = MakeContext(board,goals); context.colorGuidance = true;
    const auto score = Evaluate(BoardCells(board),context);
    Check(score.result.goalHits[0] == 2 && !score.result.goalHits[1] &&
        (score.matchedColors & 7) == 4 && score.missingColors == 32 && score.weakestColor == 5 &&
        score.colorProgress[5] == 0 && !score.result.goalsMet,
        "already-satisfied optional colours never become mandatory; an unmet heart clause remains the only guided deficit");
    goals.count = 1;
    context = MakeContext(board,goals); context.colorGuidance = true;
    const auto optional = Evaluate(BoardCells(board),context);
    Check(optional.result.goalsMet && !optional.missingColors && optional.colorGuidedProgress == 4096,
        "phase-specific different subsets satisfy the original two-of-three request without requiring the full intersection");
    CheckColorPhaseAggregation(board,goals);

    // Only a spinner can fill the third red cell; the fixed inventory has
    // two reds. The repair heuristic must not borrow the transient third orb.
    auto fixed = BoardCells(board); fixed[2] = 0;
    std::array<Bits,PHOrbTypes> colors{};
    for (unsigned i = 0; i < 30; ++i) colors[fixed[i]] |= UINT64_C(1) << i;
    Check(!RepairColor(colors[0] & ~context.dynamicSpinners,context.patterns[0],0,context).value,
        "a transient spinner cell is neither a stable template cell nor a fixed supply orb");

    // The earlier anonymous nineteen-step fixture clears first-wave colours
    // 0/3/5 and then 1/2. Relabel 1<->5 so the required heart is cascade-only.
    const uint8_t cascade[] = {4,3,4,1,0,1, 0,5,2,5,5,3, 2,3,3,3,2,0,
        5,1,0,5,0,0, 1,0,0,4,2,1};
    const uint8_t path[] = {11,4,9,16,21,28,27,20,15,8,1,2,3,9,15,21,22,28,21,15};
    auto cascaded = ColorSpinnerGuidanceBoard();
    std::copy(std::begin(cascade),std::end(cascade),cascaded.types);
    for (unsigned step = 1; step < sizeof(path); ++step)
        std::swap(cascaded.types[path[step-1]],cascaded.types[path[step]]);
    for (unsigned i = 0; i < 30; ++i)
        cascaded.types[i] = cascaded.types[i] == 1 ? 5 : cascaded.types[i] == 5 ? 1 : cascaded.types[i];
    auto cascadeGoals = Goal(PHOrbGoalColors,15,3);
    cascadeGoals.items[cascadeGoals.count++] = {PHOrbGoalMatch,32,1,3};
    for (bool first : {false,true}) {
        cascadeGoals.bonusFirstWave = first;
        auto cascadeContext = MakeContext(cascaded,cascadeGoals); cascadeContext.colorGuidance = true;
        const auto actual = Evaluate(BoardCells(cascaded),cascadeContext);
        Check(actual.result.goalHits[0] >= 3 && (actual.result.goalHits[1] != 0) == !first &&
            actual.result.goalsMet == !first && (actual.missingColors & 32) == (first ? 32 : 0),
            "cascade hearts satisfy ordinary minimum Match but cannot satisfy a first-removal-only colour/Match conjunction");
        CheckColorPhaseAggregation(cascaded,cascadeGoals);
    }
}
