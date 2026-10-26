#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/runtime.h>
#import "PHCodec.h"
#import "PHMemory.h"
#import "PHTarget.h"
#include "PHTargetGenerated.h"
#include <mach-o/dyld.h>
#include <math.h>
#include <string.h>

typedef NSURLSessionDataTask *(*PHDataTaskIMP)(id, SEL, NSURLRequest *,
                            void (^)(NSData *, NSURLResponse *, NSError *));
typedef void (*PHVoidIMP)(id, SEL);

static const struct mach_header_64 *PHGuest;
static PHDataTaskIMP PHOriginalDataTask;
static PHVoidIMP PHOriginalDraw;
static PHVoidIMP PHOriginalBackground;
static BOOL PHNetworkInstalled;
static BOOL PHFrameInstalled;
static BOOL PHValidated;
static BOOL PHFrameMainThread;
static BOOL PHAccountReady;
static BOOL PHStorageFault;
static uint32_t PHPlayer;
static NSUInteger PHAccountEpoch;
static NSObject *PHLock;
static NSMutableDictionary *PHPending;
static NSDictionary *PHConfig;
static NSDictionary *PHLastBanks;
static NSMutableArray<NSDictionary *> *PHFlagPatches;
static uint8_t PHSkinAdded[128];
static uint8_t PHBGMAdded[128];
static CFAbsoluteTime PHLastMaintenance;
static CFAbsoluteTime PHLastStatus;
static NSUInteger PHFrameCount;
static NSString *PHFolder;
static BOOL PHProbeLogged;

static uintptr_t PHSave(void) { return (uintptr_t)PHGuest + PHSaveRVA; }

static void PHLog(NSString *message) {
    NSLog(@"[PADCheaper] %@", message);
}

static BOOL PHWriteJSON(NSDictionary *object, NSString *name) {
    NSData *data = [NSJSONSerialization dataWithJSONObject:object
                    options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL];
    NSString *path = [PHFolder stringByAppendingPathComponent:name];
    if (!data || ![data writeToFile:path options:NSDataWritingAtomic error:NULL]) {
        return NO;
    }
    [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions: @0600}
                                   ofItemAtPath:path error:NULL];
    return YES;
}

static NSDictionary *PHReadJSON(NSString *name) {
    NSString *path = [PHFolder stringByAppendingPathComponent:name];
    NSDictionary *attributes = [NSFileManager.defaultManager attributesOfItemAtPath:path error:NULL];
    if ([attributes[NSFileSize] unsignedLongLongValue] > 65536) return nil;
    NSData *data = [NSData dataWithContentsOfFile:path];
    id object = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    return [object isKindOfClass:NSDictionary.class] ? object : nil;
}

static BOOL PHEnabled(NSString *key) {
    return [PHConfig[@"experimentalWrites"] isEqual:@YES] &&
           [PHConfig[@"features"][key] isEqual:@YES];
}

static BOOL PHFiltersConfigured(void) {
    return [PHConfig[@"apiHosts"] isKindOfClass:NSArray.class] &&
           [PHConfig[@"apiPaths"] isKindOfClass:NSArray.class] &&
           [PHConfig[@"apiHosts"] count] > 0 && [PHConfig[@"apiPaths"] count] > 0;
}

static PHFeatureMask PHActiveMask(void) {
    if (!PHValidated || !PHFrameInstalled || !PHNetworkInstalled || !PHFrameMainThread ||
        !PHAccountReady || PHStorageFault || !PHFiltersConfigured()) return 0;
    uint32_t player = 0;
    if (!PHReadU32(PHSave() + PHPlayerIDOffset, &player) || player == 0 || player != PHPlayer) return 0;
    PHFeatureMask mask = 0;
    if (PHEnabled(@"skin")) mask |= PHFeatureSkin;
    if (PHEnabled(@"bgm")) mask |= PHFeatureBGM;
    // Badge's parameter gate and editing-bank mapping are not implemented yet.
    return mask;
}

