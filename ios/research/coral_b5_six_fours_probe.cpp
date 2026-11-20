// Build/run only with explicit build/test. Exit 0: all searches solved and
// replayed; 2: bounded search acceptance missed; 1: invalid API/replay result.
#include "coral_b5_fixtures.h"
#include <cstdio>

int main() {
    std::atomic_bool cancel{false}; bool missed=false;
    for (const auto &fixture : CoralB5Cases) for (unsigned budget : {800u,2000u}) for (unsigned trial=0;trial<3;++trial) {
        const auto board=CoralB5Board(fixture); auto options=CoralB5Options(fixture); options.milliseconds=budget;
        PHOrbRoute route{};
        const auto status=PHOrbSolve(board,options,cancel,route);
        PHOrbEvaluation value{}; uint32_t damage=0; uint8_t contacts=0;
        const bool replayed=route.count && PHOrbEvaluateRoute(board,route,options,value,damage,contacts);
        const bool solved=status==PHOrbSolveStatus::Solved;
        if (status==PHOrbSolveStatus::Invalid || status==PHOrbSolveStatus::Failed ||
            (route.count && (!replayed || damage || contacts || route.count>49 || route.fishing)) ||
            (solved && (!replayed || !value.goalsMet || !value.fixedBoardSatisfied || !value.hazardsSafe ||
                        value.firstWaveFourColors!=63))) return 1;
        missed |= !solved;
        std::printf("{\"fixture\":\"%s\",\"budgetMS\":%u,\"trial\":%u,\"status\":%u,\"solved\":%s,\"replayed\":%s,\"steps\":%u,\"elapsedMS\":%u,\"firstFourColors\":%u,\"firstFours\":%u,\"firstCombos\":%u,\"repairPasses\":%u,\"repairCandidates\":%u,\"guidePasses\":%u,\"guideCandidates\":%u,\"deviceVerified\":false,\"path\":[",
            fixture.version,budget,trial,unsigned(status),solved ? "true":"false",replayed ? "true":"false",
            route.count ? route.count-1 : 0,route.elapsedMS,value.firstWaveFourColors,value.firstWaveFours,
            value.firstWaveCombos,route.sixFourRepairPasses,route.sixFourRepairCandidates,
            route.sixFourGuidePasses,route.sixFourGuideCandidates);
        for (unsigned i=0;i<route.count;++i) std::printf("%s%u",i ? ",":"",unsigned(route.cells[i]));
        std::puts("]}"); std::fflush(stdout);
    }
    return missed ? 2 : 0;
}
