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
// All enabled conditions in a rule are conjunctive. Turn one is the turn in
// which the user started this run, not a guessed dungeon-wide turn counter.
FOUNDATION_EXPORT PHOrbRuleMatch PHOrbMatchSkillRule(NSDictionary *rule, const PHOrbSnapshot *state, NSUInteger turn);
// Conditions only: a bound/unavailable member must not hide a required rule.
// Reservation and per-turn/once limits are checked by the session afterwards.
FOUNDATION_EXPORT PHOrbRuleMatch PHOrbTriggerSkillRule(NSDictionary *rule, const PHOrbSnapshot *state,
                                                     NSUInteger turn, NSString **reason);
FOUNDATION_EXPORT NSString *PHOrbSkillRuleDescription(NSDictionary *rule);
