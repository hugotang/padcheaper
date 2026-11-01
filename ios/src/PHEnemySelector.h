#pragma once
#include "PHEnemyActions.h"
#include <vector>

// Conditional ordinary-row analysis. This never establishes that the game's
// next dispatch will enter this selector, or that the captured AI state will
// survive the player's action. Native feasibility and random choice are not
// called: both selection and rejection remain possible for ordinary leaves.
namespace PHEnemySelector {
namespace EA = PHEnemyActions;
constexpr unsigned Version = 1, MaxStates = 512, MaxCandidates = 256, MaxCaptureSteps = 4096;
static_assert(EA::Rows == 64, "selector row masks and native weighted pool require 64 rows");
enum Issue : uint32_t {
    MissingDefinition = 1u << 0, UnsupportedOpcode = 1u << 1,
    InvalidJump = 1u << 2, RepeatedState = 1u << 3,
    StateLimit = 1u << 4, StepLimit = 1u << 5, CandidateLimit = 1u << 6,
    WeightedPoolLimit = 1u << 7
};
enum class Mode : uint8_t { OrderedWeighted, LegacyTwoPass };
enum class Origin : uint8_t { Control, Primary, Secondary, Weighted };

struct HPRange {
    // Range of the native normalized percentage, including endpoint flags.
    // Integer boundaries avoid replacing its <=/> comparisons with rounded
    // integer HP. The live adapter deliberately supplies the full [0,100].
    int low = 0, high = 100;
    bool lowOpen = false, highOpen = false;
    bool valid() const { return low < high || (low == high && !lowOpen && !highOpen); }
};
inline bool SameHP(const HPRange &a, const HPRange &b) {
    return a.low == b.low && a.high == b.high && a.lowOpen == b.lowOpen && a.highOpen == b.highOpen;
}
inline HPRange AtMost(HPRange h, int threshold, bool include) {
    if (threshold < h.high) { h.high = threshold; h.highOpen = !include; }
    else if (threshold == h.high) h.highOpen |= !include;
    return h;
}
inline HPRange AtLeast(HPRange h, int threshold, bool include) {
    if (threshold > h.low) { h.low = threshold; h.lowOpen = !include; }
    else if (threshold == h.low) h.lowOpen |= !include;
    return h;
}
struct Row {
    int32_t id = 0;
    uint8_t a = 0, b = 0;
};
struct Input {
    std::array<Row, EA::Rows> rows{};
    Mode mode = Mode::OrderedWeighted;
    uint32_t flags = 0, counter = 0;
    uint16_t level = 0, budget = 0;
};
struct Budget { unsigned steps = 0; };
struct State {
    uint32_t flags = 0, counter = 0;
    HPRange hp{};
    uint64_t weighted = 0;
    int row = 0;
    uint8_t pass = 0, weightedCount = 0;
};
inline bool SameState(const State &a, const State &b) {
    return a.flags == b.flags && a.counter == b.counter && SameHP(a.hp, b.hp) &&
        a.weighted == b.weighted && a.weightedCount == b.weightedCount && a.row == b.row && a.pass == b.pass;
}
struct Candidate {
    int32_t id = 0;
    uint32_t flags = 0, counter = 0;
    HPRange hp{};
    uint8_t row = 0;
    Origin origin = Origin::Primary;
};
struct Result {
    std::vector<Candidate> candidates;
    uint32_t issues = 0;
    uint64_t missingRows = 0, unsupportedRows = 0, invalidJumpRows = 0;
    unsigned states = 0, steps = 0;
    bool noSelectionPossible = false;
    bool rowWalkComplete() const { return issues == 0; }
};

inline bool SupportedLeaf(uint16_t opcode) {
    // 47/49 are explicitly excluded by the ordered selector. Legacy primary
    // admits 47; legacy secondary rejects both. 82 is converted to a negative
    // selected ID by the dispatcher; it is not a proven harmless no-op.
    return opcode == 47 || opcode == 49 || opcode == 82 || opcode == 83 || opcode == 88 || opcode == 141;
}
inline const char *OriginName(Origin origin) {
    switch (origin) {
        case Origin::Control: return "control-return";
        case Origin::Primary: return "primary-choice";
        case Origin::Secondary: return "secondary-choice";
        case Origin::Weighted: return "weighted-pool";
    }
    return "unknown";
}

inline Result Analyze(const Input &input, const EA::Graph &graph, Budget &budget) {
    Result result;
    result.candidates.reserve(32);
    std::vector<State> states;
    states.reserve(MaxStates);
    State initial{}; initial.flags = input.flags; initial.counter = input.counter;
    states.push_back(initial);
    const unsigned started = budget.steps;
    auto enqueue = [&](const State &next) {
        if (!next.hp.valid()) return;
        if (next.row < 0 || next.row > int(EA::Rows)) { result.issues |= InvalidJump; return; }
        for (const auto &old : states) if (SameState(old, next)) {
            // Stop a possible control loop. Treat convergence as incomplete
            // too; deduplication alone cannot establish native termination.
            result.issues |= RepeatedState; return;
        }
        if (states.size() == MaxStates) { result.issues |= StateLimit; return; }
        states.push_back(next);
    };
    auto advance = [&](State next) { ++next.row; enqueue(next); };
    auto jump = [&](State next, int target, unsigned from) {
        // No row64 endpoint is accepted as a jump: the native caller's loop
        // endpoint depends on its pre-increment cursor. Refuse, do not clamp.
        if (target < 0 || target >= int(EA::Rows)) {
            result.issues |= InvalidJump; result.invalidJumpRows |= UINT64_C(1) << from; return;
        }
        next.row = target; enqueue(next);
    };
    auto emit = [&](const State &state, unsigned row, Origin origin) {
        const Candidate candidate{input.rows[row].id, state.flags, state.counter, state.hp, uint8_t(row), origin};
        for (const auto &old : result.candidates) if (old.id == candidate.id && old.row == candidate.row &&
            old.flags == candidate.flags && old.counter == candidate.counter && old.origin == candidate.origin &&
            SameHP(old.hp, candidate.hp)) return;
        if (result.candidates.size() == MaxCandidates) { result.issues |= CandidateLimit; return; }
        result.candidates.push_back(candidate);
    };
    auto finishPass = [&](State state) {
        if (input.mode == Mode::LegacyTwoPass && !state.pass) {
            state.pass = 1; state.row = 0; enqueue(state); // Flags/counter/HP survive.
        } else {
            if (input.mode == Mode::OrderedWeighted) for (unsigned row = 0; row < EA::Rows; ++row)
                if (state.weighted & (UINT64_C(1) << row)) emit(state, row, Origin::Weighted);
            // Feasibility may reject all pooled candidates. This means the
            // selector can return no ID, not that the enemy will skip damage.
            result.noSelectionPossible = true;
        }
    };
    for (size_t cursor = 0; cursor < states.size(); ++cursor) {
        if (budget.steps >= MaxCaptureSteps) { result.issues |= StepLimit; break; }
        ++budget.steps;
        State state = states[cursor];
        if (state.row == int(EA::Rows)) { finishPass(state); continue; }
        const unsigned index = unsigned(state.row);
        const Row row = input.rows[index];
        if (!row.id) { advance(state); continue; }
        // Native legacy second pass compares the row's ID to 36 before
        // loading its definition; it is distinct from opcode36 end-pass.
        if (input.mode == Mode::LegacyTwoPass && state.pass && row.id == 36) { finishPass(state); continue; }
        const auto *node = graph.find(row.id);
        if (row.id < 0 || !node || !node->known) {
            result.issues |= MissingDefinition; result.missingRows |= UINT64_C(1) << index; continue;
        }
        const uint16_t op = node->record.opcode();
        const auto control = EA::DescribeControl(op, row.b);
        bool branch = false;
        switch (op) {
            case 0x15:
                if (!(state.flags & row.a)) { state.flags |= row.a; emit(state, index, Origin::Control); }
                else advance(state);
                continue;
            case 0x16: state.flags = row.a; advance(state); continue;
            case 0x17: case 0x2b: branch = (state.flags & row.a) != 0; break;
            case 0x18: state.flags &= ~uint32_t(row.a); advance(state); continue;
            case 0x19: state.counter = row.a; advance(state); continue;
            case 0x1a:
                if (state.counter <= 0x7ffffffeu) ++state.counter;
                advance(state); continue;
            case 0x1b: if (state.counter) --state.counter; advance(state); continue;
            case 0x1c: case 0x1d: {
                State taken = state, skipped = state;
                taken.hp = op == 0x1c ? AtMost(state.hp, row.a, true) : AtLeast(state.hp, row.a, true);
                skipped.hp = op == 0x1c ? AtLeast(state.hp, row.a, false) : AtMost(state.hp, row.a, false);
                if (taken.hp.valid()) jump(taken, control.target, index);
                if (skipped.hp.valid()) advance(skipped);
                continue;
            }
            case 0x1e: branch = state.counter <= row.a; break;
            case 0x1f: branch = state.counter == row.a; break;
            case 0x20: branch = state.counter >= row.a; break;
            case 0x21: branch = input.level <= row.a; break;
            case 0x22: branch = input.level == row.a; break;
            case 0x23: branch = input.level >= row.a; break;
            case 0x24: finishPass(state); continue;
            case 0x25:
                if (state.counter && --state.counter) emit(state, index, Origin::Control);
                else advance(state);
                continue;
            case 0x26:
                if (state.counter == row.a) state.counter = row.b;
                advance(state); continue;
            case 0x2c: state.flags |= row.a; advance(state); continue;
            case 0x2d: state.flags ^= row.a; advance(state); continue;
            case 0x5b:
                if (!(state.flags & row.a)) { state.flags |= row.a; advance(state); continue; }
                branch = true; break;
            default:
                if (!SupportedLeaf(op)) {
                    result.issues |= UnsupportedOpcode; result.unsupportedRows |= UINT64_C(1) << index; continue;
                }
                // Cross-enemy suppression and feasibility remain may/skip.
                // They only reject these reviewed leaves and do not mutate
                // AI flags/counter. Unreviewed opcodes do not use this rule.
                if (node->record.word(64) > int(input.budget) || op == 49 ||
                    (input.mode == Mode::OrderedWeighted && op == 47) ||
                    (input.mode == Mode::LegacyTwoPass && state.pass && op == 47)) {
                    advance(state); continue;
                }
                if (input.mode == Mode::LegacyTwoPass && state.pass) {
                    if (row.b) emit(state, index, Origin::Secondary);
                    advance(state); continue;
                }
                if (input.mode == Mode::LegacyTwoPass && op != 47 && !row.a) { advance(state); continue; }
                if (op != 47) {
                    State above = state;
                    above.hp = AtLeast(state.hp, node->record.word(56), false);
                    if (above.hp.valid()) advance(above);
                    state.hp = AtMost(state.hp, node->record.word(56), true);
                    if (!state.hp.valid()) continue;
                }
                if (input.mode == Mode::OrderedWeighted && row.b) {
                    // Native pool has 64 byte-sized entries; loops can add the
                    // same row repeatedly. Membership suffices for possible
                    // IDs, but multiplicity must still count toward capacity.
                    if (state.weightedCount == EA::Rows) { result.issues |= WeightedPoolLimit; continue; }
                    ++state.weightedCount; state.weighted |= UINT64_C(1) << index;
                }
                // Legacy 47 bypasses the explicit A gate. Keep it even for
                // A=0; exact native floating chance arithmetic is unmodeled.
                if (row.a || op == 47) emit(state, index, Origin::Primary);
                advance(state); continue;
        }
        if (branch) jump(state, control.target, index);
        else advance(state);
    }
    result.states = unsigned(states.size()); result.steps = budget.steps - started;
    return result;
}

struct SequenceStep {
    int32_t id = 0, parameter0 = 0, attachedPercent = 0;
    uint16_t opcode = 0;
    bool definitionKnown = false, kindKnown = false, skipped = false;
};
struct Sequence {
    std::array<SequenceStep, 8> steps{};
    unsigned count = 0;
    bool compound = false, structureKnown = false, fallbackRetained = false;
};
inline Sequence DescribeSequence(const EA::Graph &graph, int32_t id) {
    Sequence sequence;
    const auto *root = graph.find(id);
    if (!root || !root->known) return sequence;
    sequence.compound = root->record.opcode() == 83;
    sequence.structureKnown = true;
    auto append = [&](int32_t action) {
        auto &step = sequence.steps[sequence.count++]; step.id = action;
        const auto *node = graph.find(action);
        if (!node || !node->known) { sequence.structureKnown = false; return; }
        step.definitionKnown = true; step.opcode = node->record.opcode();
        step.parameter0 = node->record.parameter(0); step.attachedPercent = node->record.word(68);
        step.skipped = sequence.compound && step.opcode == 82;
        step.kindKnown = step.opcode == 88 || step.opcode == 141 || step.opcode == 82;
        sequence.structureKnown &= step.kindKnown;
        if (!sequence.compound && step.opcode == 82) sequence.fallbackRetained = true;
    };
    if (!sequence.compound) append(id);
    else {
        bool terminated = false;
        for (unsigned i = 0; i < 8; ++i) {
            const int32_t child = root->record.parameter(i);
            if (child <= 0) { terminated = true; break; }
            append(child); // Preserve duplicates. Never flatten nested 83.
        }
        sequence.structureKnown &= terminated;
    }
    return sequence;
}
} // namespace PHEnemySelector
