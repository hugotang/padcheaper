#ifndef PHOrbEightModel_h
#define PHOrbEightModel_h

// Pure C inventory arithmetic for 8P skill planning. It reads only the fixed
// in-match skill records and charges already captured in a PHOrbSnapshot, so
// it builds and is tested on any C11 compiler (including Windows), separately
// from the Objective-C activity-profile code that calls it.
#include "PHOrbModel.h"
#include <stdbool.h>
#include <stdint.h>

// Composes the fixed colour conversions of an ordered slot sequence (1-based
// slots, at most six). Fails if a slot is out of range, already attempted this
// turn, inactive, bound, in a native state, has an unknown skill, rewrites the
// whole board, generates orbs, converts to a non-basic colour, or needs more
// than `turnsAhead` further charge turns. On success map[c] is the final colour
// of every orb that started as colour c, and *readyNow (if given) reports
// whether every slot is already fully charged.
static inline bool PHOrbEightComposeConversion(const PHOrbSnapshot *s, const uint8_t *slots, unsigned count,
                                               uint8_t attempted, unsigned turnsAhead, uint8_t map[6], bool *readyNow) {
    bool ready = true;
    for (unsigned c = 0; c < 6; ++c) map[c] = (uint8_t)c;
    if (!s || count > 6) return false;
    for (unsigned i = 0; i < count; ++i) {
        if (!slots[i] || slots[i] > 6) return false;
        const unsigned slot = slots[i] - 1u;
        const PHOrbEightMember *m = &s->eight.members[slot];
        const PHOrbSkillMember *member = &s->automation.members[slot];
        if ((attempted & (1u << slot)) || !member->active || !m->skillKnown || member->bound || member->state ||
            (unsigned)member->charge + turnsAhead < m->cooldown || m->boardColors) return false;
        ready = ready && member->charge >= m->cooldown;
        for (unsigned c = 0; c < 6; ++c) if (m->generate[c] || m->convert[c] >= 6) return false;
        for (unsigned c = 0; c < 6; ++c) map[c] = m->convert[map[c]];
    }
    if (readyNow) *readyNow = ready;
    return true;
}

// Orbs counted by their converted colour.
static inline void PHOrbEightConvertedCounts(const unsigned counts[6], const uint8_t map[6], unsigned converted[6]) {
    for (unsigned c = 0; c < 6; ++c) converted[c] = 0;
    for (unsigned c = 0; c < 6; ++c) converted[map[c]] += counts[c];
}

// Orbs whose converted colour is one of the leader attack colours (bit mask).
static inline unsigned PHOrbEightAttackOrbs(const unsigned counts[6], const uint8_t map[6], unsigned colors) {
    unsigned total = 0;
    for (unsigned c = 0; c < 6; ++c) if (colors & (1u << map[c])) total += counts[c];
    return total;
}

// Whether erasing one group of three orbs of a single original colour can
// still leave, after the conversions, at least `linkLength` of some attack
// colour and at least `primaryMinimum` of the primary colour.
static inline bool PHOrbEightCanRetainAfterThree(const unsigned counts[6], const uint8_t map[6], unsigned colors,
                                                 unsigned primary, unsigned linkLength, unsigned primaryMinimum) {
    unsigned converted[6];
    if (primary >= 6) return false;
    PHOrbEightConvertedCounts(counts, map, converted);
    for (unsigned removed = 0; removed < 6; ++removed) {
        if (counts[removed] < 3) continue;
        unsigned retained[6];
        for (unsigned c = 0; c < 6; ++c) retained[c] = converted[c];
        retained[map[removed]] -= 3;
        if (retained[primary] < primaryMinimum) continue;
        for (unsigned c = 0; c < 6; ++c) if ((colors & (1u << c)) && retained[c] >= linkLength) return true;
    }
    return false;
}

#endif
