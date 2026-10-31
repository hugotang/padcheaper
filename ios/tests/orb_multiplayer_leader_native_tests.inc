// Native fixture only; it neither invokes networking nor proves device sync.
static void SetNativeLeader(unsigned slot, uint16_t number, uint32_t skill, unsigned source) {
    SetLeader(slot,number);
    const uintptr_t member = Impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    const uint32_t id = Load<uint32_t>(reinterpret_cast<uint8_t *>(member + PHBattleCatalogIDOffset));
    const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(member + PHBattleCardParamOffset));
    Put<uint16_t>(member + PHBattleBaseIDOffset,uint16_t(id));
    Protected(member + PHBattleSourceSlotOffset,source,0x6a875a8b);
    Protected(member + PHBattleFormOffset,0,0x6a875a8b);
    Protected(member + PHBattleTempIDOffset,0,0x6a875a8b);
    Protected(member + PHBattleTempTurnsOffset,0,0x6a875a8b);
    Put<uint32_t>(parameter + PHOrbLeaderSkillOffset,skill);
    Put<uint32_t>(parameter + PHOrbLeaderCatalogNextOffset,0);
    Put<uint32_t>(Header + PHSaveRVA + PHOrbSkillCountOffset,70000);
}
static void SetNativeLeaderOwner(unsigned seat) {
    const uintptr_t descriptor = Impl + PHBattleTeamDescriptorOffset + seat * PHBattleTeamDescriptorStride;
    Put<uint8_t>(descriptor,1);
    Put<uintptr_t>(descriptor + PHBattleTeamPointerOffset,Header + PHBattleTeamStorageRVA + seat * PHBattleTeamStorageStride);
}
static void NativeLeaderFixture(unsigned players, unsigned seat) {
    Reset(); SetNativeMultiplayer(players,seat); SetNativeLeaderOwner(seat);
    SetNativeLeader(0,14242,66063,0); SetNativeLeader(5,13834,63039,5);
}
static uintptr_t SetNativeExtraHelper() {
    const uintptr_t member = Impl + PHBattleMemberOffset + 5 * PHBattleMemberStride;
    const uintptr_t rows = Header + 0x1820000, registry = Header + PHOrbLeaderExtraRegistryRVA;
    Put<uint16_t>(registry + 4,1); Put<uintptr_t>(registry + 8,rows);
    Put<uint32_t>(Header + PHSaveRVA + PHOrbLeaderExtraSelectionOffset,73);
    Put<uint32_t>(rows,20000073); Put<uint32_t>(rows + 8,10000000);
    Put<uint32_t>(rows + 8 + PHOrbLeaderExtraSkillOffset,51);
    Put<uint32_t>(rows + 8 + PHOrbLeaderSkillOffset,0);
    Put<uint8_t>(member,0); Put<uint8_t>(member + PHOrbLeaderExtraFlagOffset,2);
    Put<uint32_t>(member + PHBattleCatalogIDOffset,10000000); Put<uint16_t>(member + PHBattleBaseIDOffset,uint16_t(10000000u));
    Put<uintptr_t>(member + PHBattleCardParamOffset,rows + 8);
    return rows;
}
static void MultiplayerLeaderNativeTests() {
    for (unsigned players : {3u,4u,8u}) for (unsigned seat = 0; seat < players; ++seat) {
        NativeLeaderFixture(players,seat);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot s{}; NSString *reason = nil;
        const auto originalGuest = guest, originalImpl = implBytes, originalInput = realInput;
        Check([engine read:&s frame:Frame() reason:&reason] && PHOrbLeaderContextReady(&s) &&
            s.leaderNumbers[0] == 14242 && s.leaderNumbers[1] == 13834 &&
            s.leaderContext.skillIDs[0] == 66063 && s.leaderContext.skillIDs[1] == 63039 &&
            !s.automation.valid && !s.thorns.known && !writes && guest == originalGuest &&
            implBytes == originalImpl && realInput == originalInput,
            "each multiplayer seat reads its live leader pair and skills without mutating native state");
        if (players == 3 && seat == 1) {
            Put<uint32_t>(Impl + PHBattleTeamCountOffset,2);
            Check([engine read:&s frame:Frame() reason:&reason] && PHOrbLeaderContextReady(&s),
                "mode 7 with two remaining participants keeps current local leader support");
        }
    }
    const uintptr_t first = Impl + PHBattleMemberOffset, helper = first + 5 * PHBattleMemberStride;
    for (unsigned failure = 0; failure < 15; ++failure) {
        NativeLeaderFixture(4,2);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot s{}; NSString *reason = nil;
        Check([engine read:&s frame:Frame() reason:&reason] && s.leadersValid,"capture before leader read failure");
        const uintptr_t descriptor = Impl + PHBattleTeamDescriptorOffset + 2 * PHBattleTeamDescriptorStride;
        const uintptr_t parameter = s.leaderContext.parameters[0];
        if (failure == 0) Put<uint8_t>(descriptor,0);
        if (failure == 1) Put<uintptr_t>(descriptor + PHBattleTeamPointerOffset,Header + PHBattleTeamStorageRVA);
        if (failure == 2) Put<uint8_t>(Impl + PHBattleTeamBusyOffset,8);
        if (failure == 3) Protected(first + PHBattleSourceSlotOffset,5,0x6a875a8b);
        if (failure == 4) Put<uint32_t>(first + PHBattleSourceSlotOffset,1);
        if (failure == 5) Put<uint16_t>(first + PHBattleBaseIDOffset,1);
        if (failure == 6) Protected(first + PHBattleTempTurnsOffset,2,0x6a875a8b);
        if (failure == 7) Put<uintptr_t>(first + PHBattleCardParamOffset,parameter + PHBattleCatalogStride);
        if (failure == 8) Put<uint32_t>(parameter,1);
        if (failure == 9) Put<uint32_t>(parameter + PHOrbLeaderSkillOffset,70000);
        if (failure == 10) Put<uint8_t>(helper,0);
        if (failure == 11) Put<uint8_t>(helper + PHOrbLeaderExtraFlagOffset,2);
        if (failure == 12) changeLeaderDuringPairRead = true;
        if (failure == 13) Put<uint32_t>(Header + PHSaveRVA + PHOrbSkillCountOffset,0);
        if (failure == 14) Put<uint8_t>(helper,3);
        Check([engine read:&s frame:Frame() reason:&reason] && PHOrbCanPreview(&s) && !s.leadersValid &&
            !s.leaderNumbers[0] && !s.leaderNumbers[1] && !s.leaderContext.known && !writes,
            "bad leader context publishes neither partial nor old pair; Combo/manual board remains readable");
    }
    NativeLeaderFixture(3,1);
    id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot before{}, after{}; NSString *reason = nil;
    Check([engine read:&before frame:Frame() reason:&reason],"source swap capture");
    Protected(first + PHBattleSourceSlotOffset,2,0x6a875a8b);
    Check([engine read:&after frame:Frame() reason:&reason] && after.leadersValid &&
        !PHOrbSameLeaders(&before,&after) && before.leaderNumbers[0] == after.leaderNumbers[0],
        "same-card leader swap is detected through the protected source slot");
    SetNativeLeader(0,14190,65762,2);
    Put<uint16_t>(first + PHBattleBaseIDOffset,14289);
    Protected(first + PHBattleFormOffset,14290,0x6a875a8b);
    Check([engine read:&before frame:Frame() reason:&reason] && before.leadersValid && before.leaderNumbers[0] == 14190,
        "permanent transformed leader uses its current catalog and native skill");
    SetNativeLeader(0,12572,54198,2); Put<uint16_t>(first + PHBattleBaseIDOffset,14289);
    Protected(first + PHBattleFormOffset,14290,0x6a875a8b);
    Protected(first + PHBattleTempIDOffset,12672,0x6a875a8b); Protected(first + PHBattleTempTurnsOffset,2,0x6a875a8b);
    Check([engine read:&after frame:Frame() reason:&reason] && after.leadersValid && after.leaderNumbers[0] == 12572 &&
        after.leaderContext.skillIDs[0] == 54198 && !PHOrbSameLeaders(&before,&after),
        "temporary leader takes precedence over its permanent form and source");

    NativeLeaderFixture(4,3); engine = PHCreateOrbEngine();
    const uintptr_t catalog = reinterpret_cast<uintptr_t>(catalogBytes.data()), variant = catalog + 14550 * PHBattleCatalogStride;
    Put<uint32_t>(catalog + 14342 * PHBattleCatalogStride + PHOrbLeaderCatalogNextOffset,14550);
    Put<uint32_t>(variant,1014342); Put<uint32_t>(variant + PHOrbLeaderSkillOffset,54198);
    Put<uint32_t>(variant + PHOrbLeaderCatalogNextOffset,0);
    Put<uint32_t>(first + PHBattleCatalogIDOffset,1014342); Put<uintptr_t>(first + PHBattleCardParamOffset,variant);
    Check([engine read:&before frame:Frame() reason:&reason] && before.leadersValid && before.leaderNumbers[0] == 14242 &&
        before.leaderContext.catalogIDs[0] == 1014342 && before.leaderContext.skillIDs[0] == 54198,
        "fixed-team variant follows the native catalog link and retains its own skill");
    Put<uint32_t>(variant,1); Put<uint32_t>(variant + PHOrbLeaderCatalogNextOffset,14550);
    Check([engine read:&after frame:Frame() reason:&reason] && !after.leadersValid,
        "malformed variant catalog cycle terminates with an unavailable leader pair");

    for (unsigned failure = 0; failure < 11; ++failure) {
        NativeLeaderFixture(4,1); engine = PHCreateOrbEngine(); const uintptr_t rows = SetNativeExtraHelper();
        Check([engine read:&before frame:Frame() reason:&reason] && PHOrbLeaderContextReady(&before) &&
            before.leaderContext.extraHelper && before.leaderNumbers[1] == 0,
            "4P native extra helper is recognized through its selected private row");
        if (failure == 0) Put<uint8_t>(helper,1);
        if (failure == 1) Put<uint8_t>(helper + PHOrbLeaderExtraFlagOffset,0);
        if (failure == 2) Put<uint32_t>(Header + PHSaveRVA + PHOrbLeaderExtraSelectionOffset,74);
        if (failure == 3) Put<uintptr_t>(helper + PHBattleCardParamOffset,rows + 16);
        if (failure == 4) Put<uint16_t>(Header + PHOrbLeaderExtraRegistryRVA + 4,0);
        if (failure == 5) Put<uint32_t>(rows + 8 + PHOrbLeaderExtraSkillOffset,0);
        if (failure == 6) Protected(helper + PHBattleSourceSlotOffset,4,0x6a875a8b);
        if (failure == 7) Protected(helper + PHBattleTempTurnsOffset,1,0x6a875a8b);
        if (failure == 8) Put<uint16_t>(helper + PHBattleBaseIDOffset,1);
        if (failure == 9) Put<uint16_t>(Header + PHOrbLeaderExtraRegistryRVA + 4,4097);
        if (failure == 10) { SetNativeMultiplayer(8,1); SetNativeLeaderOwner(1); }
        Check([engine read:&after frame:Frame() reason:&reason] && !after.leadersValid && !writes,
            "unknown extra-helper state cannot be treated as a normal leader or as an empty condition");
    }
    for (bool race : {false,true}) {
        NativeLeaderFixture(3,0); engine = PHCreateOrbEngine();
        Check([engine read:&before frame:Frame() reason:&reason],"capture leader pair before input race");
        [engine armMultiplayerOnce:YES];
        if (race) changeLeaderDuringCoordinateRead = true;
        else Protected(first + PHBattleSourceSlotOffset,2,0x6a875a8b);
        Check(![engine stageCell:0 down:YES beginning:YES expected:&before frame:Frame() reason:&reason] && !writes && GlobalInput() == Input,
            "changed leader before begin or during coordinate conversion cannot acquire input");
    }
    NativeLeaderFixture(3,0); engine = PHCreateOrbEngine(); StartNativeMultiplayer(engine,before);
    Protected(first + PHBattleSourceSlotOffset,2,0x6a875a8b);
    const unsigned count = writes;
    Check(![engine stageCell:1 down:YES beginning:NO expected:&before frame:Frame() reason:&reason] && writes == count,
        "leader changes during a held gesture stop further movement");
    Check([engine read:&after frame:Frame() reason:&reason] &&
        [engine stageCell:0 down:NO beginning:NO expected:&after frame:Frame() reason:&reason],
        "same-window leader change still permits releasing only the owned orb");
    NativeInputFrame(); Check([engine finishInput:&reason] && GlobalInput() == Input,"leader-change release restores real manager");
    Reset();
}
