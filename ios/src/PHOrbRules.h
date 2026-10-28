#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

typedef NS_ENUM(NSInteger, PHOrbRuleMatch) { PHOrbRuleUnknown = -1, PHOrbRuleNo, PHOrbRuleYes };

FOUNDATION_EXPORT NSDictionary *PHOrbDefaultSkillRule(void);
FOUNDATION_EXPORT NSArray<NSString *> *PHOrbEnemyConditions(void);
FOUNDATION_EXPORT NSString *PHOrbEnemyConditionTitle(NSString *condition);
FOUNDATION_EXPORT BOOL PHOrbSkillRulesValid(id rules);
// All enabled conditions in a rule are conjunctive. Turn one is the turn in
// which the user started this run, not a guessed dungeon-wide turn counter.
FOUNDATION_EXPORT PHOrbRuleMatch PHOrbMatchSkillRule(NSDictionary *rule, const PHOrbSnapshot *state, NSUInteger turn);
FOUNDATION_EXPORT NSString *PHOrbSkillRuleDescription(NSDictionary *rule);
