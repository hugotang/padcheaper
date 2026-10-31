#import "PHConfiguration.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#import "PHFormationModel.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>

static NSDictionary *PHDefaultBattleFilters(void) {
    return @{@"allowAllModes": @YES, @"entryModes": @[@0, @1, @12],
             @"allowAllSources": @NO, @"sourceModes": @[@0],
             @"allowAllMulti": @NO, @"multiModes": @[@0], @"autoApplyTeam": @NO};
}

NSDictionary *PHDefaultConfiguration(void) {
    return @{@"schema": @1, @"experimentalWrites": @NO,
             @"features": @{@"skin": @NO, @"bgm": @NO, @"bgmUncleared": @NO, @"badge": @NO, @"timerFix": @NO, @"battleSwap": @NO},
             @"apiHosts": @[], @"apiPaths": @[], @"battle": PHDefaultBattleFilters(),
             @"orbs": @{@"quality": @"balanced", @"mode": @"combo", @"goals": @[], @"rules": @[],
                 @"recovery":PHOrbDefaultRecovery(), @"thornComboPriority":@NO, @"allowPoisonWithHeartFour":@NO, @"diagonal":@NO},
             @"orbProfiles":PHOrbDefaultProfiles()};
}

static BOOL PHIsBoolean(id value) {
    return [value isKindOfClass:NSNumber.class] &&
           CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
}

static BOOL PHValidFilter(id values, uint32_t maximum) {
    if (![values isKindOfClass:NSArray.class] || [values count] > PHBattleFilterCapacity) return NO;
    NSMutableSet *seen = [NSMutableSet set];
    for (id value in values) {
        if (![value isKindOfClass:NSNumber.class] || PHIsBoolean(value)) return NO;
        double number = [value doubleValue];
        if (!isfinite(number) || number != trunc(number) || number < 0 || number > maximum || [seen containsObject:value]) return NO;
        [seen addObject:value];
    }
    return YES;
}

NSDictionary *PHConfigurationBattleFilters(NSDictionary *configuration) {
    NSMutableDictionary *filters = [PHDefaultBattleFilters() mutableCopy];
    if ([configuration[@"battle"] isKindOfClass:NSDictionary.class]) {
        for (NSString *key in filters.allKeys) if (configuration[@"battle"][key]) filters[key] = configuration[@"battle"][key];
    }
    return filters;
}

NSDictionary *PHConfigurationOrbSettings(NSDictionary *configuration) {
    NSMutableDictionary *settings = [@{@"quality":@"balanced", @"mode":@"combo", @"goals":@[], @"rules":@[],
        @"recovery":PHOrbDefaultRecovery(), @"thornComboPriority":@NO, @"allowPoisonWithHeartFour":@NO,
        @"diagonal":@NO, @"assistRecoveryL":@NO} mutableCopy];
    if ([configuration[@"orbs"] isKindOfClass:NSDictionary.class])
        for (NSString *key in settings.allKeys) if (configuration[@"orbs"][key]) settings[key] = configuration[@"orbs"][key];
    return settings;
}

NSDictionary *PHConfigurationOrbProfiles(NSDictionary *configuration) {
    return configuration[@"orbProfiles"] ?: PHOrbDefaultProfiles();
}

PHBattlePolicy PHConfigurationBattlePolicy(NSDictionary *configuration) {
    NSDictionary *filters = PHConfigurationBattleFilters(configuration);
    PHBattlePolicy policy = {.configured = true};
    PHBattleValueFilter *targets[] = {&policy.modes, &policy.sources, &policy.multiplayer};
    NSArray *allKeys = @[@"allowAllModes", @"allowAllSources", @"allowAllMulti"];
    NSArray *listKeys = @[@"entryModes", @"sourceModes", @"multiModes"];
    for (NSUInteger i = 0; i < 3; ++i) {
        NSArray *values = filters[listKeys[i]];
        targets[i]->allowAll = [filters[allKeys[i]] boolValue];
        targets[i]->count = (uint8_t)MIN(values.count, (NSUInteger)PHBattleFilterCapacity);
        for (NSUInteger j = 0; j < targets[i]->count; ++j) targets[i]->values[j] = [values[j] unsignedIntValue];
    }
    return policy;
}

