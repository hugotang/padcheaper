#import "PHTarget.h"
#import "PHMemory.h"
#include "PHTargetGenerated.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/machine.h>
#include <string.h>

static BOOL PHHeaderHasUUID(const struct mach_header_64 *header) {
    struct mach_header_64 value;
    uintptr_t address = (uintptr_t)header;
    if (!PHReadMemory(address, &value, sizeof(value)) || value.magic != MH_MAGIC_64 ||
        value.cputype != CPU_TYPE_ARM64 || value.ncmds > 512 || value.sizeofcmds > 65536 ||
        value.sizeofcmds < sizeof(struct load_command) ||
        !PHMemoryHasProtection(address + sizeof(value), value.sizeofcmds, VM_PROT_READ, 0)) {
        return NO;
    }
    uintptr_t cursor = address + sizeof(value);
    uintptr_t end = cursor + value.sizeofcmds;
    for (uint32_t i = 0; i < value.ncmds; i++) {
        struct load_command command;
        if (end - cursor < sizeof(command) || !PHReadMemory(cursor, &command, sizeof(command)) ||
            command.cmdsize < sizeof(command) || command.cmdsize > end - cursor) {
            return NO;
        }
        if (command.cmd == LC_UUID && command.cmdsize >= sizeof(struct uuid_command)) {
            struct uuid_command uuid;
            return PHReadMemory(cursor, &uuid, sizeof(uuid)) &&
                   memcmp(uuid.uuid, PHTargetUUID, sizeof(PHTargetUUID)) == 0;
        }
        cursor += command.cmdsize;
    }
    return NO;
}

const struct mach_header_64 *PHResolveGuestHeader(void) {
    // LiveContainer can translate dyld indexes. Always re-check the UUID.
    void *symbol = dlsym(RTLD_DEFAULT, "getGuestAppHeader");
    if (symbol) {
        void *(*helper)(void) = (void *(*)(void))symbol;
        const struct mach_header_64 *header = helper();
        if (PHHeaderHasUUID(header)) {
            return header;
        }
    }
    uint32_t count = MIN(_dyld_image_count(), 4096u);
    const struct mach_header_64 *found = NULL;
    for (uint32_t index = 0; index < count; index++) {
        const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(index);
        if (PHHeaderHasUUID(header)) {
            if (found && found != header) {
                return NULL;
            }
            found = header;
        }
    }
    return found;
}

BOOL PHAddressIsGuestCode(uintptr_t address, const struct mach_header_64 *header) {
    uintptr_t start = (uintptr_t)header + 0x4000u;
    uintptr_t end = (uintptr_t)header + 0x1110000u;
    return address >= start && address < end &&
           PHMemoryHasProtection(address, 4, VM_PROT_READ | VM_PROT_EXECUTE, 0);
}

BOOL PHValidateGuest(const struct mach_header_64 *header, NSString **reason) {
    NSBundle *bundle = NSBundle.mainBundle;
    if (![bundle.bundleIdentifier isEqualToString:@(PHTargetBundleID)] ||
        ![bundle.infoDictionary[@"CFBundleShortVersionString"] isEqualToString:@(PHTargetVersion)] ||
        ![bundle.infoDictionary[@"CFBundleVersion"] isEqualToString:@(PHTargetBuild)]) {
        if (reason) *reason = @"Bundle identifier, version or iOS build does not match";
        return NO;
    }
    struct mach_header_64 value;
    if (!PHHeaderHasUUID(header) || !PHReadMemory((uintptr_t)header, &value, sizeof(value)) ||
        (value.cpusubtype & ~CPU_SUBTYPE_MASK) != CPU_SUBTYPE_ARM64_ALL ||
        (value.filetype != MH_EXECUTE && value.filetype != MH_DYLIB)) {
        if (reason) *reason = @"Guest UUID or Mach-O architecture does not match";
        return NO;
    }
    for (size_t i = 0; i < PHTextGuardCount; i++) {
        const PHTextGuard *guard = &PHTextGuards[i];
        uint8_t bytes[64];
        uintptr_t address = (uintptr_t)header + guard->rva;
        if (!PHAddressIsGuestCode(address, header) ||
            !PHReadMemory(address, bytes, guard->length) ||
            memcmp(bytes, guard->bytes, guard->length) != 0) {
            if (reason) *reason = [NSString stringWithFormat:@"Text guard %zu does not match", i];
            return NO;
        }
    }
    if (reason) *reason = @"Target identity and text guards match";
    return YES;
}
