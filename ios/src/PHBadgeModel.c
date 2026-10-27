#include "PHBadgeModel.h"
#include <string.h>

static bool ValidChain(const PHBadgeEntry entries[256], size_t count, size_t identifier) {
    bool visited[256] = {false};
    while (identifier) {
        if (identifier > count || identifier > 255 || visited[identifier] || !entries[identifier].readable) return false;
        visited[identifier] = true;
        identifier = entries[identifier].prerequisite;
    }
    return true;
}

size_t PHBadgeBuildMask(const PHBadgeEntry entries[256], size_t count, uint32_t mask[8]) {
    if (!mask) return 0;
    memset(mask, 0, 8 * sizeof(*mask));
    if (!entries || !count || count > 255) return 0;
    size_t eligible = 0;
    for (size_t identifier = 1; identifier <= count; ++identifier) {
        // Keep placeholders disabled even if an owned prerequisite could make
        // the native lookup resolve to another badge's nonzero kind.
        if (!entries[identifier].kind || !ValidChain(entries, count, identifier)) continue;
        mask[identifier / 32] |= UINT32_C(1) << (identifier % 32);
        ++eligible;
    }
    return eligible;
}

uint8_t PHBadgeEncode(uint8_t identifier, size_t bankIndex) {
    return identifier ^ (uint8_t)(15u * bankIndex + 0x61u);
}
