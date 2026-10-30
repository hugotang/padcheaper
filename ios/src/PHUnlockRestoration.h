#import <Foundation/Foundation.h>
#import "PHCodec.h"
#include <stdint.h>

// Preserve restoration leases until a readback confirms their bits were removed.
FOUNDATION_EXPORT BOOL PHRestoreOwnershipBits(uintptr_t address, uint8_t added[128]);
FOUNDATION_EXPORT NSArray<NSDictionary *> *PHRestoreDescriptorFlags(
    NSArray<NSDictionary *> *patches, uintptr_t table, uint32_t count, BOOL tableReady,
    PHFeatureMask enabled, BOOL *fault);
