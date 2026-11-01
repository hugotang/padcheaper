// Written cases only, to run with the next explicitly authorized build/test.
namespace DispatchCases {
namespace D = PHEnemyDispatch;
static D::Context context() {
    D::Context c; c.known = true; c.actionsEnabled = true; c.redirect = D::Redirect::Inactive; return c;
}
static D::Enemy enemy() { D::Enemy e; e.known = true; e.hp = 100; e.interpolationEqual = true; return e; }
static void entries() {
    auto c = context(); auto e = enemy();
    Check(D::Classify(c, e, 0).route == D::Route::OrdinaryRows, "ordinary captured entry reaches row zero conditionally");
    e.selected = 8;
    for (uint8_t mode : {0, 1, 2, 3}) {
        c.cacheMode = mode; const auto r = D::Classify(c, e, 0);
        Check(r.cacheCleared == !(mode & 1u) && r.selectedAfterClear == (!(mode & 1u) ? -1 : 8) &&
            r.route == (mode == 1 ? D::Route::CachedAction : D::Route::OrdinaryRows),
            "scheduler bit test differs from exact dispatcher cache value one");
    }
    c.cacheMode = 1; c.specialMarker = D::SpecialMarker; e.compound = 0x100;
    Check(D::Classify(c, e, 0).route == D::Route::CompoundContinuation,
        "compound continuation precedes cached and special selection without predicting its child");
    e.compound = 0;
    Check(D::Classify(c, e, 0).route == D::Route::CachedAction, "positive cache precedes special predicate");
    e.selected = 0;
    Check(D::Classify(c, e, 0).route == D::Route::SpecialUnresolved, "cached zero is not a positive cached action");
    c.preDispatchHook = 1;
    Check(D::Classify(c, e, 0).route == D::Route::PreDispatchHook, "unmodeled scheduler hook prevents entry exclusion claims");
    c.actionsEnabled = false;
    Check(D::Classify(c, e, 0).route == D::Route::PreDispatchHook, "hook runs before the action-enabled check");
    c.preDispatchHook = 2;
    Check(D::Classify(c, e, 0).route == D::Route::NormalAttackOnly, "only hook value one invokes the hook; disabled skills retain normal attack");
    c = context(); e = enemy(); e.countdown = 1; e.flags = 8;
    Check(D::Classify(c, e, 0).route == D::Route::Countdown, "positive countdown is examined before native flag eight");
    e.countdown = 0;
    Check(D::Classify(c, e, 0).route == D::Route::FlagBlocked, "native flag eight excludes this captured scheduling point");
    for (unsigned life = 0; life < 2; ++life) for (unsigned survival = 0; survival < 2; ++survival) for (unsigned flag = 0; flag < 2; ++flag) {
        e = enemy(); e.hp = life; e.survivalFlags = uint8_t(survival); e.flags = uint16_t(flag * 2);
        e.special = -1;
        Check((D::Classify(c, e, 0).route == D::Route::Inactive) == (!life && !(survival && flag)),
            "scheduler HP-zero admission requires both survival and native flag two");
    }
    e.flags |= 0x100u;
    Check(D::Classify(c, e, 0).route == D::Route::Parts, "parts never reach ordinary dispatch");
    e.known = false;
    Check(D::Classify(c, e, 0).route == D::Route::Unknown, "unknown enemy is not an inactive enemy");
    e = enemy(); c.known = false;
    Check(D::Classify(c, e, 0).route == D::Route::Unknown, "unknown context is not a row-zero proof");
}
static void overrides() {
    auto c = context(); auto e = enemy(); e.attackAddition = 1; e.overrideState = 1; e.overrideAction = 8;
    auto r = D::Classify(c, e, 0);
    Check(r.route == D::Route::SelectorFallback && r.directSelectionKnown && r.directSelection == -1,
        "positive signed attack addition returns negative before override action");
    e.attackAddition = INT32_MIN;
    r = D::Classify(c, e, 0);
    Check(r.route == D::Route::ActionOverride && r.directSelection == 8, "negative signed addition does not shadow override");
    e.overrideAction = -2;
    Check(D::Classify(c, e, 0).directSelection == -2, "negative override remains its native signed short value");
    e.overrideState = 0; e.special = 0; e.flags = 2;
    r = D::Classify(c, e, 0);
    Check(r.route == D::Route::SpecialAction && !r.directSelectionKnown, "ordered nonnegative special ID can select a different parameter action");
    e.ordered = false;
    Check(D::Classify(c, e, 0).route == D::Route::SelectorFallback, "legacy special state returns negative without ordered handling");
    e = enemy(); c.redirect = D::Redirect::Unknown;
    Check(D::Classify(c, e, 0).route == D::Route::Unknown, "unread redirect definition does not imply inactive redirect");
    e.ordered = false;
    Check(D::Classify(c, e, 0).route == D::Route::OrdinaryRows, "legacy entry does not use the ordered redirect predicate");
    e.ordered = true; c.redirect = D::Redirect::RowChange; c.redirectSlot = 1; c.redirectRow = 4;
    Check(D::Classify(c, e, 0).route == D::Route::OrdinaryRows && D::Classify(c, e, 1).redirectRow == 4,
        "redirect target is local to its matching enemy slot");
    for (int row : {INT32_MIN, -1, 0, 1, 64, 65, INT32_MAX}) {
        c.redirectRow = row; r = D::Classify(c, e, 1);
        Check(r.route == (row <= 0 ? D::Route::Unknown : row > 64 ? D::Route::SelectorFallback : D::Route::RedirectedRows),
            "redirect row endpoints preserve native upper fallback without underflowing an invalid lower cursor");
    }
}
static void tick() {
    auto c = context(); auto e = enemy(); c.cacheMode = 1;
    for (int count : {INT32_MIN, -1, 0, 1, 2, INT32_MAX}) {
        e.countdown = count; const auto t = D::ConditionalTick(c, e);
        Check(t.known && t.cacheMode == 0 && t.decremented == (count > 0) && t.countdown == (count > 0 ? count - 1 : count),
            "conditional tick decrements only positive signed countdown and clears cache");
    }
    e.countdown = 1; c.loopState = D::TickExcludedState;
    auto t = D::ConditionalTick(c, e);
    Check(t.known && !t.decremented && t.countdown == 1 && t.cacheMode == 0, "excluded loop still clears cache before skipping countdown");
    c.loopState = 0; e.hp = 0; e.survivalFlags = 1; e.flags = 0;
    Check(D::ConditionalTick(c, e).decremented && D::Classify(c, e, 0).route == D::Route::Inactive,
        "tick HP-zero eligibility is weaker than scheduler eligibility and must not be shared");
    e.flags = 0x100;
    Check(!D::ConditionalTick(c, e).decremented, "parts do not receive the ordinary countdown decrement");
    c.known = false;
    Check(!D::ConditionalTick(c, e).known, "unknown inputs do not project a countdown");
}
static void selection() {
    D::SelectionInput in; in.actionCount = UINT32_MAX; in.budget = 20; in.maximumBudget = 100; in.regeneration = 3;
    in.compound = 0x321; in.protectedCountKnown = true; in.protectedCount = 59999;
    PHEnemyActions::Record record{};
    uint16_t opcode = 88; int32_t cost = 5;
    std::memcpy(record.bytes.data() + 4, &opcode, 2); std::memcpy(record.bytes.data() + 64, &cost, 4);
    auto r = D::AfterFreshSelection(in, 8, &record);
    Check(r.checkpointKnown && !r.actionCount && r.protectedCount == 60000 && r.budgetAfterRegeneration == 23 &&
        r.budgetAtCheckpoint == 18 && r.costApplied && r.compoundWritten && r.compound == ((8u << 9) | 0xf0u),
        "fresh selection count wraps and conditional accounting stops before native preparation");
    for (uint16_t count : {59998, 59999, 60000, 65535}) {
        in.protectedCount = count; r = D::AfterFreshSelection(in, -1, nullptr);
        Check(r.protectedCount == (count < 60000 ? count + 1 : count), "protected dispatch count stops at native 60000 boundary");
    }
    struct BudgetCase { uint16_t budget; int16_t regen, max; uint16_t expected; };
    for (const auto &c : {BudgetCase{20, 3, 100, 23}, BudgetCase{100, 3, 100, 100},
        BudgetCase{0, -1, 100, 0}, BudgetCase{20, 0, -1, 65535}, BudgetCase{0, -1, -100, 0},
        BudgetCase{0, 0, -32768, 32768}, BudgetCase{65535, 32767, 32767, 32767}}) {
        in.budget = c.budget; in.regeneration = c.regen; in.maximumBudget = c.max;
        r = D::AfterFreshSelection(in, -2, nullptr);
        Check(r.budgetAtCheckpoint == c.expected && r.negativeFallback && r.selected == -2 && !r.costApplied &&
            r.compound == in.compound, "signed clamp order and uint16 conversion preserve nonstandard bounds on negative return");
    }
    in.budget = 1; in.regeneration = 0; in.maximumBudget = 100;
    for (int32_t value : {5, -1, 65537}) {
        std::memcpy(record.bytes.data() + 64, &value, 4); r = D::AfterFreshSelection(in, 8, &record);
        const uint16_t expected = value == 5 ? 65532 : value == -1 ? 2 : 0;
        Check(r.chargedCost == uint16_t(value) && r.budgetAtCheckpoint == expected,
            "dispatcher subtracts low unsigned16 cost with wrap independently of selector signed32 comparison");
    }
    opcode = 82; std::memcpy(record.bytes.data() + 4, &opcode, 2);
    r = D::AfterFreshSelection(in, 8, &record);
    Check(r.negativeFallback && r.selected == -1 && !r.costApplied && r.budgetAtCheckpoint == 1 && !r.compoundWritten,
        "positive root82 converts to negative only after regeneration and before cost or pack");
    r = D::AfterFreshSelection(in, 0, &record);
    Check(r.checkpointKnown && r.costApplied && !r.negativeFallback,
        "ID zero is not covered by positive-root82 conversion if its table entry is explicitly supplied");
    r = D::AfterFreshSelection(in, 8, nullptr);
    Check(!r.checkpointKnown && r.budgetAfterRegeneration == 1 && !r.negativeFallback,
        "missing captured record yields a known prefix, not native lookup failure or zero damage");
    in.protectedCountKnown = false; opcode = 83; std::memcpy(record.bytes.data() + 4, &opcode, 2);
    r = D::AfterFreshSelection(in, 8, &record);
    Check(r.checkpointKnown && !r.protectedCountKnown && r.compoundPreparation,
        "unavailable protected count stays unknown separately from the known arithmetic checkpoint");
}
} // namespace DispatchCases
static void EnemyDispatchModelTests() {
    DispatchCases::entries(); DispatchCases::overrides(); DispatchCases::tick(); DispatchCases::selection();
}
