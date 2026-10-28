#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

@protocol PHOrbEngine <NSObject>
- (BOOL)read:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason;
// Scoped to one original native drawFrame. Must always pair with finishInput,
// including exceptions. The real manager and its UIKit records are untouched.
- (BOOL)stageCell:(NSUInteger)cell down:(BOOL)down beginning:(BOOL)beginning
    expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason;
- (BOOL)finishInput:(NSString **)reason;
@end

@interface PHOrbSession : NSObject
- (instancetype)initWithEngine:(id<PHOrbEngine>)engine;
- (void)queueRead;
- (void)queueExecute:(NSUInteger)generation;
- (void)queueStop:(NSString *)reason;
- (void)beforeFrame:(PHBattleFrame)frame time:(NSTimeInterval)time;
- (void)finishNativeFrame;
- (void)afterFrame:(PHBattleFrame)frame time:(NSTimeInterval)time;
- (NSDictionary *)snapshot;
@end

FOUNDATION_EXPORT id<PHOrbEngine> PHCreateOrbEngine(void);
