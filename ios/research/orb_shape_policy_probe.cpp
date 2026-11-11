// Local-only shape comparison. Maximum allowed time budget, not an unlimited
// search: always report deadlines. Inventory sufficiency does not prove a route.
// Args: boards-per-scenario (default 96), optional exact scenario name.
// Build against a complete source revision via its include directory.
#include "PHOrbSolver.cpp"
#include <cstdio>
#include <random>
#include <cstring>
static PHOrbBoard RandomBoard(unsigned width, unsigned height, unsigned seed) {
    PHOrbBoard board{}; board.width = uint8_t(width); board.height = uint8_t(height);
    std::mt19937 random(seed);
    for (unsigned i = 0; i < width * height; ++i) {
        board.types[i] = uint8_t(random() % 6); board.tokens[i] = i + 1;
        board.x[i] = int16_t(50 + i % width * 100); board.y[i] = int16_t(300 + i / width * 100);
    }
    return board;
}
struct Scenario { const char *name; unsigned w, h; PHOrbGoalSet goals; bool spinner; };
static PHOrbGoalSet G(std::initializer_list<PHOrbGoal> items) {
    PHOrbGoalSet goals{}; for (const auto &item : items) goals.items[goals.count++] = item; return goals;
}
int main(int argc, char **argv) {
    const unsigned boards = argc > 1 ? unsigned(std::atoi(argv[1])) : 96;
    const uint8_t light = 1u << 3, heart = 1u << 5;
    const Scenario scenarios[] = {
        {"combo only", 6, 5, G({}), false},
        {"six colours", 6, 5, G({{PHOrbGoalColors, 63, 6, 0}}), false},
        {"T", 6, 5, G({{PHOrbGoalT, light, 1, 0}}), false},
        {"L", 6, 5, G({{PHOrbGoalL, light, 1, 0}}), false},
        {"cross", 6, 5, G({{PHOrbGoalCross, light, 1, 0}}), false},
        {"square 3x3", 6, 5, G({{PHOrbGoalSquare, light, 1, 0}}), false},
        {"row", 6, 5, G({{PHOrbGoalRow, light, 1, 0}}), false},
        {"column", 6, 5, G({{PHOrbGoalColumn, light, 1, 0}}), false},
        {"four", 6, 5, G({{PHOrbGoalFour, light, 1, 0}}), false},
        {"two fours", 6, 5, G({{PHOrbGoalFour, light, 2, 0}}), false},
        {"exact five", 6, 5, G({{PHOrbGoalExact, light, 1, 5}}), false},
        {"match>=5", 6, 5, G({{PHOrbGoalMatch, light, 1, 5}}), false},
        {"T + heart four", 6, 5, G({{PHOrbGoalT, light, 1, 0}, {PHOrbGoalFour, heart, 1, 0}}), false},
        {"L + heart four", 6, 5, G({{PHOrbGoalL, light, 1, 0}, {PHOrbGoalFour, heart, 1, 0}}), false},
        {"cross + heart four", 6, 5, G({{PHOrbGoalCross, light, 1, 0}, {PHOrbGoalFour, heart, 1, 0}}), false},
        {"square + heart four", 6, 5, G({{PHOrbGoalSquare, light, 1, 0}, {PHOrbGoalFour, heart, 1, 0}}), false},
        {"L + heart column", 6, 5, G({{PHOrbGoalL, light, 1, 0}, {PHOrbGoalColumn, heart, 1, 0}}), false},
        {"L, spinner", 6, 5, G({{PHOrbGoalL, light, 1, 0}}), true},
        {"cross, spinner", 6, 5, G({{PHOrbGoalCross, light, 1, 0}}), true},
        {"T + heart four, spinner", 6, 5, G({{PHOrbGoalT, light, 1, 0}, {PHOrbGoalFour, heart, 1, 0}}), true},
        {"L + heart four, spinner", 6, 5, G({{PHOrbGoalL, light, 1, 0}, {PHOrbGoalFour, heart, 1, 0}}), true},
        {"six colours, spinner", 6, 5, G({{PHOrbGoalColors, 63, 6, 0}}), true},
        {"7x6 cross + heart four", 7, 6, G({{PHOrbGoalCross, light, 1, 0}, {PHOrbGoalFour, heart, 1, 0}}), false},
        {"7x6 square", 7, 6, G({{PHOrbGoalSquare, light, 1, 0}}), false},
    };
    unsigned invalid = 0;
    for (const auto &s : scenarios) {
        if (argc > 2 && std::strcmp(argv[2],s.name)) continue;
        unsigned used = 0, solved = 0, timedOut = 0, seed = 1;
        uint64_t combos = 0, erased = 0, steps = 0, ms = 0;
        for (; used < boards && seed < 60000; ++seed) {
            auto board = RandomBoard(s.w, s.h, seed);
            if (s.spinner) { board.spinnerMask = UINT64_C(1) << (s.w + 2); board.spinnerColors = 63; }
            auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
            options.goals = s.goals; options.milliseconds = PHOrbMaxSearchMilliseconds;
            if (PHOrbInspectInventory(board, options).kind != PHOrbInventoryKind::None) continue;
            ++used;
            std::atomic_bool cancel{false}; PHOrbRoute route{};
            const auto status = PHOrbSolve(board, options, cancel, route);
            if (route.count) {
                PHOrbEvaluation checked{}; uint32_t damage = 0; uint8_t contacts = 0;
                if (!PHOrbEvaluateRoute(board,route,options,checked,damage,contacts) ||
                    checked.goalsMet != route.goalsMet || checked.hazardsSafe != route.hazardsSafe ||
                    checked.combos != route.combos || checked.erased != route.erased) ++invalid;
            }
            const bool ok = status == PHOrbSolveStatus::Solved;
            solved += ok; timedOut += route.deadlineReached; ms += route.elapsedMS;
            // Route quality is compared on solved boards only.
            if (ok) { combos += route.combos; erased += route.erased; steps += route.count - 1; }
        }
        const double n = solved ? double(solved) : 1.0;
        std::printf("%-24s|%3u|%3u|%5.2f|%5.2f|%5.1f|%4.0f|%u\n", s.name, used, solved, combos / n, erased / n, steps / n,
            double(ms) / (used ? used : 1), timedOut);
        std::fflush(stdout);
    }
    return invalid ? 1 : 0;
}
