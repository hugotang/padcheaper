static void EnemyThreatNativeTests() {
    EnemyActionFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    EnemyActionSetRecord(20,83,{21,22,23,0}); EnemyActionSetRecord(21,18,{2,200});
    EnemyActionSetRecord(22,15,{2,4,50}); EnemyActionSetRecord(23,89,{2,5});
    EnemyActionSetRow(5,20);
    const auto originalGuest = guest, originalImpl = implBytes;
    NSDictionary *trace = CaptureEnemyActions(engine), *threat = trace[@"enemies"][0][@"threatEvents"];
    NSDictionary *action = nil;
    for (NSDictionary *entry in threat[@"actions"]) if ([entry[@"actionID"] intValue] == 20) action = entry;
    Check([threat[@"status"] isEqual:@"conditional-events"] && [action[@"eventOrderKnown"] boolValue] &&
        [action[@"events"] count] == 6 && [action[@"preShieldBoundKnown"] boolValue] &&
        [action[@"preShieldDamageUpper"] longLongValue] == 80025 && [action[@"hitsUpper"] isEqual:@4],
        "raw captured compound enrage/multihit/delay definitions produce an ordered conditional threat");
    Check(guest == originalGuest && implBytes == originalImpl && !writes && !canUseCalls &&
        ![threat[@"enemyForecastKnown"] boolValue] && ![action[@"survivalKnown"] boolValue],
        "threat interpretation cannot call native AI or promote current attack values into survival authority");
    EnemyActionSetRecord(22,50,{50}); trace = CaptureEnemyActions(engine);
    for (NSDictionary *entry in trace[@"enemies"][0][@"threatEvents"][@"actions"])
        if ([entry[@"actionID"] intValue] == 20) action = entry;
    Check([action[@"eventOrderKnown"] boolValue] && ![action[@"preShieldBoundKnown"] boolValue],
        "gravity never borrows current HP as an unproven post-player HP cap");
    Put<uint8_t>(Header + PHOrbEnemyThreatTextGuards[0].rva,uint8_t(PHOrbEnemyThreatTextGuards[0].bytes[0] ^ 1u));
    trace = CaptureEnemyActions(engine);
    PHOrbSnapshot state{};
    Check([trace[@"status"] isEqual:@"ready"] &&
        [trace[@"enemies"][0][@"threatEvents"][@"status"] isEqual:@"unavailable"] &&
        [engine readAutomation:&state frame:Frame() reason:nil],
        "threat-only guard mismatch withdraws its interpretation without disabling ordinary automation");
    Reset();
}
