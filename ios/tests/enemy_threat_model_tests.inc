#include "PHEnemyThreat.h"
namespace ThreatCases {
namespace T = PHEnemyThreat;
static void record(PHEnemyActions::Graph &graph, int32_t id, uint16_t opcode,
    std::initializer_list<int32_t> params, int32_t attached = 0) {
    auto &node = graph.nodes[graph.count++]; node.id = id; node.known = true;
    std::memcpy(node.record.bytes.data() + 4, &opcode, 2);
    unsigned index = 0;
    for (int32_t value : params) std::memcpy(node.record.bytes.data() + 16 + 4 * index++, &value, 4);
    std::memcpy(node.record.bytes.data() + 68, &attached, 4);
}
static void events() {
    PHEnemyActions::Graph graph{};
    record(graph,1,83,{2,3,4,4,0,9},900);
    record(graph,2,18,{2,200}); record(graph,3,15,{2,4,50},25); record(graph,4,88,{5},30);
    auto seq = T::Describe(graph,1);
    Check(seq.complete() && seq.count == 10 && seq.events[0].kind == T::Kind::Enrage &&
        seq.events[1].attached && seq.events[1].value == 25 && seq.events[2].value == 50 &&
        !seq.events[3].optional && seq.events[4].optional && seq.events[5].optional &&
        seq.events[6].attached && seq.events[7].kind == T::Kind::AwokenBind &&
        seq.events[8].attached && seq.events[9].kind == T::Kind::AwokenBind,
        "compound order preserves duplicate bind attacks, per-hit optional range and ignores root attachment/raw suffix");
    const auto upper = T::BeforeShields(seq,{100,7,1,0,true},1000);
    Check(upper.known && upper.hits == 7 && upper.damage == 577 && upper.effects,
        "conditional pre-shield maximum uses strongest reachable enrage and consumes addition only once");
    record(graph,5,50,{50},100); record(graph,6,47,{999,75}); record(graph,7,82,{},999);
    seq = T::Describe(graph,5);
    Check(seq.complete() && seq.count == 2 && seq.events[0].kind == T::Kind::Attack && seq.events[1].kind == T::Kind::Gravity,
        "attached attack remains before a current-HP gravity");
    seq = T::Describe(graph,6);
    Check(seq.count == 1 && seq.events[0].value == 75, "percent attack uses parameter one rather than parameter zero");
    seq = T::Describe(graph,7);
    Check(seq.count == 1 && seq.events[0].value == 100 && !seq.events[0].attached,
        "root opcode82 becomes a normal attack without its unused attached field");
    record(graph,8,83,{7,6,0}); seq = T::Describe(graph,8);
    Check(seq.complete() && seq.count == 1 && seq.events[0].value == 75, "compound child82 is skipped");
    record(graph,9,83,{8,0}); seq = T::Describe(graph,9);
    Check(!seq.complete() && (seq.issues & T::Compound), "nested compounds retain an unknown event");
    record(graph,10,83,{7,7,7,7,7,7,7,7});
    Check(!T::Describe(graph,10).complete(), "unterminated eight-child compounds do not assert complete bounds");
    record(graph,11,1,{100}); seq = T::Describe(graph,11);
    Check(!seq.complete() && seq.events[0].kind == T::Kind::Unknown, "member bind is not guessed to be a normal attack");
    record(graph,12,15,{1,INT32_MAX,50});
    Check(!T::Describe(graph,12).complete(), "unreviewed overflowing native RNG arithmetic has no hit bound");
    record(graph,13,15,{20,30,50}); seq = T::Describe(graph,13);
    Check(seq.complete() && seq.count == 15 && !seq.events[14].optional, "native multi-hit count is capped at fifteen");
    record(graph,14,107,{5,-1}); seq = T::Describe(graph,14);
    Check(seq.complete() && seq.events[0].randomColor && seq.events[0].colors == 31,
        "random unmatchable chooses an attack color, never a random heart");
    record(graph,15,140,{2,5,1}); seq = T::Describe(graph,15);
    Check(seq.complete() && seq.events[0].targets == 33 && seq.events[0].maximum == 5,
        "targeted delay retains leader targets and a raw upper duration without claiming resistance");
    record(graph,16,140,{2,5,2}); seq = T::Describe(graph,16);
    Check(seq.events[0].targets == 30, "sub-only delay excludes both leaders");
    record(graph,17,89,{2,5}); seq = T::Describe(graph,17);
    Check(seq.events[0].targets == 63, "ordinary delay covers all six members");
    seq = T::Describe(graph,-1);
    Check(seq.complete() && seq.count == 1 && seq.events[0].value == 100, "no selection preserves the default normal attack");
    Check(!T::Describe(graph,1000).complete(), "missing definitions cannot turn into harmless events");
}
static void damage() {
    T::AttackState enemy{101,7,1,0,true};
    auto hit = T::RawHit(enemy,50);
    Check(hit.known && hit.value == 58 && enemy.addition == 0, "raw native float half rounds upward before once-only addition");
    hit = T::RawHit(enemy,50);
    Check(hit.known && hit.value == 51, "a second hit gets its own rounding without the consumed addition");
    Check(T::Reduce(3,0.5f).value == 2 && T::Reduce(T::Reduce(3,0.5f).value,0.5f).value == 1,
        "shield layers round sequentially rather than multiplying all reductions before rounding");
    Check(T::Gravity(101,50).known && T::Gravity(101,50).value == 50 && T::Gravity(101,100).value == 101,
        "gravity floors fractional current HP but uses exact integer HP for one hundred percent");
    enemy = {INT64_MAX,0,1,0,true}; Check(!T::RawHit(enemy,100).known, "out-of-range float conversion stays unknown");
    enemy = {100,0,std::numeric_limits<float>::quiet_NaN(),1,true};
    Check(!T::RawHit(enemy,100).known, "nonfinite active enrage is never damage zero");
    Check(!T::Reduce(100,1.1f).known && !T::Gravity(INT32_MAX,200).known, "invalid reduction and overflowing gravity remain unknown");
    PHEnemyActions::Graph graph{}; record(graph,1,83,{2,3,0}); record(graph,2,50,{50}); record(graph,3,15,{2,2,10});
    const auto seq = T::Describe(graph,1);
    T::SurvivalInput input; input.hpAfterPlayer = 100; input.enemy = {100,0,1,0,true};
    input.branchKnown = input.postPlayerKnown = input.nextFloorCovered = input.rescueReady = true;
    for (auto &ctx : input.hitContexts) ctx.known = true;
    auto result = T::Evaluate(seq,input);
    Check(result.known && result.safe && result.hpLower == 30, "gravity then two separately rounded hits replay in original order");
    input.hpAfterPlayer = 40; result = T::Evaluate(seq,input);
    Check(result.known && !result.safe && !result.hpLower && result.failingEvent == 3, "exactly lethal final hit never passes the survival gate");
    input.hpAfterPlayer = 100; input.reserve = 30; result = T::Evaluate(seq,input);
    Check(result.known && !result.safe, "required reserve is strict and cannot compensate for uncertain evidence");
    for (unsigned missing = 0; missing < 5; ++missing) {
        auto unavailable = input;
        if (missing == 0) unavailable.branchKnown = false;
        if (missing == 1) unavailable.postPlayerKnown = false;
        if (missing == 2) unavailable.nextFloorCovered = false;
        if (missing == 3) unavailable.rescueReady = false;
        if (missing == 4) unavailable.hitContexts[1].known = false;
        Check(!T::Evaluate(seq,unavailable).known, "missing branch, HP, preemptive, rescue or shield proof cannot authorize defense");
    }
    record(graph,4,83,{5,3,0}); record(graph,5,88,{5});
    Check(!T::Evaluate(T::Describe(graph,4),input).known, "awoken bind invalidates later HP/shield proof rather than reusing the old team");
}
static void combo() {
    PHOrbEnemyComboState state{}; state.valid = true; state.enemyCount = 3; state.aliveMask = 3;
    state.hp[0] = state.hp[1] = 100; state.thresholds[0] = 4; state.thresholds[1] = 7; state.thresholds[2] = 99;
    state.turns[0] = state.turns[1] = state.turns[2] = 3;
    unsigned minimum = 0;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && minimum == 8, "live enemy maximum excludes a dead enemy's stronger absorption");
    state.bypassTurns = 0x401;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && !minimum, "packed freshness alone does not alter active bypass");
    state.bypassDelay = 1;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && minimum == 8, "future bypass grants no current countermeasure exemption");
    state.bypassDelay = 0; state.bypassTurns = 0x400;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && minimum == 8, "freshness bit with zero turns is inactive");
    auto changed = state; ++changed.thresholds[1];
    Check(PHOrbSameEnemyCombo(&state,&state) && !PHOrbSameEnemyCombo(&state,&changed), "threshold changes revoke stale enemy evidence");
    state.adjustmentTurns = 0x403; state.adjustment = -2; state.subtractTurns = 1; state.subtract = 3;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && minimum == 13,
        "current signed adjustment and separate subtraction both increase the guaranteed first-removal requirement");
    state.adjustment = 5;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && minimum == 11,
        "positive adjustment is not credited against the separate subtraction");
    state.adjustment = -2; state.adjustmentTurns = 0x400; state.subtractTurns = 0x3ff;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && minimum == 8,
        "zero and signed-negative packed durations do not activate stale Combo debits");
    state.adjustmentTurns = 1; state.subtractTurns = 1; state.subtract = UINT16_MAX;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && minimum == 65545,
        "large debit remains an unachievable unsigned requirement without narrowing into a valid goal");
    state.bypassTurns = 1;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && !minimum,
        "verified current bypass removes the lower-absorption requirement even with a debit");
    state.upperTurns[2] = 3;
    Check(PHOrbEnemyComboRequirement(&state,&minimum) && !minimum,
        "a dead enemy's upper-Combo absorption cannot block the live enemies");
    state.upperTurns[0] = 3;
    Check(!PHOrbEnemyComboRequirement(&state,&minimum),
        "upper-Combo absorption remains unproved even with lower-absorption bypass");
    state.upperTurns[0] = 0;
    for (unsigned field = 0; field < 5; ++field) {
        changed = state;
        if (field == 0) ++changed.adjustment;
        if (field == 1) ++changed.adjustmentTurns;
        if (field == 2) ++changed.subtract;
        if (field == 3) ++changed.subtractTurns;
        if (field == 4) ++changed.upperTurns[0];
        Check(!PHOrbSameEnemyCombo(&state,&changed), "debit and upper-absorption changes revoke an earlier route proof");
    }
    state.flags[2] = 0x100;
    Check(!PHOrbEnemyComboRequirement(&state,&minimum), "unknown parts are rejected even with zero captured HP");
    state.flags[2] = 0; state.aliveMask = 7;
    Check(!PHOrbEnemyComboRequirement(&state,&minimum), "inconsistent alive masks cannot authorize a route");
}
}
static void EnemyThreatModelTests() { ThreatCases::events(); ThreatCases::damage(); ThreatCases::combo(); }
