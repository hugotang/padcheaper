#import "PHOrbPlanning.h"
#import <CoreFoundation/CoreFoundation.h>
#include "PHOrbLeaderGenerated.h"
#include <math.h>

NSArray<NSString *> *PHOrbGoalKinds(void) {
    return @[@"combo", @"colors", @"match", @"l", @"cross", @"t", @"square", @"row", @"column", @"four"];
}

NSString *PHOrbGoalTitle(NSString *kind) {
    NSUInteger index = [PHOrbGoalKinds() indexOfObject:kind ?: @""];
    return index == NSNotFound ? @"未知條件" : @[@"Combo", @"多色消除", @"連消", @"L 字", @"十字", @"T 字",
        @"3×3 正方形", @"整橫排", @"整直行", @"四連"][(NSUInteger)index];
}

NSString *PHOrbColorTitle(NSUInteger mask) {
    if (mask == 63) return @"任意珠色";
    if (mask == 31) return @"五色攻擊珠";
    NSMutableArray *names = [NSMutableArray array];
    for (NSUInteger i = 0; i < 6; ++i) if (mask & (1u << i)) [names addObject:@[@"火", @"水", @"木", @"光", @"暗", @"心"][i]];
    return [names componentsJoinedByString:@"／"];
}

static BOOL UnsignedNumber(id value, NSUInteger maximum, uint8_t *out) {
    if (![value isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID()) return NO;
    double number = [value doubleValue];
    if (!isfinite(number) || number < 0 || number > maximum || number != trunc(number)) return NO;
    *out = (uint8_t)number; return YES;
}

BOOL PHOrbReadGoals(id value, PHOrbGoalSet *goals) {
    if (goals) *goals = (PHOrbGoalSet){0};
    if (![value isKindOfClass:NSArray.class] || [value count] > PHOrbMaxGoals) return NO;
    PHOrbGoalSet result = {0};
    for (id item in value) {
        if (![item isKindOfClass:NSDictionary.class] || ![item[@"kind"] isKindOfClass:NSString.class]) return NO;
        for (id key in [item allKeys]) if (![@[@"kind", @"colors", @"count", @"length"] containsObject:key]) return NO;
        NSUInteger kind = [PHOrbGoalKinds() indexOfObject:item[@"kind"]];
        if (kind == NSNotFound) return NO;
        PHOrbGoal *g = &result.items[result.count++]; g->kind = (uint8_t)kind;
        if (!UnsignedNumber(item[@"colors"] ?: (kind == PHOrbGoalCombo ? @0 : @63), 63, &g->colors) ||
            !UnsignedNumber(item[@"count"] ?: @1, 14, &g->count) ||
            !UnsignedNumber(item[@"length"] ?: @0, PHOrbMaxCells, &g->length)) return NO;
    }
    if (!PHOrbGoalsValid(&result)) return NO;
    if (goals) *goals = result;
    return YES;
}

NSArray<NSDictionary *> *PHOrbGoalsJSON(const PHOrbGoalSet *goals) {
    if (!PHOrbGoalsValid(goals)) return @[];
    NSMutableArray *result = [NSMutableArray array];
    for (unsigned i = 0; i < goals->count; ++i) {
        PHOrbGoal goal = goals->items[i];
        [result addObject:@{@"kind":PHOrbGoalKinds()[goal.kind], @"colors":@(goal.colors), @"count":@(goal.count), @"length":@(goal.length)}];
    }
    return result;
}

NSString *PHOrbGoalDescription(NSDictionary *goal) {
    NSString *kind = goal[@"kind"], *color = PHOrbColorTitle([goal[@"colors"] unsignedIntegerValue]);
    NSUInteger count = [goal[@"count"] unsignedIntegerValue];
    if ([kind isEqual:@"combo"]) return [NSString stringWithFormat:@"至少 %lu Combo", (unsigned long)count];
    if ([kind isEqual:@"colors"]) return [NSString stringWithFormat:@"%@中消除 %lu 種顏色", color, (unsigned long)count];
    NSString *shape = [kind isEqual:@"match"] ? [NSString stringWithFormat:@"至少 %@ 顆連消", goal[@"length"]] : PHOrbGoalTitle(kind);
    return [NSString stringWithFormat:@"%@ %@ × %lu 組", color, shape, (unsigned long)count];
}

NSString *PHOrbGoalsDescription(NSArray *goals) {
    if (!goals.count) return @"優先提高 Combo";
    NSMutableArray *lines = [NSMutableArray array];
    for (NSDictionary *goal in goals) [lines addObject:PHOrbGoalDescription(goal)];
    return [lines componentsJoinedByString:@"\n"];
}

#include "PHOrbRecovery.inc"

NSDictionary *PHOrbPlanForLeaderCatalog(const PHOrbSnapshot *state, NSDictionary *catalog) {
    NSMutableArray *goals = [NSMutableArray array], *leaders = [NSMutableArray array];
    NSMutableOrderedSet *warnings = [NSMutableOrderedSet orderedSet], *unsupported = [NSMutableOrderedSet orderedSet];
    if (!state || !state->present || !state->valid || !state->leadersValid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer)) {
        return @{@"available":@NO, @"goals":@[], @"leaders":@[], @"warning":@"",
            @"message":@"未能確認目前隊長與助人；請重新讀盤，或改用指定形狀。"};
    }
    NSDictionary *cards = [catalog[@"cards"] isKindOfClass:NSDictionary.class] ? catalog[@"cards"] : @{};
    NSDictionary *skills = [catalog[@"skills"] isKindOfClass:NSDictionary.class] ? catalog[@"skills"] : @{};
    for (unsigned slot = 0; slot < 2; ++slot) {
        NSString *number = [NSString stringWithFormat:@"%u", state->leaderNumbers[slot]];
        NSDictionary *card = [cards[number] isKindOfClass:NSDictionary.class] ? cards[number] : @{};
        NSDictionary *profile = skills[[card[@"skill"] description] ?: @""];
        NSString *title = [NSString stringWithFormat:@"%@ #%@ · %@", slot ? @"助人" : @"隊長", number,
            [card[@"name"] isKindOfClass:NSString.class] ? card[@"name"] : @"未收錄"];
        [leaders addObject:title];
        PHOrbGoalSet parsed = {0};
        if (![profile isKindOfClass:NSDictionary.class] || !PHOrbReadGoals(profile[@"goals"], &parsed) ||
            ![profile[@"warnings"] isKindOfClass:NSArray.class] || ![profile[@"unsupported"] isKindOfClass:NSArray.class]) {
            [unsupported addObject:[NSString stringWithFormat:@"#%@ 的隊長技資料未收錄或無效", number]]; continue;
        }
        for (id warning in profile[@"warnings"]) {
            if ([warning isKindOfClass:NSString.class]) [warnings addObject:warning];
            else [unsupported addObject:@"隊長技限制資料無效"];
        }
        for (id item in profile[@"unsupported"]) {
            if ([item isKindOfClass:NSString.class]) [unsupported addObject:item];
            else [unsupported addObject:@"隊長技限制資料無效"];
        }
        for (NSDictionary *goal in PHOrbGoalsJSON(&parsed)) {
            NSUInteger same = [goals indexOfObjectPassingTest:^BOOL(NSDictionary *other, NSUInteger index, BOOL *stop) {
                (void)index; (void)stop;
                return [other[@"kind"] isEqual:goal[@"kind"]] && [other[@"colors"] isEqual:goal[@"colors"]] &&
                    [other[@"length"] isEqual:goal[@"length"]];
            }];
            if (same == NSNotFound) [goals addObject:goal];
            else if ([goal[@"count"] unsignedIntegerValue] > [goals[same][@"count"] unsignedIntegerValue]) goals[same] = goal;
        }
    }
    if (goals.count > PHOrbMaxGoals) [unsupported addObject:@"雙隊長的盤面條件超過 6 項"];
    NSString *warning = [[@[@"只規劃最低盤面門檻；不計傷害、天降、追加 Combo 或重複發動倍率。"]
        arrayByAddingObjectsFromArray:warnings.array] componentsJoinedByString:@"\n"];
    // Keep unknown rules visible and require a deliberate manual profile.
    BOOL available = unsupported.count == 0;
    NSString *message = available ? PHOrbGoalsDescription(goals) : @"這組隊長包含尚未支援或缺少資料的條件，請改用指定形狀。";
    return @{@"available":@(available), @"goals":available ? goals : @[], @"leaders":leaders,
        @"message":message, @"warning":warning, @"unsupported":unsupported.array};
}

NSDictionary *PHOrbPlanForLeaders(const PHOrbSnapshot *state) {
    static NSDictionary *catalog;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSData *data = [NSData dataWithBytes:PHOrbLeaderData length:sizeof(PHOrbLeaderData)];
        id decoded = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        catalog = [decoded isKindOfClass:NSDictionary.class] && [decoded[@"schema"] isEqual:@1] &&
            [decoded[@"region"] isEqual:@"ja"] && [decoded[@"numbering"] isEqual:@"encyclopedia"] ? decoded : @{};
    });
    return PHOrbPlanForLeaderCatalog(state, catalog);
}
