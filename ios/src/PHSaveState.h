#import <Foundation/Foundation.h>
#import "PHCodec.h"
#include <stdint.h>

NS_ASSUME_NONNULL_BEGIN

// Serializes completion updates with the main-thread save/parse path. Public
// snapshots contain counts and messages, never account or deck contents.
@interface PHSaveState : NSObject
- (BOOL)beginSave;
- (void)recordLocalBanks:(NSDictionary *)banks;
- (void)recordPassthrough:(NSString *)reason;
- (void)recordStorageFailure;
- (NSDictionary *)prepareTransactionWithBanks:(NSDictionary *)banks
                                        mask:(PHFeatureMask)mask
                                       epoch:(NSUInteger)epoch
                                     context:(uintptr_t)context
                                     request:(uintptr_t)request
                              restoreAllowed:(BOOL)restoreAllowed
                                        time:(NSTimeInterval)time;
- (void)recordTaskCreated:(BOOL)created transaction:(NSDictionary *)transaction;
- (void)completeTransaction:(NSDictionary *)transaction
                   accepted:(BOOL)accepted
                 httpStatus:(NSInteger)httpStatus;
- (NSDictionary * _Nullable)consumeForEpoch:(NSUInteger)epoch
                                   context:(uintptr_t)context
                                   request:(uintptr_t)request
                               nativeError:(uint32_t)nativeError;
- (void)recordRestoreCount:(NSUInteger)count reason:(NSString *)reason;
- (void)resetWithBanks:(NSDictionary *)banks accountReady:(BOOL)ready;
- (void)expireAtTime:(NSTimeInterval)time;
- (NSDictionary *)snapshot;
@end

NS_ASSUME_NONNULL_END
