static void SearchAttemptPolicyTests() {
    PHOrbRoute candidate{}; candidate.count = 2; candidate.deadlineReached = true;
    for (auto status : {PHOrbSolveStatus::GoalsUnmet, PHOrbSolveStatus::Unsafe}) {
        Check(PHOrbDecideSoloRetry(true,false,false,800,status,candidate) == PHOrbSoloRetryDecision::Retry,
            "a timed-out solo failure with a retained candidate may use the longer budget");
        candidate.deadlineReached = false;
        Check(PHOrbDecideSoloRetry(true,false,false,800,status,candidate) == PHOrbSoloRetryDecision::SearchCompleted,
            "a completed failed search does not repeat the same deterministic candidates");
        candidate.deadlineReached = true;
    }
    for (auto status : {PHOrbSolveStatus::Solved, PHOrbSolveStatus::InsufficientOrbs,
            PHOrbSolveStatus::Cancelled, PHOrbSolveStatus::Invalid, PHOrbSolveStatus::NoRoute,
            PHOrbSolveStatus::Failed, PHOrbSolveStatus::IncompatibleGoals})
        Check(PHOrbDecideSoloRetry(true,false,false,800,status,candidate) == PHOrbSoloRetryDecision::Ineligible,
            "terminal and candidate-free statuses cannot acquire a solo retry from a deadline flag");
    Check(PHOrbDecideSoloRetry(false,false,false,800,PHOrbSolveStatus::GoalsUnmet,candidate) == PHOrbSoloRetryDecision::Ineligible &&
        PHOrbDecideSoloRetry(true,true,false,800,PHOrbSolveStatus::GoalsUnmet,candidate) == PHOrbSoloRetryDecision::Ineligible &&
        PHOrbDecideSoloRetry(true,false,true,800,PHOrbSolveStatus::GoalsUnmet,candidate) == PHOrbSoloRetryDecision::Ineligible &&
        PHOrbDecideSoloRetry(true,false,false,PHOrbSoloRetryMilliseconds,PHOrbSolveStatus::GoalsUnmet,candidate) ==
            PHOrbSoloRetryDecision::Ineligible,
        "multiplayer, assist work, cancellation and an already-extended budget retain their retry exclusions");
    for (unsigned count : {0u,1u,unsigned(PHOrbMaxSteps + 2)}) {
        candidate.count = uint8_t(count);
        Check(PHOrbDecideSoloRetry(true,false,false,800,PHOrbSolveStatus::GoalsUnmet,candidate) == PHOrbSoloRetryDecision::Ineligible,
            "a missing or invalid retained route cannot be treated as a timed-out search candidate");
    }

    // Script the observed ordering rather than depending on a host being slow
    // enough to hit a solver deadline: ordinary shortage, then fishing timeout.
    PHOrbSolveOptions options; options.milliseconds = 800; options.steps = 48; options.beam = 512;
    options.goals.count = 2;
    options.goals.items[0] = {PHOrbGoalT,8,1,0}; options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    options.diagonal = options.allowPoisonWithHeartFour = true;
    std::atomic_bool cancel(false);
    unsigned calls = 0;
    PHOrbSearchWithSoloRetry(options,true,false,cancel,
        [&](const PHOrbSolveOptions &trial, PHOrbRoute &output, bool retry) {
            ++calls;
            Check(trial.fishCell < 0 && trial.milliseconds == 800 && !retry && !output.count,
                "ordinary inventory rejection is one fresh attempt without a retry");
            return PHOrbSolveStatus::InsufficientOrbs;
        });
    Check(calls == 1, "ordinary shortage does not consume or preclude a later fishing retry");
    options.fishCell = 6; options.fishColor = 3; options.steps = 36;
    for (unsigned outcome = 0; outcome < 3; ++outcome) {
        PHOrbRoute selected{}; PHOrbSolveStatus selectedStatus = PHOrbSolveStatus::InsufficientOrbs;
        PHOrbSolveOptions selectedOptions; unsigned selectedIndex = 0;
        calls = 0;
        PHOrbSearchWithSoloRetry(options,true,false,cancel,
            [&](const PHOrbSolveOptions &trial, PHOrbRoute &output, bool retry) {
                const unsigned index = ++calls;
                Check(retry == (index == 2) && trial.milliseconds == (retry ? PHOrbSoloRetryMilliseconds : 800u) &&
                    trial.fishCell == 6 && trial.fishColor == 3 && trial.steps == 36 && trial.beam == 512 &&
                    trial.diagonal && trial.allowPoisonWithHeartFour && trial.goals.count == 2 &&
                    trial.goals.items[0].kind == PHOrbGoalT && trial.goals.items[0].colors == 8 &&
                    trial.goals.items[1].kind == PHOrbGoalFour && trial.goals.items[1].colors == 32 && !output.count,
                    "fishing retry changes only the time budget and starts with a fresh output route");
                PHOrbSolveStatus status = PHOrbSolveStatus::GoalsUnmet;
                if (retry && outcome == 2) status = PHOrbSolveStatus::NoRoute;
                else {
                    output.count = 2; output.cells[0] = 5; output.cells[1] = 6;
                    output.fishing = true; output.fishCell = 6; output.fishColor = 3;
                    output.hazardsSafe = true; output.goalCount = 2; output.matchedGoals = 1;
                    output.goalHits[0] = 1; output.deadlineReached = true;
                    if (retry && outcome == 0) {
                        output.goalsMet = true; output.matchedGoals = 2; output.goalHits[1] = 1;
                        status = PHOrbSolveStatus::Solved;
                    }
                }
                if (PHOrbPreferSearchCandidate(status,output,selectedStatus,selected)) {
                    selected = output; selectedStatus = status; selectedOptions = trial; selectedIndex = index;
                }
                return status;
            });
        Check(calls == 2 && selectedIndex == (outcome == 2 ? 1u : 2u) && selected.fishing &&
            selectedOptions.fishCell == 6 && selectedOptions.fishColor == 3 && selectedOptions.steps == 36 &&
            selectedOptions.milliseconds == (outcome == 2 ? 800u : PHOrbSoloRetryMilliseconds),
            "one fishing retry retains matching route, options and attempt identity even when it finds no candidate");
        Check(selectedStatus == (outcome == 0 ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet) &&
            selected.goalsMet == (outcome == 0),
            "an equal-progress retry is visible without declaring its unfinished goals solved");
        PHOrbRoute absent{};
        Check(!PHOrbPreferSearchCandidate(PHOrbSolveStatus::InsufficientOrbs,absent,selectedStatus,selected),
            "a later fishing colour rejected for shortage cannot hide a useful earlier fishing attempt");
    }

    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        calls = 0; cancel.store(scenario == 0);
        PHOrbSolveOptions trialOptions = options;
        if (scenario == 6) trialOptions.milliseconds = PHOrbSoloRetryMilliseconds;
        PHOrbSearchWithSoloRetry(trialOptions,scenario != 4,scenario == 5,cancel,
            [&](const PHOrbSolveOptions &, PHOrbRoute &output, bool retry) {
                ++calls; Check(!retry, "ineligible fishing searches do not schedule an extended attempt");
                output.count = scenario == 3 ? 0 : 2;
                output.deadlineReached = scenario != 2;
                if (scenario == 1) cancel.store(true);
                return PHOrbSolveStatus::GoalsUnmet;
            });
        Check(calls == (scenario == 0 ? 0u : 1u),
            "cancellation, completed search, absent candidate, multiplayer, assist and extended budgets suppress fishing retries");
    }
    cancel.store(false);

    PHOrbRoute partial{}; partial.count = 2; partial.hazardsSafe = true; partial.matchedGoals = 1;
    PHOrbRoute unsafe = partial; unsafe.hazardsSafe = false; unsafe.matchedGoals = 2;
    PHOrbRoute solved = partial; solved.goalsMet = true; solved.matchedGoals = 2;
    Check(PHOrbPreferSearchCandidate(PHOrbSolveStatus::GoalsUnmet,partial,PHOrbSolveStatus::Unsafe,unsafe) &&
        !PHOrbPreferSearchCandidate(PHOrbSolveStatus::Unsafe,unsafe,PHOrbSolveStatus::GoalsUnmet,partial),
        "failed candidate selection preserves the safer partial route ahead of a hazardous route with more goals");
    Check(PHOrbPreferSearchCandidate(PHOrbSolveStatus::Solved,solved,PHOrbSolveStatus::GoalsUnmet,partial) &&
        !PHOrbPreferSearchCandidate(PHOrbSolveStatus::GoalsUnmet,partial,PHOrbSolveStatus::Solved,solved),
        "a complete safe solution takes precedence and cannot be replaced by a failed fishing route");
    PHOrbRoute less = partial; less.matchedGoals = 0;
    Check(!PHOrbPreferSearchCandidate(PHOrbSolveStatus::GoalsUnmet,less,PHOrbSolveStatus::GoalsUnmet,partial),
        "a longer retry with fewer completed goals does not discard the better retained candidate");

    PHOrbGoalSet colors{}; colors.count = 2;
    colors.items[0] = {PHOrbGoalColors,31,5,0}; colors.items[1] = {PHOrbGoalMatch,32,1,3};
    auto four = partial; four.goalCount = 2; four.goalHits[0] = 4; four.goalHits[1] = 1;
    auto three = four; three.goalHits[0] = 3;
    Check(!PHOrbPreferSearchCandidate(PHOrbSolveStatus::GoalsUnmet,three,PHOrbSolveStatus::GoalsUnmet,four,&colors,&colors) &&
        PHOrbPreferSearchCandidate(PHOrbSolveStatus::GoalsUnmet,four,PHOrbSolveStatus::GoalsUnmet,three,&colors,&colors),
        "recorded single-spinner failure retains four colours plus heart ahead of later three colours plus heart");
    Check(PHOrbPreferSearchCandidate(PHOrbSolveStatus::GoalsUnmet,four,PHOrbSolveStatus::GoalsUnmet,four,&colors,&colors),
        "an actually equal-progress later attempt remains visible");
    auto extra = four; extra.goalHits[1] = 20;
    Check(PHOrbSearchGoalProgress(extra,colors) == PHOrbSearchGoalProgress(four,colors),
        "overfilling an already complete heart clause cannot compensate for a missing attack colour");
    auto different = colors; different.items[0].colors = 15; different.items[0].count = 4;
    Check(!PHOrbComparableSearchGoals(colors,different),
        "different goal masks and requirements have no comparable within-clause diagnostic progress");
    different = colors; different.bonusFirstWave = true;
    Check(!PHOrbComparableSearchGoals(colors,different),"first-removal policy differences prevent unlike progress comparison");
    auto hazardous = four; hazardous.hazardsSafe = false;
    Check(!PHOrbPreferSearchCandidate(PHOrbSolveStatus::Unsafe,hazardous,PHOrbSolveStatus::GoalsUnmet,three,&colors,&colors),
        "within-goal progress never overrides hazard safety");
}

