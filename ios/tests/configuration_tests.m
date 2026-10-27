#import <Foundation/Foundation.h>
#import "PHIdentity.h"
#import "PHConfiguration.h"
#include <stdio.h>

static NSUInteger failures;
static void Check(BOOL condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

int main(void) {
    @autoreleasepool {
        NSDictionary *info = @{@"CFBundleIdentifier": @"jp.gungho.pad",
                               @"CFBundleShortVersionString": @"23.6.0", @"CFBundleVersion": @"106878"};
        NSString *reason = nil;
        Check(PHBundleIdentityMatches(info, @{}, @"jp.gungho.pad", @"23.6.0", @"106878", &reason),
              "original PAD bundle matches");
        NSMutableDictionary *hostID = [info mutableCopy];
        hostID[@"CFBundleIdentifier"] = @"com.example.LiveContainer";
        NSDictionary *metadata = @{@"doUseLCBundleId": @YES, @"LCOrignalBundleIdentifier": @"jp.gungho.pad"};
        Check(PHBundleIdentityMatches(hostID, metadata, @"jp.gungho.pad", @"23.6.0", @"106878", &reason),
              "LC host bundle ID accepts the verified original ID");
        Check(!PHBundleIdentityMatches(hostID, @{@"doUseLCBundleId": @NO, @"LCOrignalBundleIdentifier": @"jp.gungho.pad"},
                                       @"jp.gungho.pad", @"23.6.0", @"106878", &reason), "stale original ID is rejected");
        Check(!PHBundleIdentityMatches(hostID, @{@"LCOrignalBundleIdentifier": @"jp.gungho.pad"},
                                       @"jp.gungho.pad", @"23.6.0", @"106878", &reason), "original ID requires LC mode");
        Check(!PHBundleIdentityMatches(hostID, @{@"doUseLCBundleId": @YES, @"LCOrignalBundleIdentifier": @"other.game"},
                                       @"jp.gungho.pad", @"23.6.0", @"106878", &reason), "other guest is rejected");
        hostID[@"CFBundleVersion"] = @"106879";
        Check(!PHBundleIdentityMatches(hostID, metadata, @"jp.gungho.pad", @"23.6.0", @"106878", &reason) &&
              [reason containsString:@"106879"], "LC mode still requires the exact iOS build");
        hostID[@"CFBundleVersion"] = @"106878";
        hostID[@"CFBundleShortVersionString"] = @"23.7.0";
        Check(!PHBundleIdentityMatches(hostID, metadata, @"jp.gungho.pad", @"23.6.0", @"106878", &reason),
              "LC mode still requires the exact version");
        Check([PHResolvedBundleIdentifier(@{}, @{@"doUseLCBundleId": @[], @"LCOrignalBundleIdentifier": @1}) isEqual:@""],
              "malformed metadata does not crash or match");

        NSDictionary *defaults = PHDefaultConfiguration();
        Check(PHConfigurationIsValid(defaults) && ![defaults[@"experimentalWrites"] boolValue], "defaults are diagnostic");
        NSData *serialized = [NSJSONSerialization dataWithJSONObject:defaults options:0 error:NULL];
        Check(PHConfigurationIsValid([NSJSONSerialization JSONObjectWithData:serialized options:0 error:NULL]),
              "config retains Boolean types through JSON persistence");
        NSMutableDictionary *config = [defaults mutableCopy];
        config[@"experimentalWrites"] = @YES;
        config[@"features"] = @{@"skin": @YES, @"bgm": @YES, @"timerFix": @NO};
        config[@"apiHosts"] = @[@"example.invalid"];
        config[@"apiPaths"] = @[@"/api.php"];
        Check(PHConfigurationIsValid(config), "GUI switches and observed endpoint produce a valid configuration");
        Check(![config[@"features"][@"battleSwap"] boolValue], "old schema 1 files keep battle replacement off");
        config[@"features"] = @{@"skin": @YES, @"bgm": @YES, @"timerFix": @NO, @"battleSwap": @YES};
        Check(PHConfigurationIsValid(config), "battle replacement uses an optional Boolean switch");
        config[@"features"] = @{@"skin": @YES, @"bgm": @YES, @"timerFix": @NO, @"battleSwap": @1};
        Check(!PHConfigurationIsValid(config), "numeric battle replacement switch is rejected");
        config[@"features"] = @{@"skin": @1, @"bgm": @YES, @"timerFix": @NO};
        Check(!PHConfigurationIsValid(config), "numeric feature is rejected");
        config[@"features"] = @{@"skin": @YES};
        Check(!PHConfigurationIsValid(config), "missing feature switches are rejected");
        config[@"features"] = defaults[@"features"];
        config[@"schema"] = @YES;
        Check(!PHConfigurationIsValid(config), "Boolean schema is rejected");
        config[@"schema"] = @1;
        config[@"apiHosts"] = @[@1];
        Check(!PHConfigurationIsValid(config), "malformed host list is rejected");
        config[@"apiHosts"] = @[@"example.invalid"];
        config[@"apiPaths"] = @[@"/api.php?pid=1"];
        Check(!PHConfigurationIsValid(config), "endpoint configuration cannot include a query");
        config[@"apiPaths"] = @[@"api.php"];
        Check(!PHConfigurationIsValid(config), "endpoint path must be absolute");
        Check(!PHConfigurationIsValid(nil), "missing config is rejected");
        printf("Bundle identity and configuration tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
