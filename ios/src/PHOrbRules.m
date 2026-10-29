#import "PHOrbRules.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>

NSDictionary *PHOrbDefaultSkillRule(void) {
    return @{@"enabled":@YES, @"slot":@1, @"monster":@0, @"every":@1, @"floor":@0,
             @"color":@-1, @"below":@0, @"enemy":@"none", @"group":@0, @"once":@NO,
             @"reserveUntil":@0, @"obstacle":@"none", @"unmatchableColors":@0,
             @"hpAtMost":@0, @"status":@"none", @"required":@NO,
             @"source":@"auto", @"skill":@0, @"atLeast":@0};
}

PHOrbSkillRequest PHOrbRequestForRule(NSDictionary *rule) {
    PHOrbSkillRequest request = {PHOrbSkillAutomatic, [rule[@"skill"] unsignedIntValue]};
    if ([rule[@"source"] isEqual:@"body"]) request.source = PHOrbSkillBody;
    if ([rule[@"source"] isEqual:@"assist"]) request.source = PHOrbSkillAssist;
    return request;
}

NSDictionary *PHOrbNormalizedSkillRule(NSDictionary *rule) {
    NSMutableDictionary *result = [PHOrbDefaultSkillRule() mutableCopy];
    if ([rule isKindOfClass:NSDictionary.class]) [result addEntriesFromDictionary:rule];
    return [result copy];
}

NSArray<NSString *> *PHOrbObstacleConditions(void) {
    return @[@"none", @"unmatchable", @"special_orbs", @"orb_effect", @"board_effect", @"spinner", @"locked", @"weakened",
        @"jammer", @"poison", @"mortal_poison", @"bomb", @"cloud", @"tape", @"blind", @"super_blind",
        @"nail", @"thorn", @"fixed_start", @"combo_orb", @"match_rule"];
}

NSString *PHOrbObstacleConditionTitle(NSString *condition) {
    return @{@"none":@"不限定盤面妨礙", @"unmatchable":@"有不可消效果",
        @"special_orbs":@"有基本六色以外的珠", @"orb_effect":@"有未支援的珠子效果",
        @"board_effect":@"有特殊盤面效果", @"spinner":@"有轉轉珠", @"locked":@"有鎖珠", @"weakened":@"有弱化珠",
        @"jammer":@"有邪魔珠", @"poison":@"有毒珠", @"mortal_poison":@"有猛毒珠", @"bomb":@"有爆彈珠",
        @"cloud":@"有雲", @"tape":@"有封條", @"blind":@"有普通暗闇", @"super_blind":@"有超暗闇",
        @"nail":@"有釘珠", @"thorn":@"有刺珠", @"fixed_start":@"有固定起點", @"combo_orb":@"有 Combo 珠",
        @"match_rule":@"有最少消珠限制"}[condition] ?: @"未知盤面條件";
}

static unsigned ObstacleFlag(NSString *condition) {
    return [@{@"unmatchable":@(PHOrbObstacleUnmatchable), @"special_orbs":@(PHOrbObstacleSpecialOrbs),
        @"orb_effect":@(PHOrbObstacleOrbEffect), @"board_effect":@(PHOrbObstacleBoardEffect),
        @"spinner":@(PHOrbObstacleSpinner), @"locked":@(PHOrbObstacleLocked), @"weakened":@(PHOrbObstacleWeakened),
        @"jammer":@(PHOrbObstacleJammer), @"poison":@(PHOrbObstaclePoison), @"mortal_poison":@(PHOrbObstacleMortalPoison),
        @"bomb":@(PHOrbObstacleBomb), @"cloud":@(PHOrbObstacleCloud), @"tape":@(PHOrbObstacleTape),
        @"blind":@(PHOrbObstacleBlind), @"super_blind":@(PHOrbObstacleSuperBlind), @"nail":@(PHOrbObstacleNail),
        @"thorn":@(PHOrbObstacleThorn), @"fixed_start":@(PHOrbObstacleFixedStart), @"combo_orb":@(PHOrbObstacleCombo),
        @"match_rule":@(PHOrbObstacleMatchRule)}[condition] unsignedIntValue];
}

NSString *PHOrbUnmatchableConditionTitle(unsigned colors) {
    if (!colors) return @"任意珠色不可消";
    NSMutableArray *names = [NSMutableArray array];
    for (unsigned color = 0; color < 6; ++color)
        if (colors & (1u << color)) [names addObject:@[@"火", @"水", @"木", @"光", @"暗", @"心"][color]];
    return [NSString stringWithFormat:@"%@不可消%@", [names componentsJoinedByString:@"／"],
        names.count > 1 ? @"（任一符合）" : @""];
}

