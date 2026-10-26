#import "PHMemory.h"
#include <limits.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

BOOL PHMemoryHasProtection(uintptr_t address, size_t length, vm_prot_t required,
                           vm_prot_t forbidden) {
    if (address == 0 || length == 0 || length > UINTPTR_MAX - address) {
        return NO;
    }
    mach_vm_address_t cursor = address;
    mach_vm_address_t end = address + length;
    while (cursor < end) {
        mach_vm_address_t region = cursor;
        mach_vm_size_t size = 0;
        vm_region_basic_info_data_64_t info = {0};
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object = MACH_PORT_NULL;
        kern_return_t result = mach_vm_region(mach_task_self(), &region, &size,
                        VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object);
        if (object != MACH_PORT_NULL) {
            mach_port_deallocate(mach_task_self(), object);
        }
        if (result != KERN_SUCCESS || region > cursor || size == 0 ||
            size > UINT64_MAX - region || (info.protection & required) != required ||
            (info.protection & forbidden) != 0 || region + size <= cursor) {
            return NO;
        }
        cursor = MIN(region + size, end);
    }
    return YES;
}

BOOL PHReadMemory(uintptr_t address, void *output, size_t length) {
    if (!output || !PHMemoryHasProtection(address, length, VM_PROT_READ, 0)) {
        return NO;
    }
    mach_vm_size_t actual = 0;
    return mach_vm_read_overwrite(mach_task_self(), address, length,
                                  (mach_vm_address_t)(uintptr_t)output, &actual) == KERN_SUCCESS &&
           actual == length;
}

BOOL PHWriteData(uintptr_t address, const void *input, size_t length) {
    // Never change page permissions and never write executable mappings.
    if (!input || length > UINT_MAX ||
        !PHMemoryHasProtection(address, length, VM_PROT_READ | VM_PROT_WRITE,
                                VM_PROT_EXECUTE)) {
        return NO;
    }
    return mach_vm_write(mach_task_self(), address, (vm_offset_t)(uintptr_t)input,
                          (mach_msg_type_number_t)length) == KERN_SUCCESS;
}
