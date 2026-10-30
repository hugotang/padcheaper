#import "PHDeckPersistence.h"
#import "PHMemory.h"
#import <CoreFoundation/CoreFoundation.h>
#include "PHBadgeModel.h"
#include "PHTargetGenerated.h"
#include <string.h>
#include <math.h>

static BOOL UnsignedNumber(id number, uint32_t expected) {
    return [number isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)number) != CFBooleanGetTypeID() &&
        isfinite([number doubleValue]) && [number doubleValue] == (double)expected;
}

NSDictionary *PHDeckAccountBanks(id store, uint32_t player) {
    if (!player || ![store isKindOfClass:NSDictionary.class] || !UnsignedNumber(store[@"schema"], 1) ||
        !UnsignedNumber(store[@"player"], player) ||
        ![store[@"target"] isEqual:@"jp.gungho.pad-ios-23.6.0-106878"] ||
        ![store[@"banks"] isKindOfClass:NSDictionary.class]) return @{};
    NSDictionary *banks = store[@"banks"];
    if (![banks[@"decksb"] isKindOfClass:NSArray.class]) return @{};
    for (NSString *key in banks) {
        if (![@[@"decksb", @"decksbs"] containsObject:key]) return @{};
        id records = banks[key];
        if (![records isKindOfClass:NSArray.class] || [records count] > ([key isEqual:@"decksb"] ? 32u : 16u)) return @{};
        for (id record in records) if (!PHValidateDeckRecord(record)) return @{};
    }
    return [banks[@"decksb"] count] + [banks[@"decksbs"] count] <= 32 ? banks : @{};
}

static NSArray *ReadDeck(uintptr_t address, NSUInteger index) {
    uint8_t bytes[96];
    if (!PHReadMemory(address, bytes, sizeof(bytes))) return nil;
    uint32_t cards[5], extra; uint16_t bgm[2], skin;
    memcpy(cards, bytes, sizeof(cards)); memcpy(&extra, bytes + 20, 4);
    memcpy(bgm, bytes + 24, 4); memcpy(&skin, bytes + 28, 2);
    const uint64_t packed = bgm[0] + 100000ull * bgm[1];
    if (packed > UINT32_MAX) return nil;
    return @[@(cards[0]), @(cards[1]), @(cards[2]), @(cards[3]), @(cards[4]),
        @(PHBadgeEncode(bytes[95], index)), @(extra), @(packed), @(skin)];
}

static BOOL Counts(uintptr_t header, uint8_t *primary, uint8_t *secondary, uint16_t *maximum) {
    if (!header || header > UINTPTR_MAX - 0x3000000u) return NO;
    const uintptr_t save = header + PHSaveRVA;
    return header && PHReadU8(save + PHPrimaryCountOffset, primary) &&
        PHReadU8(save + PHSecondaryCountOffset, secondary) && PHReadU16(save + PHMaxDecksOffset, maximum) &&
        *primary > 0 && *primary <= 32 && *secondary <= 16 && *maximum > 0 && *maximum <= 32 &&
        *primary <= *maximum && (unsigned)*primary + *secondary <= 32;
}

static BOOL Observe(uintptr_t header, PHDeckRequest *out) {
    *out = (PHDeckRequest){0}; uint32_t identifier = 0; uint8_t flags = 0;
    if (!Counts(header, &out->primary, &out->secondary, &out->maximum) ||
        !PHReadPointer(header + PHSaveRVA + PHAPIContextPointerOffset, &out->context) || !out->context ||
        out->context > UINTPTR_MAX - PHAPIActiveRequestOffset - 8 ||
        !PHReadPointer(out->context + PHAPIActiveRequestOffset, &out->request) || !out->request ||
        out->request > UINTPTR_MAX - 48 ||
        !PHReadU32(out->request + 8, &identifier) || identifier != PHSaveDecksRequestID ||
        !PHReadU8(out->request + 32, &flags) || (flags & 3u) != 1u ||
        !PHReadPointer(out->request + 40, &out->records) || !out->records ||
        out->records > UINTPTR_MAX - 12 - 32 * 96) return NO;
    return YES;
}

