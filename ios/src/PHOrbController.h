#import <UIKit/UIKit.h>
#import "PHOverlay.h"

@interface PHOrbController : UIViewController
- (instancetype)initWithSnapshot:(PHOverlaySnapshot)snapshot action:(PHOverlayAction)action
                           close:(void (^)(void))close;
- (void)refresh;
@end
