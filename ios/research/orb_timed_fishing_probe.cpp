// Offline recorded-board probe. Build/run only after an explicit test request.
// No device, Session, native gesture or multiplayer API is called here.
#include "PHOrbTimedFishing.h"
#include <algorithm>
#include <cstdio>

static const char *Status(PHOrbSolveStatus status) {
    switch (status) {
        case PHOrbSolveStatus::Solved: return "conditional-solved";
        case PHOrbSolveStatus::GoalsUnmet: return "conditional-goals-unmet";
        case PHOrbSolveStatus::Unsafe: return "unsafe";
        case PHOrbSolveStatus::InsufficientOrbs: return "insufficient-orbs";
        case PHOrbSolveStatus::Cancelled: return "cancelled";
        case PHOrbSolveStatus::Invalid: return "invalid";
        case PHOrbSolveStatus::NoRoute: return "no-route";
        case PHOrbSolveStatus::Failed: return "failed";
        case PHOrbSolveStatus::IncompatibleGoals: return "incompatible-goals";
    }
    return "unknown";
}

int main() {
    PHOrbBoard board{}; board.width = 6; board.height = 5;
    const uint8_t types[] = {1,3,3,5,5,3, 3,3,5,0,3,5, 4,5,0,3,3,3,
                             5,3,0,5,3,5, 4,1,5,2,5,5};
    std::copy(types,types + 30,board.types);
    for (unsigned cell = 0; cell < 30; ++cell) board.tokens[cell] = cell + 1;
    board.spinnerMask = UINT64_C(1) << 6; board.spinnerColors = 63;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.diagonal = true; options.fishCell = 6;
    options.goals.count = 3; options.goals.comboFirstWave = true;
    options.goals.items[0] = {PHOrbGoalT,8,1,0};
    options.goals.items[1] = {PHOrbGoalFour,32,1,0};
    options.goals.items[2] = {PHOrbGoalCombo,0,9,0};

    for (unsigned limit = 0; limit <= 2; ++limit) {
        const auto inventory = PHOrbInspectTimedFishing(board,options,limit);
        std::printf("inventory max_exchanges=%u valid=%d minimum=%u feasible=%u retained=%u\n",
            limit,int(inventory.valid),inventory.minimumExchanges,inventory.feasiblePlans,inventory.count);
        if (!inventory.valid) return 1;
        for (unsigned p = 0; p < inventory.count; ++p) {
            const auto &plan = inventory.plans[p];
            std::printf("  plan=%u capture_color=%u combo_upper=%u",p,unsigned(plan.releaseColor),plan.comboUpper);
            for (unsigned e = 0; e < plan.count; ++e)
                std::printf(" exchange%u=leave%u/take%u",e,unsigned(plan.exchanges[e].leave),unsigned(plan.exchanges[e].take));
            std::puts("");
        }
    }

    std::atomic_bool cancel{false};
    bool invalidResult = false;
    for (unsigned budget : {800u,2000u}) {
        options.milliseconds = budget;
        PHOrbTimedFishingRoute route;
        const auto status = PHOrbSolveTimedFishing(board,options,2,cancel,route);
        std::printf("search budget_ms=%u status=%s elapsed_ms=%u evaluated=%u plans=%u depth=%u "
            "slice_timeouts=%u search_budget_exhausted=%d deadline=%d\n",
            budget,Status(status),unsigned(route.elapsedMS),unsigned(route.evaluated),unsigned(route.plansSearched),
            unsigned(route.exploredDepth),unsigned(route.planSlicesExpired),int(route.searchBudgetExhausted),int(route.deadlineReached));
        if (status == PHOrbSolveStatus::Invalid || status == PHOrbSolveStatus::Failed ||
            status == PHOrbSolveStatus::Cancelled || (status == PHOrbSolveStatus::Solved && !route.count)) invalidResult = true;
        if (!route.count) continue;
        std::printf("  capture_colors=%u evaluated_capture_colors=%u path=",unsigned(route.releaseColors),unsigned(route.evaluatedReleaseColors));
        for (unsigned step = 0; step < route.count; ++step) std::printf("%s%u",step ? "," : "",unsigned(route.cells[step]));
        std::puts("");
        for (unsigned e = 0; e < route.exchangeCount; ++e) {
            const auto &exchange = route.exchanges[e];
            std::printf("  exchange=%u entry=%u exit=%u take=%u leave=%u\n",e,
                unsigned(exchange.entry),unsigned(exchange.exit),unsigned(exchange.take),unsigned(exchange.leave));
        }
        PHOrbBoard replay;
        PHOrbEvaluation evaluation;
        const bool valid = PHOrbReplayTimedFishing(board,route,route.count - 1,replay) &&
            PHOrbEvaluateTimedFishing(board,route,options,evaluation);
        std::printf("  replay_valid=%d\n",int(valid));
        if (!valid) { invalidResult = true; continue; }
        if (status == PHOrbSolveStatus::Solved && (!evaluation.goalsMet || !evaluation.hazardsSafe)) invalidResult = true;
        for (unsigned color = 0; color < 6; ++color) {
            auto phase = route; phase.releaseColors = uint8_t(1u << color);
            if (!PHOrbEvaluateTimedFishing(board,phase,options,evaluation)) { invalidResult = true; continue; }
            std::printf("  capture=%u goals_met=%d safe=%d first_combos=%u goal_hits=%u,%u,%u\n",color,
                int(evaluation.goalsMet),int(evaluation.hazardsSafe),unsigned(evaluation.firstWaveCombos),
                unsigned(evaluation.goalHits[0]),unsigned(evaluation.goalHits[1]),unsigned(evaluation.goalHits[2]));
        }
    }
    // A bounded search may legitimately have no solution. Exit 0 means this
    // probe observed no invalid API result; inspect status for actual success.
    std::puts("Live timing remains unverified; conditional results are not executable routes.");
    return invalidResult ? 1 : 0;
}
