// Source cases only until the next authorized build/test. Synthetic peers and
// clocks check local admission/withdrawal, not multiplayer synchronization.
static void FishingIntent(PHOrbSession *session, FakeOrbEngine *engine, bool automatic = false) {
    Tick(session,engine,Frame());
    if (automatic) [session queueAutomatic]; else [session queueOnce];
    for (unsigned i = 0; i < 2000 && [session.snapshot[@"busy"] boolValue] &&
        ![session.snapshot[@"phase"] isEqual:@"arming"] && !engine->live.dragged; ++i)
        Tick(session,engine,Frame(),1.0 / 60);
}
static void ThreeFishingSessionTests() {
    // The same public multi-take request works for solo once, solo continuous,
    // an expressly armed local 3P turn and all native continuation seats.
    // Continuation cases have four spinners and no usable peer clock.
    for (unsigned mode : {0u,1u,3u,5u,6u,7u}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(true);
        if (mode == 3) SetMultiplayer(engine,3,1);
        if (mode >= 5) {
            SetSessionContinuation(engine,mode - 5);
            engine->live.board.spinnerMask |= UINT64_C(1) << 28;
        }
        PHOrbSession *session = MultiFishingSession(engine);
        FishingIntent(session,engine,mode == 1);
        NSDictionary *trace = session.snapshot[@"searchTrace"];
        Check([trace[@"fishingEligibilityReason"] isEqual:@"eligible"] &&
            [trace[@"multiFishingSelected"] boolValue] && [trace[@"fishingExchanges"] count] == 2,
            "solo once/continuous, armed 3P and four-spinner continuation once plan two heart takes");
        Check(![trace[@"fishingNotAttemptedMessage"] length],"successful fishing has no unattempted restriction message");
        if (mode >= 3) Check(![session.snapshot[@"automatic"] boolValue] &&
            [trace[@"soloRetrySkipped"] isEqual:@"not-single-player"] &&
            [trace[@"multiplayerSearchBudgetShared"] boolValue] && [trace[@"fishingReservedBudgetMS"] unsignedIntValue] > 0 &&
            [trace[@"ordinaryStageBudgetMS"] unsignedIntValue] < [trace[@"initialBudgetMS"] unsignedIntValue],
            "3P and continuation fishing keep manual once and one shared search budget without a solo retry");
        for (unsigned i = 0; i < 1000 && !engine->releases && [session.snapshot[@"busy"] boolValue]; ++i)
            Tick(session,engine,Frame(),1.0 / 60);
        Check(engine->releases == 1 && !engine->live.dragged &&
            [session.snapshot[@"lastFishingSuccess"][@"releaseConfirmed"] boolValue],
            "each allowed intent confirms both takes and one owned release");
        const NSUInteger stages = engine->stages;
        if (mode != 1) {
            for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
            Check(engine->stages == stages && !engine->multiplayerArmed,
                "one-shot fishing cannot re-arm itself after success");
        }
        [session queueStop:@"fishing intent fixture complete"]; Tick(session,engine,Frame());
    }
    // MultiFishingTiming must reserve against the shorter co-op clock even
    // when move time would otherwise allow several full spinner cycles.
    for (unsigned clock = 0; clock < 3; ++clock) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        if (clock == 2) SetSessionContinuation(engine,1);
        if (clock) engine->live.remainingFrames = 48;
        else engine->live.multiplayer.remainingSeconds = 0.8;
        PHOrbSession *session = MultiFishingSession(engine);
        FishingIntent(session,engine);
        Check(!engine->stages && !engine->releases,
            "short online clocks or continuation move time prevent beginning a multi-take gesture");
        [session queueStop:@"short clock fixture complete"]; Tick(session,engine,Frame());
    }
    for (unsigned players : {4u,8u}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,players);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        Check([session.snapshot[@"searchTrace"][@"fishingEligibilityReason"] isEqual:@"mode-not-supported"] &&
            ![session.snapshot[@"searchTrace"][@"multiFishingSelected"] boolValue] && !engine->stages,
            "4P/8P manual once retains an explicit no-fishing reason");
        [session queueStop:@"excluded mode fixture complete"]; Tick(session,engine,Frame());
    }
    for (bool continuation : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        if (continuation) SetSessionContinuation(engine,1);
        PHOrbSession *session = MultiFishingSession(engine);
        Preview(session,engine);
        NSString *reason = continuation ? @"continuation-manual-once-required" : @"three-player-manual-once-required";
        Check([session.snapshot[@"searchTrace"][@"fishingEligibilityReason"] isEqual:reason] &&
            !engine->stages && !engine->multiplayerArmed,
            "3P and continuation read/preview alone do not authorize fishing");
        [session queueAutomatic]; Tick(session,engine,Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages,
            "3P and continuation Auto remain excluded after the fishing extension");
        [session queueStop:@"unarmed fixture complete"]; Tick(session,engine,Frame());
    }
    // Clock reduction while held immediately withdraws further takes but
    // allows releasing exactly our held token while the local window remains.
    for (unsigned clock = 0; clock < 3; ++clock) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        if (clock == 2) SetSessionContinuation(engine,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        if (!MultiFishingSessionWait(session,engine)) continue;
        const auto moves = engine->live.moves; const uintptr_t held = engine->live.dragged;
        if (clock) engine->live.remainingFrames = 18;
        else engine->live.multiplayer.remainingSeconds = 0.3;
        Tick(session,engine,Frame(),1.0 / 60);
        Check(held && engine->live.moves == moves && engine->releases == 1 && !engine->live.dragged &&
            !engine->multiplayerArmed && !engine->allowedSpinnerCell,
            "shortening online clocks or continuation move time during a wait cancels the take and releases only the owned token");
    }
    for (bool continuation : {false,true}) for (unsigned change = 0; change < (continuation ? 13u : 5u); ++change) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        if (continuation) SetSessionContinuation(engine,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        if (!MultiFishingSessionWait(session,engine)) continue;
        const NSUInteger stages = engine->stages;
        if (change == 0) engine->live.multiplayer.turnSerial += 3;
        if (change == 1) engine->live.multiplayer.viewTeam = 2;
        if (change == 2) engine->live.identity.entryHash[0] ^= 1;
        if (change == 3) engine->live.inputManager++;
        if (change == 4) engine->live.dragged = engine->live.board.tokens[29];
        if (change == 5) engine->live.multiplayer.recoverySource = 1;
        if (change == 6) engine->live.multiplayer.continuationOwnerFlags = 3;
        if (change == 7) engine->live.multiplayer.continuationOwnerKnown = false;
        if (change == 8) engine->live.multiplayer.continuationDialog = true;
        if (change == 9) engine->live.multiplayer.recoveryFlags = 0x40;
        if (change == 10) engine->live.phase = 22;
        if (change == 11) SetMultiplayer(engine,3,1);
        if (change == 12) engine->live.multiplayer.resolvePending = 1;
        Tick(session,engine,Frame(),1.0 / 60);
        Check(engine->stages == stages && !engine->releases && !engine->multiplayerArmed,
            "identity, local continuation source/window, rejoin or foreign held token receives no stale swap or release");
    }
    for (unsigned change = 0; change < 4; ++change) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(true); SetSessionContinuation(engine,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        Check([session.snapshot[@"phase"] isEqual:@"arming"] && !engine->stages,
            "continuation pre-touch withdrawal starts from a publicly planned route");
        if (change == 0) engine->live.multiplayer.recoverySource = 1;
        if (change == 1) engine->live.multiplayer.continuationOwnerFlags = 3;
        if (change == 2) engine->live.remainingFrames = 18;
        if (change == 3) engine->live.phase = 22;
        for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame(),1.0 / 60);
        Check(!engine->stages && !engine->releases && !engine->multiplayerArmed,
            "new continuation source, short move clock or skill phase cancels before first touch");
    }
    for (bool locked : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(true); SetSessionContinuation(engine,1);
        engine->live.board.spinnerMask |= UINT64_C(1) << 28;
        engine->live.board.flags[29] |= locked ? PHOrbFlagLocked : PHOrbFlagThorn;
        if (!locked) engine->live.board.thornDamage[29] = 1;
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        NSDictionary *trace = session.snapshot[@"searchTrace"];
        Check([trace[@"fishingEligibilityReason"] isEqual:@"eligible"] &&
            [trace[@"singleFishingEligibilityReason"] isEqual:@"requires-one-unlocked-spinner"] &&
            [trace[@"multiFishingEligibilityReason"] isEqual:@"locked-or-thorn-token"] &&
            [trace[@"fishingNotAttemptedReason"] isEqual:@"locked-or-thorn-token"] &&
            [trace[@"fishingNotAttemptedMessage"] containsString:(locked ? @"鎖珠" : @"荊棘")] &&
            [session.snapshot[@"message"] containsString:@"本次未嘗試取色"] &&
            ![trace[@"multiFishingAttempts"] count] && !engine->stages && !engine->releases,
            "four-spinner continuation retains locked/thorn exclusions and explains why neither take path was attempted");
        if (!locked) Check([trace[@"thornSearchRestrictionMessage"] containsString:@"零荊棘接觸"] &&
            [session.snapshot[@"message"] containsString:@"起手持有荊棘珠仍可"],
            "unknown continuation HP explains zero-contact ordinary search without forbidding a thorn start");
    }
    for (bool continuation : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); SetMultiplayer(engine,3,1);
        if (continuation) SetSessionContinuation(engine,1);
        PHOrbSession *session = MultiFishingSession(engine); FishingIntent(session,engine);
        const NSUInteger count = [session.snapshot[@"path"] count];
        for (unsigned i = 0; i < 1000 && [session.snapshot[@"busy"] boolValue] && engine->live.moves + 1 < count; ++i)
            Tick(session,engine,Frame(),1.0 / 60);
        if (count > 1 && engine->live.dragged && engine->live.moves + 1 == count) {
            for (unsigned i = 0; i < 30; ++i) if (engine->live.board.tokens[i] != engine->live.dragged) engine->live.orbMotion[i] = 0;
            if (continuation) engine->live.remainingFrames = 30;
            else engine->live.multiplayer.remainingSeconds = 0.5;
            for (unsigned i = 0; i < 10 && !engine->releases; ++i) Tick(session,engine,Frame(),1.0 / 60);
            Check([session.snapshot[@"lastFishingSuccess"][@"releaseConfirmed"] boolValue] && engine->releases == 1,
                "already stable final board uses the release margin without charging another settling wait");
        } else Check(false,"stable final boundary fixture reaches the last confirmed move");
        [session queueStop:@"stable final boundary complete"]; Tick(session,engine,Frame());
    }
    // A single take uses the same motion/timing evidence as multi-take. The
    // target color alone is insufficient while its actual source is moving.
    {
        FakeOrbEngine *engine = FishEngine(); engine->live.orbMotionKnown = false;
        PHOrbSession *session = FishSession(engine); Preview(session,engine);
        Check([session.snapshot[@"searchTrace"][@"fishingEligibilityReason"] isEqual:@"motion-unavailable"] && !engine->stages,
            "unknown source motion explains why even a single fishing take was not attempted");
        [session queueStop:@"missing motion fixture complete"]; Tick(session,engine,Frame());
    }
    for (bool changePeriod : {false,true}) {
        FakeOrbEngine *engine = FishEngine(); PHOrbSession *session = FishSession(engine);
        if (!Preview(session,engine)) continue;
        NSArray<NSNumber *> *path = session.snapshot[@"path"];
        const unsigned fish = [session.snapshot[@"searchTrace"][@"fishCell"] unsignedIntValue];
        const NSUInteger entry = [path indexOfObject:@(fish)];
        Execute(session); UntilDragging(session,engine);
        if (entry == NSNotFound || !entry) { [session queueStop:@"missing single entry"]; continue; }
        for (unsigned i = 0; i < 300 && engine->live.moves + 1 < entry; ++i) Tick(session,engine,Frame(),1.0 / 60);
        engine->live.board.types[fish] = 0; engine->live.orbMotion[fish] = 3;
        const auto moves = engine->live.moves;
        if (changePeriod) ++engine->live.spinnerPeriodFrames;
        for (unsigned i = 0; i < 8 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
        if (changePeriod) Check(engine->releases == 1 && !engine->live.dragged,
            "single-take period change stops and releases the captured token");
        else Check(engine->live.moves == moves && engine->live.board.tokens[fish] != engine->live.dragged && !engine->releases,
            "single take never enters a moving source even when the needed color is visible");
        [session queueStop:@"single fishing motion fixture complete"]; Tick(session,engine,Frame());
    }
}
