#import "PHOrbProfiles.h"
#import "PHOrbRules.h"
#import "PHOrbPlanning.h"
#import "PHFormationModel.h"
#include <stdio.h>

static unsigned failures;
static void Check(BOOL ok, const char *message) { if (!ok) { fprintf(stderr,"FAIL: %s\n",message); ++failures; } }
static NSDictionary *Settings(void) { return @{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[PHOrbDefaultSkillRule()]}; }
static NSMutableDictionary *Profile(NSString *identifier) {
    return [@{@"id":identifier, @"name":identifier, @"team":@[@0,@0,@0,@0,@0,@0],
        @"dungeon":@0, @"floor":@0, @"settings":Settings()} mutableCopy];
}
static NSDictionary *Library(NSArray *profiles, NSString *selected, BOOL automatic) {
    return @{@"schema":@1, @"profiles":profiles, @"selected":selected, @"autoSelect":@(automatic)};
}
#include "orb_plan_tests.inc"
static void SharingTests(void) {
    NSString *reason = nil;
    NSDictionary *profile = Profile(@"portable");
    NSData *data = PHOrbExportProfileData(profile,&reason);
    NSDictionary *package = data ? [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL] : nil;
    NSDictionary *library = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),data,&reason);
    Check(library && !reason && [package[@"format"] isEqual:@"padcheaper-orb-profile"] &&
        ![library[@"autoSelect"] boolValue] && [library[@"selected"] isEqual:@"portable"], "ordinary profile shares as a versioned file without automatic start");
    Check([PHOrbExportProfileData(library[@"profiles"][0],NULL) isEqual:data], "portable profile preserves canonical settings after roundtrip");
    Check(!PHOrbProfilesImportingData(library,data,&reason) && [reason containsString:@"ID"], "duplicate file import never overwrites existing settings");
    NSDictionary *planProfile = PHOrbProfileFromPlan(ReadPlanFixture(),NULL);
    NSData *planData = PHOrbExportProfileData(planProfile,NULL);
    NSDictionary *planLibrary = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),planData,NULL);
    Check([planLibrary[@"profiles"][0] isEqual:planProfile], "shared full package preserves all formation, opening, rotation and goal data");
    for (NSDictionary *changes in @[@{@"schema":@2},@{@"schema":@YES},@{@"target":@"foreign"},@{@"format":@"other"},
        @{@"apiHosts":@[@"example.invalid"]},@{@"profile":NSNull.null}]) {
        NSMutableDictionary *bad=[package mutableCopy]; [bad addEntriesFromDictionary:changes];
        NSData *encoded=[NSJSONSerialization dataWithJSONObject:bad options:0 error:NULL];
        Check(!PHOrbProfilesImportingData(PHOrbDefaultProfiles(),encoded,NULL), "portable files reject foreign versions, keys and malformed profiles");
    }
    Check(!PHOrbProfilesImportingData(PHOrbDefaultProfiles(),[NSMutableData dataWithLength:131073],NULL),"file import enforces byte limit");
    Check(!PHOrbExportProfileData(nil,NULL),"sharing with no selected profile is a reported error");
    NSMutableDictionary *badProfile=[profile mutableCopy]; badProfile[@"account"]=@42;
    Check(!PHOrbExportProfileData(badProfile,NULL),"export cannot include account or private config fields");
}
int main(void) {
    @autoreleasepool {
        PlanTests();
        SharingTests();
        Check(PHOrbProfilesValid(PHOrbDefaultProfiles()), "default has no saved start intent or automatic selection");
        Check([PHOrbCanonicalSettings(Settings())[@"thornComboPriority"] isEqual:@NO],
            "legacy profile settings keep damage-first thorn ranking");
        Check([PHOrbCanonicalSettings(Settings())[@"allowPoisonWithHeartFour"] isEqual:@NO],
            "legacy profiles retain the strict poison policy");
        for (id invalid in @[@1, @0, @"true", NSNull.null]) {
            NSMutableDictionary *settings = [Settings() mutableCopy]; settings[@"thornComboPriority"] = invalid;
            Check(!PHOrbCanonicalSettings(settings), "thorn priority requires a real JSON Boolean");
            [settings removeObjectForKey:@"thornComboPriority"]; settings[@"allowPoisonWithHeartFour"] = invalid;
            Check(!PHOrbCanonicalSettings(settings), "poison permission requires a real JSON Boolean");
        }
        NSMutableDictionary *generic = Profile(@"generic"), *team = Profile(@"team"), *stage = Profile(@"stage");
        team[@"team"] = @[@100,@101,@102,@103,@104,@0];
        stage[@"dungeon"] = @7; stage[@"floor"] = @2;
        NSArray *profiles = @[generic,team,stage];
        PHOrbSnapshot state = {0}; state.present = state.valid = state.automation.valid = true;
        state.identity.dungeon = 7; state.identity.floor = 2;
        for (NSUInteger i = 0; i < 6; ++i) { state.automation.members[i].active = true; state.automation.members[i].number = (uint16_t)(100+i); }
        NSDictionary *library = Library(profiles,@"",YES);
        Check(PHOrbProfilesValid(library) && [PHOrbResolveProfile(library,&state)[@"profile"][@"id"] isEqual:@"stage"],
            "exact dungeon stage wins over generic and team-only candidates");
        state.identity.floor = 3;
        Check([PHOrbResolveProfile(library,&state)[@"profile"][@"id"] isEqual:@"team"], "team profile is chosen when the stage does not match");
        ++state.automation.members[0].number;
        Check([PHOrbResolveProfile(library,&state)[@"profile"][@"id"] isEqual:@"generic"], "unbound profile is an explicit fallback");
        Check(![PHOrbResolveProfile(Library(@[team,stage],@"",YES),&state)[@"ok"] boolValue], "no match stops instead of using unrelated rules");
        --state.automation.members[0].number;
        NSMutableDictionary *duplicate = [team mutableCopy]; duplicate[@"id"] = @"same-team";
        Check(![PHOrbResolveProfile(Library(@[team,duplicate],@"",YES),&state)[@"ok"] boolValue], "equal specificity is ambiguous, never first-row wins");
        Check([PHOrbResolveProfile(Library(@[team,duplicate],@"same-team",NO),&state)[@"ok"] boolValue], "explicit selection resolves ambiguity");
        state.automation.members[5].number = 999;
        Check([PHOrbResolveProfile(Library(@[team],@"team",NO),&state)[@"ok"] boolValue], "an unbound helper may change");
        state.automation.members[1].active = false;
        Check(![PHOrbResolveProfile(Library(@[team],@"team",NO),&state)[@"ok"] boolValue], "selected profile must match active current members");
        state.automation.members[1].active = true;
        state.automation.valid = false;
        Check(![PHOrbResolveProfile(library,&state)[@"ok"] boolValue], "unknown context does not select even the generic profile");
        state.automation.valid = true; state.identity.multiplayer = 1;
        Check(![PHOrbResolveProfile(library,&state)[@"ok"] boolValue], "profile matching cannot override multiplayer restriction");
        state.identity.multiplayer = 0;
        Check([PHOrbResolveProfile(PHOrbDefaultProfiles(),&state)[@"profile"] count] == 0, "old users continue using their common settings");

        for (NSDictionary *change in @[@{@"id":@"bad/id"}, @{@"name":@"  "}, @{@"name":@YES}, @{@"dungeon":@YES},
            @{@"dungeon":@65536}, @{@"floor":@1}, @{@"team":@[@0]}, @{@"team":@[@YES,@0,@0,@0,@0,@0]},
            @{@"settings":@{@"quality":@"fast",@"mode":@"manual",@"goals":@[],@"rules":@[]}}, @{@"extra":@1}]) {
            NSMutableDictionary *bad = Profile(@"bad"); [bad addEntriesFromDictionary:change];
            Check(!PHOrbProfilesValid(Library(@[bad],@"bad",NO)), "malformed profile cannot be persisted");
        }
        Check(!PHOrbProfilesValid(Library(@[generic,generic],@"",NO)), "duplicate identifiers are refused");
        Check(!PHOrbProfilesValid(Library(profiles,@"missing",NO)), "selected identifier must exist");
        NSMutableDictionary *badLibrary = [library mutableCopy]; badLibrary[@"autoSelect"] = @1;
        Check(!PHOrbProfilesValid(badLibrary), "selection preference requires a real JSON Boolean");
        NSMutableArray *many = [NSMutableArray array];
        for (NSUInteger i = 0; i <= PHOrbMaxProfiles; ++i) [many addObject:Profile([NSString stringWithFormat:@"p%lu",(unsigned long)i])];
        Check(!PHOrbProfilesValid(Library(many,@"",NO)), "profile library is bounded");

        NSDictionary *original = Library(profiles,@"team",NO);
        NSMutableDictionary *settings = [Settings() mutableCopy]; settings[@"quality"] = @"thorough";
        settings[@"thornComboPriority"] = @YES;
        settings[@"allowPoisonWithHeartFour"] = @YES;
        NSDictionary *updated = PHOrbProfilesUpdatingSettings(original,settings);
        Check(PHOrbProfilesValid(updated) && [PHOrbProfileNamed(updated,@"team")[@"settings"][@"quality"] isEqual:@"thorough"] &&
            [PHOrbProfileNamed(original,@"team")[@"settings"][@"quality"] isEqual:@"fast"] &&
            [PHOrbProfileNamed(updated,@"stage") isEqual:stage], "working changes update only the selected immutable profile");
        NSData *json = [NSJSONSerialization dataWithJSONObject:updated options:0 error:NULL];
        NSDictionary *restored = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
        Check(PHOrbProfilesValid(restored) && [restored isEqual:updated] &&
            [PHOrbProfileNamed(restored,@"team")[@"settings"][@"thornComboPriority"] isEqual:@YES] &&
            [PHOrbProfileNamed(restored,@"team")[@"settings"][@"allowPoisonWithHeartFour"] isEqual:@YES],
            "profiles, bindings, thorn priority, poison policy and Boolean rules survive JSON persistence");
        NSMutableDictionary *rescue = [PHOrbDefaultSkillRule() mutableCopy];
        rescue[@"hpAtMost"] = @40; rescue[@"status"] = @"awoken_bind"; rescue[@"required"] = @YES;
        settings[@"rules"] = @[rescue];
        updated = PHOrbProfilesUpdatingSettings(original,settings);
        json = [NSJSONSerialization dataWithJSONObject:updated options:0 error:NULL];
        restored = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
        Check(PHOrbProfilesValid(restored) && [PHOrbProfileNamed(restored,@"team")[@"settings"][@"rules"] isEqual:@[rescue]] &&
            [PHOrbProfileNamed(original,@"team")[@"settings"][@"rules"] isEqual:Settings()[@"rules"]],
            "rescue thresholds, conjunctive status and required preference persist together without mutating the original profile");
        Check([PHOrbCanonicalSettings(Settings())[@"recovery"] isEqual:PHOrbDefaultRecovery()] && PHOrbProfilesValid(original),
            "legacy four-key profile settings remain loadable and normalize to disabled hearts");
        NSDictionary *heartPolicy = @{@"hpAtMost":@35,@"groups":@2,@"shape":@"four"}; settings[@"recovery"] = heartPolicy;
        updated = PHOrbProfilesUpdatingSettings(original,settings);
        json = [NSJSONSerialization dataWithJSONObject:updated options:0 error:NULL];
        restored = [NSJSONSerialization JSONObjectWithData:json options:0 error:NULL];
        Check(PHOrbProfilesValid(restored) && [PHOrbProfileNamed(restored,@"team")[@"settings"][@"recovery"] isEqual:heartPolicy] &&
            !PHOrbProfileNamed(original,@"team")[@"settings"][@"recovery"],
            "heart threshold, shape and independent groups are saved with the chosen strategy without mutating legacy profiles");
        settings[@"recovery"] = @{};
        Check(!PHOrbCanonicalSettings(settings) && !PHOrbProfilesUpdatingSettings(original,settings),
            "malformed conditional hearts cannot be dropped while saving other strategy fields");
        printf("Orb profiles tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
