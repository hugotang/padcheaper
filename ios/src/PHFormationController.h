#import <UIKit/UIKit.h>
#import "PHOverlay.h"

@interface PHFormationController : UIViewController
- (instancetype)initWithTeam:(NSDictionary *)team library:(NSDictionary *)library battle:(NSDictionary *)battle
    action:(PHOverlayAction)action close:(dispatch_block_t)close;
- (void)updateBattle:(NSDictionary *)battle;
@end
