// Written Session regressions; no route or private Session state is injected.
// This fixture has two hearts at 9/10 and only 7/8/14/15 can be entered. The
// hand witness 7 -> 14 -> 8 -> 14 -> 15 takes heart twice and leaves wood twice,
// creating the four-heart row 7/8/9/10 without relying on spinner 14's last color.
// Search may choose another valid route through the same small graph.
static FakeOrbEngine *MultiFishingSessionEngine(bool several = false, uint16_t colors = 36) {
    FakeOrbEngine *engine = [FakeOrbEngine new];
    auto &state = engine->live; auto &board = state.board;
    const uint64_t reachable = (UINT64_C(1) << 7) | (UINT64_C(1) << 8) |
        (UINT64_C(1) << 14) | (UINT64_C(1) << 15);
    board.tapeMask = ((UINT64_C(1) << 30) - 1) & ~reachable;
    board.fixedStart = 8; board.minErase = 3; board.spinnerColors = colors;
    board.spinnerMask = UINT64_C(1) << 14;
    if (several) board.spinnerMask |= 1 | (UINT64_C(1) << 29);
    for (unsigned i = 0; i < 30; ++i) {
        board.types[i] = i == 9 || i == 10 ? 5 : 2;
        board.flags[i] = board.tapeMask & (UINT64_C(1) << i) ? PHOrbFlagTape : 0;
    }
    state.spinnerTurns = 4; state.spinnerPeriodFrames = state.spinnerCountdown = 10;
    state.spinnerCountdownKnown = state.orbMotionKnown = true;
    state.remainingFrames = 1200; state.fps = 60;
    engine->nativeSpinnerTiming = YES;
    // Explicit synthetic motion duration, not a claim about native timing.
    // It crosses a 10-frame spinner tick and fits the planner's 0.20 s margin.
    engine->nativeSettleFrames = 12;
    return engine;
}
static PHOrbSession *MultiFishingSession(FakeOrbEngine *engine) {
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"diagonal":@YES,@"rules":@[],
        @"goals":@[@{@"kind":@"four",@"colors":@32,@"count":@1}]}],
        "two-heart multi-take Session fixture accepts its unchanged heart-four goal");
    return session;
}
static bool MultiFishingSessionPreview(PHOrbSession *session, FakeOrbEngine *engine) {
    const bool ready = Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue];
    Check(ready,"two-heart small graph produces an executable multi-take preview through normal public requests");
    if (!ready) { [session queueStop:@"multi-take fixture failed to preview"]; Tick(session,engine,Frame()); return false; }
    NSDictionary *state = session.snapshot, *trace = state[@"searchTrace"];
    CheckSearchAttemptTrace(trace);
    Check([trace[@"evaluationBreakdownKnown"] boolValue] && [trace[@"evaluationCalls"] unsignedLongLongValue] > 0 &&
        [trace[@"evaluated"] unsignedLongLongValue] == [trace[@"evaluationCalls"] unsignedLongLongValue] +
            [trace[@"evaluationCacheHits"] unsignedLongLongValue],
        "selected multi-take preview preserves cache counts in its public search trace");
    NSArray<NSDictionary *> *changes = trace[@"fishingExchanges"];
    Check([trace[@"multiFishingSelected"] boolValue] && [trace[@"fishCell"] isEqual:@14] && changes.count == 2 &&
        [state[@"path"][0] isEqual:@7] && !engine->stages && !engine->releases,
        "the required layout selects two exchanges and the fixed native start without executing the preview");
    for (NSDictionary *change in changes)
        Check([change[@"take"] isEqual:@5] && [change[@"leave"] isEqual:@2] &&
            [change[@"exit"] unsignedIntegerValue] == [change[@"entry"] unsignedIntegerValue] + 1,
            "each planned heart is funded by a wood donor on the next exit");
    return changes.count == 2 && [trace[@"multiFishingSelected"] boolValue];
}
static bool MultiFishingSessionWait(PHOrbSession *session, FakeOrbEngine *engine) {
    for (unsigned i = 0; i < 600; ++i) {
        Tick(session,engine,Frame(),1.0 / 60);
        if (engine->live.dragged && [session.snapshot[@"message"] containsString:@"等待轉轉"] &&
            [session.snapshot[@"phase"] isEqual:@"running"]) return true;
        if (![session.snapshot[@"busy"] boolValue]) break;
    }
    Check(false,"multi-take fixture reaches a live held wait before another exchange");
    [session queueStop:@"multi-take wait fixture ended"]; Tick(session,engine,Frame());
    return false;
}
static void MultiFishingSessionTests() {
    // The same two takes work with one source or three spinner cells. The
    // unrelated cells remain blocked and retain their shared phase; the used
    // source skips a tick while displaced, so its phase becomes independent.
    for (bool several : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(several);
        PHOrbSession *session = MultiFishingSession(engine);
        if (!MultiFishingSessionPreview(session,engine)) continue;
        NSArray<NSNumber *> *path = session.snapshot[@"path"];
        const uintptr_t held = engine->live.board.tokens[7];
        Execute(session);
        unsigned entries = 0, exits = 0; bool diverged = false;
        for (unsigned frame = 0; frame < 1000 && [session.snapshot[@"busy"] boolValue]; ++frame) {
            const PHOrbSnapshot before = engine->live;
            Tick(session,engine,Frame(),1.0 / 60);
            const auto &after = engine->live;
            if (after.dragged) Check(after.dragged == held,"multi-take route retains the original held token");
            if (several) {
                Check(after.board.types[0] == after.board.types[29],"untouched spinners keep their native shared phase");
                diverged |= after.board.types[14] != after.board.types[0];
            }
            if (after.moves == before.moves) continue;
            Check(after.moves == before.moves + 1 && after.moves < path.count,
                "one native acknowledgement cannot skip any planned swap");
            if (after.moves >= path.count) break;
            const unsigned from = path[after.moves - 1].unsignedIntValue, to = path[after.moves].unsignedIntValue;
            Check(after.board.tokens[from] == before.board.tokens[to] && after.board.tokens[to] == before.board.tokens[from],
                "every acknowledged step exchanges the two actual token identities");
            if (to == 14) {
                ++entries;
                Check(before.orbMotion[14] == 0 && before.board.types[14] == 5 && before.spinnerCountdown >= 6 &&
                    after.board.types[from] == 5 && after.orbMotion[14] == 4,
                    "each entry waits for a settled heart with enough countdown and extracts that exact heart");
            }
            if (from == 14) {
                ++exits;
                Check(before.board.types[to] == 2 && after.board.types[14] == 2 && after.orbMotion[14] == 3,
                    "each exit deposits and confirms its actual wood donor before that token can turn again");
            }
        }
        NSDictionary *state = session.snapshot;
        NSDictionary *record = [state[@"executionHistory"] lastObject];
        NSArray<NSDictionary *> *receipts = record[@"fishEntries"];
        Check([state[@"phase"] isEqual:@"complete"] && entries == 2 && exits == 2 &&
            engine->live.moves == path.count - 1 && [state[@"confirmed"] unsignedIntegerValue] == path.count - 1 &&
            engine->releases == 1 && !engine->live.dragged && !engine->allowedSpinnerCell,
            "two live takes finish every step and confirm one release while revoking spinner entry");
        Check(receipts.count == 2 && [record[@"releaseConfirmed"] boolValue] &&
            [record[@"releaseEvaluation"][@"goalsMet"] boolValue] &&
            [record[@"evaluationScope"] isEqual:@"target-independent-other-shared-phases"],
            "execution diagnostics retain both exchanges and the final successful release");
        for (NSDictionary *receipt in receipts)
            Check([receipt[@"confirmed"] boolValue] && [receipt[@"exitConfirmed"] boolValue],
                "each exchange has separate native take and donor-exit confirmation");
        if (several) Check(diverged && engine->nativeSpinnerMovingTicks,
            "multi-spinner success crosses a real fixture tick skipped by the moving source token");
        const NSUInteger stages = engine->stages;
        for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
        Check(engine->stages == stages,"successful manual multi-take does not silently start another turn");
    }

    // Inventory already has four hearts, but two are trapped far from the
    // reachable component. The public fallback must select geometry takes.
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine();
        engine->live.board.types[0] = engine->live.board.types[29] = 5;
        PHOrbSession *session = MultiFishingSession(engine);
        Check(std::count(engine->live.board.types,engine->live.board.types + 30,uint8_t(5)) == 4,
            "geometry Session fixture has sufficient fixed hearts before searching");
        if (MultiFishingSessionPreview(session,engine)) {
            NSDictionary *trace = session.snapshot[@"searchTrace"];
            Check([trace[@"geometryExtraExchanges"] isEqual:@2],
                "public Session planning identifies two geometry exchanges beyond the zero-shortage baseline");
            Execute(session);
            for (unsigned i = 0; i < 1000 && [session.snapshot[@"busy"] boolValue]; ++i)
                Tick(session,engine,Frame(),1.0 / 60);
            NSDictionary *state = session.snapshot, *record = [state[@"executionHistory"] lastObject];
            NSArray<NSDictionary *> *receipts = record[@"fishEntries"];
            Check([state[@"phase"] isEqual:@"complete"] && receipts.count == 2 &&
                [record[@"geometryExtraExchanges"] isEqual:@2] && [record[@"releaseConfirmed"] boolValue] &&
                [record[@"releaseEvaluation"][@"goalsMet"] boolValue] && engine->releases == 1 &&
                !engine->live.dragged && !engine->allowedSpinnerCell,
                "geometry-only fishing executes both confirmed takes and releases after meeting the original heart-four goal");
            for (NSDictionary *receipt in receipts)
                Check([receipt[@"confirmed"] boolValue] && [receipt[@"exitConfirmed"] boolValue],
                    "geometry provenance cannot substitute for native take and donor confirmation");
            Check([state[@"lastFishingSuccess"] isEqual:record],
                "the retained fishing success includes the geometry route and release evidence");
            [session queueStop:@"geometry fishing fixture completed"];
            Check([session configureQuality:@"thorough"] && [session.snapshot[@"phase"] isEqual:@"idle"] &&
                [session.snapshot[@"lastFishingSuccess"] isEqual:record],
                "Stop and a routine settings update preserve the completed geometry fishing evidence");
        }
    }

    // Keep a planned exit unacknowledged briefly so a native spinner tick
    // occurs while the original held token is still on the source cell.
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(true); PHOrbSession *session = MultiFishingSession(engine);
        if (MultiFishingSessionPreview(session,engine)) {
            Execute(session);
            for (unsigned i = 0; i < 600 && [session.snapshot[@"busy"] boolValue] &&
                !(engine->live.dragged && engine->live.board.tokens[14] == engine->live.dragged); ++i)
                Tick(session,engine,Frame(),1.0 / 60);
            Check(engine->live.dragged && engine->live.board.tokens[14] == engine->live.dragged,
                "held-skip fixture reaches the first native spinner entry");
            const uint8_t heldColor = engine->live.board.types[14];
            const NSUInteger moves = engine->live.moves;
            engine->ignoreMove = YES;
            const unsigned waitFrames = engine->live.spinnerCountdown;
            for (unsigned i = 0; i < waitFrames && [session.snapshot[@"busy"] boolValue]; ++i)
                Tick(session,engine,Frame(),1.0 / 60);
            Check(engine->nativeSpinnerHeldTicks && engine->live.moves == moves &&
                engine->live.board.tokens[14] == engine->live.dragged && engine->live.board.types[14] == heldColor &&
                [session.snapshot[@"phase"] isEqual:@"running"],
                "an unacknowledged exit holds the source color across a shared tick without advancing confirmed steps");
            engine->ignoreMove = NO;
            for (unsigned i = 0; i < 1000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
            Check([session.snapshot[@"phase"] isEqual:@"complete"] && engine->releases == 1 && !engine->allowedSpinnerCell,
                "a promptly acknowledged delayed exit resumes and completes the remaining take");
        }
    }

    // A different small graph needs fire and heart in the same continuous
    // gesture: 2 -> 8 -> 14 -> 8 -> 9 completes 0/1/2 and 12/13/14. The two
    // targets are ordinary team goals, with no fabricated assist-void state.
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(false,37); auto &board = engine->live.board;
        const uint64_t reachable = (UINT64_C(1) << 2) | (UINT64_C(1) << 8) |
            (UINT64_C(1) << 9) | (UINT64_C(1) << 14);
        board.tapeMask = ((UINT64_C(1) << 30) - 1) & ~reachable;
        board.spinnerMask = UINT64_C(1) << 8; board.fixedStart = 3;
        for (unsigned i = 0; i < 30; ++i) {
            board.types[i] = i == 0 || i == 1 ? 0 : i == 12 || i == 13 ? 5 : 2;
            board.flags[i] = board.tapeMask & (UINT64_C(1) << i) ? PHOrbFlagTape : 0;
        }
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:@{@"quality":@"fast",@"mode":@"manual",@"diagonal":@YES,@"rules":@[],
            @"goals":@[@{@"kind":@"match",@"colors":@1,@"count":@1,@"length":@3},
                @{@"kind":@"match",@"colors":@32,@"count":@1,@"length":@3}]}],
            "mixed-color multi-take fixture accepts both independent team requirements");
        const bool ready = Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue];
        Check(ready && [session.snapshot[@"searchTrace"][@"multiFishingSelected"] boolValue],
            "one multi-take plan fills both missing fire and missing heart");
        if (ready) {
            NSArray<NSDictionary *> *changes = session.snapshot[@"searchTrace"][@"fishingExchanges"];
            unsigned fire = 0, heart = 0;
            for (NSDictionary *change in changes) {
                fire += [change[@"take"] unsignedIntValue] == 0;
                heart += [change[@"take"] unsignedIntValue] == 5;
                Check([change[@"leave"] isEqual:@2],"mixed-color takes use real wood donors");
            }
            Check(changes.count == 2 && fire == 1 && heart == 1,"the exchange list contains both required target colors");
            Execute(session);
            for (unsigned i = 0; i < 1000 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
            NSDictionary *record = [session.snapshot[@"executionHistory"] lastObject];
            NSArray<NSDictionary *> *receipts = record[@"fishEntries"];
            unsigned observedColors = 0;
            for (NSDictionary *receipt in receipts) {
                observedColors |= 1u << [receipt[@"color"] unsignedIntValue];
                Check([receipt[@"confirmed"] boolValue] && [receipt[@"exitConfirmed"] boolValue],
                    "each color in the mixed sequence is independently taken and paid for");
            }
            Check([session.snapshot[@"phase"] isEqual:@"complete"] && receipts.count == 2 && observedColors == 33 &&
                [record[@"releaseEvaluation"][@"goalsMet"] boolValue] && [record[@"releaseConfirmed"] boolValue] &&
                engine->releases == 1 && !engine->allowedSpinnerCell,
                "native-like execution observes both target colors and releases only after both goals are met");
        } else { [session queueStop:@"mixed-color fixture did not preview"]; Tick(session,engine,Frame()); }
    }

    // The configured table, not a desired color list, bounds the search.
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(false,5); // Fire/wood, no heart.
        PHOrbSession *session = MultiFishingSession(engine);
        Tick(session,engine,Frame()); [session queueRead];
        for (unsigned i = 0; i < 2000 && ![session.snapshot[@"phase"] isEqual:@"stopped"] &&
            ![session.snapshot[@"phase"] isEqual:@"fault"] && ![session.snapshot[@"phase"] isEqual:@"blocked"]; ++i)
            Tick(session,engine,Frame());
        Check([session.snapshot[@"phase"] isEqual:@"stopped"] && ![session.snapshot[@"canExecute"] boolValue] &&
            !engine->stages && !engine->releases && !engine->allowedSpinnerCell,
            "a restricted spinner with no heart cannot publish or execute the two-heart request");
        for (NSDictionary *attempt in session.snapshot[@"searchTrace"][@"multiFishingAttempts"])
            Check(![attempt[@"plansSearched"] unsignedIntegerValue],"missing-color inventory does not masquerade as path search");
    }
    // Change the table before the first gesture, and again during an owned
    // wait. Both are valid board tables; the change itself must revoke input.
    for (bool duringWait : {false,true}) {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); PHOrbSession *session = MultiFishingSession(engine);
        if (!MultiFishingSessionPreview(session,engine)) continue;
        if (duringWait) { Execute(session); if (!MultiFishingSessionWait(session,engine)) continue; }
        const NSUInteger moves = engine->live.moves;
        engine->live.board.spinnerColors = 5; engine->live.board.types[14] = 2;
        if (!duringWait) Execute(session);
        for (unsigned i = 0; i < 40 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
        Check([session.snapshot[@"phase"] isEqual:@"stopped"] && engine->live.moves == moves &&
            !engine->live.dragged && !engine->allowedSpinnerCell && engine->releases == NSUInteger(duringWait),
            "a table change cancels before entry and releases an owned wait without making another swap");
        if (!duringWait) Check(!engine->stages,"a pre-gesture color table change submits no hold or release");
    }

    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); PHOrbSession *session = MultiFishingSession(engine);
        if (MultiFishingSessionPreview(session,engine)) {
            Execute(session);
            if (MultiFishingSessionWait(session,engine)) {
                const NSUInteger moves = engine->live.moves;
                // 0.50 s is still above the 0.40 s emergency margin, but is
                // insufficient for another take/exit, settle and release.
                engine->live.remainingFrames = 30;
                for (unsigned i = 0; i < 40 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
                Check([session.snapshot[@"phase"] isEqual:@"stopped"] && engine->live.moves == moves &&
                    engine->releases == 1 && !engine->live.dragged && !engine->allowedSpinnerCell &&
                    [session.snapshot[@"message"] containsString:@"後續取色"],
                    "shrinking native time rejects the remaining exchange suffix before the emergency release margin");
            }
        }
    }

    // A wrong donor still belongs to the legal wood/heart table. Board
    // validity therefore cannot substitute for checking the expected exit.
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(); PHOrbSession *session = MultiFishingSession(engine);
        if (MultiFishingSessionPreview(session,engine)) {
            engine->corruptNativeSpinnerExit = YES; Execute(session);
            for (unsigned i = 0; i < 600 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
            NSDictionary *record = [session.snapshot[@"executionHistory"] lastObject];
            NSArray<NSDictionary *> *receipts = record[@"fishEntries"];
            Check([session.snapshot[@"phase"] isEqual:@"stopped"] && !engine->live.dragged && engine->releases == 1 &&
                !engine->allowedSpinnerCell && ![session.snapshot[@"completed"] unsignedIntegerValue],
                "a legal-color wrong donor aborts the multi-take route and releases the held token");
            Check(receipts.count == 1 && [receipts.firstObject[@"confirmed"] boolValue] &&
                ![receipts.firstObject[@"exitConfirmed"] boolValue],
                "the corrupt exit cannot be recorded as a confirmed donor or permit a second take");
        }
    }
    {
        FakeOrbEngine *engine = MultiFishingSessionEngine(true); PHOrbSession *session = MultiFishingSession(engine);
        if (MultiFishingSessionPreview(session,engine)) {
            Execute(session);
            if (MultiFishingSessionWait(session,engine)) {
                const NSUInteger moves = engine->live.moves;
                [session queueStop:@"user stopped multi-take wait"];
                for (unsigned i = 0; i < 40 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame(),1.0 / 60);
                Check([session.snapshot[@"phase"] isEqual:@"stopped"] && engine->releases == 1 &&
                    engine->live.moves == moves && !engine->live.dragged && !engine->allowedSpinnerCell &&
                    ![session.snapshot[@"completed"] unsignedIntegerValue],
                    "Stop during a multi-spinner wait confirms release and never continues the exchange list");
                const NSUInteger stages = engine->stages;
                for (unsigned i = 0; i < 10; ++i) Tick(session,engine,Frame());
                Check(engine->stages == stages,"a stopped multi-take wait cannot re-arm itself");
            }
        }
    }
}
