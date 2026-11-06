static PHOrbAssistRecoveryState AssistFishingEvidence(unsigned turns = 4) {
    PHOrbAssistRecoveryState evidence{};
    evidence.valid = true; evidence.turns = uint16_t(turns);
    evidence.perColor[3] = 4; evidence.providers[3] = 1;
    return evidence;
}
static PHOrbSolveOptions AssistFishingOptions() {
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.fishCell = 14; options.diagonal = true; options.milliseconds = 1000;
    options.goals.count = 2; options.goals.comboFirstWave = options.goals.bonusFirstWave = true;
    options.goals.items[0] = {PHOrbGoalT,8,1,0};
    options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    return options;
}
static PHOrbBoard AssistFishingFiveBoard() {
    auto board = ShapeBoard(6,5,{});
    const uint8_t types[] = {0,1,2,3,3,3, 1,2,0,1,3,0, 0,2,2,2,3,1, 1,2,2,2,0,4, 5,5,5,5,1,0};
    std::copy(std::begin(types),std::end(types),board.types);
    board.spinnerMask = UINT64_C(1) << 14; board.spinnerColors = 63; board.fixedStart = 8;
    return board;
}
static PHOrbAssistFishingRoute AssistFishingFiveWitness() {
    PHOrbAssistFishingRoute route;
    route.count = 11; route.fishCell = 14; route.diagonal = true; route.exchangeCount = 5;
    const uint8_t path[] = {7,14,13,14,19,14,20,14,21,14,15};
    std::copy(std::begin(path),std::end(path),route.cells);
    // Five independent wood -> light exchanges deposit an L at 7,13,19,20,21.
    // The starting wood token stays held and finishes at 15. T 3,4,5,10,16 and
    // heart-four 24,25,26,27 never move; spinner 14 is not needed by any shape.
    for (unsigned e = 0; e < 5; ++e) route.exchanges[e] = {uint8_t(2 * e + 1),uint8_t(2 * e + 2),3,2};
    route.plan.goals = AssistFishingOptions().goals;
    route.plan.goals.items[route.plan.goals.count++] = {PHOrbGoalL,8,1,0};
    route.plan.lCounts[3] = 1; route.plan.predictedTurns = 4; route.plan.providers = 1;
    route.plan.take[3] = route.plan.leave[2] = route.plan.exchangeCount = 5;
    return route;
}
// Independent first-removal groups for the explicit witness, using complete
// line scans and four-neighbor flood fill, not production templates/evaluation.
static std::vector<std::set<unsigned>> AssistFishingFirstGroups(const PHOrbBoard &board) {
    const unsigned w = board.width, h = board.height;
    std::set<unsigned> marked;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const unsigned lines = axis ? w : h, length = axis ? h : w;
        for (unsigned line = 0; line < lines; ++line) {
            const auto cell = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
            for (unsigned begin = 0; begin < length;) {
                unsigned end = begin + 1;
                while (end < length && board.types[cell(end)] == board.types[cell(begin)]) ++end;
                if (end - begin >= 3) for (unsigned p = begin; p < end; ++p) marked.insert(cell(p));
                begin = end;
            }
        }
    }
    std::vector<std::set<unsigned>> groups;
    while (!marked.empty()) {
        std::set<unsigned> group; std::vector<unsigned> pending{*marked.begin()}; marked.erase(pending[0]);
        while (!pending.empty()) {
            const unsigned a = pending.back(); pending.pop_back(); group.insert(a);
            for (auto it = marked.begin(); it != marked.end();) {
                const unsigned b = *it;
                const bool adjacent = (a / w == b / w && (a + 1 == b || b + 1 == a)) || a + w == b || b + w == a;
                if (adjacent && board.types[a] == board.types[b]) { pending.push_back(b); it = marked.erase(it); }
                else ++it;
            }
        }
        groups.push_back(group);
    }
    return groups;
}
static void AssistFishingTests() {
    auto board = AssistFishingFiveBoard(); auto options = AssistFishingOptions();
    auto evidence = AssistFishingEvidence(); auto witness = AssistFishingFiveWitness();
    PHOrbBoard replay{}; PHOrbEvaluation evaluation{};
    const auto original = board;
    Check(PHOrbReplayAssistFishing(board,witness,10,replay) && replay.tokens[15] == board.tokens[7],
        "five real exchanges preserve the held orb identity through the final exit");
    std::set<uintptr_t> tokens(replay.tokens,replay.tokens + 30);
    Check(tokens.size() == 30 && std::equal(board.types,board.types + 30,original.types) &&
        std::count(replay.types,replay.types + 30,uint8_t(3)) == 10 &&
        std::count(replay.types,replay.types + 30,uint8_t(5)) == 4,
        "five-take replay creates exactly five light by sacrificing wood and does not modify its input");
    unsigned minimumCombos = 100, minimumFirst = 100;
    for (unsigned color = 0; color < 6; ++color) {
        auto released = replay; released.types[14] = uint8_t(color); released.spinnerMask = released.spinnerColors = 0;
        const auto groups = AssistFishingFirstGroups(released);
        for (const auto &expected : std::vector<std::set<unsigned>>{{3,4,5,10,16},{7,13,19,20,21},{24,25,26,27}})
            Check(std::find(groups.begin(),groups.end(),expected) != groups.end(),
                "independent first-removal groups retain a distinct light T, light L and heart-four in every color");
        const auto oracle = EffectOracle(released);
        minimumCombos = std::min(minimumCombos,oracle.combos); minimumFirst = std::min(minimumFirst,oracle.firstCombos);
    }
    Check(PHOrbEvaluateAssistFishing(board,witness,options,evidence,evaluation) && evaluation.goalsMet &&
        evaluation.hazardsSafe && evaluation.goalHits[0] == 1 && evaluation.goalHits[1] == 1 && evaluation.goalHits[2] == 1 &&
        evaluation.combos == minimumCombos && evaluation.firstWaveCombos == minimumFirst,
        "recovery evaluation agrees with independent all-color groups and Combo counts, without final phase control");
    auto fragile = board; fragile.types[8] = 3;
    Check(PHOrbReplayAssistFishing(fragile,witness,10,replay), "a phase-sensitive layout still has five valid exchanges");
    for (unsigned color : {2u,3u}) {
        auto released = replay; released.types[14] = uint8_t(color); released.spinnerMask = released.spinnerColors = 0;
        const auto groups = AssistFishingFirstGroups(released);
        const bool hasL = std::find(groups.begin(),groups.end(),std::set<unsigned>{7,13,19,20,21}) != groups.end();
        Check(hasL == (color == 2), "independent groups show the light phase merging the L through cells 8 and 14");
    }
    Check(PHOrbEvaluateAssistFishing(fragile,witness,options,evidence,evaluation) && !evaluation.goalsMet &&
        !evaluation.goalHits[2], "a route that loses the recovery L in one spinner color is never all-phase success");
    for (unsigned variant = 0; variant < 3; ++variant) {
        auto transformedBoard = board; auto transformedRoute = witness;
        auto transformedOptions = options; auto transformedEvidence = evidence;
        if (variant == 0) {
            for (unsigned i = 0; i < 30; ++i) transformedBoard.types[i] = board.types[i / 6 * 6 + 5 - i % 6];
            transformedBoard.spinnerMask = UINT64_C(1) << 15; transformedBoard.fixedStart = 11;
            transformedOptions.fishCell = transformedRoute.fishCell = 15;
            for (unsigned i = 0; i < witness.count; ++i)
                transformedRoute.cells[i] = uint8_t(witness.cells[i] / 6 * 6 + 5 - witness.cells[i] % 6);
        } else if (variant == 1) {
            for (unsigned i = 0; i < 30; ++i) transformedBoard.types[i] = board.types[29 - i];
            transformedBoard.spinnerMask = UINT64_C(1) << 15; transformedBoard.fixedStart = 23;
            transformedOptions.fishCell = transformedRoute.fishCell = 15;
            for (unsigned i = 0; i < witness.count; ++i) transformedRoute.cells[i] = uint8_t(29 - witness.cells[i]);
        } else {
            for (unsigned i = 0; i < 30; ++i) {
                if (board.types[i] == 3) transformedBoard.types[i] = 1;
                else if (board.types[i] == 1) transformedBoard.types[i] = 3;
            }
            transformedOptions.goals.items[0].colors = 2;
            transformedRoute.plan.goals.items[0].colors = transformedRoute.plan.goals.items[2].colors = 2;
            transformedRoute.plan.lCounts[3] = transformedRoute.plan.take[3] = 0;
            transformedRoute.plan.lCounts[1] = 1; transformedRoute.plan.take[1] = 5;
            for (auto &exchange : transformedRoute.exchanges) exchange.take = 1;
            transformedEvidence.perColor[3] = transformedEvidence.providers[3] = 0;
            transformedEvidence.perColor[1] = 4; transformedEvidence.providers[1] = 1;
        }
        Check(PHOrbReplayAssistFishing(transformedBoard,transformedRoute,10,replay) &&
            PHOrbEvaluateAssistFishing(transformedBoard,transformedRoute,transformedOptions,transformedEvidence,evaluation) &&
            evaluation.goalsMet && evaluation.hazardsSafe,
            "mirrored, rotated and recolored five-take witnesses retain their original recovery and shape conditions");
    }
    Check(PHOrbReplayAssistFishing(board,witness,1,replay) && replay.types[7] == 3 && replay.types[13] == 2 &&
        replay.tokens[14] == board.tokens[7], "a prefix applies one take but validates all five future exchanges");
    auto inPlace = board;
    Check(PHOrbReplayAssistFishing(inPlace,witness,0,inPlace) && PHOrbSameBoard(&board,&inPlace),
        "zero-step and in-place replay retain the original board after full validation");
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        auto broken = witness;
        if (scenario == 0) broken.exchanges[4].leave = 1;
        if (scenario == 1) broken.exchanges[4].entry = 7;
        if (scenario == 2) broken.exchangeCount = 4;
        if (scenario == 3) broken.cells[10] = 255;
        if (scenario == 4) broken.plan.leave[5] = 1;
        Check(!PHOrbReplayAssistFishing(board,broken,1,replay),
            "future wrong sacrifices, ordering, counts, cells or inventory invalidate even an early prefix");
    }
    auto changed = evidence; changed.turns = 5;
    Check(!PHOrbEvaluateAssistFishing(board,witness,options,changed,evaluation),
        "a four-turn predicted L cannot authorize a later five-turn void");
    changed = evidence; changed.providers[3] = 2;
    Check(!PHOrbEvaluateAssistFishing(board,witness,options,changed,evaluation),
        "a changed recovery provider invalidates a stored plan");
    auto weakened = witness; weakened.plan.goals.items[1] = {PHOrbGoalMatch,32,1,3};
    Check(!PHOrbEvaluateAssistFishing(board,weakened,options,evidence,evaluation),
        "replay cannot silently replace the original heart-four with three hearts");

    PHOrbTimedFishingTiming timing;
    timing.remainingFrames = 570; timing.framesPerMove = 2;
    timing.settleFramesPerWait = 8; timing.inputAndCaptureFrames = 3; timing.spinnerPeriodFrames = 15;
    const auto reservation = PHOrbReserveAssistFishing(board,witness,timing);
    Check(reservation.valid && reservation.fits && reservation.moveFrames == 20 && reservation.takeWaitFrames == 450 &&
        !reservation.releaseWaitFrames && reservation.settleFrames == 48 && reservation.requiredFrames == 521,
        "synthetic five-take timing reserves every full cycle, ten moves, six settling margins and input allowance");
    timing.remainingFrames = 521;
    Check(!PHOrbReserveAssistFishing(board,witness,timing).fits, "exact timer exhaustion never passes the reservation");
    timing.remainingFrames = 570; timing.framesPerMove = UINT32_MAX;
    Check(!PHOrbReserveAssistFishing(board,witness,timing).valid, "frame arithmetic overflow remains invalid");
    timing.framesPerMove = 2; timing.settleFramesPerWait = 0;
    Check(!PHOrbReserveAssistFishing(board,witness,timing).valid, "unknown settling frames are not replaced with zero");
    timing.settleFramesPerWait = 8;

    // A two-color spinner gives a unique donor color. The 7-cell opening also
    // keeps the explicit five-take witness tiny enough for a bounded regression.
    board.spinnerColors = 12;
    TapeExcept(board,{7,13,14,15,19,20,21});
    const auto below = PHOrbPlanAssistFishing(board,options,evidence,4);
    Check(below.valid && below.enumerationComplete && !below.count,
        "four exchanges cannot fill a five-light deficit without borrowing the final spinner color");
    const auto inventory = PHOrbPlanAssistFishing(board,options,evidence,5);
    Check(inventory.valid && inventory.enumerationComplete && inventory.count == 1 && inventory.minimumExchanges == 5 &&
        inventory.plans[0].take[3] == 5 && inventory.plans[0].leave[2] == 5 && !inventory.plans[0].leave[5] &&
        inventory.plans[0].lCounts[3] == 1 && inventory.plans[0].predictedTurns == 4,
        "deficit planning finds five wood-for-light exchanges while reserving the four hearts and original T");
    for (unsigned c = 0; c < 6; ++c)
        Check(!(inventory.plans[0].take[c] && inventory.plans[0].leave[c]),
            "net plans never pay for redundant take-and-sacrifice cycles of the same color");
    auto fullTable = board; fullTable.spinnerColors = 63;
    const auto fullInventory = PHOrbPlanAssistFishing(fullTable,options,evidence,5);
    Check(fullInventory.count && fullInventory.plans[0].leave[2] == 5 &&
        fullInventory.visitedStates <= PHOrbAssistFishingMaxInventoryStates,
        "six-color alternatives cannot crowd out the five nearby movable donors in favor of taped higher-Combo supplies");
    auto plusHeart = board; plusHeart.types[24] = 2; plusHeart.spinnerColors = 44;
    Check(!PHOrbPlanAssistFishing(plusHeart,options,evidence,5).count,
        "five light takes cannot hide an additional missing heart");
    const auto six = PHOrbPlanAssistFishing(plusHeart,options,evidence,6);
    Check(six.count == 1 && six.plans[0].exchangeCount == 6 && six.plans[0].take[3] == 5 &&
        six.plans[0].take[5] == 1 && six.plans[0].leave[2] == 6,
        "one shared six-take plan fills light and heart shortages together");
    plusHeart.spinnerColors = 12;
    Check(!PHOrbPlanAssistFishing(plusHeart,options,evidence,6).count,
        "a missing color outside the spinner table cannot be fished");

    auto crowded = options;
    crowded.goals.items[crowded.goals.count++] = {PHOrbGoalCombo,0,3,0};
    crowded.goals.items[crowded.goals.count++] = {PHOrbGoalColors,32,1,0};
    crowded.goals.items[crowded.goals.count++] = {PHOrbGoalMatch,8,1,3};
    crowded.goals.items[crowded.goals.count++] = {PHOrbGoalExact,8,1,5};
    const auto overflow = PHOrbPlanAssistFishing(board,crowded,evidence,5);
    Check(overflow.valid && overflow.goalLimit && !overflow.count,
        "a seventh recovery goal is rejected without deleting an original condition");
    auto both = board;
    for (unsigned cell : {0u,1u,2u,6u,8u}) both.types[cell] = 4;
    auto bothEvidence = evidence; bothEvidence.perColor[4] = 4; bothEvidence.providers[4] = 2;
    const auto colors = PHOrbPlanAssistFishing(both,options,bothEvidence,5);
    bool light = false, dark = false;
    for (unsigned p = 0; p < colors.count; ++p) {
        light |= colors.plans[p].lCounts[3] != 0; dark |= colors.plans[p].lCounts[4] != 0;
        Check(colors.plans[p].predictedTurns >= evidence.turns, "all color allocations cover the full current duration");
    }
    Check(light && dark && colors.minimumExchanges == 0,
        "an existing dark L and a five-take light L remain distinct recovery alternatives");
    bothEvidence.turns = 8;
    const auto combined = PHOrbPlanAssistFishing(both,options,bothEvidence,5);
    Check(combined.count == 1 && combined.plans[0].lCounts[3] == 1 && combined.plans[0].lCounts[4] == 1 &&
        combined.plans[0].predictedTurns == 8 && combined.plans[0].take[3] == 5 &&
        combined.plans[0].providers == 3, "multi-color L recovery and original shapes share one inventory plan");
    auto alreadyL = options;
    alreadyL.goals.items[alreadyL.goals.count++] = {PHOrbGoalL,8,1,0};
    const auto reused = PHOrbPlanAssistFishing(board,alreadyL,evidence,5);
    Check(reused.count == 1 && reused.plans[0].goals.count == alreadyL.goals.count &&
        reused.plans[0].lCounts[3] == 1 && reused.plans[0].predictedTurns == 4,
        "an existing same-color L is reused rather than demanding a second group or a duplicate goal");
    auto unmatchable = board; unmatchable.unmatchableColors = 8;
    Check(!PHOrbPlanAssistFishing(unmatchable,options,evidence,5).count,
        "fishing cannot make an unmatchable recovery color count as an eligible L");
    auto broad = ShapeBoard(7,6,{});
    for (unsigned cell = 0; cell < 42; ++cell) broad.types[cell] = uint8_t(cell % 6);
    broad.spinnerMask = UINT64_C(1) << 41; broad.spinnerColors = 63;
    auto broadOptions = options; broadOptions.fishCell = 41; broadOptions.goals = Goals(PHOrbGoalCombo,0,14);
    auto broadEvidence = evidence; broadEvidence.turns = 1;
    for (unsigned c = 0; c < 5; ++c) { broadEvidence.perColor[c] = 1; broadEvidence.providers[c] = 1; }
    const auto capped = PHOrbPlanAssistFishing(broad,broadOptions,broadEvidence,6);
    Check(capped.valid && capped.stateLimitReached && !capped.enumerationComplete && !capped.deadlineReached &&
        !capped.count && capped.visitedStates <= PHOrbAssistFishingMaxInventoryStates,
        "broad five-color L choices share one inventory state cap and a bounded miss is reported as incomplete");

    options.steps = 10; options.milliseconds = PHOrbMaxSearchMilliseconds;
    std::atomic_bool cancel{false}; PHOrbAssistFishingResult found;
    Check(PHOrbSolveAssistFishing(board,options,evidence,timing,5,cancel,found) == PHOrbSolveStatus::Solved &&
        found.route.exchangeCount == 5 && found.route.count == 11 && found.evaluation.goalsMet &&
        found.reservation.fits && PHOrbEvaluateAssistFishing(board,found.route,options,evidence,evaluation) && evaluation.goalsMet,
        "bounded joint search chooses an exchange order and finds the ten-move five-light recovery witness");
    Check(PHOrbSolveAssistFishing(fullTable,options,evidence,timing,5,cancel,found) == PHOrbSolveStatus::Solved &&
        found.route.exchangeCount == 5 && found.route.count == 11 && found.reservation.requiredFrames == 521 &&
        PHOrbEvaluateAssistFishing(fullTable,found.route,options,evidence,evaluation) && evaluation.goalsMet,
        "the same five-take search works with all six spinner colors and reserves all six-color waiting cycles");
    timing.remainingFrames = 221; // Two colors: 150 wait + 20 moves + 48 settle + 3 input.
    Check(PHOrbSolveAssistFishing(board,options,evidence,timing,5,cancel,found) == PHOrbSolveStatus::NoRoute &&
        found.plansTimeRejected == 1 && !found.route.count && !found.evaluated,
        "a route exactly at its assumed frame budget is rejected before beam search");
    timing.remainingFrames = 570;
    auto blocked = board; blocked.fixedStart = 1; blocked.types[0] = 2; TapeExcept(blocked,{0,1,14});
    Check(PHOrbSolveAssistFishing(blocked,options,evidence,timing,5,cancel,found) == PHOrbSolveStatus::NoRoute &&
        found.inventory.count && !found.route.count, "enough color inventory never proves a route through tape exists");
    cancel = true;
    Check(PHOrbSolveAssistFishing(board,options,evidence,timing,5,cancel,found) == PHOrbSolveStatus::Cancelled && !found.route.count,
        "cancelled multi-take recovery never publishes a route");
    cancel = false; options.milliseconds = 1;
    Check(PHOrbSolveAssistFishing(board,options,evidence,timing,5,cancel,found) == PHOrbSolveStatus::NoRoute &&
        found.searchBudgetExhausted && !found.route.count, "inventory, L choices and search share the same tiny total deadline");
    options.milliseconds = 1000;
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        auto unsupportedBoard = board; auto unsupportedOptions = options; auto unsupportedEvidence = evidence;
        if (scenario == 0) unsupportedBoard.flags[13] |= PHOrbFlagLocked;
        if (scenario == 1) unsupportedBoard.flags[13] |= PHOrbFlagThorn;
        if (scenario == 2) unsupportedEvidence.dungeonFlags |= 8;
        if (scenario == 3) unsupportedEvidence.valid = false;
        if (scenario == 4) unsupportedOptions.activity.comboPriority = true;
        if (scenario == 5) unsupportedEvidence.providers[3] = 0;
        Check(PHOrbSolveAssistFishing(unsupportedBoard,unsupportedOptions,unsupportedEvidence,timing,5,cancel,found) ==
            PHOrbSolveStatus::Invalid && !found.route.count,
            "locks, thorns, prohibited/unknown recovery, activity scoring and missing providers remain unsupported");
    }
}
