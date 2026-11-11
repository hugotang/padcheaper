// Local comparison driver. Build against the solver under test; never linked
// into the product. Compare JSON output from cached/uncached source copies.
#include "PHOrbMultiFishing.h"
#include <cstdio>
#include <initializer_list>

int main() {
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
    const PHOrbTimedFishingTiming timing{480,2,6,12,30};
    unsigned failures = 0;
    for (unsigned run = 0; run < 5; ++run) for (int cell : {13,16}) {
        options.fishCell = cell; options.milliseconds = PHOrbMaxSearchMilliseconds;
        std::atomic_bool cancel{false}; PHOrbMultiFishingResult result{};
        const auto status = PHOrbSolveMultiFishing(board,options,nullptr,timing,PHOrbMultiFishingMaxExchanges,cancel,result,0);
        PHOrbBoard replay{}; PHOrbEvaluation checked{};
        const bool valid = result.route.count >= 2 && result.reservation.fits &&
            PHOrbReplayMultiFishing(board,result.route,result.route.count - 1,replay) &&
            PHOrbEvaluateMultiFishing(board,result.route,options,nullptr,checked) && checked.goalsMet && checked.hazardsSafe;
        failures += !valid || status != PHOrbSolveStatus::Solved;
        std::printf("{\"run\":%u,\"cell\":%d,\"status\":%u,\"valid\":%s,\"ms\":%u,\"evaluated\":%u,"
            "\"calls\":%u,\"hits\":%u,\"plans\":%u,\"expired\":%u,\"deadline\":%s,\"combos\":%u,\"erased\":%u,\"path\":[",
            run,cell,unsigned(status),valid ? "true" : "false",result.elapsedMS,result.evaluated,result.evaluationCalls,
            result.evaluationCacheHits,result.plansSearched,result.planSlicesExpired,result.deadlineReached ? "true" : "false",
            result.evaluation.combos,result.evaluation.erased);
        for (unsigned i = 0; i < result.route.count; ++i) std::printf("%s%u",i ? "," : "",result.route.cells[i]);
        std::printf("],\"exchanges\":[");
        for (unsigned i = 0; i < result.route.exchangeCount; ++i) {
            const auto &x = result.route.exchanges[i];
            std::printf("%s[%u,%u,%u,%u]",i ? "," : "",x.entry,x.exit,x.take,x.leave);
        }
        std::printf("]}\n");
    }
    return failures ? 1 : 0;
}
