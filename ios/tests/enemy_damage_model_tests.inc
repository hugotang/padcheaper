// Written cases only. Execute on the next explicitly requested build/test.
namespace DamageCases {
namespace D = PHEnemyDamage;
static D::Context context() { D::Context c; c.known = c.actionsEnabled = true; return c; }
static D::Enemy enemy() {
    D::Enemy e; e.known = true; e.hp = e.maximumHP = e.snapshotHP = e.secondarySnapshotHP = 100;
    e.countdown = 3; e.selected = 17; e.active = 1; return e;
}
static D::ResolvedHit hit(int64_t damage) { D::ResolvedHit h; h.known = true; h.damage = damage; return h; }
static void resolve() {
    auto c = context(); auto e = enemy();
    e.normalPercent = 50;
    for (int64_t snapshot : {49, 50, 51}) {
        e.snapshotHP = snapshot;
        const auto r = D::AfterResolvedHit(c, e, hit(100));
        Check(r.known && r.state.hp == (snapshot >= 50 ? 1 : 0) && r.state.countdown == 3 &&
            !(r.state.flags & 2u) && bool(r.events & D::NormalTriggered) == (snapshot >= 50),
            "normal resolve uses snapshot >= threshold without resetting countdown or setting special flag two");
    }
    e.snapshotHP = 100; e.hp = 10;
    Check(D::AfterResolvedHit(c, e, hit(10)).state.hp == 1, "snapshot is distinct from current HP");
    e.snapshotHP = 0; e.hp = 100; e.survival = 1;
    auto r = D::AfterResolvedHit(c, e, hit(0));
    Check(r.known && r.state.hp == 1 && (r.events & D::NormalPreserved), "existing normal bit forces one HP even after zero damage");
    e = enemy(); e.superTriggerPercent = 50; e.superFloorPercent = 30;
    for (int64_t snapshot : {49, 50, 51}) {
        e.snapshotHP = snapshot; r = D::AfterResolvedHit(c, e, hit(100));
        Check(r.known && r.state.hp == (snapshot > 50 ? 30 : 0) &&
            bool(r.events & D::SuperTriggered) == (snapshot > 50), "super resolve requires strict snapshot > trigger threshold");
    }
    e.snapshotHP = 100; r = D::AfterResolvedHit(c, e, hit(70));
    Check(r.known && r.state.hp == 30 && !(r.events & D::SuperTriggered), "landing exactly on floor does not trigger super resolve");
    e.survival = 8; e.snapshotHP = 0; r = D::AfterResolvedHit(c, e, hit(0));
    Check(r.known && r.state.hp == 30 && (r.events & D::SuperPreserved), "existing super bit unconditionally reapplies floor");
    e.survival |= 1; r = D::AfterResolvedHit(c, e, hit(0));
    Check(r.known && r.state.hp == 1 && (r.events & D::SuperPreserved) && (r.events & D::NormalPreserved),
        "normal preserved bit is processed after super floor");
    e = enemy(); e.normalPercent = 50; e.special = 7;
    r = D::AfterResolvedHit(c, e, hit(100));
    Check(r.known && r.state.hp == 1 && r.state.countdown == 0 && (r.state.flags & 2u) &&
        (r.events & D::NormalTriggered) && (r.events & D::SpecialCountdownZero), "special lethal path precedes normal resolve in original lethal block");
    c.immediate = 1; r = D::AfterResolvedHit(c, e, hit(100));
    Check(r.known && r.state.hp == 1 && r.state.snapshotHP == 1 && r.state.survival == 1 &&
        !(r.events & D::NormalTriggered) && (r.events & D::CleanupInvoked),
        "immediate special cleanup keeps special bit then overwrites snapshot before normal resolve comparison");
    c.actionsEnabled = false; r = D::AfterResolvedHit(c, e, hit(100));
    Check(r.known && r.state.countdown == 3 && !(r.state.flags & 2u) && (r.events & D::NormalTriggered),
        "disabled action table disables special survival but does not disable normal resolve");
    c = context(); e = enemy(); e.superTriggerPercent = e.superFloorPercent = 50;
    r = D::AfterResolvedHit(c, e, hit(100));
    auto repeated = D::AfterResolvedHit(c, r.state, hit(100));
    Check(repeated.known && repeated.state.hp == 50 && (repeated.events & D::SuperPreserved),
        "repeated hit before cleanup preserves previously set super floor");
    c.immediate = 1; r = D::AfterResolvedHit(c, e, hit(100));
    repeated = D::AfterResolvedHit(c, r.state, hit(100));
    Check(repeated.known && !repeated.state.hp && !(repeated.events & D::SuperTriggered),
        "after immediate cleanup snapshot equals threshold so second lethal hit does not retrigger super resolve");
}
static void phasesAndCleanup() {
    auto c = context(); auto e = enemy();
    e.superTriggerPercent = e.superFloorPercent = 50; e.hpThreshold = 30;
    auto r = D::AfterResolvedHit(c, e, hit(80));
    Check(r.known && r.hpBeforeSurvival == 20 && r.phaseBeforeSurvival == 1 && r.state.hp == 50 && r.state.phase == 1,
        "phase trigger is retained even when super resolve later raises HP above its threshold");
    r = D::AfterResolvedHit(c, e, hit(100));
    Check(r.known && r.state.hp == 50 && !r.state.phase, "zero HP is ineligible for HP-phase check before floor");
    e.hpThreshold = 60; c.immediate = 1; r = D::AfterResolvedHit(c, e, hit(100));
    Check(r.known && r.state.hp == 50 && r.state.phase == 5 && !r.state.survival && r.state.snapshotHP == 50 &&
        (r.events & D::SuperCleared), "immediate super cleanup reevaluates phase on restored HP and rewrites snapshot");
    e = enemy(); e.hp = 1; e.survival = 9; e.hpThreshold = 10;
    for (uint8_t immediate : {0, 1, 2, 3}) {
        c.immediate = immediate; auto cleaned = D::ConditionalCleanup(c, e);
        Check(cleaned.known && !cleaned.state.survival &&
            (cleaned.state.flags & 0xa0u) == (!(immediate & 1u) ? 0xa0u : 0) &&
            cleaned.state.phase == (immediate == 1 ? 5 : 1),
            "cleanup low-bit effect flags differ from exact-one phase flag behavior");
    }
    c = context(); e.flags = 2; r = D::ConditionalCleanup(c, e);
    Check(r.known && r.state.survival == 9 && r.state.flags == 2 && !r.state.phase,
        "paired special flags skip entire enemy cleanup including the super bit");
    e.flags = 0; e.active = 2; r = D::ConditionalCleanup(c, e);
    Check(r.known && r.state.survival == 9 && !r.state.phase, "cleanup needs active byte exactly one");
    e = enemy(); e.hp = 40; e.hpThreshold = 40;
    for (unsigned phase = 0; phase < 256; ++phase) {
        auto candidate = e; candidate.phase = uint8_t(phase); const auto p = D::CheckHPPhase(c, candidate);
        Check(p.known && p.triggered == (phase == 0 || phase == 0x20), "HP-phase admits only native signed-byte zero or bit five alone");
    }
    e = enemy(); e.maximumHP = e.hp = 1000000000; e.hp = 500000001; e.hpThreshold = 50;
    auto p = D::CheckHPPhase(c, e);
    Check(p.known && p.percentKnown && p.percent == 50 && p.triggered,
        "double percentage narrows to float before ceiling; rational comparison would incorrectly reject near boundary");
    e = enemy(); e.hp = 51; e.hpThreshold = 50; p = D::CheckHPPhase(c, e);
    Check(p.known && p.percent == 51 && !p.triggered, "exact percentage above threshold stays above");
    e.hp = 1; e.maximumHP = 3; e.hpThreshold = 33; p = D::CheckHPPhase(c, e);
    Check(p.known && p.percent == 34 && !p.triggered, "fractional percent is rounded upward using native float helper");
    e = enemy(); e.countThreshold = 2;
    for (unsigned sum : {1u, 2u, 3u}) {
        const auto scan = D::ConditionalPhaseEntry(c, e, sum, true);
        Check(scan.known && scan.targetRequested == (sum <= 2) && scan.phase == (sum <= 2 ? 0x13 : 0),
            "alternate phase uses sum of raw active bytes with inclusive threshold");
    }
    e.phase = 0x20; Check(!D::ConditionalPhaseEntry(c, e, 1, true).targetRequested, "phase bit five suppresses alternate trigger");
    e.phase = 1; auto scan = D::ConditionalPhaseEntry(c, e, 3, true);
    Check(scan.known && scan.targetRequested && scan.phase == 3 && !scan.countTriggered,
        "already marked HP-phase can request target even when count trigger does not apply");
    Check(!D::ConditionalPhaseEntry(c, e, 1, false).known, "unknown aggregate cannot become a known phase route");
    Check(!D::ConditionalPhaseEntry(c, e, 255u * D::MaxEnemies + 1, true).known, "aggregate outside captured slot bound is unknown");
    e = enemy(); e.hpInterval = 65535; e.countInterval = -3;
    for (uint8_t phase : {3, 7, 0x17}) {
        e.phase = phase; r = D::ConditionalPhaseAnimation(e);
        Check(r.known && r.state.countdown == (phase == 3 ? 0 : phase == 7 ? 65535 : -3) &&
            !r.state.selected && r.state.phase == (phase | 8), "phase animation distinguishes unsigned HP interval and signed count interval");
    }
    e = enemy(); e.survival = 9; e.flags = 0x82; e.snapshotHP = 9; e.secondarySnapshotHP = 8;
    r = D::ConditionalBatchSnapshot(e);
    Check(r.known && r.state.snapshotHP == 100 && r.state.secondarySnapshotHP == 100 && !r.state.survival && r.state.flags == 2 &&
        e.snapshotHP == 9 && e.survival == 9, "assumed batch snapshot updates only returned fields without mutating captured input");
}
static void numbersAndUnknown() {
    auto c = context(); auto e = enemy();
    e.maximumHP = 99; e.hp = 99; e.normalPercent = 50; e.superTriggerPercent = e.superFloorPercent = 50;
    Check(D::NormalThreshold(e).known && D::NormalThreshold(e).value == 50 && D::SuperThreshold(e, true).value == 50,
        "positive half threshold rounds upward");
    e.fixedSuperHP = (uint64_t(1) << 53) + 1;
    Check(D::SuperThreshold(e, true).known && D::SuperThreshold(e, true).value == (int64_t(1) << 53),
        "fixed unsigned HP uses double conversion, not exact integer copy");
    e.fixedSuperHP = uint64_t(1) << 63;
    Check(!D::SuperThreshold(e, true).known, "out-of-range signed conversion is unknown before C++ cast");
    e.maximumHP = INT64_MAX; e.normalPercent = 100;
    Check(!D::NormalThreshold(e).known, "maximum signed HP rounding into 2^63 remains unknown");
    e.normalPercent = e.superTriggerPercent = 0;
    Check(D::NormalThreshold(e).known && !D::NormalThreshold(e).value && D::SuperThreshold(e, true).known &&
        !D::SuperThreshold(e, true).value, "disabled thresholds ignore unusable fixed values");
    e = enemy(); e.hp = 10;
    Check(D::AfterResolvedHit(c, e, hit(-20)).state.hp == 30, "negative resolved delta can raise ordinary HP");
    Check(D::AfterResolvedHit(c, e, hit(-100)).state.hp == 100, "ordinary HP increase clamps to maximum");
    Check(D::AfterResolvedHit(c, e, hit(INT64_MIN)).state.hp == 0, "signed native subtract wrap is handled without C++ overflow");
    Check(D::AfterResolvedHit(c, e, {}).status == D::Status::EffectiveDamageUnknown, "raw or unknown damage never qualifies as a resolved hit");
    e.separateHP = 1; Check(D::AfterResolvedHit(c, e, hit(1)).status == D::Status::SeparateHPUnresolved,
        "separate shield HP path is not applied to ordinary HP");
    auto bypass = hit(1); bypass.bypassSeparateHP = true;
    Check(D::AfterResolvedHit(c, e, bypass).known, "explicit resolved descriptor bypass admits ordinary HP");
    e.separateHP = -1; c.callbackPresent = true; auto r = D::AfterResolvedHit(c, e, hit(1));
    Check(!r.known && r.prefixKnown && r.hpBeforeSurvival == 9 && r.status == D::Status::CallbackUnresolved,
        "positive-damage callback stops at known prefix before survival floors");
    Check(D::AfterResolvedHit(c, e, hit(0)).known, "zero effective damage does not enter positive-damage callback");
    c.specialMarker = D::SpecialMarker;
    Check(D::AfterResolvedHit(c, e, hit(0)).status == D::Status::SpecialUnresolved, "special runtime predicate stays unresolved");
    c = context(); e.flags = 0x100;
    Check(D::AfterResolvedHit(c, e, hit(0)).status == D::Status::Parts, "parts excluded even with numeric HP");
    e = enemy(); e.maximumHP = 0; Check(!D::AfterResolvedHit(c, e, hit(0)).known, "zero maximum is unknown");
    e = enemy(); c.known = false;
    Check(!D::AfterResolvedHit(c, e, hit(0)).known && !D::ConditionalCleanup(c, e).known &&
        !D::CheckHPPhase(c, e).known, "unknown context cannot publish known field checkpoints");
}
}
static void EnemyDamageModelTests() { DamageCases::resolve(); DamageCases::phasesAndCleanup(); DamageCases::numbersAndUnknown(); }
