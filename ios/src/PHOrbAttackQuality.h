#pragma once
#include "PHOrbSearchAttempt.h"
#include <algorithm>
#include <iterator>

// Optional solo fixed-attack refinement, after an executable ordinary route.
// The extra stage shares at most one configured budget and the existing solo
// ceiling. It never retries, starts a gesture, or alters any native clock.
static inline unsigned PHOrbAttackQualityBudget(bool solo, bool recovery, bool cancelled,
    const PHOrbSolveOptions &options, PHOrbSolveStatus status, const PHOrbRoute &route, uint64_t elapsedMS) {
    if (!solo || recovery || cancelled || options.fixedBoard.mode != PHOrbFixedBoardMode::FourAttack ||
        status != PHOrbSolveStatus::Solved || !PHOrbHasSearchCandidate(status,route) ||
        !route.goalsMet || !route.hazardsSafe || route.fishing || elapsedMS >= PHOrbSoloRetryMilliseconds) return 0;
    return std::min(options.milliseconds,unsigned(PHOrbSoloRetryMilliseconds-elapsedMS));
}

static inline unsigned PHOrbAttackPrefixSteps(unsigned routeSteps, unsigned stepLimit) {
    const unsigned approach = routeSteps > 1 ? routeSteps - 1 : routeSteps;
    return std::min(approach, stepLimit * 3 / 4);
}

// This clause is a search aid only. Retained routes must be replayed using
// the original goals; the extra clause must not escape into saved plans.
static inline bool PHOrbAttackQualityGoals(const PHOrbGoalSet &base, unsigned fours, unsigned upper,
    PHOrbGoalSet &goals) {
    if (!base.count || base.count > PHOrbMaxGoals || fours >= upper || upper > PHOrbMaxCells/4) return false;
    goals=base;
    for (unsigned g=0;g<goals.count;++g) if (goals.items[g].kind==PHOrbGoalFour && goals.items[g].colors==63) {
        goals.items[g].count=uint8_t(std::max(unsigned(goals.items[g].count),fours+1)); return true;
    }
    if (goals.count==PHOrbMaxGoals) return false;
    goals.items[goals.count++]={PHOrbGoalFour,63,uint8_t(fours+1),0}; return true;
}

// Both arguments are complete replays, including the relevant spinner phases.
// Equal quality keeps the ordinary route and avoids unnecessary waiting.
static inline bool PHOrbPreferAttackQuality(const PHOrbEvaluation &candidate, const PHOrbEvaluation &retained) {
    return candidate.goalsMet && candidate.hazardsSafe && candidate.fixedBoardSatisfied &&
        retained.goalsMet && retained.hazardsSafe && retained.fixedBoardSatisfied &&
        candidate.firstWaveFours>retained.firstWaveFours;
}

static inline void PHOrbApplyAttackQualityEvaluation(PHOrbRoute &route, const PHOrbEvaluation &value,
    const PHOrbGoalSet &goals) {
    route.combos=value.combos; route.erased=value.erased;
    std::copy(std::begin(value.goalHits),std::end(value.goalHits),route.goalHits);
    route.goalCount=goals.count; route.matchedGoals=value.matchedGoals;
    route.goalsMet=value.goalsMet; route.hazardsSafe=value.hazardsSafe;
    route.poisonErased=value.poisonErased; route.mortalPoisonErased=value.mortalPoisonErased;
    route.unmatchedBombs=value.unmatchedBombs; route.heartFours=value.heartFours; route.heartMatches=value.heartMatches;
}
