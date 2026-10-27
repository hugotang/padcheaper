#import "PHBadges.h"
#import "PHMemory.h"
#include "PHBadgeModel.h"
#include "PHBattleModel.h"
#include "PHTargetGenerated.h"
#include "PHBattleGenerated.h"
#include <string.h>

static const uintptr_t PHBadgeTableOffset = 0xa0;
static const uintptr_t PHBadgeEndOffset = 0xb0;
static const uintptr_t PHBadgeCountOffset = 0x24e8;
static const uintptr_t PHBadgeOwnedOffset = 0x52f314;
static const uint32_t PHBadgeOwnedKey = 0x9a69f65au;

@implementation PHBadges {
    uintptr_t _header, _table;
    uint16_t _rawCount;
    uint32_t _key1, _key2, _added[8], _valid[8];
    NSUInteger _eligible;
    BOOL _ready, _writeFault;
    NSString *_message;
}

- (void)reset {
    _header = _table = 0;
    _rawCount = 0;
    _eligible = 0;
    _ready = _writeFault = NO;
    memset(_added, 0, sizeof(_added));
    memset(_valid, 0, sizeof(_valid));
    _message = @"等待有效徽章參數表";
}

- (instancetype)init {
    if ((self = [super init])) [self reset];
    return self;
}

- (BOOL)ready {
    uintptr_t table = 0;
    uint16_t count = 0;
    uint32_t key1 = 0, key2 = 0;
    return _ready && !_writeFault && _header &&
        PHReadPointer(_header + PHSaveRVA + PHBadgeTableOffset, &table) && table == _table &&
        PHReadU16(_header + PHSaveRVA + PHBadgeCountOffset, &count) && count == _rawCount &&
        PHReadU32(_header + PHBattleKey1RVA, &key1) && key1 == _key1 &&
        PHReadU32(_header + PHBattleKey2RVA, &key2) && key2 == _key2;
}

- (BOOL)writeFault { return _writeFault; }

- (void)removeAdded {
    BOOL changed = NO;
    for (size_t i = 0; i < 8; ++i) changed |= _added[i] != 0;
    if (!changed || !_header) return;
    uint32_t words[8];
    uintptr_t address = _header + PHSaveRVA + PHBadgeOwnedOffset;
    if (!PHReadMemory(address, words, sizeof(words))) { _writeFault = YES; return; }
    for (size_t i = 0; i < 8; ++i) words[i] = ((words[i] ^ PHBadgeOwnedKey) & ~_added[i]) ^ PHBadgeOwnedKey;
    if (!PHWriteData(address, words, sizeof(words))) { _writeFault = YES; return; }
    memset(_added, 0, sizeof(_added));
}

- (void)updateHeader:(uintptr_t)header enabled:(BOOL)enabled {
    NSCAssert(NSThread.isMainThread, @"Badge updates require the game main thread");
    if (_header && _header != header) [self reset];
    _header = header;
    _ready = _writeFault = NO;
    _eligible = 0;
    memset(_valid, 0, sizeof(_valid));
    if (!enabled) { [self removeAdded]; _message = @"關閉"; return; }
    _message = @"等待有效徽章參數表";
    uintptr_t end = 0;
    uint16_t count = 0;
    uint32_t key1 = 0, key2 = 0;
    uintptr_t save = header + PHSaveRVA;
    if (!header || !PHReadPointer(save + PHBadgeTableOffset, &_table) || !_table ||
        !PHReadU16(save + PHBadgeCountOffset, &count) || !count || count > 2048 ||
        _table > UINTPTR_MAX - ((size_t)count + 2) * 180 ||
        !PHReadPointer(save + PHBadgeEndOffset, &end) || end != _table + ((size_t)count + 1) * 180 ||
        !PHMemoryHasProtection(_table, ((size_t)count + 2) * 180, VM_PROT_READ, 0) ||
        !PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2)) {
        [self removeAdded];
        return;
    }
    _rawCount = count;
    _key1 = key1; _key2 = key2;
    NSUInteger supported = MIN(count, 255);
    PHBadgeEntry entries[256] = {0};
    for (NSUInteger identifier = 1; identifier <= supported; ++identifier) {
        uint8_t record[180];
        if (!PHReadMemory(_table + (identifier - 1) * 180, record, sizeof(record))) continue;
        entries[identifier].readable = true;
        for (size_t field = 0; field < 11; ++field) {
            uint32_t raw[4], decoded = 0;
            memcpy(raw, record + field * 16, sizeof(raw));
            if (!PHBattleDecodeProtected(raw, key1, key2, 0x6a875a8bu, &decoded)) entries[identifier].readable = false;
            if (!field) entries[identifier].kind = decoded;
        }
        memcpy(&entries[identifier].prerequisite, record + 176, sizeof(uint16_t));
    }
    uint32_t lastKey1 = 0, lastKey2 = 0;
    if (!PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2) {
        [self removeAdded];
        return;
    }
    _eligible = PHBadgeBuildMask(entries, supported, _valid);
    if (!_eligible) { [self removeAdded]; _message = @"沒有通過參數檢查的徽章"; return; }
    uint32_t original[8], words[8];
    uintptr_t address = save + PHBadgeOwnedOffset;
    if (!PHReadMemory(address, original, sizeof(original))) { _writeFault = YES; return; }
    for (size_t i = 0; i < 8; ++i) {
        // Remove only this module's bits if a resource-table reload invalidated them.
        uint32_t decoded = original[i] ^ PHBadgeOwnedKey;
        words[i] = ((decoded & ~(_added[i] & ~_valid[i])) | _valid[i]) ^ PHBadgeOwnedKey;
    }
    if (memcmp(original, words, sizeof(words)) && !PHWriteData(address, words, sizeof(words))) {
        _writeFault = YES;
        _message = @"徽章擁有位圖寫入失敗";
        return;
    }
    for (size_t i = 0; i < 8; ++i) _added[i] = (_added[i] & _valid[i]) | (_valid[i] & ~(original[i] ^ PHBadgeOwnedKey));
    _ready = YES;
    _message = [NSString stringWithFormat:@"已啟用 %lu 個有效徽章", (unsigned long)_eligible];
}

- (BOOL)canUse:(uint8_t)identifier {
    if (!self.ready) return NO;
    if (!identifier) return YES;
    uint32_t bit = UINT32_C(1) << (identifier % 32), word = 0;
    return (_valid[identifier / 32] & bit) &&
        PHReadU32(_header + PHSaveRVA + PHBadgeOwnedOffset + 4 * (identifier / 32), &word) &&
        ((word ^ PHBadgeOwnedKey) & bit);
}

- (NSDictionary *)snapshot {
    return @{@"implemented": @YES, @"ready": @(self.ready), @"writeFault": @(_writeFault),
             @"eligibleCount": @(_eligible), @"message": _message ?: @"", @"deviceVerified": @NO};
}
@end
