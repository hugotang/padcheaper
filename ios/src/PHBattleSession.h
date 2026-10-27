#import <Foundation/Foundation.h>
#include "PHBattleModel.h"

@protocol PHBattleEngine <NSObject>
- (BOOL)readState:(PHBattleState *)state frame:(PHBattleFrame)frame reason:(NSString **)reason;
- (BOOL)validateTarget:(uint16_t)target state:(const PHBattleState *)state reason:(NSString **)reason;
- (BOOL)validateLoadout:(const PHBattleLoadout *)loadout state:(const PHBattleState *)state reason:(NSString **)reason;
- (PHBattleMutationResult)applyLoadout:(const PHBattleLoadout *)loadout state:(const PHBattleState *)expected
    slot:(NSUInteger)slot member:(PHBattleMember *)member reason:(NSString **)reason;
- (PHBattleMutationResult)restoreLoadout:(const PHBattleMember *)original state:(const PHBattleState *)expected
    slot:(NSUInteger)slot reason:(NSString **)reason;
// The engine rechecks live identity, source, form, phase, fingerprints and
// consistency immediately before any mutation, and owns immediate rollback.
- (PHBattleMutationResult)changeState:(const PHBattleState *)expected slot:(NSUInteger)slot
    form:(uint16_t)form catalog:(uint32_t)catalog presentation:(const PHBattleMember *)presentation
    reason:(NSString **)reason;
@end

@interface PHBattleSession : NSObject
- (instancetype)initWithEngine:(id<PHBattleEngine>)engine;
// Only called by the verified game's drawFrame on the main thread.
- (void)onFrame:(PHBattleFrame)frame atTime:(NSTimeInterval)time;
// UI actions only enqueue work. No engine calls or native writes occur here.
- (void)queueApplySlot:(NSUInteger)slot targetText:(NSString *)text;
- (void)queueLoadouts:(const PHBattleLoadout *)loadouts mask:(uint8_t)mask;
- (void)queueRestoreSlot:(NSUInteger)slot;
- (void)queueRestore;
- (NSDictionary *)snapshot;
@end

FOUNDATION_EXPORT id<PHBattleEngine> PHCreateBattleEngine(void);
