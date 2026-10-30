#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/runtime.h>
#import "PHCodec.h"
#import "PHMemory.h"
#import "PHTarget.h"
#import "PHConfiguration.h"
#import "PHOverlay.h"
#import "PHSaveState.h"
#import "PHDeckPersistence.h"
#import "PHBattleSession.h"
#import "PHOrbSession.h"
#import "PHOrbPlanning.h"
#import "PHOrbProfiles.h"
#import <UIKit/UIKit.h>
#import "PHFormationModel.h"
#import "PHBadges.h"
#import "PHTimer.h"
#import "PHMusic.h"
#import "PHUnlockRestoration.h"
#include "PHBadgeModel.h"
#include "PHTargetGenerated.h"
#include <mach-o/dyld.h>
#include <math.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>

typedef NSURLSessionDataTask *(*PHDataTaskIMP)(id, SEL, NSURLRequest *,
                            void (^)(NSData *, NSURLResponse *, NSError *));
typedef void (*PHVoidIMP)(id, SEL);

static const struct mach_header_64 *PHGuest;
static PHDataTaskIMP PHOriginalDataTask;
static PHVoidIMP PHOriginalDraw;
static PHVoidIMP PHOriginalBackground;
static BOOL PHNetworkInstalled;
static BOOL PHFrameInstalled;
static BOOL PHLifecycleInstalled;
static BOOL PHValidated;
static BOOL PHFrameMainThread;
static BOOL PHAccountReady;
static BOOL PHStorageFault;
static BOOL PHStorageReady;
static BOOL PHConfigValid;
static BOOL PHContextIdentified;
static BOOL PHOverlayVisible;
static BOOL PHUnlockDataReady;
static BOOL PHOwnershipWriteFault;
static atomic_bool PHFrameOffMain;
static uint32_t PHPlayer;
static NSUInteger PHAccountEpoch;
static PHSaveState *PHSaveStateTracker;
static PHDeckRestorer *PHDeckRestoreTracker;
static PHBattleSession *PHBattleTracker;
static PHBadges *PHBadgeTracker;
static PHTimer *PHTimerTracker;
static PHMusic *PHMusicTracker;
static PHOrbSession *PHOrbTracker;
static NSString *PHOrbQualityNotice;
static NSString *PHOrbThornNotice;
static NSString *PHOrbGoalNotice;
static NSString *PHOrbRuleNotice;
static NSString *PHOrbRecoveryNotice;
static NSString *PHOrbProfileNotice;
static NSDictionary *PHConfig;
static NSDictionary *PHFormationDraft;
static NSDictionary *PHFormationLibrary;
static NSDictionary *PHFormationSaveResult;
static BOOL PHFormationLibraryStored;
static NSDictionary *PHLastBanks;
static NSMutableArray<NSDictionary *> *PHFlagPatches;
static uint8_t PHSkinAdded[128];
static uint8_t PHBGMAdded[128];
static CFAbsoluteTime PHLastMaintenance;
static CFAbsoluteTime PHLastStatus;
static NSUInteger PHFrameCount;
static NSString *PHFolder;
static BOOL PHProbeLogged;
static NSUInteger PHBootstrapGeneration;
static NSString *PHStartupState;
static NSString *PHValidationMessage;
static NSString *PHStorageMessage;
static NSString *PHFeedback;
static NSDictionary *PHObservedEndpoint;
static uint32_t PHObservedEntries;

static void PHBootstrap(NSUInteger attempt, NSUInteger generation);
static NSDictionary *PHRuntimeSnapshot(void);
static void PHHandleOverlayAction(NSString *action, id value);

static uintptr_t PHSave(void) { return (uintptr_t)PHGuest + PHSaveRVA; }

static void PHLog(NSString *message) {
    NSLog(@"[PADCheaper] %@", message);
}

static BOOL PHWriteJSON(NSDictionary *object, NSString *name) {
    if (!PHFolder.length || !object || !name.length) return NO;
    NSData *data = [NSJSONSerialization dataWithJSONObject:object
                    options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL];
    if ([name isEqual:@"config.json"] && data.length > 4 * 1024 * 1024) return NO;
    NSString *path = [PHFolder stringByAppendingPathComponent:name];
    if (!data || ![data writeToFile:path options:NSDataWritingAtomic error:NULL]) {
        return NO;
    }
    [NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions: @0600}
                                   ofItemAtPath:path error:NULL];
    return YES;
}

