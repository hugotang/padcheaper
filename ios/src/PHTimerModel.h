#pragma once
#include "PHBattleModel.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    PHBattleIdentity identity;
    uintptr_t countdown, dialog;
    bool present, valuesValid;
    PHInputState input;
    int16_t mode, limitSeconds;
    uint32_t multiplayer, sceneID, phase, pending;
    uint8_t flags;
} PHTimerState;

bool PHTimerEligible(const PHTimerState *state);
uint8_t PHTimerAutomaticFlags(const PHTimerState *state);
bool PHTimerCanDismiss(const PHTimerState *state);

#ifdef __cplusplus
}
#endif
