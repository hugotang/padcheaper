static void TestBattleMemberModel(void) {
    PHBattleMemberIdentity member = {.currentCatalogID = 101, .sourceBodyID = 101,
        .sourceAssistID = 301, .effectiveAssistID = 301};
    uint32_t body = 0; uint16_t assist = 0;
    Check(PHBattlePlanningIdentity(&member, &body, &assist) && body == 101 && assist == 301,
          "ordinary planning identity uses the current effective pair");
    member.currentCatalogID = member.persistentFormID = 201;
    Check(PHBattlePlanningIdentity(&member, &body, &assist) && body == 201 && assist == 301,
          "persistent transformation does not revert to the source body");
    member.currentCatalogID = member.temporaryFormID = 401; member.temporaryTurns = 3; member.effectiveAssistID = 0;
    Check(PHBattlePlanningIdentity(&member, &body, &assist) && body == 201 && assist == 301,
          "temporary transformation preserves underlying identity only for planning");
    member.persistentFormID = 0;
    Check(PHBattlePlanningIdentity(&member, &body, &assist) && body == 101 && assist == 301,
          "temporary transformation can restore an untransformed source identity");
    member.currentCatalogID = 402; body = 77; assist = 88;
    Check(!PHBattlePlanningIdentity(&member, &body, &assist) && body == 77 && assist == 88,
          "mismatched temporary form cannot publish a planning identity");
    member.temporaryTurns = 0;
    Check(PHBattlePlanningIdentity(&member, &body, &assist) && body == 402 && assist == 0,
          "expired temporary fields never override current effective identity");
    member.currentCatalogID = 1001645;
    Check(PHBattlePlanningIdentity(&member, &body, &assist) && body == 1001645,
          "shared identity never truncates fixed multiplayer catalog variants");
    PHBattleMemberIdentity changed = member;
    Check(PHBattleSameMemberIdentity(&member, &changed), "identical captured member facts compare equal");
    changed.sourceAssistID++;
    Check(!PHBattleSameMemberIdentity(&member, &changed), "source changes invalidate identity even with the same empty effective assist");
    changed = member; changed.persistentFormID++;
    Check(!PHBattleSameMemberIdentity(&member, &changed), "underlying form changes invalidate identity even when hidden by a temporary form");

    PHBattleDungeonMemberRules ordinary = {.address = 100, .known = true}, disabled = ordinary, unknown = {0};
    disabled.flags = PHBattleDungeonAssistsDisabled;
    const uint8_t configured[8] = {0x2d, 1, 0x89, 99, 2, 0, 0, 0};
    uint8_t actual[8]; memcpy(actual, configured, sizeof(actual)); actual[7] = 0xf1;
    Check(PHBattleEffectiveAssistMatches(actual, configured, &ordinary), "effective descriptor ignores only native padding");
    actual[4] = 3;
    Check(!PHBattleEffectiveAssistMatches(actual, configured, &ordinary), "an active assist skill level remains significant");
    const uint8_t absent[8] = {0, 0, 0, 1, 0xa3, 0x77, 0, 0xfc};
    Check(PHBattleEffectiveAssistMatches(absent, configured, &disabled) &&
          !PHBattleEffectiveAssistMatches(absent, configured, &ordinary),
          "empty effective assist is accepted only under the observed dungeon ban");
    Check(!PHBattleEffectiveAssistMatches(configured, configured, &disabled) &&
          !PHBattleEffectiveAssistMatches(absent, configured, &unknown),
          "a ban requires empty effective assist and unknown rules grant no exemption");
    Check(PHBattleEffectiveAssistAvailable(&ordinary, 301) &&
          !PHBattleEffectiveAssistAvailable(&disabled, 301) && !PHBattleEffectiveAssistAvailable(&ordinary, 0) &&
          !PHBattleEffectiveAssistAvailable(&ordinary, UINT16_MAX) && !PHBattleEffectiveAssistAvailable(&unknown, 301),
          "skill-source eligibility needs known rules and a positive signed effective ID");
    Check(PHBattleSameDungeonMemberRules(&ordinary, &ordinary) &&
          !PHBattleSameDungeonMemberRules(&ordinary, &disabled) && !PHBattleSameDungeonMemberRules(&unknown, &unknown),
          "unavailable and changed rules never compare as confirmed equal");
    Check(PHBattleAssistIsRetained(172) && PHBattleAssistIsRetained(0x100 | 172) && !PHBattleAssistIsRetained(0),
          "assist retention uses the native low byte without discarding captured word flags");
}
