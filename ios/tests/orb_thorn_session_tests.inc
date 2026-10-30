static void SetThornRoute(FakeOrbEngine *engine) {
    auto &b = engine->live.board; unsigned n = b.width * b.height;
    for (unsigned i = 0; i < n; ++i) {
        b.types[i] = uint8_t(1 + (i % b.width + 2 * (i / b.width)) % 4);
        b.flags[i] = i < n - 2 ? PHOrbFlagTape : 0;
    }
    for (unsigned i = 0; i < 4; ++i) b.types[i] = 5;
    b.tapeMask = (UINT64_C(1) << (n-2)) - 1; b.fixedStart = uint8_t(n-1);
    b.flags[n-1] = PHOrbFlagThorn; b.thornDamage[n-1] = 4;
    engine->live.thorns = {1000,1000,0,true};
}
static void ThornSessionTests() {
    for (bool priority : {false, true}) for (unsigned scenario = 0; scenario < 7; ++scenario) {
        FakeOrbEngine *engine = [FakeOrbEngine new]; SetThornRoute(engine);
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[],
            @"thornComboPriority":@(priority)}] && [session configureQuality:@"fast"],
            "changing quality preserves the selected thorn preference");
        Check(Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue] &&
              [session.snapshot[@"thornContacts"] isEqual:@1] && [session.snapshot[@"heartFours"] isEqual:@1],
              "thorn route has a verified one-contact preview with first-wave recovery hearts");
        Check([session.snapshot[@"thornComboPriority"] boolValue] == priority &&
            [session.snapshot[@"searchTrace"][@"thornComboPriority"] boolValue] == priority,
            "each pending route records the actual thorn search preference");
        if (scenario == 1) { engine->live.thorns.hp = 40; Tick(session,engine,Frame()); }
        if (scenario == 2) { engine->live.thorns.pendingDamage = 960; Tick(session,engine,Frame()); }
        if (scenario == 3) { engine->live.thorns.known = false; Tick(session,engine,Frame()); }
        Execute(session);
        if (scenario == 4) engine->live.thorns.hp = 40;
        if (scenario == 5) engine->live.thorns.maximumHP = 30000;
        if (scenario == 6) { UntilDragging(session,engine); engine->live.thorns.hp = 40; }
        for (unsigned i = 0; i < 80 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
        if (!scenario) Check([session.snapshot[@"phase"] isEqual:@"complete"] && engine->live.moves == 1 &&
              engine->live.thorns.pendingDamage == 40 && engine->releases == 1,
              "legitimate pending thorn damage does not invalidate acknowledged input or cause a duplicate contact");
        else Check(!engine->live.moves && ![session.snapshot[@"completed"] unsignedIntValue] &&
              (!engine->stages || (scenario == 6 && engine->releases == 1)),
              "changed HP, cap, pending damage or unknown budget cancels before damaging input and releases owned orbs");
    }
}
