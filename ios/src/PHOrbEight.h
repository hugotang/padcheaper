#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

FOUNDATION_EXPORT BOOL PHOrbEightActivityValid(id activity);
FOUNDATION_EXPORT BOOL PHOrbEightActivityMatches(NSDictionary *profile, const PHOrbSnapshot *state, NSString **reason);
FOUNDATION_EXPORT NSDictionary *PHOrbEightPlan(NSDictionary *activity, const PHOrbSnapshot *state);
FOUNDATION_EXPORT BOOL PHOrbEightGuideEnabled(NSDictionary *activity);
// A guide runs once before this floor's first submitted puzzle. Confirmed
// slots are skipped after Stop/Auto; native readiness is still authoritative.
FOUNDATION_EXPORT NSDictionary *PHOrbEightGuideDecision(NSDictionary *activity, const PHOrbSnapshot *state,
                                                       uint8_t completedSlots, uint8_t attemptedSlots);
// Ordered, bounded alternatives. Every effect is followed by a fresh read.
// Returned sequence contains one-based slots; nil means no unused alternative.
FOUNDATION_EXPORT NSArray *PHOrbEightSkillSequence(NSDictionary *activity, const PHOrbSnapshot *state,
                                                  uint8_t attemptedSlots, NSUInteger *cursor);
