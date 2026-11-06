#pragma once
#import <Foundation/Foundation.h>
#include "PHTargetGenerated.h"

// Data-only compatibility for validated schema-1 profiles, plans and decoded
// account deck records. This must never be used to identify a running game or
// authorize a native read/write; those require the exact current runtime gate.
static inline BOOL PHDataTargetIsSupported(id target) {
    return [target isKindOfClass:NSString.class] &&
        ([target isEqualToString:@(PHTargetID)] ||
         [target isEqualToString:@"jp.gungho.pad-ios-23.6.0-106878"]);
}
