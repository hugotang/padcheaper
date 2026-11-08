// Data and policy regression cases only. These do not model enemy turns,
// native haste timing, completed stage effects or a dungeon clear.
static NSMutableDictionary *ReadHimmelPlanFile(NSString *name) {
    NSString *path = [@"ios/templates/himmel-gamewith-545672/" stringByAppendingString:name];
    NSData *data = [NSData dataWithContentsOfFile:path];
    return data ? [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL] : nil;
}

static void HimmelOpeningIdentityTests(NSDictionary *plan, NSDictionary *profile) {
    NSArray *opening = plan[@"opening"];
    const unsigned slots[] = {3,2,1,4,5,4,6};
    const unsigned bodies[] = {13919,14084,13412,13922,13922,13922,13922};
    const unsigned assists[] = {13391,13402,14031,13853,13617,0,13437};
    const unsigned skills[] = {60147,60172,60246,63375,61750,65369,60592};
    const bool consumed[] = {true,true,false,true,true,false,true};
    const bool bodySource[] = {false,false,true,false,false,true,false};
    Check(opening.count == 7,"Himmel keeps the seven source-reviewed opening skills without inserted puzzle steps");
    if (opening.count != 7) return;
    PHOrbSnapshot state = {0}; state.valid = state.automation.valid = true;
    for (unsigned i = 0; i < 6; ++i) {
        PHOrbSkillMember *member = &state.automation.members[i];
        member->active = true; member->assistState = 172;
        member->number = [profile[@"team"][i] unsignedIntValue];
        Check(PHBattleCatalogIDForNumber([plan[@"formation"][@"slots"][i][@"assist"][@"id"] unsignedIntValue],
            &member->assistCatalogID),"Himmel opening equipment belongs to the pinned native catalog mapping");
        member->sourceCatalogIDs[1] = member->assistCatalogID;
    }
    Check(PHOrbProfileMatchesOpeningTeam(profile,&state,0),"Himmel opening binds all six initial bodies and assists");
    unsigned consumedCount = 0;
    for (unsigned i = 0; i < 7; ++i) {
        NSDictionary *step = opening[i];
        Check([step[@"slot"] unsignedIntValue] == slots[i] && [step[@"monster"] unsignedIntValue] == bodies[i] &&
            [step[@"assist"] unsignedIntValue] == assists[i] && [step[@"skill"] unsignedIntValue] == skills[i] &&
            [step[@"source"] isEqual:bodySource[i] ? @"body" : @"assist"] &&
            [step[@"consumeAssist"] boolValue] == consumed[i] &&
            [step[@"afterMonster"] unsignedIntValue] == (i == 2 ? 13413u : 0u) && ![step[@"puzzleAfter"] boolValue],
            "Himmel opening preserves its body/assist sources, five consumption steps and sole transformation");
        PHOrbSkillMember *member = &state.automation.members[slots[i] - 1];
        if (i == 2) member->number = 13413;
        if (consumed[i]) {
            ++consumedCount;
            const uint16_t source = member->assistCatalogID;
            member->assistCatalogID = 0;
            Check(!PHOrbProfileMatchesOpeningTeam(profile,&state,i + 1),
                "a missing Himmel weapon is insufficient without its native consumption state");
            member->assistState = 0;
            ++member->sourceCatalogIDs[1];
            Check(!PHOrbProfileMatchesOpeningTeam(profile,&state,i + 1),
                "Himmel consumption still binds the exact original weapon source");
            member->sourceCatalogIDs[1] = source;
        }
        Check(PHOrbProfileMatchesOpeningTeam(profile,&state,i + 1),
            "each confirmed Himmel opening prefix preserves the expected six-slot identity");
    }
    Check(consumedCount == 5 && PHOrbProfileMatchesRunningTeam(profile,&state),
        "Himmel running-team binding accepts exactly five consumed assists after all seven steps");
    for (unsigned i = 0; i < 6; ++i) Check(
        state.automation.members[i].number == (i == 0 ? 13413u : i == 1 ? 14084u : i == 2 ? 13919u : 13922u) &&
        (i == 0 ? state.automation.members[i].assistCatalogID != 0 :
            !state.automation.members[i].assistCatalogID && !state.automation.members[i].assistState),
        "the transformed leader retains its weapon while all five other slots consume theirs");
    ++state.automation.members[5].sourceCatalogIDs[1];
    Check(!PHOrbProfileMatchesRunningTeam(profile,&state),
        "the helper's consumed source remains part of the running-team identity");
}

