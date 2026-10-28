#import <UIKit/UIKit.h>

@interface PHOrbRuleEditor : UIViewController
- (instancetype)initWithRules:(NSArray *)rules save:(NSString *(^)(NSArray *rules))save;
@end
