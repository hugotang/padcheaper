#import <Foundation/Foundation.h>

FOUNDATION_EXPORT NSString *PHResolvedBundleIdentifier(NSDictionary *info, NSDictionary *liveContainerInfo);
FOUNDATION_EXPORT BOOL PHBundleIdentityMatches(NSDictionary *info, NSDictionary *liveContainerInfo,
                                              NSString *bundleID, NSString *version, NSString *build,
                                              NSString **reason);
