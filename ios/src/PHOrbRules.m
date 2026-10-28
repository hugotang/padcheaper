#import "PHOrbRules.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>

NSDictionary *PHOrbDefaultSkillRule(void) {
    return @{@"enabled":@YES, @"slot":@1, @"monster":@0, @"every":@1, @"floor":@0,
             @"color":@-1, @"below":@0, @"enemy":@"none", @"group":@0, @"once":@NO,
             @"reserveUntil":@0, @"obstacle":@"none"};
}

NSDictionary *PHOrbNormalizedSkillRule(NSDictionary *rule) {
    NSMutableDictionary *result = [PHOrbDefaultSkillRule() mutableCopy];
    if ([rule isKindOfClass:NSDictionary.class]) [result addEntriesFromDictionary:rule];
    return [result copy];
}

NSArray<NSString *> *PHOrbObstacleConditions(void) {
    return @[@"none", @"unmatchable", @"special_orbs", @"orb_effect", @"board_effect", @"spinner", @"locked", @"weakened"];
}

NSString *PHOrbObstacleConditionTitle(NSString *condition) {
    return @{@"none":@"不限定盤面妨礙", @"unmatchable":@"有不可消效果",
        @"special_orbs":@"有基本六色以外的珠", @"orb_effect":@"有未支援的珠子效果",
        @"board_effect":@"有特殊盤面效果", @"spinner":@"有轉轉珠", @"locked":@"有鎖珠", @"weakened":@"有弱化珠"}[condition] ?: @"未知盤面條件";
}

static unsigned ObstacleFlag(NSString *condition) {
    return [@{@"unmatchable":@(PHOrbObstacleUnmatchable), @"special_orbs":@(PHOrbObstacleSpecialOrbs),
        @"orb_effect":@(PHOrbObstacleOrbEffect), @"board_effect":@(PHOrbObstacleBoardEffect),
        @"spinner":@(PHOrbObstacleSpinner), @"locked":@(PHOrbObstacleLocked), @"weakened":@(PHOrbObstacleWeakened)}[condition] unsignedIntValue];
}

NSString *PHOrbObstacleDescription(const PHOrbSnapshot *state) {
    if (!state || !state->valid || state->phase != 2) return @"";
    unsigned flags = PHOrbBoardObstacles(state);
    NSMutableArray *parts = [NSMutableArray array];
    for (NSUInteger i = 1; i < PHOrbObstacleConditions().count; ++i) {
        unsigned flag = ObstacleFlag(PHOrbObstacleConditions()[i]);
        if (flags & flag) [parts addObject:PHOrbObstacleConditionTitle(PHOrbObstacleConditions()[i])];
    }
    if (!state->ordinary && !(flags & ~PHOrbObstacleLocked)) [parts addObject:@"特殊操作規則或動畫尚未結束"];
    return [parts componentsJoinedByString:@"、"];
}

NSArray<NSString *> *PHOrbEnemyConditions(void) {
    return @[@"none", @"damage_absorb", @"attribute_absorb", @"damage_void", @"combo_absorb"];
}

NSString *PHOrbEnemyConditionTitle(NSString *condition) {
    return @{@"none":@"不限定敵人效果", @"damage_absorb":@"傷害吸收", @"attribute_absorb":@"屬性吸收",
             @"damage_void":@"傷害無效", @"combo_absorb":@"Combo 吸收"}[condition] ?: @"未知敵人條件";
}

