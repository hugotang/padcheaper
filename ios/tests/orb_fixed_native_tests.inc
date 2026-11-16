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
static void FixedNativeTests() {
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
        expected:&state frame:Frame() reason:nil]==PHOrbSkillRejected && !skillStartCalls,
        "wrong fixed skill stage is unknown, never optional unavailability");
    readinessFixture.assistSkill=CurrentFixtureSkill+2; readinessFixture.totalCooldown=12;
    Put<uint16_t>(Impl+PHBattleMemberOffset+PHBattleAssistIDOffset,400);
    Put<uint16_t>(Impl+PHBattleMemberOffset+1438,400);
    Protected(Impl+PHBattleMemberOffset+PHBattleAssistStateOffset,172,0x6a875a8b);
    Check([engine readAutomation:&state frame:Frame() reason:nil] &&
        [engine fixedSkillReadiness:0 request:PHOrbSkillRequest{PHOrbSkillAssist,CurrentFixtureSkill+2}
        expected:&state frame:Frame() reason:nil]==PHOrbSkillUnavailable && !skillStartCalls && !writes,
        "known uncharged fixed assist is unavailable without being misclassified as the current body skill");
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
        Put<uintptr_t>(Impl+PHBattleMemberOffset+PHBattleCardParamOffset,
            scenario==1 ? catalog+100*PHBattleCatalogStride : parameter);
        if (scenario==2) Put<uint16_t>(Impl+PHBattleMemberOffset+PHBattleBaseIDOffset,101);
        const BOOL read=[engine readAutomation:&state frame:Frame() reason:nil];
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
