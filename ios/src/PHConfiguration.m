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

static NSDictionary *PHConfigurationCopy(NSDictionary *configuration) {
    if (!PHConfigurationIsValid(configuration)) return nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:configuration options:0 error:NULL];
    return data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
}

static BOOL PHConfigurationTeamClaimed(NSDictionary *profiles, NSString *teamID, NSString *profileID) {
    for (NSDictionary *record in profiles[@"profiles"])
        if (![record[@"id"] isEqual:profileID] && [record[@"formationID"] isEqual:teamID]) return YES;
    return NO;
}

// Legacy files have no durable link. Only an unambiguous, complete snapshot is
// evidence for migration; matching titles or six body IDs is not sufficient.
static NSString *PHConfigurationLegacyTeamID(NSDictionary *formations, NSDictionary *profile, NSDictionary *profiles) {
    NSString *identifier = nil;
    NSUInteger compatible = 0;
    for (NSDictionary *other in profiles[@"profiles"])
        if (![other[@"id"] isEqual:profile[@"id"]] && !other[@"formationID"] &&
            [other[@"formation"] isEqual:profile[@"formation"]]) return nil;
    for (NSDictionary *record in formations[@"teams"]) {
        if (PHConfigurationTeamClaimed(profiles, record[@"id"], profile[@"id"]) ||
            !PHOrbProfileUpdatingFormation(profile, record[@"team"])) continue;
        if (++compatible > 1) return nil;
        if ([record[@"team"] isEqual:profile[@"formation"]]) identifier = record[@"id"];
    }
    return identifier;
}

NSDictionary *PHConfigurationSavingFormations(NSDictionary *configuration, NSDictionary *formations, NSString **reason) {
    if (reason) *reason = @"隊伍庫或目前設定無效，尚未儲存。";
    if (!PHConfigurationIsValid(configuration) || !PHFormationReadLibrary(formations, NULL, NULL)) return nil;
    NSMutableDictionary *next = [configuration mutableCopy];
    NSMutableDictionary *library = [PHConfigurationOrbProfiles(configuration) mutableCopy];
    NSMutableArray *profiles = [library[@"profiles"] mutableCopy];
    library[@"profiles"] = profiles;
    for (NSUInteger i = 0; i < profiles.count; ++i) {
        NSDictionary *profile = profiles[i];
        if (!profile[@"formation"]) continue;
        NSString *identifier = profile[@"formationID"] ?: PHConfigurationLegacyTeamID(configuration[@"formations"], profile, library);
        if (!identifier) continue;
        NSMutableDictionary *updated = [profile mutableCopy]; updated[@"formationID"] = identifier;
        NSDictionary *team = PHFormationLibraryTeam(formations, identifier);
        // Keep an incompatible/deleted link visible. Reselecting that plan will
        // ask for an explicit compatible team instead of restoring stale data.
        profiles[i] = PHOrbProfileUpdatingFormation(updated, team) ?: updated;
    }
    next[@"formations"] = formations; next[@"orbProfiles"] = library;
    NSDictionary *result = PHConfigurationCopy(next);
    if (result && reason) *reason = nil;
    return result;
}

static BOOL PHConfigurationSameProfileRevision(NSDictionary *base, NSDictionary *current) {
    if (!PHOrbProfilesValid(base) || !PHOrbProfilesValid(current)) return NO;
    NSMutableDictionary *left = [base mutableCopy], *right = [current mutableCopy];
    [left removeObjectForKey:@"profiles"]; [right removeObjectForKey:@"profiles"];
    if (![left isEqual:right] || [base[@"profiles"] count] != [current[@"profiles"] count]) return NO;
    for (NSUInteger i = 0; i < [base[@"profiles"] count]; ++i) {
        NSDictionary *before = base[@"profiles"][i], *now = current[@"profiles"][i];
        if (before[@"formation"] && !PHOrbProfileUpdatingFormation(before, now[@"formation"])) return NO;
        left = [before mutableCopy]; right = [now mutableCopy];
        [left removeObjectForKey:@"formation"]; [right removeObjectForKey:@"formation"];
        if (![left isEqual:right]) return NO;
    }
    return YES;
}

