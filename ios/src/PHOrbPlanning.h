#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

FOUNDATION_EXPORT NSArray<NSString *> *PHOrbGoalKinds(void);
FOUNDATION_EXPORT NSString *PHOrbGoalTitle(NSString *kind);
FOUNDATION_EXPORT NSString *PHOrbColorTitle(NSUInteger mask);
FOUNDATION_EXPORT BOOL PHOrbReadGoals(id value, PHOrbGoalSet *goals);
FOUNDATION_EXPORT NSArray<NSDictionary *> *PHOrbGoalsJSON(const PHOrbGoalSet *goals);
FOUNDATION_EXPORT NSString *PHOrbGoalDescription(NSDictionary *goal);
FOUNDATION_EXPORT NSString *PHOrbGoalsDescription(NSArray *goals);
// Board objectives only, never a claim that the complete leader skill fired.
FOUNDATION_EXPORT NSDictionary *PHOrbPlanForLeaders(const PHOrbSnapshot *state);
FOUNDATION_EXPORT NSDictionary *PHOrbPlanForLeaderCatalog(const PHOrbSnapshot *state, NSDictionary *catalog);