static NSDictionary *PHLoadAccount(uint32_t player) {
    NSDictionary *store = PHReadJSON([NSString stringWithFormat:@"account-%u.json", player]);
    if (![store[@"schema"] isEqual:@1] || [store[@"player"] unsignedIntValue] != player ||
        ![store[@"target"] isEqualToString:@"jp.gungho.pad-ios-23.6.0-106878"] ||
        ![store[@"banks"] isKindOfClass:NSDictionary.class]) return @{};
    // The file must also be valid under the protocol's bank/record bounds.
    NSDictionary *banks = store[@"banks"];
    if (![banks[@"decksb"] isKindOfClass:NSArray.class]) return @{};
    for (NSString *key in @[@"decksb", @"decksbs"]) {
        id records = banks[key];
        if (!records) continue;
        NSUInteger capacity = [key isEqualToString:@"decksb"] ? 32 : 16;
        if (![records isKindOfClass:NSArray.class] || [records count] > capacity) return @{};
        for (id record in records) if (!PHValidateDeckRecord(record)) return @{};
    }
    if ([banks[@"decksb"] count] + [banks[@"decksbs"] count] > 32) return @{};
    return banks;
}

static BOOL PHStoreBanks(NSDictionary *banks) {
    NSDictionary *store = @{@"schema": @1, @"target": @"jp.gungho.pad-ios-23.6.0-106878",
                            @"player": @(PHPlayer), @"banks": banks};
    return PHWriteJSON(store, [NSString stringWithFormat:@"account-%u.json", PHPlayer]);
}

static void PHReloadConfig(void) {
    NSDictionary *next = PHReadJSON(@"config.json");
    BOOL filtersValid = [next[@"apiHosts"] isKindOfClass:NSArray.class] &&
                        [next[@"apiPaths"] isKindOfClass:NSArray.class];
    if (filtersValid) {
        for (id value in next[@"apiHosts"]) if (![value isKindOfClass:NSString.class]) filtersValid = NO;
        for (id value in next[@"apiPaths"]) if (![value isKindOfClass:NSString.class]) filtersValid = NO;
    }
    if (![next[@"schema"] isEqual:@1] || ![next[@"features"] isKindOfClass:NSDictionary.class] || !filtersValid) {
        next = @{@"schema": @1, @"experimentalWrites": @NO,
                 @"features": @{@"skin": @NO, @"bgm": @NO, @"timerFix": @NO},
                 @"apiHosts": @[], @"apiPaths": @[]};
    }
    PHConfig = next;
}

static BOOL PHCurrentPlayer(uint32_t *player) {
    return PHReadU32(PHSave() + PHPlayerIDOffset, player) && *player != 0;
}

static void PHResetAccount(uint32_t player, BOOL loginSuccess) {
    if (player != PHPlayer || loginSuccess) {
        @synchronized (PHLock) { PHPending = nil; }
        PHPlayer = player;
        PHAccountEpoch++;
        PHAccountReady = loginSuccess && player != 0;
        PHStorageFault = NO;
        PHLastBanks = PHAccountReady ? PHLoadAccount(player) : @{};
        // Login replaced ownership words. Old bit snapshots must not be restored.
        memset(PHSkinAdded, 0, sizeof(PHSkinAdded));
        memset(PHBGMAdded, 0, sizeof(PHBGMAdded));
        PHProbeLogged = NO;
        PHLog(loginSuccess ? @"Native login parse observed; account epoch reset" :
                              @"Account changed; waiting for a successful native login parse");
    }
}

static BOOL PHBankCounts(uint8_t *primary, uint8_t *secondary) {
    uint16_t maximum = 0;
    return PHReadU8(PHSave() + PHPrimaryCountOffset, primary) &&
           PHReadU8(PHSave() + PHSecondaryCountOffset, secondary) &&
           PHReadU16(PHSave() + PHMaxDecksOffset, &maximum) &&
           maximum > 0 && maximum <= 32 && *primary > 0 && *primary <= 32 &&
           *secondary <= 16 && (unsigned)*primary + *secondary <= 32;
}

