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

        team = Team();
        NSDictionary *library = PHFormationLibraryFromTeam(team), *selectedTeam = nil;
        Check(PHFormationReadLibrary(library, &selectedTeam, NULL) && [library[@"selected"] isEqual:@"legacy"] &&
              [selectedTeam isEqual:team], "legacy formation migrates without changing any member or latent ID");
        team[@"title"] = @"later edit";
        Check([selectedTeam[@"title"] isEqual:@"六人測試"], "migration copies mutable drafts independently");
        NSDictionary *emptyLibrary = PHFormationLibraryFromTeam(nil);
        Check(PHFormationReadLibrary(emptyLibrary, &selectedTeam, NULL) && [emptyLibrary[@"selected"] isEqual:@"default"] &&
              [selectedTeam[@"slots"] count] == 6, "new installations get a valid six-position default library");
        NSMutableDictionary *copy = Team(); copy[@"title"] = @"第二隊";
        library = @{@"schema":@1, @"selected":@"second", @"teams":@[
            library[@"teams"][0], @{@"id":@"second", @"team":copy}]};
        Check(PHFormationReadLibrary(library, &selectedTeam, NULL) && [selectedTeam[@"title"] isEqual:@"第二隊"],
              "a stable selected ID chooses the saved team used for entry");
        copy[@"title"] = @"改名";
        NSDictionary *updated = PHFormationLibraryUpdatingTeam(library, copy);
        copy[@"title"] = @"unsaved change";
        Check([updated[@"teams"][1][@"team"][@"title"] isEqual:@"改名"] &&
              [updated[@"teams"][0][@"team"][@"title"] isEqual:@"六人測試"], "saving or renaming one team preserves independent records");
        NSData *json = [NSJSONSerialization dataWithJSONObject:updated options:0 error:NULL];
        Check(PHFormationReadLibrary([NSJSONSerialization JSONObjectWithData:json options:0 error:NULL], NULL, NULL),
              "multi-team library survives its canonical JSON persistence format");
        for (NSDictionary *changes in @[
            @{@"selected":@"missing"}, @{@"selected":@YES}, @{@"schema":@YES}, @{@"teams":@[]},
            @{@"teams":@[@{@"id":@"bad id", @"team":Team()}]},
            @{@"teams":@[@{@"id":@"second", @"team":Team()}, @{@"id":@"second", @"team":Team()}]},
            @{@"teams":@[@{@"id":@"second", @"team":@{}}]}
        ]) {
            NSMutableDictionary *bad = [updated mutableCopy]; [bad addEntriesFromDictionary:changes];
            selectedTeam = @{@"sentinel":@YES};
            Check(!PHFormationReadLibrary(bad, &selectedTeam, NULL) && [selectedTeam[@"sentinel"] isEqual:@YES],
                  "malformed libraries leave the selected output and all saved records unchanged");
        }
        NSMutableArray *records = [NSMutableArray array];
        for (NSUInteger i = 0; i < PHFormationLibraryCapacity; ++i)
            [records addObject:@{@"id":[NSString stringWithFormat:@"t%lu",(unsigned long)i], @"team":Team()}];
        NSDictionary *full = @{@"schema":@1, @"selected":@"t0", @"teams":records};
        Check(PHFormationReadLibrary(full, NULL, NULL), "all 64 saved teams validate");
        [records addObject:@{@"id":@"overflow", @"team":Team()}];
        Check(!PHFormationReadLibrary(full, NULL, NULL), "overflowing libraries are refused");
        printf("Formation native bridge tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