static NSDictionary *PHReadJSON(NSString *name) {
    if (!PHFolder.length) return nil;
    NSString *path = [PHFolder stringByAppendingPathComponent:name];
    NSDictionary *attributes = [NSFileManager.defaultManager attributesOfItemAtPath:path error:NULL];
    NSUInteger limit = [name isEqual:@"config.json"] ? 4 * 1024 * 1024 : [name isEqual:@"formations.json"] ? 2 * 1024 * 1024 : 65536;
    if ([attributes[NSFileSize] unsignedLongLongValue] > limit) return nil;
    NSFileHandle *handle = [NSFileHandle fileHandleForReadingFromURL:[NSURL fileURLWithPath:path] error:NULL];
    NSData *data = [handle readDataUpToLength:limit + 1 error:NULL];
    [handle closeAndReturnError:NULL];
    id object = data.length && data.length <= limit ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
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

static BOOL PHObservedEndpointConfigured(void) {
    NSString *host = PHObservedEndpoint[@"host"], *path = PHObservedEndpoint[@"path"];
    return [host isKindOfClass:NSString.class] && host.length > 0 &&
           [path isKindOfClass:NSString.class] && [path hasPrefix:@"/"] &&
           [PHConfig[@"apiHosts"] containsObject:host.lowercaseString] &&
           [PHConfig[@"apiPaths"] containsObject:path];
}

static PHFeatureMask PHConfiguredMask(void) {
    // A temporarily unreadable config suspends writes in PHRequestedMask;
    // keep the last valid intent so deferred login restores can still retry.
    PHFeatureMask mask = 0;
    if (PHEnabled(@"skin")) mask |= PHFeatureSkin;
    if (PHEnabled(@"bgm")) mask |= PHFeatureBGM;
    if (PHEnabled(@"badge")) mask |= PHFeatureBadge;
    return mask;
}

static PHFeatureMask PHRequestedMask(void) {
    if (!PHValidated || !PHFrameInstalled || !PHNetworkInstalled || !PHFrameMainThread ||
        atomic_load_explicit(&PHFrameOffMain, memory_order_relaxed) ||
        !PHAccountReady || !PHStorageReady || !PHConfigValid || PHStorageFault || !PHFiltersConfigured() ||
        (PHObservedEndpoint && !PHObservedEndpointConfigured())) return 0;
    uint32_t player = 0;
    if (!PHReadU32(PHSave() + PHPlayerIDOffset, &player) || player == 0 || player != PHPlayer) return 0;
    return PHConfiguredMask();
}

static PHFeatureMask PHActiveMask(void) {
    PHFeatureMask mask = PHRequestedMask();
    if (!PHBadgeTracker.ready) mask &= ~PHFeatureBadge;
    return mask;
}

static NSDictionary *PHLoadAccount(uint32_t player) {
    NSDictionary *store = PHReadJSON([NSString stringWithFormat:@"account-%u.json", player]);
    return PHDeckAccountBanks(store, player);
}

static BOOL PHStoreBanks(NSDictionary *banks) {
    NSDictionary *store = @{@"schema": @1, @"target": @"jp.gungho.pad-ios-23.6.0-106878",
                            @"player": @(PHPlayer), @"banks": banks};
    return PHWriteJSON(store, [NSString stringWithFormat:@"account-%u.json", PHPlayer]);
}

static void PHConfigureAutomaticTeam(NSDictionary *team) {
    PHBattleLoadout loadouts[6] = {0};
    uint8_t mask = 0;
    if (PHFormationReadTeam(team, loadouts, NULL)) {
        for (NSUInteger i = 0; i < 6; ++i) if (loadouts[i].bodyNumber) mask |= (uint8_t)(1u << i);
    }
    [PHBattleTracker configureAutomaticLoadouts:loadouts mask:mask];
}

static void PHPublishFormationLibrary(NSDictionary *library) {
    NSDictionary *selected = nil;
    if (!PHFormationReadLibrary(library, &selected, NULL)) return;
    const BOOL changed = ![PHFormationLibrary isEqual:library];
    PHFormationLibrary = library; PHFormationDraft = selected; PHFormationLibraryStored = YES;
    if (changed) PHConfigureAutomaticTeam(selected);
}

static void PHReloadConfig(void) {
    NSDictionary *next = PHReadJSON(@"config.json");
    PHConfigValid = PHConfigurationIsValid(next);
    PHConfig = PHConfigValid ? next : PHConfig ?: PHDefaultConfiguration();
    if (PHConfigValid && next[@"formations"]) PHPublishFormationLibrary(next[@"formations"]);
    [PHOrbTracker configureProfiles:PHConfigurationOrbProfiles(PHConfig)];
    [PHOrbTracker configureSettings:PHConfigurationOrbSettings(PHConfig)];
}

static BOOL PHSaveFormationLibrary(NSDictionary *library) {
    NSDictionary *selected = nil;
    NSString *reason = nil;
    if (!PHFormationReadLibrary(library, &selected, &reason)) { PHFeedback = reason; return NO; }
    NSMutableDictionary *next = [PHConfig mutableCopy]; next[@"formations"] = library;
    if (!PHStorageReady || !PHConfigurationIsValid(next) || !PHWriteJSON(next, @"config.json")) {
        PHFeedback = @"儲存失敗；草稿仍在編輯器，下次入場隊伍未變更。請檢查可用空間後重試。";
        return NO;
    }
    // Publish the selection only after the canonical file was written. A
    // failed save must not silently change the next automatic entry team.
    PHConfig = [next copy]; PHConfigValid = YES;
    PHPublishFormationLibrary(library);
    // Legacy files are backups. Config is the single commit point for the
    // library and profiles, so a partial backup cannot split a plan import.
    PHWriteJSON(library, @"formations.json");
    PHWriteJSON(selected, @"formation.json");
    PHFeedback = @"隊伍庫與目前選取隊伍已儲存";
    return YES;
}

static void PHPrepareStorage(void) {
    PHFolder = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/PADHelper"];
    NSError *error = nil;
    PHStorageReady = [NSFileManager.defaultManager createDirectoryAtPath:PHFolder withIntermediateDirectories:YES
                     attributes:@{NSFilePosixPermissions: @0700} error:&error];
    PHStorageMessage = PHStorageReady ? @"" : [NSString stringWithFormat:@"無法建立設定目錄：%@", error.localizedDescription ?: @"未知錯誤"];
    NSString *configPath = [PHFolder stringByAppendingPathComponent:@"config.json"];
    if (PHStorageReady && ![NSFileManager.defaultManager fileExistsAtPath:configPath] &&
        !PHWriteJSON(PHDefaultConfiguration(), @"config.json")) {
        PHStorageReady = NO;
        PHStorageMessage = @"無法寫入 config.json；面板仍可顯示診斷。";
    }
    PHReloadConfig();
    NSDictionary *library = PHConfig[@"formations"] ?: PHReadJSON(@"formations.json"), *selected = nil;
    PHFormationLibraryStored = NO;
    if (PHFormationReadLibrary(library, &selected, NULL)) {
        PHFormationLibrary = library; PHFormationDraft = selected; PHFormationLibraryStored = YES;
    } else if (!PHFormationLibrary) {
        PHFormationLibrary = PHFormationLibraryFromTeam(PHReadJSON(@"formation.json"));
        PHFormationReadLibrary(PHFormationLibrary, &selected, NULL);
        PHFormationDraft = selected;
        NSString *libraryPath = [PHFolder stringByAppendingPathComponent:@"formations.json"];
        if (PHStorageReady && ![NSFileManager.defaultManager fileExistsAtPath:libraryPath])
            PHFormationLibraryStored = PHWriteJSON(PHFormationLibrary, @"formations.json");
    }
    PHConfigureAutomaticTeam(PHFormationDraft);
    if (!PHObservedEndpoint) {
        NSDictionary *endpoint = PHReadJSON(@"endpoint.json");
        if ([endpoint[@"host"] isKindOfClass:NSString.class] &&
            [endpoint[@"path"] isKindOfClass:NSString.class]) PHObservedEndpoint = endpoint;
    }
}

static BOOL PHCurrentPlayer(uint32_t *player) {
    return PHReadU32(PHSave() + PHPlayerIDOffset, player) && *player != 0;
}

static PHBattleFrame PHBattleFrameContext(void) {
    uint32_t player = 0;
    BOOL ready = PHValidated && PHFrameInstalled && PHFrameMainThread && PHAccountReady &&
        !atomic_load_explicit(&PHFrameOffMain, memory_order_relaxed) &&
        PHCurrentPlayer(&player) && player == PHPlayer;
    return (PHBattleFrame){.header = (uintptr_t)PHGuest, .player = PHPlayer, .epoch = PHAccountEpoch, .ready = ready,
        .enabled = PHStorageReady && PHConfigValid && !PHStorageFault && PHEnabled(@"battleSwap"),
        .policy = PHConfigurationBattlePolicy(PHConfig),
        .autoApplyTeam = [PHConfigurationBattleFilters(PHConfig)[@"autoApplyTeam"] boolValue]};
}

static PHBattleFrame PHTimerFrameContext(void) {
    PHBattleFrame frame = PHBattleFrameContext();
    frame.enabled = PHStorageReady && PHConfigValid && !PHStorageFault &&
        PHLifecycleInstalled && PHEnabled(@"timerFix");
    return frame;
}

static PHBattleFrame PHOrbFrameContext(void) {
    PHBattleFrame frame = PHBattleFrameContext();
    frame.ready = frame.ready && UIApplication.sharedApplication.applicationState == UIApplicationStateActive;
    frame.enabled = PHStorageReady && PHConfigValid && !PHStorageFault &&
        [PHConfig[@"experimentalWrites"] isEqual:@YES];
    return frame;
}

static void PHResetAccount(uint32_t player, BOOL loginSuccess) {
    if (player != PHPlayer || loginSuccess) {
        PHPlayer = player;
        PHAccountEpoch++;
        PHAccountReady = loginSuccess && player != 0;
        PHStorageFault = NO;
        PHLastBanks = PHAccountReady ? PHLoadAccount(player) : @{};
        [PHSaveStateTracker resetWithBanks:PHLastBanks accountReady:PHAccountReady];
        [PHDeckRestoreTracker cancel];
        // Login replaced ownership words. Old bit snapshots must not be restored.
        // Descriptor flags belong to the resource table, not the account. Keep
        // those leases only while table, ID and our exact flag value still match.
        memset(PHSkinAdded, 0, sizeof(PHSkinAdded));
        memset(PHBGMAdded, 0, sizeof(PHBGMAdded));
        [PHBadgeTracker reset];
        PHProbeLogged = NO;
        PHLog(loginSuccess ? @"Native login parse observed; account epoch reset" :
                              @"Account changed; waiting for a successful native login parse");
    }
}

static void PHDrainDeckRestore(void) {
    NSDictionary *result = [PHDeckRestoreTracker drainHeader:(uintptr_t)PHGuest epoch:PHAccountEpoch
        enabled:PHConfiguredMask() allowed:PHRequestedMask() badgeReady:PHBadgeTracker.ready
        canUseBadge:^BOOL(uint8_t badge) { return [PHBadgeTracker canUse:badge]; }];
    if (result) [PHSaveStateTracker recordRestoreCount:[result[@"restored"] unsignedIntegerValue]
        reason:result[@"message"]];
}

static void PHQueueDeckRestore(NSDictionary *banks, PHFeatureMask mask, NSString *source) {
    [PHDeckRestoreTracker enqueue:banks mask:mask epoch:PHAccountEpoch source:source];
    [PHSaveStateTracker recordRestoreCount:0 reason:[PHDeckRestoreTracker snapshot][@"message"]];
    PHDrainDeckRestore();
}

static void PHRevertDisabled(PHFeatureMask mask) {
    uint8_t *added[] = {PHSkinAdded, PHBGMAdded};
    uintptr_t offsets[] = {PHSkinOwnedOffset, PHBGMOwnedOffset};
    PHFeatureMask features[] = {PHFeatureSkin, PHFeatureBGM};
    for (NSUInteger i = 0; i < 2; i++) {
        if (!(mask & features[i])) {
            if (!PHRestoreOwnershipBits(PHSave() + offsets[i],added[i])) PHOwnershipWriteFault = YES;
        }
    }
    uintptr_t table = 0;
    uint32_t count = 0;
    BOOL tableValid = PHReadPointer(PHSave() + PHEntryPointerOffset, &table) && table &&
                      PHReadU32(PHSave() + PHEntryCountOffset, &count) && count > 0 && count <= 2048;
    PHFlagPatches = [PHRestoreDescriptorFlags(PHFlagPatches,table,count,tableValid,mask,&PHOwnershipWriteFault) mutableCopy];
}

static void PHApplyUnlocks(PHFeatureMask mask) {
    PHUnlockDataReady = NO;
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
    PHUnlockDataReady = YES;
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
            } else PHOwnershipWriteFault = YES;
        }
    }
    for (NSUInteger bank = 0; bank < 2; bank++) {
        if (memcmp(original[bank], bitmaps[bank], 128) != 0) {
            if (PHWriteData(PHSave() + offsets[bank], bitmaps[bank], 128)) {
                for (NSUInteger j = 0; j < 128; j++) added[bank][j] |= bitmaps[bank][j] & ~original[bank][j];
            } else PHOwnershipWriteFault = YES;
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
        (value.requestID != 33 && value.requestID != PHSaveDecksRequestID)) return value;
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
        !PHReadU32(currentContext + PHAPIErrorOffset, &nativeError)) return;
    if (before.requestID == 33) {
        uint32_t player = 0;
        if (nativeError == 0 && PHCurrentPlayer(&player)) {
            PHResetAccount(player, YES);
            [PHBadgeTracker updateHeader:(uintptr_t)PHGuest enabled:(PHRequestedMask() & PHFeatureBadge) != 0];
            PHQueueDeckRestore(PHLastBanks, PHConfiguredMask(), @"登入後");
        }
        return;
    }
    NSDictionary *transaction = [PHSaveStateTracker consumeForEpoch:PHAccountEpoch
        context:before.context request:before.requestBlock nativeError:nativeError];
    if (transaction) PHQueueDeckRestore(transaction[@"banks"],
                                   PHConfiguredMask() & [transaction[@"mask"] unsignedIntegerValue], @"儲存後");
}

