#pragma once
#import "PHMemory.h"
#include "PHInputModel.h"
#include "PHBattleGenerated.h"
#include <string.h>

// Read the native touch manager without calling or changing its input handlers.
// Both the pending UIKit records and the game-frame records must be quiet.
static inline BOOL PHReadGameInput(uintptr_t header, PHInputState *state) {
    if (!state) return NO;
    memset(state, 0, sizeof(*state));
    uintptr_t input = 0, lastInput = 0;
    uint8_t bytes[PHInputDataSize];
    if (!header || header > UINTPTR_MAX - PHBattleInputRVA ||
        !PHReadPointer(header + PHBattleInputRVA, &input) || !input || (input & 7u) ||
        input > UINTPTR_MAX - sizeof(bytes) ||
        !PHMemoryHasProtection(input, sizeof(bytes), VM_PROT_READ, VM_PROT_EXECUTE) ||
        !PHReadMemory(input, bytes, sizeof(bytes)) ||
        !PHReadPointer(header + PHBattleInputRVA, &lastInput) || input != lastInput) return NO;
    return PHInputDecode(bytes, sizeof(bytes), state);
}
