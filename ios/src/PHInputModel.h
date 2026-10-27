#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PHInputSlotCount = 5,
    PHInputRecordSize = 0x108,
    PHInputFrameOffset = 0x528,
    PHInputDataSize = 0xa52
};

typedef struct {
    bool valid;
    uint8_t rawCount, frameCount, rawActiveMask, frameActiveMask;
    // Four event bytes: began, held, ended, repeat; ORed across all five slots.
    uint32_t rawFlags, frameFlags;
} PHInputState;

bool PHInputDecode(const uint8_t *bytes, size_t length, PHInputState *state);
bool PHInputIsQuiet(const PHInputState *state);

#ifdef __cplusplus
}
#endif
