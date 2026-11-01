#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Definitions only. This parser neither runs the selector nor predicts which
// actions are reachable after a player's attack. In particular, a captured
// selected ID and an empty row list are not next-turn forecasts.
namespace PHEnemyActions {
constexpr unsigned ParserVersion = 1, MaxEnemies = 8, Rows = 64;
constexpr unsigned MaxRecords = 192, MaxDepth = 8, RecordBytes = 72;
constexpr unsigned MaxReadBytes = 128 * 1024, MaxReads = 256;

struct Record {
    std::array<uint8_t, RecordBytes> bytes{};
    uint16_t opcode() const { uint16_t v; std::memcpy(&v, bytes.data() + 4, sizeof(v)); return v; }
    int32_t word(unsigned offset) const {
        int32_t v = 0;
        if (offset <= RecordBytes - sizeof(v)) std::memcpy(&v, bytes.data() + offset, sizeof(v));
        return v;
    }
    int32_t parameter(unsigned index) const { return index < 8 ? word(16 + index * 4) : 0; }
};

enum Issue : uint32_t {
    InvalidID = 1u << 0, UnreadableRecord = 1u << 1, RecordLimit = 1u << 2,
    DepthLimit = 1u << 3, Cycle = 1u << 4, NestedCompound = 1u << 5,
    UnknownOpcode = 1u << 6, UnterminatedCompound = 1u << 7
};
constexpr uint32_t CaptureIssues = InvalidID | UnreadableRecord | RecordLimit | DepthLimit | UnterminatedCompound;

// Jump targets are zero-based row indices, after the caller's increment.
// 0x17/0x5b use operand B, while the other listed jumps use B-1. Preserve
// out-of-range targets as unknown; do not clamp them into a valid row.
struct Control {
    const char *name = nullptr;
    int target = -1;
    bool jumps = false;
};
inline Control DescribeControl(uint16_t opcode, uint8_t operandB) {
    switch (opcode) {
        case 0x15: return {"once-flags-emit-row", -1, false};
        case 0x16: return {"set-flags", -1, false};
        case 0x17: return {"jump-if-flags-set", int(operandB), true};
        case 0x18: return {"clear-flags", -1, false};
        case 0x19: return {"set-counter", -1, false};
        case 0x1a: return {"increment-counter", -1, false};
        case 0x1b: return {"decrement-counter", -1, false};
        case 0x1c: return {"jump-if-enemy-hp-at-most", int(operandB) - 1, true};
        case 0x1d: return {"jump-if-enemy-hp-at-least", int(operandB) - 1, true};
        case 0x1e: return {"jump-if-counter-at-most", int(operandB) - 1, true};
        case 0x1f: return {"jump-if-counter-equal", int(operandB) - 1, true};
        case 0x20: return {"jump-if-counter-at-least", int(operandB) - 1, true};
        case 0x21: return {"jump-if-enemy-level-at-most", int(operandB) - 1, true};
        case 0x22: return {"jump-if-enemy-level-equal", int(operandB) - 1, true};
        case 0x23: return {"jump-if-enemy-level-at-least", int(operandB) - 1, true};
        case 0x24: return {"end-selector-pass", -1, false};
        case 0x25: return {"decrement-counter-emit-if-nonzero", -1, false};
        case 0x26: return {"replace-counter-if-equal", -1, false};
        case 0x2b: return {"jump-if-flags-set-one-based", int(operandB) - 1, true};
        case 0x2c: return {"or-flags", -1, false};
        case 0x2d: return {"xor-flags", -1, false};
        case 0x5b: return {"set-flags-else-jump", int(operandB), true};
        default: return {};
    }
}

inline const char *Kind(uint16_t opcode) {
    if (DescribeControl(opcode, 0).name) return "control";
    if (opcode == 83) return "compound";
    if (opcode == 88) return "awoken-bind";
    if (opcode == 141) return "assist-void";
    return "unknown";
}

struct Node {
    int32_t id = 0;
    Record record{};
    uint32_t issues = 0;
    bool known = false, visiting = false, expanded = false;
};
struct Graph {
    std::array<Node, MaxRecords> nodes{};
    unsigned count = 0;
    uint32_t issues = 0;
    const Node *find(int32_t id) const {
        for (unsigned i = 0; i < count; ++i) if (nodes[i].id == id) return &nodes[i];
        return nullptr;
    }
};
using Reader = bool (*)(void *, uint32_t, Record &);

inline void Add(Graph &graph, int32_t id, uint32_t tableCount, Reader reader, void *context, unsigned depth = 0) {
    if (id < 0 || uint32_t(id) >= tableCount) { graph.issues |= InvalidID; return; }
    Node *node = nullptr;
    for (unsigned i = 0; i < graph.count; ++i) if (graph.nodes[i].id == id) { node = &graph.nodes[i]; break; }
    if (node && node->visiting) { node->issues |= Cycle; graph.issues |= Cycle; return; }
    if (depth > MaxDepth) { graph.issues |= DepthLimit; return; }
    if (!node) {
        if (graph.count == MaxRecords) { graph.issues |= RecordLimit; return; }
        node = &graph.nodes[graph.count++]; node->id = id;
        node->known = reader && reader(context, uint32_t(id), node->record);
        if (!node->known) { node->issues |= UnreadableRecord; graph.issues |= UnreadableRecord; return; }
        const uint16_t opcode = node->record.opcode();
        if (!std::strcmp(Kind(opcode), "unknown")) { node->issues |= UnknownOpcode; graph.issues |= UnknownOpcode; }
    }
    if (!node->known) return;
    if (node->record.opcode() == 83 && depth) { node->issues |= NestedCompound; graph.issues |= NestedCompound; }
    if (node->expanded) return;
    node->visiting = true;
    if (node->record.opcode() == 83) {
        // Native compound progress stops at the first nonpositive child.
        // Preserve all eight raw parameters, but never turn a suffix into
        // an additional action. Nested compounds are captured, not executed.
        bool terminated = false;
        for (unsigned child = 0; child < 8; ++child) {
            const int32_t next = node->record.parameter(child);
            if (next <= 0) { terminated = true; break; }
            Add(graph, next, tableCount, reader, context, depth + 1);
        }
        // The native index is four bits with no proven eight-child stop.
        // Never assume the following weight fields cannot be read as IDs.
        if (!terminated) { node->issues |= UnterminatedCompound; graph.issues |= UnterminatedCompound; }
    }
    node->visiting = false; node->expanded = true;
}
} // namespace PHEnemyActions
