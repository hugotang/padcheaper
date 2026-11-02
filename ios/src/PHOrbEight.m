#import "PHOrbEight.h"
#import "PHOrbPlanning.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>

static BOOL EKeys(id value, NSArray *keys) {
    return [value isKindOfClass:NSDictionary.class] && [[NSSet setWithArray:[value allKeys]] isEqual:[NSSet setWithArray:keys]];
}
static BOOL EInt(id value, unsigned lo, unsigned hi) {
    if (![value isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID()) return NO;
    double n = [value doubleValue]; return isfinite(n) && n >= lo && n <= hi && trunc(n) == n;
}
static BOOL EVector(id values, unsigned lo, unsigned hi) {
    if (![values isKindOfClass:NSArray.class] || [values count] != 6) return NO;
    for (id value in values) if (!EInt(value,lo,hi)) return NO;
    return YES;
}
static BOOL EBool(id value) {
    return [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
}
static BOOL EOptionalKeys(id value, NSArray *required, NSArray *optional) {
    if (![value isKindOfClass:NSDictionary.class]) return NO;
    NSSet *keys = [NSSet setWithArray:[value allKeys]];
    NSMutableSet *allowed = [NSMutableSet setWithArray:required]; [allowed addObjectsFromArray:optional];
    return [[NSSet setWithArray:required] isSubsetOfSet:keys] && [keys isSubsetOfSet:allowed];
}
static BOOL ESequence(id sequence, NSDictionary *a) {
    if (![sequence isKindOfClass:NSArray.class] || ![sequence count] || [sequence count] > 6) return NO;
    unsigned mask = 0;
    for (id value in sequence) {
        if (!EInt(value,1,6)) return NO;
        unsigned slot = [value unsignedIntValue] - 1;
        if ((mask & (1u << slot)) || [a[@"members"][slot][@"effect"] isEqual:@"observe"]) return NO;
        mask |= 1u << slot;
    }
    return YES;
}
BOOL PHOrbEightActivityValid(id a) {
    if (!EOptionalKeys(a,@[@"schema",@"mode",@"width",@"height",@"source",@"members",@"leader",@"floors"],@[@"guide"]) ||
        !EInt(a[@"schema"],1,1) || !EInt(a[@"mode"],8,8) || !EInt(a[@"width"],5,7) || !EInt(a[@"height"],4,6) ||
        !PHOrbDimensionsSupported([a[@"width"] unsignedIntValue],[a[@"height"] unsignedIntValue]) ||
        ![a[@"source"] isKindOfClass:NSString.class] || [a[@"source"] length] > 2048 ||
        ![a[@"members"] isKindOfClass:NSArray.class] || [a[@"members"] count] != 6 ||
        ![a[@"floors"] isKindOfClass:NSArray.class] || ![a[@"floors"] count] || [a[@"floors"] count] > 100) return NO;
    for (id m in a[@"members"]) {
        if (!EKeys(m,@[@"number",@"effect",@"cooldown",@"convert",@"generate",@"boardColors",@"unlock"]) ||
            !EInt(m[@"number"],1,UINT16_MAX) || !EInt(m[@"cooldown"],0,999) ||
            ![@[@"convert",@"generate",@"board",@"observe"] containsObject:m[@"effect"]] ||
            !EVector(m[@"convert"],0,5) || !EVector(m[@"generate"],0,30) || !EInt(m[@"boardColors"],0,63) ||
            ![m[@"unlock"] isKindOfClass:NSNumber.class] || CFGetTypeID((__bridge CFTypeRef)m[@"unlock"]) != CFBooleanGetTypeID()) return NO;
        unsigned generated = 0; BOOL converted = NO;
        for (unsigned c = 0; c < 6; ++c) { generated += [m[@"generate"][c] unsignedIntValue]; converted |= [m[@"convert"][c] unsignedIntValue] != c; }
        if (generated > 30 || ([m[@"effect"] isEqual:@"convert"] && (!converted || generated || [m[@"boardColors"] unsignedIntValue])) ||
            ([m[@"effect"] isEqual:@"generate"] && (!generated || converted || [m[@"boardColors"] unsignedIntValue])) ||
            ([m[@"effect"] isEqual:@"board"] && (![m[@"boardColors"] unsignedIntValue] || converted || generated)) ||
            ([m[@"effect"] isEqual:@"observe"] && (converted || generated || [m[@"boardColors"] unsignedIntValue] || [m[@"unlock"] boolValue]))) return NO;
    }
    id leader = a[@"leader"];
    if (!EKeys(leader,@[@"colors",@"attackLength",@"attackPercent",@"comboLength",@"addedCombos",@"noSkyfall"]) ||
        !EInt(leader[@"colors"],1,31) || !EInt(leader[@"attackLength"],3,42) || !EInt(leader[@"attackPercent"],100,UINT16_MAX) ||
        !EInt(leader[@"comboLength"],3,42) || !EInt(leader[@"addedCombos"],0,20) ||
        ![leader[@"noSkyfall"] isKindOfClass:NSNumber.class] ||
        CFGetTypeID((__bridge CFTypeRef)leader[@"noSkyfall"]) != CFBooleanGetTypeID()) return NO;
    for (id f in a[@"floors"]) {
        PHOrbGoalSet goals = {0};
        if (!EOptionalKeys(f,@[@"goals",@"sequences",@"searchMS",@"weights"],@[@"comboPriority"]) ||
            (f[@"comboPriority"] && !EBool(f[@"comboPriority"])) || !PHOrbReadGoals(f[@"goals"],&goals) || !goals.count ||
            !EInt(f[@"searchMS"],50,1000) || ![f[@"sequences"] isKindOfClass:NSArray.class] || [f[@"sequences"] count] > 6 ||
            !EKeys(f[@"weights"],@[@"combo",@"row",@"link",@"step"])) return NO;
        for (id value in [f[@"weights"] allValues]) if (!EInt(value,0,10000)) return NO;
        for (id sequence in f[@"sequences"]) if (!ESequence(sequence,a)) return NO;
    }
    id guide = a[@"guide"];
    if (guide) {
        if (!EKeys(guide,@[@"enabled",@"floors"]) || !EBool(guide[@"enabled"]) ||
            ![guide[@"floors"] isKindOfClass:NSArray.class] || [guide[@"floors"] count] != [a[@"floors"] count]) return NO;
        for (id branches in guide[@"floors"]) {
            if (![branches isKindOfClass:NSArray.class] || [branches count] > 6) return NO;
            for (id branch in branches) if (!EKeys(branch,@[@"slots",@"requireInventory"]) ||
                !EBool(branch[@"requireInventory"]) || !ESequence(branch[@"slots"],a)) return NO;
        }
    }
    return YES;
}

BOOL PHOrbEightActivityMatches(NSDictionary *profile, const PHOrbSnapshot *s, NSString **reason) {
    if (reason) *reason = @"8 人活動的固定版本技能、隊伍或盤面不符，未啟用活動策略";
    NSDictionary *a = profile[@"activity"], *leader = a[@"leader"];
    if (!PHOrbEightActivityValid(a) || !s || !PHOrbEightMatchReady(s) || !s->eight.valid || !s->automation.valid ||
        s->board.width != [a[@"width"] unsignedIntValue] || s->board.height != [a[@"height"] unsignedIntValue] ||
        s->wave >= [a[@"floors"] count] ||
        ([profile[@"dungeon"] unsignedIntValue] && [profile[@"dungeon"] unsignedIntValue] != s->identity.dungeon) ||
        ([profile[@"floor"] unsignedIntValue] && [profile[@"floor"] unsignedIntValue] != s->identity.floor)) return NO;
    for (unsigned i = 0; i < 6; ++i) {
        NSDictionary *expected = a[@"members"][i]; const PHOrbEightMember *m = &s->eight.members[i];
        if (reason) *reason = [NSString stringWithFormat:@"8 人位置 %u 的固定版本或技能不符（No.%u、技能 %u、CD %u）；詳細資料保留在診斷。",
            i + 1,s->automation.members[i].number,m->activeSkill,m->cooldown];
        if (!s->automation.members[i].active || s->automation.members[i].number != [expected[@"number"] unsignedIntValue] ||
            m->sourceSlot != i || m->catalogID < 1000000u || m->cooldown != [expected[@"cooldown"] unsignedIntValue]) return NO;
        if ([expected[@"effect"] isEqual:@"observe"]) continue;
        if (!m->skillKnown || m->boardColors != [expected[@"boardColors"] unsignedIntValue] || m->unlock != [expected[@"unlock"] boolValue]) return NO;
        for (unsigned c = 0; c < 6; ++c) if (m->convert[c] != [expected[@"convert"][c] unsignedIntValue] ||
            m->generate[c] != [expected[@"generate"][c] unsignedIntValue]) return NO;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const unsigned slot = i ? 5 : 0; const PHOrbEightMember *m = &s->eight.members[slot];
        if (reason) *reason = [NSString stringWithFormat:@"8 人位置 %u 的隊長效果未確認或暫時無效（技能 %u）；未套用活動加算。",slot + 1,m->leaderSkill];
        if (!m->leaderKnown || s->automation.members[slot].bound || m->attackColors != [leader[@"colors"] unsignedIntValue] ||
            m->attackLength != [leader[@"attackLength"] unsignedIntValue] || m->attackPercent != [leader[@"attackPercent"] unsignedIntValue] ||
            m->comboColors != [leader[@"colors"] unsignedIntValue] || m->comboLength != [leader[@"comboLength"] unsignedIntValue] ||
            m->addedCombos != [leader[@"addedCombos"] unsignedIntValue] || m->noSkyfall != [leader[@"noSkyfall"] boolValue]) return NO;
    }
    if (reason) *reason = nil;
    return YES;
}

NSDictionary *PHOrbEightPlan(NSDictionary *a, const PHOrbSnapshot *s) {
    if (!s || !s->eight.valid || !s->enemyCombo.valid || s->wave >= [a[@"floors"] count])
        return @{@"available":@NO,@"message":@"8 人本層技能／吸收狀態未確認"};
    PHOrbGoalSet goals = {0}; unsigned required = 0, bonus = 0;
    if (!PHOrbReadGoals(a[@"floors"][s->wave][@"goals"],&goals) || !PHOrbEnemyComboRequirement(&s->enemyCombo,&required))
        return @{@"available":@NO,@"message":@"本層 Combo 吸收規則未支援，已停止"};
    for (unsigned side = 0; side < 2; ++side) {
        const unsigned slot = side ? 5 : 0; const PHOrbEightMember *m = &s->eight.members[slot];
        if (!m->leaderKnown || s->automation.members[slot].bound) continue;
        for (unsigned g = 0; g < goals.count; ++g) {
            const PHOrbGoal *goal = &goals.items[g];
            if (goal->kind == PHOrbGoalMatch && goal->length >= m->comboLength &&
                !(goal->colors & ~m->comboColors)) { bonus += m->addedCombos; break; }
        }
    }
    unsigned actual = required > bonus ? required - bonus : 1;
    unsigned combo = goals.count;
    for (unsigned g = 0; g < goals.count; ++g) if (goals.items[g].kind == PHOrbGoalCombo) { combo = g; break; }
    if (actual > 14 || (combo == PHOrbMaxGoals && actual > 1))
        return @{@"available":@NO,@"message":@"本層所需 Combo 超出盤面／目標上限"};
    if (combo < goals.count) goals.items[combo].count = MAX(goals.items[combo].count,actual);
    else if (actual > 1) goals.items[goals.count++] = (PHOrbGoal){PHOrbGoalCombo,0,(uint8_t)actual,0};
    goals.comboFirstWave = true;
    return @{@"available":@YES,@"goals":PHOrbGoalsJSON(&goals),@"enemyComboFirstWave":@YES,
        @"leaderAddedCombos":@(bonus),@"combatComboRequired":@(required),@"leaders":@[],
        @"message":[NSString stringWithFormat:@"B%u：%@",s->wave + 1,PHOrbGoalsDescription(PHOrbGoalsJSON(&goals))],
        @"warning":@"採首消核對；消法權重用於選路，未換算成實際總分或擊倒保證。"};
}

static BOOL EInventory(const PHOrbGoalSet *goals, const unsigned counts[6], unsigned width) {
    for (unsigned g = 0; g < goals->count; ++g) {
        const PHOrbGoal *goal = &goals->items[g]; unsigned available = 0;
        if (goal->kind == PHOrbGoalCombo) {
            for (unsigned c = 0; c < 6; ++c) available += counts[c] / 3;
        } else if (goal->kind == PHOrbGoalMatch || goal->kind == PHOrbGoalRow) {
            for (unsigned c = 0; c < 6; ++c) if (goal->colors & (1u << c)) available += counts[c] / (goal->kind == PHOrbGoalRow ? width : goal->length);
        } else continue;
        if (available < goal->count) return NO;
    }
    return YES;
}

BOOL PHOrbEightGuideEnabled(NSDictionary *a) { return [a[@"guide"][@"enabled"] boolValue]; }

NSDictionary *PHOrbEightGuideDecision(NSDictionary *a, const PHOrbSnapshot *s, uint8_t completed, uint8_t attempted) {
    if (!PHOrbEightGuideEnabled(a)) return nil;
    if (!s || !s->eight.valid || !s->automation.valid || !s->automation.boardKnown ||
        s->wave >= [a[@"guide"][@"floors"] count])
        return @{@"available":@NO,@"message":@"攻略開技需要本層固定技能與盤面資料"};
    NSArray *branches = a[@"guide"][@"floors"][s->wave];
    if (!branches.count) return @{@"available":@YES,@"sequence":@[],@"message":@"本層攻略不指定技能"};
    // A fulfilled alternative wins before considering another ready branch.
    // Changing profiles after a confirmed cast must not spend a second opener.
    for (NSDictionary *branch in branches) {
        unsigned mask = 0;
        for (NSNumber *value in branch[@"slots"]) mask |= 1u << (value.unsignedIntValue - 1);
        if ((completed & mask) == mask)
            return @{@"available":@YES,@"sequence":@[],@"message":@"本層攻略技能已確認完成"};
    }
    for (NSDictionary *branch in branches) {
        NSMutableArray *remaining = [NSMutableArray array]; BOOL ready = YES;
        for (NSNumber *value in branch[@"slots"]) {
            unsigned slot = value.unsignedIntValue - 1;
            if (completed & (1u << slot)) {
                // Only an already-completed prefix can be resumed. Reversing
                // Ney -> conversion would replace the converted board.
                if (remaining.count) { ready = NO; break; }
                continue;
            }
            const PHOrbEightMember *m = &s->eight.members[slot];
            const PHOrbSkillMember *member = &s->automation.members[slot];
            if ((attempted & (1u << slot)) || !m->skillKnown || member->bound || member->state || member->charge < m->cooldown) {
                ready = NO; break;
            }
            [remaining addObject:value];
        }
        if (!ready) continue;
        if (remaining.count && [branch[@"requireInventory"] boolValue]) {
            // Use the same bounded effect feasibility check as recovery. This
            // chooses the branch only; the resulting board is always reread.
            NSMutableDictionary *trial = [a mutableCopy];
            NSMutableArray *floors = [a[@"floors"] mutableCopy];
            NSMutableDictionary *floor = [floors[s->wave] mutableCopy]; floor[@"sequences"] = @[remaining];
            floors[s->wave] = floor; trial[@"floors"] = floors;
            NSUInteger cursor = 0;
            if (!PHOrbEightSkillSequence(trial,s,attempted,&cursor)) continue;
        }
        return @{@"available":@YES,@"sequence":[remaining copy],@"message":@"依本層攻略先開技，再讀盤求路"};
    }
    return @{@"available":@NO,@"message":[NSString stringWithFormat:
        @"B%u：攻略指定技能尚未就緒或無可用分支，未略過開技；可停止後關閉攻略開技選項。",s->wave + 1]};
}

NSArray *PHOrbEightSkillSequence(NSDictionary *a, const PHOrbSnapshot *s, uint8_t attempted, NSUInteger *cursor) {
    if (!s || !cursor || s->wave >= [a[@"floors"] count] || !s->automation.boardKnown || s->board.spinnerMask) return nil;
    NSDictionary *plan = PHOrbEightPlan(a,s); PHOrbGoalSet goals = {0};
    if (![plan[@"available"] boolValue] || !PHOrbReadGoals(plan[@"goals"],&goals)) return nil;
    NSArray *sequences = a[@"floors"][s->wave][@"sequences"];
    for (; *cursor < sequences.count; ++*cursor) {
        NSArray *sequence = sequences[*cursor]; unsigned counts[6] = {0}, locked[6] = {0}; BOOL available = YES;
        const unsigned cells = s->board.width * s->board.height;
        for (unsigned i = 0; i < cells; ++i) if (s->board.types[i] < 6) {
            if (s->board.flags[i] & PHOrbFlagLocked) ++locked[s->board.types[i]];
            else ++counts[s->board.types[i]];
        }
        for (NSNumber *value in sequence) {
            unsigned slot = value.unsignedIntValue - 1; const PHOrbEightMember *m = &s->eight.members[slot];
            const PHOrbSkillMember *member = &s->automation.members[slot];
            if ((attempted & (1u << slot)) || !m->skillKnown || member->bound || member->state || member->charge < m->cooldown) { available = NO; break; }
            if (m->unlock) for (unsigned c = 0; c < 6; ++c) { counts[c] += locked[c]; locked[c] = 0; }
            unsigned converted[6] = {0};
            for (unsigned c = 0; c < 6; ++c) converted[m->convert[c]] += counts[c];
            unsigned lockedTotal = 0; for (unsigned c = 0; c < 6; ++c) lockedTotal += locked[c];
            for (unsigned c = 0; c < 6; ++c) counts[c] = MIN(cells - lockedTotal,converted[c] + m->generate[c]);
            // Optimistic feasibility only. Random results are always reread;
            // no count here is accepted as evidence of an actual match.
            if (m->boardColors) for (unsigned c = 0; c < 6; ++c) counts[c] = (m->boardColors & (1u << c)) ? cells - lockedTotal : 0;
        }
        for (unsigned c = 0; c < 6; ++c) counts[c] += locked[c];
        if (available && EInventory(&goals,counts,s->board.width)) { ++*cursor; return sequence; }
    }
    return nil;
}
