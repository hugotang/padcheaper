// Complete optimistic graph for a 6x5 basic board with at most one affordable
// uniform 4% thorn contact. Spinner cells stay blocked. Reverse moves, unlimited
// steps and interchangeable non-light orbs enlarge the shipping route space;
// hearts are ignored. Zero robust T states proves only this policy has no T.
#include "PHOrbModel.h"
#include <array>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <unordered_set>
#include <vector>
using Bits = uint64_t;
struct State { Bits light; unsigned current, barrier = 63, touched = 63; };
static uint64_t Key(const State &state) {
    return state.light | (uint64_t(state.current) << 30) |
        (uint64_t(state.barrier) << 36) | (uint64_t(state.touched) << 42);
}
int main() {
    unsigned turn, width, height, known, hp, maximum, pending, priority, colors;
    Bits spinner;
    if (!(std::cin >> turn >> width >> height >> spinner >> colors >> known >> hp >> maximum >> pending >> priority) ||
        width != 6 || height != 5 || colors != 63 || known != 1 || priority > 1 || !spinner || spinner >> 30 ||
        !hp || hp > maximum || maximum > INT32_MAX || pending >= hp) return 2;
    Bits light = 0, thorns = 0;
    for (unsigned i = 0, value; i < 30; ++i) {
        if (!(std::cin >> value) || value > 5) return 2;
        if (value == 3) light |= Bits(1) << i;
    }
    for (unsigned i = 0, value; i < 30; ++i) {
        if (!(std::cin >> value) || (value && value != 4)) return 2;
        if (value) thorns |= Bits(1) << i;
    }
    const uint32_t contactDamage = PHOrbThornDamage(4, maximum), budget = hp - pending - 1;
    const unsigned maxContacts = budget / contactDamage;
    if (maxContacts > 1) return 2;
    std::array<std::vector<unsigned>, 30> neighbours;
    for (unsigned i = 0; i < 30; ++i) for (unsigned j = 0; j < 30; ++j)
        if ((i / 6 == j / 6 && (i + 1 == j || j + 1 == i)) || i + 6 == j || j + 6 == i)
            neighbours[i].push_back(j);
    const unsigned shapes[4][5] = {{0,1,2,4,7}, {1,4,6,7,8}, {0,3,4,5,6}, {2,3,4,5,8}};
    std::vector<std::pair<Bits, Bits>> patterns;
    for (unsigned y = 0; y < 3; ++y) for (unsigned x = 0; x < 4; ++x) for (const auto &shape : shapes) {
        Bits target = 0, border = 0;
        for (unsigned i : shape) target |= Bits(1) << ((y + i / 3) * 6 + x + i % 3);
        for (unsigned i = 0; i < 30; ++i) if (target & (Bits(1) << i))
            for (unsigned j : neighbours[i]) border |= Bits(1) << j;
        patterns.emplace_back(target, border & ~target);
    }
    const auto hasT = [&](Bits orbs) {
        for (auto pattern : patterns)
            if ((orbs & pattern.first) == pattern.first && !(orbs & pattern.second)) return true;
        return false;
    };
    size_t total = 0, solutions = 0;
    std::array<size_t, 30> byStart{};
    for (unsigned start = 0; start < 30; ++start) {
        if (spinner & (Bits(1) << start)) continue;
        // Selecting a thorn holds that token; native damage applies to target
        // tokens. After the one contact, the displaced thorn stays a barrier.
        const Bits barriers = thorns & ~(Bits(1) << start);
        std::vector<State> queue{{light, start, 63, 63}};
        std::unordered_set<uint64_t> visited{Key(queue[0])};
        for (size_t at = 0; at < queue.size(); ++at) {
            const State state = queue[at];
            if (hasT(state.light & ~spinner) && hasT(state.light | spinner)) ++solutions;
            for (unsigned next : neighbours[state.current]) {
                if ((spinner & (Bits(1) << next)) || next == state.barrier) continue;
                State child = state;
                if ((barriers & (Bits(1) << next)) && next != state.touched) {
                    if (state.barrier != 63 || !maxContacts) continue;
                    child.barrier = state.current; child.touched = next;
                }
                const Bits a = Bits(1) << state.current, b = Bits(1) << next;
                if (bool(state.light & a) != bool(state.light & b)) child.light ^= a | b;
                child.current = next;
                if (visited.insert(Key(child)).second) queue.push_back(child);
            }
        }
        total += queue.size(); byStart[start] = queue.size();
    }
    std::printf("{\"turn\":%u,\"max_contacts\":%u,\"contact_damage\":%u,\"states_summed_over_starts\":%zu,"
        "\"all_phase_T_states\":%zu,\"complete\":true,\"directions\":4,\"heart_requirement\":false,\"states_by_start\":[",
        turn, maxContacts, contactDamage, total, solutions);
    for (unsigned i = 0; i < 30; ++i) std::printf("%s%zu", i ? "," : "", byStart[i]);
    std::puts("]}");
}
