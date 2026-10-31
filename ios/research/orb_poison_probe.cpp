// Controlled recorded-board poison policy comparison. Never linked into the
// dylib. Independent complete-run, union, gravity, T and first-wave four checks.
// Input header: id width height spinnerMask colors known hp maximum pending
// thornPriority poisonWithFour steps milliseconds tape cloud fixedStart
// unmatchable minErase minMatch; then types, flags, enhancement, blind, thorn %.
#include "PHOrbSolver.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <numeric>
#include <set>
#include <vector>

struct PoisonOracleResult {
    unsigned combos = 0, erased = 0, lightT = 0, hearts = 0, poison = 0, mortal = 0, bombs = 0;
};
static bool Adjacent(unsigned a, unsigned b, unsigned w) {
    return (a / w == b / w && (a + 1 == b || b + 1 == a)) || a + w == b || b + w == a;
}
static bool LightT(const std::vector<unsigned> &group, const std::vector<int> &cells, unsigned w, unsigned h) {
    if (group.size() != 5) return false;
    unsigned minX = w, minY = h, maxX = 0, maxY = 0;
    for (unsigned cell : group) {
        minX = std::min(minX, cell % w); maxX = std::max(maxX, cell % w);
        minY = std::min(minY, cell / w); maxY = std::max(maxY, cell / w);
    }
    if (maxX - minX != 2 || maxY - minY != 2) return false;
    std::set<unsigned> local;
    for (unsigned cell : group) local.insert((cell / w - minY) * 3 + cell % w - minX);
    const unsigned shapes[4][5] = {{0,1,2,4,7}, {1,4,6,7,8}, {0,3,4,5,6}, {2,3,4,5,8}};
    bool shape = false;
    for (const auto &pattern : shapes) shape |= local == std::set<unsigned>(pattern, pattern + 5);
    if (!shape) return false;
    for (unsigned cell : group) for (unsigned other = 0; other < cells.size(); ++other)
        if (Adjacent(cell, other, w) && cells[other] == 3 &&
            std::find(group.begin(), group.end(), other) == group.end()) return false;
    return true;
}
static PoisonOracleResult PoisonOracle(std::vector<int> cells, const PHOrbBoard &board) {
    PoisonOracleResult result;
    const unsigned w = board.width, h = board.height, n = w * h, minimum = board.minErase ? board.minErase : 3;
    for (unsigned wave = 0; wave <= n / 3; ++wave) {
        std::set<unsigned> marked;
        for (unsigned axis = 0; axis < 2; ++axis) {
            const unsigned lines = axis ? w : h, length = axis ? h : w;
            for (unsigned line = 0; line < lines; ++line) {
                auto at = [&](unsigned p) { return axis ? p * w + line : line * w + p; };
                for (unsigned begin = 0; begin < length;) {
                    unsigned end = begin + 1;
                    while (end < length && cells[at(end)] == cells[at(begin)]) ++end;
                    const int color = cells[at(begin)];
                    if (color >= 0 && !(board.unmatchableColors & (1u << color)) && end - begin >= minimum)
                        for (unsigned p = begin; p < end; ++p) marked.insert(at(p));
                    begin = end;
                }
            }
        }
        std::vector<unsigned> parent(n), sizes(n); std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](unsigned a) { while (parent[a] != a) a = parent[a]; return a; };
        for (unsigned a : marked) for (unsigned b : marked)
            if (a < b && cells[a] == cells[b] && Adjacent(a, b, w)) parent[root(b)] = root(a);
        for (unsigned cell : marked) ++sizes[root(cell)];
        for (auto it = marked.begin(); it != marked.end();)
            if (sizes[root(*it)] < board.minMatch) it = marked.erase(it); else ++it;
        if (!wave) for (unsigned cell = 0; cell < n; ++cell) result.bombs += cells[cell] == 9 && !marked.count(cell);
        if (marked.empty()) return result;
        for (unsigned start = 0; start < n; ++start) {
            std::vector<unsigned> group;
            for (unsigned cell : marked) if (root(cell) == start) group.push_back(cell);
            if (group.empty()) continue;
            const int color = cells[group[0]];
            ++result.combos; result.erased += unsigned(group.size());
            if (color == 7 || color == 8) result.poison += unsigned(group.size());
            if (color == 8) result.mortal += unsigned(group.size());
            if (!wave && color == 3 && LightT(group, cells, w, h)) ++result.lightT;
            if (!wave && color == 5 && group.size() == 4) {
                bool row = true, column = true;
                for (unsigned cell : group) { row &= cell / w == group[0] / w; column &= cell % w == group[0] % w; }
                result.hearts += row || column;
            }
        }
        for (unsigned cell : marked) cells[cell] = -1;
        for (unsigned x = 0; x < w; ++x) {
            std::vector<int> survivors;
            for (unsigned y = 0; y < h; ++y) if (cells[y * w + x] >= 0) survivors.push_back(cells[y * w + x]);
            survivors.insert(survivors.begin(), h - survivors.size(), -1);
            for (unsigned y = 0; y < h; ++y) cells[y * w + x] = survivors[y];
        }
    }
    return result;
}

