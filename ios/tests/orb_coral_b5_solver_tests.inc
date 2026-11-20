#include "../research/coral_b5_fixtures.h"

static void CoralB5FailureCorpusTests() {
    for (const auto &fixture : CoralB5Cases) {
        const auto board=CoralB5Board(fixture);
        auto options=CoralB5Options(fixture);
        PHOrbRoute route{}; route.count=fixture.pathCount; route.diagonal=fixture.diagonal;
        std::copy_n(fixture.path,route.count,route.cells);
        PHOrbEvaluation value{}; uint32_t damage=0; uint8_t contacts=0;
        auto legacy=options; legacy.fixedBoard.requireSixColorFours=false;
        legacy.goals={}; legacy.goals.count=2;
        legacy.goals.items[0]={PHOrbGoalColors,63,6,0}; legacy.goals.items[1]={PHOrbGoalFour,32,1,0};
        Check(PHOrbEvaluateRoute(board,route,legacy,value,damage,contacts) && value.goalsMet &&
            value.hazardsSafe && value.firstWaveFours==fixture.firstFours && value.firstWaveCombos==fixture.firstCombos,
            "four historical B5 failures reproduce the old six-colour/heart-four acceptance");
        Check(PHOrbEvaluateRoute(board,route,options,value,damage,contacts) && !value.goalsMet &&
            !value.fixedBoardSatisfied && value.firstWaveFourColors!=63 && !damage && !contacts,
            "none of the historical B5 failed attacks qualifies as a six-colour-four finisher");
        // The contract must not rely on six positive goals being present:
        // diagnostics and replay still reject a legacy goal list with r7.
        legacy.fixedBoard.requireSixColorFours=true;
        Check(PHOrbEvaluateRoute(board,route,legacy,value,damage,contacts) && !value.goalsMet && !value.fixedBoardSatisfied,
            "B5 strict release policy cannot be weakened by a legacy positive-goal list");
    }
}
