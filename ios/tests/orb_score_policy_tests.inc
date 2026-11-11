// Private contracts shared by ordinary, timed and multi-take search.
static void SquareHintPolicyTests() {
    const auto board = PartialSquare(); const auto cells = BoardCells(board);
    const auto primary = MakeContext(board,Goal(PHOrbGoalSquare));
    const auto expected = Evaluate(cells,primary);
    EvaluationCache cache(true); cache.nextPass();
    Check(SameScore(cache.score(cells,primary),expected), "square cache begins with canonical guidance");
    for (unsigned variant = 0; variant < 2; ++variant) {
        auto policy = Alternate(primary,variant);
        policy.squareUniqueSupply = variant == 0;
        policy.shapeHoleWeight = variant == 0 ? 4 : 8;
        policy.shapeDistanceWeight = variant == 0 ? 2 : 1;
        policy.borderRepairWeight = variant == 0 ? 1 : 0;
        const auto actual = Evaluate(cells,policy);
        Check(!actual.result.goalsMet && SameEvaluation(actual.result,expected.result),
            "square donor and filled-template guidance preserve every exact evaluation field");
        cache.nextPass();
        Check(SameScore(cache.score(cells,policy),actual) && cache.calls == variant+2,
            "square policy changes invalidate earlier cached guidance");
        Check(SameScore(cache.score(cells,policy),actual) && cache.hits == variant+1,
            "repeated square layouts reuse only their current policy");
    }
}

static void RouteOrderTests() {
    Node shortRoute, moreErased;
    shortRoute.length = 8;
    shortRoute.score.result.goalsMet = shortRoute.score.result.hazardsSafe = true;
    shortRoute.score.result.combos = 6; shortRoute.score.result.erased = 18;
    moreErased = shortRoute; moreErased.length = 16; moreErased.score.result.erased = 22;
    Check(Better(shortRoute,moreErased,false,false,{},true) &&
        !Better(moreErased,shortRoute,false,false,{},true) && Better(moreErased,shortRoute,false,false,{}),
        "returned ordinary routes prefer fewer moves at equal Combo; beam guidance retains erased-orb preference");
    auto equalLength = moreErased; equalLength.length = shortRoute.length;
    Check(Better(equalLength,shortRoute,false,false,{},true),
        "erased orbs break ties between equal-length, equal-Combo routes");
    auto higherCombo = moreErased; higherCombo.score.result.combos = 7;
    Check(Better(higherCombo,shortRoute,false,false,{},true), "shorter gestures cannot displace a higher Combo");
    auto bad = shortRoute; bad.score.result.hazardsSafe = false;
    Check(!Better(bad,moreErased,false,false,{},true), "shorter gestures cannot displace release safety");
    bad = shortRoute; bad.score.result.goalsMet = false;
    Check(!Better(bad,moreErased,false,false,{},true), "shorter gestures cannot displace required goals");
    bad = shortRoute; bad.thornDamage = 1;
    for (bool comboPriority : {false,true})
        Check(!Better(bad,moreErased,comboPriority,false,{},true),
            "equal-Combo shorter gestures cannot displace the lower thorn-damage route");
    bad = shortRoute; bad.score.result.poisonErased = 3; bad.score.result.heartFours = 1;
    Check(!Better(bad,moreErased,false,true,{},true),
        "even opted-in poison stays ahead of gesture length in final ranking");
    PHOrbActivityObjective activity; activity.firstWaveOnly = true;
    Check(Better(moreErased,shortRoute,false,false,activity,true),
        "activity profiles keep their existing ranking instead of inheriting ordinary route preferences");
    activity.stepCost = 2;
    Check(Better(shortRoute,moreErased,false,false,activity,true), "activity profiles retain their explicit step cost");

    // Four reds permit at most one Combo; only three currently form a run.
    // A longer route might clear the fourth, but cannot outrank one move.
    PHOrbBoard board{}; board.width = 5; board.height = 4; board.unmatchableColors = 2;
    for (unsigned i = 0; i < 20; ++i) { board.types[i] = 1; board.tokens[i] = i + 1; }
    for (unsigned i : {0u,1u,2u,18u}) board.types[i] = 0;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = Goal(PHOrbGoalCombo,0,1);
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    const auto status = PHOrbSolve(board,options,cancel,route);
    PHOrbEvaluation checked{}; uint32_t damage = 0; uint8_t contacts = 0;
    Check(status == PHOrbSolveStatus::Solved && route.count == 2 && route.combos == 1 && route.erased == 3 &&
        route.exploredDepth == 1 && !route.deadlineReached &&
        PHOrbEvaluateRoute(board,route,options,checked,damage,contacts) && checked.goalsMet && checked.hazardsSafe,
        "Combo upper bound stops deeper ordinary search once the best one-move route wins before erased count");
}

