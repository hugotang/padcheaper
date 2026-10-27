#include "PHInputModel.h"
#include <string.h>

bool PHInputDecode(const uint8_t *bytes, size_t length, PHInputState *state) {
    if (!state) return false;
    memset(state, 0, sizeof(*state));
    if (!bytes || length != PHInputDataSize) return false;
    state->rawCount = bytes[0xa50];
    state->frameCount = bytes[0xa51];
    bool valid = state->rawCount <= PHInputSlotCount && state->frameCount <= PHInputSlotCount;
    for (size_t slot = 0; slot < PHInputSlotCount; ++slot) {
        const uint8_t *raw = bytes + slot * PHInputRecordSize;
        const uint8_t *frame = raw + PHInputFrameOffset;
        uint32_t rawFlags = 0, frameFlags = 0;
        for (size_t event = 0; event < 4; ++event) {
            rawFlags |= (uint32_t)raw[0xd5 + event] << (8 * event);
            frameFlags |= (uint32_t)frame[0xd5 + event] << (8 * event);
            valid &= raw[0xd5 + event] <= 1 && frame[0xd5 + event] <= 1;
        }
        state->rawFlags |= rawFlags;
        state->frameFlags |= frameFlags;
        valid &= raw[0xdc] <= 1 && frame[0xdb] <= 1;
        bool contact = false;
        for (size_t byte = 0; byte < 8; ++byte) contact |= raw[0xf8 + byte] != 0;
        // Raw release/repeat bytes can outlive their event. Began/held, a live
        // UITouch, or the deferred short-tap latch still require processing.
        if ((rawFlags & 0xffffu) || contact || raw[0xdc]) state->rawActiveMask |= (uint8_t)(1u << slot);
        // sub_100069CAC clears frame began/end/repeat each update. Its +0xdb
        // latch also tracks a held touch whose visible events were suppressed.
        if (frameFlags || frame[0xdb]) state->frameActiveMask |= (uint8_t)(1u << slot);
    }
    state->valid = valid;
    return valid;
}

bool PHInputIsQuiet(const PHInputState *state) {
    return state && state->valid && !state->rawActiveMask && !state->frameActiveMask;
}
