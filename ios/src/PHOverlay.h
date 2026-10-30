#import <Foundation/Foundation.h>

#define PH_RUNTIME_VERSION @"0.5.10-orbs"

typedef NSDictionary *(^PHOverlaySnapshot)(void);
typedef void (^PHOverlayAction)(NSString *action, id value);

// All callbacks execute on the main thread. The overlay does not become key.
FOUNDATION_EXPORT void PHShowOverlay(PHOverlaySnapshot snapshot, PHOverlayAction action);
