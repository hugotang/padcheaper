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
        Check([PHConfigurationBattleFilters(defaults)[@"autoApplyTeam"] isEqual:@NO], "entry auto-apply defaults off");
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
        Check(![config[@"features"][@"badge"] boolValue], "old schema 1 files keep badge unlock off");
        config[@"features"] = @{@"skin": @YES, @"bgm": @YES, @"timerFix": @NO, @"badge": @YES};
        Check(PHConfigurationIsValid(config), "badge uses an optional Boolean switch");
        config[@"features"] = @{@"skin": @YES, @"bgm": @YES, @"timerFix": @NO, @"badge": @1};
        Check(!PHConfigurationIsValid(config), "numeric badge switch is rejected");
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
        config = [defaults mutableCopy];
        [config removeObjectForKey:@"battle"];
        Check([PHConfigurationBattleFilters(config)[@"autoApplyTeam"] isEqual:@NO], "old configs keep entry auto-apply off");
        PHBattlePolicy policy = PHConfigurationBattlePolicy(config);
        Check(PHConfigurationIsValid(config) && PHBattlePolicyAllows(&policy, 7, 0, 0) &&
              !PHBattlePolicyAllows(&policy, 7, 1, 0), "old configs gain all-mode support while keeping source/multi zero defaults");
        config[@"battle"] = @{@"allowAllModes": @NO, @"entryModes": @[@0, @12, @7],
            @"sourceModes": @[@0, @3], @"multiModes": @[@0, @2]};
        policy = PHConfigurationBattlePolicy(config);
        Check(PHConfigurationIsValid(config) && PHBattlePolicyAllows(&policy, 7, 3, 2) &&
              !PHBattlePolicyAllows(&policy, 1, 3, 2), "mode/source/multi filters have independent bounds and consume GUI-persisted values");
        config[@"battle"] = @{@"allowAllModes": @YES, @"allowAllSources": @YES, @"allowAllMulti": @YES};
        policy = PHConfigurationBattlePolicy(config);
        Check(PHConfigurationIsValid(config) && PHBattlePolicyAllows(&policy, 7, 3, 1) &&
            !PHBattleIsSinglePlayer(7, 0) && !PHBattleIsSinglePlayer(12, 1) && !PHBattleIsSinglePlayer(12, 2),
            "legacy allow-all config remains readable but cannot override the engine's single-player restriction");
        config[@"battle"] = @{@"entryModes": @[]};
        Check(PHConfigurationIsValid(config), "empty lists are valid even when the all-mode switch is on");
        for (id invalid in @[@[@YES], @[@1.5], @[@-1], @[@65536], @[@0,@0], @"0,1"]) {
            config[@"battle"] = @{@"entryModes": invalid};
            Check(!PHConfigurationIsValid(config), "malformed mode filters are rejected even while allow-all is on");
        }
        config[@"battle"] = @{@"allowAllSources": @1};
        Check(!PHConfigurationIsValid(config), "filter switches must be JSON booleans");
        for (id invalid in @[@1, @0, @"true", NSNull.null, @[]]) {
            config[@"battle"] = @{@"autoApplyTeam":invalid};
            Check(!PHConfigurationIsValid(config), "entry auto-apply rejects numbers, strings and malformed values");
        }
        config[@"battle"] = @{@"autoApplyTeam":@YES};
        NSData *automaticJSON = [NSJSONSerialization dataWithJSONObject:config options:0 error:NULL];
        NSDictionary *automaticConfig = [NSJSONSerialization JSONObjectWithData:automaticJSON options:0 error:NULL];
        Check(PHConfigurationIsValid(automaticConfig) && [PHConfigurationBattleFilters(automaticConfig)[@"autoApplyTeam"] isEqual:@YES],
              "entry switch persists as a Boolean without changing existing filter defaults");
        config[@"battle"] = @{@"sourceModes": @[@65536]};
        Check(!PHConfigurationIsValid(config), "source selector is bounded by its native u16 field");
        config[@"battle"] = @{@"multiModes": @[@4294967295ULL]};
        Check(PHConfigurationIsValid(config), "multi selector accepts its full u32 range");
        config[@"battle"] = @{@"multiModes": @[@4294967296ULL]};
        Check(!PHConfigurationIsValid(config), "overflowing multiplayer selectors are rejected");
        NSMutableArray *tooMany = [NSMutableArray array];
        for (NSUInteger i = 0; i < 33; ++i) [tooMany addObject:@(i)];
        config[@"battle"] = @{@"entryModes": tooMany};
        Check(!PHConfigurationIsValid(config), "filter lists are bounded at 32 entries");
        Check([PHConfigurationReadFilterText(@"0, 1，12\n7", UINT16_MAX) isEqual:@[@0,@1,@12,@7]],
              "GUI text accepts commas, full-width separators and whitespace");
        Check([PHConfigurationReadFilterText(@" ", UINT16_MAX) isEqual:@[]], "GUI can intentionally clear a list");
        for (NSString *invalid in @[@"0,0", @"-1", @"1.5", @"true", @"65536", @"4294967296"]) {
            Check(!PHConfigurationReadFilterText(invalid, UINT16_MAX), "invalid GUI text cannot alter config");
        }
        Check(!PHConfigurationIsValid(nil), "missing config is rejected");
        printf("Bundle identity and configuration tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
