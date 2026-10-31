#import "PHOrbProfiles.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import "PHFormationModel.h"
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
static BOOL KeysWithOptional(NSDictionary *value, NSArray *required, NSArray *optional) {
    if (![value isKindOfClass:NSDictionary.class]) return NO;
    NSSet *actual = [NSSet setWithArray:value.allKeys];
    NSMutableSet *allowed = [NSMutableSet setWithArray:required]; [allowed addObjectsFromArray:optional];
    return [[NSSet setWithArray:required] isSubsetOfSet:actual] && [actual isSubsetOfSet:allowed];
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
    NSNumber *thornComboPriority = settings[@"thornComboPriority"] ?: @NO;
    NSNumber *allowPoisonWithHeartFour = settings[@"allowPoisonWithHeartFour"] ?: @NO;
    NSNumber *diagonal = settings[@"diagonal"] ?: @NO;
    if (![quality isKindOfClass:NSString.class] || ![@[@"fast", @"balanced", @"thorough"] containsObject:quality] ||
        ![mode isKindOfClass:NSString.class] || ![@[@"combo", @"manual", @"leaders"] containsObject:mode] ||
        !PHOrbReadGoals(settings[@"goals"], &goals) || ([mode isEqual:@"manual"] && !goals.count) ||
        !PHOrbSkillRulesValid(rules) || !PHOrbRecoveryValid(recovery) || !PHOrbProfileIsBoolean(thornComboPriority) ||
        !PHOrbProfileIsBoolean(allowPoisonWithHeartFour) || !PHOrbProfileIsBoolean(diagonal)) return nil;
    NSMutableArray *saved = [NSMutableArray array];
    for (NSDictionary *rule in rules) [saved addObject:PHOrbNormalizedSkillRule(rule)];
    return @{@"quality":quality, @"mode":mode, @"goals":PHOrbGoalsJSON(&goals), @"rules":[saved copy],
        @"recovery":[recovery copy], @"thornComboPriority":thornComboPriority,
        @"allowPoisonWithHeartFour":allowPoisonWithHeartFour, @"diagonal":diagonal};
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
        if (![record isKindOfClass:NSDictionary.class] ||
            !(Keys(record, @[@"id", @"name", @"team", @"dungeon", @"floor", @"settings"]) ||
              Keys(record, @[@"id", @"name", @"team", @"dungeon", @"floor", @"settings", @"formation", @"opening", @"source"])) ||
            !Identifier(record[@"id"], NO) || [ids containsObject:record[@"id"]] ||
            ![record[@"name"] isKindOfClass:NSString.class] || [record[@"name"] length] > 60 ||
            ![[record[@"name"] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet] length] ||
            ![record[@"team"] isKindOfClass:NSArray.class] || [record[@"team"] count] != 6 ||
            !Integer(record[@"dungeon"], UINT16_MAX) || !Integer(record[@"floor"], UINT16_MAX) ||
            (![record[@"dungeon"] unsignedIntValue] && [record[@"floor"] unsignedIntValue]) ||
            !PHOrbCanonicalSettings(record[@"settings"]) ||
            !KeysWithOptional(record[@"settings"], @[@"quality", @"mode", @"goals", @"rules"],
                @[@"recovery", @"thornComboPriority", @"allowPoisonWithHeartFour", @"diagonal"])) return NO;
        for (id number in record[@"team"]) {
            uint16_t catalogID = 0;
            if (!Integer(number, UINT16_MAX) || ([number unsignedIntValue] &&
                !PHBattleCatalogIDForNumber([number unsignedIntValue], &catalogID))) return NO;
        }
        if (record[@"formation"]) {
            if (!PHFormationReadTeam(record[@"formation"], NULL, NULL) ||
                !PHOrbOpeningValid(record[@"opening"], record[@"formation"]) ||
                ![record[@"source"] isKindOfClass:NSString.class] || [record[@"source"] length] > 2048) return NO;
            for (NSUInteger i = 0; i < 6; ++i)
                if (![record[@"team"][i] isEqual:record[@"formation"][@"slots"][i][@"body"][@"id"]]) return NO;
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
    if (record[@"formation"]) return PHOrbProfileMatchesOpeningTeam(record, state, 0);
    for (NSUInteger i = 0; i < 6; ++i) {
        unsigned number = [record[@"team"][i] unsignedIntValue];
        if (number && (!state->automation.members[i].active || number != state->automation.members[i].number)) return NO;
    }
    return YES;
}

BOOL PHOrbOpeningValid(id opening, NSDictionary *formation) {
    if (![opening isKindOfClass:NSArray.class] || [opening count] > 24 || !PHFormationReadTeam(formation, NULL, NULL)) return NO;
    if (!Keys(formation, @[@"schema", @"title", @"slots"])) return NO;
    NSMutableArray *bodies = [NSMutableArray array], *assists = [NSMutableArray array];
    for (NSDictionary *slot in formation[@"slots"]) {
        if (!Keys(slot, @[@"body", @"assist"]) || ![slot[@"body"][@"id"] unsignedIntValue]) return NO;
        for (NSString *kind in @[@"body", @"assist"]) {
            NSDictionary *card = slot[kind]; uint16_t catalogID = 0;
            if (!Keys(card, @[@"id", @"level", @"skill", @"awakenings", @"plus", @"superAwakening", @"latents"]) ||
                ([card[@"id"] unsignedIntValue] && !PHBattleCatalogIDForNumber([card[@"id"] unsignedIntValue], &catalogID))) return NO;
        }
        [bodies addObject:slot[@"body"][@"id"]];
        [assists addObject:slot[@"assist"][@"id"]];
    }
    NSUInteger index = 0;
    for (id step in opening) {
        if (!KeysWithOptional(step, @[@"slot", @"source", @"monster", @"assist", @"skill", @"afterMonster"],
                @[@"consumeAssist", @"puzzleAfter"]) ||
            !Integer(step[@"slot"], 6) || ![step[@"slot"] unsignedIntValue] ||
            ![step[@"source"] isKindOfClass:NSString.class] || ![@[@"body", @"assist"] containsObject:step[@"source"]] ||
            !Integer(step[@"monster"], UINT16_MAX) || !Integer(step[@"assist"], UINT16_MAX) ||
            !Integer(step[@"skill"], 0x1ffff) || ![step[@"skill"] unsignedIntValue] || !Integer(step[@"afterMonster"], UINT16_MAX) ||
            (step[@"consumeAssist"] && !PHOrbProfileIsBoolean(step[@"consumeAssist"])) ||
            (step[@"puzzleAfter"] && !PHOrbProfileIsBoolean(step[@"puzzleAfter"]))) return NO;
        NSUInteger slot = [step[@"slot"] unsignedIntegerValue] - 1;
        uint16_t catalogID = 0;
        if (![step[@"monster"] isEqual:bodies[slot]] ||
            ![step[@"assist"] isEqual:assists[slot]] ||
            ([step[@"source"] isEqual:@"assist"] && ![step[@"assist"] unsignedIntValue]) ||
            ([step[@"afterMonster"] unsignedIntValue] && !PHBattleCatalogIDForNumber([step[@"afterMonster"] unsignedIntValue], &catalogID)) ||
            ([step[@"consumeAssist"] boolValue] && (![step[@"source"] isEqual:@"assist"] || [step[@"afterMonster"] unsignedIntValue])) ||
            ([step[@"puzzleAfter"] boolValue] && index + 1 != [opening count])) return NO;
        if ([step[@"afterMonster"] unsignedIntValue]) bodies[slot] = step[@"afterMonster"];
        if ([step[@"consumeAssist"] boolValue]) assists[slot] = @0;
        ++index;
    }
    return YES;
}

static uint8_t OpeningConsumedAssists(NSDictionary *profile, NSUInteger completed) {
    uint8_t mask = 0;
    for (NSUInteger i = 0; i < completed; ++i) {
        NSDictionary *step = profile[@"opening"][i];
        if ([step[@"consumeAssist"] boolValue]) mask |= 1u << ([step[@"slot"] unsignedIntegerValue] - 1);
    }
    return mask;
}

BOOL PHOrbProfileMatchesOpeningTeam(NSDictionary *profile, const PHOrbSnapshot *state, NSUInteger completed) {
    if (!state || !state->automation.valid || !profile[@"formation"] || completed > [profile[@"opening"] count]) return NO;
    NSMutableArray *bodies = [NSMutableArray array];
    for (NSDictionary *slot in profile[@"formation"][@"slots"]) [bodies addObject:slot[@"body"][@"id"]];
    for (NSUInteger i = 0; i < completed; ++i) {
        NSDictionary *step = profile[@"opening"][i];
        if ([step[@"afterMonster"] unsignedIntValue]) bodies[[step[@"slot"] unsignedIntegerValue] - 1] = step[@"afterMonster"];
    }
    const uint8_t consumed = OpeningConsumedAssists(profile, completed);
    for (NSUInteger i = 0; i < 6; ++i) {
        const PHOrbSkillMember *member = &state->automation.members[i];
        uint16_t assist = 0;
        unsigned number = [profile[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue];
        if (number && !PHBattleCatalogIDForNumber(number, &assist)) return NO;
        if (!member->active || member->temporaryTurns || member->number != [bodies[i] unsignedIntValue]) return NO;
        if (consumed & (1u << i)) {
            // Native type 250 clears the protected assist state and active
            // assist, retaining its source ID. Missing equipment alone is
            // insufficient evidence that this exact weapon was consumed.
            if (member->assistCatalogID || member->assistState || member->sourceCatalogIDs[1] != assist) return NO;
        } else if (member->assistCatalogID != assist) return NO;
    }
    return YES;
}

BOOL PHOrbProfileMatchesRunningTeam(NSDictionary *profile, const PHOrbSnapshot *state) {
    if (!state || !state->automation.valid) return NO;
    PHOrbSnapshot identity = *state;
    const uint8_t consumed = OpeningConsumedAssists(profile, [profile[@"opening"] count]);
    for (NSUInteger i = 0; i < 6; ++i) {
        PHOrbSkillMember *member = &identity.automation.members[i];
        if (!member->temporaryTurns) continue;
        if (!member->temporaryKnown || !member->temporaryID || !member->identityNumber) return NO;
        member->number = member->identityNumber; member->assistCatalogID = member->identityAssist;
        if (consumed & (1u << i)) {
            uint16_t source = 0;
            if (!PHBattleCatalogIDForNumber([profile[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue], &source) ||
                member->identityAssist != source || member->assistState) return NO;
            member->assistCatalogID = 0;
        }
        member->temporaryTurns = 0;
    }
    return PHOrbProfileMatchesOpeningTeam(profile, &identity, [profile[@"opening"] count]);
}

NSDictionary *PHOrbProfileFromPlan(id plan, NSString **reason) {
    if (reason) *reason = @"整包設定格式、版本、隊伍或開場步驟無效；未匯入";
    if (![plan isKindOfClass:NSDictionary.class] || !Keys(plan, @[@"format", @"schema", @"target", @"id", @"name", @"formation",
            @"opening", @"rotation", @"board", @"dungeon", @"floor", @"source"]) ||
        ![plan[@"format"] isEqual:@"padcheaper-team-plan"] || !Integer(plan[@"schema"], 1) || ![plan[@"schema"] isEqual:@1] ||
        ![plan[@"target"] isEqual:@"jp.gungho.pad-ios-23.6.0-106878"] ||
        !PHFormationReadTeam(plan[@"formation"], NULL, NULL) || !PHOrbOpeningValid(plan[@"opening"], plan[@"formation"]) ||
        !KeysWithOptional(plan[@"board"], @[@"quality", @"mode", @"goals", @"recovery"],
            @[@"thornComboPriority", @"allowPoisonWithHeartFour", @"diagonal"])) return nil;
    NSMutableDictionary *settings = [plan[@"board"] mutableCopy]; settings[@"rules"] = plan[@"rotation"];
    NSDictionary *canonical = PHOrbCanonicalSettings(settings);
    if (!canonical) return nil;
    NSMutableArray *team = [NSMutableArray array];
    for (NSDictionary *slot in plan[@"formation"][@"slots"]) [team addObject:slot[@"body"][@"id"]];
    NSDictionary *profile = @{@"id":plan[@"id"], @"name":plan[@"name"], @"team":team, @"dungeon":plan[@"dungeon"],
        @"floor":plan[@"floor"], @"settings":canonical, @"formation":plan[@"formation"], @"opening":plan[@"opening"], @"source":plan[@"source"]};
    if (!PHOrbProfilesValid(@{@"schema":@1, @"selected":@"", @"autoSelect":@NO, @"profiles":@[profile]})) return nil;
    NSData *data = [NSJSONSerialization dataWithJSONObject:profile options:0 error:NULL];
    if (reason) *reason = nil;
    return data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
}

NSDictionary *PHOrbPlanFromProfile(NSDictionary *profile) {
    if (!profile[@"formation"] || !PHOrbProfilesValid(@{@"schema":@1, @"selected":@"", @"autoSelect":@NO, @"profiles":@[profile]})) return nil;
    NSMutableDictionary *board = [PHOrbCanonicalSettings(profile[@"settings"]) mutableCopy]; [board removeObjectForKey:@"rules"];
    return @{@"format":@"padcheaper-team-plan", @"schema":@1, @"target":@"jp.gungho.pad-ios-23.6.0-106878",
        @"id":profile[@"id"], @"name":profile[@"name"], @"formation":profile[@"formation"], @"opening":profile[@"opening"],
        @"rotation":profile[@"settings"][@"rules"], @"board":board, @"dungeon":profile[@"dungeon"],
        @"floor":profile[@"floor"], @"source":profile[@"source"]};
}

NSDictionary *PHOrbProfilesImportingPlan(NSDictionary *library, id plan, NSString **reason) {
    NSDictionary *profile = PHOrbProfileFromPlan(plan, reason);
    if (!profile || !PHOrbProfilesValid(library)) return nil;
    if (PHOrbProfileNamed(library, profile[@"id"])) {
        if (reason) *reason = @"相同 ID 的策略已存在；請先刪除舊版或修改匯入 ID，避免覆蓋設定";
        return nil;
    }
    if ([library[@"profiles"] count] >= PHOrbMaxProfiles) { if (reason) *reason = @"策略庫已滿（64 份）"; return nil; }
    NSMutableArray *profiles = [library[@"profiles"] mutableCopy]; [profiles addObject:profile];
    return @{@"schema":@1, @"selected":profile[@"id"], @"autoSelect":@NO, @"profiles":profiles};
}

NSDictionary *PHOrbFormationsSelectingProfile(NSDictionary *formations, NSDictionary *profile) {
    if (!PHFormationReadLibrary(formations, NULL, NULL) || !profile[@"formation"]) return nil;
    NSMutableArray *teams = [formations[@"teams"] mutableCopy]; NSString *identifier = nil;
    for (NSDictionary *record in teams) if ([record[@"team"] isEqual:profile[@"formation"]]) { identifier = record[@"id"]; break; }
    if (!identifier) {
        if (teams.count >= PHFormationLibraryCapacity) return nil;
        identifier = NSUUID.UUID.UUIDString.lowercaseString;
        [teams addObject:@{@"id":identifier, @"team":profile[@"formation"]}];
    }
    return @{@"schema":@1, @"selected":identifier, @"teams":teams};
}

NSData *PHOrbExportProfileData(NSDictionary *profile, NSString **reason) {
    if (reason) *reason = @"請先選擇一份有效策略";
    if (![profile isKindOfClass:NSDictionary.class] || !PHOrbProfilesValid(@{@"schema":@1, @"selected":@"",
            @"autoSelect":@NO, @"profiles":@[profile]})) return nil;
    NSMutableDictionary *canonical = [profile mutableCopy]; canonical[@"settings"] = PHOrbCanonicalSettings(profile[@"settings"]);
    NSDictionary *package = profile[@"formation"] ? PHOrbPlanFromProfile(canonical) :
        @{@"format":@"padcheaper-orb-profile", @"schema":@1, @"target":@"jp.gungho.pad-ios-23.6.0-106878", @"profile":canonical};
    NSData *data = [NSJSONSerialization dataWithJSONObject:package options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL];
    if (!data.length || data.length > 131072) { if (reason) *reason = @"分享檔案超過單份 128 KiB 上限"; return nil; }
    if (reason) *reason = nil;
    return data;
}

NSDictionary *PHOrbProfilesImportingData(NSDictionary *library, NSData *data, NSString **reason) {
    if (reason) *reason = @"JSON 格式、版本或內容無效；未更動隊伍與策略";
    if (!PHOrbProfilesValid(library) || ![data isKindOfClass:NSData.class] || !data.length || data.length > 131072) return nil;
    id package = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    if (![package isKindOfClass:NSDictionary.class]) return nil;
    if ([package[@"format"] isEqual:@"padcheaper-team-plan"]) return PHOrbProfilesImportingPlan(library,package,reason);
    if (!Keys(package,@[@"format",@"schema",@"target",@"profile"]) ||
        ![package[@"format"] isEqual:@"padcheaper-orb-profile"] || !Integer(package[@"schema"],1) ||
        ![package[@"schema"] isEqual:@1] || ![package[@"target"] isEqual:@"jp.gungho.pad-ios-23.6.0-106878"] ||
        ![package[@"profile"] isKindOfClass:NSDictionary.class]) return nil;
    NSDictionary *profile = package[@"profile"];
    if (profile[@"formation"] || !PHOrbProfilesValid(@{@"schema":@1, @"selected":@"", @"autoSelect":@NO, @"profiles":@[profile]})) return nil;
    if (PHOrbProfileNamed(library,profile[@"id"])) { if (reason) *reason = @"相同 ID 的策略已存在；未覆蓋舊設定"; return nil; }
    if ([library[@"profiles"] count] >= PHOrbMaxProfiles) { if (reason) *reason = @"策略庫已滿（64 份）"; return nil; }
    NSMutableDictionary *canonical = [profile mutableCopy]; canonical[@"settings"] = PHOrbCanonicalSettings(profile[@"settings"]);
    NSArray *profiles = [library[@"profiles"] arrayByAddingObject:[canonical copy]];
    if (reason) *reason = nil;
    return @{@"schema":@1, @"selected":profile[@"id"], @"autoSelect":@NO, @"profiles":profiles};
}

NSString *PHOrbPlanDescription(NSDictionary *plan) {
    NSDictionary *profile = PHOrbProfileFromPlan(plan, NULL);
    if (!profile) return @"無效整包設定";
    NSMutableArray *lines = [NSMutableArray arrayWithObject:profile[@"name"]];
    for (NSUInteger i = 0; i < 6; ++i) {
        NSDictionary *slot = profile[@"formation"][@"slots"][i];
        [lines addObject:[NSString stringWithFormat:@"位置 %lu：#%@／武裝 #%@ · Lv.%@", (unsigned long)i + 1,
            slot[@"body"][@"id"], slot[@"assist"][@"id"], slot[@"body"][@"level"]]];
    }
    [lines addObject:@"\n開場（依序完成）："];
    NSUInteger index = 0;
    for (NSDictionary *step in profile[@"opening"]) [lines addObject:[NSString stringWithFormat:@"%lu. 位置 %@ %@技能 %@%@%@%@",
        (unsigned long)++index, step[@"slot"], [step[@"source"] isEqual:@"body"] ? @"本體" : @"武裝", step[@"skill"],
        [step[@"afterMonster"] unsignedIntValue] ? [NSString stringWithFormat:@" → #%@", step[@"afterMonster"]] : @"",
        [step[@"consumeAssist"] boolValue] ? @" · 確認武裝消耗" : @"",
        [step[@"puzzleAfter"] boolValue] ? @" · 先轉此盤再接循環" : @""]];
    if (!index) [lines addObject:@"沒有開場步驟，開始後使用循環規則"];
    [lines addObject:@"\n循環（依優先順序）："];
    for (NSDictionary *rule in profile[@"settings"][@"rules"]) [lines addObject:PHOrbSkillRuleDescription(rule)];
    [lines addObject:[NSString stringWithFormat:@"\n盤面目標：%@\n求路品質：%@", PHOrbGoalsDescription(profile[@"settings"][@"goals"]), profile[@"settings"][@"quality"]]];
    [lines addObject:[profile[@"settings"][@"diagonal"] boolValue] ?
        @"斜向轉珠：開啟；可走相鄰八方向，消除仍按橫直規則" : @"斜向轉珠：關閉；只走上下左右"];
    [lines addObject:[profile[@"settings"][@"thornComboPriority"] boolValue] ?
        @"荊棘盤優先 Combo：開啟；首消心四連且拖珠傷害低於目前 HP 才可接觸" : @"荊棘盤優先 Combo：關閉；優先較少傷害"];
    [lines addObject:[profile[@"settings"][@"allowPoisonWithHeartFour"] boolValue] ?
        @"心四連時允許消一般毒：開啟；採用已確認的隊伍回復能力，優先目標與 Combo；猛毒仍避開，爆彈須首消全消" :
        @"心四連時允許消一般毒：關閉；避免毒／猛毒消除，爆彈須首消全消"];
    [lines addObject:@"\n儲存並選用會同時選取隊伍與策略；入場套隊沿用既有開關。按「開始自動轉」才執行開場及循環。"];
    return [lines componentsJoinedByString:@"\n"];
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