NSDictionary *PHReadRequestedDecks(uintptr_t header, NSDictionary *serialized, PHDeckRequest *observation) {
    if (observation) *observation = (PHDeckRequest){0};
    PHDeckRequest before, after;
    if (![serialized isKindOfClass:NSDictionary.class] ||
        (serialized[@"decksbs"] && ![serialized[@"decksbs"] isKindOfClass:NSArray.class]) ||
        !Observe(header, &before) || ![serialized[@"decksb"] isKindOfClass:NSArray.class] ||
        [serialized[@"decksb"] count] != before.primary || before.maximum < before.primary ||
        before.maximum - before.primary > 16 ||
        [serialized[@"decksbs"] count] != before.maximum - before.primary ||
        (before.maximum != before.primary && ![serialized[@"decksbs"] isKindOfClass:NSArray.class])) return nil;
    NSMutableDictionary *result = [NSMutableDictionary dictionary];
    NSUInteger requestIndex = 0;
    for (NSString *key in @[@"decksb", @"decksbs"]) {
        NSArray *records = serialized[key];
        if (!records) continue;
        NSMutableArray *selected = [NSMutableArray array];
        for (NSArray *wire in records) {
            NSArray *native = ReadDeck(before.records + 12 + 96 * requestIndex, requestIndex);
            ++requestIndex;
            if (!PHValidateDeckRecord(wire) || !native || ![PHDeckIdentity(wire) isEqual:PHDeckIdentity(native)] ||
                ![wire[7] isEqual:native[7]] || ![wire[8] isEqual:native[8]] ||
                ([wire[5] unsignedIntValue] && ![wire[5] isEqual:native[5]])) return nil;
            [selected addObject:native];
        }
        result[key] = selected;
    }
    if (!Observe(header, &after) || before.context != after.context || before.request != after.request ||
        before.records != after.records || before.primary != after.primary || before.secondary != after.secondary ||
        before.maximum != after.maximum) return nil;
    if (observation) *observation = before;
    return result;
}

