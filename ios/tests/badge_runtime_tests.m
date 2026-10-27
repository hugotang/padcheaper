#import <Foundation/Foundation.h>
#import "PHBadges.h"
#import "PHMemory.h"
#include "PHTargetGenerated.h"
#include "PHBattleGenerated.h"
#include <stdio.h>
#include <string.h>

static const uintptr_t Header = 0x100000000, Table = 0x200000000;
static uint32_t key1, key2, words[8];
static uint16_t count;
static uintptr_t tablePointer, endPointer;
static uint8_t records[5 * 180];
static unsigned failures, writes;
static BOOL refuseWrite;
typedef struct { uintptr_t address; void *bytes; size_t size; } Mapping;

static void *Resolve(uintptr_t address, size_t size) {
    Mapping mappings[] = {
        {Header + PHSaveRVA + 0xa0, &tablePointer, sizeof(tablePointer)},
        {Header + PHSaveRVA + 0xb0, &endPointer, sizeof(endPointer)},
        {Header + PHSaveRVA + 0x24e8, &count, sizeof(count)},
        {Header + PHSaveRVA + 0x52f314, words, sizeof(words)},
        {Header + PHBattleKey1RVA, &key1, sizeof(key1)},
        {Header + PHBattleKey2RVA, &key2, sizeof(key2)},
        {Table, records, sizeof(records)},
    };
    for (size_t i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
        Mapping m = mappings[i];
        if (address >= m.address && address - m.address <= m.size && size <= m.size - (address - m.address))
            return (uint8_t *)m.bytes + (address - m.address);
    }
    return NULL;
}

BOOL PHReadMemory(uintptr_t address, void *output, size_t size) {
    void *source = Resolve(address, size);
    if (!source || !output || !size) return NO;
    memcpy(output, source, size); return YES;
}
BOOL PHWriteData(uintptr_t address, const void *input, size_t size) {
    void *destination = Resolve(address, size);
    if (refuseWrite || !destination || address != Header + PHSaveRVA + 0x52f314 || size != sizeof(words)) return NO;
    memcpy(destination, input, size); writes++; return YES;
}
BOOL PHMemoryHasProtection(uintptr_t address, size_t size, vm_prot_t required, vm_prot_t forbidden) {
    (void)required; (void)forbidden;
    return Resolve(address, size) != NULL;
}

static void Check(BOOL condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}
static uint32_t Rotate(uint32_t value, unsigned n) { return value << n | value >> (32 - n); }
static void Seed(void) {
    count = 3; tablePointer = Table; endPointer = Table + 4 * 180;
    key1 = 0x11223344; key2 = 0x55667788;
    memset(records, 0, sizeof(records));
    for (unsigned id = 1; id <= count; ++id) {
        for (unsigned field = 0; field < 11; ++field) {
            uint32_t value = !field ? id : 0;
            uint32_t encoded[] = {Rotate(value, 5) ^ 0x6a875a8bu, Rotate(0x12345678u, 13),
                value ^ key1 ^ key2, value ^ 0x12345678u};
            memcpy(records + (id - 1) * 180 + field * 16, encoded, sizeof(encoded));
        }
    }
    for (unsigned i = 0; i < 8; ++i) words[i] = 0x9a69f65au;
    words[0] ^= 2u; // Badge 1 was legitimately owned before enabling the module.
    writes = 0; refuseWrite = NO;
}

int main(void) {
    @autoreleasepool {
        Seed();
        PHBadges *badges = [PHBadges new];
        [badges updateHeader:Header enabled:YES];
        Check(badges.ready && [badges canUse:0] && [badges canUse:1] && [badges canUse:2] && [badges canUse:3],
              "valid initialized native records enable only their bitmap IDs");
        Check(![badges canUse:4] && writes == 1 && (words[0] ^ 0x9a69f65au) == 14u, "default and out-of-table IDs stay unset");
        [badges updateHeader:Header enabled:YES];
        Check(writes == 1, "unchanged ownership does not write again");
        [badges updateHeader:Header enabled:NO];
        Check(!badges.ready && (words[0] ^ 0x9a69f65au) == 2u, "disable removes only added bits and retains original ownership");

        [badges updateHeader:Header enabled:YES];
        key1++;
        Check(!badges.ready && ![badges canUse:2], "re-keying immediately invalidates cached eligibility");
        [badges updateHeader:Header enabled:YES];
        Check(!badges.ready && (words[0] ^ 0x9a69f65au) == 2u, "invalid native replicas revoke added ownership");

        Seed(); [badges reset];
        endPointer += 180;
        [badges updateHeader:Header enabled:YES];
        Check(!badges.ready && writes == 0, "partial table initialization cannot enable badges");
        endPointer -= 180;
        count = 2049;
        [badges updateHeader:Header enabled:YES];
        Check(!badges.ready && writes == 0, "unbounded table count cannot trigger reads or writes");

        Seed(); [badges reset];
        refuseWrite = YES;
        [badges updateHeader:Header enabled:YES];
        Check(!badges.ready && badges.writeFault && writes == 0, "failed ownership write never reports readiness");
        refuseWrite = NO;
        [badges updateHeader:Header enabled:YES];
        refuseWrite = YES;
        [badges updateHeader:Header enabled:NO];
        Check(badges.writeFault, "failed revert is visible");
        refuseWrite = NO;
        [badges updateHeader:Header enabled:NO];
        Check((words[0] ^ 0x9a69f65au) == 2u, "failed revert retains tracking for a later retry");

        Seed(); [badges reset];
        [badges updateHeader:Header enabled:YES];
        words[0] = 8u ^ 0x9a69f65au; // A new native login replaces ownership.
        [badges reset];
        [badges updateHeader:Header enabled:NO];
        Check((words[0] ^ 0x9a69f65au) == 8u, "account reset never replays old ownership patches");
        printf("Badge native-layout tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
