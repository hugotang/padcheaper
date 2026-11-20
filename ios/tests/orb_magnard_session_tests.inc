// State-machine regressions only. 1100 is a synthetic skill ID; the template
// deliberately does not guess the real transformed Magnard ID or model damage.
@interface MagnardOpeningEngine : FakeOrbEngine {
@public
    unsigned bindingReads;
    uint32_t bindingFixture;
    BOOL bindingUnavailable, changeSkillAfterBinding;
}
@end
@implementation MagnardOpeningEngine
- (instancetype)init {
    if ((self = [super init])) bindingFixture = 1100;
    return self;
}
- (BOOL)readOpeningBodySkill:(NSUInteger)slot skill:(uint32_t *)skill
    expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    ++bindingReads; *skill = 0;
    if (bindingUnavailable || slot != 0 || !PHOrbSameAutomation(expected,&live) ||
        live.automation.members[slot].number != 14360 || live.automation.members[slot].temporaryTurns ||
        !frame.ready || !frame.enabled || !live.automation.window || live.automation.busy) return NO;
    *skill = bindingFixture;
    if (changeSkillAfterBinding) ++chosenSkill[slot];
    return YES;
}
@end

static NSDictionary *MagnardSessionProfile(void) {
    NSData *data = [NSData dataWithContentsOfFile:@"ios/templates/magnard-gamewith-589197/team-plan.json"];
    NSDictionary *plan = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    NSMutableDictionary *profile = [PHOrbProfileFromPlan(plan,NULL) mutableCopy];
    // Keep actual opening/rotation and native binding; isolate route difficulty.
    NSMutableDictionary *settings = [profile[@"settings"] mutableCopy];
    settings[@"quality"] = @"fast"; settings[@"mode"] = @"combo"; settings[@"goals"] = @[];
    profile[@"settings"] = settings;
    return profile;
}
static void PrepareMagnardOpening(PHOrbSession *session, FakeOrbEngine *engine, NSDictionary *profile, uint32_t boundSkill = 1100) {
    const NSUInteger index = [session.snapshot[@"opening"][@"completed"] unsignedIntegerValue];
    if (index >= [profile[@"opening"] count]) return;
    NSDictionary *step = profile[@"opening"][index]; const unsigned slot = [step[@"slot"] unsignedIntValue] - 1;
    engine->chosenSource[slot] = PHOrbSkillBody;
    engine->chosenSkill[slot] = [step[@"bindBodySkill"] boolValue] ? boundSkill : [step[@"skill"] unsignedIntValue];
    engine->transformAfterSkill[slot] = [step[@"afterMonster"] unsignedIntValue];
}
static void MagnardOpeningSessionTests() {
    NSDictionary *profile = MagnardSessionProfile();
    Check(profile != nil, "full Magnard profile is available to opening Session regressions");
    if (!profile) return;
    const std::vector<NSUInteger> expectedOrder{4,0,2,3,1,0,5};
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        FakeOrbEngine *engine = scenario == 7 ? [FakeOrbEngine new] : [MagnardOpeningEngine new];
        MagnardOpeningEngine *binding = scenario == 7 ? nil : (MagnardOpeningEngine *)engine;
        PHOrbSession *session = PlanSession(engine,profile);
        bool interrupted = false;
        for (unsigned i = 0; i < 1800 && [session.snapshot[@"automatic"] boolValue]; ++i) {
            const NSUInteger completed = [session.snapshot[@"opening"][@"completed"] unsignedIntegerValue];
            if (completed == 7) break;
            PrepareMagnardOpening(session,engine,profile);
            if (completed == 5) {
                if (scenario == 2) binding->bindingUnavailable = YES;
                if (scenario == 3) engine->chosenSource[0] = PHOrbSkillAssist;
                if (scenario == 4) binding->changeSkillAfterBinding = YES;
                if (scenario == 5) engine->unavailableSkills[0] = YES;
                if (scenario == 6) engine->failSkillAck = YES;
            }
            Tick(session,engine,Frame());
            if (scenario == 1 && !interrupted && engine->skillOrder.size() == 6 &&
                [session.snapshot[@"opening"][@"inFlight"] boolValue]) {
                interrupted = true; [session queueStop:@"stop while bound opening body skill runs"];
                for (unsigned j = 0; j < 40; ++j) Tick(session,engine,Frame());
                Check([session.snapshot[@"opening"][@"completed"] unsignedIntegerValue] == 6,
                    "a bound opening skill can confirm once after Stop without starting the next step");
                [session queueAutomatic];
            }
        }
        if (scenario <= 1) {
            Check(engine->skillOrder == expectedOrder && [session.snapshot[@"opening"][@"completed"] unsignedIntegerValue] == 7 &&
                binding->bindingReads == 1 && ObservedSkillTurn(session) == 1 && !engine->stages,
                "seven exact ordered opening steps finish without a puzzle, including stopped sixth-step completion");
            NSArray *records = session.snapshot[@"opening"][@"steps"];
            Check(records.count == 7 && [records[5][@"skill"] isEqual:@1100] &&
                [records[5][@"skillBinding"] isEqual:@"native-body"] && [records[5][@"status"] isEqual:@"confirmed"],
                "opening diagnostics record the actual bound ID and confirmed receipt");
            [session queueStop:@"stop after seven opening steps"]; [session queueAutomatic];
            // Isolate the required Magnard interval from optional native-ready loops.
            for (unsigned slot = 0; slot < 6; ++slot) {
                engine->chosenSource[slot] = PHOrbSkillBody; engine->chosenSkill[slot] = 1100 + slot;
                engine->transformAfterSkill[slot] = 0;
                engine->unavailableSkills[slot] = slot != 0 && slot != 5;
            }
            for (unsigned turn = 1; turn <= 5; ++turn) {
                const bool finished = turn == 1 ? UntilPhase(session,engine,@"turnWaiting") : NextOrbTurn(session,engine);
                std::vector<NSUInteger> order = expectedOrder;
                if (turn >= 3) order.push_back(0);
                if (turn >= 5) order.push_back(5);
                Check(finished && engine->skillOrder == order && ObservedSkillTurn(session) == turn,
                    "full opening hands off without replay; Magnards wait two puzzles then use leader on three and helper on five");
            }
            [session queueStop:@"end full Magnard opening fixture"];
            continue;
        }
        const size_t expectedUses = scenario == 7 ? 0 : scenario == 6 ? 6 : 5;
        const NSUInteger expectedCompleted = scenario == 7 ? 0 : 5;
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && engine->skillOrder.size() == expectedUses &&
            [session.snapshot[@"opening"][@"completed"] unsignedIntegerValue] == expectedCompleted,
            "missing binding, changed skill/source, unavailable native body or missing receipt stops before the final transform");
        if (scenario == 5) {
            engine->unavailableSkills[0] = NO; ++binding->bindingFixture;
            [session queueAutomatic];
            for (unsigned i = 0; i < 80 && [session.snapshot[@"automatic"] boolValue]; ++i) {
                PrepareMagnardOpening(session,engine,profile,binding->bindingFixture); Tick(session,engine,Frame());
            }
            Check(![session.snapshot[@"automatic"] boolValue] && engine->skillOrder.size() == 5 &&
                [session.snapshot[@"opening"][@"boundBodySkill"] isEqual:@1100],
                "a stopped unready step retains its original binding and rejects a different root on resume");
        }
        if (scenario == 6) {
            engine->failSkillAck = NO; [session queueAutomatic];
            for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
            Check(![session.snapshot[@"automatic"] boolValue] && engine->skillOrder.size() == 6 && !engine->stages,
                "an unconfirmed body skill is neither replayed nor inferred from the already-transformed team");
        }
    }
    MagnardOpeningEngine *manualEngine = [MagnardOpeningEngine new];
    PHOrbSession *manual = PlanSession(manualEngine,profile,^{
        for (NSDictionary *step in profile[@"opening"])
            if ([step[@"afterMonster"] unsignedIntValue])
                manualEngine->live.automation.members[[step[@"slot"] unsignedIntValue]-1].number = [step[@"afterMonster"] unsignedIntValue];
        manualEngine->live.automation.skillUses = 6; // All transforms, but omitted the non-transform sixth step.
    });
    for (unsigned i = 0; i < 40; ++i) Tick(manual,manualEngine,Frame());
    Check(![manual.snapshot[@"automatic"] boolValue] && manualEngine->skillOrder.empty() && !manualEngine->stages && !manualEngine->bindingReads,
        "a fully transformed manual team cannot skip the required seventh-action history of the full plan");
    for (unsigned scenario = 0; scenario < 2; ++scenario) {
        MagnardOpeningEngine *engine = [MagnardOpeningEngine new]; PHOrbSession *session = PlanSession(engine,profile);
        for (unsigned i = 0; i < 1000 && [session.snapshot[@"automatic"] boolValue] &&
            [session.snapshot[@"opening"][@"completed"] unsignedIntegerValue] < 5; ++i) {
            PrepareMagnardOpening(session,engine,profile); Tick(session,engine,Frame());
        }
        Check(engine->skillOrder.size() == 5, "unrecorded-action fixture reaches the non-transform opening step");
        [session queueStop:@"pause before step six"];
        if (scenario == 0) ++engine->live.automation.skillUses; // A manual body skill leaves all forms unchanged.
        else ObservedManualPuzzle(session,engine);
        [session queueAutomatic];
        for (unsigned i = 0; i < 40; ++i) Tick(session,engine,Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && engine->skillOrder.size() == 5 && !engine->bindingReads && !engine->stages &&
            [session.snapshot[@"message"] containsString:@"開場進度之外"],
            "unrecorded manual skills or puzzles during Stop cannot replay the pending body skill even when all forms still match");
    }
}