@implementation PHDeckRestorer {
    NSMutableArray<NSMutableDictionary *> *_pending;
    NSUInteger _epoch, _restored, _mismatched, _invalid;
    NSString *_source, *_message;
}
- (instancetype)init { if ((self = [super init])) [self cancel]; return self; }
- (void)cancel { _pending = [NSMutableArray array]; _message = @"沒有待回蓋的選擇"; }
- (void)enqueue:(NSDictionary *)banks mask:(PHFeatureMask)mask epoch:(NSUInteger)epoch source:(NSString *)source {
    [self cancel]; _epoch = epoch; _source = [source copy]; _restored = _mismatched = _invalid = 0;
    NSUInteger bank = 0;
    for (NSString *key in @[@"decksb", @"decksbs"]) {
        NSArray *records = banks[key];
        if ([records isKindOfClass:NSArray.class]) for (NSUInteger i = 0; i < records.count; ++i) {
            if (!PHValidateDeckRecord(records[i])) continue;
            PHFeatureMask needed = 0;
            if ((mask & PHFeatureSkin) && [records[i][8] unsignedIntValue]) needed |= PHFeatureSkin;
            if ((mask & PHFeatureBGM) && [records[i][7] unsignedIntValue]) needed |= PHFeatureBGM;
            if ((mask & PHFeatureBadge) && [records[i][5] unsignedIntValue]) needed |= PHFeatureBadge;
            if (needed) [_pending addObject:[@{@"bank":@(bank), @"index":@(i), @"count":@(records.count),
                @"record":[records[i] copy], @"mask":@(needed)} mutableCopy]];
        }
        ++bank;
    }
    _message = _pending.count ? @"等待隊伍與功能資料就緒後回蓋" : @"沒有非預設的本地選擇需要回蓋";
}
- (NSDictionary *)drainHeader:(uintptr_t)header epoch:(NSUInteger)epoch enabled:(PHFeatureMask)enabled
    allowed:(PHFeatureMask)allowed badgeReady:(BOOL)badgeReady canUseBadge:(BOOL (^)(uint8_t))canUse {
    if (_epoch != epoch) { [self cancel]; return nil; }
    if (!_pending.count) return nil;
    for (NSMutableDictionary *entry in _pending) entry[@"mask"] = @([entry[@"mask"] unsignedIntegerValue] & enabled);
    NSIndexSet *cancelled = [_pending indexesOfObjectsPassingTest:^BOOL(NSDictionary *entry, NSUInteger i, BOOL *stop) {
        (void)i; (void)stop;
        return ![entry[@"mask"] unsignedIntegerValue];
    }];
    [_pending removeObjectsAtIndexes:cancelled];
    uint8_t primary = 0, secondary = 0, editing = 0; uint16_t maximum = 0;
    if (!_pending.count) { _message = @"功能已關閉，取消待回蓋選擇"; return nil; }
    if (!allowed || !Counts(header, &primary, &secondary, &maximum) ||
        !PHReadU8(header + PHSaveRVA + PHEditDirtyOffset, &editing) || editing) return nil;
    NSUInteger restored = 0, failures = 0;
    for (NSMutableDictionary *entry in _pending) {
        PHFeatureMask remaining = [entry[@"mask"] unsignedIntegerValue];
        const NSUInteger bank = [entry[@"bank"] unsignedIntegerValue], index = [entry[@"index"] unsignedIntegerValue];
        const NSUInteger count = bank ? secondary : primary;
        if ([entry[@"count"] unsignedIntegerValue] != count || index >= count) {
            entry[@"mask"] = @0; ++_mismatched; continue;
        }
        const uintptr_t address = header + PHSaveRVA + (bank ? PHSecondaryDecksOffset : PHPrimaryDecksOffset) + index * 96;
        NSArray *saved = entry[@"record"], *current = ReadDeck(address, index);
        if (!current) continue;
        if (![PHDeckIdentity(saved) isEqual:PHDeckIdentity(current)]) {
            entry[@"mask"] = @0; ++_mismatched; continue;
        }
        const NSUInteger fields[] = {8, 7, 5}, offsets[] = {28, 24, 95}, sizes[] = {2, 4, 1};
        const PHFeatureMask features[] = {PHFeatureSkin, PHFeatureBGM, PHFeatureBadge};
        for (NSUInteger f = 0; f < 3; ++f) {
            if (!(remaining & features[f])) continue;
            // Resolve nondefaults even while a resource table is loading: a
            // newer explicit native selection must never be overwritten later.
            if ([current[fields[f]] unsignedIntValue]) { remaining &= ~features[f]; continue; }
            if (!(allowed & features[f]) || (f == 2 && !badgeReady)) continue;
            if (f == 2 && (!canUse || !canUse([saved[5] unsignedCharValue]))) { remaining &= ~features[f]; ++_invalid; continue; }
            uint16_t data[2] = {[saved[8] unsignedShortValue], 0};
            if (f == 1) { uint32_t packed = [saved[7] unsignedIntValue]; data[0] = packed % 100000; data[1] = packed / 100000; }
            if (f == 2) data[0] = PHBadgeEncode([saved[5] unsignedCharValue], index);
            if (!PHWriteData(address + offsets[f], data, sizes[f])) { ++failures; continue; }
            NSArray *written = ReadDeck(address, index);
            if (!written || ![written[fields[f]] isEqual:saved[fields[f]]]) { ++failures; continue; }
            remaining &= ~features[f]; ++restored;
        }
        entry[@"mask"] = @(remaining);
    }
    NSIndexSet *done = [_pending indexesOfObjectsPassingTest:^BOOL(NSDictionary *entry, NSUInteger i, BOOL *stop) {
        (void)i; (void)stop;
        return ![entry[@"mask"] unsignedIntegerValue];
    }];
    [_pending removeObjectsAtIndexes:done]; _restored += restored;
    _message = [NSString stringWithFormat:@"%@：已回蓋 %lu 欄位；待重試 %lu 隊；未匹配 %lu 隊；無效徽章 %lu；本次寫入失敗 %lu",
        _source, (unsigned long)_restored, (unsigned long)_pending.count, (unsigned long)_mismatched,
        (unsigned long)_invalid, (unsigned long)failures];
    return @{@"restored":@(restored), @"message":_message};
}
- (NSDictionary *)snapshot { return @{@"pending":@(_pending.count), @"message":_message ?: @""}; }
@end