static void PHMaintenance(void) {
    uint32_t player = 0;
    if (PHCurrentPlayer(&player) && player != PHPlayer) PHResetAccount(player, NO);
    PHReloadConfig();
    PHFeatureMask mask = PHRequestedMask();
    PHOwnershipWriteFault = NO;
    PHRevertDisabled(mask);
    PHApplyUnlocks(mask & (PHFeatureSkin | PHFeatureBGM));
    [PHBadgeTracker updateHeader:(uintptr_t)PHGuest enabled:(mask & PHFeatureBadge) != 0];
    PHDrainDeckRestore();
    [PHSaveStateTracker expireAtTime:CFAbsoluteTimeGetCurrent()];
    uint32_t count = 0;
    uintptr_t pointer = 0;
    PHReadU32(PHSave() + PHEntryCountOffset, &count);
    PHReadPointer(PHSave() + PHEntryPointerOffset, &pointer);
    PHObservedEntries = count <= 2048 ? count : 0;
    if (!PHProbeLogged && count > 0 && count <= 2048 && pointer) {
        PHLog([NSString stringWithFormat:@"Read-only entry probe: %u entries, pointer range %@",
               count, PHMemoryHasProtection(pointer, count * 52u, VM_PROT_READ, 0) ? @"valid" : @"invalid"]);
        PHProbeLogged = YES;
    }
}

static void PHDrawFrame(id self, SEL command) {
    BOOL onMain = NSThread.isMainThread;
    if (!onMain) {
        atomic_store_explicit(&PHFrameOffMain, true, memory_order_relaxed);
        PHOriginalDraw(self, command);
        return;
    }
    [PHOrbTracker finishNativeFrame];
    if (!PHValidated) {
        PHOriginalDraw(self, command);
        return;
    }
    PHFrameMainThread = YES;
    [PHTimerTracker onFrame:PHTimerFrameContext() atTime:CFAbsoluteTimeGetCurrent() force:NO];
    PHParseObservation before = PHBeforeFrame();
    [PHOrbTracker beforeFrame:PHOrbFrameContext() time:NSProcessInfo.processInfo.systemUptime];
    @try {
        PHOriginalDraw(self, command);
    } @finally {
        [PHOrbTracker finishNativeFrame];
    }
    PHFrameCount++;
    PHAfterParse(before);
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (now - PHLastMaintenance >= 1.0) {
        PHLastMaintenance = now;
        PHMaintenance();
    }
    [PHBattleTracker onFrame:PHBattleFrameContext() atTime:now];
    PHBattleFrame musicFrame = PHOrbFrameContext();
    musicFrame.enabled = musicFrame.enabled && PHEnabled(@"bgmUncleared") && (PHActiveMask() & PHFeatureBGM);
    [PHMusicTracker onFrame:musicFrame time:NSProcessInfo.processInfo.systemUptime];
    [PHOrbTracker afterFrame:PHOrbFrameContext() time:NSProcessInfo.processInfo.systemUptime];
}

