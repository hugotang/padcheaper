#import "PHOrbProfiles.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>

static BOOL PHOrbProfileIsBoolean(id value) {
    return [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
}
static BOOL Integer(id value, NSUInteger maximum) {
    if (![value isKindOfClass:NSNumber.class] || PHOrbProfileIsBoolean(value)) return NO;
    double n = [value doubleValue];
    return isfinite(n) && n >= 0 && n <= maximum && trunc(n) == n;
}
static BOOL Identifier(id value, BOOL empty) {
    if (![value isKindOfClass:NSString.class] || [value length] > 80 || (!empty && ![value length])) return NO;
    NSCharacterSet *allowed = [NSCharacterSet characterSetWithCharactersInString:
        @"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"];
    return [value rangeOfCharacterFromSet:allowed.invertedSet].location == NSNotFound;
}
static BOOL Keys(NSDictionary *value, NSArray *keys) {
    return [[NSSet setWithArray:value.allKeys] isEqual:[NSSet setWithArray:keys]];
}

NSDictionary *PHOrbDefaultProfiles(void) {
    return @{@"schema":@1, @"selected":@"", @"autoSelect":@NO, @"profiles":@[]};
}

NSDictionary *PHOrbCanonicalSettings(id settings) {
    if (![settings isKindOfClass:NSDictionary.class]) return nil;
    NSString *quality = settings[@"quality"], *mode = settings[@"mode"];
    PHOrbGoalSet goals = {0};
    NSArray *rules = settings[@"rules"] ?: @[];
    NSDictionary *recovery = settings[@"recovery"] ?: PHOrbDefaultRecovery();
    if (![quality isKindOfClass:NSString.class] || ![@[@"fast", @"balanced", @"thorough"] containsObject:quality] ||
        ![mode isKindOfClass:NSString.class] || ![@[@"combo", @"manual", @"leaders"] containsObject:mode] ||
        !PHOrbReadGoals(settings[@"goals"], &goals) || ([mode isEqual:@"manual"] && !goals.count) ||
        !PHOrbSkillRulesValid(rules) || !PHOrbRecoveryValid(recovery)) return nil;
    NSMutableArray *saved = [NSMutableArray array];
    for (NSDictionary *rule in rules) [saved addObject:PHOrbNormalizedSkillRule(rule)];
    return @{@"quality":quality, @"mode":mode, @"goals":PHOrbGoalsJSON(&goals), @"rules":[saved copy], @"recovery":[recovery copy]};
}

NSDictionary *PHOrbProfileNamed(NSDictionary *library, NSString *identifier) {
    for (NSDictionary *record in library[@"profiles"]) if ([record[@"id"] isEqual:identifier]) return record;
    return nil;
}

BOOL PHOrbProfilesValid(id library) {
    if (![library isKindOfClass:NSDictionary.class] || !Keys(library, @[@"schema", @"selected", @"autoSelect", @"profiles"]) ||
        !Integer(library[@"schema"], 1) || ![library[@"schema"] isEqual:@1] || !Identifier(library[@"selected"], YES) ||
        !PHOrbProfileIsBoolean(library[@"autoSelect"]) || ![library[@"profiles"] isKindOfClass:NSArray.class] ||
        [library[@"profiles"] count] > PHOrbMaxProfiles) return NO;
    NSMutableSet *ids = [NSMutableSet set];
    for (id record in library[@"profiles"]) {
        if (![record isKindOfClass:NSDictionary.class] || !Keys(record, @[@"id", @"name", @"team", @"dungeon", @"floor", @"settings"]) ||
            !Identifier(record[@"id"], NO) || [ids containsObject:record[@"id"]] ||
            ![record[@"name"] isKindOfClass:NSString.class] || [record[@"name"] length] > 60 ||
            ![[record[@"name"] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] length] ||
            ![record[@"team"] isKindOfClass:NSArray.class] || [record[@"team"] count] != 6 ||
            !Integer(record[@"dungeon"], UINT16_MAX) || !Integer(record[@"floor"], UINT16_MAX) ||
            (![record[@"dungeon"] unsignedIntValue] && [record[@"floor"] unsignedIntValue]) ||
            !PHOrbCanonicalSettings(record[@"settings"]) ||
            !(Keys(record[@"settings"], @[@"quality", @"mode", @"goals", @"rules"]) ||
              Keys(record[@"settings"], @[@"quality", @"mode", @"goals", @"rules", @"recovery"]))) return NO;
        for (id number in record[@"team"]) {
            uint16_t catalogID = 0;
            if (!Integer(number, UINT16_MAX) || ([number unsignedIntValue] &&
                !PHBattleCatalogIDForNumber([number unsignedIntValue], &catalogID))) return NO;
        }
        [ids addObject:record[@"id"]];
    }
    return ![library[@"selected"] length] || [ids containsObject:library[@"selected"]];
}

NSDictionary *PHOrbProfilesUpdatingSettings(NSDictionary *library, NSDictionary *settings) {
    NSDictionary *canonical = PHOrbCanonicalSettings(settings);
    if (!PHOrbProfilesValid(library) || !canonical) return nil;
    if (![library[@"selected"] length]) return library;
    NSMutableArray *profiles = [NSMutableArray array];
    for (NSDictionary *record in library[@"profiles"]) {
        NSMutableDictionary *updated = [record mutableCopy];
        if ([record[@"id"] isEqual:library[@"selected"]]) updated[@"settings"] = canonical;
        [profiles addObject:[updated copy]];
    }
    NSMutableDictionary *result = [library mutableCopy]; result[@"profiles"] = [profiles copy];
    return [result copy];
}

static BOOL Matches(NSDictionary *record, const PHOrbSnapshot *state) {
    if ([record[@"dungeon"] unsignedIntValue] && [record[@"dungeon"] unsignedIntValue] != state->identity.dungeon) return NO;
    if ([record[@"floor"] unsignedIntValue] && [record[@"floor"] unsignedIntValue] != state->identity.floor) return NO;
    for (NSUInteger i = 0; i < 6; ++i) {
        unsigned number = [record[@"team"][i] unsignedIntValue];
        if (number && (!state->automation.members[i].active || number != state->automation.members[i].number)) return NO;
    }
    return YES;
}

NSDictionary *PHOrbResolveProfile(NSDictionary *library, const PHOrbSnapshot *state) {
    if (!PHOrbProfilesValid(library) || !state || !state->valid || !state->present || !state->automation.valid ||
        state->multiplayerDisabled || !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) ||
        state->identity.sourceMode != 0) return @{@"ok":@NO, @"message":@"無法核對策略所需的單人隊伍及關卡資料"};
    if (![library[@"autoSelect"] boolValue]) {
        NSDictionary *record = PHOrbProfileNamed(library, library[@"selected"]);
        if (!record) return @{@"ok":@YES, @"profile":@{}, @"message":@"通用設定"};
        if (!Matches(record, state)) return @{@"ok":@NO, @"message":[NSString stringWithFormat:
            @"策略「%@」與目前隊伍或關卡不符；請選擇相符策略後再開始", record[@"name"]]};
        return @{@"ok":@YES, @"profile":record, @"message":record[@"name"]};
    }
    NSDictionary *best = nil; NSUInteger bestScore = 0, ties = 0;
    for (NSDictionary *record in library[@"profiles"]) if (Matches(record, state)) {
        NSUInteger score = [record[@"floor"] unsignedIntValue] ? 128 : [record[@"dungeon"] unsignedIntValue] ? 64 : 0;
        for (NSNumber *number in record[@"team"]) score += number.unsignedIntValue != 0;
        if (!best || score > bestScore) { best = record; bestScore = score; ties = 1; }
        else if (score == bestScore) ++ties;
    }
    if (!best) return @{@"ok":@NO, @"message":@"沒有符合目前隊伍與關卡的策略；請選擇策略或關閉開始時自動配對"};
    if (ties > 1) return @{@"ok":@NO, @"message":@"有多個同樣具體的策略符合目前隊伍與關卡；請改為手動選擇"};
    return @{@"ok":@YES, @"profile":best, @"message":best[@"name"]};
}
