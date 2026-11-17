static void FixedNativeFixture() {
    SkillFixture();
    for (const auto &g:PHOrbFixedTextGuards) std::memcpy(reinterpret_cast<void *>(Header+g.rva),g.bytes,64);
    for (const auto &g:PHOrbEnemyActionsTextGuards) std::memcpy(reinterpret_cast<void *>(Header+g.rva),g.bytes,64);
    const uintptr_t a=Impl+PHOrbEnemyOffset;
    Protected(Impl+PHOrbEnemyCountOffset,2,0x766cfc6d);
    std::memcpy(reinterpret_cast<void *>(a+PHOrbEnemyStride),reinterpret_cast<void *>(a),PHOrbEnemyStride);
    Put<int16_t>(Impl+753336,-1); Put<int16_t>(Impl+603008,-1);
    for (unsigned i=0;i<2;++i) {
        const uintptr_t e=a+i*PHOrbEnemyStride;
        Put<uint32_t>(e+580,42+i); Put<uint8_t>(e+1,1);
        const uintptr_t parameter=reinterpret_cast<uintptr_t>(catalogBytes.data())+(42+i)*PHBattleCatalogStride;
        Put<uint32_t>(parameter,42+i); Put<uintptr_t>(e+PHOrbEnemyActionsParameterOffset,parameter);
        Protected(e+592,1000,0x766cfc6d); Protected(e+608,0,0x766cfc6d);
        Protected(e+2480,0,0x6a875a8b); Protected(e+2496,0,0x6a875a8b);
        Put<float>(e+360,100+150*i); Put<float>(e+364,100);
        Put<int16_t>(e+322,300); Put<int16_t>(e+324,80); Put<int16_t>(e+326,80);
    }
}
static void FixedSoloLeaderNativeTests() {
    for (unsigned scenario=0;scenario<10;++scenario) {
        SkillFixture();
        SetNativeLeader(0,13828,50,0); SetNativeLeader(5,8476,51,5);
        const uintptr_t catalog=reinterpret_cast<uintptr_t>(catalogBytes.data());
        const uintptr_t member=Impl+PHBattleMemberOffset, variant=catalog+14600*PHBattleCatalogStride;
        const uintptr_t ordinary=catalog+13928*PHBattleCatalogStride;
        std::memcpy(reinterpret_cast<void *>(variant),reinterpret_cast<void *>(ordinary),PHBattleCatalogStride);
        Put<uint32_t>(variant,90013928); Put<uint32_t>(variant+PHOrbLeaderSkillOffset,52);
        Put<uint32_t>(ordinary+PHOrbLeaderCatalogNextOffset,14600);
        Put<uint32_t>(Header+PHSaveRVA+PHOrbLeaderCatalogRecordsOffset,14601);
        Put<uint32_t>(member+PHBattleCatalogIDOffset,90013928); Put<uintptr_t>(member+PHBattleCardParamOffset,variant);
        if (scenario==1) Put<uintptr_t>(member+PHBattleCardParamOffset,ordinary);
        if (scenario==2) Put<uint16_t>(member+PHBattleBaseIDOffset,13929);
        if (scenario==3) Protected(member+PHBattleTempTurnsOffset,1,0x6a875a8b);
        if (scenario==4) Put<uint32_t>(Header+PHSaveRVA+PHOrbLeaderCatalogRecordsOffset,14600);
        if (scenario==5) changeLeaderDuringPairRead=true;
        if (scenario==6) changeLeaderCatalogDuringPairRead=true;
        if (scenario==7) Protected(member+PHBattleSourceSlotOffset,5,0x6a875a8b);
        if (scenario==8) { Put<uint32_t>(variant,10013928); Put<uint32_t>(variant+PHOrbLeaderCatalogNextOffset,14600); }
        if (scenario==9) Put<uint32_t>(variant+PHOrbLeaderSkillOffset,70000);
        auto engine=PHCreateOrbEngine(); PHOrbSnapshot before{}, after{};
        Check([engine read:&before frame:Frame() reason:nil] && PHOrbCanPreview(&before) &&
            before.leadersValid==(scenario==0) && !writes,
            "solo variant-90 leader requires exact source, record, skill, capacity and a stable pair without writes");
        if (!scenario) {
            Check(PHOrbLeaderContextReady(&before) && before.leaderNumbers[0]==13828 &&
                before.leaderContext.known && !before.leaderContext.teamBank &&
                before.leaderContext.catalogIDs[0]==90013928 && before.leaderContext.skillIDs[0]==52,
                "solo fixed leader retains its private skill rather than the displayed ordinary card skill");
            Put<uint32_t>(variant+PHOrbLeaderSkillOffset,53);
            Check([engine read:&after frame:Frame() reason:nil] && after.leadersValid && !PHOrbSameLeaders(&before,&after),
                "same-number solo variant skill changes invalidate captured leader decisions");
            Put<uint32_t>(variant+PHOrbLeaderSkillOffset,52);
            Protected(member+PHBattleSourceSlotOffset,2,0x6a875a8b);
            Check([engine read:&after frame:Frame() reason:nil] && after.leadersValid && !PHOrbSameLeaders(&before,&after),
                "same-number solo fixed leader swaps invalidate captured leader decisions");
        }
    }
}
static void FixedPrivateSkillReadinessTests() {
    SkillFixture(); SetLeader(0,13876);
    const uintptr_t member=Impl+PHBattleMemberOffset, catalog=reinterpret_cast<uintptr_t>(catalogBytes.data());
    const uintptr_t ordinary=catalog+13976*PHBattleCatalogStride, variant=catalog+14600*PHBattleCatalogStride;
    std::memcpy(reinterpret_cast<void *>(variant),reinterpret_cast<void *>(ordinary),PHBattleCatalogStride);
    Put<uint32_t>(variant,90013976); Put<uint32_t>(ordinary+PHOrbLeaderCatalogNextOffset,14600);
    Put<uint32_t>(Header+PHSaveRVA+PHOrbLeaderCatalogRecordsOffset,14601);
    Put<uint32_t>(member+PHBattleCatalogIDOffset,90013976); Put<uintptr_t>(member+PHBattleCardParamOffset,variant);
    Put<uint16_t>(member+PHBattleBaseIDOffset,13976); Put<uint16_t>(member+PHBattleAssistIDOffset,13719);
    Put<uint16_t>(member+PHBattleEffectiveAssistOffset,13719);
    Put<uint8_t>(member+PHBattleEffectiveAssistOffset+PHBattleAssistVariantByte,90);
    Protected(member+PHBattleAssistStateOffset,172,0x6a875a8b);
    Protected(member+PHBattleChargeOffset,14,0x6a875a8b);
    Put<uint32_t>(member+PHBattleChargeShadowOffset,14u^0x68fb65ceu);
    readinessFixture={66724,66752,9,14};
    auto engine=[FixtureSkillEngine new]; PHOrbSnapshot state{}; NSString *reason=nil;
    Check([engine readAutomation:&state frame:Frame() reason:nil] &&
        state.automation.members[0].nativeCatalogIDs[0]==90013976 && state.automation.members[0].nativeCatalogIDs[1]==90013719,
        "Coral B1 readiness fixture preserves the observed private body and assist identities");
    Check([engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillAssist,61754}
        expected:&state frame:Frame() reason:&reason]==PHOrbSkillRejected &&
        [reason containsString:@"預期武裝 61754，實際 66752"] && !skillStartCalls && !writes,
        "ordinary Chronos skill cannot authorize the observed private assist even when it is charged");
    Check([engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillAssist,uint16_t(66752)}
        expected:&state frame:Frame() reason:nil]==PHOrbSkillRejected && !skillStartCalls && !writes,
        "a truncated private skill ID cannot match a native skill above UINT16_MAX");
    Check([engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillAssist,66752}
        expected:&state frame:Frame() reason:nil]==PHOrbSkillStarted && !skillStartCalls && !writes,
        "the exact private assist passes readiness without starting a skill or overriding selection");
    Check([engine useSkill:0 request:PHOrbSkillRequest{PHOrbSkillAssist,66752}
        expected:&state frame:Frame() reason:nil]==PHOrbSkillStarted && skillStartCalls==1 && !writes &&
        sourceAtStart==1 && effectiveStartedSkill==66752 && effectsStartedSkill==66752,
        "the exact private assist ID survives request and native start as 32 bits with the native assist source");
}
static void FixedPrivateTargetNativeTests() {
    for (unsigned scenario=0;scenario<5;++scenario) {
        FixedNativeFixture();
        const uintptr_t catalog=reinterpret_cast<uintptr_t>(catalogBytes.data());
        const uint32_t ordinaryIDs[]={38,10524}, privateIDs[]={200038,210524};
        // Synthetic linked records fit the fixture catalog; no private dump is required.
        for (unsigned slot=0;slot<2;++slot) {
            const uintptr_t ordinary=catalog+ordinaryIDs[slot]*PHBattleCatalogStride;
            const uintptr_t variant=catalog+(14610+slot)*PHBattleCatalogStride;
            std::memcpy(reinterpret_cast<void *>(ordinary),reinterpret_cast<void *>(catalog+(42+slot)*PHBattleCatalogStride),PHBattleCatalogStride);
            Put<uint32_t>(ordinary,ordinaryIDs[slot]);
            std::memcpy(reinterpret_cast<void *>(variant),reinterpret_cast<void *>(ordinary),PHBattleCatalogStride);
            Put<uint32_t>(ordinary+PHOrbLeaderCatalogNextOffset,14610+slot);
            Put<uint32_t>(variant,privateIDs[slot]); Put<uint32_t>(variant+PHOrbLeaderCatalogNextOffset,0);
            const uintptr_t enemy=Impl+PHOrbEnemyOffset+slot*PHOrbEnemyStride;
            Put<uint32_t>(enemy+580,privateIDs[slot]); Put<uintptr_t>(enemy+PHOrbEnemyActionsParameterOffset,variant);
        }
        Put<uint32_t>(Header+PHSaveRVA+PHOrbLeaderCatalogRecordsOffset,14612);
        auto engine=[FixtureSkillEngine new]; PHOrbSnapshot state{};
        Check([engine readAutomation:&state frame:Frame() reason:nil] && [engine readFixedPlan:&state frame:Frame() reason:nil] &&
            state.fixedPlan.enemies[0].catalogID==200038 && state.fixedPlan.enemies[1].catalogID==210524 &&
            state.fixedPlan.enemies[1].identityKnown && state.fixedPlan.enemies[1].hpKnown &&
            state.fixedPlan.enemies[1].parameter==catalog+14611*PHBattleCatalogStride && !writes,
            "fixed evidence preserves complete private enemy IDs and their verified linked native parameters");
        const uintptr_t enemy=Impl+PHOrbEnemyOffset+PHOrbEnemyStride;
        const uintptr_t ordinary=catalog+10524*PHBattleCatalogStride, variant=catalog+14611*PHBattleCatalogStride;
        if (scenario==1) Put<uintptr_t>(enemy+PHOrbEnemyActionsParameterOffset,ordinary);
        if (scenario==2) { Put<uint32_t>(enemy+580,110524); Put<uint32_t>(variant,110524); }
        if (scenario==3) Put<uint32_t>(variant,110524);
        if (scenario==4) Put<uint32_t>(ordinary+PHOrbLeaderCatalogNextOffset,10524);
        const auto before=realInput;
        const BOOL staged=[engine stageFixedPlanTarget:1 expected:&state frame:Frame() reason:nil];
        Check(staged==(scenario==0),
            "private target taps require the same full ID, exact parameter and a valid noncyclic native catalog chain");
        if (staged) Check(GlobalInput()!=Input && [engine finishInput:nil],"verified private target uses and releases the leased input bank");
        Check(GlobalInput()==Input && realInput==before,"private target staging and rejection preserve the real input manager");
    }
}
static void FixedNativeTests() {
    FixedSoloLeaderNativeTests();
    FixedPrivateSkillReadinessTests();
    FixedPrivateTargetNativeTests();
    FixedNativeFixture(); auto engine=[FixtureSkillEngine new]; PHOrbSnapshot state{}; NSString *reason=nil;
    Check([engine readAutomation:&state frame:Frame() reason:&reason] &&
        [engine readFixedPlan:&state frame:Frame() reason:&reason] && state.fixedPlan.targetAllowed &&
        state.fixedPlan.enemies[0].hp==100 && state.fixedPlan.enemies[0].maximumHP==1000,
        "fixed evidence uses fresh protected enemy HP and native target window");
    const auto before=realInput;
    Check([engine stageFixedPlanTarget:1 expected:&state frame:Frame() reason:&reason] && GlobalInput()!=Input,
        "target request stages only the leased input bank");
    Check([engine finishInput:&reason] && GlobalInput()==Input && realInput==before,
        "target input restores real manager without changing UIKit touches");
    for (unsigned scenario=0;scenario<6;++scenario) {
        FixedNativeFixture(); engine=[FixtureSkillEngine new];
        Check([engine readAutomation:&state frame:Frame() reason:nil] && [engine readFixedPlan:&state frame:Frame() reason:nil],"fresh fixed fixture");
        if (scenario==0) Put<uint16_t>(Impl+739544,1); // Forced target.
        if (scenario==1) Put<uint8_t>(Impl+600479,1); // Native debounce.
        if (scenario==2) Put<uint32_t>(Impl+PHOrbEnemyOffset+64,0); // Protected disagreement.
        if (scenario==3) Put<uint8_t>(Header+PHOrbFixedTextGuards[0].rva,0);
        if (scenario==4) Put<uint32_t>(Header+PHBattleMultiplayerRVA,1);
        if (scenario==5) Put<uintptr_t>(Impl+PHOrbEnemyOffset+PHOrbEnemyActionsParameterOffset,0);
        Check(![engine stageFixedPlanTarget:1 expected:&state frame:Frame() reason:nil] && GlobalInput()==Input,
            "changed target policy, HP, binary or multiplayer cannot stage a target tap");
    }
    SkillFixture(); engine=[FixtureSkillEngine new];
    Check([engine readAutomation:&state frame:Frame() reason:nil],"readiness probe fixture");
    Check([engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillBody,CurrentFixtureSkill}
        expected:&state frame:Frame() reason:nil]==PHOrbSkillStarted && !skillStartCalls && !writes,
        "fixed readiness query does not start or override a skill");
    Check([engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillBody,CurrentFixtureSkill+1}
        expected:&state frame:Frame() reason:&reason]==PHOrbSkillRejected && !skillStartCalls && !writes &&
        [reason containsString:[NSString stringWithFormat:@"預期本體 %u，實際 %u",CurrentFixtureSkill+1,CurrentFixtureSkill]],
        "wrong fixed skill stage is unknown, never optional unavailability");
    readinessFixture.assistSkill=CurrentFixtureSkill+2; readinessFixture.totalCooldown=12;
    Put<uint16_t>(Impl+PHBattleMemberOffset+PHBattleAssistIDOffset,400);
    Put<uint16_t>(Impl+PHBattleMemberOffset+1438,400);
    Protected(Impl+PHBattleMemberOffset+PHBattleAssistStateOffset,172,0x6a875a8b);
    Check([engine readAutomation:&state frame:Frame() reason:nil] &&
        [engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillAssist,CurrentFixtureSkill+2}
        expected:&state frame:Frame() reason:nil]==PHOrbSkillUnavailable && !skillStartCalls && !writes,
        "known uncharged fixed assist is unavailable without being misclassified as the current body skill");
    Check([engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillAssist,61754}
        expected:&state frame:Frame() reason:&reason]==PHOrbSkillRejected && !skillStartCalls && !writes &&
        [reason containsString:[NSString stringWithFormat:@"預期武裝 61754，實際 %u",CurrentFixtureSkill+2]] &&
        [reason containsString:@"階段 0／0"] && [reason containsString:@"CD 4／12"],
        "fixed assist mismatch exposes expected and actual IDs, stages and cooldown without authorizing a replacement");
    auto held=state.fixedPlan; held.valid=true; auto idle=held; held.targetAllowed=false; idle.targetAllowed=true;
    Check(PHOrbSameFixedPlan(&held,&idle),"owned drag closes only tap permission, not fixed evidence identity");
    held.count=idle.count=1; held.enemies[0].targetX=40; idle.enemies[0].targetX=50;
    Check(PHOrbSameFixedPlan(&held,&idle),"sprite motion does not invalidate fixed HP evidence during a drag");
    held.enemies[0].hp=1;
    Check(!PHOrbSameFixedPlan(&held,&idle),"actual enemy HP changes still invalidate a fixed route");
    for (unsigned scenario=0;scenario<3;++scenario) {
        SkillFixture(); engine=[FixtureSkillEngine new];
        const uintptr_t catalog=reinterpret_cast<uintptr_t>(catalogBytes.data());
        const uintptr_t parameter=catalog+14600*PHBattleCatalogStride;
        std::memcpy(reinterpret_cast<void *>(parameter),reinterpret_cast<void *>(catalog+100*PHBattleCatalogStride),PHBattleCatalogStride);
        Put<uint32_t>(Header+PHSaveRVA+PHOrbLeaderCatalogRecordsOffset,14601);
        Put<uint32_t>(catalog+100*PHBattleCatalogStride+PHOrbLeaderCatalogNextOffset,14600);
        Put<uint32_t>(parameter,1000100); Put<uint32_t>(Impl+PHBattleMemberOffset+PHBattleCatalogIDOffset,1000100);
        Put<uint16_t>(Impl+PHBattleMemberOffset+PHBattleBaseIDOffset,100);
        Put<uintptr_t>(Impl+PHBattleMemberOffset+PHBattleCardParamOffset,
            scenario==1 ? catalog+100*PHBattleCatalogStride : parameter);
        if (scenario==2) Put<uint16_t>(Impl+PHBattleMemberOffset+PHBattleBaseIDOffset,101);
        reason=nil;
        const BOOL read=[engine readAutomation:&state frame:Frame() reason:&reason];
        if (read!=(scenario==0)) std::fprintf(stderr,"Fixed variant scenario %u: read=%d reason=%s\n",
            scenario,int(read),reason.UTF8String ?: "nil");
        Check(read==(scenario==0),"solo fixed variants require the exact native record and unchanged underlying source");
        if (read) Check(state.automation.members[0].number==100 && state.automation.members[0].sourceCatalogIDs[0]==100,
            "verified private variants preserve the public form and original fixed source separately");
    }
    FixedNativeFixture(); engine=[FixtureSkillEngine new];
    const uintptr_t part=Impl+PHOrbEnemyOffset+PHOrbEnemyStride;
    Put<uint16_t>(part+532,0x100); Put<uint32_t>(part+580,0);
    Put<uint8_t>(part+36,0); Put<uint8_t>(part+37,0);
    Protected(part+2704,0,0x6a875a8b); Protected(part+2672,0,0x6a875a8b); Protected(part+2688,50,0x6a875a8b);
    Check([engine readAutomation:&state frame:Frame() reason:nil] && [engine readFixedPlan:&state frame:Frame() reason:nil] &&
        state.fixedPlan.partsMask==2 && state.fixedPlan.enemies[1].hp==50,
        "part identity comes from protected mode, HP and living owner without an ordinary catalog record");
    Protected(part+2704,255,0x6a875a8b);
    Check([engine readAutomation:&state frame:Frame() reason:nil] && [engine readFixedPlan:&state frame:Frame() reason:nil] &&
        state.automation.partsMask==2 && !state.fixedPlan.partsMask && !state.fixedPlan.enemies[1].hp,
        "destroyed parts retain branch identity while ceasing to be live targets");
    Reset();
}
