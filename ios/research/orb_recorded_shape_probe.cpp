// Recorded 0.5.9 stopped boards. Reuses only the independent runs/union/gravity
// oracle, and independently swaps moving thorn tokens and all spinner phases.
// Never linked into the dylib. Input: turn width height spinnerMask colors
// hpKnown hp maximumHP pendingDamage priority, then cell colors and thorn %.
// --policy-input adds poison-policy/diagonal fields after priority and a
// full flags and blind-turn arrays after thorn %. Only basic-color boards are accepted.
#define main PreviousProbeMain
#include "orb_shape_probe.cpp"
#undef main
#include <cmath>
#include <iostream>

// The same probe can be built against the baseline header, which predates
// these counters. It reports null instead of inventing baseline measurements.
template<typename Route> static auto PrintEvaluationCounters(const Route &route, int)
    -> decltype((void)route.evaluationCalls, (void)route.evaluationCacheHits) {
    std::printf(",\"evaluationCalls\":%u,\"evaluationCacheHits\":%u", route.evaluationCalls, route.evaluationCacheHits);
}
template<typename Route> static void PrintEvaluationCounters(const Route &, long) {
    std::printf(",\"evaluationCalls\":null,\"evaluationCacheHits\":null");
}
template<typename Status> static auto Incompatible(Status value, int) -> decltype((void)Status::IncompatibleGoals, bool()) {
    return value == Status::IncompatibleGoals;
}
template<typename Status> static bool Incompatible(Status, long) { return false; }
template<typename Options> static auto SetProbePolicy(Options &options, bool allow, bool diagonal, int)
    -> decltype((void)options.allowPoisonWithHeartFour, (void)options.diagonal, bool()) {
    options.allowPoisonWithHeartFour = allow; options.diagonal = diagonal; return true;
}
template<typename Options> static bool SetProbePolicy(Options &, bool allow, bool diagonal, long) {
    return !allow && !diagonal;
}