NSArray<NSNumber *> *PHConfigurationReadFilterText(NSString *text, uint32_t maximum) {
    if (![text isKindOfClass:NSString.class] || text.length > 512) return nil;
    NSMutableArray *values = [NSMutableArray array];
    NSCharacterSet *separators = [NSCharacterSet characterSetWithCharactersInString:@",，、 \t\r\n"];
    for (NSString *part in [text componentsSeparatedByCharactersInSet:separators]) {
        if (!part.length) continue;
        if (part.length > 10 || [part rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789"].invertedSet].location != NSNotFound) return nil;
        [values addObject:@(part.longLongValue)];
    }
    return PHValidFilter(values, maximum) ? values : nil;
}

BOOL PHConfigurationIsValid(NSDictionary *configuration) {
    if (![configuration isKindOfClass:NSDictionary.class]) return NO;
    id schema = configuration[@"schema"];
    NSDictionary *features = configuration[@"features"];
    if (![schema isKindOfClass:NSNumber.class] || PHIsBoolean(schema) || ![schema isEqual:@1] ||
        !PHIsBoolean(configuration[@"experimentalWrites"]) ||
        ![features isKindOfClass:NSDictionary.class]) return NO;
    for (NSString *key in @[@"skin", @"bgm", @"timerFix"]) {
        if (!PHIsBoolean(features[key])) return NO;
    }
    // Schema 1 files created by older dylibs remain valid; absent means off.
    if (features[@"battleSwap"] && !PHIsBoolean(features[@"battleSwap"])) return NO;
    if (features[@"badge"] && !PHIsBoolean(features[@"badge"])) return NO;
    if (features[@"bgmUncleared"] && !PHIsBoolean(features[@"bgmUncleared"])) return NO;
    if (configuration[@"orbs"] && ![configuration[@"orbs"] isKindOfClass:NSDictionary.class]) return NO;
    if (configuration[@"formations"] && !PHFormationReadLibrary(configuration[@"formations"], NULL, NULL)) return NO;
    if (!PHOrbProfilesValid(PHConfigurationOrbProfiles(configuration))) return NO;
    NSDictionary *orbs = PHConfigurationOrbSettings(configuration);
    id quality = orbs[@"quality"], mode = orbs[@"mode"];
    if (![quality isKindOfClass:NSString.class] || ![@[@"fast", @"balanced", @"thorough"] containsObject:quality]) return NO;
    if (![mode isKindOfClass:NSString.class] || ![@[@"combo", @"manual", @"leaders"] containsObject:mode] ||
        !PHOrbReadGoals(orbs[@"goals"], NULL) || ([mode isEqual:@"manual"] && ![orbs[@"goals"] count]) ||
        !PHOrbSkillRulesValid(orbs[@"rules"]) || !PHOrbRecoveryValid(orbs[@"recovery"]) ||
        !PHIsBoolean(orbs[@"thornComboPriority"]) || !PHIsBoolean(orbs[@"allowPoisonWithHeartFour"]) ||
        !PHIsBoolean(orbs[@"diagonal"]) || !PHIsBoolean(orbs[@"assistRecoveryL"])) return NO;
    NSDictionary *profiles = PHConfigurationOrbProfiles(configuration);
    NSDictionary *selectedProfile = PHOrbProfileNamed(profiles, profiles[@"selected"]);
    if (selectedProfile && ![PHOrbCanonicalSettings(orbs) isEqual:PHOrbCanonicalSettings(selectedProfile[@"settings"])]) return NO;
    if (configuration[@"battle"] && ![configuration[@"battle"] isKindOfClass:NSDictionary.class]) return NO;
    NSDictionary *battle = PHConfigurationBattleFilters(configuration);
    for (NSString *key in @[@"allowAllModes", @"allowAllSources", @"allowAllMulti", @"autoApplyTeam"]) if (!PHIsBoolean(battle[key])) return NO;
    if (!PHValidFilter(battle[@"entryModes"], UINT16_MAX) || !PHValidFilter(battle[@"sourceModes"], UINT16_MAX) ||
        !PHValidFilter(battle[@"multiModes"], UINT32_MAX)) return NO;
    for (NSString *key in @[@"apiHosts", @"apiPaths"]) {
        NSArray *values = configuration[key];
        if (![values isKindOfClass:NSArray.class] || values.count > 16) return NO;
        for (id value in values) {
            if (![value isKindOfClass:NSString.class] || [value length] == 0 || [value length] > 1024) return NO;
            if ([key isEqualToString:@"apiPaths"] && (![value hasPrefix:@"/"] ||
                [value rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"?#"]].location != NSNotFound)) return NO;
        }
    }
    return YES;
}
