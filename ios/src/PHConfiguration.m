#import "PHConfiguration.h"
#import <CoreFoundation/CoreFoundation.h>

NSDictionary *PHDefaultConfiguration(void) {
    return @{@"schema": @1, @"experimentalWrites": @NO,
             @"features": @{@"skin": @NO, @"bgm": @NO, @"timerFix": @NO},
             @"apiHosts": @[], @"apiPaths": @[]};
}

static BOOL PHIsBoolean(id value) {
    return [value isKindOfClass:NSNumber.class] &&
           CFGetTypeID((__bridge CFTypeRef)value) == CFBooleanGetTypeID();
}

BOOL PHConfigurationIsValid(NSDictionary *configuration) {
    if (![configuration isKindOfClass:NSDictionary.class]) return NO;
    id schema = configuration[@"schema"];
    NSDictionary *features = configuration[@"features"];
    if (![schema isKindOfClass:NSNumber.class] || PHIsBoolean(schema) || ![schema isEqual:@1] ||
        !PHIsBoolean(configuration[@"experimentalWrites"]) ||
        ![features isKindOfClass:NSDictionary.class]) return NO;
    for (NSString *key in @[@"skin", @"bgm", @"timerFix"]) {
        if (!PHIsBoolean(features[key])) return NO;
    }
    for (NSString *key in @[@"apiHosts", @"apiPaths"]) {
        NSArray *values = configuration[key];
        if (![values isKindOfClass:NSArray.class] || values.count > 16) return NO;
        for (id value in values) {
            if (![value isKindOfClass:NSString.class] || [value length] == 0 || [value length] > 1024) return NO;
            if ([key isEqualToString:@"apiPaths"] && (![value hasPrefix:@"/"] ||
                [value rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"?#"]].location != NSNotFound)) return NO;
        }
    }
    return YES;
}
