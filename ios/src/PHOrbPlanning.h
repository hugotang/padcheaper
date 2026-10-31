#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

FOUNDATION_EXPORT NSArray<NSString *> *PHOrbGoalKinds(void);
FOUNDATION_EXPORT NSString *PHOrbGoalTitle(NSString *kind);
FOUNDATION_EXPORT NSString *PHOrbColorTitle(NSUInteger mask);
FOUNDATION_EXPORT BOOL PHOrbReadGoals(id value, PHOrbGoalSet *goals);
FOUNDATION_EXPORT NSArray<NSDictionary *> *PHOrbGoalsJSON(const PHOrbGoalSet *goals);
FOUNDATION_EXPORT NSString *PHOrbGoalDescription(NSDictionary *goal);
FOUNDATION_EXPORT NSString *PHOrbGoalsDescription(NSArray *goals);
FOUNDATION_EXPORT NSDictionary *PHOrbDefaultRecovery(void);
FOUNDATION_EXPORT BOOL PHOrbRecoveryValid(id settings);
FOUNDATION_EXPORT NSString *PHOrbRecoveryDescription(NSDictionary *settings);
// Adds a conditional heart goal after skill completion, only in automatic
// mode. Base goals are retained for later editing; no healing is simulated.
FOUNDATION_EXPORT NSDictionary *PHOrbPlanWithRecovery(NSDictionary *plan, NSDictionary *settings,
                                                      const PHOrbSnapshot *state, BOOL automatic);
// Preserve all base/heart goals. Return a bounded list of color/group choices
// whose predicted first-wave L amount covers the current assist-void duration.
FOUNDATION_EXPORT NSDictionary *PHOrbPlanWithAssistRecovery(NSDictionary *plan, BOOL enabled,
    const PHOrbSnapshot *state, BOOL automatic);
// Board objectives only, never a claim that the complete leader skill fired.
FOUNDATION_EXPORT NSDictionary *PHOrbPlanForLeaders(const PHOrbSnapshot *state);
FOUNDATION_EXPORT NSDictionary *PHOrbPlanForLeaderCatalog(const PHOrbSnapshot *state, NSDictionary *catalog);
