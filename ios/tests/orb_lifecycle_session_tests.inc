static void LifecycleSessionTests() {
    FakeOrbEngine *engine = [FakeOrbEngine new]; PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    [session queueStop:@"轉轉資料不符"]; NSDictionary *before = session.snapshot;
    [session stopForLifecycle:@"從背景返回"]; [session stopForLifecycle:@"離開前景"];
    Check([session.snapshot[@"message"] isEqual:before[@"message"]] &&
          [session.snapshot[@"cancelled"] isEqual:before[@"cancelled"]],
          "background callbacks preserve an earlier stopped reason without inventing cancellations");
    Check(Preview(session,engine),"lifecycle preview has active intent");
    [session stopForLifecycle:@"離開前景"];
    Check([session.snapshot[@"phase"] isEqual:@"stopped"] && [session.snapshot[@"message"] isEqual:@"離開前景"],
          "background cancels a still-active preview");
    Check(Preview(session,engine),"lifecycle owned-release setup"); Execute(session); UntilDragging(session,engine);
    [session queueStop:@"荊棘 HP 已改變"]; [session stopForLifecycle:@"從背景返回"];
    for (unsigned i = 0; i < 20 && [session.snapshot[@"busy"] boolValue]; ++i) Tick(session,engine,Frame());
    Check(!engine->live.dragged && engine->releases == 1 && [session.snapshot[@"message"] isEqual:@"荊棘 HP 已改變"],
          "aborted owned gestures finish release while retaining the original error across lifecycle callbacks");
    engine = [FakeOrbEngine new]; session = [[PHOrbSession alloc] initWithEngine:engine];
    Check(Preview(session,engine),"input fault lifecycle fixture"); Execute(session); engine->failRestore = YES;
    for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
    before = session.snapshot; [session stopForLifecycle:@"從背景返回"];
    Check([before[@"inputFault"] boolValue] && [session.snapshot[@"inputFault"] boolValue] &&
          [session.snapshot[@"message"] isEqual:before[@"message"]],
          "background never hides the original private-input restoration fault");
}
