// Local-only shape probe. Never linked into the product.
// One stdin record: id width height goalCount [kind mask count length]*
// [cell]* fixedStart beam milliseconds diagonal recovery backtracks steps.
#include "PHOrbSolver.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

int main() {
    std::string id;
    std::atomic_bool cancel{false};
    while (std::cin >> id) {
        if (id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) return 2;
        unsigned w, h, count;
        if (!(std::cin >> w >> h >> count) || !PHOrbDimensionsSupported(w,h) || !count || count > PHOrbMaxGoals) return 2;
        PHOrbBoard board{}; board.width = uint8_t(w); board.height = uint8_t(h);
        PHOrbSolveOptions options{}; options.goals.count = uint8_t(count);
        for (unsigned g = 0; g < count; ++g) {
            unsigned kind, colors, amount, length;
            if (!(std::cin >> kind >> colors >> amount >> length) || kind > PHOrbGoalExact || colors > 63 || amount > 42 || length > 42) return 2;
            options.goals.items[g] = {uint8_t(kind),uint8_t(colors),uint8_t(amount),uint8_t(length)};
        }
        for (unsigned i = 0; i < w * h; ++i) {
            unsigned color;
            if (!(std::cin >> color) || color >= PHOrbBasicTypes) return 2;
            board.types[i] = uint8_t(color); board.tokens[i] = i + 1;
        }
        unsigned fixedStart, diagonal;
        if (!(std::cin >> fixedStart >> options.beam >> options.milliseconds >> diagonal >> options.recoveryPasses >>
              options.backtrackLimit >> options.steps) || fixedStart > w*h || diagonal > 1) return 2;
        board.fixedStart = uint8_t(fixedStart); options.diagonal = diagonal != 0;
        if (!PHOrbBoardValid(&board) || !PHOrbGoalsValid(&options.goals)) return 2;
        const auto before = board;
        PHOrbRoute route{};
        const auto start = std::chrono::steady_clock::now();
        const auto status = PHOrbSolve(board,options,cancel,route);
        const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        const bool immutable = std::memcmp(&board, &before, sizeof(board)) == 0;
        std::cout << "{\"id\":\"" << id << "\",\"status\":" << unsigned(status)
            << ",\"goalsMet\":" << (route.goalsMet ? "true" : "false")
            << ",\"hazardsSafe\":" << (route.hazardsSafe ? "true" : "false")
            << ",\"immutable\":" << (immutable ? "true" : "false") << ",\"hits\":[";
        for (unsigned g = 0; g < count; ++g) { if (g) std::cout << ','; std::cout << unsigned(route.goalHits[g]); }
        std::cout << "],\"elapsedMS\":" << route.elapsedMS << ",\"wallUS\":" << wall
            << ",\"deadline\":" << (route.deadlineReached ? "true" : "false")
            << ",\"evaluated\":" << route.evaluated << ",\"evaluationCalls\":" << route.evaluationCalls
            << ",\"cacheHits\":" << route.evaluationCacheHits << ",\"passes\":" << unsigned(route.searchPasses)
            << ",\"backtracks\":" << unsigned(route.backtracks) << ",\"depth\":" << unsigned(route.exploredDepth)
            << ",\"combos\":" << unsigned(route.combos) << ",\"erased\":" << unsigned(route.erased) << ",\"path\":[";
        for (unsigned i = 0; i < route.count; ++i) { if (i) std::cout << ','; std::cout << unsigned(route.cells[i]); }
        std::cout << "]}\n" << std::flush;
    }
    return 0;
}
