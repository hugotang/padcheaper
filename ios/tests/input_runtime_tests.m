#import "PHInput.h"
#include <stdio.h>

static unsigned failures, pointerReads;
static const uintptr_t Header = 0x100000000;
static uint64_t storage[(PHInputDataSize + 7) / 8];
static uintptr_t manager;
static BOOL unreadable, changedPointer;

static void Check(BOOL condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

BOOL PHReadMemory(uintptr_t address, void *output, size_t length) {
    if (address == Header + 0x2ac6fd8 && length == sizeof(uintptr_t)) {
        ++pointerReads;
        uintptr_t value = changedPointer && pointerReads > 1 ? manager + 8 : manager;
        memcpy(output, &value, length);
        return YES;
    }
    if (!unreadable && address == (uintptr_t)storage && length == PHInputDataSize) {
        memcpy(output, storage, length);
        return YES;
    }
    // The old GameMain +92 location contains nonzero initial configuration.
    if (address == Header + 0x2abcdf0 + 92 && length == 4) {
        uint32_t initial = 0x02050103;
        memcpy(output, &initial, length);
        return YES;
    }
    return NO;
}

BOOL PHMemoryHasProtection(uintptr_t address, size_t length, vm_prot_t required, vm_prot_t forbidden) {
    return !unreadable && address == (uintptr_t)storage && length == PHInputDataSize &&
           required == VM_PROT_READ && forbidden == VM_PROT_EXECUTE;
}

int main(void) {
    @autoreleasepool {
        PHInputState state = {0};
        manager = (uintptr_t)storage;
        Check(PHReadGameInput(Header, &state) && PHInputIsQuiet(&state) && pointerReads == 2,
              "reader uses the native touch manager, not nonzero GameMain settings, and rechecks its identity");
        uint8_t *bytes = (uint8_t *)storage;
        bytes[4 * 264 + 214] = 1;
        Check(PHReadGameInput(Header, &state) && !PHInputIsQuiet(&state) && state.rawActiveMask == 16,
              "reader includes pending input in the fifth slot");
        bytes[4 * 264 + 214] = 0;
        bytes[215] = 1; bytes[1320 + 215] = 1;
        Check(PHReadGameInput(Header, &state) && !PHInputIsQuiet(&state), "reader retains release-frame input");
        bytes[1320 + 215] = 0;
        Check(PHReadGameInput(Header, &state) && PHInputIsQuiet(&state), "release consumption unlocks reader while raw ended stays set");
        pointerReads = 0; changedPointer = YES;
        Check(!PHReadGameInput(Header, &state) && !PHInputIsQuiet(&state), "manager replacement cannot reuse a snapshot");
        changedPointer = NO; unreadable = YES;
        Check(!PHReadGameInput(Header, &state) && !state.valid, "unreadable input fails closed");
        unreadable = NO; manager = 0;
        Check(!PHReadGameInput(Header, &state) && !state.valid, "missing manager fails closed");
        manager = UINTPTR_MAX - 7;
        Check(!PHReadGameInput(Header, &state) && !state.valid, "overflowing manager range fails closed");
        Check(!PHReadGameInput(UINTPTR_MAX, &state) && !state.valid, "overflowing header fails closed");
        Check(!PHReadGameInput(Header, NULL), "null output fails closed");
        printf("Input reader tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
