#import "PHFormationModel.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <string.h>

static BOOL ReadInteger(id value, NSInteger minimum, NSInteger maximum, NSInteger *result) {
    if (![value isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID()) return NO;
    double number = [value doubleValue];
    if (!isfinite(number) || number != trunc(number) || number < minimum || number > maximum) return NO;
    *result = (NSInteger)number;
    return YES;
}

static BOOL ReadCard(id value, BOOL assist, PHBattleLoadout *loadout) {
    if (![value isKindOfClass:NSDictionary.class]) return NO;
    NSInteger number, level, skill, awakenings, super;
    if (!ReadInteger(value[@"id"], 0, 65435, &number) || !ReadInteger(value[@"level"], 0, 120, &level) ||
        !ReadInteger(value[@"skill"], 0, 999, &skill) || !ReadInteger(value[@"awakenings"], -1, 10, &awakenings) ||
        !ReadInteger(value[@"superAwakening"], -1, 255, &super)) return NO;
    NSArray *plus = value[@"plus"], *latents = value[@"latents"];
    if (![plus isKindOfClass:NSArray.class] || plus.count != 3 || ![latents isKindOfClass:NSArray.class] || latents.count > 8) return NO;
    uint16_t additions[3];
    for (NSUInteger i = 0; i < 3; ++i) {
        NSInteger addition;
        if (!ReadInteger(plus[i], 0, assist ? 99 : 297, &addition)) return NO;
        additions[i] = (uint16_t)addition;
    }
    if (assist) {
        if (latents.count || (additions[0] != 0 && additions[0] != 99) || additions[0] != additions[1] || additions[1] != additions[2]) return NO;
        loadout->assistNumber = (uint16_t)number;
        loadout->assistLevel = (uint16_t)level;
        loadout->assistSkillLevel = (uint16_t)skill;
        loadout->assistAwakenings = (int16_t)awakenings;
        loadout->assistPlus = additions[0] == 99;
    } else {
        uint8_t encoded[3];
        if (!PHBattleEncodePlus(additions, encoded)) return NO;
        loadout->bodyNumber = (uint16_t)number;
        loadout->level = (uint16_t)level;
        loadout->skillLevel = (uint16_t)skill;
        loadout->awakenings = (int16_t)awakenings;
        loadout->superAwakening = (int16_t)super;
        memcpy(loadout->plus, additions, sizeof(additions));
        loadout->latentCount = (uint8_t)latents.count;
        unsigned cost = 0;
        for (NSUInteger i = 0; i < latents.count; ++i) {
            NSInteger latent;
            if (!ReadInteger(latents[i], 1, 52, &latent)) return NO;
            loadout->latents[i] = (uint8_t)latent;
            cost += PHBattleLatentCost((unsigned)latent);
        }
        if (cost > 8) return NO;
    }
    return YES;
}

BOOL PHFormationReadTeam(id value, PHBattleLoadout loadouts[6], NSString **reason) {
    PHBattleLoadout parsed[6] = {0};
    NSInteger schema;
    BOOL valid = [value isKindOfClass:NSDictionary.class] && ReadInteger(value[@"schema"], 1, 1, &schema) &&
        [value[@"title"] isKindOfClass:NSString.class] && [value[@"title"] length] <= 80 &&
        [value[@"slots"] isKindOfClass:NSArray.class] && [value[@"slots"] count] == 6;
    if (valid) for (NSUInteger i = 0; i < 6; ++i) {
        id slot = value[@"slots"][i];
        if (![slot isKindOfClass:NSDictionary.class] || !ReadCard(slot[@"body"], NO, &parsed[i]) ||
            !ReadCard(slot[@"assist"], YES, &parsed[i])) { valid = NO; break; }
        parsed[i].parameters = true;
    }
    if (!valid) { if (reason) *reason = @"編隊資料格式或數值無效，未套用"; return NO; }
    if (loadouts) memcpy(loadouts, parsed, sizeof(parsed));
    return YES;
}

BOOL PHFormationReadApply(id value, PHBattleLoadout loadouts[6], uint8_t *mask, NSString **reason) {
    NSInteger selected = 0;
    PHBattleLoadout parsed[6];
    if (![value isKindOfClass:NSDictionary.class] || !ReadInteger(value[@"mask"], 1, 63, &selected)) {
        if (reason) *reason = @"要套用的位置無效";
        return NO;
    }
    if (!PHFormationReadTeam(value[@"team"], parsed, reason)) return NO;
    for (NSUInteger i = 0; i < 6; ++i) if ((selected & (1u << i)) && !parsed[i].bodyNumber) {
        if (reason) *reason = @"要套用的位置尚未選擇本體";
        return NO;
    }
    if (loadouts) memcpy(loadouts, parsed, sizeof(parsed));
    if (mask) *mask = (uint8_t)selected;
    return YES;
}

BOOL PHFormationReadRestoreSlot(id value, NSUInteger *slot) {
    NSInteger selected = 0;
    if (!ReadInteger(value, 0, 6, &selected)) return NO;
    if (slot) *slot = (NSUInteger)selected;
    return YES;
}

static BOOL ValidTeamID(id value) {
    if (![value isKindOfClass:NSString.class] || ![value length] || [value length] > 80) return NO;
    NSCharacterSet *allowed = [NSCharacterSet characterSetWithCharactersInString:
        @"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"];
    return [value rangeOfCharacterFromSet:allowed.invertedSet].location == NSNotFound;
}

BOOL PHFormationReadLibrary(id value, NSDictionary **selectedTeam, NSString **reason) {
    NSInteger schema = 0;
    BOOL valid = [value isKindOfClass:NSDictionary.class] && ReadInteger(value[@"schema"], 1, 1, &schema) &&
        ValidTeamID(value[@"selected"]) && [value[@"teams"] isKindOfClass:NSArray.class] &&
        [value[@"teams"] count] > 0 && [value[@"teams"] count] <= PHFormationLibraryCapacity;
    NSMutableSet *ids = [NSMutableSet set];
    NSDictionary *selected = nil;
    if (valid) for (id record in value[@"teams"]) {
        if (![record isKindOfClass:NSDictionary.class] || !ValidTeamID(record[@"id"]) ||
            [ids containsObject:record[@"id"]] || !PHFormationReadTeam(record[@"team"], NULL, NULL)) {
            valid = NO; break;
        }
        [ids addObject:record[@"id"]];
        if ([record[@"id"] isEqual:value[@"selected"]]) selected = record[@"team"];
    }
    if (!valid || !selected) { if (reason) *reason = @"隊伍庫格式、隊伍數量或目前選取的隊伍無效"; return NO; }
    if (selectedTeam) *selectedTeam = selected;
    return YES;
}

NSDictionary *PHFormationLibraryFromTeam(NSDictionary *team) {
    BOOL legacy = PHFormationReadTeam(team, NULL, NULL);
    if (!legacy) {
        NSDictionary *body = @{@"id":@0, @"level":@0, @"skill":@0, @"awakenings":@-1,
            @"superAwakening":@-1, @"plus":@[@297,@297,@297], @"latents":@[]};
        NSDictionary *assist = @{@"id":@0, @"level":@0, @"skill":@1, @"awakenings":@-1,
            @"superAwakening":@0, @"plus":@[@99,@99,@99], @"latents":@[]};
        NSMutableArray *slots = [NSMutableArray array];
        for (unsigned i = 0; i < 6; ++i) [slots addObject:@{@"body":body, @"assist":assist}];
        team = @{@"schema":@1, @"title":@"我的隊伍", @"slots":slots};
    }
    NSString *identifier = legacy ? @"legacy" : @"default";
    NSDictionary *library = @{@"schema":@1, @"selected":identifier, @"teams":@[@{@"id":identifier, @"team":team}]};
    NSData *data = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
}

NSDictionary *PHFormationLibraryUpdatingTeam(NSDictionary *library, NSDictionary *team) {
    if (!PHFormationReadLibrary(library, NULL, NULL) || !PHFormationReadTeam(team, NULL, NULL)) return nil;
    NSMutableArray *records = [library[@"teams"] mutableCopy];
    for (NSUInteger i = 0; i < records.count; ++i) if ([records[i][@"id"] isEqual:library[@"selected"]]) {
        records[i] = @{@"id":library[@"selected"], @"team":team}; break;
    }
    NSDictionary *updated = @{@"schema":@1, @"selected":library[@"selected"], @"teams":records};
    NSData *data = [NSJSONSerialization dataWithJSONObject:updated options:0 error:NULL];
    return [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
}
