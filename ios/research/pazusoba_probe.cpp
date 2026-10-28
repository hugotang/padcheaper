// Standalone review harness, not part of the iOS dylib.
// Build against unmodified pazusoba/core 493dd14746eb97f275704bef73baa0f8edcf61a8.
#include <pazusoba/core.h>
#include <array>
#include <iostream>
#include <thread>
#include <vector>

int main() {
    auto solver = pazusoba::solver();
    solver.set_board("DGRRBLHGBBGGRDDDDLBGHDBLLHDBLD");
    pazusoba::profile profile;
    profile.name = pazusoba::target_combo;
    solver.set_profiles(&profile, 1);

    pazusoba::state current{};
    current.board = solver.board();
    current.begin = current.prev = 1;
    current.curr = 7;
    current.step = 1;
    // A non-initial node is allocated three output slots by adventure().
    // Allocate a fourth sentinel here so the probe itself stays in bounds.
    std::vector<pazusoba::state> output(4);
    output[3].begin = 254;
    solver.expand(current.board, current, output, 0);
    bool outsidePartition = output[3].begin != 254;

    // Both buffers describe the same complete 7x6 board. Extra bytes are
    // allocated deliberately so the probe does not perform an invalid read.
    std::array<unsigned char, 44> a{}, b{};
    for (int i = 0; i < 42; ++i) a[i] = b[i] = (i % 6) + 1;
    b[42] = 5;
    bool hashReadsPastBoard = pazusoba::hash::pazusoba_hash(a.data(), 0) !=
                              pazusoba::hash::pazusoba_hash(b.data(), 0);
    auto workers = std::thread::hardware_concurrency();
    auto covered = workers ? (30 / workers) * workers : 0;
    int firstMax = solver.max_combo();
    solver.set_board("DGRRBLHGBBGGRDDDDLBGHDBLLHDBLD");

    std::cout << "{\"write_past_three_slot_partition\":" << (outsidePartition ? "true" : "false")
              << ",\"hash_depends_on_byte_after_42_orbs\":" << (hashReadsPastBoard ? "true" : "false")
              << ",\"hardware_workers\":" << workers
              << ",\"covered_initial_30_states\":" << covered
              << ",\"max_combo_first_board\":" << firstMax
              << ",\"max_combo_after_setting_same_board_twice\":" << solver.max_combo() << "}\n";
    return outsidePartition && hashReadsPastBoard ? 0 : 1;
}