static void HimmelRotationTests(NSDictionary *profile) {
    NSDictionary *settings = profile[@"settings"];
    NSArray *rules = settings[@"rules"];
    Check(rules.count == 9,"Himmel has nine declarative rules within the existing twelve-rule limit");
    if (rules.count != 9) return;
    NSArray *enemyConditions = @[@"damage_absorb",@"attribute_absorb",@"damage_void"];
    for (unsigned i = 0; i < 9; ++i) {
        NSDictionary *rule = rules[i];
        Check([rule[@"enabled"] boolValue] && [rule[@"source"] isEqual:@"body"] &&
            [rule[@"intervalMode"] isEqual:@"accepted"] && [rule[@"every"] isEqual:@1] &&
            ![rule[@"floor"] unsignedIntValue] && ![rule[@"reserveUntil"] unsignedIntValue] && ![rule[@"once"] boolValue],
            "Himmel rotation uses accepted native starts without an invented dungeon schedule");
        if (i < 4) Check([rule[@"slot"] isEqual:@1] && [rule[@"monster"] isEqual:@13413] &&
            [rule[@"skill"] isEqual:@60253] && [rule[@"group"] isEqual:@2] &&
            ![rule[@"startAfter"] unsignedIntValue] && [rule[@"required"] boolValue] == (i < 3) &&
            [rule[@"enemy"] isEqual:i < 3 ? enemyConditions[i] : @"none"],
            "three independent required Himmel bypass conditions share the optional damage-cap loop group");
        if (i == 4) Check([rule[@"slot"] isEqual:@2] && [rule[@"monster"] isEqual:@14084] &&
            [rule[@"skill"] isEqual:@0] && ![rule[@"required"] boolValue] && ![rule[@"startAfter"] unsignedIntValue],
            "Gerard keeps native evolving-skill selection instead of pinning its first stage forever");
        if (i == 5) Check([rule[@"slot"] isEqual:@3] && [rule[@"monster"] isEqual:@13919] &&
            [rule[@"skill"] isEqual:@65361] && ![rule[@"required"] boolValue] && ![rule[@"startAfter"] unsignedIntValue],
            "Indra's ordinary loop remains optional and bound to the reviewed body skill");
        if (i >= 6) {
            const unsigned order[] = {5,4,6};
            Check([rule[@"slot"] unsignedIntValue] == order[i - 6] && [rule[@"monster"] isEqual:@13922] &&
                [rule[@"skill"] isEqual:@65369] && [rule[@"group"] isEqual:@1] &&
                ![rule[@"required"] boolValue] && [rule[@"startAfter"] isEqual:@1],
                "optional Kali supply starts after the opening puzzle and rotates slots five, four, then helper");
        }
    }
    PHOrbSnapshot state = {0}; state.valid = state.automation.valid = state.automation.enemiesKnown = true;
    for (unsigned i = 0; i < 6; ++i) {
        state.automation.members[i].active = true;
        state.automation.members[i].number = i == 0 ? 13413 : i == 1 ? 14084 : i == 2 ? 13919 : 13922;
    }
    for (unsigned i = 0; i < 3; ++i) {
        state.automation.enemyEffects = (uint8_t)(1u << i); state.automation.playerBypass = 0;
        for (unsigned j = 0; j < 3; ++j) Check(
            PHOrbTriggerSkillRuleWithTiming(rules[j],&state,1,1,0,NULL) == (i == j ? PHOrbRuleYes : PHOrbRuleNo),
            "each Himmel rescue reacts to its own enemy effect without requiring all three effects together");
        state.automation.playerBypass = (uint8_t)(1u << i);
        Check(PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,1,0,NULL) == PHOrbRuleNo,
            "already-active team bypass suppresses the matching required rescue");
    }
    state.automation.enemyEffects = state.automation.playerBypass = 0;
    for (unsigned i = 6; i < 9; ++i) {
        Check(PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,1,0,NULL) == PHOrbRuleNo &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,2,0,NULL) == PHOrbRuleYes &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,2,2,NULL) == PHOrbRuleNo &&
            PHOrbTriggerSkillRuleWithTiming(rules[i],&state,1,3,2,NULL) == PHOrbRuleYes,
            "Kali waits through exactly the first observed puzzle and retains accepted-use timing after stop/restart");
    }
    // These are trigger checks, not claims that cooldown, native skill
    // readiness, damage-cap uptime or the complete dungeon are verified.
}

