static void ThornNativeTests() {
    for (unsigned scenario = 0; scenario < 7; ++scenario) {
        SkillFixture(); SetSurvivalHP(1000,1000);
        Put<uint32_t>(OrbCell(1) + 4,PHOrbFlagThorn); Put<uint8_t>(OrbCell(1) + 12,4);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && state.thorns.known && state.thorns.hp == 1000 && !writes,
              "thorn planning reads protected current HP, effective maximum and pending damage without native calls or writes");
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
              "thorn input fixture begins on an ordinary held token");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset,state.board.tokens[0]);
        Check([engine finishInput:&reason],"thorn input fixture returns the real input bank");
        if (scenario == 1) SetSurvivalHP(40,1000);
        if (scenario == 2) { Put<uint32_t>(Impl + PHOrbPendingDamageOffset,960); Put<uint32_t>(Impl + PHOrbPendingThornDamageOffset,960); }
        if (scenario == 3) Put<uint32_t>(Impl + PHBattleHPShadowOffset,0);
        if (scenario == 4) Put<uint32_t>(Impl + PHOrbPendingThornDamageOffset,1);
        if (scenario == 5) SetSurvivalHP(41,1000);
        if (scenario == 6) SetSurvivalHP(1000,2000);
        Check([engine read:&state frame:Frame() reason:&reason],"updated thorn budget is sampled before each held frame");
        unsigned before = writes;
        bool allowed = scenario == 0 || scenario == 5 || scenario == 6;
        Check(bool([engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason]) == allowed,
              "native input rechecks HP, maximum, pending damage and consistency before touching thorns");
        if (allowed) Check([engine finishInput:&reason],"accepted thorn input restores its private bank");
        else Check(writes == before && [engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] &&
                   [engine finishInput:&reason],"rejected contact still allows releasing the held orb without damage");
    }
}
