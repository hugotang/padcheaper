// Exact B4 start boards from coral-b4-20261119.json. Never linked into the dylib.
// Search/replay evidence only; no native clock, target, damage or clear receipt.
#include "PHOrbMultiFishing.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

int main(int argc, char **argv) {
    const bool single=argc==2 && !std::strcmp(argv[1],"--single");
    const bool multi=argc==2 && !std::strcmp(argv[1],"--multi");
    if (argc>2 || (argc==2 && !single && !multi)) return 1;
    const uint8_t cells[2][42]={
        {1,5,3,3,1,2,4, 4,2,4,5,1,5,5, 4,5,4,3,1,0,5,
         4,3,3,3,1,2,1, 5,2,0,0,1,3,4, 5,3,0,4,0,4,1},
        {0,1,5,0,4,5,2, 3,5,3,2,3,4,0, 0,0,1,4,4,2,5,
         0,1,0,2,0,2,3, 1,4,3,2,5,5,2, 3,2,3,1,5,1,4}};
    const uint64_t enhanced[2]={UINT64_C(0x360bdd732),UINT64_C(0x1062cb18d38)};
    bool missed=false;
    std::atomic_bool cancel{false};
    for (unsigned b=0;b<2;++b) {
        PHOrbBoard board{}; board.width=7; board.height=6; board.minErase=3;
        std::copy(std::begin(cells[b]),std::end(cells[b]),board.types);
        for (unsigned i=0;i<42;++i) {
            board.tokens[i]=i+1; board.x[i]=int16_t((i%7)*40+20); board.y[i]=int16_t((i/7)*40+20);
            board.enhancement[i]=(enhanced[b] & (UINT64_C(1)<<i)) ? 1031127695 : 0;
        }
        board.spinnerMask=128; board.spinnerColors=63;
        auto options=PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
        options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true,0,true};
        options.goals.count=6;
        for (unsigned c=0;c<6;++c) options.goals.items[c]={PHOrbGoalFour,uint8_t(1u<<c),1,0};
        for (unsigned budget : {800u,2000u}) for (unsigned trial=0;trial<3;++trial) {
            if (multi) {
                options.milliseconds=budget; options.fishCell=7;
                // Clock snapshot at the stop: 810 frames, 30fps and 30-frame
                // spinner period. Applying it to both starts is conditional,
                // not a claim that the earlier turn had the same live clock.
                const PHOrbTimedFishingTiming timing{810,2,6,12,30};
                PHOrbMultiFishingResult result{};
                const auto status=PHOrbSolveMultiFishing(board,options,nullptr,timing,
                    PHOrbMultiFishingMaxExchanges,cancel,result,budget==800 ? 2 : 0);
                PHOrbEvaluation value{};
                const bool replay=result.route.count && PHOrbEvaluateMultiFishing(board,result.route,options,nullptr,value);
                const bool solved=status==PHOrbSolveStatus::Solved;
                const auto reservation=PHOrbReserveMultiFishing(board,result.route,timing);
                if (status==PHOrbSolveStatus::Invalid || status==PHOrbSolveStatus::Failed ||
                    (solved && (!replay || !value.goalsMet || !value.fixedBoardSatisfied ||
                        !value.hazardsSafe || value.firstWaveFourColors!=63 || !reservation.fits || result.route.count>49))) return 1;
                missed |= !solved;
                std::printf("{\"kind\":\"multi\",\"turn\":%u,\"budgetMS\":%u,\"trial\":%u,\"status\":%u,\"solved\":%s,\"replayed\":%s,\"steps\":%u,\"elapsedMS\":%u,\"evaluated\":%u,\"firstFourColors\":%u,\"firstFours\":%u,\"exchanges\":%u,\"reservedFrames\":%u,\"conditionalClock\":true,\"deviceVerified\":false}\n",
                    b+12,budget,trial,unsigned(status),solved ? "true":"false",replay ? "true":"false",
                    result.route.count ? result.route.count-1 : 0,result.elapsedMS,result.evaluated,
                    value.firstWaveFourColors,value.firstWaveFours,result.route.exchangeCount,reservation.requiredFrames);
                std::fflush(stdout); continue;
            }
            PHOrbFishingPlan fishing{};
            if (single) { options.fishCell=7; fishing=PHOrbPlanFishing(board,options); if (!fishing.valid) return 1; }
            for (unsigned choice=0;choice<(single ? fishing.selectedCount : 1);++choice) {
            if (single) options.fishColor=fishing.candidates[choice].color;
            options.milliseconds=budget; PHOrbRoute route{};
            const auto status=PHOrbSolve(board,options,cancel,route);
            PHOrbEvaluation value{}; uint32_t damage=0; uint8_t contacts=0;
            const bool replay=route.count && PHOrbEvaluateRoute(board,route,options,value,damage,contacts);
            const bool solved=status==PHOrbSolveStatus::Solved;
            if (status==PHOrbSolveStatus::Invalid || status==PHOrbSolveStatus::Failed ||
                (solved && (!replay || !value.goalsMet || !value.fixedBoardSatisfied ||
                    value.firstWaveFourColors!=63 || route.count>49 || damage || contacts))) return 1;
            missed |= !solved;
            std::printf("{\"kind\":\"%s\",\"candidateColor\":%d,\"turn\":%u,\"budgetMS\":%u,\"trial\":%u,\"status\":%u,\"solved\":%s,\"replayed\":%s,\"steps\":%u,\"elapsedMS\":%u,\"evaluated\":%u,\"firstColors\":%u,\"firstFourColors\":%u,\"firstFours\":%u,\"firstCombos\":%u,\"combos\":%u,\"deviceVerified\":false,\"path\":[",
                single ? "single":"ordinary",single ? int(options.fishColor):-1,b+12,budget,trial,unsigned(status),solved ? "true":"false",replay ? "true":"false",
                route.count ? route.count-1 : 0,route.elapsedMS,route.evaluated,value.firstWaveColors,
                value.firstWaveFourColors,value.firstWaveFours,value.firstWaveCombos,value.combos);
            for (unsigned i=0;i<route.count;++i) std::printf("%s%u",i ? ",":"",unsigned(route.cells[i]));
            std::puts("]}"); std::fflush(stdout);
            }
        }
    }
    return missed ? 2 : 0;
}
