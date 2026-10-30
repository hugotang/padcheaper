// Recorded thorn-board comparison; never linked into the dylib.
// Reuses the independent run/union/gravity oracle, not the shipping evaluator.
#define main PreviousProbeMain
#include "orb_shape_probe.cpp"
#undef main
#include <cmath>
#include <iostream>

int main() {
    unsigned turn, width, height, known, hp, maximum, pending, priority;
    uint64_t mask; unsigned colors;
    while (std::cin >> turn >> width >> height >> mask >> colors >> known >> hp >> maximum >> pending >> priority) {
        PHOrbBoard board{}; board.width = width; board.height = height;
        board.spinnerMask = mask; board.spinnerColors = colors;
        for (unsigned i = 0, v; i < width * height; ++i) {
            std::cin >> v; board.types[i] = v; board.tokens[i] = i + 1;
            board.x[i] = 50 + i % width * 90; board.y[i] = 300 + i / width * 90;
        }
        for (unsigned i = 0, v; i < width * height; ++i) {
            std::cin >> v; board.thornDamage[i] = v; board.flags[i] = v ? PHOrbFlagThorn : 0;
        }
        auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
        options.goals.count = 2;
        options.goals.items[0] = {PHOrbGoalT,8,1,0}; options.goals.items[1] = {PHOrbGoalFour,32,1,0};
        options.thorns = {hp, maximum, pending, bool(known)};
#ifdef PH_ORB_THORN_PRIORITY
        options.thornComboPriority = priority;
#endif
        const auto original = board;
        std::atomic_bool cancel{false}; PHOrbRoute route{};
        const auto status = PHOrbSolve(board, options, cancel, route);
        bool valid = !std::memcmp(&board,&original,sizeof(board)) && route.count <= 49;
        std::vector<int> cells(board.types,board.types + width * height);
        std::vector<unsigned> thorns(board.thornDamage,board.thornDamage + width * height);
        uint64_t damage = 0; unsigned contacts = 0;
        for (unsigned i = 0; i < route.count; ++i) valid &= route.cells[i] < width * height && !(mask & (UINT64_C(1) << route.cells[i]));
        for (unsigned i = 1; i < route.count && valid; ++i) {
            const unsigned a = route.cells[i-1], b = route.cells[i];
            valid &= (a / width == b / width && (a + 1 == b || b + 1 == a)) || a + width == b || b + width == a;
            if (thorns[b]) {
                ++contacts;
                damage += std::max(1.0, std::floor(double(float(thorns[b]) / 100.0f) * maximum + 0.5));
            }
            std::swap(cells[a],cells[b]); std::swap(thorns[a],thorns[b]);
        }
        unsigned minT = 42, minFour = 42, minCombo = 42, minErased = 42;
        if (route.count && valid) {
            std::vector<unsigned> cycle;
            for (unsigned c = 0; c < 10; ++c) if (colors & (1u << c)) cycle.push_back(c);
            const unsigned phases = mask ? unsigned(cycle.size()) : 1;
            valid &= phases > 0;
            for (unsigned phase = 0; phase < phases; ++phase) {
                auto variant = cells;
                for (unsigned i = 0; i < width * height; ++i) if (mask & (UINT64_C(1) << i)) {
                    auto at = std::find(cycle.begin(),cycle.end(),unsigned(board.types[i]));
                    if (at == cycle.end()) { valid = false; break; }
                    variant[i] = cycle[(unsigned(at - cycle.begin()) + phase) % phases];
                }
                const auto oracle = Oracle(variant,width,height,true);
                minT = std::min(minT,oracle.lightT); minFour = std::min(minFour,oracle.heart);
                minCombo = std::min(minCombo,oracle.combos); minErased = std::min(minErased,oracle.erased);
            }
            valid &= route.combos == minCombo && route.erased == minErased && route.goalHits[0] == minT &&
                route.goalHits[1] == minFour && route.goalsMet == (minT > 0 && minFour > 0) &&
                route.thornContacts == contacts && route.thornDamage == damage &&
                (!contacts || (known && uint64_t(pending) + damage < hp && minFour > 0));
        } else if (!route.count) valid &= status == PHOrbSolveStatus::InsufficientOrbs;
        std::printf("{\"turn\":%u,\"hpKnown\":%s,\"priority\":%s,\"status\":%u,\"steps\":%u,\"combos\":%u,\"elapsedMS\":%u,\"evaluated\":%u,\"deadline\":%s,\"passes\":%u,\"backtracks\":%u,\"depth\":%u,\"thornContacts\":%u,\"thornDamage\":%llu,\"valid\":%s,\"path\":[",
            turn,known?"true":"false",priority?"true":"false",unsigned(status),route.count?route.count-1:0,route.combos,route.elapsedMS,route.evaluated,
            route.deadlineReached?"true":"false",route.searchPasses,route.backtracks,route.exploredDepth,contacts,
            (unsigned long long)damage,valid?"true":"false");
        for (unsigned i = 0; i < route.count; ++i) std::printf("%s%u",i?",":"",route.cells[i]);
        std::puts("]}");
        if (!valid) return 1;
    }
}
