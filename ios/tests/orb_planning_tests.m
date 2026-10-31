#import "PHOrbPlanning.h"
#include <stdio.h>

static unsigned failures;
static void Check(BOOL ok, const char *message) { if (!ok) { fprintf(stderr, "FAIL: %s\n", message); ++failures; } }
static NSDictionary *Goal(NSString *kind, unsigned colors, unsigned count, unsigned length) {
    return @{@"kind":kind, @"colors":@(colors), @"count":@(count), @"length":@(length)};
}
static NSDictionary *Profile(NSArray *goals) { return @{@"goals":goals, @"warnings":@[], @"unsupported":@[]}; }

#include "orb_recovery_planning_tests.inc"

int main(void) {
    @autoreleasepool {
        PHOrbGoalSet goals = {0};
        NSArray *manual = @[Goal(@"l", 4, 1, 0), Goal(@"cross", 32, 1, 0), Goal(@"combo", 0, 7, 0)];
        Check(PHOrbReadGoals(manual, &goals) && goals.count == 3 && [PHOrbGoalsJSON(&goals) isEqual:manual],
              "manual multi-goal plan round-trips without changing meaning");
        for (id invalid in @[@YES, @"l", NSNull.null, @[@1], @[@{@"kind":@"unknown"}],
            @[Goal(@"l", 64, 1, 0)], @[Goal(@"combo", 1, 7, 0)], @[Goal(@"colors", 3, 3, 0)],
            @[Goal(@"match", 1, 1, 2)], @[Goal(@"l", 1, 1, 3)], @[Goal(@"combo", 0, 15, 0)],
            @[Goal(@"l", 1, 1, 0), Goal(@"l", 1, 2, 0)]]) {
            Check(!PHOrbReadGoals(invalid, &goals) && goals.count == 0, "malformed, duplicate, or out-of-range goals are rejected atomically");
        }
        for (id value in @[@YES, @-1, @1.5, @"1", NSNull.null]) {
            NSMutableDictionary *bad = [Goal(@"l", 4, 1, 0) mutableCopy]; bad[@"count"] = value;
            Check(!PHOrbReadGoals(@[bad], NULL), "goal counts require integers, not booleans, fractions or strings");
        }
        Check([PHOrbGoalDescription(Goal(@"column", 4, 1, 0)) containsString:@"木 整直行"], "goals name both color and shape");

        PHOrbSnapshot state = {0}; state.present = state.valid = state.leadersValid = true;
        state.leaderNumbers[0] = 14242; state.leaderNumbers[1] = 13834;
        NSDictionary *plan = PHOrbPlanForLeaders(&state);
        NSSet *actual = [NSSet setWithArray:plan[@"goals"]];
        Check([plan[@"available"] boolValue] && actual.count == 3 &&
              [actual containsObject:Goal(@"column", 4, 1, 0)] && [actual containsObject:Goal(@"cross", 32, 1, 0)] &&
              [actual containsObject:Goal(@"four", 31, 1, 0)],
              "recorded leader pair yields wood column plus heart cross plus attacking-color four-match");
        Check([plan[@"warning"] containsString:@"有效覺醒"] && [plan[@"warning"] containsString:@"追加 Combo"],
              "board plan never claims to have verified awakening activation or leader-added combos");
        PHOrbSnapshot douma = state; douma.leaderNumbers[0] = douma.leaderNumbers[1] = 12832;
        NSDictionary *doumaPlan = PHOrbPlanForLeaders(&douma);
        NSSet *doumaGoals = [NSSet setWithArray:doumaPlan[@"goals"]];
        Check([doumaPlan[@"available"] boolValue] && doumaGoals.count == 2 &&
            [doumaGoals containsObject:Goal(@"match",2,1,5)] && [doumaGoals containsObject:Goal(@"cross",31,1,0)],
            "recorded unchanged Douma leaders yield one water five-match and one attacking-color cross without duplicating both leaders");
        state.leaderNumbers[0] = 13834;
        plan = PHOrbPlanForLeaders(&state);
        Check([plan[@"available"] boolValue] && [plan[@"goals"] count] == 2, "identical leader clauses share the same matches");
        PHOrbSnapshot previous = state;
        state.leaderNumbers[0] = 12572;
        plan = PHOrbPlanForLeaders(&state);
        Check(!PHOrbSameLeaders(&state, &previous) && [plan[@"goals"] containsObject:Goal(@"colors", 31, 4, 0)],
              "new current leader changes the plan instead of retaining a saved recipe or old profile");
        state.leaderNumbers[0] = 3230;
        plan = PHOrbPlanForLeaders(&state);
        Check(![plan[@"available"] boolValue] && [plan[@"goals"] count] == 0 && [plan[@"unsupported"] count] > 0,
              "unsupported poison requirement blocks the entire automatic pair instead of silently dropping a clause");
        state.leaderNumbers[0] = 65000;
        Check(![PHOrbPlanForLeaders(&state)[@"available"] boolValue], "missing card data does not fall back to an unrelated leader");
        state.leadersValid = false;
        Check(![PHOrbPlanForLeaders(&state)[@"available"] boolValue], "read failures cannot reuse earlier leader IDs");
        state.leadersValid = true; state.leaderNumbers[0] = state.leaderNumbers[1] = 1; state.identity.entryMode = 7;
        Check(![PHOrbPlanForLeaders(&state)[@"available"] boolValue], "leader planning retains multiplayer exclusion");
        state.identity.entryMode = 0; state.leaderNumbers[1] = 2;
        NSDictionary *catalog = @{@"cards":@{@"1":@{@"name":@"A", @"skill":@1}, @"2":@{@"name":@"B", @"skill":@2}},
            @"skills":@{@"1":Profile(@[Goal(@"combo", 0, 5, 0)]), @"2":Profile(@[Goal(@"combo", 0, 7, 0)])}};
        plan = PHOrbPlanForLeaderCatalog(&state, catalog);
        Check([plan[@"available"] boolValue] && [plan[@"goals"] isEqual:@[Goal(@"combo", 0, 7, 0)]],
              "paired minimum-combo clauses merge to the stronger threshold");
        RecoveryPlanningTests();
    }
    printf("Orb planning tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