static void MultiFishingCacheTests() {
    auto board = ShapeFixture(PHOrbGoalL);
    board.spinnerMask = (Bits(1) << 22) | (Bits(1) << 23); board.spinnerColors = 30;
    auto goals = Goal(PHOrbGoalL); goals.items[goals.count++] = {PHOrbGoalFour,32,1,0};
    auto context = MakeContext(board,goals,true,false,false,{},22);
    const auto originalPhases = context.spinnerStates;
    const auto cells = BoardCells(board);
    const unsigned size = board.width * board.height;
    EvaluationCache cache(true); cache.nextPass();
    unsigned actualCalls = 0;
    const auto score = [&](const Cells &layout) {
        return cache.score(layout,size,[&]() {
            ++actualCalls; return MultiFishingScore(layout,context,22,board.spinnerColors);
        });
    };
    const auto direct = MultiFishingScore(cells,context,22,board.spinnerColors);
    auto incomplete = score(cells); incomplete.result.goalsMet = false;
    Check(direct.result.goalsMet && !incomplete.result.goalsMet && SameScore(score(cells),direct) &&
        actualCalls == 1 && cache.calls == 1 && cache.hits == 1,
        "incomplete exchange gating on a candidate copy cannot poison a later complete cache hit");
    Check(context.spinnerStates == originalPhases, "multi-take evaluation restores the independent selected-spinner phases");
    // Equal colors preserve the evaluation, regardless of a different path or
    // pending donor/take counts. These remain in search nodes, not the cache.
    auto identical = cells; std::swap(identical[0],identical[6]);
    Check(identical == cells && SameScore(score(identical),direct) && actualCalls == 1,
        "equal-color swaps reuse the ungated layout score");
    // Find a different board in the same direct-mapped bucket. A collision
    // must compare cells; a hash-only lookup would incorrectly reuse direct.
    const auto bucket = [&](const Cells &layout) {
        uint64_t hash = UINT64_C(14695981039346656037);
        for (unsigned i = 0; i < size; ++i) { hash ^= layout[i]; hash *= UINT64_C(1099511628211); }
        return hash & 4095;
    };
    Cells collision{}; bool collided = false;
    for (unsigned n = 0; n < 46656 && !collided; ++n) {
        collision = cells; unsigned digits = n;
        for (unsigned i = 0; i < 6; ++i) { collision[i] = uint8_t(digits % 6); digits /= 6; }
        collided = collision != cells && bucket(collision) == bucket(cells);
    }
    Check(collided, "cache collision fixture has a different valid layout in the same bucket");
    if (collided) {
        const unsigned before = actualCalls;
        Check(SameScore(score(collision),MultiFishingScore(collision,context,22,board.spinnerColors)) &&
            SameScore(score(cells),direct) && actualCalls == before + 2,
            "colliding layouts evict and recompute rather than sharing a score");
    }
    auto differentGoals = Goal(PHOrbGoalSquare);
    context = MakeContext(board,differentGoals,true,false,false,{},22); cache.nextPass();
    const unsigned before = actualCalls;
    const auto other = score(cells);
    Check(!other.result.goalsMet && SameScore(other,MultiFishingScore(cells,context,22,board.spinnerColors)) &&
        actualCalls == before + 1 && cache.calls == actualCalls,
        "a new inventory plan invalidates scores even for an identical layout");
}
