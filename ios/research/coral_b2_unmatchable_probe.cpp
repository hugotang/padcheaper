// Recorded B2 board, conditional on fresh native fire/wood eligibility.
// Reports search limits; only invalid APIs or an invalid solved replay fail.
// Never linked into the dylib; does not prove native timing or damage.
#include "PHOrbSolver.h"
#include <algorithm>
#include <cstdio>
#include <iterator>

int main() {
    PHOrbBoard board{}; board.width=7; board.height=6;
    const uint8_t cells[]={1,2,3,1,1,1,4, 2,5,0,4,2,2,4, 1,3,3,0,0,3,5,
        4,0,1,2,3,0,0, 3,5,1,2,3,2,5, 2,4,4,5,1,0,0};
    std::copy(std::begin(cells),std::end(cells),board.types);
    for (unsigned i=0;i<42;++i) { board.tokens[i]=i+1; board.x[i]=int16_t((i%7)*40+20); board.y[i]=int16_t((i/7)*40+20); }
    board.unmatchableColors=16; board.spinnerMask=UINT64_C(1)<<39; board.spinnerColors=63;
    auto options=PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true,5};
    options.goals.count=3; options.goals.items[0]={PHOrbGoalColors,63,6,0};
    options.goals.items[1]={PHOrbGoalFour,32,1,0}; options.goals.items[2]={PHOrbGoalFour,5,1,0};
    std::atomic_bool cancel{false};
    for (unsigned budget : {800u,2000u}) for (unsigned trial=0;trial<3;++trial) {
        options.milliseconds=budget; PHOrbRoute route{};
        const auto status=PHOrbSolve(board,options,cancel,route);
        PHOrbEvaluation value{}; uint32_t damage=0; uint8_t contacts=0;
        const bool replay=route.count && PHOrbEvaluateRoute(board,route,options,value,damage,contacts);
        if (status==PHOrbSolveStatus::Invalid || status==PHOrbSolveStatus::Failed ||
            (status==PHOrbSolveStatus::Solved && (!replay || !value.goalsMet || !value.fixedBoardSatisfied))) return 1;
        std::printf("{\"budgetMS\":%u,\"trial\":%u,\"status\":%u,\"solved\":%s,\"replayed\":%s,\"steps\":%u,\"elapsedMS\":%u,\"evaluated\":%u,\"firstColors\":%u,\"recoveredColors\":%u,\"firstFours\":%u,\"firstCombos\":%u,\"combos\":%u,\"deviceVerified\":false}\n",
            budget,trial,unsigned(status),status==PHOrbSolveStatus::Solved ? "true":"false",replay ? "true":"false",
            route.count ? route.count-1 : 0,route.elapsedMS,route.evaluated,value.firstWaveColors,value.recoveredColors,
            value.firstWaveFours,value.firstWaveCombos,value.combos);
    }
}
