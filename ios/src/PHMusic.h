#import <Foundation/Foundation.h>
#include "PHBattleModel.h"

@interface PHMusic : NSObject
- (void)onFrame:(PHBattleFrame)frame time:(NSTimeInterval)time;
- (NSDictionary *)snapshot;
@end
