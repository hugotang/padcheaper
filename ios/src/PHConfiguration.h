#import <Foundation/Foundation.h>
#include "PHBattlePolicy.h"

FOUNDATION_EXPORT NSDictionary *PHDefaultConfiguration(void);
FOUNDATION_EXPORT BOOL PHConfigurationIsValid(NSDictionary *configuration);
FOUNDATION_EXPORT NSDictionary *PHConfigurationBattleFilters(NSDictionary *configuration);
FOUNDATION_EXPORT NSDictionary *PHConfigurationOrbSettings(NSDictionary *configuration);
FOUNDATION_EXPORT PHBattlePolicy PHConfigurationBattlePolicy(NSDictionary *configuration);
FOUNDATION_EXPORT NSArray<NSNumber *> *PHConfigurationReadFilterText(NSString *text, uint32_t maximum);
