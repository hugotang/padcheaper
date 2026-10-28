// Bounded iOS beam-search adaptation of the approach in pazusoba/core.
// See ios/third_party/pazusoba/LICENSE and NOTICE.md for provenance.
#include "PHOrbSolver.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <iterator>
#include <unordered_set>
#include <vector>

namespace {
using Cells = std::array<uint8_t, PHOrbMaxCells>;
using Bits = uint64_t;
using Clock = std::chrono::steady_clock;
constexpr uint8_t Empty = 255;
unsigned Count(Bits bits) { return unsigned(__builtin_popcountll(bits)); }
struct Pattern { Bits cells, border; };
struct Context {
    unsigned width, height;
    PHOrbGoalSet goals;
    std::array<std::vector<Pattern>, PHOrbMaxGoals> patterns;
};
struct Score { PHOrbEvaluation result{}; unsigned progress = 0, potential = 0; };
struct Node {
    Cells cells{};
    std::array<uint8_t, PHOrbMaxSteps + 1> path{};
    uint8_t length = 1;
    Score score;
};

void AddPattern(std::vector<Pattern> &patterns, Bits cells, unsigned w, unsigned h) {
    Bits border = 0;
    for (unsigned i = 0; i < w * h; ++i) if (cells & (Bits(1) << i)) {
        for (int next : {int(i) - int(w), int(i) + int(w), int(i) - 1, int(i) + 1})
            if (next >= 0 && PHOrbAdjacent(i, unsigned(next), w, h)) border |= Bits(1) << next;
    }
    patterns.push_back({cells, border & ~cells});
}

Context MakeContext(unsigned w, unsigned h, const PHOrbGoalSet &goals) {
    Context context{w, h, goals, {}};
    for (unsigned g = 0; g < goals.count; ++g) {
        auto &out = context.patterns[g];
        const auto kind = goals.items[g].kind;
        if (kind == PHOrbGoalCombo || kind == PHOrbGoalMatch) continue;
        if (kind == PHOrbGoalColors || kind == PHOrbGoalFour || kind == PHOrbGoalRow || kind == PHOrbGoalColumn) {
            for (unsigned axis = 0; axis < 2; ++axis) {
                if ((kind == PHOrbGoalRow && axis) || (kind == PHOrbGoalColumn && !axis)) continue;
                unsigned length = kind == PHOrbGoalColors ? 3 : kind == PHOrbGoalFour ? 4 : axis ? h : w;
                unsigned dx = axis ? 0 : 1, dy = axis ? 1 : 0;
                for (unsigned y = 0; y + dy * (length - 1) < h; ++y)
                    for (unsigned x = 0; x + dx * (length - 1) < w; ++x) {
                        Bits cells = 0;
                        for (unsigned i = 0; i < length; ++i) cells |= Bits(1) << ((y + dy * i) * w + x + dx * i);
                        AddPattern(out, cells, w, h);
                    }
            }
        } else {
            // 3x3 row-major masks: all four L/T rotations, cross, and square.
            std::vector<unsigned> masks;
            if (kind == PHOrbGoalL) masks = {0x1c9, 0x1e4, 0x04f, 0x127};
            else if (kind == PHOrbGoalT) masks = {0x097, 0x1d2, 0x079, 0x13c};
            else if (kind == PHOrbGoalCross) masks = {0x0ba};
            else if (kind == PHOrbGoalSquare) masks = {0x1ff};
            for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x)
                for (unsigned mask : masks) {
                    Bits cells = 0;
                    for (unsigned i = 0; i < 9; ++i) if (mask & (1u << i)) cells |= Bits(1) << ((y + i / 3) * w + x + i % 3);
                    AddPattern(out, cells, w, h);
                }
        }
    }
    return context;
}

unsigned Needed(const PHOrbGoal &goal, unsigned w, unsigned h) {
    switch (goal.kind) {
        case PHOrbGoalSquare: return 9;
        case PHOrbGoalL: case PHOrbGoalT: case PHOrbGoalCross: return 5;
        case PHOrbGoalRow: return w;
        case PHOrbGoalColumn: return h;
        case PHOrbGoalFour: return 4;
        case PHOrbGoalMatch: return goal.length;
        default: return 3;
    }
}

bool EnoughOrbs(const Cells &cells, const Context &context) {
    unsigned inventory[6]{};
    for (unsigned i = 0; i < context.width * context.height; ++i) ++inventory[cells[i]];
    for (unsigned g = 0; g < context.goals.count; ++g) {
        const auto &goal = context.goals.items[g];
        unsigned possible = 0, needed = Needed(goal, context.width, context.height);
        for (unsigned c = 0; c < 6; ++c) if (goal.kind == PHOrbGoalCombo || (goal.colors & (1u << c)))
            possible += goal.kind == PHOrbGoalColors ? unsigned(inventory[c] >= 3) : inventory[c] / needed;
        if (possible < goal.count) return false;
    }
    return true; // Necessary inventory check, not a proof that a route exists.
}