static void HimmelPlanRejectionTests(void) {
    NSMutableDictionary *bad = ReadHimmelPlanFile(@"team-plan.json");
    bad[@"waitTurns"] = @1;
    Check(!PHOrbProfileFromPlan(bad,NULL),"Himmel plans cannot add unsupported top-level waiting instructions");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"opening"][0][@"waitTurns"] = @1;
    Check(!PHOrbProfileFromPlan(bad,NULL),"Himmel opening cannot insert an unsupported wait between skills");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"opening"][0][@"puzzleAfter"] = @YES;
    Check(!PHOrbProfileFromPlan(bad,NULL),"Himmel cannot silently turn its first consuming skill into a middle puzzle step");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"opening"][5][@"assist"] = @13853;
    Check(!PHOrbProfileFromPlan(bad,NULL),"slot four's later body skill must identify its already-consumed assist as zero");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"opening"][2][@"consumeAssist"] = @YES;
    Check(!PHOrbProfileFromPlan(bad,NULL),"Himmel's body transformation cannot also declare assist consumption");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"opening"][6][@"consumeAssist"] = @1;
    Check(!PHOrbProfileFromPlan(bad,NULL),"helper assist consumption requires a JSON Boolean");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"rotation"][0][@"effectTurns"] = @3;
    Check(!PHOrbProfileFromPlan(bad,NULL),"unmodeled effect-duration conditions cannot enter the executable rotation");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"board"][@"autoFishing"] = @YES;
    Check(!PHOrbProfileFromPlan(bad,NULL),"a Himmel template cannot introduce an unsupported fishing permission");
    bad = ReadHimmelPlanFile(@"team-plan.json"); bad[@"formation"][@"badge"] = @256;
    Check(!PHOrbProfileFromPlan(bad,NULL),"Himmel badge data remains within the existing formation byte range");
}

