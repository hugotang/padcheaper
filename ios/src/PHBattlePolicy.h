#pragma once
#include <stdbool.h>
#include <stdint.h>

enum { PHBattleFilterCapacity = 32 };

typedef struct {
    bool allowAll;
    uint8_t count;
    uint32_t values[PHBattleFilterCapacity];
} PHBattleValueFilter;

// A zero-initialized frame uses all entry categories and source/multi zero.
// Configured filters are copied with each frame and mutation transaction.
typedef struct {
    bool configured;
    PHBattleValueFilter modes, sources, multiplayer;
} PHBattlePolicy;

static inline bool PHBattleFilterAllows(const PHBattleValueFilter *filter, uint32_t value) {
    if (!filter || filter->count > PHBattleFilterCapacity) return false;
    if (filter->allowAll) return true;
    for (unsigned i = 0; i < filter->count; ++i) if (filter->values[i] == value) return true;
    return false;
}

static inline bool PHBattlePolicyAllows(const PHBattlePolicy *policy, int16_t mode,
                                      uint16_t source, uint32_t multiplayer) {
    if (!policy || !policy->configured) return source == 0 && multiplayer == 0;
    return PHBattleFilterAllows(&policy->modes, (uint16_t)mode) &&
           PHBattleFilterAllows(&policy->sources, source) &&
           PHBattleFilterAllows(&policy->multiplayer, multiplayer);
}

// This is an engine restriction, independent of legacy allow-all filters.
// Mode 7 remains multiplayer even while its multi selector is transitioning.
static inline bool PHBattleIsSinglePlayer(int16_t mode, uint32_t multiplayer) {
    return multiplayer == 0 && mode != 7;
}

// Describes native storage only; it must never authorize battle writes.
static inline bool PHBattleUsesPrimaryTeam(uint32_t multiplayer) {
    return multiplayer != 1 && multiplayer != 4;
}
