// Written, unexecuted scheduling regressions for the GameWith 545672 plan.
// Only the synthetic cooldown thresholds, use reset, listed haste and ordinary
// turn charge below are modeled. They do not establish native charge caps,
// six-colour generation, skill effects, spinner handling, damage or survival.
@interface HimmelSessionEngine : FakeOrbEngine {
@public
    unsigned himmelBypassTurns;
}
@end
@implementation HimmelSessionEngine
- (instancetype)init {
    if ((self = [super init])) {
        const uint16_t numbers[] = {13413,14084,13919,13922,13922,13922};
        const uint32_t skills[] = {60253,64942,65361,65369,65369,65369};
        const unsigned charges[] = {8,11,13,2,2,0};
        for (unsigned slot = 0; slot < 6; ++slot) {
            live.automation.members[slot].number = numbers[slot];
            live.automation.members[slot].charge = charges[slot];
            chosenSource[slot] = PHOrbSkillBody; chosenSkill[slot] = skills[slot];
        }
    }
    return self;
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (slot >= 6) return PHOrbSkillRejected;
    unsigned cooldown = slot == 0 ? (live.automation.members[0].number == 13412 ? 26 : 10) :
        slot == 1 ? 12 : slot == 2 ? 5 : 3;
    if (chosenSource[slot] == PHOrbSkillAssist) {
        const unsigned assistCooldowns[] = {19,6,5,6,7,6};
        cooldown += assistCooldowns[slot];
    }
    if (live.automation.members[slot].charge < cooldown) {
        if (reason) *reason = @"Himmel fixture skill has not reached its cooldown";
        return PHOrbSkillUnavailable;
    }
    const PHOrbSkillResult result = [super useSkill:slot expected:expected frame:frame reason:reason];
    if (result == PHOrbSkillStarted) live.automation.members[slot].charge = 0;
    return result;
}
- (void)nativeFrame {
    const bool finishing = skillFrames == 1 && !hangSkill;
    const NSUInteger slot = activeSkill;
    const uint32_t skill = finishing ? chosenSkill[slot] : 0;
    [super nativeFrame];
    if (!finishing) return;
    unsigned haste = 0;
    switch (skill) {
        case 60147: chosenSkill[slot] = 65361; break;
        case 60172: chosenSkill[slot] = 64942; haste = 2; break;
        case 60246: chosenSkill[slot] = 60253; haste = 3; break;
        case 63375: case 61750: chosenSkill[slot] = 65369; haste = 3; break;
        case 60592: chosenSkill[slot] = 65369; haste = 2; break;
        case 64942: case 64943: case 64944: chosenSkill[slot] = skill + 1; break;
        case 60253:
            himmelBypassTurns = 3;
            live.automation.playerBypass |= PHOrbEnemyDamageAbsorb | PHOrbEnemyAttributeAbsorb | PHOrbEnemyDamageVoid;
            break;
    }
    for (unsigned other = 0; other < 6; ++other)
        if (other != slot) live.automation.members[other].charge += haste;
}
@end

