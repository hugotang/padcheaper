#import <UIKit/UIKit.h>

@interface PHOrbRecoveryEditor : UIViewController
- (instancetype)initWithSettings:(NSDictionary *)settings save:(NSString *(^)(NSDictionary *settings))save;
@end