static NSArray<NSNumber *> *PHReadDeck(uintptr_t address, NSUInteger index) {
    uint8_t bytes[96];
    if (!PHReadMemory(address, bytes, sizeof(bytes))) return nil;
    uint32_t cards[5], extra;
    uint16_t bgm1, bgm2, skin;
    memcpy(cards, bytes, sizeof(cards));
    memcpy(&extra, bytes + 0x14, sizeof(extra));
    memcpy(&bgm1, bytes + 0x18, sizeof(bgm1));
    memcpy(&bgm2, bytes + 0x1a, sizeof(bgm2));
    memcpy(&skin, bytes + 0x1c, sizeof(skin));
    uint8_t badge = bytes[0x5f] ^ (uint8_t)(15u * index + 0x61u);
    uint64_t packed = bgm1 + 100000ull * bgm2;
    if (packed > UINT32_MAX) return nil;
    return @[@(cards[0]), @(cards[1]), @(cards[2]), @(cards[3]), @(cards[4]),
             @(badge), @(extra), @(packed), @(skin)];
}

static void PHRestoreBanks(NSDictionary *banks, PHFeatureMask mask) {
    uint8_t primary = 0, secondary = 0, editing = 0;
    if (!mask || !PHBankCounts(&primary, &secondary) ||
        !PHReadU8(PHSave() + PHEditDirtyOffset, &editing) || editing != 0) return;
    NSUInteger restored = 0;
    NSArray *keys = @[@"decksb", @"decksbs"];
    uintptr_t offsets[] = {PHPrimaryDecksOffset, PHSecondaryDecksOffset};
    NSUInteger counts[] = {primary, secondary};
    for (NSUInteger bank = 0; bank < 2; bank++) {
        NSArray *records = banks[keys[bank]];
        if (![records isKindOfClass:NSArray.class] || records.count != counts[bank]) continue;
        for (NSUInteger index = 0; index < records.count; index++) {
            NSArray *saved = records[index];
            uintptr_t address = PHSave() + offsets[bank] + index * 96u;
            NSArray *current = PHReadDeck(address, index);
            if (!PHValidateDeckRecord(saved) || !current ||
                ![PHDeckIdentity(saved) isEqual:PHDeckIdentity(current)]) continue;
            // Restore only a matching parser-produced default. A nondefault value
            // can be a newer user edit and is never overwritten.
            if ((mask & PHFeatureSkin) && [current[8] unsignedIntValue] == 0 &&
                [saved[8] unsignedIntValue] != 0) {
                uint16_t value = [saved[8] unsignedShortValue];
                if (PHWriteData(address + 0x1c, &value, sizeof(value))) restored++;
            }
            if ((mask & PHFeatureBGM) && [current[7] unsignedIntValue] == 0 &&
                [saved[7] unsignedIntValue] != 0) {
                uint32_t packed = [saved[7] unsignedIntValue];
                uint16_t values[] = {(uint16_t)(packed % 100000u), (uint16_t)(packed / 100000u)};
                if (PHWriteData(address + 0x18, values, sizeof(values))) restored++;
            }
        }
    }
    if (restored) PHLog([NSString stringWithFormat:@"Restored %lu selected fields after native parse",
                         (unsigned long)restored]);
}

