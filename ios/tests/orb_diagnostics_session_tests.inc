static void DiagnosticAssemblySessionTests() {
    const NSSet *commonKeys = [NSSet setWithArray:@[@"reason", @"phase", @"valid", @"automationValid",
        @"width", @"height", @"resizeProgress", @"resizeTarget", @"openingStep", @"duringAutomatic",
        @"sessionPhase", @"capturedSearchGeneration"]];
    {
        FakeOrbEngine *engine = [FakeOrbEngine new]; engine->failRead = YES;
        engine->readFailureReason = @"fixture read failed";
        PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
        [session monitorReadiness]; Tick(session, engine, Frame());
        const NSUInteger reads = engine->reads;
        NSDictionary *snapshot = session.snapshot, *issue = snapshot[@"lastReadIssue"];
        NSMutableSet *expected = [commonKeys mutableCopy]; [expected addObject:@"multiplayer"];
        Check([[NSSet setWithArray:issue.allKeys] isEqual:expected] &&
            [issue[@"reason"] isEqual:@"fixture read failed"] && [issue[@"sessionPhase"] isEqual:@"idle"] &&
            ![issue[@"valid"] boolValue] && ![issue[@"automationValid"] boolValue] &&
            ![issue[@"duringAutomatic"] boolValue] && [issue[@"width"] isEqual:@0] &&
            [issue[@"capturedSearchGeneration"] isEqual:@0],
            "passive read failures preserve the existing keys, captured state and optional multiplayer detail");
        for (unsigned i = 0; i < 10; ++i) snapshot = session.snapshot;
        Check(engine->reads == reads && !engine->skillAttempts && !engine->stages &&
            [NSJSONSerialization isValidJSONObject:snapshot],
            "snapshot assembly adds no native reads or action intent and remains JSON serializable");
    }
    for (bool resizing : {false, true}) {
        FakeOrbEngine *engine = [FakeOrbEngine new];
        PHOrbSession *session = AutomaticSession(engine, @[]);
        if (resizing) {
            engine->live.boardTransition = true;
            engine->live.resizeProgress = 0.25f; engine->live.resizeTarget = 1;
        } else {
            engine->failRead = YES; engine->readFailureReason = @"Auto read failed";
        }
        // Capture before afterFrame can publish a later passive observation.
        now += 0.02; [session beforeFrame:Frame() time:now];
        const NSUInteger reads = engine->reads;
        NSDictionary *snapshot = session.snapshot, *issue = snapshot[@"lastReadIssue"];
        Check([[NSSet setWithArray:issue.allKeys] isEqual:commonKeys] &&
            [issue[@"duringAutomatic"] boolValue] && [issue[@"sessionPhase"] isEqual:@"autoWaiting"] &&
            [issue[@"valid"] boolValue] == resizing && [issue[@"automationValid"] boolValue] == resizing &&
            [issue[@"openingStep"] isEqual:@0] && [issue[@"capturedSearchGeneration"] isEqual:@0] &&
            [issue[@"reason"] isEqual:resizing ? @"等待原生改盤動畫完成" : @"Auto read failed"],
            "Auto failure and resize use their own captured phase and original no-multiplayer trace contract");
        Check(engine->reads == reads && !engine->skillAttempts && !engine->stages &&
            [NSJSONSerialization isValidJSONObject:snapshot], "diagnostic export cannot turn failed or resizing reads into actions");
        [session queueStop:@"diagnostic fixture finished"];
    }
}