NSString *PHOrbSpinnerReadDescription(const PHOrbSnapshot *state) {
    if (!state || !state->spinnerTurns) return @"";
    switch (state->spinnerReadStatus) {
        case PHOrbSpinnerReady: return state->board.spinnerMask ? @"轉轉資料已確認" : @"轉轉位置均在目前盤面外";
        case PHOrbSpinnerBadCount: return @"轉轉數量無法確認";
        case PHOrbSpinnerBadPeriod: return @"轉轉週期無法確認";
        case PHOrbSpinnerBadColors: return @"轉轉色序未支援或無法確認";
        case PHOrbSpinnerBadRows: return @"轉轉位置無法確認";
        case PHOrbSpinnerCountMismatch: return @"轉轉數量與保存位置不符";
        case PHOrbSpinnerBadPhase: return @"等待轉轉進入可驗證的色序與珠子狀態";
        default: return @"轉轉尚未完成讀取校驗";
    }
}

NSString *PHOrbObstacleDescription(const PHOrbSnapshot *state) {
    if (!state || !state->valid || state->phase != 2) return @"";
    unsigned flags = PHOrbBoardObstacles(state);
    BOOL unknownType = NO;
    if (PHOrbDimensionsSupported(state->board.width, state->board.height))
        for (unsigned i = 0; i < state->board.width * state->board.height; ++i) unknownType |= state->board.types[i] >= PHOrbTypes;
    NSMutableArray *parts = [NSMutableArray array];
    for (NSUInteger i = 1; i < PHOrbObstacleConditions().count; ++i) {
        unsigned flag = ObstacleFlag(PHOrbObstacleConditions()[i]);
        if (flag == PHOrbObstacleSpecialOrbs && !unknownType && (flags & (PHOrbObstacleJammer | PHOrbObstaclePoison |
            PHOrbObstacleMortalPoison | PHOrbObstacleBomb))) continue;
        if (flags & flag) [parts addObject:PHOrbObstacleConditionTitle(PHOrbObstacleConditions()[i])];
    }
    if (state->spinnerTurns && state->spinnerReadStatus != PHOrbSpinnerReady)
        [parts addObject:PHOrbSpinnerReadDescription(state)];
    if (!state->ordinary) [parts addObject:@"盤面資料、操作規則或動畫尚未就緒"];
    return [parts componentsJoinedByString:@"、"];
}

NSArray<NSString *> *PHOrbEnemyConditions(void) {
    return @[@"none", @"damage_absorb", @"attribute_absorb", @"damage_void", @"combo_absorb"];
}

NSString *PHOrbEnemyConditionTitle(NSString *condition) {
    return @{@"none":@"不限定敵人效果", @"damage_absorb":@"傷害吸收", @"attribute_absorb":@"屬性吸收",
             @"damage_void":@"傷害無效", @"combo_absorb":@"Combo 吸收"}[condition] ?: @"未知敵人條件";
}

NSArray<NSString *> *PHOrbPlayerConditions(void) {
    return @[@"none", @"awoken_bind", @"member_bind", @"skill_seal"];
}

NSString *PHOrbPlayerConditionTitle(NSString *condition) {
    return @{@"none":@"不限定我方異常", @"awoken_bind":@"有暫時覺醒無效",
        @"member_bind":@"有隊員被綁定", @"skill_seal":@"有技能封印"}[condition] ?: @"未知我方條件";
}

static unsigned PlayerFlag(NSString *condition) {
    return [@{@"awoken_bind":@(PHOrbPlayerAwokenBind), @"member_bind":@(PHOrbPlayerMemberBind),
        @"skill_seal":@(PHOrbPlayerSkillSeal)}[condition] unsignedIntValue];
}

NSString *PHOrbSurvivalDescription(const PHOrbSnapshot *state) {
    if (!state || !state->valid || !state->automation.valid) return @"";
    const PHOrbAutomationState *player = &state->automation;
    NSMutableArray *parts = [NSMutableArray array];
    if (player->hpKnown && player->maximumHP) [parts addObject:[NSString stringWithFormat:@"HP %u／%u（%.1f%%）",
        player->hp, player->maximumHP, 100.0 * player->hp / player->maximumHP]];
    else [parts addObject:@"有效 HP 上限尚未確認"];
    if (!player->statusKnown) [parts addObject:@"我方異常資料尚未確認"];
    else {
        for (NSString *condition in PHOrbPlayerConditions())
            if (player->playerEffects & PlayerFlag(condition)) [parts addObject:PHOrbPlayerConditionTitle(condition)];
        if (player->awakeningRestricted) [parts addObject:@"關卡禁止覺醒（不屬於技能可解除的暫時效果）"];
        if (!player->playerEffects && !player->awakeningRestricted) [parts addObject:@"未見已支援的我方異常"];
    }
    return [parts componentsJoinedByString:@" · "];
}

