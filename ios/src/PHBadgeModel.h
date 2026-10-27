#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool readable;
    uint32_t kind;
    uint16_t prerequisite;
} PHBadgeEntry;

// Entries are indexed by badge ID; index zero is the default and is never set.
size_t PHBadgeBuildMask(const PHBadgeEntry entries[256], size_t count, uint32_t mask[8]);
uint8_t PHBadgeEncode(uint8_t identifier, size_t bankIndex);
