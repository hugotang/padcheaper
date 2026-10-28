// Bounded iOS beam-search adaptation of the approach in pazusoba/core.
// See ios/third_party/pazusoba/LICENSE and NOTICE.md for provenance.
#include "PHOrbSolver.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <unordered_set>
#include <vector>

namespace {
using Cells = std::array<uint8_t, PHOrbMaxCells>;
using Clock = std::chrono::steady_clock;
constexpr uint8_t Empty = 255;
struct Score { unsigned combos = 0, erased = 0, potential = 0; };
struct Node {
    Cells cells{};
    std::array<uint8_t, PHOrbMaxSteps + 1> path{};
    uint8_t length = 1;
    Score score;
};

// All matches in one wave are marked before removal. Connected matches of the
// same color count once; only existing orbs fall, with no random skyfall.
Score Evaluate(Cells cells, unsigned width, unsigned height) {
    Score result;
    unsigned size = width * height;
    for (unsigned i = 0; i < size; ++i) {
        if (i % width + 1 < width && cells[i] == cells[i + 1]) ++result.potential;
        if (i + width < size && cells[i] == cells[i + width]) ++result.potential;
        if (i % width + 2 < width && cells[i] == cells[i + 2]) ++result.potential;
        if (i + 2 * width < size && cells[i] == cells[i + 2 * width]) ++result.potential;
    }
    for (unsigned wave = 0; wave < size / 3; ++wave) {
        std::array<bool, PHOrbMaxCells> marked{};
        bool any = false;
        for (unsigned i = 0; i < size; ++i) {
            if (cells[i] == Empty) continue;
            if (i % width + 2 < width && cells[i] == cells[i + 1] && cells[i] == cells[i + 2])
                marked[i] = marked[i + 1] = marked[i + 2] = any = true;
            if (i + 2 * width < size && cells[i] == cells[i + width] && cells[i] == cells[i + 2 * width])
                marked[i] = marked[i + width] = marked[i + 2 * width] = any = true;
        }
        if (!any) break;
        std::array<bool, PHOrbMaxCells> seen{};
        for (unsigned start = 0; start < size; ++start) {
            if (!marked[start] || seen[start]) continue;
            ++result.combos;
            std::array<unsigned, PHOrbMaxCells> queue{};
            unsigned head = 0, tail = 1;
            queue[0] = start; seen[start] = true;
            while (head < tail) {
                unsigned index = queue[head++];
                ++result.erased;
                const int neighbors[] = {int(index) - int(width), int(index) + int(width), int(index) - 1, int(index) + 1};
                for (int next : neighbors) {
                    if (next < 0 || unsigned(next) >= size || !PHOrbAdjacent(index, unsigned(next), width, height) ||
                        !marked[next] || seen[next] || cells[next] != cells[start]) continue;
                    seen[next] = true; queue[tail++] = unsigned(next);
                }
            }
        }
        for (unsigned i = 0; i < size; ++i) if (marked[i]) cells[i] = Empty;
        for (unsigned column = 0; column < width; ++column) {
            int target = int(height) - 1;
            for (int row = int(height) - 1; row >= 0; --row) {
                uint8_t value = cells[unsigned(row) * width + column];
                if (value != Empty) cells[unsigned(target--) * width + column] = value;
            }
            while (target >= 0) cells[unsigned(target--) * width + column] = Empty;
        }
    }
    return result;
}

bool Better(const Node &a, const Node &b) {
    if (a.score.combos != b.score.combos) return a.score.combos > b.score.combos;
    if (a.score.erased != b.score.erased) return a.score.erased > b.score.erased;
    if (a.length != b.length) return a.length < b.length;
    if (a.score.potential != b.score.potential) return a.score.potential > b.score.potential;
    return std::lexicographical_compare(a.path.begin(), a.path.begin() + a.length, b.path.begin(), b.path.begin() + b.length);
}

uint64_t Hash(const Node &node, unsigned size) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned i = 0; i < size; ++i) { hash ^= node.cells[i]; hash *= 1099511628211ULL; }
    hash ^= node.path[node.length - 1]; hash *= 1099511628211ULL;
    // The forbidden immediate reverse depends on the preceding position.
    hash ^= node.length > 1 ? node.path[node.length - 2] : 255;
    return hash * 1099511628211ULL;
}
}

PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept {
    route = {};
    if (!PHOrbBoardValid(&board) || !options.steps || options.steps > PHOrbMaxSteps ||
        !options.beam || options.beam > 512 || !options.milliseconds || options.milliseconds > 1000) return PHOrbSolveStatus::Invalid;
    if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
    try {
        const auto start = Clock::now(), deadline = start + std::chrono::milliseconds(options.milliseconds);
        const unsigned size = board.width * board.height;
        Cells initial{};
        std::copy(board.types, board.types + size, initial.begin());
        std::vector<Node> beam, next;
        beam.reserve(std::max(size, options.beam)); next.reserve(options.beam * 4);
        for (unsigned i = 0; i < size; ++i) {
            Node node; node.cells = initial; node.path[0] = uint8_t(i); beam.push_back(node);
        }
        Node best;
        bool found = false, timedOut = false;
        unsigned evaluated = 0;
        std::unordered_set<uint64_t> visited;
        visited.reserve(options.beam * 4);
        for (unsigned depth = 0; depth < options.steps && !timedOut; ++depth) {
            next.clear(); visited.clear();
            for (const Node &parent : beam) {
                unsigned current = parent.path[parent.length - 1];
                const int neighbors[] = {int(current) - int(board.width), int(current) + int(board.width), int(current) - 1, int(current) + 1};
                for (int cell : neighbors) {
                    if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
                    if ((evaluated & 31u) == 0 && Clock::now() >= deadline) { timedOut = true; break; }
                    if (cell < 0 || !PHOrbAdjacent(current, unsigned(cell), board.width, board.height) ||
                        (parent.length > 1 && unsigned(cell) == parent.path[parent.length - 2])) continue;
                    Node child = parent;
                    child.path[child.length++] = uint8_t(cell);
                    std::swap(child.cells[current], child.cells[cell]);
                    if (!visited.insert(Hash(child, size)).second) continue;
                    child.score = Evaluate(child.cells, board.width, board.height);
                    ++evaluated;
                    if (!found || Better(child, best)) { best = child; found = true; }
                    next.push_back(child);
                }
                if (timedOut) break;
            }
            if (next.empty()) break;
            const size_t keep = std::min(size_t(options.beam), next.size());
            std::partial_sort(next.begin(), next.begin() + keep, next.end(), Better);
            next.resize(keep); beam.swap(next);
        }
        if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
        if (!found) return PHOrbSolveStatus::NoRoute;
        route.count = best.length;
        std::copy(best.path.begin(), best.path.begin() + best.length, route.cells);
        route.combos = uint8_t(best.score.combos); route.erased = uint8_t(best.score.erased);
        route.evaluated = evaluated; route.deadlineReached = timedOut;
        route.elapsedMS = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
        PHOrbBoard replay{};
        if (!PHOrbReplay(&board, &route, route.count - 1, &replay) || std::memcmp(replay.types, best.cells.data(), size)) {
            route = {}; return PHOrbSolveStatus::Failed;
        }
        return PHOrbSolveStatus::Solved;
    } catch (...) { route = {}; return PHOrbSolveStatus::Failed; }
}
