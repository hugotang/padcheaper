#import <Foundation/Foundation.h>
#include "PHOrbModel.h"

typedef NS_ENUM(NSInteger, PHOrbSkillResult) { PHOrbSkillRejected, PHOrbSkillUnavailable, PHOrbSkillStarted };

@protocol PHOrbEngine <NSObject>
- (BOOL)read:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason;
- (BOOL)readAutomation:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason;
// Runs on the native frame thread, with a fresh single-player/board/context
// check. Unavailable means the game refused this skill, so another rule may run.
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason;
// Scoped to one original native drawFrame. Must always pair with finishInput,
// including exceptions. The real manager and its UIKit records are untouched.
- (BOOL)stageCell:(NSUInteger)cell down:(BOOL)down beginning:(BOOL)beginning
    expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason;
- (BOOL)finishInput:(NSString **)reason;
@end

@interface PHOrbSession : NSObject
- (instancetype)initWithEngine:(id<PHOrbEngine>)engine;
// Publish only a successfully saved setting. A change invalidates old previews;
// an in-flight native gesture retains its captured options until it finishes.
- (BOOL)configureQuality:(NSString *)quality;
- (BOOL)configureSettings:(NSDictionary *)settings;
- (BOOL)configureProfiles:(NSDictionary *)library;
- (void)queueRead;
// Explicit one-click read/solve/execute, without continuous skill rules.
- (void)queueOnce;
- (void)queueAutomatic;
- (void)queueExecute:(NSUInteger)generation;
- (void)queueStop:(NSString *)reason;
// Interrupt pending work without replacing an already recorded stop/failure.
- (void)stopForLifecycle:(NSString *)reason;
- (void)beforeFrame:(PHBattleFrame)frame time:(NSTimeInterval)time;
- (void)finishNativeFrame;
- (void)afterFrame:(PHBattleFrame)frame time:(NSTimeInterval)time;
- (NSDictionary *)snapshot;
@end

FOUNDATION_EXPORT id<PHOrbEngine> PHCreateOrbEngine(void);
