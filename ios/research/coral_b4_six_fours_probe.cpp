// Exact B4 start boards from both retained device reports. Never linked into the dylib.
// Search/replay evidence only; no native clock, target, damage or clear receipt.
#include "coral_b4_fixtures.h"
#include "PHOrbMultiFishing.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

int main(int argc, char **argv) {
    const bool single=argc==2 && !std::strcmp(argv[1],"--single");
    const bool multi=argc==2 && !std::strcmp(argv[1],"--multi");
    if (argc>2 || (argc==2 && !single && !multi)) return 1;
    bool missed=false;
    std::atomic_bool cancel{false};
    for (const auto &fixture : CoralB4Cases) {
        const auto board=CoralB4Board(fixture);
        auto options=CoralB4Options();
        for (unsigned budget : {800u,2000u}) for (unsigned trial=0;trial<3;++trial) {
            if (multi) {
                options.milliseconds=budget; options.fishCell=fixture.spinner;
                // Clock snapshot at the stop: 810 frames, 30fps and 30-frame
                // spinner period. Applying it to earlier starts is conditional,
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
                std::printf("{\"fixture\":\"%s\",\"kind\":\"multi\",\"turn\":%u,\"budgetMS\":%u,\"trial\":%u,\"status\":%u,\"solved\":%s,\"replayed\":%s,\"steps\":%u,\"elapsedMS\":%u,\"evaluated\":%u,\"firstFourColors\":%u,\"firstFours\":%u,\"exchanges\":%u,\"reservedFrames\":%u,\"conditionalClock\":true,\"deviceVerified\":false}\n",
                    fixture.version,unsigned(fixture.turn),budget,trial,unsigned(status),solved ? "true":"false",replay ? "true":"false",
                    result.route.count ? result.route.count-1 : 0,result.elapsedMS,result.evaluated,
                    value.firstWaveFourColors,value.firstWaveFours,result.route.exchangeCount,reservation.requiredFrames);
                std::fflush(stdout); continue;
            }
            PHOrbFishingPlan fishing{};
            if (single) { options.fishCell=fixture.spinner; fishing=PHOrbPlanFishing(board,options); if (!fishing.valid) return 1; }
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
            std::printf("{\"fixture\":\"%s\",\"kind\":\"%s\",\"candidateColor\":%d,\"turn\":%u,\"budgetMS\":%u,\"trial\":%u,\"status\":%u,\"solved\":%s,\"replayed\":%s,\"steps\":%u,\"elapsedMS\":%u,\"evaluated\":%u,\"firstColors\":%u,\"firstFourColors\":%u,\"firstFours\":%u,\"firstCombos\":%u,\"combos\":%u,\"guidePasses\":%u,\"guideCandidates\":%u,\"deviceVerified\":false,\"path\":[",
                fixture.version,single ? "single":"ordinary",single ? int(options.fishColor):-1,unsigned(fixture.turn),budget,trial,unsigned(status),solved ? "true":"false",replay ? "true":"false",
                route.count ? route.count-1 : 0,route.elapsedMS,route.evaluated,value.firstWaveColors,
                value.firstWaveFourColors,value.firstWaveFours,value.firstWaveCombos,value.combos,
                unsigned(route.sixFourGuidePasses),route.sixFourGuideCandidates);
            for (unsigned i=0;i<route.count;++i) std::printf("%s%u",i ? ",":"",unsigned(route.cells[i]));
            std::puts("]}"); std::fflush(stdout);
            }
        }
    }
    return missed ? 2 : 0;
}