static void PHRevertDisabled(PHFeatureMask mask) {
    uint8_t *added[] = {PHSkinAdded, PHBGMAdded};
    uintptr_t offsets[] = {PHSkinOwnedOffset, PHBGMOwnedOffset};
    PHFeatureMask features[] = {PHFeatureSkin, PHFeatureBGM};
    for (NSUInteger i = 0; i < 2; i++) {
        if (!(mask & features[i])) {
            uint8_t current[128];
            if (PHReadMemory(PHSave() + offsets[i], current, sizeof(current))) {
                BOOL changed = NO;
                for (NSUInteger j = 0; j < sizeof(current); j++) {
                    uint8_t next = current[j] & (uint8_t)~added[i][j];
                    changed |= next != current[j];
                    current[j] = next;
                }
                if (changed) PHWriteData(PHSave() + offsets[i], current, sizeof(current));
            }
            memset(added[i], 0, 128);
        }
    }
    NSMutableArray *remaining = [NSMutableArray array];
    uintptr_t table = 0;
    uint32_t count = 0;
    BOOL tableValid = PHReadPointer(PHSave() + PHEntryPointerOffset, &table) && table &&
                      PHReadU32(PHSave() + PHEntryCountOffset, &count) && count > 0 && count <= 2048;
    for (NSDictionary *patch in PHFlagPatches) {
        uintptr_t address = [patch[@"address"] unsignedLongLongValue];
        if (!tableValid || [patch[@"table"] unsignedLongLongValue] != table || address < table + 0xe ||
            address - table >= (uintptr_t)count * 52u || (address - table - 0xe) % 52u != 0) continue;
        if (mask & [patch[@"feature"] unsignedIntegerValue]) {
            [remaining addObject:patch];
            continue;
        }
        uint16_t idValue = 0, flags = 0, original = [patch[@"original"] unsignedShortValue];
        if (PHReadU16(address - 0xe, &idValue) && idValue == [patch[@"id"] unsignedShortValue] &&
            PHReadU16(address, &flags) && flags == (uint16_t)(original | 1u)) {
            PHWriteData(address, &original, sizeof(original));
        }
    }
    PHFlagPatches = remaining;
}

static void PHApplyUnlocks(PHFeatureMask mask) {
    if (!mask) return;
    uint32_t count = 0;
    uintptr_t table = 0;
    if (!PHReadU32(PHSave() + PHEntryCountOffset, &count) || count == 0 || count > 2048 ||
        !PHReadPointer(PHSave() + PHEntryPointerOffset, &table) || table == 0 ||
        table > UINTPTR_MAX - (uintptr_t)count * 52u) return;
    NSMutableData *entries = [NSMutableData dataWithLength:(NSUInteger)count * 52u];
    if (!PHReadMemory(table, entries.mutableBytes, entries.length)) return;
    uint8_t bitmaps[2][128], original[2][128];
    uintptr_t offsets[] = {PHSkinOwnedOffset, PHBGMOwnedOffset};
    uint8_t *added[] = {PHSkinAdded, PHBGMAdded};
    if (!PHReadMemory(PHSave() + offsets[0], bitmaps[0], 128) ||
        !PHReadMemory(PHSave() + offsets[1], bitmaps[1], 128)) return;
    memcpy(original, bitmaps, sizeof(original));
    const uint8_t *bytes = entries.bytes;
    for (NSUInteger index = 0; index < count; index++) {
        uint16_t identifier, flags;
        memcpy(&identifier, bytes + index * 52, sizeof(identifier));
        memcpy(&flags, bytes + index * 52 + 0xe, sizeof(flags));
        NSUInteger bank = identifier < 10000 ? 0 : 1;
        NSUInteger bitmapIndex = bank ? identifier - 10000u : identifier;
        PHFeatureMask feature = bank ? PHFeatureBGM : PHFeatureSkin;
        if (!(mask & feature) || bitmapIndex >= 1024) continue;
        bitmaps[bank][bitmapIndex / 8] |= (uint8_t)(1u << (bitmapIndex % 8));
        if (flags != 0 && !(flags & 1u)) {
            uintptr_t address = table + index * 52 + 0xe;
            uint16_t next = flags | 1u;
            if (PHWriteData(address, &next, sizeof(next))) {
                [PHFlagPatches addObject:@{@"address": @(address), @"id": @(identifier),
                                          @"table": @(table), @"original": @(flags), @"feature": @(feature)}];
            }
        }
    }
    for (NSUInteger bank = 0; bank < 2; bank++) {
        if (memcmp(original[bank], bitmaps[bank], 128) != 0 &&
            PHWriteData(PHSave() + offsets[bank], bitmaps[bank], 128)) {
            for (NSUInteger j = 0; j < 128; j++) added[bank][j] |= bitmaps[bank][j] & ~original[bank][j];
        }
    }
}

typedef struct {
    BOOL ready;
    uintptr_t context;
    uintptr_t requestBlock;
    uint32_t requestID;
} PHParseObservation;

