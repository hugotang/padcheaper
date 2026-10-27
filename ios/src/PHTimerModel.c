#include "PHTimerModel.h"

bool PHTimerEligible(const PHTimerState *state) {
    return state && state->present && state->valuesValid && PHBattleSupportedEntryCategory(state->mode) &&
        state->multiplayer == 0 && state->limitSeconds > 0 && state->countdown && state->sceneID != 0x80386;
}

uint8_t PHTimerAutomaticFlags(const PHTimerState *state) {
    if (!state) return 0;
    if (!PHTimerEligible(state) || (state->phase != 2 && state->phase != 30)) return state->flags;
    uint8_t flags = state->flags;
    // As in the Android change: only the paused signature permits clearing
    // abandonment; a standalone end/abandon bit is preserved. Never set phase.
    if (flags & 0x20u) flags &= (uint8_t)~0x22u;
    return flags | 0x40u;
}

bool PHTimerCanDismiss(const PHTimerState *state) {
    return PHTimerEligible(state) && state->inputQuiet && state->phase == 58 &&
        state->pending == 32 && state->dialog;
}
