#include "PHOrbSolver.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <random>
#include <set>
#include <vector>

static unsigned failures;
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

// Independent oracle: scan complete runs, union marked neighbors, then rebuild
// each column from its survivors. It does not call the production evaluator.
static std::pair<unsigned, unsigned> Oracle(const PHOrbBoard &board) {
    const unsigned w = board.width, h = board.height, n = w * h;
    std::vector<int> cells(board.types, board.types + n);
    unsigned combos = 0, erased = 0;
    for (;;) {
        std::set<unsigned> marked;
        for (unsigned axis = 0; axis < 2; ++axis) {
            unsigned lines = axis ? w : h, length = axis ? h : w;
            for (unsigned line = 0; line < lines; ++line) {
                auto index = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
                for (unsigned start = 0; start < length;) {
                    unsigned end = start + 1;
                    while (end < length && cells[index(start)] == cells[index(end)]) ++end;
                    if (cells[index(start)] >= 0 && end - start >= 3)
                        for (unsigned p = start; p < end; ++p) marked.insert(index(p));
                    start = end;
                }
            }
        }
        if (marked.empty()) return {combos, erased};
        std::vector<unsigned> parent(n); std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](unsigned a) { while (parent[a] != a) a = parent[a]; return a; };
        for (unsigned a : marked) for (unsigned b : marked) {
            if (a < b && cells[a] == cells[b] &&
                ((a / w == b / w && b - a == 1) || b - a == w)) parent[root(b)] = root(a);
        }
        std::set<unsigned> groups;
        for (unsigned i : marked) { groups.insert(root(i)); cells[i] = -1; }
        combos += unsigned(groups.size()); erased += unsigned(marked.size());
        for (unsigned column = 0; column < w; ++column) {
            std::vector<int> survivors;
            for (unsigned row = 0; row < h; ++row) if (cells[row * w + column] >= 0) survivors.push_back(cells[row * w + column]);
            survivors.insert(survivors.begin(), h - survivors.size(), -1);
            for (unsigned row = 0; row < h; ++row) cells[row * w + column] = survivors[row];
        }
    }
}

static PHOrbBoard Board(unsigned width, unsigned height, unsigned seed) {
    PHOrbBoard board{}; board.width = uint8_t(width); board.height = uint8_t(height);
    std::mt19937 random(seed);
    for (unsigned i = 0; i < width * height; ++i) {
        board.types[i] = uint8_t(random() % 6); board.tokens[i] = i + 1;
        board.x[i] = int16_t(50 + i % width * 100); board.y[i] = int16_t(300 + i / width * 100);
    }
    return board;
}

int main() {
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    for (auto dimensions : {std::pair<unsigned, unsigned>{6,5}, {7,6}, {5,4}}) {
        for (unsigned seed = 1; seed <= 8; ++seed) {
            auto board = Board(dimensions.first, dimensions.second, seed);
            const auto original = board;
            Check(PHOrbSolve(board, {12, 64, 1000}, cancel, route) == PHOrbSolveStatus::Solved, "fixed board solves");
            Check(std::memcmp(&board, &original, sizeof(board)) == 0, "solver leaves snapshot immutable");
            PHOrbBoard replay{};
            Check(PHOrbReplay(&board, &route, route.count - 1, &replay), "path contains only adjacent legal cells");
            auto score = Oracle(replay);
            Check(score.first == route.combos && score.second == route.erased, "independent run/union/gravity oracle matches score");
            Check(route.count <= 13 && route.combos <= dimensions.first * dimensions.second / 3, "route and combo bounds");
        }
    }
    auto board = Board(6,5,42);
    Check(!PHOrbAdjacent(5,6,6,5) && !PHOrbAdjacent(0,7,6,5), "row wrap and diagonals rejected");
    PHOrbRoute invalid{}; invalid.count = 2; invalid.cells[0] = 5; invalid.cells[1] = 6;
    PHOrbBoard replay{};
    Check(!PHOrbReplay(&board, &invalid, 1, &replay), "replay rejects a malformed solver route");
    auto same = board;
    std::swap(same.tokens[0], same.tokens[1]);
    Check(!PHOrbSameBoard(&board, &same), "same-color token swaps invalidate stale previews");
    same = board; same.flags[0] = 0x800;
    Check(!PHOrbBoardValid(&same), "unmodeled flags fail closed");
    same = board; same.types[0] = 9;
    Check(PHOrbSolve(same, {}, cancel, route) == PHOrbSolveStatus::Invalid, "unsupported orb rejected without process exit");
    Check(PHOrbSolve(board, {49, 64, 100}, cancel, route) == PHOrbSolveStatus::Invalid, "depth bounded");
    Check(PHOrbSolve(board, {20, 513, 100}, cancel, route) == PHOrbSolveStatus::Invalid, "beam memory bounded");
    cancel = true;
    Check(PHOrbSolve(board, {}, cancel, route) == PHOrbSolveStatus::Cancelled && route.count == 0, "cancellation returns no executable route");
    cancel = false;
    auto before = std::chrono::steady_clock::now();
    auto status = PHOrbSolve(board, {48, 512, 1}, cancel, route);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count();
    Check((status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::NoRoute) && elapsed < 500, "deadline returns promptly");
    if (status == PHOrbSolveStatus::Solved) Check(PHOrbReplay(&board, &route, route.count - 1, &replay), "partial deadline result remains replayable");
    PHOrbRoute first{}, second{};
    PHOrbSolve(board, {4,32,1000}, cancel, first); PHOrbSolve(board, {4,32,1000}, cancel, second);
    Check(first.count == second.count && !std::memcmp(first.cells, second.cells, first.count) && first.combos == second.combos,
          "sequential solves have no retained board/counter state");
    std::printf("Orb solver/model tests: %s (24 seeded boards; independent oracle; bounds, stale tokens, cancellation, deadline)\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
