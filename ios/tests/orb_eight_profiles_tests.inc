#include "orb_eight_fixture.h"
static void EightProfilesTests(void) {
    NSDictionary *profile = EightProfileFixture();
    Check(profile && PHOrbEightActivityValid(profile[@"activity"]),"portable 8P activity validates independently of ordinary team skills");
    NSData *data = PHOrbExportProfileData(profile,NULL);
    NSDictionary *library = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),data,NULL);
    Check(library && [PHOrbProfileNamed(library,profile[@"id"])[@"activity"] isEqual:profile[@"activity"]],
        "activity semantics, weights and ordered alternatives survive export/import");
    NSMutableDictionary *invalid = [profile mutableCopy], *activity = [profile[@"activity"] mutableCopy];
    activity[@"script"] = @"auto()"; invalid[@"activity"] = activity;
    Check(!PHOrbProfilesValid(Library(@[invalid],@"",NO)),"unknown activity instructions cannot be silently accepted");
    activity = [profile[@"activity"] mutableCopy]; activity[@"mode"] = @4; invalid[@"activity"] = activity;
    Check(!PHOrbProfilesValid(Library(@[invalid],@"",NO)),"activity config cannot authorize 4P Auto");
    activity = [profile[@"activity"] mutableCopy];
    NSMutableArray *floors = [activity[@"floors"] mutableCopy]; NSMutableDictionary *floor = [floors[0] mutableCopy];
    floor[@"sequences"] = @[@[@5]]; floors[0] = floor; activity[@"floors"] = floors; invalid[@"activity"] = activity;
    Check(!PHOrbProfilesValid(Library(@[invalid],@"",NO)),"opponent-only observe slot cannot enter an automatic skill sequence");
    Check(!PHOrbProfilesUpdatingSettings(library,Settings()),"solo skill rules cannot be saved over an activity profile");
    PHOrbSnapshot s = {0};
    s.valid = s.present = s.settled = s.ordinary = true; s.phase = 2; s.identity.entryMode = 10; s.identity.multiplayer = 1;
    s.multiplayer.kind = PHOrbMultiplayerEight; s.multiplayer.known = s.multiplayer.clockKnown = true;
    s.multiplayer.teamCount = 8; s.multiplayer.localStatus = 3; s.multiplayer.connectionState = 2; s.multiplayer.networkFlags = 1;
    s.multiplayer.clockStart = 9; s.multiplayer.clockFlags = 4; s.multiplayer.countdownReady = 1; s.multiplayer.remainingSeconds = 60;
    s.board.width = 6; s.board.height = 5;
    EightTeamFixture(&s,profile);
    Check(PHOrbEightActivityMatches(profile,&s,NULL),"all six fixed variants and both leader effects authorize selected activity");
    s.eight.members[2].catalogID %= 1000000;
    Check(!PHOrbEightActivityMatches(profile,&s,NULL),"ordinary catalog number cannot impersonate the fixed team variant");
    EightTeamFixture(&s,profile); s.eight.members[1].convert[5] = 5;
    Check(!PHOrbEightActivityMatches(profile,&s,NULL),"one wrong actual conversion prevents the cup strategy");
    EightTeamFixture(&s,profile); s.wave = 5;
    s.enemyCombo.turns[0] = 1; s.enemyCombo.thresholds[0] = 5;
    NSDictionary *plan = PHOrbEightPlan(profile[@"activity"],&s);
    PHOrbGoalSet goals = {0}; PHOrbReadGoals(plan[@"goals"],&goals);
    Check([plan[@"leaderAddedCombos"] isEqual:@2] && [plan[@"combatComboRequired"] isEqual:@6] &&
        goals.items[1].kind == PHOrbGoalCombo && goals.items[1].count == 4,
        "B6 uses four actual combos plus one bonus per confirmed leader");
    s.automation.members[5].bound = true; plan = PHOrbEightPlan(profile[@"activity"],&s);
    PHOrbReadGoals(plan[@"goals"],&goals);
    Check([plan[@"leaderAddedCombos"] isEqual:@1] && goals.items[1].count == 5,"bound helper grants no bonus");
    s.enemyCombo.turns[0] = 0; plan = PHOrbEightPlan(profile[@"activity"],&s); PHOrbReadGoals(plan[@"goals"],&goals);
    Check(goals.count == 1,"expired absorption is reread and removes its combo requirement");
}