static BOOL Boolean(id value) {
    return [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
}
static BOOL Integer(id value, NSInteger minimum, NSInteger maximum) {
    if (![value isKindOfClass:NSNumber.class] || Boolean(value)) return NO;
    double number = [value doubleValue];
    return isfinite(number) && trunc(number) == number && number >= minimum && number <= maximum;
}

BOOL PHOrbSkillRulesValid(id rules) {
    if (![rules isKindOfClass:NSArray.class] || [rules count] > PHOrbMaxSkillRules) return NO;
    NSSet *keys = [NSSet setWithArray:PHOrbDefaultSkillRule().allKeys];
    NSSet *required = [NSSet setWithArray:@[@"enabled", @"slot", @"monster", @"every", @"floor", @"color", @"below", @"enemy"]];
    for (id raw in rules) {
        if (![raw isKindOfClass:NSDictionary.class]) return NO;
        NSSet *actual = [NSSet setWithArray:[raw allKeys]];
        if (![actual isSubsetOfSet:keys] || ![required isSubsetOfSet:actual]) return NO;
        NSDictionary *rule = PHOrbNormalizedSkillRule(raw);
        if (
            !Boolean(rule[@"enabled"]) || !Integer(rule[@"slot"], 1, 6) || !Integer(rule[@"monster"], 0, UINT16_MAX) ||
            !Integer(rule[@"every"], 1, 999) || !Integer(rule[@"floor"], 0, 999) || !Integer(rule[@"color"], -1, 5) ||
            !Integer(rule[@"below"], 0, PHOrbMaxCells + 1) || ![rule[@"enemy"] isKindOfClass:NSString.class] ||
            ![PHOrbEnemyConditions() containsObject:rule[@"enemy"]] || !Integer(rule[@"group"], 0, PHOrbMaxSkillGroups) ||
            !Boolean(rule[@"once"]) || !Integer(rule[@"reserveUntil"], 0, 999) ||
            ![rule[@"obstacle"] isKindOfClass:NSString.class] || ![PHOrbObstacleConditions() containsObject:rule[@"obstacle"]] ||
            ([rule[@"floor"] unsignedIntValue] && [rule[@"floor"] unsignedIntValue] < [rule[@"reserveUntil"] unsignedIntValue])) return NO;
        uint16_t catalogID = 0;
        if ([rule[@"monster"] unsignedIntValue] && !PHBattleCatalogIDForNumber([rule[@"monster"] unsignedIntValue], &catalogID)) return NO;
        if ([rule[@"color"] integerValue] < 0 ? [rule[@"below"] integerValue] != 0 : [rule[@"below"] integerValue] < 1) return NO;
    }
    return YES;
}

NSUInteger PHOrbSkillReservedUntil(NSArray *rules, NSUInteger slot) {
    NSUInteger floor = 0;
    for (NSDictionary *rule in rules) if ([rule[@"enabled"] boolValue] && [rule[@"slot"] unsignedIntegerValue] == slot + 1)
        floor = MAX(floor, [rule[@"reserveUntil"] unsignedIntegerValue]);
    return floor;
}

NSArray<NSNumber *> *PHOrbSkillRuleOrder(NSArray *rules, const uint8_t next[PHOrbMaxSkillGroups]) {
    if (!PHOrbSkillRulesValid(rules) || !next) return @[];
    NSMutableArray *order = [NSMutableArray array];
    unsigned seen = 0;
    for (NSUInteger i = 0; i < rules.count; ++i) {
        NSUInteger group = [rules[i][@"group"] unsignedIntegerValue];
        if (!group) { [order addObject:@(i)]; continue; }
        if (seen & (1u << (group - 1))) continue;
        seen |= 1u << (group - 1);
        // The first row sets the group's priority. Within it, resume after
        // the last accepted member, wrap once, and allow unavailable fallback.
        NSUInteger start = next[group - 1] % rules.count;
        for (NSUInteger step = 0; step < rules.count; ++step) {
            NSUInteger index = (start + step) % rules.count;
            if ([rules[index][@"group"] unsignedIntegerValue] == group) [order addObject:@(index)];
        }
    }
    return order;
}

PHOrbRuleMatch PHOrbMatchSkillRule(NSDictionary *rule, const PHOrbSnapshot *state, NSUInteger turn) {
    if (!PHOrbSkillRulesValid(@[rule ?: @{}]) || !state || !turn) return PHOrbRuleUnknown;
    if (![rule[@"enabled"] boolValue] || (turn - 1) % [rule[@"every"] unsignedIntegerValue] ||
        ([rule[@"floor"] unsignedIntValue] && [rule[@"floor"] unsignedIntValue] != state->wave + 1) ||
        [rule[@"reserveUntil"] unsignedIntValue] > state->wave + 1) return PHOrbRuleNo;
    if (!state->automation.valid) return PHOrbRuleUnknown;
    const PHOrbSkillMember *member = &state->automation.members[[rule[@"slot"] unsignedIntegerValue] - 1];
    if (!member->active || member->bound || member->state ||
        ([rule[@"monster"] unsignedIntValue] && [rule[@"monster"] unsignedIntValue] != member->number)) return PHOrbRuleNo;
    NSInteger color = [rule[@"color"] integerValue];
    if (color >= 0) {
        if (!state->automation.boardKnown || !state->settled ||
            !PHOrbDimensionsSupported(state->board.width, state->board.height)) return PHOrbRuleUnknown;
        unsigned count = 0;
        for (unsigned i = 0; i < state->board.width * state->board.height; ++i) count += state->board.types[i] == color;
        if (count >= [rule[@"below"] unsignedIntValue]) return PHOrbRuleNo;
    }
    NSUInteger enemy = [PHOrbEnemyConditions() indexOfObject:rule[@"enemy"]];
    if (enemy) {
        if (!state->automation.enemiesKnown) return PHOrbRuleUnknown;
        unsigned flag = 1u << (enemy - 1);
        if (!(state->automation.enemyEffects & flag) || (state->automation.playerBypass & flag)) return PHOrbRuleNo;
    }
    NSString *obstacle = rule[@"obstacle"] ?: @"none";
    if (![obstacle isEqual:@"none"]) {
        if (!state->automation.boardKnown) return PHOrbRuleUnknown;
        unsigned flag = ObstacleFlag(obstacle);
        if (!(PHOrbBoardObstacles(state) & flag)) return PHOrbRuleNo;
    }
    return PHOrbRuleYes;
}

NSString *PHOrbSkillRuleDescription(NSDictionary *rule) {
    if (!PHOrbSkillRulesValid(@[rule ?: @{}])) return @"無效技能規則";
    NSMutableArray *parts = [NSMutableArray array];
    [parts addObject:[NSString stringWithFormat:@"位置 %@%@", rule[@"slot"], [rule[@"monster"] unsignedIntValue] ?
        [NSString stringWithFormat:@"（No.%@）", rule[@"monster"]] : @""]];
    [parts addObject:[rule[@"every"] unsignedIntValue] == 1 ? @"每回合" : [NSString stringWithFormat:@"每 %@ 回合", rule[@"every"]]];
    if ([rule[@"floor"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"第 %@ 層", rule[@"floor"]]];
    if ([rule[@"color"] integerValue] >= 0) [parts addObject:[NSString stringWithFormat:@"%@珠少於 %@ 顆",
        @[@"火", @"水", @"木", @"光", @"暗", @"心"][[rule[@"color"] unsignedIntegerValue]], rule[@"below"]]];
    if (![rule[@"enemy"] isEqual:@"none"]) [parts addObject:PHOrbEnemyConditionTitle(rule[@"enemy"])];
    if ([rule[@"group"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"輪替組 %@", rule[@"group"]]];
    if ([rule[@"once"] boolValue]) [parts addObject:@"本場一次"];
    if ([rule[@"reserveUntil"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"此位置保留至第 %@ 層", rule[@"reserveUntil"]]];
    if (rule[@"obstacle"] && ![rule[@"obstacle"] isEqual:@"none"]) [parts addObject:PHOrbObstacleConditionTitle(rule[@"obstacle"])];
    if (![rule[@"enabled"] boolValue]) [parts addObject:@"已停用"];
    return [parts componentsJoinedByString:@" · "];
}
