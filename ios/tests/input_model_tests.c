#include "PHInputModel.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

int main(void) {
    uint8_t bytes[PHInputDataSize] = {0};
    PHInputState state = {0};
    Check(PHInputDecode(bytes, sizeof(bytes), &state) && PHInputIsQuiet(&state), "initialized native touch manager is idle");
    for (size_t slot = 0; slot < 5; ++slot) {
        memset(bytes, 0, sizeof(bytes));
        uint8_t *raw = bytes + slot * 264;
        uint8_t *frame = raw + 1320;
        // Native began writes 0x01000101 at +213; ended writes 0x00010000.
        raw[213] = 1; raw[214] = 1; raw[216] = 1; raw[248] = 1;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state) &&
              state.rawActiveMask == (1u << slot) && state.rawFlags == 0x01000101u,
              "pending begin in every slot blocks before a frame consumes it, even with zero counts");
        raw[213] = 0; raw[216] = 0;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "held touch blocks after began is consumed");
        raw[214] = 0;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "live UITouch still blocks when input events are suppressed");
        raw[248] = 0; raw[215] = 1;
        frame[215] = 1;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state) &&
              state.rawActiveMask == 0 && state.frameActiveMask == (1u << slot),
              "release frame blocks while raw ended is already inactive");
        frame[215] = 0;
        bytes[2640] = 1; bytes[2641] = 1;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && PHInputIsQuiet(&state) && state.rawFlags == 0x10000u,
              "consumed release becomes idle despite the persistent raw ended byte and event counts");
        raw[215] = 0; raw[220] = 1;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "very short tap waits for its deferred begin");
        raw[220] = 0;
        for (size_t event = 213; event <= 216; ++event) {
            frame[event] = 1;
            Check(PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "each processed frame event blocks");
            frame[event] = 0;
        }
        frame[219] = 1;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "suppressed frame retains its held latch");
        frame[219] = 0; frame[248] = 1;
        Check(PHInputDecode(bytes, sizeof(bytes), &state) && PHInputIsQuiet(&state), "a copied frame pointer is not a live raw contact");
    }
    memset(bytes, 0, sizeof(bytes));
    bytes[2640] = 6;
    Check(!PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "out-of-range raw count fails closed");
    bytes[2640] = 0; bytes[2641] = 255;
    Check(!PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "invalid signed frame count fails closed");
    bytes[2641] = 0; bytes[1320 + 214] = 2;
    Check(!PHInputDecode(bytes, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "malformed boolean event fails closed");
    Check(!PHInputDecode(bytes, sizeof(bytes) - 1, &state) && !state.valid, "truncated native snapshot cannot reuse prior input state");
    Check(!PHInputDecode(NULL, sizeof(bytes), &state) && !PHInputIsQuiet(&state), "missing snapshot is not quiet");
    Check(!PHInputDecode(bytes, sizeof(bytes), NULL) && !PHInputIsQuiet(NULL), "null output/state is rejected");
    printf("Input model tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
