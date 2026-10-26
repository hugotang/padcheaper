#import <Foundation/Foundation.h>
#include <mach-o/loader.h>
#include <stdint.h>

FOUNDATION_EXPORT const struct mach_header_64 *PHResolveGuestHeader(void);
FOUNDATION_EXPORT NSString *PHGuestBundleIdentifier(void);
FOUNDATION_EXPORT BOOL PHValidateGuest(const struct mach_header_64 *header, NSString **reason);
FOUNDATION_EXPORT BOOL PHAddressIsGuestCode(uintptr_t address, const struct mach_header_64 *header);
