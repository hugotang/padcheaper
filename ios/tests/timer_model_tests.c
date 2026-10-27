#include "PHTimerModel.h"
#include <stdio.h>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

int main(void) {
    PHTimerState state = {.present = true, .valuesValid = true, .inputQuiet = true,
        .countdown = 1, .dialog = 2, .limitSeconds = 3600, .phase = 2, .pending = 32};
    state.flags = 0x22;
    Check(PHTimerAutomaticFlags(&state) == 0x40, "paused restore signature clears pause/abandon and keeps alert latch");
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
    state.dialog = 2; state.inputQuiet = false;
    Check(!PHTimerCanDismiss(&state), "active native input prevents manual continuation");
    state.inputQuiet = true;
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        PHTimerState invalid = state;
        if (scenario == 0) invalid.limitSeconds = 0;
        if (scenario == 1) invalid.multiplayer = 2;
        if (scenario == 2) invalid.mode = 7;
        if (scenario == 3) invalid.valuesValid = false;
        if (scenario == 4) invalid.sceneID = 0x80386;
        Check(!PHTimerCanDismiss(&invalid), "unsupported countdown/mode/protected state prevents manual continuation");
        invalid.phase = 2;
        Check(PHTimerAutomaticFlags(&invalid) == invalid.flags, "unsupported state prevents automatic flag writes");
    }
    printf("Timer policy tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
