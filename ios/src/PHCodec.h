#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_OPTIONS(NSUInteger, PHFeatureMask) {
    PHFeatureSkin = 1u << 0,
    PHFeatureBGM = 1u << 1,
    PHFeatureBadge = 1u << 2,
};

FOUNDATION_EXPORT NSString *const PHCodecErrorDomain;

@interface PHRewriteResult : NSObject
@property(nonatomic, copy) NSData *body;
@property(nonatomic, copy) NSDictionary<NSString *, NSArray<NSArray<NSNumber *> *> *> *banks;
@property(nonatomic) BOOL changed;
@end

// Accepts the raw JSON values emitted by this iOS game's save_decks serializer.
// Unknown body parameters remain byte-for-byte intact. An invalid known field
// rejects the whole rewrite, so no bank is partially defaulted.
FOUNDATION_EXPORT PHRewriteResult *_Nullable PHRewriteDeckBody(
    NSData *body, PHFeatureMask mask, NSError *_Nullable *_Nullable error);
FOUNDATION_EXPORT BOOL PHValidateDeckRecord(id record);
FOUNDATION_EXPORT NSArray<NSNumber *> *PHDeckIdentity(NSArray<NSNumber *> *record);
FOUNDATION_EXPORT NSDictionary *PHMergeBanks(
    NSDictionary *previous, NSDictionary *observed, PHFeatureMask mask);

NS_ASSUME_NONNULL_END
