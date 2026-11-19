// Schema and pure policy cases only; running these awaits explicit build/test.
static NSMutableDictionary *FixedFixture(void) {
    NSMutableArray *members = [NSMutableArray array];
    for (unsigned i = 0; i < 6; ++i) [members addObject:@{@"body":@100,@"assist":@0,
        @"forms":@[@{@"number":@100,@"skills":@[@700]},@{@"number":@101,@"skills":@[@701]}],@"assistSkills":@[]}];
    NSDictionary *action = @{@"kind":@"skill",@"slot":@1,@"source":@"body",@"monster":@100,@"skill":@700,
        @"afterMonster":@101,@"optional":@NO,@"frequency":@"once",@"when":@"always"};
    NSDictionary *stage = @{@"id":@"attack",@"actions":@[action],
        @"board":@{@"policy":@"six_colors_fours",@"assistRecoveryL":@YES,@"maxGestureMS":@4500},
        @"repeat":@{@"minimumPuzzles":@1,@"maximumPuzzles":@30,@"until":@{@"kind":@"floor_change"}}};
    NSDictionary *plan = @{@"schema":@1,@"members":members,@"floors":@[@{@"number":@1,
        @"variants":@[@{@"kind":@"any",@"stages":@[stage]}]}]};
    NSDictionary *profile = @{@"id":@"fixed-fixture",@"name":@"固定隊伍",@"team":@[@100,@100,@100,@100,@100,@100],
        @"dungeon":@0,@"floor":@0,@"source":@"https://example.invalid/fixed",@"fixedPlan":plan,
        @"settings":@{@"quality":@"thorough",@"mode":@"combo",@"goals":@[],@"rules":@[]}};
    NSData *data = [NSJSONSerialization dataWithJSONObject:@{@"format":@"padcheaper-fixed-plan",@"schema":@1,
        @"target":@(PHTargetID),@"profile":profile} options:0 error:NULL];
    return [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL];
}
static void FixedProfilesTests(void) {
    NSData *coralData=[NSData dataWithContentsOfFile:@"ios/templates/coral-fixed-gamewith-585607/fixed-plan.json"];
    NSDictionary *coralPackage=coralData ? [NSJSONSerialization JSONObjectWithData:coralData options:0 error:NULL] : nil;
    NSDictionary *coral=PHOrbFixedPlanFromPackage(coralPackage,NULL);
    Check(coral && [coral[@"fixedPlan"][@"floors"] count]==15 &&
        [coral[@"fixedPlan"][@"leaderSwap"] isEqual:@5],"actual Coral package imports all fifteen floors and declared swap");
    if (coral) {
        NSDictionary *bodySkills=@{@13828:@[@66716,@66718],@13876:@[@66724],@13744:@[@66735],
            @13745:@[@66740],@13878:@[@66729],@8476:@[@66700],@8477:@[@66706]};
        NSDictionary *assistSkills=@{@13876:@[@66752],@13878:@[@66755]};
        NSDictionary *ordinarySkills=@{@66716:@63191,@66718:@63192,@66724:@63220,@66735:@62494,
            @66740:@62502,@66729:@63241,@66700:@27820,@66706:@27827,@66752:@61754,@66755:@63251};
        Check([coral[@"id"] isEqual:@"coral-fixed-585607-v90-r6"] &&
            [coral[@"dungeon"] isEqual:@7801] && [coral[@"floor"] isEqual:@1],
            "Coral guide-reviewed revision has a distinct ID and the observed native dungeon and sub-dungeon");
        NSDictionary *finisher=[coral[@"fixedPlan"][@"floors"][14][@"variants"][0][@"stages"] lastObject];
        Check([finisher[@"id"] isEqual:@"finish-with-riku"] &&
            [finisher[@"repeat"][@"maximumPuzzles"] isEqual:@2] &&
            [finisher[@"repeat"][@"until"][@"kind"] isEqual:@"floor_change"] &&
            [finisher[@"actions"][2][@"when"] isEqual:@"attack_ready"] &&
            [finisher[@"actions"][2][@"frequency"] isEqual:@"once"],
            "B15 preserves one ready-window Riku and permits no more than two final puzzles including absorption stalls");
        NSDictionary *opener=[coral[@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"] lastObject];
        NSDictionary *plainExit=[coral[@"fixedPlan"][@"floors"][1][@"variants"][0][@"stages"] lastObject];
        NSUInteger recoveryStages=0;
        for (NSDictionary *floor in coral[@"fixedPlan"][@"floors"]) for (NSDictionary *variant in floor[@"variants"])
            for (NSDictionary *stage in variant[@"stages"]) recoveryStages+=[stage[@"board"][@"unmatchableRecovery"] boolValue];
        Check(recoveryStages==1 && [plainExit[@"board"][@"unmatchableRecovery"] isEqual:@YES] &&
            [plainExit[@"actions"] count]==1 && [plainExit[@"actions"][0][@"skill"] isEqual:@66706],
            "r5 enables latent recovery only on B2 finish without inserting another skill or turn");
        Check([opener[@"id"] isEqual:@"buff-and-finish"] && [opener[@"repeat"][@"maximumPuzzles"] isEqual:@2] &&
            !opener[@"actions"][0][@"optionalAfterFirst"] && [opener[@"actions"][2][@"optionalAfterFirst"] isEqual:@YES] &&
            ![opener[@"actions"][0][@"optional"] boolValue] && !opener[@"actions"][1][@"optionalAfterFirst"] &&
            [opener[@"retryRequiresBypass"] isEqual:@YES] &&
            [plainExit[@"repeat"][@"maximumPuzzles"] isEqual:@1] && !plainExit[@"actions"][0][@"optionalAfterFirst"],
            "only B1 adds one guarded retry; its first Atori remains mandatory and only the duplicate can be skipped");
        for (NSNumber *f in @[@4,@9,@12]) {
            NSDictionary *exit=[coral[@"fixedPlan"][@"floors"][f.unsignedIntegerValue][@"variants"][0][@"stages"] lastObject];
            Check([exit[@"repeat"][@"maximumPuzzles"] isEqual:@1],
                "B5 expired absorption, B10 new absorption and B13 HP timing do not gain an unchecked turn");
            for (NSDictionary *a in exit[@"actions"]) Check(!a[@"optionalAfterFirst"],"these floors keep required Atori actions");
        }
        NSDictionary *b7Target=coral[@"fixedPlan"][@"floors"][6][@"variants"][0][@"stages"][0][@"actions"][0];
        NSArray *b4=coral[@"fixedPlan"][@"floors"][3][@"variants"][0][@"stages"];
        for (NSDictionary *s in b4) Check([s[@"board"][@"requireSixColorFours"] isEqual:@YES] &&
            [s[@"requireEnemyState"] isEqual:@[@{@"monster":@202575},@{@"monster":@202578},@{@"monster":@202576}]] &&
            [s[@"requireEffectsClear"] isEqual:@[@"damage_absorb",@"attribute_absorb",@"damage_void"]] &&
            [s[@"repeat"][@"maximumPuzzles"] isEqual:@1] &&
            [[s[@"actions"] lastObject][@"target"] isEqual:@"attribute_absorb"] &&
            [[s[@"actions"] lastObject][@"color"] isEqual:@2],
            "both B4 attacks require six distinct fours, fresh coverage, the complete roster and a wood-absorb target check without another turn");
        Check([b7Target[@"kind"] isEqual:@"target"] && [b7Target[@"target"] isEqual:@"enemy"] &&
            [b7Target[@"monster"] isEqual:@210524] && ![b7Target[@"optional"] boolValue] &&
            [b7Target[@"frequency"] isEqual:@"once"],
            "B7 requires the exact observed special Clammy catalog ID without truncation or wildcard");
        NSArray *b7=coral[@"fixedPlan"][@"floors"][6][@"variants"][0][@"stages"];
        Check([b7[0][@"repeat"][@"maximumPuzzles"] isEqual:@2] &&
            [b7[0][@"repeat"][@"until"][@"kind"] isEqual:@"enemy_state_or_floor_change"] &&
            [[b7[0][@"actions"] lastObject] isEqual:b7Target] &&
            [b7[1][@"repeat"][@"maximumPuzzles"] isEqual:@1] &&
            [b7[1][@"requireEnemyState"] isEqual:b7[0][@"repeat"][@"until"][@"enemies"]] &&
            [b7[1][@"requireEffectsClear"] isEqual:@[@"damage_absorb",@"attribute_absorb",@"damage_void"]],
            "B7 rechecks its target after skills and adds one guarded survivor board after at most two original attacks");
        NSArray *b12=coral[@"fixedPlan"][@"floors"][11][@"variants"][0][@"stages"];
        Check([b12[2][@"repeat"][@"minimumPuzzles"] isEqual:@0] &&
            [b12[2][@"repeat"][@"until"] isEqual:@{@"kind":@"enemy_hp_at_most",@"percent":@50}] &&
            [b12[3][@"actions"][0][@"skill"] isEqual:@66718],
            "B12 observes the 50-percent checkpoint before issuing another attack, then casts Solomon");
        NSArray *b13=coral[@"fixedPlan"][@"floors"][12][@"variants"][0][@"stages"];
        Check([b13[1][@"repeat"][@"until"][@"kind"] isEqual:@"effects_clear"] &&
            [b13[1][@"actions"][0][@"skill"] isEqual:@66718] &&
            [b13[1][@"actions"][0][@"when"] isEqual:@"damage_absorb"] &&
            [[b13[2][@"actions"] valueForKey:@"skill"] isEqual:@[@66706,@66724,@66729,@66706]],
            "B13 restores expired bypass and applies Diena before Riku's one-turn HP buff");
        for (NSDictionary *s in b13) Check([s[@"holdLoops"] isEqual:@[@3]],"B13 reserves Keisy through preparation and finishing");
        NSArray *b14=coral[@"fixedPlan"][@"floors"][13][@"variants"][0][@"stages"];
        Check([b14[0][@"holdLoops"] isEqual:@[@3]] && [b14[1][@"actions"][0][@"skill"] isEqual:@66740] &&
            [b14[1][@"actions"][0][@"requireReady"] isEqual:@YES] &&
            [b14[2][@"repeat"][@"until"][@"skill"] isEqual:@66718],
            "B14 requires Keisy's resize and reserves native-ready Solomon for the B15 opening");
        NSDictionary *b15=coral[@"fixedPlan"][@"floors"][14][@"variants"][0][@"stages"][0];
        Check([[b15[@"actions"] valueForKey:@"skill"] isEqual:@[@66718,@66729,@66706]] &&
            [b15[@"actions"][0][@"when"] isEqual:@"unmatchable"] && ![b15[@"actions"][0][@"optional"] boolValue] &&
            [b15[@"requireEffectsClear"] isEqual:@[@"unmatchable"]],
            "B15 clears unmatchable with required Solomon and verifies the native result before a six-color board");
        for (NSUInteger floorIndex=7;floorIndex<=8;++floorIndex) {
            NSDictionary *partTarget=coral[@"fixedPlan"][@"floors"][floorIndex][@"variants"][1][@"stages"][0][@"actions"][0];
            Check([partTarget[@"kind"] isEqual:@"target"] && [partTarget[@"target"] isEqual:@"part"] &&
                [partTarget[@"monster"] isEqual:@0] && ![partTarget[@"optional"] boolValue],
                "B8 and B9 retain their required unique live part selection");
        }
        for (NSDictionary *member in coral[@"fixedPlan"][@"members"]) {
            for (NSDictionary *form in member[@"forms"])
                Check([form[@"skills"] isEqual:bodySkills[form[@"number"]]],
                    "every shipped Coral form declares the exact reviewed native current-stage skills");
            NSArray *expectedAssists=assistSkills[member[@"body"]] ?: @[];
            Check([member[@"assistSkills"] isEqual:expectedAssists],
                "only the two consumed Coral assists declare their reviewed private skills");
        }
        NSUInteger skillActions=0, readyConditions=0;
        for (NSDictionary *floor in coral[@"fixedPlan"][@"floors"]) for (NSDictionary *variant in floor[@"variants"])
            for (NSDictionary *stage in variant[@"stages"]) {
                for (NSDictionary *action in stage[@"actions"]) if ([action[@"kind"] isEqual:@"skill"]) {
                    ++skillActions;
                    const BOOL assist=[action[@"source"] isEqual:@"assist"];
                    NSArray *declared=(assist ? assistSkills : bodySkills)[action[@"monster"]];
                    PHOrbSkillRequest request=PHOrbFixedPlanActionRequest(action);
                    Check([declared containsObject:action[@"skill"]] && request.skill>UINT16_MAX &&
                        request.skill==[action[@"skill"] unsignedIntValue] &&
                        request.source==(assist ? PHOrbSkillAssist : PHOrbSkillBody),
                        "all shipped Coral actions preserve exact private skill IDs and body or assist source");
                    if ([action[@"monster"] isEqual:@13828]) {
                        NSNumber *expectedSolomon=[stage[@"id"] isEqual:@"evolve-solomon"] ? @66716 : @66718;
                        Check([action[@"skill"] isEqual:expectedSolomon],
                            "Solomon uses its first executable stage once, then only the evolved stage");
                    }
                }
                NSDictionary *until=stage[@"repeat"][@"until"];
                if ([until[@"kind"] isEqual:@"skill_ready"]) {
                    ++readyConditions;
                    const BOOL keisy=[floor[@"number"] isEqual:@13];
                    Check((keisy || [floor[@"number"] isEqual:@14]) && [until[@"slot"] isEqual:(keisy ? @3 : @5)] &&
                        [until[@"monster"] isEqual:(keisy ? @13745 : @13828)] && [until[@"skill"] isEqual:(keisy ? @66740 : @66718)] &&
                        [bodySkills[until[@"monster"]] containsObject:until[@"skill"]],
                        "B13 reserves exact private Keisy and B14 reserves evolved Solomon using native readiness");
                }
            }
        Check(skillActions==87 && readyConditions==2,"all 87 Coral skill actions and both readiness conditions are audited");
        NSMutableDictionary *oldPackage=[NSJSONSerialization JSONObjectWithData:coralData options:NSJSONReadingMutableContainers error:NULL];
        NSMutableDictionary *oldProfile=oldPackage[@"profile"];
        oldProfile[@"id"]=@"coral-fixed-585607"; oldProfile[@"name"]=@"珊瑚挑戰・固定 Atori・GameWith";
        oldProfile[@"dungeon"]=@0; oldProfile[@"floor"]=@0;
        oldProfile[@"fixedPlan"][@"floors"][6][@"variants"][0][@"stages"][0][@"actions"][0][@"monster"]=@10524;
        [oldProfile[@"fixedPlan"][@"floors"][14][@"variants"][0][@"stages"] lastObject][@"repeat"][@"maximumPuzzles"]=@8;
        for (NSMutableDictionary *member in oldProfile[@"fixedPlan"][@"members"]) {
            for (NSMutableDictionary *form in member[@"forms"]) {
                NSMutableArray *skills=[NSMutableArray array];
                for (NSNumber *skill in form[@"skills"]) [skills addObject:ordinarySkills[skill]];
                form[@"skills"]=skills;
            }
            NSMutableArray *skills=[NSMutableArray array];
            for (NSNumber *skill in member[@"assistSkills"]) [skills addObject:ordinarySkills[skill]];
            member[@"assistSkills"]=skills;
        }
        for (NSDictionary *floor in oldProfile[@"fixedPlan"][@"floors"]) for (NSDictionary *variant in floor[@"variants"])
            for (NSDictionary *stage in variant[@"stages"]) {
                for (NSMutableDictionary *action in stage[@"actions"]) if ([action[@"kind"] isEqual:@"skill"])
                    action[@"skill"]=ordinarySkills[action[@"skill"]];
                NSMutableDictionary *until=stage[@"repeat"][@"until"];
                if ([until[@"kind"] isEqual:@"skill_ready"]) until[@"skill"]=ordinarySkills[until[@"skill"]];
            }
        NSData *oldData=[NSJSONSerialization dataWithJSONObject:oldPackage options:0 error:NULL];
        NSDictionary *oldLibrary=PHOrbProfilesImportingData(PHOrbDefaultProfiles(),oldData,NULL);
        NSDictionary *oldRecord=PHOrbProfileNamed(oldLibrary,@"coral-fixed-585607");
        NSData *oldExport=PHOrbExportProfileData(oldRecord,NULL);
        NSMutableDictionary *v90Package=[NSJSONSerialization JSONObjectWithData:coralData options:NSJSONReadingMutableContainers error:NULL];
        v90Package[@"profile"][@"id"]=@"coral-fixed-585607-v90";
        v90Package[@"profile"][@"name"]=@"珊瑚挑戰・固定 Atori・專用技能 v90";
        v90Package[@"profile"][@"fixedPlan"][@"floors"][6][@"variants"][0][@"stages"][0][@"actions"][0][@"monster"]=@10524;
        [v90Package[@"profile"][@"fixedPlan"][@"floors"][14][@"variants"][0][@"stages"] lastObject][@"repeat"][@"maximumPuzzles"]=@8;
        NSDictionary *v90Library=PHOrbProfilesImportingData(oldLibrary,
            [NSJSONSerialization dataWithJSONObject:v90Package options:0 error:NULL],NULL);
        NSData *v90Export=PHOrbExportProfileData(PHOrbProfileNamed(v90Library,@"coral-fixed-585607-v90"),NULL);
        NSMutableDictionary *b7Package=[NSJSONSerialization JSONObjectWithData:coralData options:NSJSONReadingMutableContainers error:NULL];
        b7Package[@"profile"][@"id"]=@"coral-fixed-585607-v90-b7";
        b7Package[@"profile"][@"name"]=@"珊瑚挑戰・固定 Atori・v90 B7 修正";
        [b7Package[@"profile"][@"fixedPlan"][@"floors"][14][@"variants"][0][@"stages"] lastObject][@"repeat"][@"maximumPuzzles"]=@8;
        NSDictionary *b7Library=PHOrbProfilesImportingData(v90Library,
            [NSJSONSerialization dataWithJSONObject:b7Package options:0 error:NULL],NULL);
        NSData *b7Export=PHOrbExportProfileData(PHOrbProfileNamed(b7Library,@"coral-fixed-585607-v90-b7"),NULL);
        // A deliberately different stored record under the immediately prior
        // ID detects accidental migrations as well as exact-ID overwrites.
        NSMutableDictionary *priorPackage=FixedFixture();
        priorPackage[@"profile"][@"id"]=@"coral-fixed-585607-v90-r3";
        NSDictionary *priorLibrary=PHOrbProfilesImportingData(b7Library,
            [NSJSONSerialization dataWithJSONObject:priorPackage options:0 error:NULL],NULL);
        NSData *priorExport=PHOrbExportProfileData(PHOrbProfileNamed(priorLibrary,@"coral-fixed-585607-v90-r3"),NULL);
        NSMutableDictionary *r4Package=FixedFixture(); r4Package[@"profile"][@"id"]=@"coral-fixed-585607-v90-r4";
        NSDictionary *r4Library=PHOrbProfilesImportingData(priorLibrary,
            [NSJSONSerialization dataWithJSONObject:r4Package options:0 error:NULL],NULL);
        NSData *r4Export=PHOrbExportProfileData(PHOrbProfileNamed(r4Library,@"coral-fixed-585607-v90-r4"),NULL);
        NSMutableDictionary *r5Package=FixedFixture(); r5Package[@"profile"][@"id"]=@"coral-fixed-585607-v90-r5";
        NSDictionary *r5Library=PHOrbProfilesImportingData(r4Library,
            [NSJSONSerialization dataWithJSONObject:r5Package options:0 error:NULL],NULL);
        NSData *r5Export=PHOrbExportProfileData(PHOrbProfileNamed(r5Library,@"coral-fixed-585607-v90-r5"),NULL);
        NSDictionary *both=PHOrbProfilesImportingData(r5Library,coralData,NULL);
        NSDictionary *reloaded=both ? [NSJSONSerialization JSONObjectWithData:
            [NSJSONSerialization dataWithJSONObject:both options:0 error:NULL] options:0 error:NULL] : nil;
        NSDictionary *newRecord=PHOrbProfileNamed(reloaded,@"coral-fixed-585607-v90-r6");
        NSData *newExport=PHOrbExportProfileData(newRecord,NULL);
        Check(oldRecord && v90Export && b7Export && priorExport && r4Export && r5Export && reloaded && PHOrbProfilesValid(reloaded) && [reloaded[@"profiles"] count]==7 &&
            [reloaded[@"selected"] isEqual:@"coral-fixed-585607-v90-r6"] && ![reloaded[@"autoSelect"] boolValue] &&
            [PHOrbExportProfileData(PHOrbProfileNamed(reloaded,@"coral-fixed-585607"),NULL) isEqual:oldExport] &&
            [PHOrbExportProfileData(PHOrbProfileNamed(reloaded,@"coral-fixed-585607-v90"),NULL) isEqual:v90Export] &&
            [PHOrbExportProfileData(PHOrbProfileNamed(reloaded,@"coral-fixed-585607-v90-b7"),NULL) isEqual:b7Export] &&
            [PHOrbExportProfileData(PHOrbProfileNamed(reloaded,@"coral-fixed-585607-v90-r3"),NULL) isEqual:priorExport] &&
            [PHOrbExportProfileData(PHOrbProfileNamed(reloaded,@"coral-fixed-585607-v90-r4"),NULL) isEqual:r4Export] &&
            [PHOrbExportProfileData(PHOrbProfileNamed(reloaded,@"coral-fixed-585607-v90-r5"),NULL) isEqual:r5Export] &&
            [newRecord isEqual:coral] && newExport && !PHOrbProfilesImportingData(reloaded,newExport,NULL),
            "r6 import and JSON reload preserve all earlier records including r5 and reject duplicate new IDs");
        PHOrbSnapshot native={0}; native.valid=native.present=native.automation.valid=true;
        native.identity.entryMode=1; native.identity.dungeon=7801; native.identity.floor=1;
        for (unsigned i=0;i<6;++i) {
            NSDictionary *member=coral[@"fixedPlan"][@"members"][i]; PHOrbSkillMember *live=&native.automation.members[i];
            live->active=live->temporaryKnown=true; live->number=[member[@"body"] unsignedIntValue];
            PHBattleCatalogIDForNumber(live->number,&live->sourceCatalogIDs[0]);
            if (member[@"assist"]!=NSNull.null) PHBattleCatalogIDForNumber([member[@"assist"] unsignedIntValue],&live->sourceCatalogIDs[1]);
            live->assistCatalogID=live->sourceCatalogIDs[1];
        }
        Check([PHOrbResolveProfile(reloaded,&native)[@"ok"] boolValue],"revised Coral profile resolves on the observed native dungeon");
        native.identity.dungeon=7802;
        Check(![PHOrbResolveProfile(reloaded,&native)[@"ok"] boolValue],"identical team in another dungeon cannot select the revised Coral plan");
        native.identity.dungeon=7801; native.identity.floor=2;
        Check(![PHOrbResolveProfile(reloaded,&native)[@"ok"] boolValue],"another sub-dungeon cannot select the revised Coral plan");
    }
    NSMutableDictionary *package = FixedFixture(); NSString *reason = nil;
    for (unsigned mutation=0;mutation<7;++mutation) {
        NSMutableDictionary *p=FixedFixture(), *s=p[@"profile"][@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        NSMutableDictionary *b=s[@"board"]; b[@"assistRecoveryL"]=@NO; b[@"requireSixColorFours"]=@YES;
        if (mutation==1) b[@"requireSixColorFours"]=@1;
        if (mutation==2) b[@"requireSixColorFours"]=@"true";
        if (mutation==3) b[@"policy"]=@"six_colors_no_fours";
        if (mutation==4) b[@"lColors"]=@5;
        if (mutation==5) b[@"assistRecoveryL"]=@YES;
        if (mutation==6) s[@"absorbStall"]=@YES;
        Check((PHOrbFixedPlanFromPackage(p,NULL)!=nil)==!mutation,
            "six-colour fours is a strict attack Boolean and cannot silently combine with stall or L contracts");
    }
    for (unsigned mutation=0;mutation<8;++mutation) {
        NSMutableDictionary *p=FixedFixture(), *s=p[@"profile"][@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        NSMutableDictionary *a=[@{@"kind":@"target",@"target":@"attribute_absorb",@"monster":@0,@"color":@2,
            @"optional":@NO,@"frequency":@"each"} mutableCopy];
        s[@"requireEnemyState"]=@[@{@"monster":@42}]; s[@"actions"]=@[a];
        if (mutation==1) [s removeObjectForKey:@"requireEnemyState"];
        if (mutation==2) a[@"color"]=@YES;
        if (mutation==3) a[@"color"]=@5;
        if (mutation==4) a[@"monster"]=@202576;
        if (mutation==5) a[@"optional"]=@YES;
        if (mutation==6) a[@"target"]=@"enemy";
        if (mutation==7) [a removeObjectForKey:@"color"];
        Check((PHOrbFixedPlanFromPackage(p,NULL)!=nil)==!mutation,
            "conditional absorption targeting requires a complete roster, an exact colour and known-absence semantics");
    }
    for (unsigned mutation=0;mutation<9;++mutation) {
        NSMutableDictionary *scoped=FixedFixture(), *plan=scoped[@"profile"][@"fixedPlan"];
        NSMutableDictionary *floor=[plan[@"floors"][0] mutableCopy]; floor[@"number"]=@2;
        // Deep-copy stages so a B2 edit cannot also change B1.
        floor=[NSJSONSerialization JSONObjectWithData:[NSJSONSerialization dataWithJSONObject:floor options:0 error:NULL]
            options:NSJSONReadingMutableContainers error:NULL];
        [plan[@"floors"] addObject:floor];
        NSMutableDictionary *stage=floor[@"variants"][0][@"stages"][0], *board=stage[@"board"];
        board[@"assistRecoveryL"]=@NO; board[@"unmatchableRecovery"]=mutation==1 ? (id)@1 : (id)@YES;
        if (mutation==2) board[@"policy"]=@"six_colors_no_fours";
        if (mutation==3) board[@"assistRecoveryL"]=@YES;
        if (mutation==4) board[@"lColors"]=@5;
        if (mutation==5) stage[@"absorbStall"]=@YES;
        if (mutation==6) plan[@"floors"][0][@"variants"][0][@"stages"][0][@"board"][@"unmatchableRecovery"]=@YES;
        if (mutation==7) board[@"unmatchableRecovery"]=@"true";
        if (mutation==8) board[@"requireSixColorFours"]=@YES;
        Check((PHOrbFixedPlanFromPackage(scoped,NULL)!=nil)==(mutation==0),
            "unmatchable recovery requires a strict Boolean, B2 four attack and no assist-L or stall contract");
    }
    NSDictionary *profile = PHOrbFixedPlanFromPackage(package,&reason);
    NSData *exported = PHOrbExportProfileData(profile,&reason);
    NSDictionary *library = PHOrbProfilesImportingData(PHOrbDefaultProfiles(),exported,&reason);
    Check(profile && library && !reason && [library[@"selected"] isEqual:@"fixed-fixture"] &&
        ![library[@"autoSelect"] boolValue] && !library[@"profiles"][0][@"formation"],
        "fixed plans import without formation replacement or automatic start");
    Check([PHOrbExportProfileData(library[@"profiles"][0],NULL) isEqual:exported],
        "fixed plan stages and native identities roundtrip canonically");
    Check(!PHOrbProfilesImportingData(library,exported,&reason) && [reason containsString:@"ID"],
        "fixed plan duplicate import preserves the original record");
    NSMutableDictionary *ordinary = [package mutableCopy]; ordinary[@"format"] = @"padcheaper-orb-profile";
    Check(!PHOrbProfilesImportingData(PHOrbDefaultProfiles(),[NSJSONSerialization dataWithJSONObject:ordinary options:0 error:NULL],NULL),
        "ordinary format cannot smuggle fixed execution stages");
    for (unsigned mutation=0;mutation<11;++mutation) {
        NSMutableDictionary *retry=FixedFixture();
        NSMutableDictionary *stage=retry[@"profile"][@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        NSMutableDictionary *action=[stage[@"actions"][0] mutableCopy];
        action[@"afterMonster"]=@0; action[@"frequency"]=@"each";
        NSMutableDictionary *required=[action mutableCopy];
        action[@"frequency"]=mutation==1 ? @"once" : @"each"; action[@"optional"]=mutation==2 ? @YES : @NO;
        action[@"optionalAfterFirst"]=mutation==3 ? (id)@1 : (id)@YES;
        stage[@"retryRequiresBypass"]=@YES;
        stage[@"actions"]=mutation==4 ? @[action] : @[required,action];
        if (mutation==5) action[@"afterMonster"]=@101;
        if (mutation==6) required[@"frequency"]=@"once";
        if (mutation==7) action[@"slot"]=@2;
        if (mutation==8) [stage removeObjectForKey:@"retryRequiresBypass"];
        if (mutation==9) stage[@"retryRequiresBypass"]=@1;
        if (mutation==10) stage[@"absorbStall"]=@YES;
        Check(!PHOrbFixedPlanFromPackage(retry,NULL)==(mutation!=0),
            "retry skips require an earlier identical mandatory body action and a guarded exit; invalid source, transform and guard forms fail");
    }
    NSMutableDictionary *optionalTarget=FixedFixture();
    optionalTarget[@"profile"][@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0][@"actions"]=
        @[@{@"kind":@"target",@"target":@"part",@"monster":@0,@"optional":@YES,@"frequency":@"once"}];
    Check(!PHOrbFixedPlanFromPackage(optionalTarget,NULL),"unsupported optional targets cannot silently skip a required enemy selection");
    for (unsigned mutation=0;mutation<17;++mutation) {
        NSMutableDictionary *guarded=FixedFixture();
        NSMutableDictionary *stage=guarded[@"profile"][@"fixedPlan"][@"floors"][0][@"variants"][0][@"stages"][0];
        NSMutableDictionary *enemy=[@{@"monster":@210524,@"alive":@YES,@"maximumPercent":@50} mutableCopy];
        stage[@"requireEnemyState"]=@[enemy]; stage[@"requireEffectsClear"]=@[@"unmatchable"]; stage[@"holdLoops"]=@[@3];
        if (mutation==1) enemy[@"alive"]=@1;
        if (mutation==2) enemy[@"maximumPercent"]=@0;
        if (mutation==3) enemy[@"maximumPercent"]=@101;
        if (mutation==4) [enemy removeObjectForKey:@"alive"];
        if (mutation==5) enemy[@"monster"]=@0;
        if (mutation==6) enemy[@"cachedAction"]=@90746;
        if (mutation==7) stage[@"requireEnemyState"]=@[enemy,enemy];
        if (mutation==8) stage[@"requireEnemyState"]=@[];
        if (mutation==9) stage[@"requireEffectsClear"]=@[@"unknown"];
        if (mutation==10) stage[@"requireEffectsClear"]=@[@"unmatchable",@"unmatchable"];
        if (mutation==11) stage[@"holdLoops"]=@[@0];
        if (mutation==12) stage[@"holdLoops"]=@[@3,@3];
        if (mutation==13) stage[@"holdLoops"]=@[@7];
        if (mutation>=14) {
            NSMutableDictionary *action=stage[@"actions"][0];
            action[@"afterMonster"]=@0; action[@"loop"]=@YES; action[@"requireReady"]=mutation==14 ? (id)@1 : (id)@YES;
            if (mutation==15) action[@"optional"]=@YES;
            if (mutation==16) action[@"loop"]=@NO;
        }
        Check((PHOrbFixedPlanFromPackage(guarded,NULL)!=nil)==(mutation==0),
            "state and effect guards, held loops and mandatory loop casts reject ambiguous or unsupported declarations");
    }
    for (NSString *mutation in @[@"unknown-key",@"source",@"skill",@"form",@"gesture",@"repeat",@"missing-floor",@"mixed-variants",@"boolean",@"profile-team",@"rules",@"no-last-exit"]) {
        NSMutableDictionary *bad = FixedFixture(), *p = bad[@"profile"], *plan = p[@"fixedPlan"];
        NSMutableDictionary *stage = plan[@"floors"][0][@"variants"][0][@"stages"][0], *action = stage[@"actions"][0];
        if ([mutation isEqual:@"unknown-key"]) plan[@"timerEdit"] = @YES;
        if ([mutation isEqual:@"source"]) action[@"source"] = @"auto";
        if ([mutation isEqual:@"skill"]) action[@"skill"] = @702;
        if ([mutation isEqual:@"form"]) action[@"afterMonster"] = @102;
        if ([mutation isEqual:@"gesture"]) stage[@"board"][@"maxGestureMS"] = @4501;
        if ([mutation isEqual:@"repeat"]) stage[@"repeat"][@"maximumPuzzles"] = @0;
        if ([mutation isEqual:@"missing-floor"]) plan[@"floors"][0][@"number"] = @2;
        if ([mutation isEqual:@"mixed-variants"]) plan[@"floors"][0][@"variants"] = @[
            plan[@"floors"][0][@"variants"][0],@{@"kind":@"parts",@"stages":@[stage]}];
        if ([mutation isEqual:@"boolean"]) action[@"optional"] = @1;
        if ([mutation isEqual:@"profile-team"]) p[@"team"][0] = @101;
        if ([mutation isEqual:@"rules"]) p[@"settings"][@"rules"] = @[PHOrbDefaultSkillRule()];
        if ([mutation isEqual:@"no-last-exit"]) stage[@"repeat"][@"until"] = @{@"kind":@"puzzles"};
        Check(!PHOrbFixedPlanFromPackage(bad,NULL),"fixed plans reject undeclared sources, unsafe bounds, shape keys and conflicting execution models");
    }
    PHOrbSnapshot state = {0}; state.valid = state.present = state.automation.valid = true;
    state.identity.entryMode = 1; state.automation.enemiesKnown = true;
    for (unsigned i = 0; i < 6; ++i) {
        state.automation.members[i].active = state.automation.members[i].temporaryKnown = true;
        state.automation.members[i].number = 100; state.automation.members[i].sourceCatalogIDs[0] = 100;
    }
    Check(PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"fixed plan matches its six declared native sources");
    state.automation.members[0].number = 101;
    Check(PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"declared native transform remains bound to original fixed source");
    state.automation.members[0].sourceCatalogIDs[0] = 101;
    Check(!PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"same displayed form cannot hide a changed native source");
    state.automation.members[0].sourceCatalogIDs[0] = 100;
    state.automation.members[0].temporaryTurns = 1;
    Check(!PHOrbFixedPlanMatchesTeam(profile[@"fixedPlan"],&state),"temporary replacements cannot authorize fixed-plan actions");
    PHOrbFixedPlanEvidence evidence = {0};
    NSDictionary *repeat = @{@"minimumPuzzles":@2,@"maximumPuzzles":@8,@"until":@{@"kind":@"effects_clear",@"effects":@[@"awoken_bind",@"assist_void"]}};
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&evidence)==PHOrbFixedPlanNo,
        "stage never advances before required confirmed puzzle count");
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,2,0,&evidence)==PHOrbFixedPlanUnknown,
        "missing native effect evidence cannot finish a stage");
    evidence.statusKnown = evidence.assistVoidKnown = true;
    evidence.assistVoidTurns = 1;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,2,0,&evidence)==PHOrbFixedPlanNo,
        "one remaining native effect retains the current stage");
    evidence.assistVoidTurns = 0;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,2,0,&evidence)==PHOrbFixedPlanYes,
        "all fresh native effects cleared can advance after confirmed puzzles");
    repeat = @{@"minimumPuzzles":@0,@"maximumPuzzles":@8,@"until":@{@"kind":@"effects_clear",@"effects":@[@"damage_absorb"]}};
    evidence.enemiesKnown = true; evidence.enemyEffects = evidence.playerBypass = PHOrbEnemyDamageAbsorb;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,0,&evidence)==PHOrbFixedPlanYes,
        "confirmed native bypass can finish a condition-only stage before its puzzle");
    repeat = @{@"minimumPuzzles":@1,@"maximumPuzzles":@8,@"until":@{@"kind":@"enemy_hp_at_most",@"percent":@50}};
    evidence.enemiesKnown = true; evidence.aliveMask = evidence.hpKnownMask = 1;
    evidence.enemyMaximumHP[0] = UINT64_MAX; evidence.enemyHP[0] = UINT64_MAX/2;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&evidence)==PHOrbFixedPlanYes,
        "native HP boundary calculation cannot overflow at maximum uint64 HP");
    ++evidence.enemyHP[0];
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&evidence)==PHOrbFixedPlanNo,
        "one native HP above the threshold does not advance");
    repeat=@{@"minimumPuzzles":@0,@"maximumPuzzles":@8,@"until":@{@"kind":@"skill_ready",@"slot":@5,@"monster":@13828,@"skill":@63192}};
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,12,&evidence)==PHOrbFixedPlanUnknown,"cooldown wait needs native readiness evidence");
    evidence.readinessKnownSlots=16;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,12,&evidence)==PHOrbFixedPlanNo,"known unready remains in B13 stall");
    evidence.readySlots=16;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,12,&evidence)==PHOrbFixedPlanYes,"ready Solomon leaves B13 without an extra puzzle");
    NSDictionary *attack=@{@"kind":@"skill",@"when":@"attack_ready"};
    evidence.enemyEffects=PHOrbEnemyAttributeAbsorb; evidence.playerBypass=0;
    Check(PHOrbFixedPlanActionCondition(attack,&evidence)==PHOrbFixedPlanNo,"absorption stall preserves final Riku skill");
    evidence.playerBypass=PHOrbEnemyAttributeAbsorb;
    Check(PHOrbFixedPlanActionCondition(attack,&evidence)==PHOrbFixedPlanYes,"fresh bypass permits final attack buff");
    NSArray *survivor=@[@{@"monster":@200038,@"alive":@NO},@{@"monster":@210524,@"alive":@YES,@"maximumPercent":@50}];
    PHOrbFixedPlanEvidence pair={0}; pair.enemiesKnown=true; pair.enemyCount=2;
    pair.identityKnownMask=pair.hpKnownMask=pair.bodyMask=3; pair.aliveMask=2;
    pair.enemyIDs[0]=200038; pair.enemyIDs[1]=210524;
    pair.enemyMaximumHP[0]=pair.enemyMaximumHP[1]=UINT64_C(360000000000);
    pair.enemyHP[1]=UINT64_C(180000000000);
    Check(PHOrbFixedPlanEnemyState(survivor,&pair)==PHOrbFixedPlanYes,"the observed dead Awaryn and half-HP Clammy meet the exact survivor condition");
    for (unsigned mutation=0;mutation<10;++mutation) {
        PHOrbFixedPlanEvidence changed=pair;
        if (mutation==0) changed.enemyHP[0]=1;
        if (mutation==1) ++changed.enemyHP[1];
        if (mutation==2) changed.enemyHP[1]=0;
        if (mutation==3) changed.enemyIDs[1]=10524;
        if (mutation==4) changed.enemyIDs[0]=210524;
        if (mutation==5) changed.identityKnownMask=1;
        if (mutation==6) changed.hpKnownMask=1;
        if (mutation==7) changed.enemyCount=3;
        if (mutation==8) changed.bodyMask=1;
        if (mutation==9) changed.enemyMaximumHP[1]=0;
        Check(PHOrbFixedPlanEnemyState(survivor,&changed)!=PHOrbFixedPlanYes,
            "alive partner, wrong HP/ID, duplicate, unknown, extra enemy or part never authorizes the survivor finisher");
    }
    repeat=@{@"minimumPuzzles":@1,@"maximumPuzzles":@2,@"until":@{@"kind":@"enemy_state_or_floor_change",@"enemies":survivor}};
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,0,0,&pair)==PHOrbFixedPlanNo &&
        PHOrbFixedPlanRepeatSatisfied(repeat,2,0,&pair)==PHOrbFixedPlanYes,
        "survivor transition needs a confirmed puzzle and remains valid exactly at the original two-board cap");
    pair.wave=1;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&pair)==PHOrbFixedPlanYes,"a full clear exits the guarded attack phase without a survivor board");
    pair.wave=2;
    Check(PHOrbFixedPlanRepeatSatisfied(repeat,1,0,&pair)==PHOrbFixedPlanNo,"a skipped floor cannot satisfy a state-or-floor predicate");
}
