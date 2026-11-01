#import <UIKit/UIKit.h>
#import "PHOverlay.h"

@interface PHOrbController : UIViewController
@property(nonatomic, copy) void (^compareDungeon)(NSDictionary *report);
- (instancetype)initWithSnapshot:(PHOverlaySnapshot)snapshot action:(PHOverlayAction)action
                           close:(void (^)(void))close;
- (void)refresh;
@end
