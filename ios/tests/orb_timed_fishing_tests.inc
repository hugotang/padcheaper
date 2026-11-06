static void TimedFishingTests() {
    std::atomic_bool cancel{false};
    auto board = ShapeBoard(6,5,{});
    const uint8_t types[] = {1,3,3,5,5,3, 3,3,5,0,3,5, 4,5,0,3,3,3,
                             5,3,0,5,3,5, 4,1,5,2,5,5};
    std::copy(std::begin(types),std::end(types),board.types);
    board.spinnerMask = UINT64_C(1) << 6; board.spinnerColors = 63;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.fishCell = 6; options.diagonal = true; options.goals.count = 3; options.goals.comboFirstWave = true;
    options.goals.items[0] = {PHOrbGoalT,8,1,0};
    options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    options.goals.items[2] = {PHOrbGoalCombo,0,9,0};
    for (unsigned limit : {0u,1u}) {
        const auto inventory = PHOrbInspectTimedFishing(board,options,limit);
        Check(inventory.valid && !inventory.count && !inventory.feasiblePlans,
            "recorded nine-Combo board fails even a selected release color with fewer than two exchanges");
    }
    const auto inventory = PHOrbInspectTimedFishing(board,options,2);
    Check(inventory.valid && inventory.minimumExchanges == 2 && inventory.count &&
        inventory.count <= PHOrbTimedFishingMaxPlans && options.goals.items[2].count == 9,
        "recorded board has bounded two-exchange inventory candidates without lowering the nine-Combo goal");
    for (unsigned p = 0; p < inventory.count; ++p) {
        const auto &plan = inventory.plans[p]; int counts[6] = {3,2,1,10,2,11};
        Check(plan.count == 2, "each retained recorded-board plan really needs two exchanges");
        for (unsigned e = 0; e < plan.count; ++e) {
            Check(counts[plan.exchanges[e].leave] > 0, "a sacrifice must exist before its exchange");
            --counts[plan.exchanges[e].leave]; ++counts[plan.exchanges[e].take];
        }
        ++counts[plan.releaseColor];
        unsigned total = 0;
        for (unsigned color = 0; color < 6; ++color) {
            total += unsigned(counts[color]);
            Check(counts[color] % 3 == (color == 3 ? 2 : color == 5 ? 1 : 0),
                "nine groups with one T and one four require the independently derived color remainders");
        }
        Check(total == 30 && plan.comboUpper == 9 && counts[3] >= 5 && counts[5] >= 4,
            "inventory plans reserve shape costs and never manufacture a thirty-first orb");
    }

    // Explicit two-take witness. Only 2,8,14,9 are movable. Fixed pairs of
    // fire/water/heart each need one orb; two takes plus water capture suffice.
    // At 2 -> 8 take fire, 8 -> 14 leaves wood; 14 -> 8 takes heart,
    // 8 -> 9 leaves wood, and the final spinner color completes water 6,7,8.
    board = ShapeBoard(6,5,{});
    for (unsigned i = 0; i < 30; ++i) { board.types[i] = 2; board.flags[i] = PHOrbFlagTape; }
    board.types[0] = board.types[1] = 0; board.types[6] = board.types[7] = 1;
    board.types[12] = board.types[13] = 5;
    board.tapeMask = (UINT64_C(1) << 30) - 1;
    for (unsigned i : {2u,8u,14u,9u}) { board.flags[i] = 0; board.tapeMask &= ~(UINT64_C(1) << i); }
    board.fixedStart = 3; board.spinnerMask = UINT64_C(1) << 8; board.spinnerColors = 39;
    options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.steps = 4; options.milliseconds = 1000; options.fishCell = 8;
    options.goals.count = 3; options.goals.bonusFirstWave = true;
    options.goals.items[0] = {PHOrbGoalMatch,1,1,3};
    options.goals.items[1] = {PHOrbGoalMatch,2,1,3};
    options.goals.items[2] = {PHOrbGoalMatch,32,1,3};
    PHOrbTimedFishingRoute witness;
    witness.count = 5; witness.fishCell = 8; witness.exchangeCount = 2; witness.releaseColors = 2;
    const uint8_t path[] = {2,8,14,8,9}; std::copy(std::begin(path),std::end(path),witness.cells);
    witness.exchanges[0] = {1,2,0,2}; witness.exchanges[1] = {3,4,5,2};
    PHOrbBoard replay{}; PHOrbEvaluation evaluation{};
    Check(PHOrbReplayTimedFishing(board,witness,1,replay) && replay.types[2] == 0 &&
        replay.tokens[8] == board.tokens[2] && replay.types[14] == 2,
        "prefix replay performs only the first take and preserves held-token identity");
    Check(PHOrbReplayTimedFishing(board,witness,4,replay) && replay.types[2] == 0 && replay.types[14] == 5 &&
        replay.types[8] == 2 && replay.tokens[9] == board.tokens[2] &&
        PHOrbEvaluateTimedFishing(board,witness,options,evaluation) && evaluation.goalsMet && evaluation.hazardsSafe,
        "two-take replay accounts for both sacrifices and evaluates the declared capture color separately");
    auto captured = replay; captured.types[8] = 1; captured.spinnerMask = 0; captured.spinnerColors = 0;
    const auto oracle = EffectOracle(captured);
    Check(oracle.firstCombos == 4 && oracle.erased == 30 && evaluation.firstWaveCombos == oracle.firstCombos &&
        evaluation.erased == oracle.erased,
        "an independent run/group oracle confirms the constructed conditional layout clears all thirty orbs");
    auto broad = witness; broad.releaseColors = 39;
    Check(PHOrbEvaluateTimedFishing(board,broad,options,evaluation) && !evaluation.goalsMet,
        "success at water capture cannot be reported as success at every release color");
    Check(PHOrbEvaluate(replay,options.goals,evaluation) && !evaluation.goalsMet,
        "ordinary evaluation still demands every spinner phase and never inherits conditional success");
    auto broken = witness; broken.exchanges[1].leave = 1;
    Check(!PHOrbReplayTimedFishing(board,broken,4,replay) && !PHOrbReplayTimedFishing(board,broken,1,replay),
        "a wrong but in-table future sacrifice invalidates the entire route, even a requested prefix");
    broken = witness; broken.exchanges[1].entry = 2;
    Check(!PHOrbReplayTimedFishing(board,broken,1,replay), "even prefix validation rejects an out-of-order future take");
    broken = witness; broken.exchangeCount = 1;
    Check(!PHOrbReplayTimedFishing(board,broken,4,replay), "an unrecorded second visit is never a free take");
    broken = witness; broken.count = 4;
    Check(!PHOrbReplayTimedFishing(board,broken,3,replay), "release while holding the spinner is outside the model");
    broken = witness; broken.releaseColors = 8;
    Check(!PHOrbEvaluateTimedFishing(board,broken,options,evaluation), "capture colors outside the native table are invalid");
    broken = witness; broken.cells[4] = 255;
    Check(!PHOrbReplayTimedFishing(board,broken,1,replay), "an out-of-board future cell cannot be hidden behind a valid prefix");
    auto inPlace = board;
    Check(PHOrbReplayTimedFishing(inPlace,witness,1,inPlace) && inPlace.types[2] == 0 &&
        inPlace.tokens[8] == board.tokens[2], "replay can safely use the same input and output board");
    Check(PHOrbReplayTimedFishing(board,witness,0,replay) && PHOrbSameBoard(&board,&replay),
        "a validated zero-step prefix still returns the original board");
    auto unsupported = options; unsupported.activity.comboWeight = 10;
    Check(!PHOrbInspectTimedFishing(board,unsupported).valid &&
        !PHOrbEvaluateTimedFishing(board,witness,unsupported,evaluation),
        "offline fishing never silently discards a non-default activity scoring request");
    auto restricted = board; restricted.minErase = 4;
    Check(PHOrbEvaluateTimedFishing(restricted,witness,options,evaluation) && !evaluation.goalsMet,
        "conditional evaluation retains the native minimum run restriction");
    restricted = board; restricted.minMatch = 4;
    Check(PHOrbEvaluateTimedFishing(restricted,witness,options,evaluation) && !evaluation.goalsMet,
        "conditional evaluation retains the native minimum connected-match restriction");
    restricted = board; restricted.unmatchableColors = 2;
    Check(PHOrbEvaluateTimedFishing(restricted,witness,options,evaluation) && !evaluation.goalsMet,
        "a selected water capture does not bypass water being unmatchable");
    restricted = board; restricted.fixedStart = 10;
    Check(!PHOrbReplayTimedFishing(restricted,witness,4,replay), "timed routes still obey the fixed starting cell");

    PHOrbTimedFishingTiming timing;
    timing.remainingFrames = 1000; timing.framesPerMove = 2;
    timing.settleFramesPerWait = 8; timing.inputAndCaptureFrames = 3; timing.spinnerPeriodFrames = 15;
    const auto reservation = PHOrbReserveTimedFishing(board,witness,timing);
    // Four colors * 15 frames: two take waits + one capture wait. Four moves
    // cost 8, three settling allowances cost 24, and input/capture costs 3.
    Check(reservation.valid && reservation.fits && reservation.moveFrames == 8 &&
        reservation.takeWaitFrames == 120 && reservation.releaseWaitFrames == 60 &&
        reservation.settleFrames == 24 && reservation.requiredFrames == 215,
        "time reservation includes every exchange, final phase wait, settling and input/capture allowance");
    timing.remainingFrames = 215;
    Check(!PHOrbReserveTimedFishing(board,witness,timing).fits,
        "a route exactly consuming the remaining timer has no time margin");
    broad = witness; broad.releaseColors = board.spinnerColors;
    const auto allColorsReservation = PHOrbReserveTimedFishing(board,broad,timing);
    Check(allColorsReservation.valid && allColorsReservation.fits && !allColorsReservation.releaseWaitFrames &&
        allColorsReservation.requiredFrames == 155,
        "phase-independent arithmetic skips the final color wait but never grants goal success");
    timing.framesPerMove = UINT32_MAX;
    Check(!PHOrbReserveTimedFishing(board,witness,timing).valid, "overflowed frame reservations are rejected");
    timing.framesPerMove = 2; timing.settleFramesPerWait = 0;
    Check(!PHOrbReserveTimedFishing(board,witness,timing).valid, "unknown settling allowance cannot be treated as zero");

    PHOrbTimedFishingRoute found;
    Check(PHOrbSolveTimedFishing(board,options,1,cancel,found) == PHOrbSolveStatus::InsufficientOrbs && !found.count,
        "one take cannot fill three fixed color-pair shortages even with a controlled capture color");
    Check(PHOrbSolveTimedFishing(board,options,2,cancel,found) == PHOrbSolveStatus::Solved &&
        found.exchangeCount == 2 && found.count == 5 && found.releaseColors == 2 &&
        (found.evaluatedReleaseColors & found.releaseColors) == found.releaseColors &&
        PHOrbReplayTimedFishing(board,found,found.count - 1,replay) &&
        PHOrbEvaluateTimedFishing(board,found,options,evaluation) && evaluation.goalsMet && evaluation.hazardsSafe,
        "bounded offline search finds the four-step two-take witness without weakening ordinary release gates");
    Check(PHOrbSolveTimedFishing(board,unsupported,2,cancel,found) == PHOrbSolveStatus::Invalid && !found.count,
        "non-default activity options are rejected by the search as well as the evaluator");
    cancel = true;
    Check(PHOrbSolveTimedFishing(board,options,2,cancel,found) == PHOrbSolveStatus::Cancelled && !found.count,
        "cancelled timed fishing has no retained conditional route");
    cancel = false;
    board.flags[14] |= PHOrbFlagLocked;
    Check(!PHOrbInspectTimedFishing(board,options).valid && !PHOrbReplayTimedFishing(board,witness,4,replay),
        "locked moving tokens are excluded until their spinner behavior is modeled");
    board.flags[14] &= ~PHOrbFlagLocked; board.flags[14] |= PHOrbFlagThorn;
    Check(PHOrbSolveTimedFishing(board,options,2,cancel,found) == PHOrbSolveStatus::Invalid,
        "the timed model cannot silently allow thorn contacts");
    board.flags[14] &= ~PHOrbFlagThorn; board.tapeMask |= UINT64_C(1) << 8; board.flags[8] |= PHOrbFlagTape;
    Check(!PHOrbInspectTimedFishing(board,options).valid, "a spinner on tape is never opened by the timed model");

    // Phase-only routing needs no exchange: move between two identical wood
    // cells while fixed water 6,7 waits for spinner 8 to become water.
    board = ShapeBoard(6,5,{});
    std::fill(board.types,board.types + 30,uint8_t(2));
    board.types[6] = board.types[7] = 1;
    board.spinnerMask = UINT64_C(1) << 8; board.spinnerColors = 6; board.fixedStart = 30;
    options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast);
    options.fishCell = 8; options.steps = 1; options.goals.count = 1; options.goals.bonusFirstWave = true;
    options.goals.items[0] = {PHOrbGoalMatch,2,1,3};
    Check(PHOrbSolveTimedFishing(board,options,0,cancel,found) == PHOrbSolveStatus::Solved &&
        !found.exchangeCount && found.count == 2 && found.releaseColors == 2 &&
        PHOrbEvaluateTimedFishing(board,found,options,evaluation) && evaluation.goalsMet,
        "a zero-exchange phase-only route is independently replayed and conditionally evaluated");
    options.goals.items[0].count = 2;
    Check(PHOrbSolveTimedFishing(board,options,0,cancel,found) == PHOrbSolveStatus::InsufficientOrbs && !found.count,
        "phase-only inventory cannot invent enough water for a second matching group");
    options.milliseconds = 1;
    Check(PHOrbSolveTimedFishing(board,options,2,cancel,found) == PHOrbSolveStatus::NoRoute &&
        found.searchBudgetExhausted && !found.count && !found.evaluated,
        "an exhausted preflight budget cannot be misreported as proven insufficient inventory");

    board = ShapeBoard(6,5,{0,1,2,13});
    board.types[7] = 1; board.types[28] = board.types[29] = 2;
    board.spinnerMask = UINT64_C(1) << 7; board.spinnerColors = 3; board.fixedStart = 30;
    options = PHOrbOptionsForQuality(PHOrbSearchQuality::Fast); options.fishCell = 7;
    options.goals = Goals(PHOrbGoalT);
    PHOrbTimedFishingRoute shape;
    shape.count = 2; shape.cells[0] = 29; shape.cells[1] = 28; shape.fishCell = 7; shape.releaseColors = 1;
    Check(PHOrbEvaluateTimedFishing(board,shape,options,evaluation) && evaluation.goalsMet && evaluation.goalHits[0] == 1,
        "a conditional exact T may include the spinner cell itself");
    shape.releaseColors = 3;
    Check(PHOrbEvaluateTimedFishing(board,shape,options,evaluation) && !evaluation.goalsMet && !evaluation.goalHits[0],
        "a T completed only in the fire phase is not credited across fire and water together");
}
