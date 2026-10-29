#import "PHOrbRules.h"
#include <stdio.h>

static unsigned failures;
static void Check(BOOL ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

#include "orb_effect_rules_tests.inc"

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
        state.automation.enemiesKnown = false;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo &&
            PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleUnknown,
            "legacy matching skips an unavailable caster before unknown conditions while required evaluation still sees them");
        state.automation.members[0].bound = false; rule[@"floor"] = @0; rule[@"reserveUntil"] = @5;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo &&
            PHOrbTriggerSkillRule(rule, &state, 1, NULL) == PHOrbRuleUnknown,
            "reserved legacy rules do not require unknown enemy data before their reserved floor");
        state.automation.enemiesKnown = true; rule[@"floor"] = @3; rule[@"reserveUntil"] = @0;
        state.automation.members[0].bound = false; rule[@"enabled"] = @NO;
        Check(PHOrbMatchSkillRule(rule, &state, 1) == PHOrbRuleNo, "disabled rules never trigger");
        NSData *data = [NSJSONSerialization dataWithJSONObject:@[rule] options:0 error:NULL];
        id saved = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        Check(PHOrbSkillRulesValid(saved) && [saved isEqual:@[rule]], "all conditions and Boolean enabled state survive persistence");
        NSMutableDictionary *legacy = [PHOrbDefaultSkillRule() mutableCopy];
        [legacy removeObjectsForKeys:@[@"group",@"once",@"reserveUntil",@"obstacle",@"hpAtMost",@"status",@"required"]];
        Check(PHOrbSkillRulesValid(@[legacy]) && [PHOrbNormalizedSkillRule(legacy) isEqual:PHOrbDefaultSkillRule()],
            "legacy rules gain no grouping, reservation, once or obstacle condition");
        for (NSDictionary *change in @[@{@"group":@7},@{@"group":@YES},@{@"once":@1},@{@"reserveUntil":@1000},
            @{@"reserveUntil":@5,@"floor":@3},@{@"obstacle":@"made_up"}]) {
            NSMutableDictionary *bad = [PHOrbDefaultSkillRule() mutableCopy]; [bad addEntriesFromDictionary:change];
            Check(!PHOrbSkillRulesValid(@[bad]), "cycle and obstacle fields are validated strictly");
        }
        NSMutableDictionary *a = [PHOrbDefaultSkillRule() mutableCopy], *b = [a mutableCopy], *c = [a mutableCopy];
        a[@"group"] = @1; b[@"slot"] = @2; c[@"slot"] = @3; c[@"group"] = @1;
        uint8_t next[PHOrbMaxSkillGroups] = {0}; next[0] = 1;
        Check([PHOrbSkillRuleOrder(@[a,b,c],next) isEqual:@[@2,@0,@1]], "group priority and rotation wrap before independent next group");
        a[@"reserveUntil"] = @5;
        Check(PHOrbSkillReservedUntil(@[a,b,c],0) == 5 && PHOrbSkillReservedUntil(@[a,b,c],1) == 0,
            "reservation is shared by all rules at that position only");
        a[@"enabled"] = @NO;
        Check(PHOrbSkillReservedUntil(@[a,b,c],0) == 0, "disabled rules do not reserve skills");
        rule = [PHOrbDefaultSkillRule() mutableCopy]; state.automation.members[0].active = true;
        state.automation.members[0].bound = false; state.ordinary = true; state.phase = 2;
        rule[@"obstacle"] = @"spinner"; state.obstacles = PHOrbObstacleSpinner;
        Check(PHOrbMatchSkillRule(rule,&state,1) == PHOrbRuleYes, "known spinner effect can request a remedy skill without authorizing a route");
        state.obstacles = 0;
        Check(PHOrbMatchSkillRule(rule,&state,1) == PHOrbRuleNo, "removed spinner no longer requests the same remedy");
        rule[@"obstacle"] = @"locked"; state.board.flags[29] = PHOrbFlagLocked;
        Check(PHOrbMatchSkillRule(rule,&state,1) == PHOrbRuleYes && PHOrbBoardValid(&state.board), "locked basic orb is a skill condition but still supports ordinary matching");
        state.board.flags[29] |= 0x40000000u;
        Check(!PHOrbBoardValid(&state.board) && (PHOrbBoardObstacles(&state) & PHOrbObstacleOrbEffect), "lock acceptance does not authorize other combined effects");
        for (NSDictionary *change in @[@{@"hpAtMost":@101}, @{@"hpAtMost":@-1}, @{@"hpAtMost":@YES},
            @{@"hpAtMost":@40.5}, @{@"hpAtMost":@"40"}, @{@"required":@1}, @{@"status":@"unknown"}]) {
            NSMutableDictionary *bad = [PHOrbDefaultSkillRule() mutableCopy]; [bad addEntriesFromDictionary:change];
            Check(!PHOrbSkillRulesValid(@[bad]), "HP, player status and required fields cannot silently coerce malformed settings");
        }
        rule = [PHOrbDefaultSkillRule() mutableCopy]; rule[@"hpAtMost"] = @40; rule[@"required"] = @YES;
        state.automation.hpKnown = true; state.automation.hp = 400; state.automation.maximumHP = 1000;
        NSString *reason = nil;
        Check(PHOrbTriggerSkillRule(rule,&state,1,&reason) == PHOrbRuleYes && [reason containsString:@"40"],
            "inclusive HP threshold includes a useful explanation");
        state.automation.hp = 401;
        Check(PHOrbTriggerSkillRule(rule,&state,1,&reason) == PHOrbRuleNo && [reason containsString:@"高於"],
            "one HP above the threshold is not rounded into a rescue trigger");
        state.automation.hpKnown = false;
        Check(PHOrbTriggerSkillRule(rule,&state,1,&reason) == PHOrbRuleUnknown && [reason containsString:@"上限"],
            "unknown HP is neither full health nor a confirmed low-health trigger");
        state.automation.hpKnown = true; state.automation.hp = state.automation.maximumHP = INT32_MAX;
        rule[@"hpAtMost"] = @99;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleNo, "HP comparison uses wide products without signed or 32-bit overflow");
        rule[@"hpAtMost"] = @100;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleYes, "100 percent includes the largest supported full-health value");
        rule[@"hpAtMost"] = @0; rule[@"status"] = @"awoken_bind";
        state.automation.statusKnown = true; state.automation.awakeningRestricted = true; state.automation.playerEffects = 0;
        Check(PHOrbTriggerSkillRule(rule,&state,1,&reason) == PHOrbRuleNo && [reason containsString:@"關卡"],
            "fixed dungeon restriction cannot masquerade as temporary awakening bind");
        state.automation.playerEffects = PHOrbPlayerAwokenBind;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleYes, "temporary bind is detected even when the dungeon also restricts awakenings");
        rule[@"hpAtMost"] = @40;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleNo, "HP and player status predicates are conjunctive");
        state.automation.hp = 1;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleYes, "low HP and the requested status can jointly request rescue");
        state.automation.statusKnown = false;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleUnknown, "unknown player effects cannot bypass a rescue rule");
        state.automation.statusKnown = true; state.automation.members[0].bound = true;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleYes && PHOrbMatchSkillRule(rule,&state,1) == PHOrbRuleNo,
            "unavailable caster does not hide the condition of a required rule");
        rule[@"reserveUntil"] = @5;
        Check(PHOrbTriggerSkillRule(rule,&state,1,NULL) == PHOrbRuleYes, "reservation is reported as an execution constraint rather than a missing rescue condition");
        state.automation.members[0].bound = false;
        PHOrbSnapshot before = state;
        ++state.automation.hp;
        Check(!PHOrbSamePlayerState(&before,&state), "new HP invalidates captured rescue decisions");
        state = before; state.automation.awakeningRestricted = false;
        Check(!PHOrbSamePlayerState(&before,&state), "changed fixed awakening restriction invalidates player context");
        OrbEffectRulesTests();
        printf("Orb rules tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
