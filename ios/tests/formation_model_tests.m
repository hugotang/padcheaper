#import "PHFormationModel.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
static void Check(BOOL value, const char *message) {
    if (!value) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
static NSDictionary *Card(BOOL assist) {
    return @{@"id": assist ? @14132 : @14238, @"level": assist ? @99 : @120, @"skill": assist ? @1 : @2,
        @"awakenings": @-1, @"superAwakening": assist ? @0 : @153,
        @"plus": assist ? @[@99,@99,@99] : @[@297,@297,@297], @"latents": assist ? @[] : @[@11,@11]};
}
static NSMutableDictionary *Team(void) {
    NSMutableArray *slots = [NSMutableArray array];
    for (unsigned i = 0; i < 6; ++i) [slots addObject:@{@"body": Card(NO), @"assist": Card(YES)}];
    return [@{@"schema": @1, @"title": @"六人測試", @"slots": slots} mutableCopy];
}
int main(void) {
    @autoreleasepool {
        PHBattleLoadout values[6]; uint8_t mask = 0;
        NSMutableDictionary *team = Team();
        Check(PHFormationReadApply(@{@"team":team, @"mask":@63}, values, &mask, NULL) && mask == 63,
              "all six native loadouts parse before any queue submission");
        Check(values[5].bodyNumber == 14238 && values[5].assistNumber == 14132 && values[5].skillLevel == 2 &&
              values[5].assistSkillLevel == 1 && values[5].superAwakening == 153 && values[5].latentCount == 2 && values[5].assistPlus,
              "bridge keeps encyclopedia IDs, body/weapon skill levels, super, latents and +297 separate");
        PHBattleLoadout sentinel[6]; memcpy(sentinel, values, sizeof(values));
        NSMutableArray *slots = [team[@"slots"] mutableCopy];
        NSMutableDictionary *slot = [slots[5] mutableCopy], *body = [Card(NO) mutableCopy];
        body[@"id"] = @YES; slot[@"body"] = body; slots[5] = slot; team[@"slots"] = slots;
        Check(!PHFormationReadTeam(team, values, NULL) && !memcmp(sentinel, values, sizeof(values)),
              "one invalid final position leaves the entire output batch unchanged");
        for (id invalid in @[@YES, @0, @64, @1.5, @"1"]) {
            Check(!PHFormationReadApply(@{@"team":Team(), @"mask":invalid}, NULL, NULL, NULL), "bridge masks reject booleans, fractions and invalid positions");
        }
        team = Team(); slots = [team[@"slots"] mutableCopy]; slot = [slots[0] mutableCopy];
        NSMutableDictionary *assist = [Card(YES) mutableCopy]; assist[@"plus"] = @[@99,@0,@99];
        slot[@"assist"] = assist; slots[0] = slot; team[@"slots"] = slots;
        Check(!PHFormationReadTeam(team, NULL, NULL), "weapon plus uses a separate all-or-none +297 marker");
        body = [Card(NO) mutableCopy]; body[@"id"] = @0; slot[@"body"] = body; slot[@"assist"] = Card(YES); slots[0] = slot;
        Check(!PHFormationReadApply(@{@"team":team,@"mask":@1}, NULL, NULL, NULL), "selected empty bodies cannot be applied");
        NSUInteger position = 0;
        Check(PHFormationReadRestoreSlot(@6, &position) && position == 6, "restore-all sentinel crosses the native bridge");
        Check(!PHFormationReadRestoreSlot(@YES, NULL) && !PHFormationReadRestoreSlot(@7, NULL), "restore positions are strictly bounded");
        printf("Formation native bridge tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
