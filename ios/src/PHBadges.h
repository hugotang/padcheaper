#import <Foundation/Foundation.h>
#include <stdint.h>

@interface PHBadges : NSObject
@property(nonatomic, readonly) BOOL ready;
@property(nonatomic, readonly) BOOL writeFault;
- (void)reset;
// Call from the validated game's main thread, after account changes are handled.
- (void)updateHeader:(uintptr_t)header enabled:(BOOL)enabled;
- (BOOL)canUse:(uint8_t)identifier;
- (NSDictionary *)snapshot;
@end