static PHParseObservation PHBeforeFrame(void) {
    PHParseObservation value = {0};
    uint32_t state = 0, phase = 0;
    uint8_t flags = 0;
    if (!PHReadU32((uintptr_t)PHGuest + PHNetworkStateRVA, &state) || state != 2 ||
        !PHReadU32((uintptr_t)PHGuest + PHNetworkPhaseRVA, &phase) || phase != 6 ||
        !PHReadPointer(PHSave() + PHAPIContextPointerOffset, &value.context) || !value.context ||
        !PHReadPointer(value.context + PHAPIActiveRequestOffset, &value.requestBlock) || !value.requestBlock ||
        !PHReadU8(value.requestBlock + 32, &flags) || (flags & 3u) != 1u ||
        !PHReadU32(value.requestBlock + 8, &value.requestID) ||
        (value.requestID != 33 && value.requestID != 122)) return value;
    value.ready = YES;
    return value;
}

static void PHAfterParse(PHParseObservation before) {
    if (!before.ready) return;
    uintptr_t currentContext = 0, active = 0;
    uint32_t nativeError = UINT32_MAX;
    if (!PHReadPointer(PHSave() + PHAPIContextPointerOffset, &currentContext) ||
        currentContext != before.context ||
        !PHReadPointer(currentContext + PHAPIActiveRequestOffset, &active) ||
        active == before.requestBlock ||
        !PHReadU32(currentContext + PHAPIErrorOffset, &nativeError) || nativeError != 0) return;
    if (before.requestID == 33) {
        uint32_t player = 0;
        if (PHCurrentPlayer(&player)) {
            PHResetAccount(player, YES);
            PHRestoreBanks(PHLastBanks, PHActiveMask());
        }
        return;
    }
    NSDictionary *transaction = nil;
    @synchronized (PHLock) {
        if ([PHPending[@"success"] isEqual:@YES] &&
            [PHPending[@"restoreAllowed"] isEqual:@YES] &&
            [PHPending[@"nativeContext"] unsignedLongLongValue] == before.context &&
            [PHPending[@"nativeRequest"] unsignedLongLongValue] == before.requestBlock &&
            [PHPending[@"epoch"] unsignedIntegerValue] == PHAccountEpoch) {
            transaction = [PHPending copy];
            PHPending = nil;
        }
    }
    if (transaction) PHRestoreBanks(transaction[@"banks"],
                                   PHActiveMask() & [transaction[@"mask"] unsignedIntegerValue]);
}

static void PHMaintenance(void) {
    uint32_t player = 0;
    if (PHCurrentPlayer(&player) && player != PHPlayer) PHResetAccount(player, NO);
    PHReloadConfig();
    PHFeatureMask mask = PHActiveMask();
    PHRevertDisabled(mask);
    PHApplyUnlocks(mask);
    @synchronized (PHLock) {
        if (PHPending && CFAbsoluteTimeGetCurrent() - [PHPending[@"time"] doubleValue] > 60) {
            PHPending = nil;
            PHLog(@"Pending restore expired; local snapshot remains on disk");
        }
    }
    uint32_t count = 0;
    uintptr_t pointer = 0;
    PHReadU32(PHSave() + PHEntryCountOffset, &count);
    PHReadPointer(PHSave() + PHEntryPointerOffset, &pointer);
    if (!PHProbeLogged && count > 0 && count <= 2048 && pointer) {
        PHLog([NSString stringWithFormat:@"Read-only entry probe: %u entries, pointer range %@",
               count, PHMemoryHasProtection(pointer, count * 52u, VM_PROT_READ, 0) ? @"valid" : @"invalid"]);
        PHProbeLogged = YES;
    }
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (now - PHLastStatus >= 5) {
        PHLastStatus = now;
        PHWriteJSON(@{@"schema": @1, @"targetMatched": @(PHValidated),
                      @"version": @(PHTargetVersion), @"iosBuild": @(PHTargetBuild),
                      @"frameOnMainThread": @(PHFrameMainThread), @"accountReady": @(PHAccountReady),
                      @"apiFiltersConfigured": @(PHFiltersConfigured()), @"activeMask": @(mask),
                      @"entryCount": @(count), @"storageFault": @(PHStorageFault),
                      @"badgeImplemented": @NO, @"deviceVerified": @NO}, @"status.json");
    }
}

