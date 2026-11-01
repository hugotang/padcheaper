// Source-only; exercise the definition parser without game memory or a selector.
static void EnemyActionModelTests() {
    using namespace PHEnemyActions;
    struct Records { Record values[256]{}; unsigned reads = 0; int fail = -1; } records;
    auto set = [&](unsigned id, uint16_t opcode, std::initializer_list<int32_t> params) {
        auto &bytes = records.values[id].bytes;
        bytes.fill(0); std::memcpy(bytes.data() + 4, &opcode, sizeof(opcode));
        unsigned i = 0;
        for (int32_t value : params) { std::memcpy(bytes.data() + 16 + i * 4, &value, sizeof(value)); ++i; }
    };
    auto reader = +[](void *context, uint32_t id, Record &out) {
        auto &fixture = *static_cast<Records *>(context); ++fixture.reads;
        if (id >= 256 || int(id) == fixture.fail) return false;
        out = fixture.values[id]; return true;
    };
    set(1, 83, {2, 2, 3, 0, 4}); set(2, 88, {4}); set(3, 141, {5}); set(4, 88, {9});
    Graph graph{}; Add(graph, 1, 256, reader, &records);
    Check(graph.count == 3 && records.reads == 3 && graph.find(1)->record.parameter(1) == 2 &&
        graph.find(1)->record.parameter(4) == 4 && !graph.find(4) && !graph.issues,
        "duplicate compound children retain their order, share a definition and stop at zero while preserving the raw suffix");
    Add(graph, 2, 256, reader, &records);
    Check(records.reads == 3, "a record referenced by both a row and a compound is read once per capture");
    set(6, 83, {2,2,2,2,2,2,2,2}); Graph unterminated{}; Add(unterminated, 6, 256, reader, &records);
    Check((unterminated.issues & UnterminatedCompound) && (unterminated.issues & CaptureIssues),
        "eight positive compound parameters are not assumed to terminate before native weight fields");
    set(4, 65535, {-7, INT32_MAX}); Add(graph, 4, 256, reader, &records);
    Check(graph.find(4)->known && graph.find(4)->record.parameter(0) == -7 &&
        (graph.issues & UnknownOpcode) && !(graph.issues & CaptureIssues),
        "unknown opcodes and signed raw parameters remain captured rather than becoming a harmless action");
    const unsigned before = records.reads;
    Add(graph, -1, 256, reader, &records); Add(graph, 256, 256, reader, &records);
    Check(records.reads == before && (graph.issues & InvalidID), "negative and count-bound IDs cannot invoke the memory reader");
    records.fail = 5; Add(graph, 5, 256, reader, &records);
    Check(graph.find(5) && !graph.find(5)->known && (graph.issues & UnreadableRecord), "unreadable children stay explicit incomplete nodes");
    records.fail = -1;
    set(1, 83, {2}); set(2, 83, {1}); graph = {}; records.reads = 0;
    Add(graph, 1, 256, reader, &records);
    Check(graph.count == 2 && records.reads == 2 && (graph.issues & Cycle) && (graph.issues & NestedCompound),
        "a compound cycle terminates and nested execution remains unsupported");
    graph = {}; records.reads = 0;
    for (unsigned i = 1; i < 14; ++i) set(i, 83, {int32_t(i + 1)});
    Add(graph, 1, 256, reader, &records);
    Check((graph.issues & DepthLimit) && records.reads == MaxDepth + 1,
        "deep compounds stop before another read once the shared depth bound is reached");
    graph = {}; records.reads = 0;
    for (unsigned i = 1; i < 240; ++i) { set(i, 88, {1}); Add(graph, int32_t(i), 256, reader, &records); }
    Check(graph.count == MaxRecords && records.reads == MaxRecords && (graph.issues & RecordLimit),
        "record budget covers all roots in the capture instead of restarting for each enemy");
    Check(DescribeControl(0x17, 5).target == 5 && DescribeControl(0x5b, 5).target == 5 &&
        DescribeControl(0x1c, 5).target == 4 && DescribeControl(0x2b, 5).target == 4 &&
        DescribeControl(0x1c, 0).target == -1 && DescribeControl(0x17, 64).target == 64,
        "flag and HP/counter jumps retain distinct native target conventions including invalid endpoints");
    Check(!std::strcmp(Kind(1), "unknown") && !std::strcmp(Kind(82), "unknown") &&
        !std::strcmp(Kind(88), "awoken-bind") && !std::strcmp(Kind(141), "assist-void"),
        "bind/fallback opcodes are never guessed to be ordinary attacks");
}
