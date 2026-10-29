// Standalone basic-board comparison harness, never linked into the dylib.
// Uses the public API shared with 0.5.2; validates every returned route using
// its own runs/union/gravity and shape checks, not PHOrbEvaluate/PHOrbReplay.
// Args: width height row-major-digits match|four fast|balanced|thorough [steps]
#include "PHOrbSolver.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <set>
#include <string>
#include <vector>

struct OracleResult { unsigned combos = 0, erased = 0, lightT = 0, heart = 0; };

static bool IsT(const std::vector<unsigned> &group, const std::vector<int> &cells, unsigned w, unsigned h) {
    if (group.size() != 5) return false;
    unsigned minX = w, minY = h, maxX = 0, maxY = 0;
    for (unsigned cell : group) {
        minX = std::min(minX, cell % w); maxX = std::max(maxX, cell % w);
        minY = std::min(minY, cell / w); maxY = std::max(maxY, cell / w);
    }
    if (maxX - minX != 2 || maxY - minY != 2) return false;
    const unsigned patterns[4][5] = {{0, 1, 2, 4, 7}, {1, 4, 6, 7, 8}, {0, 3, 4, 5, 6}, {2, 3, 4, 5, 8}};
    std::set<unsigned> local;
    for (unsigned cell : group) local.insert((cell / w - minY) * 3 + cell % w - minX);
    bool shape = false;
    for (const auto &pattern : patterns) shape |= local == std::set<unsigned>(pattern, pattern + 5);
    if (!shape) return false;
    // T retains the conservative colour-border rule in both versions.
    for (unsigned cell : group) for (unsigned next = 0; next < cells.size(); ++next) {
        const bool adjacent = (cell / w == next / w && (cell + 1 == next || next + 1 == cell)) ||
            cell + w == next || next + w == cell;
        if (adjacent && cells[next] == 3 && std::find(group.begin(), group.end(), next) == group.end()) return false;
    }
    return true;
}