static void PHDrawFrame(id self, SEL command) {
    BOOL onMain = NSThread.isMainThread;
    if (!onMain) {
        PHOriginalDraw(self, command);
        return;
    }
    PHFrameMainThread = YES;
    PHParseObservation before = PHBeforeFrame();
    PHOriginalDraw(self, command);
    PHFrameCount++;
    PHAfterParse(before);
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (now - PHLastMaintenance >= 1.0) {
        PHLastMaintenance = now;
        PHMaintenance();
    }
}

static void PHRestoreBackground(id self, SEL command) {
    PHOriginalBackground(self, command);
    if (!NSThread.isMainThread || !PHValidated || !PHEnabled(@"timerFix")) return;
    uint8_t scene = 0, requested = 0;
    uintptr_t object = 0, impl = 0;
    uintptr_t game = (uintptr_t)PHGuest + PHGameMainRVA;
    uint8_t flags = 0;
    if (!PHReadU8(PHSave(), &scene) || !PHReadU8(PHSave() + 1, &requested) ||
        scene != 1 || requested != scene ||
        !PHReadPointer((uintptr_t)PHGuest + PHSceneTableRVA + 16u + 8u, &object) || object != game ||
        !PHReadPointer(game + PHTimerImplOffset, &impl) || !impl ||
        !PHReadU8(impl + PHTimerFlagsOffset, &flags)) return;
    // Preserve abandonment/alert bits. This remains an experimental background
    // resume fix, not a claim of complete Android timer behavior.
    if ((flags & 0x22u) == 0x20u) {
        uint8_t next = flags & (uint8_t)~0x20u;
        if (PHWriteData(impl + PHTimerFlagsOffset, &next, sizeof(next)))
            PHLog(@"Cleared background-resume bit 5; other timer flags preserved");
    }
}

static BOOL PHQueryMatches(NSURL *url, NSString *key, NSString *expected) {
    NSArray<NSURLQueryItem *> *items = [NSURLComponents componentsWithURL:url
                                                      resolvingAgainstBaseURL:NO].queryItems;
    NSUInteger count = 0;
    for (NSURLQueryItem *item in items) {
        if ([item.name isEqualToString:key]) {
            if (![item.value isEqualToString:expected]) return NO;
            count++;
        }
    }
    return count == 1;
}

