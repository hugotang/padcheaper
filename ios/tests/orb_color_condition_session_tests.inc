static void ColorConditionSessionTests() {
    for (unsigned conditions = 0; conditions < 4; ++conditions) {
        FakeOrbEngine *engine = [FakeOrbEngine new];
        engine->live.automation.playerEffects = conditions & 1 ? PHOrbPlayerAwokenBind : 0;
        engine->live.board.unmatchableColors = conditions & 2 ? 8 : 0;
        NSMutableDictionary *bind = [PHOrbDefaultSkillRule() mutableCopy]; bind[@"status"] = @"awoken_bind";
        NSMutableDictionary *color = [PHOrbDefaultSkillRule() mutableCopy];
        color[@"obstacle"] = @"unmatchable"; color[@"unmatchableColors"] = @8;
        PHOrbSession *session = AutomaticSession(engine,@[bind,color]);
        Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.size() == (conditions ? 1u : 0u),
              "two independent rules implement awoken-bind OR light-unmatchable without casting one position twice");
        [session queueStop:@"color condition fixture complete"];
    }
    FakeOrbEngine *engine = [FakeOrbEngine new]; engine->live.board.unmatchableColors = 8;
    NSMutableDictionary *color = [PHOrbDefaultSkillRule() mutableCopy]; color[@"obstacle"] = @"unmatchable";
    color[@"unmatchableColors"] = @8;
    PHOrbSession *session = AutomaticSession(engine,@[color]);
    Check(UntilPhase(session,engine,@"skillArming"),"color-specific skill arms from matching live mask");
    engine->live.board.unmatchableColors = 32;
    for (unsigned i = 0; i < 20; ++i) Tick(session,engine,Frame());
    Check(engine->skillOrder.empty(),"a changed unmatchable color is rechecked before the native skill call");
    [session queueStop:@"color changed fixture complete"];
}
