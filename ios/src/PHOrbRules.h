#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

typedef NS_ENUM(NSInteger, PHOrbRuleMatch) { PHOrbRuleUnknown = -1, PHOrbRuleNo, PHOrbRuleYes };

FOUNDATION_EXPORT NSDictionary *PHOrbDefaultSkillRule(void);
FOUNDATION_EXPORT PHOrbSkillRequest PHOrbRequestForRule(NSDictionary *rule);
FOUNDATION_EXPORT NSArray<NSString *> *PHOrbEnemyConditions(void);
FOUNDATION_EXPORT NSString *PHOrbEnemyConditionTitle(NSString *condition);
FOUNDATION_EXPORT NSArray<NSString *> *PHOrbObstacleConditions(void);
FOUNDATION_EXPORT NSString *PHOrbObstacleConditionTitle(NSString *condition);
FOUNDATION_EXPORT NSString *PHOrbUnmatchableConditionTitle(unsigned colors);
FOUNDATION_EXPORT NSString *PHOrbSpinnerReadDescription(const PHOrbSnapshot *state);
FOUNDATION_EXPORT NSString *PHOrbObstacleDescription(const PHOrbSnapshot *state);
FOUNDATION_EXPORT NSArray<NSString *> *PHOrbPlayerConditions(void);
FOUNDATION_EXPORT NSString *PHOrbPlayerConditionTitle(NSString *condition);
FOUNDATION_EXPORT NSString *PHOrbSurvivalDescription(const PHOrbSnapshot *state);
FOUNDATION_EXPORT BOOL PHOrbSkillRulesValid(id rules);
// Legacy rules acquire optional fields without changing their behavior.
FOUNDATION_EXPORT NSDictionary *PHOrbNormalizedSkillRule(NSDictionary *rule);
FOUNDATION_EXPORT NSArray<NSNumber *> *PHOrbSkillRuleOrder(NSArray *rules, const uint8_t next[PHOrbMaxSkillGroups]);
FOUNDATION_EXPORT NSUInteger PHOrbSkillReservedUntil(NSArray *rules, NSUInteger slot);
// All enabled conditions are conjunctive. Legacy run timing starts at one on
// each run. Accepted timing requires the explicit retained-history entry point.
FOUNDATION_EXPORT PHOrbRuleMatch PHOrbMatchSkillRule(NSDictionary *rule, const PHOrbSnapshot *state, NSUInteger turn);
// Conditions only: a bound/unavailable member must not hide a required rule.
// Reservation and per-turn/once limits are checked by the session afterwards.
FOUNDATION_EXPORT PHOrbRuleMatch PHOrbTriggerSkillRule(NSDictionary *rule, const PHOrbSnapshot *state,
                                                     NSUInteger turn, NSString **reason);
// Accepted-use timing counts only observed completed orb turns for the same
// battle and rule set. A group shares its last accepted (or uncertain) use.
// Zero observedTurn means timing is unknown; native cooldown is still checked
// separately by the engine when a rule is eligible.
FOUNDATION_EXPORT PHOrbRuleMatch PHOrbTriggerSkillRuleWithTiming(NSDictionary *rule, const PHOrbSnapshot *state,
    NSUInteger runTurn, NSUInteger observedTurn, NSUInteger lastUseTurn, NSString **reason);
FOUNDATION_EXPORT NSString *PHOrbSkillRuleDescription(NSDictionary *rule);
