#pragma once
#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

// Inputs are freshly captured native facts supplied by the session. Diagnostic
// observations and inferred turns must never populate these authorization bits.
typedef struct {
    uint32_t wave;
    uint64_t enemyHP[8], enemyMaximumHP[8];
    uint8_t aliveMask, hpKnownMask;
    uint8_t enemyEffects, playerBypass, playerEffects, partsMask;
    uint16_t assistVoidTurns, unmatchableTurns, spinnerTurns;
    bool enemiesKnown, partsKnown, statusKnown, assistVoidKnown, unmatchableKnown, spinnerKnown;
    uint8_t readySlots, readinessKnownSlots;
} PHOrbFixedPlanEvidence;
typedef enum { PHOrbFixedPlanUnknown, PHOrbFixedPlanNo, PHOrbFixedPlanYes } PHOrbFixedPlanMatch;

FOUNDATION_EXPORT BOOL PHOrbFixedPlanValid(id plan);
FOUNDATION_EXPORT BOOL PHOrbFixedPlanMatchesTeam(NSDictionary *plan, const PHOrbSnapshot *state);
FOUNDATION_EXPORT BOOL PHOrbFixedPlanMatchesTeamOrder(NSDictionary *plan, const PHOrbSnapshot *state, BOOL swapped);
// Choose only when entering a floor; retain the selected dictionary after a
// part is destroyed instead of switching to the ordinary variant mid-floor.
FOUNDATION_EXPORT NSDictionary *PHOrbFixedPlanVariantForFloor(NSDictionary *plan, NSUInteger wave,
    const PHOrbSnapshot *state);
FOUNDATION_EXPORT NSDictionary *PHOrbFixedPlanStage(NSDictionary *plan, NSUInteger wave,
    NSString *variant, NSUInteger index);
FOUNDATION_EXPORT PHOrbSkillRequest PHOrbFixedPlanActionRequest(NSDictionary *action);
FOUNDATION_EXPORT BOOL PHOrbFixedPlanActionMatchesTeam(NSDictionary *action, const PHOrbSnapshot *state);
// Equality of the declared effect source, not permission to substitute a skill.
FOUNDATION_EXPORT BOOL PHOrbFixedPlanSameSkill(NSDictionary *a, NSDictionary *b);
FOUNDATION_EXPORT PHOrbFixedPlanMatch PHOrbFixedPlanActionCondition(NSDictionary *action,
    const PHOrbFixedPlanEvidence *evidence);
FOUNDATION_EXPORT PHOrbFixedPlanMatch PHOrbFixedPlanRepeatSatisfied(NSDictionary *repeat,
    NSUInteger confirmedPuzzles, NSUInteger floorWave, const PHOrbFixedPlanEvidence *evidence);
FOUNDATION_EXPORT NSDictionary *PHOrbFixedPlanFromPackage(id package, NSString **reason);
FOUNDATION_EXPORT NSDictionary *PHOrbFixedPlanPackageFromProfile(NSDictionary *profile);
FOUNDATION_EXPORT NSString *PHOrbFixedPlanDescription(NSDictionary *profile);