static void PHRestoreBackground(id self, SEL command) {
    if (NSThread.isMainThread) [PHOrbTracker stopForLifecycle:@"遊戲從背景返回；先前的轉珠路徑已停止"];
    PHOriginalBackground(self, command);
    if (NSThread.isMainThread && PHValidated)
        [PHTimerTracker onFrame:PHTimerFrameContext() atTime:CFAbsoluteTimeGetCurrent() force:YES];
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
    if (!NSThread.isMainThread || !PHValidated || ![request.HTTPMethod isEqualToString:@"POST"] ||
        !PHQueryMatches(request.URL, @"action", @"save_decks") || !request.HTTPBody) {
        return PHOriginalDataTask(self, command, request, completion);
    }
    // A new save supersedes any older restore, including an explicit default,
    // disabled-feature request or snapshot write failure. Never resurrect an
    // older selection after the user has submitted a newer one.
    BOOL hasPending = [PHSaveStateTracker beginSave];
    [PHDeckRestoreTracker cancel];
    PHRewriteResult *probe = PHRewriteDeckBody(request.HTTPBody, 0, NULL);
    if (!probe) {
        [PHSaveStateTracker recordPassthrough:@"隊伍格式未通過校驗；請求保持原樣"];
        return PHOriginalDataTask(self, command, request, completion);
    }
    static NSMutableSet *seen;
    if (!seen) seen = [NSMutableSet set];
    NSString *endpoint = [NSString stringWithFormat:@"%@%@", request.URL.host, request.URL.path];
    if (![seen containsObject:endpoint]) {
        [seen addObject:endpoint];
        PHLog([NSString stringWithFormat:@"PAD API candidate host=%@ path=%@ (query omitted)",
                request.URL.host, request.URL.path]);
    }
    NSDictionary *candidate = @{@"schema": @1, @"host": request.URL.host.lowercaseString ?: @"",
                               @"path": request.URL.path ?: @"", @"queryIncluded": @NO};
    if (![PHObservedEndpoint isEqual:candidate]) {
        PHObservedEndpoint = candidate;
        PHWriteJSON(candidate, @"endpoint.json");
    }
    PHFeatureMask mask = PHActiveMask();
    uint32_t player = 0;
    if (!mask || !completion || !PHCurrentPlayer(&player) || player != PHPlayer ||
        ![PHConfig[@"apiHosts"] containsObject:request.URL.host.lowercaseString] ||
        ![PHConfig[@"apiPaths"] containsObject:request.URL.path] ||
        !PHQueryMatches(request.URL, @"pid", [NSString stringWithFormat:@"%u", player])) {
        [PHSaveStateTracker recordPassthrough:@"功能未啟用或請求未匹配設定；請求保持原樣"];
        return PHOriginalDataTask(self, command, request, completion);
    }
    PHRewriteResult *result = PHRewriteDeckBody(request.HTTPBody, mask, NULL);
    if (!result) {
        [PHSaveStateTracker recordPassthrough:@"隊伍格式未通過校驗；請求保持原樣"];
        return PHOriginalDataTask(self, command, request, completion);
    }
    PHDeckRequest native = {0};
    NSDictionary *nativeBanks = PHReadRequestedDecks((uintptr_t)PHGuest, result.banks, &native);
    if (mask & PHFeatureBadge) {
        BOOL badgesValid = nativeBanks != nil;
        for (NSArray *records in nativeBanks.allValues)
            for (NSArray *record in records)
                if (![PHBadgeTracker canUse:[record[5] unsignedCharValue]]) badgesValid = NO;
        if (!badgesValid) {
            // A wire zero does not distinguish a user default from a rejected
            // badge. Preserve the previous selection unless this exact native
            // request and the current badge table both validate it.
            mask &= ~PHFeatureBadge;
            result = PHRewriteDeckBody(request.HTTPBody, mask, NULL);
        }
    }
    BOOL recoveredBadge = NO;
    if (mask & PHFeatureBadge) for (NSString *key in nativeBanks) {
        NSArray *records = nativeBanks[key], *wire = result.banks[key];
        for (NSUInteger i = 0; i < records.count; ++i)
            recoveredBadge |= ![records[i][5] isEqual:wire[i][5]];
    }
    NSDictionary *banks = PHMergeBanks(PHLastBanks ?: @{}, (mask & PHFeatureBadge) ? nativeBanks : result.banks, mask);
    if (!PHStoreBanks(banks)) {
        PHStorageFault = YES;
        [PHSaveStateTracker recordStorageFailure];
        PHLog(@"Local snapshot write failed; feature writes disabled");
        return PHOriginalDataTask(self, command, request, completion);
    }
    PHLastBanks = banks;
    [PHSaveStateTracker recordLocalBanks:banks];
    if (!result.changed && !recoveredBadge) {
        [PHSaveStateTracker recordPassthrough:@"本地選擇已保存；此請求無需預設化"];
        return PHOriginalDataTask(self, command, request, completion);
    }
    NSURLRequest *outgoing = request;
    if (result.changed) {
        NSMutableURLRequest *copy = [request mutableCopy]; copy.HTTPBody = result.body;
        [copy setValue:[NSString stringWithFormat:@"%lu", (unsigned long)result.body.length]
            forHTTPHeaderField:@"Content-Length"];
        outgoing = copy;
    }
    BOOL nativeMatch = nativeBanks != nil;
    NSDictionary *transaction = [PHSaveStateTracker prepareTransactionWithBanks:banks mask:mask
        epoch:PHAccountEpoch context:native.context request:native.request
        restoreAllowed:nativeMatch && !hasPending time:CFAbsoluteTimeGetCurrent()];
    NSURLSessionDataTask *task = PHOriginalDataTask(self, command, outgoing,
                ^(NSData *data, NSURLResponse *response, NSError *error) {
        id object = (!error && data.length > 0 && data.length <= 4 * 1024 * 1024) ?
             [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
        BOOL success = !error && [response isKindOfClass:NSHTTPURLResponse.class] &&
                       [(NSHTTPURLResponse *)response statusCode] == 200 &&
                       [object isKindOfClass:NSDictionary.class] &&
                       [object[@"res"] isKindOfClass:NSNumber.class] &&
                       CFGetTypeID((__bridge CFTypeRef)object[@"res"]) != CFBooleanGetTypeID() &&
                       [object[@"res"] doubleValue] == 0;
        NSInteger status = [response isKindOfClass:NSHTTPURLResponse.class] ? [(NSHTTPURLResponse *)response statusCode] : 0;
        [PHSaveStateTracker completeTransaction:transaction accepted:success httpStatus:status];
        completion(data, response, error);
    });
    [PHSaveStateTracker recordTaskCreated:task != nil transaction:transaction];
    if (task) PHLog(result.changed ? @"Submitted copied save_decks body with selected fields defaulted" :
        @"Tracked native save_decks selection already defaulted by serializer");
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

static NSString *PHUnlockBlockReason(void) {
    if (!PHValidated) return PHValidationMessage ?: @"等待遊戲校驗";
    if (!PHStorageReady) return PHStorageMessage ?: @"設定目錄無法寫入";
    if (!PHConfigValid) return @"config.json 格式無效；切換開關可重新儲存設定。";
    if (PHStorageFault) return @"本地選擇寫入失敗；請按「重新檢查」。";
    if (!PHFrameInstalled) return @"drawFrame hook 未安裝；請按「重新檢查」。";
    if (!PHNetworkInstalled) return @"網路 hook 未安裝；請按「重新檢查」。";
    if (!PHFrameMainThread) return atomic_load_explicit(&PHFrameOffMain, memory_order_relaxed) ?
                                  @"drawFrame 在背景執行緒執行，Skin／BGM 已停用。" : @"等待第一個遊戲畫面更新";
    if (!PHAccountReady) return @"等待原生登入成功；若已登入，請完整關閉遊戲再啟動。";
    if (!PHFiltersConfigured()) return @"請先儲存一次隊伍，再按「套用偵測到的 API」。";
    if (PHObservedEndpoint && !PHObservedEndpointConfigured()) return @"API 設定與偵測地址不符；請重新套用。";
    uint32_t player = 0;
    if (!PHCurrentPlayer(&player) || player != PHPlayer) return @"玩家資料未就緒，等待重新登入";
    return nil;
}

static NSDictionary *PHRuntimeSnapshot(void) {
    if (PHFolder.length) PHReloadConfig();
    PHFeatureMask mask = PHActiveMask();
    BOOL master = [PHConfig[@"experimentalWrites"] isEqual:@YES];
    BOOL wantsSkin = PHEnabled(@"skin"), wantsBGM = PHEnabled(@"bgm"), wantsBadge = PHEnabled(@"badge");
    NSDictionary *badge = [PHBadgeTracker snapshot];
    NSDictionary *timer = [PHTimerTracker snapshot];
    NSDictionary *music = [PHMusicTracker snapshot];
    NSMutableDictionary *orbs = [[PHOrbTracker snapshot] mutableCopy];
    orbs[@"canSetQuality"] = @(PHStorageReady && ![orbs[@"busy"] boolValue] && ![orbs[@"inputFault"] boolValue]);
    orbs[@"qualityMessage"] = PHOrbQualityNotice ?: @"";
    orbs[@"canSetThornPolicy"] = orbs[@"canSetQuality"];
    orbs[@"thornPolicyMessage"] = PHOrbThornNotice ?: @"";
    orbs[@"canSetGoals"] = orbs[@"canSetQuality"];
    orbs[@"goalMessage"] = PHOrbGoalNotice ?: @"";
    orbs[@"canSetRules"] = orbs[@"canSetQuality"];
    orbs[@"ruleMessage"] = PHOrbRuleNotice ?: @"";
    orbs[@"canSetRecovery"] = orbs[@"canSetQuality"];
    orbs[@"recoveryMessage"] = PHOrbRecoveryNotice ?: @"";
    orbs[@"profiles"] = PHConfigurationOrbProfiles(PHConfig);
    orbs[@"canSetProfiles"] = orbs[@"canSetQuality"];
    orbs[@"profileMessage"] = PHOrbProfileNotice ?: @"";
    NSUInteger timerClears = [timer[@"pauseClearCount"] unsignedIntegerValue];
    NSString *timerSummary = [NSString stringWithFormat:@"%@\n%@\n修正暫停 %@ 次 · 手動撤窗 %@ 次",
        timer[@"gate"], timer[@"message"], timer[@"pauseClearCount"], timer[@"dismissCount"]];
    NSMutableDictionary *battle = [[PHBattleTracker snapshot] mutableCopy];
    if ([battle[@"restorePending"] boolValue] || [battle[@"pendingMask"] unsignedIntegerValue]) {
        orbs[@"canOnce"] = @NO; orbs[@"canStart"] = @NO; orbs[@"canExecute"] = @NO;
    }
    battle[@"automaticConfigured"] = @([PHConfigurationBattleFilters(PHConfig)[@"autoApplyTeam"] boolValue]);
    battle[@"automaticSwitchesReady"] = @(PHEnabled(@"battleSwap"));
    battle[@"automaticTeamID"] = PHFormationLibrary[@"selected"] ?: @"";
    battle[@"automaticTeamTitle"] = PHFormationDraft[@"title"] ?: @"未命名隊伍";
    BOOL battleActive = PHEnabled(@"battleSwap") && ([battle[@"canApply"] boolValue] || [battle[@"applied"] boolValue]);
    BOOL timerActive = PHEnabled(@"timerFix") && [timer[@"active"] boolValue];
    BOOL cosmeticActive = (mask & (PHFeatureSkin | PHFeatureBGM)) && PHUnlockDataReady && !PHOwnershipWriteFault;
    BOOL badgeActive = (mask & PHFeatureBadge) && PHBadgeTracker.ready;
    BOOL unlockActive = cosmeticActive || badgeActive;
    NSString *blocked = PHUnlockBlockReason();
    NSString *tone = @"pending", *title = @"已載入 · 初始化中", *reason = PHValidationMessage ?: @"正在檢查遊戲";
    if ([PHStartupState isEqualToString:@"checking"]) {
        title = @"已載入 · 正在檢查遊戲";
    } else if (!PHValidated) {
        tone = @"error";
        title = @"已載入 · 遊戲校驗未通過";
    } else if (!PHStorageReady || !PHConfigValid || PHStorageFault) {
        tone = @"error";
        title = @"已載入 · 設定或儲存異常";
        reason = blocked;
    } else if (!PHFrameInstalled || !PHNetworkInstalled) {
        tone = @"error";
        title = @"已載入 · hooks 未完整安裝";
        reason = blocked;
    } else if ([battle[@"fault"] boolValue] || [timer[@"fault"] boolValue]) {
        tone = @"error";
        title = @"已載入 · 原生操作已停止";
        reason = [battle[@"fault"] boolValue] ? battle[@"message"] : timer[@"message"];
    } else if (!master) {
        tone = @"ready";
        title = @"已載入 · 診斷模式";
        reason = @"開啟總開關及要使用的功能；設定會自動儲存。";
    } else if (PHOwnershipWriteFault || (wantsBadge && PHBadgeTracker.writeFault)) {
        tone = @"error";
        title = @"已載入 · 遊戲資料寫入失敗";
        reason = @"解鎖資料未完整套用，請複製診斷。";
    } else if (unlockActive || timerActive || battleActive) {
        BOOL cosmeticWaiting = (wantsSkin || wantsBGM) && !cosmeticActive;
        BOOL badgeWaiting = wantsBadge && !badgeActive;
        BOOL timerWaiting = PHEnabled(@"timerFix") && !timerActive;
        BOOL partial = cosmeticWaiting || badgeWaiting || timerWaiting || (PHEnabled(@"battleSwap") && !battleActive);
        tone = @"active";
        title = partial ? @"已載入 · 部分功能啟用" : @"已載入 · 功能已啟用";
        reason = partial ? (blocked ?: (badgeWaiting ? badge[@"message"] : cosmeticWaiting ? @"Skin／BGM 等待遊戲資源表初始化" :
                                       timerWaiting ? timer[@"gate"] : battle[@"gate"])) :
                           @"開關已套用；可在下方查看上傳、回蓋及計時修正次數。";
    } else if (wantsSkin || wantsBGM || wantsBadge) {
        title = @"已載入 · 等待啟用條件";
        reason = blocked ?: (wantsBadge ? badge[@"message"] : @"等待 Skin／BGM 資源表初始化");
    } else if (PHEnabled(@"timerFix") && !PHLifecycleInstalled) {
        tone = @"error";
        title = @"已載入 · 背景 hook 未安裝";
        reason = @"計時修正無法啟用，請按「重新檢查」。";
    } else if (PHEnabled(@"timerFix")) {
        title = @"已載入 · 等待計時關卡";
        reason = timer[@"gate"];
    } else if (PHEnabled(@"battleSwap")) {
        title = @"已載入 · 等待戰鬥替換條件";
        reason = battle[@"gate"] ?: @"等待戰鬥檢查";
    } else {
        tone = @"ready";
        title = @"已載入 · 尚未選擇功能";
        reason = @"請開啟要使用的功能。";
    }
    NSMutableDictionary *featureStates = [NSMutableDictionary dictionary];
    featureStates[@"bgmUncleared"] = !PHEnabled(@"bgmUncleared") ? @"關閉" :
        !PHEnabled(@"bgm") ? @"請先啟用 BGM" : music[@"message"] ?: @"等待單人未通關關卡";
    for (NSString *key in @[@"skin", @"bgm"]) {
        PHFeatureMask feature = [key isEqualToString:@"skin"] ? PHFeatureSkin : PHFeatureBGM;
        BOOL selected = [PHConfig[@"features"][key] isEqual:@YES];
        featureStates[key] = !selected ? @"關閉" : !master ? @"等待總開關" :
                             !(mask & feature) ? (blocked ?: @"等待啟用條件") :
                             PHOwnershipWriteFault ? @"資料寫入失敗" :
                             !PHUnlockDataReady ? @"等待遊戲資源表" : @"已套用";
    }
    featureStates[@"timerFix"] = ![PHConfig[@"features"][@"timerFix"] isEqual:@YES] ? @"關閉" : !master ? @"等待總開關" :
                                 !PHValidated ? @"等待遊戲校驗" : !PHLifecycleInstalled ? @"背景 hook 未安裝" :
                                 !PHStorageReady || !PHConfigValid ? @"等待可用設定" :
                                 timer[@"gate"];
    featureStates[@"badge"] = ![PHConfig[@"features"][@"badge"] isEqual:@YES] ? @"關閉" : !master ? @"等待總開關" :
        (blocked ?: badge[@"message"]);
    featureStates[@"battleSwap"] = ![PHConfig[@"features"][@"battleSwap"] isEqual:@YES] ? @"關閉" : !master ? @"等待總開關" :
        [battle[@"restorePending"] boolValue] ? @"等待還原" : [battle[@"applied"] boolValue] ?
        [NSString stringWithFormat:@"已替換 %@ 名隊員", battle[@"activeCount"]] :
        !PHValidated ? @"等待遊戲校驗" : battle[@"gate"] ?: @"等待戰鬥檢查";
    featureStates[@"autoApplyTeam"] = ![PHConfigurationBattleFilters(PHConfig)[@"autoApplyTeam"] boolValue] ? @"關閉" :
        !master ? @"等待總開關" : !PHEnabled(@"battleSwap") ? @"等待開啟戰鬥隊員替換" :
        [NSString stringWithFormat:@"%@ · %@", PHFormationDraft[@"title"] ?: @"未命名隊伍",
            battle[@"automaticMessage"] ?: @"下次入場自動套用，重算 SB"];
    NSMutableArray<NSString *> *battleLines = [NSMutableArray arrayWithObjects:battle[@"message"] ?: @"尚未替換隊員",
        battle[@"gate"] ?: @"等待戰鬥檢查", nil];
    for (NSDictionary *member in battle[@"members"]) {
        [battleLines addObject:![member[@"active"] boolValue] ? [NSString stringWithFormat:@"位置 %@：空", member[@"slot"]] :
            [NSString stringWithFormat:@"位置 %@：#%@ · 武裝 #%@ · Lv.%@ · 充能 %@%@", member[@"slot"], member[@"current"],
                member[@"assist"], member[@"level"], member[@"charge"], [member[@"temporary"] boolValue] ? @" · 暫時變身" : @""]];
    }
    for (NSDictionary *failure in battle[@"restoreFailures"]) {
        [battleLines addObject:[NSString stringWithFormat:@"位置 %@ 還原失敗：%@", failure[@"slot"], failure[@"reason"]]];
    }
    [battleLines addObject:[NSString stringWithFormat:@"替換 %@ 次 · 還原 %@ 次 · 回復 %@ 次", battle[@"applyCount"], battle[@"restoreCount"], battle[@"rollbackCount"]]];
    NSString *battleSummary = [battleLines componentsJoinedByString:@"\n"];
    NSString *frameState = PHFrameMainThread ? @"主執行緒" :
                          atomic_load_explicit(&PHFrameOffMain, memory_order_relaxed) ? @"非主執行緒" : @"等待 frame";
    NSDictionary *saveState = [PHSaveStateTracker snapshot];
    NSString *saveSummary = [NSString stringWithFormat:
        @"儲存：%@\n本機保存：%@ 次 · 主要 %@ 隊／次要 %@ 隊\n伺服器接受：%@ 次 · 失敗：%@ 次\n%@",
        saveState[@"message"], saveState[@"localSaveCount"], saveState[@"primaryDeckCount"], saveState[@"secondaryDeckCount"],
        saveState[@"acceptedCount"], saveState[@"failedCount"], saveState[@"restoreMessage"]];
    NSString *checks = [NSString stringWithFormat:
        @"版本校驗：%@\nHooks：frame %@ · network %@ · background %@\nFrame：%@ · %lu 次\n登入：%@ · API：%@\n資源條目：%u\n上傳預設化：%lu 次 · 本地回蓋：%lu 欄位\n計時修正：%lu 次",
        PHValidated ? @"通過" : @"未通過", PHFrameInstalled ? @"✓" : @"×", PHNetworkInstalled ? @"✓" : @"×",
        PHLifecycleInstalled ? @"✓" : @"×", frameState, (unsigned long)PHFrameCount,
        PHAccountReady ? @"已觀察成功" : @"等待", PHFiltersConfigured() ? @"已設定" : @"未設定", PHObservedEntries,
        [saveState[@"submittedCount"] unsignedLongValue], [saveState[@"restoredFieldCount"] unsignedLongValue], (unsigned long)timerClears];
    NSString *path = PHFolder.length ? [PHFolder stringByAppendingPathComponent:@"config.json"] : @"尚未建立";
    NSBundle *bundle = NSBundle.mainBundle;
    NSString *diagnostics = [NSString stringWithFormat:
        @"PADCheaper %@\nLoaded: YES\nOverlay visible: %@\nBundle ID: %@\nOriginal ID: %@\nGame version: %@ / %@\nTarget UUID: %@\nValidation: %@\nStatus: %@\nReason: %@\n%@\n%@\nBattle: %@\nBattle phase: %@ / mode %@ / source %@ / multi %@\nBattle audit: %@ / digest %@ / HP %@\nEndpoint: %@%@ (query omitted)\nConfig: %@\nStorage: %@\nDevice verified by CI: NO",
        PH_RUNTIME_VERSION, PHOverlayVisible ? @"YES" : @"NO", bundle.bundleIdentifier ?: @"未取得", PHGuestBundleIdentifier(),
        bundle.infoDictionary[@"CFBundleShortVersionString"] ?: @"未取得", bundle.infoDictionary[@"CFBundleVersion"] ?: @"未取得",
        @(PHTargetUUIDString), PHValidationMessage ?: @"初始化中", title, reason ?: @"", checks, saveSummary,
        battleSummary, battle[@"battlePhase"], battle[@"mode"], battle[@"sourceMode"], battle[@"multiplayer"],
        battle[@"auditFlags"], battle[@"digestMatches"], battle[@"hpMatches"],
        PHObservedEndpoint[@"host"] ?: @"", PHObservedEndpoint[@"path"] ?: @"", path,
        PHStorageMessage.length ? PHStorageMessage : PHStorageReady ? @"ready" : @"unavailable"];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nDeck restore: %@", [PHDeckRestoreTracker snapshot]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nBGM playback: %@", music];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nBattle filters: %@\nBadge: %@\nTimer: %@",
        PHConfigurationBattleFilters(PHConfig), badge[@"message"], timerSummary];
    diagnostics = [diagnostics stringByAppendingFormat:
        @"\nAutomatic entry: enabled %@ / waiting %@ / count %@ / last SB %@\nEntry window: %@ / charges valid %@\nEntry result: %@\nTeam banks: valid %@ / current %@ / count %@ / offscreen leases %@",
        battle[@"automaticEnabled"], battle[@"automaticWaiting"], battle[@"automaticCount"], battle[@"initialBoost"],
        battle[@"entryWindow"], battle[@"entryChargesValid"], battle[@"automaticMessage"],
        battle[@"teamBanksValid"], battle[@"currentTeam"], battle[@"teamCount"], battle[@"offscreenCount"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nThree-player turn: valid %@ / player %@ / owner %@ / serial %@",
        battle[@"turnInfoValid"], battle[@"playerTeam"], battle[@"turnTeam"], battle[@"turnSerial"]];
    diagnostics = [diagnostics stringByAppendingFormat:
        @"\nCurrent entry: applied %@ / lost mask 0x%02x / rebind waiting %@ / rebound members %@\nLast binding loss: phase %@ / serial %@",
        battle[@"entryApplied"], [battle[@"entryLostMask"] unsignedIntValue], battle[@"rebindPendingCount"],
        battle[@"entryRebindCount"], battle[@"lastLossPhase"], battle[@"lastLossSerial"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nBattle writes: single-player only %@ / multiplayer disabled %@",
        battle[@"singlePlayerOnly"], battle[@"multiplayerDisabled"]];
    diagnostics = [diagnostics stringByAppendingFormat:
        @"\nBattle script: readable %@ / program %@ / running %@ / events %@ / source bytes %@ / empty parameters 0x%03x\nBattle restore: pending 0x%02x / failed 0x%02x / provider uncertain 0x%02x",
        battle[@"scriptReadable"], battle[@"scriptProgram"], battle[@"scriptRunning"], battle[@"scriptEventCount"],
        battle[@"scriptSourceBytes"], [battle[@"parameterEmptyMask"] unsignedIntValue],
        [battle[@"restorePendingMask"] unsignedIntValue], [battle[@"restoreFailedMask"] unsignedIntValue],
        [battle[@"providerUncertainMask"] unsignedIntValue]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nCurrent HP: %@ / local team HP: %@ / valid %@",
        battle[@"currentHP"], battle[@"localTeamHP"], battle[@"hpKnown"]];
    for (NSDictionary *member in battle[@"members"]) if ([member[@"active"] boolValue]) {
        diagnostics = [diagnostics stringByAppendingFormat:@"\nMember %@ source: team %@ / slot %@ / script %@ / provider %@ / rebind %@",
            member[@"slot"], member[@"sourceTeam"], member[@"sourceSlot"], member[@"scriptSlot"],
            member[@"providerToken"], member[@"rebindToken"]];
    }
    for (NSString *inputName in @[@"Battle", @"Timer"]) {
        NSDictionary *inputState = [inputName isEqualToString:@"Battle"] ? battle : timer;
        diagnostics = [diagnostics stringByAppendingFormat:
            @"\n%@ input: valid %@ / quiet %@\nRaw input: count %@ / mask 0x%02x / events 0x%08x\nFrame input: count %@ / mask 0x%02x / events 0x%08x",
            inputName, inputState[@"inputValid"], inputState[@"inputQuiet"], inputState[@"rawInputCount"],
            [inputState[@"rawInputMask"] unsignedIntValue], [inputState[@"rawInputFlags"] unsignedIntValue],
            inputState[@"frameInputCount"], [inputState[@"frameInputMask"] unsignedIntValue],
            [inputState[@"frameInputFlags"] unsignedIntValue]];
    }
    diagnostics = [diagnostics stringByAppendingFormat:@"\nBattle drag active: %@", battle[@"dragActive"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nAuto orbs: %@ · %@\nOrb progress: %@/%@ · completed %@ · input frames %@ · input fault %@\nOrb board: %@×%@ / settled %@ / ordinary %@ / phase %@ / wave %@ / moves %@ / time %@ frames at %@ fps\nOrb solver: %@ / %@ ms / %@ candidates / deadline %@ / limits %@ steps, %@ beam, %@ ms\nOrb device verified by CI: NO\nOrb baseline: 0.5.0 6×5 execute-once reported passed by user on 2026-10-28",
        orbs[@"phase"], orbs[@"message"], orbs[@"confirmed"], orbs[@"steps"], orbs[@"completed"], orbs[@"inputFrames"],
        orbs[@"inputFault"], orbs[@"board"][@"width"], orbs[@"board"][@"height"], orbs[@"settled"], orbs[@"ordinary"],
        orbs[@"nativePhase"], orbs[@"wave"], orbs[@"moves"], orbs[@"remainingFrames"], orbs[@"fps"], orbs[@"quality"],
        orbs[@"elapsedMS"], orbs[@"evaluated"], orbs[@"deadline"], orbs[@"searchSteps"], orbs[@"searchBeam"], orbs[@"searchBudgetMS"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nOrb search passes: %@ / backtracks %@ / deepest %@ steps",
        orbs[@"searchPasses"], orbs[@"backtracks"], orbs[@"exploredDepth"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nThorn Combo priority: %@", orbs[@"thornComboPriority"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nOrb start: sent %@ / accepted %@",
        orbs[@"startSent"], orbs[@"startAccepted"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nOrb execution: %@ ms / limit %@ ms",
        orbs[@"executionElapsedMS"], orbs[@"executionLimitMS"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nOrb board transition: %@ / observed frames %@ / progress %@ / target %@\nOrb last read issue: %@",
        orbs[@"boardTransition"], orbs[@"boardTransitionFrames"], orbs[@"resizeProgress"], orbs[@"resizeTarget"], orbs[@"lastReadIssue"]];
    diagnostics = [diagnostics stringByAppendingFormat:
        @"\nOrb effects: %@\nOrb hazard policy (candidate only): met %@ / poison erased %@ / unmatched bombs %@\nSpinners: mask %@ / colors %@ / phases %@ / turns %@ / period %@ frames\nBoard restrictions: blocked %@ / tape %@ / cloud %@ / fixed start (one-based) %@ / unmatchable %@ / min run %@ / min group %@",
        orbs[@"obstacles"], orbs[@"hazardsSafe"], orbs[@"poisonErased"], orbs[@"unmatchedBombs"],
        orbs[@"spinnerMask"], orbs[@"spinnerColors"], orbs[@"spinnerVariants"], orbs[@"spinnerTurns"], orbs[@"spinnerPeriodFrames"],
        orbs[@"blockedMask"], orbs[@"tapeMask"], orbs[@"cloudMask"], orbs[@"fixedStart"], orbs[@"unmatchableColors"],
        orbs[@"minErase"], orbs[@"minMatch"]];
    diagnostics = [diagnostics stringByAppendingFormat:
        @"\nSpinner read: %@ / stored count %@ / cells %@ / raw period %@ / raw colors %@ / rows %@\nUnmatchable known: %@\nThorns: HP known %@ / HP %@ / maximum %@ / pending damage %@ / route contacts %@ / damage upper bound %@ / first-wave heart fours %@ (healing and resistance not credited)",
        orbs[@"spinnerReadStatus"], orbs[@"spinnerRawCount"], orbs[@"spinnerStoredCells"], orbs[@"spinnerRawPeriod"],
        orbs[@"spinnerRawColors"], orbs[@"spinnerRawRows"], orbs[@"unmatchableKnown"],
        orbs[@"thornHPKnown"], orbs[@"thornHP"], orbs[@"thornMaximumHP"], orbs[@"pendingOrbDamage"],
        orbs[@"thornContacts"], orbs[@"thornDamage"], orbs[@"heartFours"]];
    diagnostics = [diagnostics stringByAppendingFormat:@"\nOrb goals: %@ / met %@ of %@ / all %@\nOrb leaders: %@ / valid %@\nOrb goal plan: %@",
        orbs[@"goalMode"], orbs[@"matchedGoals"], orbs[@"goalCount"], orbs[@"goalsMet"],
        orbs[@"leaderNumbers"], orbs[@"leadersValid"], orbs[@"goalPlan"]];
    for (NSString *key in @[@"searchTrace", @"executionHistory", @"opening"]) {
        id trace = orbs[key];
        NSData *data = trace ? [NSJSONSerialization dataWithJSONObject:trace options:NSJSONWritingSortedKeys error:NULL] : nil;
        NSString *json = data ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] : @"null";
        diagnostics = [diagnostics stringByAppendingFormat:@"\nOrb %@ (row-major, zero-based path): %@", key, json];
    }
    diagnostics = [diagnostics stringByAppendingFormat:@"\nContinuous orbs: %@ / turn %@ / moves %@ / skills %@ / skill window %@\nSkill rules: %@\nSkill log: %@\nEnemy effects: %@ / player bypass: %@",
        orbs[@"automatic"], orbs[@"autoTurn"], orbs[@"autoMoves"], orbs[@"autoSkills"], orbs[@"skillWindow"],
        orbs[@"rules"], [orbs[@"automationLog"] componentsJoinedByString:@"\n"], orbs[@"enemyEffects"], orbs[@"playerBypass"]];
    diagnostics = [diagnostics stringByAppendingFormat:
        @"\nSkill decisions: turn %@ / observed phase %@ / skill window %@ / busy %@",
        orbs[@"decisionTurn"], orbs[@"decisionPhase"], orbs[@"decisionSkillWindow"], orbs[@"decisionSkillBusy"]];
    for (NSDictionary *decision in orbs[@"ruleDecisionDetails"]) {
        NSString *selection = [decision[@"selection"] unsignedIntValue] == UINT32_MAX ? @"auto" :
            [decision[@"selection"] description];
        diagnostics = [diagnostics stringByAppendingFormat:
            @"\nRule %@ -> position %@ (#%@): %@ / state %@ / accumulated charge %@ / selection %@ / active %@ / bound %@",
            decision[@"rule"], decision[@"slot"], decision[@"number"], decision[@"decision"], decision[@"state"],
            decision[@"charge"], selection, decision[@"active"], decision[@"bound"]];
        diagnostics = [diagnostics stringByAppendingFormat:
            @" / skill stages %@ / cached stages %@ / levels %@ / use counts %@ / assist catalog %@",
            decision[@"skillStages"], decision[@"cachedSkillStages"], decision[@"skillLevels"],
            decision[@"memberSkillUses"], decision[@"assistCatalogID"]];
        if ([decision[@"temporaryTurns"] unsignedIntValue]) diagnostics = [diagnostics stringByAppendingFormat:
            @" / temporary %@ / original #%@ / original assist %@ / native duration %@",
            decision[@"temporaryKnown"], decision[@"identityNumber"], decision[@"identityAssist"], decision[@"temporaryTurns"]];
        diagnostics = [diagnostics stringByAppendingFormat:
            @"\n  Observation: %@ / phase %@ / wave %@ / skill uses %@ / board %@×%@ / spinners %@ / orb counts [fire,water,wood,light,dark,heart,jammer,poison,mortal,bomb]: [%@]",
            [decision[@"nativeAttempt"] boolValue] ? @"native attempt" : @"rule scan", decision[@"observedPhase"],
            decision[@"observedWave"], decision[@"observedSkillUses"], decision[@"boardWidth"], decision[@"boardHeight"],
            decision[@"spinnerMask"], [decision[@"orbCounts"] componentsJoinedByString:@","]];
    }
    NSDictionary *snapshot = @{
        @"schema": @1, @"dylibVersion": PH_RUNTIME_VERSION, @"loaded": @YES, @"overlayVisible": @(PHOverlayVisible),
        @"tone": tone, @"title": title, @"reason": reason ?: @"", @"validation": PHValidationMessage ?: @"",
        @"targetMatched": @(PHValidated), @"version": @(PHTargetVersion), @"iosBuild": @(PHTargetBuild),
        @"frameHook": @(PHFrameInstalled), @"networkHook": @(PHNetworkInstalled), @"lifecycleHook": @(PHLifecycleInstalled),
        @"frameOnMainThread": @(PHFrameMainThread), @"accountReady": @(PHAccountReady),
        @"apiFiltersConfigured": @(PHFiltersConfigured()), @"endpointConfigured": @(PHObservedEndpointConfigured()),
        @"activeMask": @(mask), @"unlockDataReady": @(PHUnlockDataReady), @"ownershipWriteFault": @(PHOwnershipWriteFault),
        @"timerEnabled": @(timerActive), @"entryCount": @(PHObservedEntries), @"storageFault": @(PHStorageFault),
        @"configValid": @(PHConfigValid), @"canSaveConfig": @(PHStorageReady), @"config": PHConfig ?: PHDefaultConfiguration(),
        @"featureStates": featureStates, @"endpoint": PHObservedEndpoint ?: @{}, @"checks": checks,
        @"saveState": saveState, @"saveSummary": saveSummary,
        @"battleState": battle, @"battleSummary": battleSummary, @"formationDraft": PHFormationDraft ?: @{},
        @"formationLibrary": PHFormationLibrary ?: @{},
        @"formationSaveResult": PHFormationSaveResult ?: @{},
        @"formationLibraryStored": @(PHFormationLibraryStored),
        @"badgeState": badge,
        @"timerState": timer, @"timerSummary": timerSummary,
        @"orbState": orbs,
        @"configPath": path, @"feedback": PHFeedback ?: @"", @"diagnostics": diagnostics,
        @"badgeImplemented": @YES, @"deviceVerified": @NO
    };
    CFAbsoluteTime now = CFAbsoluteTimeGetCurrent();
    if (PHContextIdentified && PHFolder.length && now - PHLastStatus >= 5) {
        PHLastStatus = now;
        NSMutableDictionary *status = [snapshot mutableCopy];
        [status removeObjectForKey:@"formationLibrary"];
        status[@"formationLibraryCount"] = @([PHFormationLibrary[@"teams"] count]);
        status[@"formationSelectedID"] = PHFormationLibrary[@"selected"] ?: @"";
        PHWriteJSON(status, @"status.json");
    }
    return snapshot;
}

static void PHHandleOverlayAction(NSString *action, id value) {
    if ([action isEqual:@"orbsOnce"] || [action isEqual:@"orbsStart"] || [action isEqual:@"orbsExecute"]) {
        NSDictionary *battle = [PHBattleTracker snapshot];
        if ([battle[@"restorePending"] boolValue] || [battle[@"pendingMask"] unsignedIntegerValue]) {
            [PHOrbTracker queueStop:@"隊伍正在套用或還原，完成後可開始轉珠"];
            PHLastStatus = 0; return;
        }
    }
    if ([action isEqual:@"orbsOnce"]) {
        [PHOrbTracker configureSettings:PHConfigurationOrbSettings(PHConfig)];
        [PHOrbTracker queueOnce]; return;
    }
    if ([action isEqual:@"orbsStart"]) {
        [PHOrbTracker configureProfiles:PHConfigurationOrbProfiles(PHConfig)];
        [PHOrbTracker configureSettings:PHConfigurationOrbSettings(PHConfig)];
        [PHOrbTracker queueAutomatic]; return;
    }
    if ([action isEqual:@"orbsRead"]) {
        [PHOrbTracker configureSettings:PHConfigurationOrbSettings(PHConfig)];
        [PHOrbTracker queueRead]; return;
    }
    if ([action isEqual:@"orbsExecute"]) {
        if ([value isKindOfClass:NSNumber.class]) [PHOrbTracker queueExecute:[value unsignedIntegerValue]];
        return;
    }
    if ([action isEqual:@"orbsStop"]) { [PHOrbTracker queueStop:@"已手動停止自動轉珠"]; return; }
    if ([action isEqualToString:@"formationLibrarySave"]) {
        BOOL request = [value isKindOfClass:NSDictionary.class] &&
            [value[@"requestID"] isKindOfClass:NSString.class];
        BOOL saved = PHSaveFormationLibrary(request ? value[@"library"] : value);
        PHFormationSaveResult = @{@"requestID":request ? value[@"requestID"] : @"",
            @"ok":@(saved), @"message":PHFeedback ?: @"儲存未完成"};
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"formationSave"]) {
        NSString *reason = nil;
        if (PHFormationReadTeam(value, NULL, &reason)) {
            NSDictionary *library = PHFormationLibraryUpdatingTeam(PHFormationLibrary, value);
            PHSaveFormationLibrary(library ?: PHFormationLibraryFromTeam(value));
        } else PHFeedback = reason;
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"battleTeamApply"]) {
        PHBattleLoadout loadouts[6] = {0};
        NSString *reason = nil;
        uint8_t mask = 0;
        if (PHFormationReadApply(value, loadouts, &mask, &reason))
            [PHBattleTracker queueLoadouts:loadouts mask:mask];
        else PHFeedback = reason ?: @"要套用的位置無效";
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"battleRestoreSlot"]) {
        NSUInteger slot = 0;
        if (PHFormationReadRestoreSlot(value, &slot)) {
            [PHOrbTracker queueStop:@"已要求還原隊伍，停止自動轉珠與技能，等待還原"];
            [PHBattleTracker queueRestoreSlot:slot];
        }
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"overlayVisible"]) {
        PHOverlayVisible = [value boolValue];
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"copied"]) {
        PHFeedback = @"已複製診斷（不含帳號、query 或憑證）";
        return;
    }
    if ([action isEqualToString:@"battleApply"]) {
        if ([value isKindOfClass:NSDictionary.class] && [value[@"slot"] isKindOfClass:NSNumber.class] &&
            [value[@"target"] isKindOfClass:NSString.class]) {
            [PHBattleTracker queueApplySlot:[value[@"slot"] unsignedIntegerValue] targetText:value[@"target"]];
        }
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"battleRestore"]) {
        [PHOrbTracker queueStop:@"已要求還原隊伍，停止自動轉珠與技能，等待還原"];
        [PHBattleTracker queueRestore];
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"timerDismiss"]) {
        [PHTimerTracker queueDismiss];
        PHLastStatus = 0;
        return;
    }
    if ([action isEqualToString:@"retry"]) {
        PHPrepareStorage();
        if (PHStorageReady && PHConfigValid) {
            if (PHWriteJSON(PHConfig, @"config.json")) PHStorageFault = NO;
            else {
                PHStorageReady = NO;
                PHStorageMessage = @"config.json 寫入失敗；請檢查遊戲容器的儲存空間。";
            }
        }
        PHFeedback = @"正在重新檢查…";
        PHStartupState = @"checking";
        PHBootstrapGeneration++;
        PHBootstrap(0, PHBootstrapGeneration);
        return;
    }
    if (!PHStorageReady) {
        PHFeedback = @"設定無法儲存，請按「重新檢查」。";
        if ([action isEqual:@"orbsQuality"]) PHOrbQualityNotice = PHFeedback;
        if ([action isEqual:@"orbsThornPriority"]) PHOrbThornNotice = PHFeedback;
        if ([action isEqual:@"orbsGoals"]) PHOrbGoalNotice = PHFeedback;
        if ([action isEqual:@"orbsRules"]) PHOrbRuleNotice = PHFeedback;
        if ([action isEqual:@"orbsRecovery"]) PHOrbRecoveryNotice = PHFeedback;
        if ([action isEqual:@"orbsProfiles"]) PHOrbProfileNotice = PHFeedback;
        return;
    }
    PHReloadConfig();
    NSMutableDictionary *next = [PHConfig mutableCopy];
    if ([action isEqual:@"orbsProfiles"]) {
        NSDictionary *orbState = [PHOrbTracker snapshot];
        if ([orbState[@"busy"] boolValue] || [orbState[@"inputFault"] boolValue]) {
            PHOrbProfileNotice = @"請先停止連續模式及轉珠，再儲存策略。"; return;
        }
        if (![value isKindOfClass:NSDictionary.class] || !PHOrbProfilesValid(value[@"library"]) ||
            !PHOrbCanonicalSettings(value[@"settings"])) { PHOrbProfileNotice = @"策略格式或設定無效。"; return; }
        NSDictionary *library = value[@"library"];
        NSDictionary *selected = PHOrbProfileNamed(library, library[@"selected"]);
        NSDictionary *settings = PHOrbCanonicalSettings(selected ? selected[@"settings"] : value[@"settings"]);
        next[@"orbProfiles"] = library; next[@"orbs"] = settings;
        if (selected[@"formation"]) {
            NSDictionary *formations = PHOrbFormationsSelectingProfile(PHFormationLibrary, selected);
            if (!formations) { PHOrbProfileNotice = @"隊伍庫已滿或資料無效；整包未儲存，請先整理隊伍庫。"; return; }
            next[@"formations"] = formations;
        }
    } else if ([action isEqual:@"orbsQuality"] || [action isEqual:@"orbsGoals"] || [action isEqual:@"orbsRules"] ||
        [action isEqual:@"orbsRecovery"] || [action isEqual:@"orbsThornPriority"]) {
        NSDictionary *orbState = [PHOrbTracker snapshot];
        if ([orbState[@"busy"] boolValue] || [orbState[@"inputFault"] boolValue]) return;
        NSMutableDictionary *settings = [PHConfigurationOrbSettings(next) mutableCopy];
        if ([action isEqual:@"orbsQuality"]) {
            if (![value isKindOfClass:NSString.class] || ![@[@"fast", @"balanced", @"thorough"] containsObject:value]) return;
            settings[@"quality"] = value;
        } else if ([action isEqual:@"orbsThornPriority"]) {
            if (!value) return;
            settings[@"thornComboPriority"] = value;
        } else if ([action isEqual:@"orbsRules"]) {
            if (![value isKindOfClass:NSArray.class]) return;
            settings[@"rules"] = value;
        } else if ([action isEqual:@"orbsRecovery"]) {
            if (!PHOrbRecoveryValid(value)) { PHOrbRecoveryNotice = @"心珠設定無效；HP 需為 0–100，組數需為 1–3。"; return; }
            settings[@"recovery"] = [value copy];
        } else {
            if (![value isKindOfClass:NSDictionary.class] || !value[@"mode"] || !value[@"goals"]) return;
            settings[@"mode"] = value[@"mode"]; settings[@"goals"] = value[@"goals"];
        }
        next[@"orbs"] = settings;
        NSDictionary *profiles = PHOrbProfilesUpdatingSettings(PHConfigurationOrbProfiles(next), settings);
        if (!profiles) {
            if ([action isEqual:@"orbsThornPriority"]) PHOrbThornNotice = @"荊棘設定或目前策略無效，未儲存。";
            else if ([action isEqual:@"orbsRecovery"]) PHOrbRecoveryNotice = @"心珠設定或目前策略無效，未儲存。";
            else if ([action isEqual:@"orbsRules"]) PHOrbRuleNotice = @"規則無效；請檢查角色、樓層、群組、HP 百分比及異常條件。";
            else PHOrbGoalNotice = @"目標設定無效；請檢查數量、珠色及是否有重複條件。";
            return;
        }
        next[@"orbProfiles"] = profiles;
        if (!PHConfigurationIsValid(next)) {
            if ([action isEqual:@"orbsThornPriority"]) PHOrbThornNotice = @"荊棘設定與目前策略無法一致儲存，請重新檢查。";
            else if ([action isEqual:@"orbsRecovery"]) PHOrbRecoveryNotice = @"心珠設定與目前策略無法一致儲存，請重新檢查。";
            else if ([action isEqual:@"orbsRules"]) PHOrbRuleNotice = @"規則無效；請檢查位置、角色編號、樓層及缺珠條件。";
            else PHOrbGoalNotice = @"目標設定無效；請檢查數量、珠色及是否有重複條件。";
            return;
        }
        // The selected profile and its working settings share one atomic
        // config write. A failed save cannot update one without the other.
    } else if ([action isEqualToString:@"adoptEndpoint"]) {
        NSString *host = PHObservedEndpoint[@"host"], *path = PHObservedEndpoint[@"path"];
        if (![host isKindOfClass:NSString.class] || !host.length || ![path isKindOfClass:NSString.class] || ![path hasPrefix:@"/"]) {
            PHFeedback = @"尚未偵測 API，請在遊戲內先儲存一次隊伍。";
            return;
        }
        next[@"apiHosts"] = @[host.lowercaseString];
        next[@"apiPaths"] = @[path];
    } else if ([action isEqualToString:@"experimentalWrites"]) {
        next[action] = @([value boolValue]);
    } else if ([action isEqual:@"allowAllMulti"] || [action isEqual:@"multiModes"]) {
        PHFeedback = @"多人戰鬥操作已停用，不能透過入口設定開啟";
        return;
    } else if ([@[@"allowAllModes", @"allowAllSources", @"entryModes", @"sourceModes", @"autoApplyTeam"] containsObject:action]) {
        NSMutableDictionary *battle = [PHConfigurationBattleFilters(next) mutableCopy];
        if ([action hasPrefix:@"allowAll"] || [action isEqual:@"autoApplyTeam"]) battle[action] = @([value boolValue]);
        else {
            NSArray *values = PHConfigurationReadFilterText(value, UINT16_MAX);
            if (!values) { PHFeedback = @"清單需為不重複的非負整數，以逗號分隔，最多 32 個"; return; }
            battle[action] = values;
        }
        next[@"battle"] = battle;
    } else if ([@[@"skin", @"bgm", @"bgmUncleared", @"badge", @"timerFix", @"battleSwap"] containsObject:action]) {
        NSMutableDictionary *features = [next[@"features"] mutableCopy];
        features[action] = @([value boolValue]);
        next[@"features"] = features;
    } else {
        return;
    }
    if (PHConfigurationIsValid(next) && PHWriteJSON(next, @"config.json")) {
        PHConfig = next;
        PHConfigValid = YES;
        if (next[@"formations"]) PHPublishFormationLibrary(next[@"formations"]);
        [PHOrbTracker configureProfiles:PHConfigurationOrbProfiles(PHConfig)];
        [PHOrbTracker configureSettings:PHConfigurationOrbSettings(PHConfig)];
        PHOrbQualityNotice = nil; PHOrbGoalNotice = nil; PHOrbRuleNotice = nil; PHOrbRecoveryNotice = nil; PHOrbProfileNotice = nil;
        PHOrbThornNotice = nil;
        PHUnlockDataReady = NO;
        PHFeedback = [action isEqualToString:@"adoptEndpoint"] ? @"API 已套用並儲存" : @"設定已儲存";
    } else {
        PHStorageReady = NO;
        PHStorageMessage = @"config.json 寫入失敗；請按「重新檢查」。";
        PHFeedback = PHStorageMessage;
        if ([action isEqual:@"orbsQuality"]) PHOrbQualityNotice = @"品質未能儲存，仍保留原設定；請返回 PC 面板重新檢查。";
        if ([action isEqual:@"orbsThornPriority"]) PHOrbThornNotice = @"荊棘設定未能儲存，仍保留原設定；請重新檢查儲存空間。";
        if ([action isEqual:@"orbsGoals"]) PHOrbGoalNotice = @"目標未能儲存，仍保留原設定；請返回 PC 面板重新檢查。";
        if ([action isEqual:@"orbsRules"]) PHOrbRuleNotice = @"規則未能儲存，仍保留原設定；請返回 PC 面板重新檢查。";
        if ([action isEqual:@"orbsRecovery"]) PHOrbRecoveryNotice = @"心珠設定未能儲存，仍保留原設定；請重新檢查儲存空間。";
        if ([action isEqual:@"orbsProfiles"]) PHOrbProfileNotice = @"策略未能儲存，仍保留原設定；請返回 PC 面板重新檢查。";
    }
    PHLastStatus = 0;
}

