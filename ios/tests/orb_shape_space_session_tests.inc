static void ShapeSpaceSessionTests() {
    FakeOrbEngine *engine = [[FakeOrbEngine alloc] initWithWidth:5 height:4];
    const uint8_t cells[] = {5,3,3,3,3, 5,3,3,3,3, 3,5,5,5,3, 3,5,3,5,5};
    std::copy(std::begin(cells),std::end(cells),engine->live.board.types);
    engine->live.board.spinnerMask = UINT64_C(1) << 19; engine->live.board.spinnerColors = 63;
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:@{@"quality":@"thorough", @"mode":@"manual", @"rules":@[],
        @"diagonal":@YES, @"goals":@[@{@"kind":@"t",@"colors":@8,@"count":@1,@"length":@0},
                                      @{@"kind":@"four",@"colors":@32,@"count":@1,@"length":@0}]}],
        "dense-board session uses explicit T and heart-four goals");
    Tick(session,engine,Frame()); [session queueRead];
    for (unsigned i = 0; i < 400 && ![session.snapshot[@"searchTrace"][@"status"] isEqual:@"incompatible-goals"]; ++i)
        Tick(session,engine,Frame());
    Check([session.snapshot[@"searchTrace"][@"status"] isEqual:@"incompatible-goals"] &&
        [session.snapshot[@"message"] containsString:@"珠色比例"] && ![session.snapshot[@"canExecute"] boolValue] && !engine->stages,
        "shape-space failure gives a specific explanation without publishing an executable preview");
    Check([session configureSettings:@{@"quality":@"fast", @"mode":@"combo", @"goals":@[], @"rules":@[]}],
        "changing the goal after a space failure remains available");
    Check(Preview(session,engine) && [session.snapshot[@"canExecute"] boolValue],
        "space failure does not latch unrelated Combo goals or reuse the failed shape proof");
    [session queueStop:@"end shape-space fixture"];
}