static void HimmelPlanTests(void) {
    NSDictionary *formation = ReadHimmelPlanFile(@"formation.json");
    NSDictionary *plan = ReadHimmelPlanFile(@"team-plan.json");
    NSString *reason = nil;
    NSDictionary *profile = PHOrbProfileFromPlan(plan,&reason);
    Check(formation && plan && profile && !reason,
        "Himmel formation and complete plan parse through the current production import schema");
    if (!formation || !plan || !profile) return;
    const unsigned bodies[] = {13412,14084,13919,13922,13922,13922};
    const unsigned assists[] = {14031,13402,13391,13853,13617,13437};
    Check([plan[@"target"] isEqual:@(PHTargetID)] && [formation isEqual:plan[@"formation"]] &&
        [formation[@"badge"] isEqual:@89] && ![plan[@"dungeon"] unsignedIntValue] && ![plan[@"floor"] unsignedIntValue],
        "Himmel full package preserves the reviewed formation and badge without binding an invented dungeon");
    for (unsigned i = 0; i < 6; ++i) Check(
        [formation[@"slots"][i][@"body"][@"id"] unsignedIntValue] == bodies[i] &&
        [formation[@"slots"][i][@"assist"][@"id"] unsignedIntValue] == assists[i] &&
        [formation[@"slots"][i][@"body"][@"superAwakening"] unsignedIntValue] == (i ? 139u : 142u),
        "Himmel's six bodies, assists and super/sync awakenings retain article-selected identities");
    NSDictionary *roundtrip = PHOrbPlanFromProfile(profile);
    Check([PHOrbProfileFromPlan(roundtrip,NULL) isEqual:profile] && [roundtrip[@"formation"][@"badge"] isEqual:@89],
        "Himmel complete-plan roundtrip retains its badge, ordered opening and canonical rules");
    NSDictionary *savedFormations = PHOrbFormationsSelectingProfile(PHFormationLibraryFromTeam(nil),profile);
    NSDictionary *selectedFormation = nil; BOOL badgeSpecified = NO; uint8_t badge = 0;
    Check(PHFormationReadLibrary(savedFormations,&selectedFormation,NULL) &&
        [selectedFormation isEqual:formation] && PHFormationReadBadge(selectedFormation,&badgeSpecified,&badge) &&
        badgeSpecified && badge == 89,
        "selecting the complete Himmel profile retains badge 89 in its saved formation without native writes");
    NSData *shared = PHOrbExportProfileData(profile,NULL);
    NSDictionary *imported = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),shared,NULL);
    Check(shared.length && shared.length <= 131072 && [imported[@"profiles"] count] == 1 &&
        [imported[@"profiles"][0] isEqual:profile] && ![imported[@"autoSelect"] boolValue] &&
        ![imported[@"eightAutoStart"] boolValue],
        "Himmel sharing imports one selected profile without enabling automatic selection or match-start intent");
    Check(!PHOrbProfilesImportingData(imported,shared,NULL),"a repeated Himmel import cannot overwrite the user's saved strategy");
    NSDictionary *settings = profile[@"settings"];
    PHOrbGoalSet goals = {0};
    Check([settings[@"quality"] isEqual:@"thorough"] && [settings[@"mode"] isEqual:@"manual"] &&
        ![settings[@"assistRecoveryL"] boolValue] && [settings[@"diagonal"] boolValue] &&
        ![settings[@"allowPoisonWithHeartFour"] boolValue] && PHOrbReadGoals(settings[@"goals"],&goals) &&
        goals.count == 2 && goals.items[0].kind == PHOrbGoalColors && goals.items[0].colors == 31 &&
        goals.items[0].count == 5 && goals.items[1].kind == PHOrbGoalMatch && goals.items[1].colors == 32 &&
        goals.items[1].count == 1 && goals.items[1].length == 3,
        "Himmel retains five attack colors plus a heart match without inheriting unsupported assist recovery, poison or exact-four policies");
    HimmelOpeningIdentityTests(plan,profile);
    HimmelRotationTests(profile);
    HimmelPlanRejectionTests();

    NSDictionary *post = ReadHimmelPlanFile(@"post-opening.json");
    NSData *postData = post ? [NSJSONSerialization dataWithJSONObject:post options:0 error:NULL] : nil;
    NSDictionary *postLibrary = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),postData,NULL);
    NSDictionary *postProfile = [postLibrary[@"profiles"] count] == 1 ? postLibrary[@"profiles"][0] : nil;
    Check([post[@"format"] isEqual:@"padcheaper-orb-profile"] && [post[@"target"] isEqual:@(PHTargetID)] &&
        postProfile && !postProfile[@"formation"] && !postProfile[@"opening"] &&
        [postProfile[@"team"] isEqual:@[@13413,@14084,@13919,@13922,@13922,@13922]] &&
        [postProfile[@"settings"] isEqual:settings] && ![postLibrary[@"autoSelect"] boolValue],
        "manual-opening Himmel profile binds the transformed team and same first-puzzle policy without replaying skills");
    NSData *postShared = PHOrbExportProfileData(postProfile,NULL);
    NSDictionary *postAgain = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),postShared,NULL);
    Check(postShared && [postAgain[@"profiles"][0] isEqual:postProfile],
        "the portable post-opening Himmel profile has its own exact import/export roundtrip");
}