bool ShapeMatches(Bits group, Bits color, const PHOrbGoal &goal, const std::vector<Pattern> &patterns) {
    for (const auto &pattern : patterns) {
        if (goal.kind == PHOrbGoalRow || goal.kind == PHOrbGoalColumn) {
            if ((group & pattern.cells) == pattern.cells) return true;
        } else if (group == pattern.cells && !(color & pattern.border)) return true;
    }
    return false;
}

// Near-complete templates guide the beam even before they become legal
// matches. They never count as a fulfilled goal or authorize execution.
unsigned NearPattern(Bits color, const std::vector<Pattern> &patterns, Bits used, bool exact) {
    unsigned best = 0;
    for (const auto &pattern : patterns) {
        if (pattern.cells & used) continue;
        unsigned size = Count(pattern.cells), filled = Count(color & pattern.cells);
        unsigned score = 3072 * filled * filled / (size * size);
        unsigned penalty = exact ? Count(color & pattern.border) * 96 : 0;
        best = std::max(best, score > penalty ? score - penalty : 0);
    }
    return best;
}

// All matches in a wave are marked together. Only existing orbs fall; no
// random skyfall or leader-added combos. Exact shapes require an entire
// first-wave group, so a 3x4 blob is never reported as a 3x3 square.
Score Evaluate(Cells cells, const Context &context) {
    Score score;
    auto &result = score.result;
    const unsigned width = context.width, height = context.height, size = width * height;
    const Cells initial = cells;
    std::array<Bits, 6> colors{};
    std::array<Bits, PHOrbMaxGoals> used{};
    unsigned matchedColors = 0;
    for (unsigned i = 0; i < size; ++i) {
        colors[cells[i]] |= Bits(1) << i;
        if (i % width + 1 < width && cells[i] == cells[i + 1]) ++score.potential;
        if (i + width < size && cells[i] == cells[i + width]) ++score.potential;
        if (i % width + 2 < width && cells[i] == cells[i + 2]) ++score.potential;
        if (i + 2 * width < size && cells[i] == cells[i + 2 * width]) ++score.potential;
    }
    for (unsigned wave = 0; wave < size / 3; ++wave) {
        std::array<bool, PHOrbMaxCells> marked{}, seen{};
        bool any = false;
        for (unsigned i = 0; i < size; ++i) {
            if (cells[i] == Empty) continue;
            if (i % width + 2 < width && cells[i] == cells[i + 1] && cells[i] == cells[i + 2])
                marked[i] = marked[i + 1] = marked[i + 2] = any = true;
            if (i + 2 * width < size && cells[i] == cells[i + width] && cells[i] == cells[i + 2 * width])
                marked[i] = marked[i + width] = marked[i + 2 * width] = any = true;
        }
        if (!any) break;
        for (unsigned start = 0; start < size; ++start) {
            if (!marked[start] || seen[start]) continue;
            ++result.combos;
            matchedColors |= 1u << cells[start];
            Bits group = 0;
            std::array<unsigned, PHOrbMaxCells> queue{};
            unsigned head = 0, tail = 1;
            queue[0] = start; seen[start] = true;
            while (head < tail) {
                unsigned index = queue[head++];
                group |= Bits(1) << index; ++result.erased;
                for (int next : {int(index) - int(width), int(index) + int(width), int(index) - 1, int(index) + 1}) {
                    if (next < 0 || unsigned(next) >= size || !PHOrbAdjacent(index, unsigned(next), width, height) ||
                        !marked[next] || seen[next] || cells[next] != cells[start]) continue;
                    seen[next] = true; queue[tail++] = unsigned(next);
                }
            }
            for (unsigned g = 0; g < context.goals.count; ++g) {
                const auto &goal = context.goals.items[g];
                if (!(goal.colors & (1u << cells[start]))) continue;
                bool hit = goal.kind == PHOrbGoalMatch ? tail >= goal.length :
                    wave == 0 && goal.kind >= PHOrbGoalL && ShapeMatches(group, colors[cells[start]], goal, context.patterns[g]);
                if (hit) { ++result.goalHits[g]; if (wave == 0) used[g] |= group; }
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
    for (unsigned g = 0; g < context.goals.count; ++g) {
        const auto &goal = context.goals.items[g];
        if (goal.kind == PHOrbGoalCombo) result.goalHits[g] = result.combos;
        if (goal.kind == PHOrbGoalColors) result.goalHits[g] = uint8_t(Count(matchedColors & goal.colors));
        if (result.goalHits[g] >= goal.count) { ++result.matchedGoals; score.progress += 4096; continue; }
        unsigned near = 0;
        if (goal.kind == PHOrbGoalColors) {
            std::array<unsigned, 6> partial{};
            for (unsigned c = 0; c < 6; ++c) if ((goal.colors & (1u << c)) && !(matchedColors & (1u << c)))
                partial[c] = NearPattern(colors[c], context.patterns[g], 0, false);
            std::sort(partial.begin(), partial.end(), std::greater<unsigned>());
            for (unsigned i = 0; i < unsigned(goal.count - result.goalHits[g]); ++i) near += partial[i];
        } else if (goal.kind >= PHOrbGoalL) {
            for (unsigned c = 0; c < 6; ++c) if (goal.colors & (1u << c))
                near = std::max(near, NearPattern(colors[c], context.patterns[g], used[g],
                    goal.kind != PHOrbGoalRow && goal.kind != PHOrbGoalColumn));
        } else if (goal.kind == PHOrbGoalMatch) {
            // Largest connected area is only a heuristic; the actual result
            // above still requires marked triples and the requested length.
            Bits seen = 0;
            for (unsigned start = 0; start < size; ++start) {
                if ((seen & (Bits(1) << start)) || !(goal.colors & (1u << initial[start])) || (used[g] & (Bits(1) << start))) continue;
                std::array<unsigned, PHOrbMaxCells> queue{};
                unsigned head = 0, tail = 1; queue[0] = start; seen |= Bits(1) << start;
                while (head < tail) {
                    unsigned index = queue[head++];
                    for (int next : {int(index) - int(width), int(index) + int(width), int(index) - 1, int(index) + 1}) {
                        if (next < 0 || !PHOrbAdjacent(index, unsigned(next), width, height) ||
                            (seen & (Bits(1) << next)) || (used[g] & (Bits(1) << next)) || initial[next] != initial[start]) continue;
                        seen |= Bits(1) << next; queue[tail++] = unsigned(next);
                    }
                }
                near = std::max(near, 3072 * std::min(tail, unsigned(goal.length)) / goal.length);
            }
        }
        score.progress += (result.goalHits[g] * 4096 + near) / goal.count;
    }
    result.goalsMet = result.matchedGoals == context.goals.count;
    return score;
}

bool Better(const Node &a, const Node &b) {
    const auto &ar = a.score.result, &br = b.score.result;
    if (ar.goalsMet != br.goalsMet) return ar.goalsMet;
    if (!ar.goalsMet && a.score.progress != b.score.progress) return a.score.progress > b.score.progress;
    if (ar.combos != br.combos) return ar.combos > br.combos;
    if (ar.erased != br.erased) return ar.erased > br.erased;
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

PHOrbSolveOptions PHOrbOptionsForQuality(PHOrbSearchQuality quality) noexcept {
    switch (quality) {
        case PHOrbSearchQuality::Fast: return {24, 128, 120, {}};
        case PHOrbSearchQuality::Thorough: return {48, 512, 800, {}};
        case PHOrbSearchQuality::Balanced: return {};
    }
    return {};
}

bool PHOrbEvaluate(const PHOrbBoard &board, const PHOrbGoalSet &goals, PHOrbEvaluation &result) noexcept {
    result = {};
    if (!PHOrbBoardValid(&board) || !PHOrbGoalsValid(&goals)) return false;
    try {
        Cells cells{};
        std::copy(board.types, board.types + board.width * board.height, cells.begin());
        result = Evaluate(cells, MakeContext(board.width, board.height, goals)).result;
        return true;
    } catch (...) { return false; }
}

PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept {
    route = {};
    if (!PHOrbBoardValid(&board) || !PHOrbGoalsValid(&options.goals) || !options.steps || options.steps > PHOrbMaxSteps ||
        !options.beam || options.beam > 512 || !options.milliseconds || options.milliseconds > 1000) return PHOrbSolveStatus::Invalid;
    if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
    try {
        const auto start = Clock::now(), deadline = start + std::chrono::milliseconds(options.milliseconds);
        const unsigned size = board.width * board.height;
        Cells initial{};
        std::copy(board.types, board.types + size, initial.begin());
        const Context context = MakeContext(board.width, board.height, options.goals);
        if (!EnoughOrbs(initial, context)) return PHOrbSolveStatus::InsufficientOrbs;
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
                for (int cell : {int(current) - int(board.width), int(current) + int(board.width), int(current) - 1, int(current) + 1}) {
                    if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
                    if ((evaluated & (options.goals.count ? 7u : 31u)) == 0 && Clock::now() >= deadline) { timedOut = true; break; }
                    if (cell < 0 || !PHOrbAdjacent(current, unsigned(cell), board.width, board.height) ||
                        (parent.length > 1 && unsigned(cell) == parent.path[parent.length - 2])) continue;
                    Node child = parent;
                    child.path[child.length++] = uint8_t(cell);
                    std::swap(child.cells[current], child.cells[cell]);
                    if (!visited.insert(Hash(child, size)).second) continue;
                    child.score = Evaluate(child.cells, context);
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
        route.combos = best.score.result.combos; route.erased = best.score.result.erased;
        route.goalCount = options.goals.count; route.matchedGoals = best.score.result.matchedGoals;
        route.goalsMet = best.score.result.goalsMet;
        std::copy(std::begin(best.score.result.goalHits), std::end(best.score.result.goalHits), route.goalHits);
        route.evaluated = evaluated; route.deadlineReached = timedOut;
        route.elapsedMS = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
        PHOrbBoard replay{};
        if (!PHOrbReplay(&board, &route, route.count - 1, &replay) || std::memcmp(replay.types, best.cells.data(), size)) {
            route = {}; return PHOrbSolveStatus::Failed;
        }
        return route.goalsMet ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet;
    } catch (...) { route = {}; return PHOrbSolveStatus::Failed; }
}