int main(int argc, char **argv) {
    const bool policyInput = argc == 2 && std::strcmp(argv[1], "--policy-input") == 0;
    if (argc > 1 && !policyInput) return 2;
    unsigned turn, width, height, known, hp, maximum, pending, priority, colors;
    uint64_t mask;
    while (std::cin >> turn >> width >> height >> mask >> colors >> known >> hp >> maximum >> pending >> priority) {
        if (!((width == 6 && height == 5) || (width == 7 && height == 6) || (width == 5 && height == 4)) ||
            known > 1 || priority > 1 || !colors || colors > 63 || mask >> (width * height)) return 2;
        unsigned allow = 0, diagonal = 0;
        if (policyInput && (!(std::cin >> allow >> diagonal) || allow > 1 || diagonal > 1)) return 2;
        PHOrbBoard board{}; board.width = uint8_t(width); board.height = uint8_t(height);
        board.spinnerMask = mask; board.spinnerColors = uint16_t(colors);
        for (unsigned i = 0, value; i < width * height; ++i) {
            if (!(std::cin >> value) || value > 5) return 2;
            board.types[i] = uint8_t(value); board.tokens[i] = i + 1;
            board.x[i] = int16_t(50 + i % width * 90); board.y[i] = int16_t(300 + i / width * 90);
        }
        for (unsigned i = 0, value; i < width * height; ++i) {
            if (!(std::cin >> value) || value > 127) return 2;
            board.thornDamage[i] = uint8_t(value); board.flags[i] = value ? PHOrbFlagThorn : 0;
        }
        if (policyInput) for (unsigned i = 0; i < width * height; ++i)
            if (!(std::cin >> board.flags[i])) return 2;
        if (policyInput) for (unsigned i = 0, value; i < width * height; ++i) {
            if (!(std::cin >> value) || value > 99) return 2;
            board.blindTurns[i] = uint8_t(value);
        }
        auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
        options.goals.count = 2;
        options.goals.items[0] = {PHOrbGoalT,8,1,0}; options.goals.items[1] = {PHOrbGoalFour,32,1,0};
        options.thorns = {hp, maximum, pending, bool(known)}; options.thornComboPriority = bool(priority);
        if (!SetProbePolicy(options, bool(allow), bool(diagonal), 0)) return 2;
        const auto original = board;
        std::atomic_bool cancel{false}; PHOrbRoute route{};
        const auto began = std::chrono::steady_clock::now();
        const auto status = PHOrbSolve(board, options, cancel, route);
        const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        bool valid = !std::memcmp(&board, &original, sizeof(board)) && route.count <= 49;
        std::vector<int> cells(board.types, board.types + width * height);
        std::vector<unsigned> thorns(board.thornDamage, board.thornDamage + width * height);
        uint64_t damage = 0; unsigned contacts = 0;
        for (unsigned i = 0; i < route.count; ++i)
            valid &= route.cells[i] < width * height && !(mask & (UINT64_C(1) << route.cells[i]));
        for (unsigned i = 1; i < route.count && valid; ++i) {
            const unsigned a = route.cells[i - 1], b = route.cells[i];
            const unsigned dx = a % width > b % width ? a % width - b % width : b % width - a % width;
            const unsigned dy = a / width > b / width ? a / width - b / width : b / width - a / width;
            valid &= diagonal ? (dx <= 1 && dy <= 1 && dx + dy > 0) : (dx + dy == 1);
            if (!valid) break;
            if (thorns[b]) {
                ++contacts;
                damage += std::max(1.0, std::floor(double(float(thorns[b]) / 100.0f) * maximum + 0.5));
            }
            std::swap(cells[a], cells[b]); std::swap(thorns[a], thorns[b]);
        }
        unsigned minT = 0, minFour = 0, minCombo = 0, minErased = 0;
        bool oracleSafe = true;
        if (route.count && valid) {
            valid &= route.count >= 2 && route.goalCount == 2;
            std::vector<unsigned> cycle;
            for (unsigned c = 0; c < 6; ++c) if (colors & (1u << c)) cycle.push_back(c);
            const unsigned phases = mask ? unsigned(cycle.size()) : 1;
            minT = minFour = minCombo = minErased = 42;
            for (unsigned phase = 0; phase < phases; ++phase) {
                auto variant = cells;
                for (unsigned i = 0; i < width * height; ++i) if (mask & (UINT64_C(1) << i)) {
                    auto at = std::find(cycle.begin(), cycle.end(), unsigned(board.types[i]));
                    if (at == cycle.end()) { valid = false; break; }
                    variant[i] = int(cycle[(unsigned(at - cycle.begin()) + phase) % phases]);
                }
                const auto oracle = Oracle(variant, width, height, true);
                minT = std::min(minT, oracle.lightT); minFour = std::min(minFour, oracle.heart);
                minCombo = std::min(minCombo, oracle.combos); minErased = std::min(minErased, oracle.erased);
            }
            oracleSafe = !contacts || (known && hp > 0 && hp <= maximum && maximum <= INT32_MAX &&
                uint64_t(pending) + damage < hp && minFour > 0);
            valid &= route.combos == minCombo && route.erased == minErased && route.heartFours == minFour &&
                route.goalHits[0] == minT && route.goalHits[1] == minFour && route.goalsMet == (minT > 0 && minFour > 0) &&
                route.matchedGoals == unsigned(minT > 0) + unsigned(minFour > 0) && route.hazardsSafe == oracleSafe &&
                !route.poisonErased && !route.unmatchedBombs && route.thornContacts == contacts && route.thornDamage == damage &&
                status == (!oracleSafe ? PHOrbSolveStatus::Unsafe : route.goalsMet ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet);
        } else if (!route.count) valid &= status == PHOrbSolveStatus::InsufficientOrbs || status == PHOrbSolveStatus::NoRoute || Incompatible(status, 0);
        const char *name = status == PHOrbSolveStatus::Solved ? "solved" : status == PHOrbSolveStatus::GoalsUnmet ? "goals_unmet" :
            status == PHOrbSolveStatus::Unsafe ? "unsafe" : status == PHOrbSolveStatus::InsufficientOrbs ? "insufficient" :
            status == PHOrbSolveStatus::NoRoute ? "no_route" : Incompatible(status, 0) ? "incompatible_goals" : "unexpected";
        std::printf("{\"turn\":%u,\"hpKnown\":%s,\"priority\":%s,\"status\":\"%s\",\"goals_met\":%s,\"hazards_safe\":%s,"
            "\"goal_hits\":[%u,%u],\"oracle\":{\"T\":%u,\"four\":%u,\"combos\":%u,\"erased\":%u,\"safe\":%s},"
            "\"steps\":%u,\"combos\":%u,\"elapsedMS\":%u,\"wall_ms\":%.3f,\"evaluated\":%u,\"deadline\":%s,\"passes\":%u,"
            "\"backtracks\":%u,\"depth\":%u,\"thornContacts\":%u,\"thornDamage\":%llu,\"valid\":%s,\"path\":[",
            turn, known ? "true" : "false", priority ? "true" : "false", name, route.goalsMet ? "true" : "false",
            route.hazardsSafe ? "true" : "false", route.goalHits[0], route.goalHits[1], minT, minFour, minCombo, minErased,
            oracleSafe ? "true" : "false", route.count ? route.count - 1 : 0, route.combos, route.elapsedMS, wall, route.evaluated,
            route.deadlineReached ? "true" : "false", route.searchPasses, route.backtracks, route.exploredDepth, contacts,
            (unsigned long long)damage, valid ? "true" : "false");
        for (unsigned i = 0; i < route.count; ++i) std::printf("%s%u", i ? "," : "", route.cells[i]);
        std::printf("]"); PrintEvaluationCounters(route, 0); std::puts("}");
        if (!valid) return 1;
    }
}
