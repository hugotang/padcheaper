// This fake models only the independently documented charge reset / ordinary
// turn +1 behavior for the two CD2 Byakuya bodies. It does not simulate damage,
// orb creation, haste or the device's native readiness conditions.
@interface UryuStartupEngine : FakeOrbEngine
@end
@implementation UryuStartupEngine
- (instancetype)init {
    if ((self = [super init])) {
        const uint16_t numbers[] = {14038,14055,14060,14048,14065,14065};
        for (unsigned i = 0; i < 6; ++i) {
            live.automation.members[i].number = numbers[i];
            unavailableSkills[i] = i < 4;
        }
        for (unsigned slot = 4; slot < 6; ++slot) {
            chosenSource[slot] = PHOrbSkillBody; chosenSkill[slot] = 64914;
            live.automation.members[slot].charge = slot == 4 ? 10 : 0;
        }
    }
    return self;
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (slot >= 4 && slot < 6 && live.automation.members[slot].charge < 2) {
        if (reason) *reason = @"fixture CD2 is not charged";
        return PHOrbSkillUnavailable;
    }
    PHOrbSkillResult result = [super useSkill:slot expected:expected frame:frame reason:reason];
    if (slot >= 4 && slot < 6 && result == PHOrbSkillStarted) live.automation.members[slot].charge = 0;
    return result;
}
@end

static void SetUryuStartupBoard(UryuStartupEngine *engine, unsigned light, unsigned hearts) {
    for (unsigned i = 0; i < 30; ++i) engine->live.board.types[i] = i < light ? 3 : i < light + hearts ? 5 : i % 3;
}
static void AdvanceUryuCharge(UryuStartupEngine *engine) {
    for (unsigned slot = 4; slot < 6; ++slot) ++engine->live.automation.members[slot].charge;
    engine->live.automation.skillUses = 0;
}
static NSDictionary *UryuStartupProfile(NSString *name) {
    NSData *data = [NSData dataWithContentsOfFile:[@"ios/templates/ishida-uryu-gamewith-574233/" stringByAppendingString:name]];
    NSDictionary *plan = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    NSDictionary *profile = PHOrbProfileFromPlan(plan,NULL);
    Check(profile != nil, "Uryu startup fixture loads the full template with real rules");
    return profile;
}
static PHOrbSession *UryuStartupSession(UryuStartupEngine *engine, NSDictionary *profile) {
    NSMutableDictionary *settings = [profile[@"settings"] mutableCopy];
    // Keep the template's actual rule array while isolating scheduling from
    // shape-search success. Exact T/heart goals are covered by plan fixtures.
    settings[@"quality"] = @"fast"; settings[@"mode"] = @"combo"; settings[@"goals"] = @[];
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:settings], "Uryu startup settings accept all real rotation rows");
    Tick(session,engine,Frame()); [session queueAutomatic]; return session;
}
static void UryuStartupSessionTests() {
    for (NSString *name in @[@"team-plan.json",@"team-plan-heart-l.json"]) {
        NSDictionary *profile = UryuStartupProfile(name); if (!profile) continue;
        const unsigned hearts = [name isEqual:@"team-plan.json"] ? 4 : 5;
        UryuStartupEngine *engine = [UryuStartupEngine new]; SetUryuStartupBoard(engine,9,hearts);
        PHOrbSession *session = UryuStartupSession(engine,profile);
        Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder.empty() &&
            engine->live.automation.members[4].charge == 10 && engine->live.automation.members[5].charge == 0,
            "sufficient first-puzzle light/hearts preserve the charged slot five and freshly spent slot six");
        [session queueStop:@"pause after Uryu first puzzle"];
        AdvanceUryuCharge(engine); engine->live.phase = 2; engine->live.moves = 0;
        Tick(session,engine,Frame(),0.12); SetUryuStartupBoard(engine,0,0); [session queueAutomatic];
        Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder == std::vector<NSUInteger>{4} &&
            ObservedSkillTurn(session) == 2 && [session.snapshot[@"autoTurn"] unsignedIntegerValue] == 1,
            "second puzzle has slot five available after stop/restart even with no natural supply");
        AdvanceUryuCharge(engine);
        Check(NextOrbTurn(session,engine) && engine->skillOrder == (std::vector<NSUInteger>{4,5}),
            "slot six reaches CD2 on the third puzzle and continues the normal alternating cycle");
        AdvanceUryuCharge(engine);
        Check(NextOrbTurn(session,engine) && engine->skillOrder == (std::vector<NSUInteger>{4,5,4}),
            "slot five recharges in time for the fourth puzzle");
        [session queueStop:@"end Uryu reserved startup fixture"];

        for (bool heartShortage : {false,true}) {
            engine = [UryuStartupEngine new]; SetUryuStartupBoard(engine,heartShortage ? 9 : 8,heartShortage ? hearts - 1 : hearts);
            session = UryuStartupSession(engine,profile);
            Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder == std::vector<NSUInteger>{4},
                "first-puzzle light OR heart shortage may consume one Byakuya without falsely claiming a gap-free start");
            AdvanceUryuCharge(engine);
            Check(NextOrbTurn(session,engine) && engine->skillOrder.size() == 1 &&
                engine->live.automation.members[4].charge == 1 && engine->live.automation.members[5].charge == 1,
                "using slot five for an actual first-puzzle shortage leaves both CD2 members unavailable next puzzle");
            [session queueStop:@"end Uryu shortage startup fixture"];
        }

        engine = [UryuStartupEngine new]; SetUryuStartupBoard(engine,9,hearts);
        engine->live.automation.playerEffects = PHOrbPlayerAwokenBind;
        session = UryuStartupSession(engine,profile);
        for (unsigned i = 0; i < 150 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
        Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && engine->skillOrder.empty() &&
            [session.snapshot[@"message"] containsString:@"必要規則"],
            "reserving first-puzzle Byakuya cannot skip an unavailable required Unohana rescue");
    }
}
