#import "PHTarget.h"
#import "PHMemory.h"
#import "PHIdentity.h"
#include "PHTargetGenerated.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/machine.h>
#include <objc/runtime.h>
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
    if (found) return found;
    // Some LC modes hide dyld images. An Objective-C method can still identify
    // its image through dladdr; it must pass the same UUID and text guards.
    Class view = objc_getClass("ViewController");
    Method method = view ? class_getInstanceMethod(view, sel_registerName("drawFrame")) : NULL;
    Dl_info image = {0};
    if (method && dladdr((const void *)method_getImplementation(method), &image)) {
        const struct mach_header_64 *header = image.dli_fbase;
        if (PHHeaderHasUUID(header)) return header;
    }
    return NULL;
}

static NSDictionary *PHLiveContainerMetadata(NSBundle *bundle) {
    NSDictionary *metadata = [NSDictionary dictionaryWithContentsOfFile:
                             [bundle.bundlePath stringByAppendingPathComponent:@"LCAppInfo.plist"]];
    return metadata ?: @{};
}

NSString *PHGuestBundleIdentifier(void) {
    NSBundle *bundle = NSBundle.mainBundle;
    return PHResolvedBundleIdentifier(bundle.infoDictionary, PHLiveContainerMetadata(bundle));
}

BOOL PHAddressIsGuestCode(uintptr_t address, const struct mach_header_64 *header) {
    uintptr_t start = (uintptr_t)header + 0x4000u;
    uintptr_t end = (uintptr_t)header + 0x1110000u;
    return address >= start && address < end &&
           PHMemoryHasProtection(address, 4, VM_PROT_READ | VM_PROT_EXECUTE, 0);
}

BOOL PHValidateGuest(const struct mach_header_64 *header, NSString **reason) {
    NSBundle *bundle = NSBundle.mainBundle;
    if (!PHBundleIdentityMatches(bundle.infoDictionary, PHLiveContainerMetadata(bundle),
                                 @(PHTargetBundleID), @(PHTargetVersion), @(PHTargetBuild), reason)) {
        return NO;
    }
    struct mach_header_64 value;
    if (!PHHeaderHasUUID(header) || !PHReadMemory((uintptr_t)header, &value, sizeof(value)) ||
        (value.cpusubtype & ~CPU_SUBTYPE_MASK) != CPU_SUBTYPE_ARM64_ALL ||
        (value.filetype != MH_EXECUTE && value.filetype != MH_DYLIB)) {
        if (reason) *reason = @"遊戲 UUID 或 Mach-O 架構不符";
        return NO;
    }
    for (size_t i = 0; i < PHTextGuardCount; i++) {
        const PHTextGuard *guard = &PHTextGuards[i];
        uint8_t bytes[64];
        uintptr_t address = (uintptr_t)header + guard->rva;
        if (!PHAddressIsGuestCode(address, header) ||
            !PHReadMemory(address, bytes, guard->length) ||
            memcmp(bytes, guard->bytes, guard->length) != 0) {
            if (reason) *reason = [NSString stringWithFormat:@"程式校驗第 %zu 項不符（RVA 0x%lx）",
                                  i + 1, (unsigned long)guard->rva];
            return NO;
        }
    }
    if (reason) *reason = @"遊戲版本、UUID 及程式校驗通過";
    return YES;
}
