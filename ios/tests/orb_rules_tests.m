#import "PHOrbRules.h"
#include <stdio.h>

static unsigned failures;
static void Check(BOOL ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

int main(void) {
    @autoreleasepool {
        Check(PHOrbSkillRulesValid(@[]), "empty rules enable continuous orbs without skills");
        NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy];
        for (NSDictionary *change in @[@{@"slot":@0}, @{@"slot":@7}, @{@"slot":@YES}, @{@"every":@0},
            @{@"every":@1.5}, @{@"every":@1000}, @{@"floor":@-1}, @{@"monster":@65536}, @{@"enemy":@"unknown"},
            @{@"enabled":@1}, @{@"color":@6}, @{@"color":@0}, @{@"below":@3}, @{@"floor":@"2"}, @{@"extra":@0}]) {
            NSMutableDictionary *bad = [rule mutableCopy]; [bad addEntriesFromDictionary:change];
            Check(!PHOrbSkillRulesValid(@[bad]), "invalid conditions and unknown keys cannot be persisted");
        }
        NSMutableArray *many = [NSMutableArray array];
        for (unsigned i = 0; i < PHOrbMaxSkillRules + 1; ++i) [many addObject:rule];
        Check(!PHOrbSkillRulesValid(many), "rule count is bounded");
        PHOrbSnapshot state = {0}; state.wave = 2; state.settled = true;
        state.board.width = 6; state.board.height = 5;
        for (unsigned i = 0; i < 30; ++i) state.board.types[i] = i % 6;
        state.automation.valid = state.automation.boardKnown = state.automation.enemiesKnown = true;
        state.automation.members[0].active = true; state.automation.members[0].number = 100;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleYes, "default rule targets first active member each turn");
        rule[@"every"] = @2; rule[@"floor"] = @3; rule[@"monster"] = @100;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleYes && PHOrbMatchSkillRule(rule, &state, 2) == PHOrbRuleNo &&
            PHOrbMatchSkillRule(rule, &state, 3) == PHOrbRuleYes, "interval is relative to starting turn and wave is shown one-based");
        rule[@"floor"] = @2;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo, "wrong floor does not trigger"); rule[@"floor"] = @3;
        rule[@"monster"] = @101;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo, "current transformed number is matched when specified"); rule[@"monster"] = @0;
        rule[@"color"] = @5; rule[@"below"] = @5;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo, "below is strict, not less-than-or-equal"); rule[@"below"] = @6;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleYes, "board shortage joins floor and interval conditions");
        state.automation.boardKnown = false;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleUnknown, "missing board data is not an empty board"); state.automation.boardKnown = true;
        for (NSUInteger i = 1; i < PHOrbEnemyConditions().count; ++i) {
            rule[@"enemy"] = PHOrbEnemyConditions()[i]; state.automation.enemyEffects = 1u << (i - 1);
            Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleYes, "each verified enemy condition has its own bit");
            state.automation.playerBypass = state.automation.enemyEffects;
            Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo, "active team-wide bypass prevents wasting another skill");
            state.automation.playerBypass = 0; state.automation.enemiesKnown = false;
            Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleUnknown, "unknown enemy state cannot silently fall back to no effect");
            state.automation.enemiesKnown = true;
        }
        state.automation.members[0].bound = true;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo, "bound member is not a skill candidate");
        state.automation.members[0].bound = false; rule[@"enabled"] = @NO;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo, "disabled rules never trigger");
        NSData *data = [NSJSONSerialization dataWithJSONObject:@[rule] options:0 error:NULL];
        id saved = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        Check(PHOrbSkillRulesValid(saved) && [saved isEqual:@[rule]], "all conditions and Boolean enabled state survive persistence");
        printf("Orb rules tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
