#import <Foundation/Foundation.h>
#include "PHBattleModel.h"

@interface PHTimer : NSObject
// Called before the original drawFrame, or just after native background resume.
- (void)onFrame:(PHBattleFrame)frame atTime:(NSTimeInterval)time force:(BOOL)force;
// A UI click only queues a command tied to the currently observed dialog.
- (void)queueDismiss;
- (NSDictionary *)snapshot;
@end