int main() {
    unsigned id, w, h, colors, known, hp, maximum, pending, priority, allow, steps, milliseconds, fixed, unmatchable, run, group;
    uint64_t mask, tape, cloud;
    while (std::cin >> id >> w >> h >> mask >> colors >> known >> hp >> maximum >> pending >> priority >> allow >>
        steps >> milliseconds >> tape >> cloud >> fixed >> unmatchable >> run >> group) {
        if (!((w == 6 && h == 5) || (w == 7 && h == 6) || (w == 5 && h == 4)) ||
            colors > 0x3ff || known > 1 || priority > 1 || allow > 1 || fixed > w * h || unmatchable > 0x3ff || run > 7 || group > 42) return 2;
        PHOrbBoard board{}; board.width = uint8_t(w); board.height = uint8_t(h);
        board.spinnerMask = mask; board.spinnerColors = uint16_t(colors); board.tapeMask = tape; board.cloudMask = cloud;
        board.fixedStart = uint8_t(fixed); board.unmatchableColors = uint16_t(unmatchable); board.minErase = uint8_t(run); board.minMatch = uint8_t(group);
        for (unsigned i = 0, value; i < w * h; ++i) {
            if (!(std::cin >> value) || value >= PHOrbTypes) return 2;
            board.types[i] = uint8_t(value); board.tokens[i] = i + 1;
            board.x[i] = int16_t(50 + i % w * 90); board.y[i] = int16_t(300 + i / w * 90);
        }
        for (unsigned i = 0; i < w * h; ++i) if (!(std::cin >> board.flags[i])) return 2;
        for (unsigned i = 0; i < w * h; ++i) if (!(std::cin >> board.enhancement[i])) return 2;
        for (unsigned i = 0, value; i < w * h; ++i) { if (!(std::cin >> value) || value > 255) return 2; board.blindTurns[i] = uint8_t(value); }
        for (unsigned i = 0, value; i < w * h; ++i) { if (!(std::cin >> value) || value > 127) return 2; board.thornDamage[i] = uint8_t(value); }
        auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
        options.steps = steps; options.milliseconds = milliseconds;
        options.goals.count = 2; options.goals.items[0] = {PHOrbGoalT,8,1,0}; options.goals.items[1] = {PHOrbGoalFour,32,1,0};
        options.thorns = {hp, maximum, pending, bool(known)}; options.thornComboPriority = bool(priority);
        options.allowPoisonWithHeartFour = bool(allow);
        const auto original = board; std::atomic_bool cancel{false}; PHOrbRoute route{};
        const auto began = std::chrono::steady_clock::now();
        const auto status = PHOrbSolve(board, options, cancel, route);
        const double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        bool valid = !std::memcmp(&board, &original, sizeof(board)) && route.count <= steps + 1;
        std::vector<int> cells(board.types, board.types + w * h);
        std::vector<unsigned> thorns(w * h);
        for (unsigned i = 0; i < w * h; ++i) thorns[i] = board.flags[i] & PHOrbFlagThorn ? board.thornDamage[i] : 0;
        uint64_t damage = 0; unsigned contacts = 0;
        if (route.count) valid &= !fixed || unsigned(route.cells[0]) + 1 == fixed;
        for (unsigned i = 0; i < route.count; ++i) valid &= route.cells[i] < w * h && !((mask | tape) & (UINT64_C(1) << route.cells[i]));
        for (unsigned i = 1; i < route.count && valid; ++i) {
            const unsigned a = route.cells[i - 1], b = route.cells[i];
            valid &= Adjacent(a, b, w); if (!valid) break;
            if (thorns[b]) { ++contacts; damage += std::max(1.0, std::floor(double(float(thorns[b]) / 100.0f) * maximum + 0.5)); }
            std::swap(cells[a], cells[b]); std::swap(thorns[a], thorns[b]);
        }
        PoisonOracleResult worst{}; bool safe = true;
        if (route.count && valid) {
            valid &= route.count >= 2 && route.goalCount == 2;
            std::vector<unsigned> cycle;
            for (unsigned c = 0; c < PHOrbTypes; ++c) if (colors & (1u << c)) cycle.push_back(c);
            const bool rotating = [&]() { for (unsigned i = 0; i < w * h; ++i) if ((mask & (UINT64_C(1) << i)) && !(board.flags[i] & PHOrbFlagLocked)) return true; return false; }();
            const unsigned phases = rotating ? unsigned(cycle.size()) : 1;
            valid &= phases > 0;
            worst.combos = worst.erased = worst.lightT = worst.hearts = w * h;
            for (unsigned phase = 0; phase < phases && valid; ++phase) {
                auto variant = cells;
                for (unsigned i = 0; i < w * h; ++i) if ((mask & (UINT64_C(1) << i)) && !(board.flags[i] & PHOrbFlagLocked)) {
                    auto at = std::find(cycle.begin(), cycle.end(), unsigned(board.types[i]));
                    if (at == cycle.end()) { valid = false; break; }
                    variant[i] = int(cycle[(unsigned(at - cycle.begin()) + phase) % phases]);
                }
                const auto oracle = PoisonOracle(variant, board);
                worst.combos = std::min(worst.combos, oracle.combos); worst.erased = std::min(worst.erased, oracle.erased);
                worst.lightT = std::min(worst.lightT, oracle.lightT); worst.hearts = std::min(worst.hearts, oracle.hearts);
                worst.poison = std::max(worst.poison, oracle.poison); worst.mortal = std::max(worst.mortal, oracle.mortal); worst.bombs = std::max(worst.bombs, oracle.bombs);
            }
            safe = !worst.bombs && (!worst.poison || (allow && worst.hearts));
            safe &= !contacts || (known && hp && hp <= maximum && maximum <= INT32_MAX && uint64_t(pending) + damage < hp && worst.hearts);
            const bool met = worst.lightT && worst.hearts;
            valid &= route.combos == worst.combos && route.erased == worst.erased && route.heartFours == worst.hearts &&
                route.goalHits[0] == worst.lightT && route.goalHits[1] == worst.hearts && route.goalsMet == met &&
                route.matchedGoals == unsigned(worst.lightT > 0) + unsigned(worst.hearts > 0) && route.hazardsSafe == safe &&
                route.poisonErased == worst.poison && route.mortalPoisonErased == worst.mortal && route.unmatchedBombs == worst.bombs &&
                route.thornContacts == contacts && route.thornDamage == damage &&
                status == (!safe ? PHOrbSolveStatus::Unsafe : met ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet);
        } else if (!route.count) valid &= status == PHOrbSolveStatus::InsufficientOrbs || status == PHOrbSolveStatus::NoRoute || status == PHOrbSolveStatus::IncompatibleGoals;
        const char *name = status == PHOrbSolveStatus::Solved ? "solved" : status == PHOrbSolveStatus::GoalsUnmet ? "goals_unmet" :
            status == PHOrbSolveStatus::Unsafe ? "unsafe" : status == PHOrbSolveStatus::InsufficientOrbs ? "insufficient" :
            status == PHOrbSolveStatus::NoRoute ? "no_route" : status == PHOrbSolveStatus::IncompatibleGoals ? "incompatible_goals" : "unexpected";
        std::printf("{\"id\":%u,\"allowPoisonWithHeartFour\":%s,\"status\":\"%s\",\"goals_met\":%s,\"hazards_safe\":%s,"
            "\"goal_hits\":[%u,%u],\"combos\":%u,\"steps\":%u,\"poisonErased\":%u,\"mortalPoisonErased\":%u,\"unmatchedBombs\":%u,"
            "\"thornContacts\":%u,\"thornDamage\":%llu,\"heartFours\":%u,\"elapsedMS\":%u,\"wall_ms\":%.3f,\"evaluated\":%u,"
            "\"deadline\":%s,\"passes\":%u,\"backtracks\":%u,\"depth\":%u,\"valid\":%s,\"path\":[",
            id, allow ? "true" : "false", name, route.goalsMet ? "true" : "false", route.hazardsSafe ? "true" : "false",
            route.goalHits[0], route.goalHits[1], route.combos, route.count ? route.count - 1 : 0, route.poisonErased, route.mortalPoisonErased, route.unmatchedBombs,
            contacts, (unsigned long long)damage, route.heartFours, route.elapsedMS, wall, route.evaluated,
            route.deadlineReached ? "true" : "false", route.searchPasses, route.backtracks, route.exploredDepth, valid ? "true" : "false");
        for (unsigned i = 0; i < route.count; ++i) std::printf("%s%u", i ? "," : "", route.cells[i]);
        std::puts("]}"); if (!valid) return 1;
    }
}