static void CheckSearchAttemptTrace(NSDictionary *trace) {
    NSArray<NSDictionary *> *attempts = trace[@"candidateAttempts"];
    const NSInteger selected = [trace[@"selectedAttemptIndex"] integerValue];
    Check(attempts.count > 0 && trace[@"selectedAttemptIndex"] && selected >= 0 && NSUInteger(selected) < attempts.count,
        "completed search identifies the selected attempt by its zero-based index");
    if (!attempts.count || selected < 0 || NSUInteger(selected) >= attempts.count) return;
    uint64_t wallMS = 0, evaluated = 0, calls = 0, hits = 0;
    unsigned passes = 0, backtracks = 0, deepest = 0, fishingRetries = 0;
    bool deadline = false, complete = true;
    for (NSDictionary *attempt in attempts) {
        for (NSString *key in @[@"budgetMS", @"elapsedMS", @"wallElapsedMS", @"deadline", @"deadlineKnown",
                @"statisticsAvailable", @"evaluated", @"evaluationCalls", @"evaluationCacheHits", @"searchPasses",
                @"backtracks", @"exploredDepth", @"stepLimit", @"beam", @"fishingRequested", @"fishing"])
            Check([attempt[key] isKindOfClass:NSNumber.class], "each attempt retains its own limits and search statistics");
        const bool fishingRequested = [attempt[@"fishingRequested"] boolValue];
        fishingRetries += fishingRequested && [attempt[@"soloRetry"] boolValue];
        const bool multiFishing = [attempt[@"multiFishing"] boolValue];
        NSArray<NSDictionary *> *exchanges = attempt[@"exchanges"];
        const bool hasSingleColor = !multiFishing || exchanges.count;
        Check(fishingRequested ? [attempt[@"fishCell"] isKindOfClass:NSNumber.class] &&
            (hasSingleColor ? [attempt[@"fishColor"] isKindOfClass:NSNumber.class] : !attempt[@"fishColor"]) :
            !attempt[@"fishCell"] && !attempt[@"fishColor"],
            "single-take requests retain their chosen colour; multi-take inventories name no colour without a retained exchange");
        if (multiFishing && exchanges.count) Check([attempt[@"fishColor"] isEqual:exchanges.firstObject[@"take"]],
            "a retained multi-take colour is the first actual exchange, not an unrequested default");
        if ([attempt[@"statisticsAvailable"] boolValue]) {
            Check([attempt[@"path"] isKindOfClass:NSArray.class] && [attempt[@"path"] count] >= 2 &&
                [attempt[@"goalHits"] isKindOfClass:NSArray.class] &&
                [attempt[@"goalHits"] count] == [attempt[@"goals"] count] &&
                [attempt[@"goalCount"] unsignedIntegerValue] == [attempt[@"goals"] count],
                "every retained candidate includes its own path and per-goal result, including failed fishing attempts");
            for (NSString *key in @[@"goalsMet",@"matchedGoals",@"hazardsSafe"])
                Check([attempt[key] isKindOfClass:NSNumber.class], "candidate outcome flags accompany per-goal diagnostics");
        } else {
            Check(!attempt[@"path"] && !attempt[@"goalHits"] && !attempt[@"goalsMet"] &&
                !attempt[@"matchedGoals"] && !attempt[@"goalCount"] && !attempt[@"hazardsSafe"],
                "inventory failures do not fabricate evaluated goal results or an executable path");
        }
        wallMS += [attempt[@"wallElapsedMS"] unsignedLongLongValue];
        evaluated += [attempt[@"evaluated"] unsignedLongLongValue];
        calls += [attempt[@"evaluationCalls"] unsignedLongLongValue];
        hits += [attempt[@"evaluationCacheHits"] unsignedLongLongValue];
        passes += [attempt[@"searchPasses"] unsignedIntValue];
        backtracks += [attempt[@"backtracks"] unsignedIntValue];
        deepest = std::max(deepest,[attempt[@"exploredDepth"] unsignedIntValue]);
        deadline |= [attempt[@"deadline"] boolValue];
        complete &= [attempt[@"statisticsAvailable"] boolValue];
    }
    NSDictionary *chosen = attempts[NSUInteger(selected)];
    for (NSString *key in @[@"elapsedMS", @"deadline", @"deadlineKnown", @"statisticsAvailable", @"evaluated",
            @"evaluationCalls", @"evaluationCacheHits", @"searchPasses", @"backtracks", @"exploredDepth", @"stepLimit", @"beam"])
        Check([trace[key] isEqual:chosen[key]], "selected-route statistics come from the chosen attempt, not the last attempt");
    Check([trace[@"selectedStatus"] isEqual:chosen[@"status"]] && [trace[@"selectedBudgetMS"] isEqual:chosen[@"budgetMS"]] &&
        [trace[@"selectedWallElapsedMS"] isEqual:chosen[@"wallElapsedMS"]] &&
        [trace[@"statisticsScope"] isEqual:@"selected-attempt"] && [trace[@"budgetMS"] isEqual:trace[@"initialBudgetMS"]],
        "initial job budget is distinct from the chosen attempt's budget and elapsed time");
    Check([trace[@"totalElapsedMS"] unsignedLongLongValue] >= wallMS &&
        [trace[@"totalEvaluated"] unsignedLongLongValue] == evaluated &&
        [trace[@"totalEvaluationCalls"] unsignedLongLongValue] == calls &&
        [trace[@"totalEvaluationCacheHits"] unsignedLongLongValue] == hits &&
        [trace[@"totalSearchPasses"] unsignedIntValue] == passes &&
        [trace[@"totalBacktracks"] unsignedIntValue] == backtracks &&
        [trace[@"maximumExploredDepth"] unsignedIntValue] == deepest &&
        [trace[@"fishingRetries"] isKindOfClass:NSNumber.class] &&
        [trace[@"fishingRetries"] unsignedIntValue] == fishingRetries &&
        [trace[@"anyAttemptDeadlineReached"] boolValue] == deadline &&
        [trace[@"totalStatisticsComplete"] boolValue] == complete,
        "whole-job totals include all attempts without claiming a complete count for candidate-free failures");
    Check([NSJSONSerialization isValidJSONObject:trace], "attempt diagnostics remain JSON serializable");
}