static NSURLSessionDataTask *PHDataTask(id self, SEL command, NSURLRequest *request,
                                      void (^completion)(NSData *, NSURLResponse *, NSError *)) {
    if (!NSThread.isMainThread || ![request.HTTPMethod isEqualToString:@"POST"] ||
        !PHQueryMatches(request.URL, @"action", @"save_decks") || !request.HTTPBody) {
        return PHOriginalDataTask(self, command, request, completion);
    }
    PHRewriteResult *probe = PHRewriteDeckBody(request.HTTPBody, 0, NULL);
    if (!probe) return PHOriginalDataTask(self, command, request, completion);
    static NSMutableSet *seen;
    if (!seen) seen = [NSMutableSet set];
    NSString *endpoint = [NSString stringWithFormat:@"%@%@", request.URL.host, request.URL.path];
    if (![seen containsObject:endpoint]) {
        [seen addObject:endpoint];
        PHLog([NSString stringWithFormat:@"PAD API candidate host=%@ path=%@ (query omitted)",
                request.URL.host, request.URL.path]);
        PHWriteJSON(@{@"schema": @1, @"host": request.URL.host ?: @"",
                      @"path": request.URL.path ?: @"", @"queryIncluded": @NO}, @"endpoint.json");
    }
    PHFeatureMask mask = PHActiveMask();
    uint32_t player = 0;
    if (!mask || !completion || !PHCurrentPlayer(&player) || player != PHPlayer ||
        ![PHConfig[@"apiHosts"] containsObject:request.URL.host.lowercaseString] ||
        ![PHConfig[@"apiPaths"] containsObject:request.URL.path] ||
        !PHQueryMatches(request.URL, @"pid", [NSString stringWithFormat:@"%u", player])) {
        return PHOriginalDataTask(self, command, request, completion);
    }
    BOOL hasPending = NO;
    @synchronized (PHLock) { hasPending = PHPending != nil; }
    PHRewriteResult *result = PHRewriteDeckBody(request.HTTPBody, mask, NULL);
    if (!result) return PHOriginalDataTask(self, command, request, completion);
    NSDictionary *banks = PHMergeBanks(PHLastBanks ?: @{}, result.banks, mask);
    if (!PHStoreBanks(banks)) {
        PHStorageFault = YES;
        PHLog(@"Local snapshot write failed; feature writes disabled");
        return PHOriginalDataTask(self, command, request, completion);
    }
    PHLastBanks = banks;
    if (!result.changed) return PHOriginalDataTask(self, command, request, completion);
    NSMutableURLRequest *copy = [request mutableCopy];
    copy.HTTPBody = result.body;
    [copy setValue:[NSString stringWithFormat:@"%lu", (unsigned long)result.body.length]
          forHTTPHeaderField:@"Content-Length"];
    uintptr_t nativeContext = 0, nativeRequest = 0;
    uint32_t requestID = 0;
    BOOL nativeMatch = PHReadPointer(PHSave() + PHAPIContextPointerOffset, &nativeContext) && nativeContext &&
                       PHReadPointer(nativeContext + PHAPIActiveRequestOffset, &nativeRequest) && nativeRequest &&
                       PHReadU32(nativeRequest + 8, &requestID) && requestID == 122;
    NSMutableDictionary *transaction = [@{@"banks": banks, @"mask": @(mask),
                       @"epoch": @(PHAccountEpoch), @"time": @(CFAbsoluteTimeGetCurrent()),
                       @"nativeContext": @(nativeContext), @"nativeRequest": @(nativeRequest),
                       @"restoreAllowed": @(nativeMatch && !hasPending)} mutableCopy];
    @synchronized (PHLock) { PHPending = transaction; }
    NSURLSessionDataTask *task = PHOriginalDataTask(self, command, copy,
                ^(NSData *data, NSURLResponse *response, NSError *error) {
        id object = (!error && data.length > 0 && data.length <= 4 * 1024 * 1024) ?
             [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
        BOOL success = !error && [response isKindOfClass:NSHTTPURLResponse.class] &&
                       [(NSHTTPURLResponse *)response statusCode] == 200 &&
                       [object isKindOfClass:NSDictionary.class] &&
                       [object[@"res"] isKindOfClass:NSNumber.class] &&
                       CFGetTypeID((__bridge CFTypeRef)object[@"res"]) != CFBooleanGetTypeID() &&
                       [object[@"res"] doubleValue] == 0;
        @synchronized (PHLock) {
            if (PHPending == transaction) {
                transaction[@"success"] = @(success);
                if (!success) PHPending = nil;
            }
        }
        completion(data, response, error);
    });
    if (!task) {
        @synchronized (PHLock) { if (PHPending == transaction) PHPending = nil; }
    }
    PHLog(@"Submitted copied save_decks body with selected fields defaulted");
    if (!nativeMatch || hasPending) PHLog(@"Restore correlation unavailable; latest local snapshot preserved for next login");
    return task;
}

static BOOL PHVoidMethod(Class cls, SEL selector, uintptr_t expectedRVA,
                         IMP replacement, PHVoidIMP *original) {
    Method method = class_getInstanceMethod(cls, selector);
    if (!method || method_getNumberOfArguments(method) != 2) return NO;
    char returnType[8];
    method_getReturnType(method, returnType, sizeof(returnType));
    IMP implementation = method_getImplementation(method);
    if (returnType[0] != 'v' || (uintptr_t)implementation != (uintptr_t)PHGuest + expectedRVA ||
        !PHAddressIsGuestCode((uintptr_t)implementation, PHGuest)) return NO;
    const char *encoding = method_getTypeEncoding(method);
    *original = (PHVoidIMP)implementation;
    if (!class_addMethod(cls, selector, replacement, encoding)) {
        *original = (PHVoidIMP)method_setImplementation(method, replacement);
    }
    return YES;
}

static BOOL PHInstallNetwork(void) {
    Class cls = object_getClass(NSURLSession.sharedSession);
    SEL selector = @selector(dataTaskWithRequest:completionHandler:);
    Method method = class_getInstanceMethod(cls, selector);
    if (!method || method_getNumberOfArguments(method) != 4) return NO;
    char returnType[8], requestType[8], blockType[8];
    method_getReturnType(method, returnType, sizeof(returnType));
    method_getArgumentType(method, 2, requestType, sizeof(requestType));
    method_getArgumentType(method, 3, blockType, sizeof(blockType));
    if (returnType[0] != '@' || requestType[0] != '@' || strncmp(blockType, "@?", 2) != 0) return NO;
    const char *encoding = method_getTypeEncoding(method);
    PHOriginalDataTask = (PHDataTaskIMP)method_getImplementation(method);
    if (!class_addMethod(cls, selector, (IMP)PHDataTask, encoding)) {
        PHOriginalDataTask = (PHDataTaskIMP)method_setImplementation(method, (IMP)PHDataTask);
    }
    return YES;
}

static void PHBootstrap(NSUInteger attempt) {
    PHGuest = PHResolveGuestHeader();
    if (!PHGuest && attempt < 20) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            PHBootstrap(attempt + 1);
        });
        return;
    }
    NSString *reason = nil;
    if (!PHGuest || !PHValidateGuest(PHGuest, &reason)) {
        PHLog(reason ?: @"Guest image not found; no hooks installed");
        return;
    }
    PHFolder = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/PADHelper"];
    [NSFileManager.defaultManager createDirectoryAtPath:PHFolder withIntermediateDirectories:YES
                                            attributes:@{NSFilePosixPermissions: @0700} error:NULL];
    if (!PHReadJSON(@"config.json")) {
        PHWriteJSON(@{@"schema": @1, @"experimentalWrites": @NO,
             @"features": @{@"skin": @NO, @"bgm": @NO, @"timerFix": @NO},
             @"apiHosts": @[], @"apiPaths": @[]}, @"config.json");
    }
    PHReloadConfig();
    PHValidated = YES;
    PHLog([NSString stringWithFormat:@"Matched PAD %@ build %@ UUID %@; JIT-less prototype",
           @(PHTargetVersion), @(PHTargetBuild), @(PHTargetUUIDString)]);
    Class view = objc_getClass("ViewController");
    PHFrameInstalled = view && PHVoidMethod(view, sel_registerName("drawFrame"), PHDrawFrameRVA,
                                            (IMP)PHDrawFrame, &PHOriginalDraw);
    BOOL lifecycle = view && PHVoidMethod(view, sel_registerName("restoreFromBackground"), PHRestoreBackgroundRVA,
                                           (IMP)PHRestoreBackground, &PHOriginalBackground);
    PHNetworkInstalled = PHInstallNetwork();
    PHLog([NSString stringWithFormat:@"Hooks frame=%d lifecycle=%d network=%d; writes default to off",
           PHFrameInstalled, lifecycle, PHNetworkInstalled]);
    PHWriteJSON(@{@"schema": @1, @"targetMatched": @YES, @"version": @(PHTargetVersion),
                  @"iosBuild": @(PHTargetBuild), @"frameHook": @(PHFrameInstalled),
                  @"networkHook": @(PHNetworkInstalled), @"lifecycleHook": @(lifecycle),
                  @"activeMask": @0, @"deviceVerified": @NO}, @"status.json");
}

__attribute__((constructor))
static void PADCheaperInitialize(void) {
    @autoreleasepool {
        PHLock = [NSObject new];
        PHFlagPatches = [NSMutableArray array];
        PHLastBanks = @{};
        // Guest classes and image metadata must be available before hook setup.
        // Heavy initialization runs after dyld's constructor returns.
        dispatch_async(dispatch_get_main_queue(), ^{ PHBootstrap(0); });
    }
}
