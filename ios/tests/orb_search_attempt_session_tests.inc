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
}

static void CheckSearchAttemptTrace(NSDictionary *trace) {
    NSArray<NSDictionary *> *attempts = trace[@"candidateAttempts"];
    const NSInteger selected = [trace[@"selectedAttemptIndex"] integerValue];
    Check(attempts.count > 0 && trace[@"selectedAttemptIndex"] && selected >= 0 && NSUInteger(selected) < attempts.count,
        "completed search identifies the selected attempt by its zero-based index");
    if (!attempts.count || selected < 0 || NSUInteger(selected) >= attempts.count) return;
    uint64_t wallMS = 0, evaluated = 0, calls = 0, hits = 0;
    unsigned passes = 0, backtracks = 0, deepest = 0;
    bool deadline = false, complete = true;
    for (NSDictionary *attempt in attempts) {
        for (NSString *key in @[@"budgetMS", @"elapsedMS", @"wallElapsedMS", @"deadline", @"deadlineKnown",
                @"statisticsAvailable", @"evaluated", @"evaluationCalls", @"evaluationCacheHits", @"searchPasses",
                @"backtracks", @"exploredDepth", @"stepLimit", @"beam"])
            Check([attempt[key] isKindOfClass:NSNumber.class], "each attempt retains its own limits and search statistics");
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
        [trace[@"anyAttemptDeadlineReached"] boolValue] == deadline &&
        [trace[@"totalStatisticsComplete"] boolValue] == complete,
        "whole-job totals include all attempts without claiming a complete count for candidate-free failures");
    Check([NSJSONSerialization isValidJSONObject:trace], "attempt diagnostics remain JSON serializable");
}
