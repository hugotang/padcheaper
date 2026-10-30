// Release-layout proof for exactly six fixed light and otherwise fixed heart
// orbs, one avoided six-color spinner, minErase=3 and no connected restriction.
// Any robust T must exist when the spinner is non-light, so enumerate each
// five-cell fixed T and the remaining fixed light; placements without a T are
// excluded by a necessary condition. Thorn/path limits are deliberately ignored.
#define main PreviousProbeMain
#include "orb_shape_probe.cpp"
#undef main
#include <iostream>

int main() {
    unsigned turn, width, height, known, hp, maximum, pending, priority, colors;
    uint64_t spinner;
    if (!(std::cin >> turn >> width >> height >> spinner >> colors >> known >> hp >> maximum >> pending >> priority) ||
        width != 6 || height != 5 || colors != 63 || known > 1 || priority > 1 ||
        !spinner || spinner >> 30 || (spinner & (spinner - 1))) return 2;
    unsigned fixedLights = 0;
    for (unsigned i = 0, value; i < 30; ++i) {
        if (!(std::cin >> value) || value > 5) return 2;
        if (spinner & (UINT64_C(1) << i)) continue;
        if (value != 3 && value != 5) return 2;
        fixedLights += value == 3;
    }
    for (unsigned i = 0, value; i < 30; ++i)
        if (!(std::cin >> value) || value > 127) return 2;
    if (fixedLights != 6) return 2;
    const unsigned shapes[4][5] = {{0,1,2,4,7}, {1,4,6,7,8}, {0,3,4,5,6}, {2,3,4,5,8}};
    std::set<uint64_t> placements;
    for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 4; ++x) for (const auto &shape : shapes) {
        uint64_t target = 0;
        for (unsigned i : shape) target |= UINT64_C(1) << ((y + i / 3) * width + x + i % 3);
        if (target & spinner) continue;
        for (unsigned extra = 0; extra < 30; ++extra)
            if (!((target | spinner) & (UINT64_C(1) << extra))) placements.insert(target | (UINT64_C(1) << extra));
    }
    unsigned robustT = 0, robustBoth = 0;
    uint64_t witness = 0;
    for (uint64_t lights : placements) {
        unsigned minT = 42, minFour = 42;
        for (unsigned color = 0; color < 6; ++color) {
            std::vector<int> cells(30, 5);
            for (unsigned i = 0; i < 30; ++i) {
                if (lights & (UINT64_C(1) << i)) cells[i] = 3;
                if (spinner & (UINT64_C(1) << i)) cells[i] = int(color);
            }
            const auto result = Oracle(cells, width, height, true);
            minT = std::min(minT, result.lightT); minFour = std::min(minFour, result.heart);
        }
        robustT += minT > 0;
        if (minT && minFour) { ++robustBoth; if (!witness) witness = lights; }
    }
    std::printf("{\"turn\":%u,\"fixed_lights\":6,\"fixed_hearts\":23,\"relevant_release_placements\":%zu,"
        "\"all_phase_T_placements\":%u,\"all_phase_T_four_placements\":%u,\"complete\":true,"
        "\"path_and_thorn_limits\":false,\"witness_light_mask\":%llu}\n",
        turn, placements.size(), robustT, robustBoth, (unsigned long long)witness);
}
