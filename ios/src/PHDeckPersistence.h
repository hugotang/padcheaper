#import <Foundation/Foundation.h>
#import "PHCodec.h"
#include <stdint.h>

typedef struct {
    uintptr_t context, request, records;
    uint8_t primary, secondary;
    uint16_t maximum;
} PHDeckRequest;

// The serializer can emit badge zero even when the request retains a selection.
// Only the same live request, matching every other serialized field, is trusted.
FOUNDATION_EXPORT NSDictionary *PHReadRequestedDecks(uintptr_t header, NSDictionary *serialized,
    PHDeckRequest *observation);
FOUNDATION_EXPORT NSDictionary *PHDeckAccountBanks(id store, uint32_t player);

@interface PHDeckRestorer : NSObject
- (void)cancel;
- (void)enqueue:(NSDictionary *)banks mask:(PHFeatureMask)mask epoch:(NSUInteger)epoch source:(NSString *)source;
- (NSDictionary *)drainHeader:(uintptr_t)header epoch:(NSUInteger)epoch enabled:(PHFeatureMask)enabled
    allowed:(PHFeatureMask)allowed badgeReady:(BOOL)badgeReady canUseBadge:(BOOL (^)(uint8_t))canUse;
- (NSDictionary *)snapshot;
@end