static void PHBootstrap(NSUInteger attempt, NSUInteger generation) {
    if (generation != PHBootstrapGeneration) return;
    // If discovery used an original Objective-C IMP, keep its verified header
    // when retrying: that IMP now belongs to our own dylib after replacement.
    const struct mach_header_64 *header = PHGuest ?: PHResolveGuestHeader();
    if (!PHContextIdentified) {
        NSString *identifier = PHGuestBundleIdentifier();
        NSString *executable = NSBundle.mainBundle.infoDictionary[@"CFBundleExecutable"];
        BOOL looksLikePAD = [identifier isEqualToString:@(PHTargetBundleID)] || [executable isEqualToString:@"pad"];
        if (looksLikePAD || header) {
            PHContextIdentified = YES;
            PHStartupState = @"checking";
            PHValidationMessage = @"正在尋找遊戲影像及檢查版本";
            // Create diagnostics and show UI before the strict feature gate.
            PHPrepareStorage();
            PHShowOverlay(^{ return PHRuntimeSnapshot(); }, ^(NSString *action, id value) {
                PHHandleOverlayAction(action, value);
            });
        } else {
            if (attempt < 20) {
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                    PHBootstrap(attempt + 1, generation);
                });
            } else PHLog(@"Not a PAD guest; overlay and hooks skipped");
            return;
        }
    }
    if (!header && attempt < 20) {
        PHValidationMessage = @"正在尋找遊戲影像；GUI 已載入，尚未安裝 hooks。";
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            PHBootstrap(attempt + 1, generation);
        });
        return;
    }
    NSString *reason = nil;
    PHValidated = header && PHValidateGuest(header, &reason);
    PHValidationMessage = reason ?: @"未找到匹配的遊戲 UUID；檢查是否使用日服 23.6.0／106878。";
    PHLastStatus = 0;
    if (!PHValidated) {
        PHStartupState = @"failed";
        PHFeedback = attempt == 0 ? @"校驗未通過，請查看診斷" : @"找尋遊戲影像已完成，請查看診斷";
        PHLog(PHValidationMessage);
        PHRuntimeSnapshot();
        return;
    }
    PHGuest = header;
    PHLog([NSString stringWithFormat:@"Matched PAD %@ build %@ UUID %@; GUI %@",
           @(PHTargetVersion), @(PHTargetBuild), @(PHTargetUUIDString), PH_RUNTIME_VERSION]);
    Class view = objc_getClass("ViewController");
    // Retrying must never capture our own replacement as the original IMP.
    if (!PHFrameInstalled) PHFrameInstalled = view && PHVoidMethod(view, sel_registerName("drawFrame"), PHDrawFrameRVA,
                                                   (IMP)PHDrawFrame, &PHOriginalDraw);
    if (!PHLifecycleInstalled) PHLifecycleInstalled = view && PHVoidMethod(view, sel_registerName("restoreFromBackground"),
                                                        PHRestoreBackgroundRVA, (IMP)PHRestoreBackground, &PHOriginalBackground);
    if (!PHNetworkInstalled) PHNetworkInstalled = PHInstallNetwork();
    PHStartupState = @"ready";
    if ([PHFeedback isEqualToString:@"正在重新檢查…"]) PHFeedback = @"重新檢查完成";
    PHLog([NSString stringWithFormat:@"Hooks frame=%d lifecycle=%d network=%d; floating overlay initialized",
           PHFrameInstalled, PHLifecycleInstalled, PHNetworkInstalled]);
    PHRuntimeSnapshot();
}

