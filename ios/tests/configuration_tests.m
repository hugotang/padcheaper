#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import "PHIdentity.h"
#import "PHConfiguration.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#import "PHOrbPlanning.h"
#import "PHFormationModel.h"
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
        Check([PHConfigurationOrbSettings(defaults)[@"quality"] isEqual:@"balanced"], "orb quality defaults to the proven balanced profile");
        Check([PHConfigurationOrbSettings(defaults)[@"thornComboPriority"] isEqual:@NO],
            "new and legacy configurations default to lower thorn damage");
        Check([PHConfigurationOrbSettings(defaults)[@"allowPoisonWithHeartFour"] isEqual:@NO],
            "new and legacy configurations keep normal poison consumption opt-in");
        Check([PHConfigurationOrbSettings(defaults)[@"diagonal"] isEqual:@NO], "diagonal movement defaults off in new and legacy settings");
        Check([PHConfigurationOrbSettings(defaults)[@"assistRecoveryL"] isEqual:@NO], "assist recovery defaults off in new and legacy settings");
        for (id value in @[@YES, @NO, @1, @"true", NSNull.null]) {
            NSMutableDictionary *thornConfig = [defaults mutableCopy]; thornConfig[@"orbs"] = @{@"thornComboPriority":value};
            NSData *json = [NSJSONSerialization dataWithJSONObject:thornConfig options:0 error:NULL];
            NSDictionary *readback = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
            const BOOL valid = [value isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
            Check(PHConfigurationIsValid(readback) == valid, "saved thorn setting accepts only true JSON booleans");
            thornConfig[@"orbs"] = @{@"allowPoisonWithHeartFour":value};
            json = [NSJSONSerialization dataWithJSONObject:thornConfig options:0 error:NULL];
            readback = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
            Check(PHConfigurationIsValid(readback) == valid, "saved poison policy accepts only true JSON booleans");
            thornConfig[@"orbs"] = @{@"diagonal":value};
            json = [NSJSONSerialization dataWithJSONObject:thornConfig options:0 error:NULL];
            readback = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
            Check(PHConfigurationIsValid(readback) == valid, "saved diagonal movement requires a true JSON Boolean");
            thornConfig[@"orbs"] = @{@"assistRecoveryL":value};
            json = [NSJSONSerialization dataWithJSONObject:thornConfig options:0 error:NULL];
            readback = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
            Check(PHConfigurationIsValid(readback) == valid, "saved assist recovery requires a true JSON Boolean");
        }
        Check([PHConfigurationOrbSettings(defaults)[@"mode"] isEqual:@"combo"] &&
            [PHConfigurationOrbSettings(defaults)[@"goals"] count] == 0, "existing behavior remains the default without leader inference");
        Check([PHConfigurationOrbSettings(defaults)[@"rules"] isEqual:@[]], "skills are opt-in rules and continuous running is never a saved switch");
        NSData *serialized = [NSJSONSerialization dataWithJSONObject:defaults options:0 error:NULL];
        Check(PHConfigurationIsValid([NSJSONSerialization JSONObjectWithData:serialized options:0 error:NULL]),
              "config retains Boolean types through JSON persistence");
        NSMutableDictionary *config = [defaults mutableCopy];
        [config removeObjectForKey:@"orbs"];
        Check(PHConfigurationIsValid(config) && [PHConfigurationOrbSettings(config)[@"quality"] isEqual:@"balanced"],
            "existing schema 1 files gain balanced quality without migration or lost settings");
        for (NSString *quality in @[@"fast", @"balanced", @"thorough"]) {
            config[@"orbs"] = @{@"quality":quality};
            NSData *qualityJSON = [NSJSONSerialization dataWithJSONObject:config options:0 error:NULL];
            NSDictionary *restored = [NSJSONSerialization JSONObjectWithData:qualityJSON options:0 error:NULL];
            Check(PHConfigurationIsValid(restored) && [PHConfigurationOrbSettings(restored)[@"quality"] isEqual:quality],
                "each quality round-trips through saved config");
        }
        for (id invalid in @[@YES, @1, @"unknown", NSNull.null, @[]]) {
            config[@"orbs"] = @{@"quality":invalid};
            Check(!PHConfigurationIsValid(config), "invalid quality cannot select unbounded solver options");
        }
        config[@"orbs"] = @"thorough";
        Check(!PHConfigurationIsValid(config), "malformed orb settings are rejected");
        NSArray *goals = @[@{@"kind":@"column", @"colors":@4, @"count":@1, @"length":@0},
            @{@"kind":@"cross", @"colors":@32, @"count":@1, @"length":@0}];
        config[@"orbs"] = @{@"quality":@"thorough", @"mode":@"manual", @"goals":goals};
        serialized = [NSJSONSerialization dataWithJSONObject:config options:0 error:NULL];
        NSDictionary *goalConfig = [NSJSONSerialization JSONObjectWithData:serialized options:0 error:NULL];
        Check(PHConfigurationIsValid(goalConfig) && [PHConfigurationOrbSettings(goalConfig)[@"goals"] isEqual:goals],
            "shape, color, conjunction and quality persist together");
        for (NSDictionary *invalid in @[@{@"mode":@"manual"}, @{@"mode":@"unknown"}, @{@"goals":@YES},
            @{@"mode":@"manual", @"goals":@[goals[0], goals[0]]},
            @{@"mode":@"manual", @"goals":@[@{@"kind":@"l", @"colors":@YES}]}]) {
            config[@"orbs"] = invalid;
            Check(!PHConfigurationIsValid(config), "unusable target configuration is rejected before saving");
        }
        config[@"orbs"] = @{@"mode":@"leaders", @"goals":goals};
        Check(PHConfigurationIsValid(config) && [PHConfigurationOrbSettings(config)[@"quality"] isEqual:@"balanced"],
            "automatic leader mode retains the saved manual profile and default quality");
        [config removeObjectForKey:@"orbs"];
        config[@"experimentalWrites"] = @YES;
        Check([PHConfigurationOrbSettings(config)[@"rules"] isEqual:@[]], "old schema 1 configs gain no automatic skill behavior");
        config[@"orbs"] = @{@"rules":@[PHOrbDefaultSkillRule()]};
        serialized = [NSJSONSerialization dataWithJSONObject:config options:0 error:NULL];
        NSDictionary *skillConfig = [NSJSONSerialization JSONObjectWithData:serialized options:0 error:NULL];
        Check(PHConfigurationIsValid(skillConfig) && [PHConfigurationOrbSettings(skillConfig)[@"rules"] isEqual:@[PHOrbDefaultSkillRule()]],
            "rule settings persist without modifying search quality or saved goals");
        config[@"orbs"] = @{@"rules":@[@{@"slot":@1}]};
        Check(!PHConfigurationIsValid(config), "partial malformed rules are rejected instead of becoming unconditional casts");
        [config removeObjectForKey:@"orbs"];
        Check([PHConfigurationOrbProfiles(config) isEqual:PHOrbDefaultProfiles()], "old configurations have no bound strategy or automatic selection");
        NSDictionary *profileSettings = PHOrbCanonicalSettings(PHConfigurationOrbSettings(config));
        NSDictionary *profile = @{@"id":@"team-a",@"name":@"隊伍 A",@"team":@[@0,@0,@0,@0,@0,@0],
            @"dungeon":@0,@"floor":@0,@"settings":profileSettings};
        config[@"orbProfiles"] = @{@"schema":@1,@"selected":@"team-a",@"autoSelect":@NO,@"profiles":@[profile]};
        Check(PHConfigurationIsValid(config), "selected profile and common settings initially agree");
        config[@"orbs"] = @{@"quality":@"fast"};
        Check(!PHConfigurationIsValid(config), "partial writes cannot leave profile and effective settings disagreeing");
        config[@"orbProfiles"] = PHOrbProfilesUpdatingSettings(config[@"orbProfiles"],PHConfigurationOrbSettings(config));
        Check(PHConfigurationIsValid(config), "one config transaction publishes selected profile and settings together");
        NSMutableDictionary *rescueRule = [PHOrbDefaultSkillRule() mutableCopy];
        rescueRule[@"hpAtMost"] = @40; rescueRule[@"status"] = @"awoken_bind"; rescueRule[@"required"] = @YES;
        rescueRule[@"obstacle"] = @"unmatchable"; rescueRule[@"unmatchableColors"] = @40;
        rescueRule[@"intervalMode"] = @"accepted"; rescueRule[@"startAfter"] = @2;
        NSMutableDictionary *rescueSettings = [PHConfigurationOrbSettings(config) mutableCopy]; rescueSettings[@"rules"] = @[rescueRule];
        config[@"orbs"] = rescueSettings;
        Check(!PHConfigurationIsValid(config), "a partial rescue-setting update cannot leave the selected profile stale");
        config[@"orbProfiles"] = PHOrbProfilesUpdatingSettings(config[@"orbProfiles"],rescueSettings);
        serialized = [NSJSONSerialization dataWithJSONObject:config options:0 error:NULL];
        NSDictionary *rescueConfig = [NSJSONSerialization JSONObjectWithData:serialized options:0 error:NULL];
        Check(PHConfigurationIsValid(rescueConfig) && [PHConfigurationOrbSettings(rescueConfig)[@"rules"] isEqual:@[rescueRule]],
            "rescue conditions and retained timing/warmup round-trip with their selected strategy");
        Check([PHConfigurationOrbSettings(rescueConfig)[@"recovery"] isEqual:PHOrbDefaultRecovery()],
            "older configuration and profile settings gain a disabled conditional heart policy");
        NSDictionary *heartPolicy = @{@"hpAtMost":@40, @"groups":@2, @"shape":@"four"};
        NSMutableDictionary *heartSettings = [PHConfigurationOrbSettings(config) mutableCopy]; heartSettings[@"recovery"] = heartPolicy;
        config[@"orbs"] = heartSettings;
        Check(!PHConfigurationIsValid(config), "updating only the common heart policy cannot leave the selected strategy stale");
        config[@"orbProfiles"] = PHOrbProfilesUpdatingSettings(config[@"orbProfiles"],heartSettings);
        serialized = [NSJSONSerialization dataWithJSONObject:config options:0 error:NULL];
        NSDictionary *heartConfig = [NSJSONSerialization JSONObjectWithData:serialized options:0 error:NULL];
        Check(PHConfigurationIsValid(heartConfig) && [PHConfigurationOrbSettings(heartConfig)[@"recovery"] isEqual:heartPolicy] &&
            [PHConfigurationOrbSettings(heartConfig)[@"rules"] isEqual:@[rescueRule]],
            "conditional hearts and skill rescue rules persist in the same atomic profile transaction");
        for (id malformed in @[@YES, @{}, @{@"hpAtMost":@40,@"groups":@0,@"shape":@"match"}]) {
            heartSettings[@"recovery"] = malformed; config[@"orbs"] = heartSettings;
            Check(!PHConfigurationIsValid(config), "malformed heart policies cannot silently revert to disabled recovery");
        }
        heartSettings[@"recovery"] = heartPolicy;
        config[@"orbProfiles"] = @{};
        Check(!PHConfigurationIsValid(config), "malformed profile libraries do not silently discard bindings");
        [config removeObjectForKey:@"orbProfiles"]; [config removeObjectForKey:@"orbs"];
        config[@"features"] = @{@"skin": @YES, @"bgm": @YES, @"timerFix": @NO};
        config[@"apiHosts"] = @[@"example.invalid"];
        config[@"apiPaths"] = @[@"/api.php"];
        Check(PHConfigurationIsValid(config), "GUI switches and observed endpoint produce a valid configuration");
        Check(![config[@"features"][@"battleSwap"] boolValue], "old schema 1 files keep battle replacement off");
        Check(![config[@"features"][@"badge"] boolValue], "old schema 1 files keep badge unlock off");
        Check(![config[@"features"][@"bgmUncleared"] boolValue], "old schema 1 files keep uncleared music disabled");
        for (id choice in @[@YES,@NO,@1,@0,@"true",NSNull.null]) {
            config[@"features"] = @{@"skin":@YES,@"bgm":@YES,@"timerFix":@NO,@"bgmUncleared":choice};
            BOOL valid = [choice isKindOfClass:NSNumber.class] && CFGetTypeID((__bridge CFTypeRef)choice) == CFBooleanGetTypeID();
            Check(PHConfigurationIsValid(config) == valid, "uncleared music is an optional strict Boolean");
        }
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
        NSMutableDictionary *atomic = [PHDefaultConfiguration() mutableCopy];
        atomic[@"formations"] = PHFormationLibraryFromTeam(nil);
        Check(PHConfigurationIsValid(atomic), "formation library can share the config atomic commit with profiles");
        NSData *planData = [NSData dataWithContentsOfFile:@"ios/templates/ishida-uryu-gamewith-574233/team-plan.json"];
        NSDictionary *plan = [NSJSONSerialization JSONObjectWithData:planData options:0 error:NULL];
        NSDictionary *planLibrary = PHOrbProfilesImportingPlan(PHOrbDefaultProfiles(),plan,NULL);
        NSDictionary *planProfile = PHOrbProfileNamed(planLibrary,planLibrary[@"selected"]);
        atomic[@"orbProfiles"] = planLibrary; atomic[@"orbs"] = planProfile[@"settings"];
        atomic[@"formations"] = PHOrbFormationsSelectingProfile(atomic[@"formations"],planProfile);
        NSData *committed = [NSJSONSerialization dataWithJSONObject:atomic options:0 error:NULL];
        NSDictionary *restoredPlan = [NSJSONSerialization JSONObjectWithData:committed options:0 error:NULL];
        Check(PHConfigurationIsValid(restoredPlan) && [restoredPlan isEqual:atomic],
            "one config roundtrip retains the full team, opener, loop and goals together");
        atomic[@"formations"] = @{};
        Check(!PHConfigurationIsValid(atomic), "invalid embedded formation library rejects the full config");
        printf("Bundle identity and configuration tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