static NSDictionary *HimmelSessionJSON(NSString *name) {
    NSString *path = [@"ios/templates/himmel-gamewith-545672/" stringByAppendingString:name];
    NSData *data = [NSData dataWithContentsOfFile:path];
    return data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
}
static NSDictionary *HimmelSessionProfile(bool postOpening = false) {
    NSDictionary *document = HimmelSessionJSON(postOpening ? @"post-opening.json" : @"team-plan.json");
    NSDictionary *profile = postOpening ? document[@"profile"] : PHOrbProfileFromPlan(document,NULL);
    Check(profile != nil,"Himmel Session fixture loads the actual complete or just-opened template");
    if (!profile) return nil;
    NSMutableDictionary *copy = [profile mutableCopy], *settings = [profile[@"settings"] mutableCopy];
    // Retain all nine real rules; scheduling is independent from shape search.
    settings[@"quality"] = @"fast"; settings[@"mode"] = @"combo"; settings[@"goals"] = @[];
    copy[@"settings"] = settings;
    return copy;
}
static PHOrbSession *HimmelAfterOpeningSession(HimmelSessionEngine *engine, NSDictionary *profile) {
    PHOrbSession *session = [[PHOrbSession alloc] initWithEngine:engine];
    Check([session configureSettings:profile[@"settings"]],"Himmel scheduling accepts the template's unchanged rotation");
    Tick(session,engine,Frame()); [session queueAutomatic]; return session;
}
static void AdvanceHimmelCharge(HimmelSessionEngine *engine) {
    // Explicit fixture premise: the preceding puzzle matched five attack
    // colours, so Himmel and Gerard's skill-charge awakening adds one more.
    // Solver goals are deliberately replaced by Combo in this scheduling test.
    for (unsigned slot = 0; slot < 6; ++slot) engine->live.automation.members[slot].charge += slot < 2 ? 2 : 1;
    if (engine->himmelBypassTurns && !--engine->himmelBypassTurns)
        engine->live.automation.playerBypass &= ~(PHOrbEnemyDamageAbsorb | PHOrbEnemyAttributeAbsorb | PHOrbEnemyDamageVoid);
    engine->live.automation.skillUses = 0;
}
static std::vector<NSUInteger> HimmelKaliUses(HimmelSessionEngine *engine) {
    std::vector<NSUInteger> result;
    for (NSUInteger slot : engine->skillOrder) if (slot >= 3) result.push_back(slot);
    return result;
}
static void PrepareHimmelOpening(HimmelSessionEngine *engine) {
    const uint32_t skills[] = {60246,60172,60147,63375,61750,60592};
    for (unsigned slot = 0; slot < 6; ++slot) {
        engine->live.automation.members[slot].charge = 25;
        engine->chosenSource[slot] = slot ? PHOrbSkillAssist : PHOrbSkillBody;
        engine->chosenSkill[slot] = skills[slot];
        engine->consumeAssistAfterSkill[slot] = slot != 0;
    }
}
static void HimmelSessionTests() {
    NSDictionary *complete = HimmelSessionProfile(), *post = HimmelSessionProfile(true);
    if (!complete || !post) return;
    Check([complete[@"settings"][@"rules"] isEqual:post[@"settings"][@"rules"]],
        "manual seven-step handoff and complete plan share the same first-unplayed-puzzle schedule");

    for (NSDictionary *profile in @[complete,post]) {
        HimmelSessionEngine *engine = [HimmelSessionEngine new];
        PHOrbSession *session = HimmelAfterOpeningSession(engine,profile);
        Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder == std::vector<NSUInteger>{2} &&
            engine->live.automation.members[3].charge == 2 && engine->live.automation.members[4].charge == 2 &&
            engine->live.automation.members[5].charge == 0,
            "first puzzle retains the opening Kali board while allowing the ready Indra body to run");
        AdvanceHimmelCharge(engine);
        Check(NextOrbTurn(session,engine) && HimmelKaliUses(engine) == std::vector<NSUInteger>{4} &&
            std::count(engine->skillOrder.begin(),engine->skillOrder.end(),NSUInteger(0)) == 1 &&
            std::count(engine->skillOrder.begin(),engine->skillOrder.end(),NSUInteger(1)) == 1 &&
            engine->chosenSkill[1] == 64943,
            "second puzzle starts Kali slot five and accepts the now-ready Himmel and evolving Gerard bodies");
        [session queueStop:@"pause Himmel after second puzzle"];
        AdvanceHimmelCharge(engine); engine->live.phase = 2; engine->live.moves = 0;
        for (unsigned i = 0; i < 8; ++i) Tick(session,engine,Frame(),0.12);
        [session queueAutomatic];
        Check(UntilPhase(session,engine,@"turnWaiting") && ObservedSkillTurn(session) == 3 &&
            [session.snapshot[@"autoTurn"] isEqual:@1] && HimmelKaliUses(engine) == (std::vector<NSUInteger>{4,3}),
            "stop and resume retain the observed turn and advance from Kali slot five to slot four");
        AdvanceHimmelCharge(engine);
        Check(NextOrbTurn(session,engine) && HimmelKaliUses(engine) == (std::vector<NSUInteger>{4,3,5}),
            "fourth puzzle gives the freshly recharged third Kali its turn");
        AdvanceHimmelCharge(engine);
        Check(NextOrbTurn(session,engine) && HimmelKaliUses(engine) == (std::vector<NSUInteger>{4,3,5,4}),
            "the three-Kali rotation wraps to slot five with one confirmed supply per puzzle");
        [session queueStop:@"end Himmel rotation fixture"];
    }

    {
        HimmelSessionEngine *engine = [HimmelSessionEngine new];
        PHOrbSession *session = HimmelAfterOpeningSession(engine,post);
        Check(UntilPhase(session,engine,@"turnWaiting"),"unavailable Kali fixture completes only the reserved first puzzle");
        const NSUInteger stages = engine->stages, releases = engine->releases;
        for (unsigned slot = 3; slot < 6; ++slot) engine->unavailableSkills[slot] = YES;
        AdvanceHimmelCharge(engine);
        // 2026-11-08 device log: a desynced rotation (skill delay, manual cast,
        // mid-run start) left every Kali unready. Supply is optional, so the
        // puzzle continues under the unchanged six-colour goal.
        Check(NextOrbTurn(session,engine) && [session.snapshot[@"automatic"] boolValue] &&
            HimmelKaliUses(engine).empty() && engine->stages > stages && engine->releases == releases + 1,
            "unready optional Kali alternatives are skipped and the second puzzle still runs");
        [session queueStop:@"end unavailable Kali fixture"];
    }

    for (unsigned effect : {unsigned(PHOrbEnemyDamageAbsorb),unsigned(PHOrbEnemyAttributeAbsorb),unsigned(PHOrbEnemyDamageVoid)})
        for (bool covered : {false,true}) {
            HimmelSessionEngine *engine = [HimmelSessionEngine new];
            engine->live.automation.enemyEffects = effect;
            engine->live.automation.playerBypass = covered ? effect : 0;
            PHOrbSession *session = HimmelAfterOpeningSession(engine,post);
            if (covered) {
                Check(UntilPhase(session,engine,@"turnWaiting") && engine->skillOrder == std::vector<NSUInteger>{2},
                    "an existing matching bypass permits the first puzzle without recasting unavailable Himmel");
                [session queueStop:@"end covered Himmel fixture"];
            } else {
                for (unsigned i = 0; i < 300 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
                Check(![session.snapshot[@"automatic"] boolValue] && !engine->stages && !engine->releases &&
                    [session.snapshot[@"message"] containsString:@"必要規則"] &&
                    std::count(engine->skillOrder.begin(),engine->skillOrder.end(),NSUInteger(0)) == 0,
                    "an uncovered absorb or void requirement stops when Himmel has only eight of ten charge");
            }
        }
    {
        HimmelSessionEngine *engine = [HimmelSessionEngine new];
        engine->live.automation.enemyEffects = PHOrbEnemyDamageAbsorb | PHOrbEnemyAttributeAbsorb | PHOrbEnemyDamageVoid;
        engine->live.automation.members[0].charge = 10;
        PHOrbSession *session = HimmelAfterOpeningSession(engine,post);
        Check(UntilPhase(session,engine,@"turnWaiting") &&
            std::count(engine->skillOrder.begin(),engine->skillOrder.end(),NSUInteger(0)) == 1 &&
            (engine->live.automation.playerBypass & 7u) == 7u,
            "one confirmed Himmel skill satisfies its three rescue rows and optional loop without a duplicate cast");
        [session queueStop:@"end ready Himmel rescue fixture"];
    }

    // Exercise the real seven-step opening with synthetic native skill
    // acknowledgements. Production still reads the actual cooldown and effect.
    for (bool missingConsumption : {false,true}) {
        HimmelSessionEngine *engine = [HimmelSessionEngine new];
        PHOrbSession *session = PlanSession(engine,complete,^{
            PrepareHimmelOpening(engine);
            if (missingConsumption) engine->consumeAssistAfterSkill[2] = NO;
        });
        if (missingConsumption) {
            for (unsigned i = 0; i < 300 && [session.snapshot[@"automatic"] boolValue]; ++i) Tick(session,engine,Frame());
            Check(![session.snapshot[@"automatic"] boolValue] && [session.snapshot[@"opening"][@"completed"] isEqual:@0] &&
                engine->skillOrder == std::vector<NSUInteger>{2} && !engine->stages,
                "the opening cannot advance from its first weapon without confirmed native consumption");
            continue;
        }
        Check(UntilPhase(session,engine,@"turnWaiting") && [session.snapshot[@"opening"][@"completed"] isEqual:@7] &&
            ![session.snapshot[@"opening"][@"puzzlePending"] boolValue] &&
            engine->skillOrder == (std::vector<NSUInteger>{2,1,0,3,4,3,5,2}),
            "the seven real opening skills consume the five weapons, use Kali slot four, then allow first-puzzle Indra");
        Check(engine->live.automation.members[0].number == 13413 && engine->live.automation.members[0].assistCatalogID,
            "Himmel's opening confirms transformation while retaining his unscheduled cloak");
        for (unsigned slot = 1; slot < 6; ++slot)
            Check(!engine->live.automation.members[slot].assistCatalogID && !engine->live.automation.members[slot].assistState &&
                engine->live.automation.members[slot].sourceCatalogIDs[1],
                "each consumed opening weapon retains its original source identity after the active assist disappears");
        Check(engine->live.automation.members[3].charge == 2 && engine->live.automation.members[4].charge == 2 &&
            engine->live.automation.members[5].charge == 0,
            "the full opening reaches the independently derived two-two-zero Kali scheduling state");
        [session queueStop:@"end full Himmel opening fixture"];
    }
}