static OracleResult Oracle(std::vector<int> cells, unsigned w, unsigned h, bool four) {
    OracleResult result;
    const unsigned n = w * h;
    for (unsigned wave = 0; wave < n / 3; ++wave) {
        std::set<unsigned> marked;
        for (unsigned axis = 0; axis < 2; ++axis) {
            const unsigned lines = axis ? w : h, length = axis ? h : w;
            for (unsigned line = 0; line < lines; ++line) {
                auto at = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
                for (unsigned begin = 0; begin < length;) {
                    unsigned end = begin + 1;
                    while (end < length && cells[at(end)] == cells[at(begin)]) ++end;
                    if (cells[at(begin)] >= 0 && end - begin >= 3)
                        for (unsigned p = begin; p < end; ++p) marked.insert(at(p));
                    begin = end;
                }
            }
        }
        if (marked.empty()) break;
        std::vector<unsigned> parent(n);
        std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](unsigned a) { while (parent[a] != a) a = parent[a]; return a; };
        for (unsigned a : marked) for (unsigned b : marked)
            if (a < b && cells[a] == cells[b] && ((a / w == b / w && b == a + 1) || b == a + w)) parent[root(b)] = root(a);
        for (unsigned i = 0; i < n; ++i) {
            std::vector<unsigned> group;
            for (unsigned cell : marked) if (root(cell) == i) group.push_back(cell);
            if (group.empty()) continue;
            ++result.combos;
            result.erased += unsigned(group.size());
            if (!wave && cells[group[0]] == 3 && IsT(group, cells, w, h)) ++result.lightT;
            if (cells[group[0]] != 5) continue;
            if (!four && group.size() >= 4) ++result.heart;
            if (four && !wave && group.size() == 4) {
                bool row = true, column = true;
                for (unsigned cell : group) { row &= cell / w == group[0] / w; column &= cell % w == group[0] % w; }
                result.heart += row || column;
            }
        }
        for (unsigned cell : marked) cells[cell] = -1;
        for (unsigned x = 0; x < w; ++x) {
            std::vector<int> column;
            for (unsigned y = 0; y < h; ++y) if (cells[y * w + x] >= 0) column.push_back(cells[y * w + x]);
            column.insert(column.begin(), h - column.size(), -1);
            for (unsigned y = 0; y < h; ++y) cells[y * w + x] = column[y];
        }
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc != 6 && argc != 7) return 2;
    const std::string widthArg(argv[1]), heightArg(argv[2]);
    const unsigned w = unsigned(std::strtoul(argv[1], nullptr, 10)), h = unsigned(std::strtoul(argv[2], nullptr, 10));
    if (widthArg != std::to_string(w) || heightArg != std::to_string(h) ||
        !((w == 6 && h == 5) || (w == 7 && h == 6) || (w == 5 && h == 4))) return 2;
    const std::string digits(argv[3]), mode(argv[4]), quality(argv[5]);
    if (digits.size() != w * h || digits.find_first_not_of("012345") != std::string::npos ||
        (mode != "match" && mode != "four") || (quality != "fast" && quality != "balanced" && quality != "thorough")) return 2;
    PHOrbBoard board{}; board.width = uint8_t(w); board.height = uint8_t(h);
    for (unsigned i = 0; i < w * h; ++i) {
        board.types[i] = uint8_t(digits[i] - '0'); board.tokens[i] = i + 1;
        board.x[i] = int16_t(50 + i % w * 100); board.y[i] = int16_t(300 + i / w * 100);
    }
    const auto original = board;
    auto options = PHOrbOptionsForQuality(quality == "fast" ? PHOrbSearchQuality::Fast :
        quality == "balanced" ? PHOrbSearchQuality::Balanced : PHOrbSearchQuality::Thorough);
    if (argc == 7) {
        const unsigned steps = unsigned(std::strtoul(argv[6], nullptr, 10));
        if (!steps || steps > options.steps || std::string(argv[6]) != std::to_string(steps)) return 2;
        options.steps = steps;
    }
    options.goals.count = 3;
    options.goals.items[0] = {PHOrbGoalT, 8, 1, 0};
    options.goals.items[1] = {uint8_t(mode == "four" ? PHOrbGoalFour : PHOrbGoalMatch), 32, 1, uint8_t(mode == "four" ? 0 : 4)};
    options.goals.items[2] = {PHOrbGoalCombo, 0, 1, 0};
    PHOrbRoute route{};
    std::atomic_bool cancel{false};
    const auto started = std::chrono::steady_clock::now();
    const auto status = PHOrbSolve(board, options, cancel, route);
    const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    const char *name = status == PHOrbSolveStatus::Solved ? "solved" : status == PHOrbSolveStatus::GoalsUnmet ? "goals_unmet" :
        status == PHOrbSolveStatus::NoRoute ? "no_route" : status == PHOrbSolveStatus::InsufficientOrbs ? "insufficient" : "unexpected";
    bool valid = !std::memcmp(&board, &original, sizeof(board));
    const bool hasRoute = status == PHOrbSolveStatus::Solved || status == PHOrbSolveStatus::GoalsUnmet;
    valid &= hasRoute ? route.count >= 2 && route.count <= options.steps + 1 : !route.count && std::strcmp(name, "unexpected");
    OracleResult oracle;
    if (hasRoute && valid) {
        std::vector<int> cells(board.types, board.types + w * h);
        for (unsigned i = 0; i < route.count; ++i) valid &= route.cells[i] < w * h;
        for (unsigned i = 1; i < route.count && valid; ++i) {
            const unsigned a = route.cells[i - 1], b = route.cells[i];
            valid &= (a / w == b / w && (a + 1 == b || b + 1 == a)) || a + w == b || b + w == a;
            if (valid) std::swap(cells[a], cells[b]);
        }
        if (valid) {
            oracle = Oracle(cells, w, h, mode == "four");
            const unsigned oracleMatched = unsigned(oracle.lightT > 0) + unsigned(oracle.heart > 0) + unsigned(oracle.combos > 0);
            valid &= route.combos == oracle.combos && route.erased == oracle.erased && route.goalCount == 3 &&
                route.goalHits[0] == oracle.lightT && route.goalHits[2] == oracle.combos &&
                (mode == "four" ? route.goalHits[1] <= oracle.heart : route.goalHits[1] == oracle.heart) &&
                (!route.goalsMet || oracleMatched == 3) && ((status == PHOrbSolveStatus::Solved) == route.goalsMet);
            const unsigned reported = unsigned(route.goalHits[0] > 0) + unsigned(route.goalHits[1] > 0) + unsigned(route.goalHits[2] > 0);
            valid &= route.matchedGoals == reported && route.goalsMet == (reported == 3);
        }
    }
    // Old versions can conservatively undercount fours. The oracle records
    // that separately; any false acceptance, score or path mismatch fails.
    std::printf("{\"mode\":\"%s\",\"quality\":\"%s\",\"status\":\"%s\",\"goals_met\":%s,\"matched\":%u,"
        "\"goal_hits\":[%u,%u,%u],\"oracle\":{\"T\":%u,\"heart\":%u,\"combos\":%u,\"erased\":%u},"
        "\"steps\":%u,\"step_budget\":%u,\"elapsed_ms\":%u,\"wall_ms\":%.3f,\"deadline\":%s,\"evaluated\":%u,"
        "\"valid\":%s,\"route\":[", mode.c_str(), quality.c_str(), name, route.goalsMet ? "true" : "false",
        unsigned(route.matchedGoals), unsigned(route.goalHits[0]), unsigned(route.goalHits[1]), unsigned(route.goalHits[2]),
        oracle.lightT, oracle.heart, oracle.combos, oracle.erased, route.count ? unsigned(route.count - 1) : 0,
        options.steps, unsigned(route.elapsedMS), wall, route.deadlineReached ? "true" : "false", unsigned(route.evaluated), valid ? "true" : "false");
    for (unsigned i = 0; i < route.count && i < PHOrbMaxSteps + 1; ++i) std::printf("%s%u", i ? "," : "", unsigned(route.cells[i]));
    std::puts("]}");
    return valid ? 0 : 1;
}
