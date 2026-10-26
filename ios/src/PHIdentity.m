#import "PHIdentity.h"

NSString *PHResolvedBundleIdentifier(NSDictionary *info, NSDictionary *liveContainerInfo) {
    id useHostID = liveContainerInfo[@"doUseLCBundleId"];
    id original = liveContainerInfo[@"LCOrignalBundleIdentifier"];
    // LC intentionally spells this key "Orignal". Only trust it in that mode;
    // stale metadata must not turn an unrelated bundle into a matching target.
    if ([useHostID isKindOfClass:NSNumber.class] && [useHostID boolValue] &&
        [original isKindOfClass:NSString.class] && [original length] > 0) {
        return original;
    }
    id current = info[@"CFBundleIdentifier"];
    return [current isKindOfClass:NSString.class] ? current : @"";
}

BOOL PHBundleIdentityMatches(NSDictionary *info, NSDictionary *liveContainerInfo,
                             NSString *bundleID, NSString *version, NSString *build,
                             NSString **reason) {
    NSString *resolved = PHResolvedBundleIdentifier(info, liveContainerInfo);
    if (![resolved isEqualToString:bundleID]) {
        if (reason) *reason = [NSString stringWithFormat:@"Bundle ID 不符：%@（需要 %@）",
                              resolved.length ? resolved : @"未取得", bundleID];
        return NO;
    }
    id observedVersion = info[@"CFBundleShortVersionString"];
    id observedBuild = info[@"CFBundleVersion"];
    if (![observedVersion isKindOfClass:NSString.class] ||
        ![observedBuild isKindOfClass:NSString.class] ||
        ![observedVersion isEqualToString:version] || ![observedBuild isEqualToString:build]) {
        if (reason) *reason = [NSString stringWithFormat:@"遊戲版本不符：%@／%@（需要 %@／%@）",
                              observedVersion ?: @"未取得", observedBuild ?: @"未取得", version, build];
        return NO;
    }
    return YES;
}
