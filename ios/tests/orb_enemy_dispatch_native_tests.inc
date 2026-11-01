// Written, not run. Use actual optional reader and full existing board fixture.
static void EnemyDispatchFixture() {
    EnemySelectorFixture();
    for (const auto &guard : PHOrbEnemyDispatchTextGuards) std::memcpy(reinterpret_cast<void *>(Header + guard.rva), guard.bytes, 64);
    Put<uint8_t>(Impl + PHOrbEnemyDispatchCacheOffset, 1);
    Put<uint8_t>(Impl + PHOrbEnemyDispatchHookOffset, 0);
    Put<uint32_t>(Impl + PHOrbEnemyDispatchSpecialOffset, 0);
    Put<uint32_t>(Impl + PHOrbEnemyDispatchLoopOffset, 0);
    Protected(Impl + PHOrbEnemyDispatchRedirectOffset, UINT32_MAX, 0x6a875a8b);
    Protected(Impl + PHOrbEnemyDispatchRedirectOffset + 16, 0, 0x6a875a8b);
    Protected(Impl + PHOrbEnemyDispatchRedirectOffset + 32, 0, 0x6a875a8b);
    const auto dungeon = Load<uintptr_t>(reinterpret_cast<const uint8_t *>(Impl + PHBattleDungeonPointerOffset));
    const auto flags = Load<uint16_t>(reinterpret_cast<const uint8_t *>(dungeon + 24));
    Put<uint16_t>(dungeon + 24, uint16_t(flags | 0x80u));
    const auto enemy = Impl + PHOrbEnemyOffset;
    Put<uint8_t>(enemy + 60, 0); Put<float>(enemy + 904, 1); Put<float>(enemy + 908, 1);
    Protected(enemy + 2032, 0, 0x6a875a8b); Protected(enemy + 2736, 59999, 0x6a875a8b);
}
static void EnemyDispatchNativeTests() {
    EnemySelectorFixture(); FixtureSkillEngine *engine = [FixtureSkillEngine new];
    NSDictionary *trace = CaptureEnemyActions(engine);
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"selectorAnalysis"][@"status"] isEqual:@"ready"] &&
        [trace[@"dispatchAnalysis"][@"status"] isEqual:@"unavailable"],
        "existing action and selector diagnostics work without optional dispatch guards");
    EnemyDispatchFixture(); engine = [FixtureSkillEngine new]; PHOrbSnapshot state{};
    const auto beforeGuest = guest, beforeImpl = implBytes, beforeCatalog = catalogBytes;
    trace = CaptureEnemyActions(engine, &state);
    NSDictionary *dispatch = trace[@"enemies"][0][@"dispatchAnalysis"];
    Check([trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"] &&
        [dispatch[@"current"][@"route"] isEqual:@"countdown-pending"] &&
        [dispatch[@"afterConditionalTick"][@"countdown"] intValue] == 0 &&
        [dispatch[@"afterConditionalTick"][@"entry"][@"route"] isEqual:@"ordinary-row-zero"] &&
        [dispatch[@"afterConditionalTick"][@"entry"][@"selectedCacheCleared"] boolValue] &&
        ![dispatch[@"postPlayerStateKnown"] boolValue] && ![dispatch[@"nextEntryKnown"] boolValue],
        "one assumed tick clears positive cached action but never establishes post-player state");
    NSDictionary *candidate = trace[@"enemies"][0][@"selectorAnalysis"][@"candidates"][0];
    NSDictionary *post = candidate[@"postSelectionCheckpoint"];
    Check([post[@"status"] isEqual:@"conditional-checkpoint"] && [post[@"actionCountAtCheckpoint"] intValue] == 10 &&
        [post[@"budgetAfterRegeneration"] intValue] == 21 && [post[@"protectedCountAtCheckpoint"] intValue] == 60000 &&
        ![post[@"postPreparationStateKnown"] boolValue] && ![trace[@"enemyForecastKnown"] boolValue] &&
        guest == beforeGuest && implBytes == beforeImpl && catalogBytes == beforeCatalog && !writes && !canUseCalls,
        "candidate accounting is separately conditional and performs no native calls or mutations");
    Check([trace[@"dispatchAnalysis"][@"readBytes"] unsignedIntValue] <= ED::MaxReadBytes &&
        [trace[@"dispatchAnalysis"][@"readCalls"] unsignedIntValue] <= ED::MaxReads,
        "extra diagnostic has its own bounded data reads");
    uint8_t bytes[2]{};
    EnemyDispatchCapture limited{}; limited.readBytes = ED::MaxReadBytes - 1;
    Check(!EnemyDispatchRead(limited, Impl, bytes, 2) && limited.readBudgetExceeded && !limited.readCalls,
        "extra byte limit rejects before invoking a guest read");
    limited = {}; limited.readCalls = ED::MaxReads;
    Check(!EnemyDispatchRead(limited, Impl, bytes, 1) && limited.readBudgetExceeded && !limited.readBytes,
        "extra call limit cannot be bypassed with a small read");
    limited = {};
    Check(!EnemyDispatchRead(limited, UINTPTR_MAX - 1, bytes, 2), "overflowing optional memory span is rejected");
    const auto enemy = Impl + PHOrbEnemyOffset;
    Protected(enemy + 296, 0, 0x766cfc6d); trace = CaptureEnemyActions(engine);
    Check([trace[@"enemies"][0][@"dispatchAnalysis"][@"current"][@"route"] isEqual:@"cached-action"],
        "current dispatch uses positive cache before running fresh selector");
    Put<uint32_t>(enemy + 2012, (2u << 9) | 0x100u); trace = CaptureEnemyActions(engine);
    Check([trace[@"enemies"][0][@"dispatchAnalysis"][@"current"][@"route"] isEqual:@"compound-continuation"],
        "compound continuation precedes cached path in optional adapter");
    Put<uint32_t>(enemy + 2012, 0); Put<uint8_t>(Impl + PHOrbEnemyDispatchCacheOffset, 0);
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        Put<uint32_t>(Impl + PHOrbEnemyDispatchSpecialOffset, scenario == 0 ? ED::SpecialMarker : 0);
        Put<uint8_t>(Impl + PHOrbEnemyDispatchHookOffset, scenario == 1 ? 1 : 0);
        Protected(enemy + 2032, scenario == 2 ? 1 : 0, 0x6a875a8b);
        trace = CaptureEnemyActions(engine);
        NSString *expected = scenario == 0 ? @"special-mode-predicate-unresolved" :
            scenario == 1 ? @"pre-dispatch-hook-unresolved" : @"selector-negative-fallback";
        Check([trace[@"enemies"][0][@"dispatchAnalysis"][@"current"][@"route"] isEqual:expected],
            "special predicate, pre-dispatch hook and selector negative fallback remain distinct");
    }
    Put<uint8_t>(Impl + PHOrbEnemyDispatchHookOffset, 0); Protected(enemy + 2032, 0, 0x6a875a8b);
    Protected(enemy + 2336, 1, 0x6a875a8b); Protected(enemy + 2352, 7, 0x6a875a8b);
    trace = CaptureEnemyActions(engine); post = trace[@"enemies"][0][@"dispatchAnalysis"][@"postSelectionCheckpoint"];
    Check([trace[@"enemies"][0][@"dispatchAnalysis"][@"current"][@"route"] isEqual:@"override-action"] &&
        [post[@"selectedAtCheckpoint"] intValue] == -1 && ![post[@"costApplied"] boolValue] &&
        [post[@"budgetAtCheckpoint"] intValue] == 21 && [post[@"negativeSelectionFallback"] boolValue],
        "direct override to root82 accounts for regeneration before retaining a normal attack fallback");
    Protected(enemy + 2336, 0, 0x6a875a8b);
    Protected(Impl + PHOrbEnemyDispatchRedirectOffset, 0, 0x6a875a8b);
    Protected(Impl + PHOrbEnemyDispatchRedirectOffset + 32, 10, 0x6a875a8b);
    EnemyActionSetRecord(10, 180, {0,0,0,0,0,0,4,64});
    for (uint8_t choice : {0, 2}) {
        Protected(Impl + PHOrbEnemyDispatchRedirectOffset + 16, choice, 0x6a875a8b);
        trace = CaptureEnemyActions(engine); dispatch = trace[@"enemies"][0][@"dispatchAnalysis"];
        Check([dispatch[@"current"][@"route"] isEqual:@"redirected-row-entry"] &&
            [dispatch[@"current"][@"redirectRow"] intValue] == (choice ? 4 : 64) &&
            [trace[@"dispatchAnalysis"][@"redirectDefinitionHex"] length] == EA::RecordBytes * 2 &&
            [trace[@"selectorAnalysis"][@"status"] isEqual:@"ready"] &&
            [trace[@"enemies"][0][@"selectorAnalysis"][@"candidates"] count] == 2,
            "separate opcode180 redirect lookup leaves hypothetical row-zero walk visibly separate");
    }
    Protected(Impl + PHOrbEnemyDispatchRedirectOffset, UINT32_MAX, 0x6a875a8b);
    for (uint32_t bits : {0x80000000u, 0x7fc00001u}) {
        Put<uint32_t>(enemy + 904, bits); Put<uint32_t>(enemy + 908, bits == 0x80000000u ? 0 : bits);
        trace = CaptureEnemyActions(engine);
        Check([trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"] &&
            [trace[@"enemies"][0][@"dispatchAnalysis"][@"current"][@"route"] isEqual:@"ordinary-row-zero"] &&
            [trace[@"enemies"][0][@"dispatchAnalysis"][@"current"][@"selectedCacheCleared"] boolValue] == (bits == 0x80000000u),
            "native interpolation equality treats signed zero as equal and identical NaNs as unequal");
    }
    EnemyDispatchFixture(); engine = [FixtureSkillEngine new]; trace = CaptureEnemyActions(engine, &state);
    // Unique first byte: static audit checks no previous guard covers it.
    Put<uint8_t>(Header + PHOrbEnemyDispatchSchedulerCacheRVA, uint8_t(guest[PHOrbEnemyDispatchSchedulerCacheRVA] ^ 1u));
    trace = [engine observeEnemyActions:&state];
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"dispatchAnalysis"][@"status"] isEqual:@"unavailable"] &&
        [trace[@"selectorAnalysis"][@"status"] isEqual:@"ready"] &&
        !trace[@"enemies"][0][@"selectorAnalysis"][@"candidates"][0][@"postSelectionCheckpoint"] &&
        [engine readAutomation:&state frame:Frame() reason:nil] && !writes,
        "dispatch-only code mismatch clears its checkpoints but preserves raw, selector and automation paths");
    EnemyDispatchFixture(); engine = [FixtureSkillEngine new]; rejectEnemyDispatchRead = true;
    trace = CaptureEnemyActions(engine);
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"dispatchAnalysis"][@"status"] isEqual:@"unavailable"] &&
        [trace[@"selectorAnalysis"][@"status"] isEqual:@"ready"], "optional read failure is not a failure of old action capture");
    for (unsigned mutation = 1; mutation <= 4; ++mutation) {
        EnemyDispatchFixture(); engine = [FixtureSkillEngine new]; enemyDispatchMutation = mutation;
        trace = CaptureEnemyActions(engine);
        if (mutation == 4) Check(![trace[@"status"] isEqual:@"ready"] && !trace[@"enemies"],
            "global active impl identity change discards all stale diagnostics");
        else Check([trace[@"status"] isEqual:@"ready"] && [trace[@"dispatchAnalysis"][@"status"] isEqual:@"changed"] &&
            !trace[@"enemies"][0][@"dispatchAnalysis"][@"current"] &&
            [trace[@"selectorAnalysis"][@"status"] isEqual:@"ready"],
            "cache, special marker or hook races drop only optional entry checkpoints");
    }
    EnemyDispatchFixture(); engine = [FixtureSkillEngine new];
    Protected(Impl + PHOrbEnemyDispatchRedirectOffset, 256, 0x6a875a8b); trace = CaptureEnemyActions(engine);
    Check([trace[@"status"] isEqual:@"ready"] && [trace[@"dispatchAnalysis"][@"status"] isEqual:@"unavailable"],
        "noncanonical protected byte is not accepted as row redirect slot zero");
    EnemyDispatchFixture(); engine = [FixtureSkillEngine new];
    Protected(enemy + 2736, 65536, 0x6a875a8b); trace = CaptureEnemyActions(engine);
    post = trace[@"enemies"][0][@"selectorAnalysis"][@"candidates"][0][@"postSelectionCheckpoint"];
    Check([trace[@"dispatchAnalysis"][@"status"] isEqual:@"ready"] && ![post[@"protectedCountKnown"] boolValue] &&
        [post[@"protectedCountAtCheckpoint"] isEqual:NSNull.null], "invalid protected count stays unknown without masking other evidence");
    for (auto kind : {PHOrbMultiplayerThree, PHOrbMultiplayerFour, PHOrbMultiplayerEight, PHOrbMultiplayerThreeSolo}) {
        EnemyDispatchFixture(); engine = [FixtureSkillEngine new]; CaptureEnemyActions(engine, &state);
        state.multiplayer.kind = kind; trace = [engine observeEnemyActions:&state];
        Check([trace[@"status"] isEqual:@"multiplayer"] && !trace[@"dispatchAnalysis"] && !writes && !canUseCalls,
            "all multiplayer and three-player solo continuation exclude optional dispatch diagnostics");
    }
}
