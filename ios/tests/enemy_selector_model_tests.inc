// Written for the next explicit build/test request; no native RNG or helpers.
namespace SelectorCases {
namespace S = PHEnemySelector;
namespace A = PHEnemyActions;
struct Fixture {
    A::Graph graph{};
    S::Input input{};
    static void word(A::Record &record, unsigned offset, int32_t value) {
        std::memcpy(record.bytes.data() + offset, &value, sizeof(value));
    }
    A::Record &define(int32_t id, uint16_t opcode, int32_t hp = 100, int32_t cost = 0) {
        A::Node *node = nullptr;
        for (unsigned i = 0; i < graph.count; ++i) if (graph.nodes[i].id == id) node = &graph.nodes[i];
        if (!node) node = &graph.nodes[graph.count++];
        *node = {}; node->id = id; node->known = true;
        word(node->record, 0, id); std::memcpy(node->record.bytes.data() + 4, &opcode, sizeof(opcode));
        word(node->record, 48, 100); word(node->record, 52, 100);
        word(node->record, 56, hp); word(node->record, 64, cost);
        return node->record;
    }
    A::Record &row(unsigned at, uint16_t opcode, uint8_t a = 100, uint8_t b = 0, int32_t id = 0) {
        if (!id) id = int32_t(100 + at);
        input.rows[at] = {id, a, b}; return define(id, opcode);
    }
    S::Result run() const { S::Budget budget{}; return S::Analyze(input, graph, budget); }
};
static const S::Candidate *candidate(const S::Result &result, unsigned row, S::Origin origin) {
    for (const auto &item : result.candidates) if (item.row == row && item.origin == origin) return &item;
    return nullptr;
}
static bool hp(const S::Candidate *item, int low, int high, bool lowOpen = false, bool highOpen = false) {
    return item && item->hp.low == low && item->hp.high == high &&
        item->hp.lowOpen == lowOpen && item->hp.highOpen == highOpen;
}
static void controls() {
    Fixture f;
    f.input.flags = 0x80000008u;
    f.row(0, 0x16, 3); f.row(1, 0x2c, 4); f.row(2, 0x2d, 2); f.row(3, 0x18, 1);
    f.row(4, 0x15, 2); f.row(5, 88);
    auto r = f.run();
    Check(r.rowWalkComplete() && r.candidates.size() == 1 && r.candidates[0].row == 4 &&
        r.candidates[0].flags == 6 && !r.noSelectionPossible,
        "set/OR/XOR/clear update flags in order; once control returns immediately with its new flag");
    f = {}; f.input.flags = 2; f.row(0, 0x15, 3); f.row(1, 88);
    r = f.run();
    Check(!candidate(r, 0, S::Origin::Control) && candidate(r, 1, S::Origin::Primary),
        "any matching flag bit suppresses once control, even when not every bit is set");
    f.input.flags = 0; f.input.rows[0].a = 0; r = f.run();
    Check(r.candidates.size() == 1 && r.candidates[0].row == 0 && r.candidates[0].flags == 0,
        "zero once mask still returns the row without fabricating a flag");

    for (uint32_t count : {0u, 0x7ffffffeu, 0x7fffffffu, 0xffffffffu}) {
        f = {}; f.input.counter = count; f.row(0, 0x1a); f.row(1, 0x15, 1);
        r = f.run();
        Check(r.candidates.size() == 1 && r.candidates[0].counter ==
            (count <= 0x7ffffffeu ? count + 1 : count), "counter increment preserves the native unsigned saturation boundary");
        f.row(0, 0x1b); r = f.run();
        Check(r.candidates.size() == 1 && r.candidates[0].counter == (count ? count - 1 : 0),
            "counter decrement never underflows zero and treats high-bit values as unsigned");
    }
    f = {}; f.row(0, 0x19, 2); f.row(1, 0x26, 2, 7); f.row(2, 0x25); f.row(3, 88);
    r = f.run();
    Check(r.candidates.size() == 1 && r.candidates[0].row == 2 && r.candidates[0].counter == 6,
        "counter replacement uses operand B and decrement-return emits only when the resulting count is nonzero");
    f.input.rows[1].a = 9; r = f.run();
    Check(r.candidates.size() == 1 && r.candidates[0].counter == 1, "nonmatching replace leaves the counter alone");
    for (uint32_t count : {0u, 1u, 2u}) {
        f = {}; f.input.counter = count; f.row(0, 0x25); f.row(1, 88); r = f.run();
        Check(count == 2 ? r.candidates.size() == 1 && r.candidates[0].row == 0 :
            !candidate(r, 0, S::Origin::Control) && candidate(r, 1, S::Origin::Primary),
            "decrement-return distinguishes zero, one, and two before the decrement");
    }

    for (unsigned op : {0x17u, 0x2bu, 0x5bu}) {
        f = {}; f.input.flags = 2; f.row(0, uint16_t(op), 3, op == 0x2b ? 3 : 2);
        f.row(1, 0x15, 4); f.row(2, 0x15, 8); r = f.run();
        Check(r.candidates.size() == 1 && r.candidates[0].row == 2 && r.candidates[0].flags == 10,
            "flag jumps use any-bit tests and preserve their distinct operand B row conventions");
    }
    f.input.flags = 0; r = f.run();
    Check(r.candidates.size() == 1 && r.candidates[0].row == 1 && r.candidates[0].flags == 7,
        "set-flags-else-jump writes the mask then falls through when no bit was present");
    for (unsigned op : {0x1eu, 0x1fu, 0x20u, 0x21u, 0x22u, 0x23u}) {
        f = {}; f.input.counter = 5; f.input.level = 5;
        f.row(0, uint16_t(op), 5, 3); f.row(1, 0x15, 1); f.row(2, 0x15, 2); r = f.run();
        Check(r.candidates.size() == 1 && r.candidates[0].row == 2,
            "unsigned counter and level comparisons include equality at both inclusive boundaries");
    }
    f = {}; f.input.counter = 0xffffffffu; f.row(0, 0x1e, 255, 3);
    f.row(1, 0x15, 1); f.row(2, 0x15, 2); r = f.run();
    Check(r.candidates.size() == 1 && r.candidates[0].row == 1, "unsigned high-bit count is not less than byte operand 255");
}
static void intervalsAndPasses() {
    Fixture f;
    f.row(0, 0x1c, 50, 4); f.row(1, 0x1c, 40, 5); f.row(2, 0x15, 1);
    f.row(3, 0x15, 2); f.row(4, 0x15, 4);
    auto r = f.run();
    Check(r.rowWalkComplete() && r.candidates.size() == 2 &&
        hp(candidate(r, 2, S::Origin::Control), 50, 100, true) &&
        hp(candidate(r, 3, S::Origin::Control), 0, 50) && !candidate(r, 4, S::Origin::Control),
        "HP branches keep open equality boundaries and discard contradictory nested conditions");
    for (unsigned op : {0x1cu, 0x1du}) for (unsigned threshold : {0u, 100u}) {
        f = {}; f.row(0, uint16_t(op), uint8_t(threshold), 3); f.row(1, 0x15, 1); f.row(2, 0x15, 2);
        r = f.run(); const auto *taken = candidate(r, 2, S::Origin::Control);
        Check(op == 0x1c ? hp(taken, 0, int(threshold)) : hp(taken, int(threshold), 100),
            "HP zero and one hundred preserve inclusive singleton intervals without rounding current HP");
        Check((op == 0x1c && threshold == 100) || (op == 0x1d && threshold == 0) ? r.candidates.size() == 1 :
            r.candidates.size() == 2, "boundary splits retain every nonempty open complement");
    }
    f = {}; auto &leaf = f.row(0, 88, 100, 1); Fixture::word(leaf, 56, 20); f.row(1, 0x24);
    r = f.run();
    Check(hp(candidate(r, 0, S::Origin::Primary), 0, 20) && hp(candidate(r, 0, S::Origin::Weighted), 0, 20),
        "ordinary leaves retain low-HP candidates independently of the captured pre-player HP");
    Fixture::word(leaf, 56, -1); r = f.run();
    Check(r.candidates.empty() && r.rowWalkComplete(), "negative signed HP threshold cannot match normalized HP");
    Fixture::word(leaf, 56, INT32_MAX); r = f.run();
    Check(hp(candidate(r, 0, S::Origin::Primary), 0, 100),
        "HP threshold above one hundred retains the full normalized domain");

    f = {}; f.input.mode = S::Mode::LegacyTwoPass;
    f.row(0, 0x1f, 0, 4); f.row(1, 88, 0, 100); f.row(2, 0x24);
    f.row(3, 0x19, 1); f.row(4, 0x2c, 4); f.row(5, 0x24); r = f.run();
    const auto *second = candidate(r, 1, S::Origin::Secondary);
    Check(r.rowWalkComplete() && second && second->flags == 4 && second->counter == 1 &&
        !candidate(r, 1, S::Origin::Primary), "legacy second scan restarts at row zero but inherits first-pass flags and counter");
    f = {}; f.input.mode = S::Mode::LegacyTwoPass;
    f.row(0, 0x1c, 20, 3); f.row(1, 0x24); f.row(2, 88, 0, 100); f.row(3, 0x24); r = f.run();
    Check(hp(candidate(r, 2, S::Origin::Secondary), 0, 20),
        "legacy second-pass reachability retains the HP partition inherited from its first pass");
    f = {}; f.input.mode = S::Mode::LegacyTwoPass;
    f.row(0, 88, 0, 100, 36); f.row(1, 141, 0, 100); r = f.run();
    Check(r.rowWalkComplete() && r.candidates.empty() && r.noSelectionPossible,
        "legacy second-pass row ID 36 terminates before lookup even when its opcode is a normal leaf");
    f.input.mode = S::Mode::OrderedWeighted; f.input.rows[0].a = 100; r = f.run();
    Check(candidate(r, 0, S::Origin::Primary), "row ID 36 is not an ordered-selector sentinel");
    f = {}; f.row(0, 0x24); f.row(1, 88); r = f.run();
    Check(r.candidates.empty() && r.noSelectionPossible, "opcode 36 ends the pass regardless of its row ID");
}
static void choicesAndLimits() {
    Fixture f; f.input.budget = 3;
    auto &expensive = f.row(0, 88, 100, 100); Fixture::word(expensive, 64, 4);
    auto &cheap = f.row(1, 141, 0, 100); Fixture::word(cheap, 64, -1);
    f.row(2, 82, 100); f.row(3, 47); f.row(4, 49); f.row(5, 0x24);
    auto r = f.run();
    Check(r.rowWalkComplete() && !candidate(r, 0, S::Origin::Primary) &&
        !candidate(r, 0, S::Origin::Weighted) && candidate(r, 1, S::Origin::Weighted) &&
        !candidate(r, 1, S::Origin::Primary) && candidate(r, 2, S::Origin::Primary) &&
        !candidate(r, 3, S::Origin::Primary) && !candidate(r, 4, S::Origin::Primary),
        "cost is signed, weighted candidates need B, and ordered mode explicitly excludes 47 and 49");
    f = {}; f.input.mode = S::Mode::LegacyTwoPass;
    auto &percent = f.row(0, 47, 100, 100); Fixture::word(percent, 56, -1);
    f.row(1, 49); r = f.run();
    Check(hp(candidate(r, 0, S::Origin::Primary), 0, 100) && !candidate(r, 0, S::Origin::Secondary) &&
        !candidate(r, 1, S::Origin::Primary), "legacy primary 47 bypasses HP filtering, while secondary rejects 47 and 49");
    f.input.rows[0].a = 0; r = f.run();
    Check(candidate(r, 0, S::Origin::Primary),
        "legacy 47 bypasses the explicit A gate; unmodeled floating chance cannot prove rejection from A zero alone");
    f = {}; auto &zeroProbability = f.row(0, 88); Fixture::word(zeroProbability, 48, 0); r = f.run();
    Check(candidate(r, 0, S::Origin::Primary) && r.noSelectionPossible,
        "unmodeled probability deliberately keeps both candidate and skip without inventing certainty");
    f = {}; f.define(9, 88); r = f.run();
    Check(r.rowWalkComplete() && r.candidates.empty() && r.noSelectionPossible,
        "cached-only definitions do not enter row candidates and empty rows do not prove a harmless turn");

    f = {}; f.input.rows[0].id = 999; f.row(1, 88); r = f.run();
    Check((r.issues & S::MissingDefinition) && r.missingRows == 1 && r.candidates.empty(),
        "missing definition creates an unknown frontier, not an assumed fallthrough");
    f.row(0, 65535); r = f.run();
    Check((r.issues & S::UnsupportedOpcode) && r.unsupportedRows == 1 && r.candidates.empty(),
        "unknown control effects cannot be skipped to claim a complete candidate set");
    for (unsigned op : {0x17u, 0x1cu}) {
        f = {}; f.input.flags = 1; f.row(0, uint16_t(op), op == 0x1c ? 100 : 1, op == 0x17 ? 64 : 0);
        r = f.run();
        Check((r.issues & S::InvalidJump) && r.invalidJumpRows == 1, "out-of-range jumps are never clamped to an in-range row");
    }
    f = {}; f.input.flags = 1; f.row(0, 0x17, 1, 0); r = f.run();
    Check((r.issues & S::RepeatedState) && !r.rowWalkComplete(), "backward repeated state terminates as an incomplete frontier");
    f = {}; f.input.flags = 1; f.row(0, 0x1a); f.row(1, 0x17, 1, 0); r = f.run();
    Check((r.issues & S::StateLimit) && r.states == S::MaxStates && r.steps <= S::MaxStates,
        "changing-counter loops cannot bypass the per-enemy state bound");
    f = {}; f.input.flags = 1; f.row(0, 88, 0, 1); f.row(1, 0x17, 1, 0); r = f.run();
    Check((r.issues & S::WeightedPoolLimit) && !r.rowWalkComplete(),
        "repeated weighted rows consume native pool capacity even when the action ID set is unchanged");
    f = {}; for (unsigned row = 0; row < A::Rows; ++row) f.row(row, 88, 0, 1);
    r = f.run();
    Check(r.rowWalkComplete() && r.candidates.size() == A::Rows && candidate(r, 63, S::Origin::Weighted),
        "all sixty-four weighted entries fit and the final row survives natural pass exhaustion");
    f = {}; f.input.flags = 1; f.row(0, 88); f.row(1, 88); f.row(2, 88);
    f.row(3, 0x1a); f.row(4, 0x17, 1, 0); r = f.run();
    Check((r.issues & S::CandidateLimit) && r.candidates.size() == S::MaxCandidates,
        "distinct return states cannot grow candidate output beyond the independent candidate bound");
    S::Budget shared{S::MaxCaptureSteps - 2}; f = {}; f.row(0, 88); f.row(1, 0x24);
    auto first = S::Analyze(f.input, f.graph, shared); auto next = S::Analyze(f.input, f.graph, shared);
    Check(first.rowWalkComplete() && first.steps == 2 && next.steps == 0 && (next.issues & S::StepLimit) &&
        shared.steps == S::MaxCaptureSteps, "the capture step budget persists across enemies and is not reset per analysis");
    shared.steps = S::MaxCaptureSteps - 1; auto partial = S::Analyze(f.input, f.graph, shared);
    Check(partial.steps == 1 && (partial.issues & S::StepLimit) && partial.candidates.size() == 1 &&
        !partial.rowWalkComplete(), "step exhaustion after a candidate still marks the unresolved skip branch incomplete");
}
static void sequences() {
    Fixture f; auto &root = f.define(1, 83);
    const int32_t children[]{2, 3, 2, 4, 0, 99};
    for (unsigned i = 0; i < 6; ++i) Fixture::word(root, 16 + 4 * i, children[i]);
    auto &bind = f.define(2, 88); Fixture::word(bind, 16, 4); Fixture::word(bind, 68, 150);
    f.define(3, 141); f.define(4, 82);
    auto s = S::DescribeSequence(f.graph, 1);
    Check(s.structureKnown && s.compound && s.count == 4 && s.steps[0].id == 2 && s.steps[2].id == 2 &&
        s.steps[0].attachedPercent == 150 && s.steps[0].parameter0 == 4 && s.steps[3].skipped && !s.fallbackRetained,
        "compound sequence preserves repeated effects, raw attached attack and the first nonpositive terminator");
    s = S::DescribeSequence(f.graph, 4);
    Check(s.structureKnown && s.fallbackRetained && !s.steps[0].skipped,
        "root 82 retains negative-selection normal attack fallback while child 82 is skipped");
    f.define(3, 83); s = S::DescribeSequence(f.graph, 1);
    Check(!s.structureKnown && s.count == 4 && s.steps[1].opcode == 83,
        "nested compounds stay unknown and are never flattened into a fabricated execution sequence");
    Fixture::word(root, 16, 1); s = S::DescribeSequence(f.graph, 1);
    Check(!s.structureKnown && s.count == 4, "self-referencing sequence stays bounded without recursively expanding itself");
    for (unsigned i = 0; i < 8; ++i) Fixture::word(root, 16 + 4 * i, 2);
    s = S::DescribeSequence(f.graph, 1);
    Check(!s.structureKnown && s.count == 8, "eight positive children do not assume the native four-bit cursor ends there");
    Fixture::word(root, 16, 999); Fixture::word(root, 20, 0); s = S::DescribeSequence(f.graph, 1);
    Check(!s.structureKnown && s.count == 1 && !s.steps[0].definitionKnown, "missing child remains an explicit unknown sequence step");
}
} // namespace SelectorCases
static void EnemySelectorModelTests() {
    SelectorCases::controls(); SelectorCases::intervalsAndPasses();
    SelectorCases::choicesAndLimits(); SelectorCases::sequences();
}