NSDictionary *PHConfigurationSavingOrbProfiles(NSDictionary *configuration, NSDictionary *profiles,
    NSDictionary *settings, NSDictionary *baseProfiles, NSString **reason) {
    if (reason) *reason = @"策略格式或目前設定無效，尚未儲存。";
    if (!PHConfigurationIsValid(configuration) || !PHOrbProfilesValid(profiles) || !PHOrbCanonicalSettings(settings)) return nil;
    NSDictionary *previous = PHConfigurationOrbProfiles(configuration);
    if (baseProfiles && !PHConfigurationSameProfileRevision(baseProfiles, previous)) {
        if (reason) *reason = @"策略已在其他操作中更新；請關閉後重新開啟，避免蓋掉較新的設定。";
        return nil;
    }
    NSMutableDictionary *next = [configuration mutableCopy], *library = [profiles mutableCopy];
    NSMutableArray *records = [profiles[@"profiles"] mutableCopy]; library[@"profiles"] = records;
    NSDictionary *formations = configuration[@"formations"];
    for (NSUInteger i = 0; i < records.count; ++i) {
        NSDictionary *profile = records[i];
        if (!profile[@"formation"]) continue;
        NSDictionary *old = PHOrbProfileNamed(previous, profile[@"id"]);
        // Deleting then re-importing the same ID in this editor deliberately
        // removes its old local link. Preserve the old team as an independent
        // record, but let the newly reviewed package establish a fresh link.
        if (!profile[@"formationID"] && old[@"formationID"] &&
            PHOrbProfileNamed(baseProfiles, profile[@"id"])[@"formationID"]) old = nil;
        BOOL selected = [profile[@"id"] isEqual:library[@"selected"]];
        NSString *identifier = profile[@"formationID"] ?: old[@"formationID"];
        if (!identifier && old) identifier = PHConfigurationLegacyTeamID(formations, old, library);
        if (!identifier && (selected || !old)) {
            BOOL ambiguous = NO;
            if (old) for (NSDictionary *record in formations[@"teams"])
                if (PHOrbProfileUpdatingFormation(profile, record[@"team"])) ambiguous = YES;
            if (ambiguous) {
                if (reason) *reason = @"隊伍庫有相同或修改過的配裝；請在策略的「使用的隊伍」選回要保留的一份。";
                return nil;
            }
            if (!PHFormationReadLibrary(formations, NULL, NULL) || [formations[@"teams"] count] >= PHFormationLibraryCapacity) {
                if (reason) *reason = @"隊伍庫已滿或資料無效；整包未儲存，請先整理隊伍庫。";
                return nil;
            }
            identifier = NSUUID.UUID.UUIDString.lowercaseString;
            NSMutableArray *teams = [formations[@"teams"] mutableCopy];
            [teams addObject:@{@"id":identifier, @"team":profile[@"formation"]}];
            formations = @{@"schema":@1, @"selected":selected ? identifier : formations[@"selected"], @"teams":teams};
        }
        if (!identifier) continue;
        NSMutableDictionary *linked = [profile mutableCopy]; linked[@"formationID"] = identifier;
        NSDictionary *team = PHFormationLibraryTeam(formations, identifier);
        NSDictionary *updated = PHOrbProfileUpdatingFormation(linked, team);
        if (!updated && (selected || ![old[@"formationID"] isEqual:identifier])) {
            if (reason) *reason = @"策略使用的隊伍已刪除或本體／武裝已更換；請選擇相容隊伍，或重新匯入配套開場。";
            return nil;
        }
        // The latest saved team wins over the editor's older loadout snapshot.
        records[i] = updated ?: PHOrbProfileUpdatingFormation(linked, old[@"formation"]) ?: linked;
        if (selected) formations = @{@"schema":@1, @"selected":identifier, @"teams":formations[@"teams"]};
    }
    NSDictionary *selected = PHOrbProfileNamed(library, library[@"selected"]);
    next[@"orbProfiles"] = library;
    next[@"orbs"] = PHOrbCanonicalSettings(selected ? selected[@"settings"] : settings);
    if (formations) next[@"formations"] = formations;
    NSDictionary *result = PHConfigurationCopy(next);
    if (result && reason) *reason = nil;
    return result;
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
