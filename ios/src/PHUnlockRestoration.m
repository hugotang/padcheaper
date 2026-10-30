#import "PHUnlockRestoration.h"
#import "PHMemory.h"
#include <string.h>

BOOL PHRestoreOwnershipBits(uintptr_t address, uint8_t added[128]) {
    uint8_t any = 0;
    for (NSUInteger i = 0; i < 128; ++i) any |= added[i];
    if (!any) return YES;
    uint8_t current[128], next[128], after[128];
    if (!PHReadMemory(address,current,sizeof(current))) return NO;
    for (NSUInteger i = 0; i < 128; ++i) {
        added[i] &= current[i]; // Native code may already have removed our bit.
        next[i] = current[i] & (uint8_t)~added[i];
    }
    if (memcmp(current,next,sizeof(current)) == 0) return YES;
    if (!PHWriteData(address,next,sizeof(next)) || !PHReadMemory(address,after,sizeof(after))) return NO;
    any = 0;
    for (NSUInteger i = 0; i < 128; ++i) { added[i] &= after[i]; any |= added[i]; }
    return !any;
}

NSArray<NSDictionary *> *PHRestoreDescriptorFlags(NSArray<NSDictionary *> *patches,
    uintptr_t table, uint32_t count, BOOL tableReady, PHFeatureMask enabled, BOOL *fault) {
    NSMutableArray *remaining = [NSMutableArray array];
    if (!tableReady || !table || !count || count > 2048 || table > UINTPTR_MAX - (uintptr_t)count * 52u) {
        if (fault && patches.count) *fault = YES;
        return patches;
    }
    for (NSDictionary *patch in patches) {
        const uintptr_t address = [patch[@"address"] unsignedLongLongValue];
        // A proven replacement table has no addresses owned by the old lease.
        if ([patch[@"table"] unsignedLongLongValue] != table || address < table + 14 ||
            address - table >= (uintptr_t)count * 52u || (address - table - 14) % 52u != 0) continue;
        uint16_t identifier = 0, flags = 0, after = 0, original = [patch[@"original"] unsignedShortValue];
        if (!PHReadU16(address - 14,&identifier) || !PHReadU16(address,&flags)) {
            [remaining addObject:patch]; if (fault) *fault = YES; continue;
        }
        // Reordered descriptors and native changes revoke the old lease even
        // while the feature is enabled, avoiding duplicate or stale restores.
        if (identifier != [patch[@"id"] unsignedShortValue] || flags != (uint16_t)(original | 1u)) continue;
        if (enabled & [patch[@"feature"] unsignedIntegerValue]) { [remaining addObject:patch]; continue; }
        if (!PHWriteData(address,&original,sizeof(original)) || !PHReadU16(address,&after) || after != original) {
            [remaining addObject:patch]; if (fault) *fault = YES;
        }
    }
    return remaining;
}
