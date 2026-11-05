static void TestBattleMemberReader() {
    Fixture f;
    Fixture::put<uint16_t>(f.member(0) + PHBattleAssistIDOffset, 301);
    Fixture::put<uint16_t>(f.member(0) + PHBattleEffectiveAssistOffset, 0);
    Fixture::encode(f.member(0) + PHBattleFormOffset, 201);
    Fixture::encode(f.member(0) + PHBattleTempIDOffset, 401);
    Fixture::encode(f.member(0) + PHBattleTempTurnsOffset, 3);
    Fixture::put<uint32_t>(f.member(0) + PHBattleCatalogIDOffset, 401);
    PHBattleMemberIdentity live{}, buffered{};
    Check(PHReadBattleMemberIdentity(f.member(0), Fixture::Key1, Fixture::Key2, live) &&
          PHReadBattleMemberIdentityBytes(reinterpret_cast<const uint8_t *>(f.member(0)), PHBattleMemberStride,
              Fixture::Key1, Fixture::Key2, buffered), "live and snapshot adapters decode the same raw member");
    Check(live.currentCatalogID == 401 && live.sourceBodyID == 100 && live.sourceAssistID == 301 &&
          !live.effectiveAssistID && live.persistentFormID == 201 && live.temporaryFormID == 401 && live.temporaryTurns == 3 &&
          buffered.currentCatalogID == live.currentCatalogID && buffered.sourceBodyID == live.sourceBodyID &&
          buffered.sourceAssistID == live.sourceAssistID && buffered.effectiveAssistID == live.effectiveAssistID &&
          buffered.persistentFormID == live.persistentFormID && buffered.temporaryFormID == live.temporaryFormID &&
          buffered.temporaryTurns == live.temporaryTurns, "readers preserve every source/effective/form distinction");
    for (uintptr_t offset : {PHBattleFormOffset, PHBattleTempIDOffset, PHBattleTempTurnsOffset}) {
        uint32_t old = Fixture::get<uint32_t>(f.member(0) + offset);
        Fixture::put<uint32_t>(f.member(0) + offset, old ^ 1u);
        PHBattleMemberIdentity sentinel{}; sentinel.currentCatalogID = 77;
        Check(!PHReadBattleMemberIdentity(f.member(0), Fixture::Key1, Fixture::Key2, sentinel) &&
              sentinel.currentCatalogID == 77, "corrupt form replicas never publish partial member facts");
        Fixture::put<uint32_t>(f.member(0) + offset, old);
    }
    f.failReadAddress = f.member(0) + PHBattleAssistIDOffset;
    live.currentCatalogID = 77;
    Check(!PHReadBattleMemberIdentity(f.member(0), Fixture::Key1, Fixture::Key2, live) && live.currentCatalogID == 77,
          "failed source reads leave caller output unchanged");
    f.failReadAddress = 0;
    Check(!PHReadBattleMemberIdentityBytes(reinterpret_cast<const uint8_t *>(f.member(0)), 64,
              Fixture::Key1, Fixture::Key2, live) && live.currentCatalogID == 77,
          "short buffered members fail without reading outside the snapshot");
    PHBattleDungeonMemberRules rules{}, absent{}, unavailable{};
    Fixture::put<uint8_t>((uintptr_t)f.dungeon.data() + 33, 8);
    Check(PHReadBattleDungeonMemberRules(f.impl(), rules) && rules.known && rules.address == (uintptr_t)f.dungeon.data() &&
          PHBattleDungeonDisables(&rules, PHBattleDungeonAssistsDisabled), "shared reader captures dungeon assist ban");
    Check(PHReadDungeonMemberRules(0, absent) && absent.known && !absent.address && !absent.flags,
          "known absence of a dungeon rule object remains distinct from failed reads");
    f.failReadAddress = (uintptr_t)f.dungeon.data() + 33;
    Check(!PHReadBattleDungeonMemberRules(f.impl(), unavailable) && !unavailable.known,
          "unreadable rules never become known permissive defaults");
    Check(!PHReadDungeonMemberRules(UINTPTR_MAX, unavailable), "overflowing dungeon address is rejected");
    Check(!f.writeCalls && !f.rebuilds && !f.allocated, "shared readers never rebuild, allocate or write");
}
