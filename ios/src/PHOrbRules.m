#import "PHOrbRules.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>

NSDictionary *PHOrbDefaultSkillRule(void) {
    return @{@"enabled":@YES, @"slot":@1, @"monster":@0, @"every":@1, @"floor":@0,
             @"color":@-1, @"below":@0, @"enemy":@"none"};
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
    for (id rule in rules) {
        if (![rule isKindOfClass:NSDictionary.class] || ![[NSSet setWithArray:[rule allKeys]] isEqual:keys] ||
            !Boolean(rule[@"enabled"]) || !Integer(rule[@"slot"], 1, 6) || !Integer(rule[@"monster"], 0, UINT16_MAX) ||
            !Integer(rule[@"every"], 1, 999) || !Integer(rule[@"floor"], 0, 999) || !Integer(rule[@"color"], -1, 5) ||
            !Integer(rule[@"below"], 0, PHOrbMaxCells + 1) || ![rule[@"enemy"] isKindOfClass:NSString.class] ||
            ![PHOrbEnemyConditions() containsObject:rule[@"enemy"]]) return NO;
        uint16_t catalogID = 0;
        if ([rule[@"monster"] unsignedIntValue] && !PHBattleCatalogIDForNumber([rule[@"monster"] unsignedIntValue], &catalogID)) return NO;
        if ([rule[@"color"] integerValue] < 0 ? [rule[@"below"] integerValue] != 0 : [rule[@"below"] integerValue] < 1) return NO;
    }
    return YES;
}

PHOrbRuleMatch PHOrbMatchSkillRule(NSDictionary *rule, const PHOrbSnapshot *state, NSUInteger turn) {
    if (!PHOrbSkillRulesValid(@[rule ?: @{}]) || !state || !turn) return PHOrbRuleUnknown;
    if (![rule[@"enabled"] boolValue] || (turn - 1) % [rule[@"every"] unsignedIntegerValue] ||
        ([rule[@"floor"] unsignedIntValue] && [rule[@"floor"] unsignedIntValue] != state->wave + 1)) return PHOrbRuleNo;
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
    if (![rule[@"enabled"] boolValue]) [parts addObject:@"已停用"];
    return [parts componentsJoinedByString:@" · "];
}
