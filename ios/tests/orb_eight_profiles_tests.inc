#include "orb_eight_fixture.h"
static void EightPreparationProfilesTests(void) {
    NSDictionary *profile = EightGuideProfileFixture(YES), *a = profile[@"activity"];
    PHOrbSnapshot s = {0}; s.board.width = 6; s.board.height = 5;
    EightTeamFixture(&s,profile); s.wave = 5; s.identity.dungeon = 6535; s.identity.floor = 1;
    s.multiplayer.remainingSeconds = 60; s.enemyCombo.turns[0] = 1; s.enemyCombo.thresholds[0] = 5;
    s.automation.members[3].charge = 0; s.automation.members[1].charge = 4; s.automation.members[2].charge = 3;
    const uint8_t cells[30] = {0,1,2,3,4,3, 1,2,3,4,0,1, 2,3,4,2,3,4, 3,4,1,3,2,3, 4,5,4,2,3,1};
    memcpy(s.board.types,cells,sizeof(cells));
    NSDictionary *decision = PHOrbEightPreparationDecision(a,&s,0);
    Check([decision[@"available"] boolValue] && ![decision[@"readyNow"] boolValue] && [decision[@"sequence"] isEqual:@[@2,@3]] &&
        [decision[@"conversion"] isEqual:@[@0,@0,@4,@0,@4,@4]],"preparation projects the currently verified skill conversions and one-turn CD");
    PHOrbSnapshot charged = s; charged.automation.members[2].charge = 4;
    Check([PHOrbEightPreparationDecision(a,&charged,0)[@"readyNow"] boolValue],
        "already-ready conversion pair does not need the preferred charge turn");
    Check(![decision[@"scoreTurn"] boolValue],"profiles without scoreTurn keep the three-orb waiting turn");
    {
        NSDictionary *scoring = EightScoreTurnProfileFixture()[@"activity"];
        Check(PHOrbEightActivityValid(scoring) && [PHOrbEightPreparationDecision(scoring,&s,0)[@"scoreTurn"] boolValue],
            "a pair that turns every colour into fire/dark makes the waiting turn a scoring turn");
        PHOrbSnapshot partial = s; partial.eight.members[2].convert[3] = 3; // light stays light
        NSDictionary *partialDecision = PHOrbEightPreparationDecision(scoring,&partial,0);
        Check(!partialDecision || ![partialDecision[@"scoreTurn"] boolValue],
            "a conversion that leaves a non-attack colour keeps the orb-saving three-orb route");
        for (id value in @[@1,@"true",NSNull.null]) {
            NSMutableDictionary *bad = [scoring mutableCopy], *g = [scoring[@"guide"] mutableCopy], *p = [g[@"preparation"] mutableCopy];
            p[@"scoreTurn"] = value; g[@"preparation"] = p; bad[@"guide"] = g;
            Check(!PHOrbEightActivityValid(bad),"scoreTurn requires an explicit JSON Boolean");
        }
    }
    PHOrbSnapshot dense = s;
    for (unsigned i = 0; i < 30; ++i) dense.board.types[i] = i < 15 ? 0 : 4;
    Check(![PHOrbEightPreparationDecision(a,&dense,0)[@"preferable"] boolValue],
        "an already-dense attack board does not spend a preparation turn merely to follow a recipe");
    for (unsigned bad = 0; bad < 12; ++bad) {
        PHOrbSnapshot changed = s;
        if (bad == 0) changed.identity.dungeon++;
        if (bad == 1) changed.wave = 4;
        if (bad == 2) changed.enemyCombo.turns[0] = 2;
        if (bad == 3) changed.enemyCombo.turns[0] = 0;
        if (bad == 4) changed.automation.members[2].charge = 2;
        if (bad == 5) changed.automation.members[3].charge = 6;
        if (bad == 6) changed.board.flags[0] = PHOrbFlagLocked;
        if (bad == 7) changed.automation.members[2].bound = true;
        if (bad == 8) changed.multiplayer.remainingSeconds = 14;
        if (bad == 9) changed.eight.members[2].skillKnown = false;
        if (bad == 10) changed.enemyCombo.valid = false;
        if (bad == 11) changed.board.types[0] = PHOrbBomb;
        Check(![PHOrbEightPreparationDecision(a,&changed,0)[@"available"] boolValue],
            "wrong match, absorption, cooldown, native effects, timing and board hazards cannot use the preparation recipe");
    }
    Check(![PHOrbEightPreparationDecision(EightGuideProfileFixture(NO)[@"activity"],&s,0)[@"available"] boolValue] &&
        ![PHOrbEightPreparationDecision(a,&s,4)[@"available"] boolValue],"disabled guide and attempted conversion cannot start preparation");
    NSDictionary *blocked = PHOrbEightGuideDecision(a,&s,0,0);
    Check(![blocked[@"available"] boolValue] && [blocked[@"message"] containsString:@"轉色後珠數不足"] &&
        [blocked[@"message"] containsString:@"CD"],"guide reports inventory and cooldown blockers separately at decision time");
    NSMutableDictionary *legacy = [a mutableCopy], *guide = [a[@"guide"] mutableCopy];
    [guide removeObjectForKey:@"preparation"]; legacy[@"guide"] = guide;
    Check(PHOrbEightActivityValid(legacy) && !PHOrbEightPreparationDecision(legacy,&s,0),"older activity imports do not gain a new delay recipe silently");
    for (NSString *key in @[@"spentSlot",@"floor",@"minimumSeconds",@"primaryMinimum",@"primaryColor"]) {
        NSMutableDictionary *bad = [a mutableCopy], *g = [a[@"guide"] mutableCopy], *p = [g[@"preparation"] mutableCopy];
        p[key] = @1000; g[@"preparation"] = p; bad[@"guide"] = g;
        Check(!PHOrbEightActivityValid(bad),"preparation recipe rejects invalid bounds");
    }
    for (id value in @[@1,@"true",NSNull.null]) {
        NSMutableDictionary *bad = [a mutableCopy], *g = [a[@"guide"] mutableCopy], *p = [g[@"preparation"] mutableCopy];
        p[@"preferBeforeOutput"] = value; g[@"preparation"] = p; bad[@"guide"] = g;
        Check(!PHOrbEightActivityValid(bad),"preferred preparation requires an explicit JSON Boolean");
    }
    Check(PHOrbEightActivityValid(EightPreparationProfileFixture(NO)[@"activity"]) &&
        ![EightPreparationProfileFixture(NO)[@"activity"][@"guide"][@"preparation"][@"preferBeforeOutput"] boolValue],
        "older preparation JSON retains output-first behavior");
    for (unsigned bad = 0; bad < 4; ++bad) {
        NSMutableDictionary *changed = [a mutableCopy]; NSMutableArray *floors = [a[@"floors"] mutableCopy];
        NSMutableDictionary *floor = [floors[1] mutableCopy], *attack = [floor[@"attack"] mutableCopy];
        if (bad == 0) attack[@"weights"] = @[@3,@0];
        if (bad == 1) attack[@"rowPercent"] = @[@540,@0,@0,@0,@540,@10001];
        if (bad == 2) attack[@"linkMultiplier"] = @0;
        if (bad == 3) attack[@"linkComboBonus"] = @21;
        floor[@"attack"] = attack; floors[1] = floor; changed[@"floors"] = floors;
        Check(!PHOrbEightActivityValid(changed),"relative attack ranking validates every bounded assumption");
    }
}
static void EightGuideProfilesTests(void) {
    NSDictionary *profile = EightGuideProfileFixture(YES), *activity = profile[@"activity"];
    NSDictionary *library = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),PHOrbExportProfileData(profile,NULL),NULL);
    Check(PHOrbEightGuideEnabled(library[@"profiles"][0][@"activity"]) &&
        [library[@"profiles"][0][@"activity"] isEqual:activity],"guide switch and ordered branches survive portable export/import");
    NSMutableDictionary *legacy = [activity mutableCopy]; [legacy removeObjectForKey:@"guide"];
    NSMutableArray *floors = [legacy[@"floors"] mutableCopy]; NSMutableDictionary *first = [floors[0] mutableCopy];
    [first removeObjectForKey:@"comboPriority"]; floors[0] = first; legacy[@"floors"] = floors;
    Check(PHOrbEightActivityValid(legacy) && !PHOrbEightGuideEnabled(legacy),"legacy activity remains valid and cannot silently enable guide skills");
    for (NSDictionary *bad in @[@{@"enabled":@YES,@"floors":@[]},
        @{@"enabled":@YES,@"floors":activity[@"guide"][@"floors"],@"script":@"x"}]) {
        NSMutableDictionary *invalid = [activity mutableCopy]; invalid[@"guide"] = bad;
        Check(!PHOrbEightActivityValid(invalid),"guide must cover exactly the activity floors and cannot contain extra instructions");
    }
    for (id bad in @[@1,@0,@"true",NSNull.null]) {
        NSMutableDictionary *invalid = [activity mutableCopy], *guide = [activity[@"guide"] mutableCopy];
        guide[@"enabled"] = bad; invalid[@"guide"] = guide;
        Check(!PHOrbEightActivityValid(invalid),"guide permission requires an actual JSON Boolean");
        invalid = [activity mutableCopy]; first = [floors[0] mutableCopy]; first[@"comboPriority"] = bad;
        NSMutableArray *invalidFloors = [floors mutableCopy]; invalidFloors[0] = first; invalid[@"floors"] = invalidFloors;
        Check(!PHOrbEightActivityValid(invalid),"Combo priority rejects malformed Boolean values");
    }
    for (NSDictionary *bad in @[@{@"slots":@[@2,@2],@"requireInventory":@NO},
        @{@"slots":@[@5],@"requireInventory":@NO},@{@"slots":@[@0],@"requireInventory":@NO},
        @{@"slots":@[@2],@"requireInventory":@1},@{@"slots":@[@2],@"requireInventory":@NO,@"script":@"x"}]) {
        NSMutableDictionary *invalid = [activity mutableCopy], *guide = [activity[@"guide"] mutableCopy];
        NSMutableArray *guideFloors = [guide[@"floors"] mutableCopy]; guideFloors[1] = @[bad]; guide[@"floors"] = guideFloors;
        invalid[@"guide"] = guide; Check(!PHOrbEightActivityValid(invalid),"guide rejects duplicate, opponent, invalid slots and extra instructions");
    }
    PHOrbSnapshot s = {0}; s.board.width = 6; s.board.height = 5; EightTeamFixture(&s,profile);
    for (unsigned i = 0; i < 30; ++i) s.board.types[i] = i < 15 ? 0 : 4;
    Check(!PHOrbEightGuideDecision(EightGuideProfileFixture(NO)[@"activity"],&s,0,0),"disabled guide does not request any opener");
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] count] == 0,"B1 maximizes Combo without a guide skill");
    for (unsigned wave = 1; wave <= 3; ++wave) {
        s.wave = wave; unsigned slot = wave == 3 ? 1 : wave + 1;
        Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@(slot)]],
            "B2/B3/B4 opener runs even when the board already meets its match goal");
    }
    s.wave = 1; s.automation.members[1].charge = 0;
    Check(![PHOrbEightGuideDecision(activity,&s,0,0)[@"available"] boolValue],"required unavailable guide skill cannot silently become a puzzle");
    Check([PHOrbEightGuideDecision(activity,&s,2,2)[@"sequence"] count] == 0,"confirmed guide skill survives cooldown and attempted mask");
    EightTeamFixture(&s,profile); s.wave = 3; s.automation.members[0].charge = 0;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@6]],"B4 can use the independently ready second Sonia");
    s.automation.members[0].charge = 20;
    Check([PHOrbEightGuideDecision(activity,&s,32,32)[@"sequence"] count] == 0,"completed alternative prevents spending the other Sonia after profile changes");
    s.wave = 4;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@4,@6]],
        "B5 prioritizes Ney then Sonia even when generation alone could meet the minimum link goal");
    s.automation.members[5].charge = 0;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@4,@1]],
        "B5 chooses the other ready Sonia after Ney before a weaker standalone opener");
    s.automation.members[5].charge = 20; s.automation.members[3].charge = 0;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@6]],
        "B5 still permits adequate standalone generation when Ney is not ready");
    s.automation.members[3].charge = 20;
    for (unsigned i = 0; i < 30; ++i) s.board.types[i] = i % 6;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@4,@6]],"B5 uses Ney then Sonia if generation alone lacks twelve");
    s.automation.members[3].charge = 0;
    Check([PHOrbEightGuideDecision(activity,&s,8,8)[@"sequence"] isEqual:@[@6]],"paused B5 resumes only the remainder of the confirmed prefix");
    // Synthetic equivalent inventory exercises both missing B5 rescue slots.
    s.board.types[1] = 0; // Six fire, five dark, four water.
    s.automation.members[5].charge = 0; NSUInteger cursor = 0;
    Check([PHOrbEightSkillSequence(activity,&s,0,&cursor) isEqual:@[@1]],"B5 can rescue with independent first Sonia while Ney and second Sonia recharge");
    s.automation.members[0].charge = 0;
    for (unsigned i = 0; i < 30; ++i) s.board.types[i] = i < 6 ? 0 : i < 13 ? 1 : i < 18 ? 4 : 5;
    cursor = 0;
    Check([PHOrbEightSkillSequence(activity,&s,0,&cursor) isEqual:@[@2]],"B5 can rescue six fire plus seven water with Kagutsuchi alone");
    EightTeamFixture(&s,profile); s.wave = 5;
    s.enemyCombo.turns[0] = 1; s.enemyCombo.thresholds[0] = 5;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@4,@2]],"B6 uses retained Ney before Kagutsuchi");
    s.automation.members[3].charge = 0;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@2,@3]],
        "B6 prioritizes both ready conversions after spent Ney even when Kagutsuchi alone has twelve");
    s.automation.members[2].charge = 3;
    Check([PHOrbEightGuideDecision(activity,&s,0,0)[@"sequence"] isEqual:@[@2]],
        "B6 direct fallback remains available when the second conversion is not ready");
    s.automation.members[3].charge = 20;
    Check([PHOrbEightGuideDecision(activity,&s,2,2)[@"sequence"] count] == 0,"confirmed Kagutsuchi cannot be followed by an out-of-order Ney");
    s.automation.members[1].charge = s.automation.members[3].charge = 0;
    Check(![PHOrbEightGuideDecision(activity,&s,0,0)[@"available"] boolValue],"B6 never creates an unsupported wait turn for unavailable guide skills");
}
static void EightProfilesTests(void) {
    EightPreparationProfilesTests();
    EightGuideProfilesTests();
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
