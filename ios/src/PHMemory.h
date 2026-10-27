#import <Foundation/Foundation.h>
#include <stdint.h>
#include <stddef.h>
#include <mach/vm_prot.h>

FOUNDATION_EXPORT BOOL PHReadMemory(uintptr_t address, void *output, size_t length);
FOUNDATION_EXPORT BOOL PHMemoryHasProtection(uintptr_t address, size_t length, vm_prot_t required,
                           vm_prot_t forbidden);
FOUNDATION_EXPORT BOOL PHWriteData(uintptr_t address, const void *input, size_t length);

static inline BOOL PHReadU8(uintptr_t address, uint8_t *value) {
    return PHReadMemory(address, value, sizeof(*value));
}
static inline BOOL PHReadU16(uintptr_t address, uint16_t *value) {
    return PHReadMemory(address, value, sizeof(*value));
}
static inline BOOL PHReadU32(uintptr_t address, uint32_t *value) {
    return PHReadMemory(address, value, sizeof(*value));
}
static inline BOOL PHReadPointer(uintptr_t address, uintptr_t *value) {
    return PHReadMemory(address, value, sizeof(*value));
}
