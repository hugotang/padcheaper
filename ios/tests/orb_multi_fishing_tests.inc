// Shared multi-take cases. These are written regression scenarios, not device
// evidence; the recorded poison board's inventory is not a solved-route claim.
static PHOrbBoard MultiFishingRecordedHeartBoard() {
    auto board = ShapeBoard(6,5,{});
    const uint8_t types[] = {3,2,3,2,3,4, 4,0,8,8,3,4, 0,4,5,5,3,1,
                             2,1,2,8,4,3, 3,8,0,3,1,3};
    std::copy(std::begin(types),std::end(types),board.types);
    board.spinnerMask = UINT64_C(1) << 5; board.spinnerColors = 63;
    return board;
}
static PHOrbSolveOptions MultiFishingHeartOptions() {
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.fishCell = 5; options.diagonal = true;
    options.goals.count = 2;
    options.goals.items[0] = {PHOrbGoalT,8,1,0};
    options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    options.allowPoisonWithHeartFour = true;
    return options;
}
// 2026-11-08 Himmel device log, wave 3: two water/dark/heart spinners (cells
// 13 and 16), two fixed hearts, five colours plus a heart three. On device
// each cell received 400 ms and every plan slice expired; offline the same
// board solves with one heart take once a plan has enough time. The solo
// Session retry (PHOrbShouldRetryMultiFishing) supplies 2000 ms without the
// geometry reserve. CI 157 showed host speed decides the exact threshold, so
// this regression asserts solvability with the maximum budget, not timing.
static void MultiFishingRecordedDoubleSpinnerTests() {
    PHOrbBoard board{}; board.width = 6; board.height = 5;
    const uint8_t types[] = {0,1,2,1,4,0, 3,3,4,4,4,2, 0,1,1,4,5,0, 1,5,0,3,2,4, 3,3,3,5,1,3};
    for (unsigned i = 0; i < 30; ++i) {
        board.types[i] = types[i]; board.tokens[i] = i + 1;
        board.x[i] = int16_t(50 + i % 6 * 100); board.y[i] = int16_t(300 + i / 6 * 100);
    }
    board.spinnerMask = (UINT64_C(1) << 13) | (UINT64_C(1) << 16); board.spinnerColors = 0x32;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.diagonal = true; options.goals.count = 2;
    options.goals.items[0] = {PHOrbGoalColors,31,5,0}; options.goals.items[1] = {PHOrbGoalMatch,32,1,3};
    Check(PHOrbInspectInventory(board,options).kind == PHOrbInventoryKind::GoalCapacity,
        "recorded double-spinner board lacks a fixed heart three without a take");
    const PHOrbTimedFishingTiming timing{480,2,6,12,30};
    for (int cell : {13,16}) {
        auto trial = options; trial.fishCell = cell; trial.milliseconds = PHOrbMaxSearchMilliseconds;
        std::atomic_bool cancel{false}; PHOrbMultiFishingResult result{};
        const auto status = PHOrbSolveMultiFishing(board,trial,nullptr,timing,PHOrbMultiFishingMaxExchanges,cancel,result,0);
        PHOrbBoard replay{}; PHOrbEvaluation evaluation{};
        Check(status == PHOrbSolveStatus::Solved && result.evaluation.goalsMet && result.evaluation.hazardsSafe &&
            result.route.exchangeCount >= 1 && result.reservation.fits &&
            PHOrbReplayMultiFishing(board,result.route,result.route.count - 1,replay) &&
            PHOrbEvaluateMultiFishing(board,result.route,trial,nullptr,evaluation) && evaluation.goalsMet,
            "each recorded spinner entry solves with one baseline take plan and replays");
    }
    Check(PHOrbShouldRetryMultiFishing(true,false,800,false,true) &&
        !PHOrbShouldRetryMultiFishing(false,false,800,false,true) &&
        !PHOrbShouldRetryMultiFishing(true,true,800,false,true) &&
        !PHOrbShouldRetryMultiFishing(true,false,PHOrbSoloRetryMilliseconds,false,true) &&
        !PHOrbShouldRetryMultiFishing(true,false,800,true,true) &&
        !PHOrbShouldRetryMultiFishing(true,false,800,false,false),
        "multi-take retries once only for an unsolved, time-limited, uncancelled solo search below the retry budget");
}
static void MultiFishingTests() {
    MultiFishingRecordedDoubleSpinnerTests();
    auto board = MultiFishingRecordedHeartBoard(); auto options = MultiFishingHeartOptions();
    const auto one = PHOrbPlanMultiFishing(board,options,nullptr,1);
    const auto two = PHOrbPlanMultiFishing(board,options,nullptr,2);
    Check(one.valid && one.enumerationComplete && !one.count && two.valid && two.count &&
        two.minimumExchanges == 2 && two.fixed[3] == 9 && two.fixed[5] == 2,
        "recorded two-heart/four-mortal board needs two net heart takes; poison does not reject ordinary multi planning");
    for (unsigned p = 0; p < two.count; ++p) {
        const auto &plan = two.plans[p];
        Check(plan.exchangeCount == 2 && plan.take[5] == 2 && !plan.leave[5] &&
            !plan.predictedTurns && !plan.providers && plan.goals.count == 2,
            "ordinary plans preserve T and heart-four without inventing recovery L or sacrificing required hearts");
        unsigned takes = 0, leaves = 0;
        for (unsigned c = 0; c < 6; ++c) { takes += plan.take[c]; leaves += plan.leave[c]; }
        Check(takes == 2 && leaves == 2 && plan.leave[3] <= 4,
            "all ordinary takes are paid with real basic-color donors while reserving the five-light T");
    }

    auto restricted = board; restricted.spinnerColors = 7; restricted.types[5] = 0;
    const auto absent = PHOrbPlanMultiFishing(restricted,options,nullptr,6);
    Check(absent.valid && absent.enumerationComplete && !absent.count && !absent.visitedStates,
        "fire-water-wood-only spinner rejects a missing heart in inventory before path search");
    restricted.spinnerColors = 1;
    Check(!PHOrbPlanMultiFishing(restricted,options,nullptr,6).valid,
        "a singleton spinner color is not a fishing source and cannot create an endless wait");
    auto invalidRecovery = AssistFishingEvidence(0);
    Check(!PHOrbPlanMultiFishing(board,options,&invalidRecovery,6).valid,
        "a nonnull recovery request must be active; nullptr is the explicit ordinary mode");

    auto multi = board;
    multi.spinnerMask |= (UINT64_C(1) << 0) | (UINT64_C(1) << 29);
    const auto three = PHOrbPlanMultiFishing(multi,options,nullptr,2);
    Check(three.valid && three.count && three.minimumExchanges == 2 && three.fixed[3] == 7 && three.fixed[5] == 2,
        "three-spinner inventory excludes all dynamic cells from donors and selects one target for two-heart fishing");
    auto legacyRecovery = AssistFishingEvidence();
    Check(!PHOrbPlanAssistFishing(board,options,legacyRecovery,6).valid &&
        !PHOrbPlanAssistFishing(multi,options,legacyRecovery,6).valid,
        "legacy assist API preserves its original basic-token/single-spinner validation contract");

    // A known five-take route is ordinary light L/T/four when the complete
    // goals are supplied directly; it needs no invented active void status.
    auto known = AssistFishingFiveBoard(); auto knownRoute = AssistFishingFiveWitness();
    auto knownOptions = AssistFishingOptions(); knownOptions.goals = knownRoute.plan.goals;
    knownRoute.plan.predictedTurns = knownRoute.plan.providers = 0;
    PHOrbBoard replay{}; PHOrbEvaluation evaluation{};
    Check(PHOrbReplayMultiFishing(known,knownRoute,knownRoute.count - 1,replay) &&
        PHOrbEvaluateMultiFishing(known,knownRoute,knownOptions,nullptr,evaluation) && evaluation.goalsMet,
        "shared ordinary evaluation accepts an explicit five-exchange witness while retaining its user-supplied L");
    auto withPoison = known; withPoison.types[0] = PHOrbMortalPoison;
    Check(PHOrbReplayMultiFishing(withPoison,knownRoute,knownRoute.count - 1,replay) &&
        replay.types[0] == PHOrbMortalPoison && replay.tokens[0] == withPoison.tokens[0],
        "nonbasic tokens and their identities survive replay without entering the basic-color spinner");
    auto inPlace = withPoison;
    Check(PHOrbReplayMultiFishing(inPlace,knownRoute,0,inPlace) && PHOrbSameBoard(&inPlace,&withPoison),
        "multi replay permits aliased input/output and validates the entire route before returning prefix zero");
    auto badDonor = known; badDonor.types[13] = PHOrbMortalPoison;
    Check(!PHOrbReplayMultiFishing(badDonor,knownRoute,1,replay),
        "a nonbasic future donor invalidates the whole route even when asking for an earlier prefix");
    auto badHeld = known; badHeld.types[7] = PHOrbMortalPoison;
    Check(!PHOrbReplayMultiFishing(badHeld,knownRoute,1,replay),
        "a held poison token cannot enter a spinner whose color table contains only basic colors");
    auto blocked = known; blocked.spinnerMask |= UINT64_C(1) << 13;
    Check(!PHOrbReplayMultiFishing(blocked,knownRoute,1,replay),
        "replay never crosses an unselected spinner even when another selected spinner has a valid take");
    auto impossibleColor = known; impossibleColor.spinnerColors = 36; impossibleColor.types[14] = 2;
    Check(!PHOrbReplayMultiFishing(impossibleColor,knownRoute,1,replay),
        "every requested take must exist in the actual restricted spinner table");
    impossibleColor = known; impossibleColor.spinnerColors = 40; impossibleColor.types[14] = 3;
    Check(!PHOrbReplayMultiFishing(impossibleColor,knownRoute,1,replay),
        "every donor and held entry must also belong to that spinner's restricted table");

    auto plusHeart = known; plusHeart.types[24] = 2; plusHeart.spinnerColors = 44;
    const auto mixed = PHOrbPlanMultiFishing(plusHeart,knownOptions,nullptr,6);
    Check(mixed.count && mixed.minimumExchanges == 6 && mixed.plans[0].take[3] == 5 &&
        mixed.plans[0].take[5] == 1 && mixed.plans[0].leave[2] == 6,
        "one ordinary plan jointly fills light and heart shortages within its wood-light-heart table");
    plusHeart.spinnerColors = 12;
    Check(!PHOrbPlanMultiFishing(plusHeart,knownOptions,nullptr,6).count,
        "an unavailable heart cannot be hidden by a feasible light-take part of the same multicolor request");

    // Under the initial shared phase, one of two disjoint heart-fours always
    // exists. Resetting the selected spinner's phase can remove both; a poison
    // triple then makes that previously correlated assumption unsafe.
    auto phaseBoard = ShapeBoard(6,5,{});
    for (unsigned i = 0; i < 30; ++i) phaseBoard.types[i] = uint8_t(1 + i % 2);
    phaseBoard.types[0] = 0; phaseBoard.types[24] = 5;
    for (unsigned i : {1u,2u,3u,25u,26u,27u}) phaseBoard.types[i] = 5;
    for (unsigned i : {12u,13u,14u}) phaseBoard.types[i] = PHOrbMortalPoison;
    phaseBoard.types[10] = phaseBoard.types[11] = 1;
    phaseBoard.spinnerMask = (UINT64_C(1) << 0) | (UINT64_C(1) << 24); phaseBoard.spinnerColors = 33;
    auto phaseOptions = MultiFishingHeartOptions(); phaseOptions.fishCell = 0;
    phaseOptions.goals = Goals(PHOrbGoalFour,32);
    PHOrbMultiFishingRoute phaseRoute; phaseRoute.count = 2; phaseRoute.cells[0] = 10; phaseRoute.cells[1] = 11;
    phaseRoute.fishCell = 0; phaseRoute.diagonal = true; phaseRoute.plan.goals = phaseOptions.goals;
    PHOrbEvaluation shared{};
    Check(PHOrbEvaluateWithOptions(phaseBoard,phaseOptions,shared) && shared.goalsMet && shared.hazardsSafe,
        "fixture has one first-wave heart-four in either original shared spinner phase");
    Check(PHOrbEvaluateMultiFishing(phaseBoard,phaseRoute,phaseOptions,nullptr,evaluation) &&
        !evaluation.goalsMet && !evaluation.heartFours && !evaluation.hazardsSafe && evaluation.mortalPoisonErased >= 3,
        "independent selected-spinner phases cannot borrow a heart-four from stale shared correlation for poison safety");
    auto beforeExchange = phaseBoard; beforeExchange.types[6] = beforeExchange.types[7] = 0;
    PHOrbRoute nativeRoute{}; nativeRoute.count = 3; nativeRoute.cells[0] = 6; nativeRoute.cells[1] = 0; nativeRoute.cells[2] = 7;
    nativeRoute.diagonal = nativeRoute.fishing = true; nativeRoute.fishCell = 0; nativeRoute.fishColor = 5;
    nativeRoute.fishingExchangeCount = 1; nativeRoute.fishingExchanges[0] = {1,2,5,0};
    uint32_t thornDamage = UINT32_MAX; uint8_t thornContacts = UINT8_MAX;
    Check(PHOrbReplay(&beforeExchange,&nativeRoute,nativeRoute.count - 1,&replay) &&
        PHOrbEvaluateWithOptions(replay,phaseOptions,shared) && shared.goalsMet && shared.hazardsSafe &&
        PHOrbEvaluateRoute(beforeExchange,nativeRoute,phaseOptions,evaluation,thornDamage,thornContacts) &&
        !evaluation.goalsMet && !evaluation.hazardsSafe && !evaluation.heartFours && !thornDamage && !thornContacts,
        "generic native route verification cannot accept the correlated replay phase after an explicit exchange");
    PHOrbEvaluation direct{};
    Check(PHOrbEvaluateMultiFishingBoard(replay,0,phaseOptions,direct) &&
        direct.goalsMet == evaluation.goalsMet && direct.hazardsSafe == evaluation.hazardsSafe &&
        direct.heartFours == evaluation.heartFours && direct.mortalPoisonErased == evaluation.mortalPoisonErased,
        "native route and shared planner use one independent final-board evaluation contract");
    auto unsupportedActivity = phaseOptions; unsupportedActivity.activity.firstWaveOnly = true;
    Check(!PHOrbEvaluateMultiFishingBoard(replay,0,unsupportedActivity,direct),
        "independent fishing evaluation cannot silently drop unsupported activity goals");
    for (unsigned chosen : {0u,5u}) for (unsigned other : {0u,5u}) {
        auto concrete = phaseBoard; concrete.types[0] = uint8_t(chosen); concrete.types[24] = uint8_t(other);
        concrete.spinnerMask = concrete.spinnerColors = 0;
        const auto groups = AssistFishingFirstGroups(concrete);
        const bool heartFour = std::find(groups.begin(),groups.end(),std::set<unsigned>{0,1,2,3}) != groups.end() ||
            std::find(groups.begin(),groups.end(),std::set<unsigned>{24,25,26,27}) != groups.end();
        Check(heartFour == (chosen == 5 || other == 5),
            "independent line/group oracle covers every selected-color and untouched shared-phase combination");
    }

    PHOrbTimedFishingTiming timing{390,2,8,3,15};
    const auto reserved = PHOrbReserveMultiFishing(known,knownRoute,timing);
    Check(reserved.valid && !reserved.fits && reserved.takeWaitFrames == 450,
        "native candidate reservation charges all five waits even when its ordinary goals need no recovery state");
    PHOrbMultiFishingResult result{}; std::atomic_bool cancel{true};
    Check(PHOrbSolveMultiFishing(board,options,nullptr,timing,2,cancel,result) == PHOrbSolveStatus::Cancelled && !result.route.count,
        "cancelled ordinary multi-take solve never publishes an executable route");
}