__attribute__((constructor))
static void PADCheaperInitialize(void) {
    @autoreleasepool {
        PHSaveStateTracker = [PHSaveState new];
        PHDeckRestoreTracker = [PHDeckRestorer new];
        PHBattleTracker = [[PHBattleSession alloc] initWithEngine:PHCreateBattleEngine()];
        PHBadgeTracker = [PHBadges new];
        PHMusicTracker = [PHMusic new];
        PHTimerTracker = [PHTimer new];
        PHOrbTracker = [[PHOrbSession alloc] initWithEngine:PHCreateOrbEngine()];
        PHFlagPatches = [NSMutableArray array];
        PHLastBanks = @{};
        PHConfig = PHDefaultConfiguration();
        PHStartupState = @"checking";
        PHBootstrapGeneration = 1;
        PHLog([NSString stringWithFormat:@"dylib %@ constructor loaded", PH_RUNTIME_VERSION]);
        // Guest classes and image metadata must be available before hook setup.
        // Heavy initialization runs after dyld's constructor returns.
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationWillResignActiveNotification object:nil
                queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {
                    [PHOrbTracker stopForLifecycle:@"遊戲離開前景，已停止自動轉珠"];
                }];
            PHBootstrap(0, PHBootstrapGeneration);
        });
    }
}
