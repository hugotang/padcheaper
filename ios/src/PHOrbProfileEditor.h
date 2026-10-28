#import <UIKit/UIKit.h>

@interface PHOrbProfileEditor : UIViewController
- (instancetype)initWithLibrary:(NSDictionary *)library settings:(NSDictionary *)settings
    formations:(NSDictionary *)formations context:(NSDictionary *)context
    save:(NSString *(^)(NSDictionary *library, NSDictionary *settings))save;
@end
