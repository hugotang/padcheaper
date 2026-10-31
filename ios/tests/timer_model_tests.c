#include "PHTimerModel.h"
#include <stdio.h>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

int main(void) {
    PHTimerState state = {.present = true, .valuesValid = true, .input = {.valid = true},
        .countdown = 1, .dialog = 2, .limitSeconds = 3600, .phase = 2, .pending = 32};
    state.flags = 0x22;
    Check(PHTimerAutomaticFlags(&state) == 0x40, "paused restore signature clears pause/abandon and keeps alert latch");
    state.mode = 1;
    Check(PHTimerAutomaticFlags(&state) == 0x40, "reported category 1 uses the same solo timer policy");
    state.mode = 12;
    Check(PHTimerAutomaticFlags(&state) == 0x40, "timer uses the same all-mode default as battle replacement");
    state.flags = 0x82;
    Check(PHTimerAutomaticFlags(&state) == 0xc2, "standalone abandonment and unrelated bits remain intact");
    for (unsigned phase = 0; phase <= 80; ++phase) {
        state.phase = phase; state.flags = 0x22;
        Check(PHTimerAutomaticFlags(&state) == (phase == 2 || phase == 30 ? 0x40 : 0x22),
              "automatic writes occur only in the two supported gameplay phases");
        Check(state.phase == phase && state.pending == 32, "automatic policy never changes current or pending phase");
    }
    state.phase = 58;
    Check(PHTimerCanDismiss(&state), "only an existing timed exit popup can be dismissed");
    state.pending = 2;
    Check(!PHTimerCanDismiss(&state), "time tutorial/notice popup is not an exit popup");
    state.pending = 32; state.dialog = 0;
    Check(!PHTimerCanDismiss(&state), "a popup object must exist");
    state.dialog = 2; state.input.rawActiveMask = 1;
    Check(!PHTimerCanDismiss(&state), "active native input prevents manual continuation");
    uint8_t inputBytes[PHInputDataSize] = {0};
    inputBytes[215] = 1; inputBytes[1320 + 215] = 1;
    Check(PHInputDecode(inputBytes, sizeof(inputBytes), &state.input) && !PHTimerCanDismiss(&state),
          "manual continuation waits for the native release frame");
    inputBytes[1320 + 215] = 0;
    Check(PHInputDecode(inputBytes, sizeof(inputBytes), &state.input) && PHTimerCanDismiss(&state),
          "persistent raw ended flag does not permanently block manual continuation");
    state.input.valid = false;
    Check(!PHTimerCanDismiss(&state), "missing input prevents manual continuation");
    state.phase = 2; state.flags = 0x22;
    Check(PHTimerAutomaticFlags(&state) == 0x40, "input guard does not change automatic flag-only policy");
    state.phase = 58; state.input.valid = true;
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        PHTimerState invalid = state;
        if (scenario == 0) invalid.limitSeconds = 0;
        if (scenario == 1) invalid.multiplayer = 2;
        if (scenario == 2) { invalid.mode = 7; invalid.policy.configured = true; }
        if (scenario == 3) invalid.valuesValid = false;
        if (scenario == 4) invalid.sceneID = 0x80386;
        Check(!PHTimerCanDismiss(&invalid), "unsupported countdown/mode/protected state prevents manual continuation");
        invalid.phase = 2;
        Check(PHTimerAutomaticFlags(&invalid) == invalid.flags, "unsupported state prevents automatic flag writes");
    }
    state.mode = 12; state.phase = 58;
    state.policy = (PHBattlePolicy){.configured = true, .modes = {.allowAll = true},
        .sources = {.allowAll = true}, .multiplayer = {.allowAll = true}};
    state.sourceMode = 3;
    Check(PHTimerCanDismiss(&state), "single-player timers still consume configured source filters");
    for (uint32_t multi = 0; multi <= 5; ++multi) {
        state.mode = multi ? 12 : 7; state.multiplayer = multi; state.phase = 58;
        Check(!PHTimerCanDismiss(&state), "legacy allow-all filters never enable multiplayer popup writes");
        state.phase = 2; state.flags = 0x22;
        Check(PHTimerAutomaticFlags(&state) == 0x22, "multiplayer and mode-7 transition states preserve timer flags");
    }
    state.multiplayer = 0;
    for (int16_t mode = 10; mode <= 11; ++mode) {
        state.mode = mode; state.multiplayerDisabled = false; state.phase = 58;
        Check(!PHTimerCanDismiss(&state), "competitive selector zero cannot enable timer popup writes");
        state.phase = 2;
        Check(PHTimerAutomaticFlags(&state) == 0x22, "competitive selector zero preserves native clock flags");
    }
    state.mode = 12; state.multiplayerDisabled = true;
    Check(PHTimerAutomaticFlags(&state) == 0x22, "a detected multiplayer timer remains disabled after selector reset");
    printf("Timer policy tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