static BOOL PHOrbRuleIsBoolean(id value) {
    return [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
}
static BOOL Integer(id value, NSInteger minimum, NSInteger maximum) {
    if (![value isKindOfClass:NSNumber.class] || PHOrbRuleIsBoolean(value)) return NO;
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
            !PHOrbRuleIsBoolean(rule[@"enabled"]) || !Integer(rule[@"slot"], 1, 6) || !Integer(rule[@"monster"], 0, UINT16_MAX) ||
            !Integer(rule[@"every"], 1, 999) || !Integer(rule[@"floor"], 0, 999) || !Integer(rule[@"color"], -1, 5) ||
            !Integer(rule[@"below"], 0, PHOrbMaxCells + 1) || ![rule[@"enemy"] isKindOfClass:NSString.class] ||
            !Integer(rule[@"atLeast"], 0, PHOrbMaxCells) || !Integer(rule[@"skill"], 0, 0x1ffff) ||
            ![rule[@"source"] isKindOfClass:NSString.class] || ![@[@"auto", @"body", @"assist"] containsObject:rule[@"source"]] ||
            ![PHOrbEnemyConditions() containsObject:rule[@"enemy"]] || !Integer(rule[@"group"], 0, PHOrbMaxSkillGroups) ||
            !PHOrbRuleIsBoolean(rule[@"once"]) || !Integer(rule[@"reserveUntil"], 0, 999) ||
            ![rule[@"obstacle"] isKindOfClass:NSString.class] || ![PHOrbObstacleConditions() containsObject:rule[@"obstacle"]] ||
            !Integer(rule[@"unmatchableColors"], 0, 63) ||
            ([rule[@"unmatchableColors"] unsignedIntValue] && ![rule[@"obstacle"] isEqual:@"unmatchable"]) ||
            !Integer(rule[@"hpAtMost"], 0, 100) || !PHOrbRuleIsBoolean(rule[@"required"]) ||
            ![rule[@"status"] isKindOfClass:NSString.class] || ![PHOrbPlayerConditions() containsObject:rule[@"status"]] ||
            ([rule[@"floor"] unsignedIntValue] && [rule[@"floor"] unsignedIntValue] < [rule[@"reserveUntil"] unsignedIntValue])) return NO;
        uint16_t catalogID = 0;
        if ([rule[@"monster"] unsignedIntValue] && !PHBattleCatalogIDForNumber([rule[@"monster"] unsignedIntValue], &catalogID)) return NO;
        if ([rule[@"color"] integerValue] < 0 ? ([rule[@"below"] unsignedIntValue] || [rule[@"atLeast"] unsignedIntValue]) :
            (([rule[@"below"] unsignedIntValue] > 0) == ([rule[@"atLeast"] unsignedIntValue] > 0))) return NO;
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

static PHOrbRuleMatch Explained(PHOrbRuleMatch match, NSString *message, NSString **reason) {
    if (reason) *reason = message;
    return match;
}

PHOrbRuleMatch PHOrbTriggerSkillRule(NSDictionary *rule, const PHOrbSnapshot *state, NSUInteger turn, NSString **reason) {
    if (!PHOrbSkillRulesValid(@[rule ?: @{}]) || !state || !turn)
        return Explained(PHOrbRuleUnknown, @"規則或回合資料無效", reason);
    rule = PHOrbNormalizedSkillRule(rule);
    if (![rule[@"enabled"] boolValue]) return Explained(PHOrbRuleNo, @"已停用", reason);
    if ((turn - 1) % [rule[@"every"] unsignedIntegerValue])
        return Explained(PHOrbRuleNo, @"目前回合不符合間隔", reason);
    if ([rule[@"floor"] unsignedIntValue] && [rule[@"floor"] unsignedIntValue] != state->wave + 1)
        return Explained(PHOrbRuleNo, [NSString stringWithFormat:@"目前第 %u 層，不是指定的第 %@ 層", state->wave + 1, rule[@"floor"]], reason);
    if (!state->automation.valid) return Explained(PHOrbRuleUnknown, @"技能與隊伍資料無法確認", reason);
    const PHOrbSkillMember *member = &state->automation.members[[rule[@"slot"] unsignedIntegerValue] - 1];
    if ([rule[@"monster"] unsignedIntValue] && [rule[@"monster"] unsignedIntValue] != member->number)
        return Explained(PHOrbRuleNo, @"目前角色形態不符合指定編號", reason);
    NSMutableArray *matched = [NSMutableArray array];
    NSUInteger hpLimit = [rule[@"hpAtMost"] unsignedIntegerValue];
    if (hpLimit) {
        const PHOrbAutomationState *player = &state->automation;
        if (!player->hpKnown || !player->maximumHP || player->hp > player->maximumHP)
            return Explained(PHOrbRuleUnknown, @"目前 HP 或有效上限無法確認", reason);
        if ((uint64_t)player->hp * 100 > (uint64_t)player->maximumHP * hpLimit)
            return Explained(PHOrbRuleNo, [NSString stringWithFormat:@"HP %.1f%% 高於 %@%% 門檻",
                100.0 * player->hp / player->maximumHP, rule[@"hpAtMost"]], reason);
        [matched addObject:[NSString stringWithFormat:@"HP %.1f%% ≤ %@%%",
            100.0 * player->hp / player->maximumHP, rule[@"hpAtMost"]]];
    }
    NSString *status = rule[@"status"];
    if (![status isEqual:@"none"]) {
        if (!state->automation.statusKnown) return Explained(PHOrbRuleUnknown, @"我方異常狀態無法確認", reason);
        if (!(state->automation.playerEffects & PlayerFlag(status)))
            return Explained(PHOrbRuleNo, [status isEqual:@"awoken_bind"] && state->automation.awakeningRestricted ?
                @"關卡禁止覺醒，但沒有暫時覺醒無效" : @"未出現指定的我方異常", reason);
        [matched addObject:PHOrbPlayerConditionTitle(status)];
    }
    NSInteger color = [rule[@"color"] integerValue];
    if (color >= 0) {
        if (!state->automation.boardKnown || !state->settled ||
            !PHOrbDimensionsSupported(state->board.width, state->board.height))
            return Explained(PHOrbRuleUnknown, @"盤面珠數無法確認", reason);
        unsigned minimum = 0, maximum = 0;
        PHOrbColorCountRange(&state->board, (unsigned)color, &minimum, &maximum);
        if ([rule[@"atLeast"] unsignedIntValue]) {
            if (minimum < [rule[@"atLeast"] unsignedIntValue])
                return Explained(PHOrbRuleNo, [NSString stringWithFormat:@"指定珠色保證 %u 顆，未達至少 %@ 顆", minimum, rule[@"atLeast"]], reason);
            [matched addObject:[NSString stringWithFormat:@"指定珠色至少 %u 顆 ≥ %@ 顆", minimum, rule[@"atLeast"]]];
        } else {
            if (minimum >= [rule[@"below"] unsignedIntValue]) return Explained(PHOrbRuleNo, @"指定珠色數量已足夠", reason);
            if (maximum >= [rule[@"below"] unsignedIntValue])
                return Explained(PHOrbRuleUnknown, @"缺珠門檻會隨轉轉變色，請先手動處理或調整規則", reason);
            [matched addObject:[NSString stringWithFormat:@"指定珠色最多 %u 顆 < %@ 顆", maximum, rule[@"below"]]];
        }
    }
    NSUInteger enemy = [PHOrbEnemyConditions() indexOfObject:rule[@"enemy"]];
    if (enemy) {
        if (!state->automation.enemiesKnown) return Explained(PHOrbRuleUnknown, @"敵人效果無法確認", reason);
        unsigned flag = 1u << (enemy - 1);
        if (!(state->automation.enemyEffects & flag)) return Explained(PHOrbRuleNo, @"敵人沒有指定效果", reason);
        if (state->automation.playerBypass & flag) return Explained(PHOrbRuleNo, @"我方已有對應的全隊無效化效果", reason);
        [matched addObject:PHOrbEnemyConditionTitle(rule[@"enemy"])];
    }
    NSString *obstacle = rule[@"obstacle"] ?: @"none";
    if (![obstacle isEqual:@"none"]) {
        if (!state->automation.boardKnown) return Explained(PHOrbRuleUnknown, @"盤面妨礙無法確認", reason);
        unsigned flag = ObstacleFlag(obstacle);
        const unsigned colors = [rule[@"unmatchableColors"] unsignedIntValue];
        if ([obstacle isEqual:@"unmatchable"] && colors) {
            if (!state->unmatchableKnown) return Explained(PHOrbRuleUnknown, @"不可消珠色無法確認", reason);
            if (!(state->board.unmatchableColors & colors))
                return Explained(PHOrbRuleNo, @"所選珠色沒有不可消效果", reason);
        }
        if (!(PHOrbBoardObstacles(state) & flag)) return Explained(PHOrbRuleNo, @"沒有指定的盤面妨礙", reason);
        [matched addObject:[obstacle isEqual:@"unmatchable"] ? PHOrbUnmatchableConditionTitle(colors) : PHOrbObstacleConditionTitle(obstacle)];
    }
    return Explained(PHOrbRuleYes, matched.count ? [matched componentsJoinedByString:@"、"] : @"角色、回合與樓層條件符合", reason);
}

PHOrbRuleMatch PHOrbMatchSkillRule(NSDictionary *rule, const PHOrbSnapshot *state, NSUInteger turn) {
    if (!PHOrbSkillRulesValid(@[rule ?: @{}]) || !state || !turn) return PHOrbRuleUnknown;
    // Keep the legacy availability-first behavior. Required rules use the
    // condition-only entry point so an unavailable caster cannot hide them.
    if ([rule[@"reserveUntil"] unsignedIntValue] > state->wave + 1) return PHOrbRuleNo;
    if (state->automation.valid) {
        const PHOrbSkillMember *member = &state->automation.members[[rule[@"slot"] unsignedIntegerValue] - 1];
        if (!member->active || member->bound || member->state) return PHOrbRuleNo;
    }
    return PHOrbTriggerSkillRule(rule, state, turn, NULL);
}

NSString *PHOrbSkillRuleDescription(NSDictionary *rule) {
    if (!PHOrbSkillRulesValid(@[rule ?: @{}])) return @"無效技能規則";
    rule = PHOrbNormalizedSkillRule(rule);
    NSMutableArray *parts = [NSMutableArray array];
    [parts addObject:[NSString stringWithFormat:@"位置 %@%@", rule[@"slot"], [rule[@"monster"] unsignedIntValue] ?
        [NSString stringWithFormat:@"（No.%@）", rule[@"monster"]] : @""]];
    [parts addObject:[rule[@"every"] unsignedIntValue] == 1 ? @"每回合" : [NSString stringWithFormat:@"每 %@ 回合", rule[@"every"]]];
    if ([rule[@"floor"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"第 %@ 層", rule[@"floor"]]];
    if (![rule[@"source"] isEqual:@"auto"]) [parts addObject:[rule[@"source"] isEqual:@"body"] ? @"僅本體技能" : @"僅武裝技能"];
    if ([rule[@"skill"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"技能 %@", rule[@"skill"]]];
    if ([rule[@"color"] integerValue] >= 0) [parts addObject:[NSString stringWithFormat:@"%@珠%@ %@ 顆",
        @[@"火", @"水", @"木", @"光", @"暗", @"心"][[rule[@"color"] unsignedIntegerValue]],
        [rule[@"atLeast"] unsignedIntValue] ? @"至少" : @"少於", [rule[@"atLeast"] unsignedIntValue] ? rule[@"atLeast"] : rule[@"below"]]];
    if (![rule[@"enemy"] isEqual:@"none"]) [parts addObject:PHOrbEnemyConditionTitle(rule[@"enemy"])];
    if ([rule[@"group"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"輪替組 %@", rule[@"group"]]];
    if ([rule[@"once"] boolValue]) [parts addObject:@"本場一次"];
    if ([rule[@"reserveUntil"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"此位置保留至第 %@ 層", rule[@"reserveUntil"]]];
    if (rule[@"obstacle"] && ![rule[@"obstacle"] isEqual:@"none"])
        [parts addObject:[rule[@"obstacle"] isEqual:@"unmatchable"] ?
            PHOrbUnmatchableConditionTitle([rule[@"unmatchableColors"] unsignedIntValue]) : PHOrbObstacleConditionTitle(rule[@"obstacle"])];
    if ([rule[@"hpAtMost"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"HP ≤ %@%%", rule[@"hpAtMost"]]];
    if (rule[@"status"] && ![rule[@"status"] isEqual:@"none"]) [parts addObject:PHOrbPlayerConditionTitle(rule[@"status"])];
    if ([rule[@"required"] boolValue]) [parts addObject:@"必要：未確認施放就停止轉珠"];
    if (![rule[@"enabled"] boolValue]) [parts addObject:@"已停用"];
    return [parts componentsJoinedByString:@" · "];
}
