#import "PHCodec.h"
#import <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <stdint.h>

NSString *const PHCodecErrorDomain = @"PADCheaper.Codec";

@implementation PHRewriteResult
@end

static void PHCodecError(NSError **error, NSString *message) {
    if (error) {
        *error = [NSError errorWithDomain:PHCodecErrorDomain code:1
                                userInfo:@{NSLocalizedDescriptionKey: message}];
    }
}

static BOOL PHIsInteger(id value, uint64_t maximum) {
    if (![value isKindOfClass:NSNumber.class] ||
        CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID()) {
        return NO;
    }
    double number = [value doubleValue];
    return isfinite(number) && number >= 0 && floor(number) == number &&
           number <= (double)maximum;
}

BOOL PHValidateDeckRecord(id object) {
    if (![object isKindOfClass:NSArray.class] || [object count] != 9) {
        return NO;
    }
    NSArray *record = object;
    for (NSUInteger i = 0; i < record.count; i++) {
        uint64_t maximum = i == 5 ? UINT8_MAX : (i == 8 ? UINT16_MAX : UINT32_MAX);
        if (!PHIsInteger(record[i], maximum)) {
            return NO;
        }
    }
    uint64_t packedBGM = [record[7] unsignedLongLongValue];
    return packedBGM % 100000u <= UINT16_MAX && packedBGM / 100000u <= UINT16_MAX;
}

NSArray<NSNumber *> *PHDeckIdentity(NSArray<NSNumber *> *record) {
    return @[record[0], record[1], record[2], record[3], record[4], record[6]];
}

PHRewriteResult *PHRewriteDeckBody(NSData *body, PHFeatureMask mask, NSError **error) {
    if (body.length == 0 || body.length > 16384 || (mask & ~(PHFeatureMask)7u) != 0) {
        PHCodecError(error, @"Unsupported body size or feature mask");
        return nil;
    }
    NSString *text = [[NSString alloc] initWithData:body encoding:NSUTF8StringEncoding];
    if (!text || [text rangeOfString:[NSString stringWithFormat:@"%C", (unichar)0]].location != NSNotFound) {
        PHCodecError(error, @"Body is not a valid UTF-8 form");
        return nil;
    }
    NSMutableArray<NSString *> *parts = [[text componentsSeparatedByString:@"&"] mutableCopy];
    NSMutableDictionary *banks = [NSMutableDictionary dictionary];
    BOOL changed = NO;
    for (NSUInteger index = 0; index < parts.count; index++) {
        NSString *part = parts[index];
        NSRange equals = [part rangeOfString:@"="];
        if (equals.location == NSNotFound) {
            continue;
        }
        NSString *key = [part substringToIndex:equals.location];
        if (![key isEqualToString:@"decksb"] && ![key isEqualToString:@"decksbs"]) {
            continue;
        }
        if (banks[key]) {
            PHCodecError(error, @"Duplicate deck bank");
            return nil;
        }
        NSData *json = [[part substringFromIndex:equals.location + 1]
                        dataUsingEncoding:NSUTF8StringEncoding];
        id value = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
        if (![value isKindOfClass:NSDictionary.class] ||
            !PHIsInteger(value[@"fmt"], 1) || [value[@"fmt"] unsignedIntValue] != 1 ||
            ![value[@"decks"] isKindOfClass:NSArray.class]) {
            PHCodecError(error, @"Unsupported deck bank format");
            return nil;
        }
        NSArray *records = value[@"decks"];
        NSUInteger capacity = [key isEqualToString:@"decksb"] ? 32 : 16;
        if (records.count > capacity) {
            PHCodecError(error, @"Deck bank exceeds the analyzed capacity");
            return nil;
        }
        NSMutableArray *replacement = [NSMutableArray arrayWithCapacity:records.count];
        BOOL bankChanged = NO;
        for (id object in records) {
            if (!PHValidateDeckRecord(object)) {
                PHCodecError(error, @"Invalid deck record");
                return nil;
            }
            NSMutableArray *record = [object mutableCopy];
            NSUInteger fields[] = {8, 7, 5};
            PHFeatureMask features[] = {PHFeatureSkin, PHFeatureBGM, PHFeatureBadge};
            for (NSUInteger i = 0; i < 3; i++) {
                if ((mask & features[i]) && [record[fields[i]] unsignedLongLongValue] != 0) {
                    record[fields[i]] = @0;
                    bankChanged = YES;
                }
            }
            [replacement addObject:record];
        }
        banks[key] = records;
        if (bankChanged) {
            NSMutableDictionary *object = [value mutableCopy];
            object[@"decks"] = replacement;
            NSData *encoded = [NSJSONSerialization dataWithJSONObject:object
                                 options:NSJSONWritingSortedKeys error:NULL];
            if (!encoded) {
                PHCodecError(error, @"Cannot encode deck bank");
                return nil;
            }
            parts[index] = [NSString stringWithFormat:@"%@=%@", key,
                            [[NSString alloc] initWithData:encoded encoding:NSUTF8StringEncoding]];
            changed = YES;
        }
    }
    if (!banks[@"decksb"] || [banks[@"decksb"] count] + [banks[@"decksbs"] count] > 32) {
        PHCodecError(error, @"Missing primary bank or invalid combined count");
        return nil;
    }
    PHRewriteResult *result = [PHRewriteResult new];
    result.body = changed ? [[parts componentsJoinedByString:@"&"]
                              dataUsingEncoding:NSUTF8StringEncoding] : body;
    result.banks = banks;
    result.changed = changed;
    return result;
}

NSDictionary *PHMergeBanks(NSDictionary *previous, NSDictionary *observed, PHFeatureMask mask) {
    NSMutableDictionary *merged = [NSMutableDictionary dictionary];
    for (NSString *key in @[@"decksb", @"decksbs"]) {
        NSArray *incoming = observed[key];
        NSArray *old = previous[key];
        if (![incoming isKindOfClass:NSArray.class]) {
            if ([old isKindOfClass:NSArray.class]) {
                merged[key] = old;
            }
            continue;
        }
        NSMutableArray *records = [NSMutableArray arrayWithCapacity:incoming.count];
        for (NSUInteger index = 0; index < incoming.count; index++) {
            NSArray *record = incoming[index];
            if (!PHValidateDeckRecord(record)) {
                continue;
            }
            NSMutableArray *next = [record mutableCopy];
            if (index < old.count && PHValidateDeckRecord(old[index]) &&
                [PHDeckIdentity(old[index]) isEqual:PHDeckIdentity(record)]) {
                NSUInteger fields[] = {8, 7, 5};
                PHFeatureMask features[] = {PHFeatureSkin, PHFeatureBGM, PHFeatureBadge};
                for (NSUInteger i = 0; i < 3; i++) {
                    if (!(mask & features[i])) {
                        next[fields[i]] = old[index][fields[i]];
                    }
                }
            }
            [records addObject:next];
        }
        merged[key] = records;
    }
    return merged;
}
