#import <UIKit/UIKit.h>
#import "PHOverlay.h"
#include <dlfcn.h>

static NSString *PHProbeRoot;
static NSMutableArray<NSString *> *PHProbeFailures;
static UIWindow *PHProbeLegacyOverlay;

static NSDictionary *PHProbeJSON(NSString *name) {
    NSData *data = [NSData dataWithContentsOfFile:[PHProbeRoot stringByAppendingPathComponent:name]];
    return data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
}

static BOOL PHProbeLegacy(void) {
    return [NSBundle.mainBundle.infoDictionary[@"PHProbeLegacy"] boolValue];
}

static void PHProbeCheck(BOOL condition, NSString *message) {
    if (!condition) [PHProbeFailures addObject:message];
}

static void PHProbeReport(NSString *phase) {
    NSDictionary *report = @{@"phase": phase, @"success": @(PHProbeFailures.count == 0), @"failures": PHProbeFailures};
    NSData *data = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];
    [data writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/overlay-probe.json"] atomically:YES];
}

static UIView *PHProbeFind(UIView *root, NSString *identifier) {
    if ([root.accessibilityIdentifier isEqualToString:identifier]) return root;
    for (UIView *child in root.subviews) {
        UIView *found = PHProbeFind(child, identifier);
        if (found) return found;
    }
    return nil;
}

static UIWindow *PHProbeOverlay(UIWindowScene *scene) {
    if (PHProbeLegacy()) {
        if (PHProbeFind(PHProbeLegacyOverlay, @"padcheaper.bubble")) return PHProbeLegacyOverlay;
    }
    for (UIWindow *window in scene.windows) {
        if (PHProbeFind(window, @"padcheaper.bubble")) return window;
    }
    return nil;
}

@interface PHProbeController : UIViewController
@end
@implementation PHProbeController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.09 green:0.24 blue:0.28 alpha:1];
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(24, 80, 260, 80)];
    label.text = @"PADCheaper\nSimulator load probe";
    label.numberOfLines = 2;
    label.textColor = UIColor.whiteColor;
    label.font = [UIFont systemFontOfSize:22 weight:UIFontWeightSemibold];
    [self.view addSubview:label];
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
@end

@interface PHProbeSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow *window;
- (void)startWithWindowScene:(UIWindowScene * _Nullable)scene;
- (void)exerciseOverlay;
- (void)verifyRejectedTarget;
- (void)verifyLandscape;
@end

@implementation PHProbeSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    [self startWithWindowScene:(UIWindowScene *)scene];
}
- (void)startWithWindowScene:(UIWindowScene * _Nullable)scene {
    self.window = scene ? [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene] :
                          [[UIWindow alloc] initWithFrame:CGRectMake(0, 0, 393, 852)];
    self.window.rootViewController = [PHProbeController new];
    [self.window makeKeyAndVisible];
    if (PHProbeLegacy()) {
        [NSNotificationCenter.defaultCenter addObserverForName:UIWindowDidBecomeVisibleNotification object:nil
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {
                UIWindow *window = notification.object;
                if (window != self.window && window.windowLevel > UIWindowLevelNormal) PHProbeLegacyOverlay = window;
            }];
    }
    PHProbeRoot = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/PADHelper"];
    PHProbeFailures = [NSMutableArray array];
    [NSFileManager.defaultManager createDirectoryAtPath:PHProbeRoot withIntermediateDirectories:YES attributes:nil error:NULL];
    NSDictionary *endpoint = @{@"schema": @1, @"host": @"example.invalid", @"path": @"/api.php", @"queryIncluded": @NO};
    [[NSJSONSerialization dataWithJSONObject:endpoint options:0 error:NULL]
     writeToFile:[PHProbeRoot stringByAppendingPathComponent:@"endpoint.json"] atomically:YES];
    NSString *library = [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Frameworks/PADCheaperTest.dylib"];
    void *handle = dlopen(library.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    const char *error = handle ? NULL : dlerror();
    PHProbeCheck(handle != NULL, error ? @(error) : @"Simulator dylib could not be loaded");
    [self performSelector:@selector(exerciseOverlay) withObject:nil afterDelay:3];
    [self performSelector:@selector(verifyRejectedTarget) withObject:nil afterDelay:23];
}

- (void)exerciseOverlay {
    UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
    PHProbeCheck(overlay != nil && !overlay.hidden, @"Floating GUI must appear before the target gate finishes");
    PHProbeCheck(!overlay.isKeyWindow && self.window.isKeyWindow, @"Overlay must leave the guest window key");
    NSDictionary *config = PHProbeJSON(@"config.json");
    PHProbeCheck(config != nil && [config[@"experimentalWrites"] isEqual:@NO], @"Config must exist before UUID validation");
    NSDictionary *status = PHProbeJSON(@"status.json");
    PHProbeCheck([status[@"loaded"] isEqual:@YES] && [status[@"targetMatched"] isEqual:@NO], @"Loaded status must be independent of feature activation");
    UIButton *bubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    CGPoint point = [bubble convertPoint:CGPointMake(28, 28) toView:overlay];
    PHProbeCheck([overlay hitTest:point withEvent:nil] == bubble, @"Floating button must receive touches");
    PHProbeCheck([overlay hitTest:CGPointMake(20, CGRectGetHeight(overlay.bounds) - 50) withEvent:nil] == nil,
                 @"Touches outside the button must pass through");
    [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIView *panel = PHProbeFind(overlay, @"padcheaper.panel");
    PHProbeCheck(panel != nil && !panel.hidden, @"Floating button must open the panel");
    for (NSString *key in @[@"experimentalWrites", @"skin", @"bgm", @"timerFix"]) {
        UISwitch *toggle = (UISwitch *)PHProbeFind(overlay, [@"padcheaper." stringByAppendingString:key]);
        PHProbeCheck(toggle != nil && toggle.enabled, [@"Switch unavailable: " stringByAppendingString:key]);
        toggle.on = YES;
        [toggle sendActionsForControlEvents:UIControlEventValueChanged];
    }
    UIButton *adopt = (UIButton *)PHProbeFind(overlay, @"padcheaper.adoptEndpoint");
    PHProbeCheck(adopt.enabled, @"Observed endpoint must be available in diagnostic mode");
    [adopt sendActionsForControlEvents:UIControlEventTouchUpInside];
    config = PHProbeJSON(@"config.json");
    PHProbeCheck([config[@"experimentalWrites"] isEqual:@YES] && [config[@"features"][@"skin"] isEqual:@YES] &&
                 [config[@"features"][@"bgm"] isEqual:@YES] && [config[@"features"][@"timerFix"] isEqual:@YES],
                 @"GUI switches must persist to config.json");
    PHProbeCheck([config[@"apiHosts"] isEqual:@[@"example.invalid"]] && [config[@"apiPaths"] isEqual:@[@"/api.php"]],
                 @"GUI must persist only the endpoint host and path");
    UIButton *copy = (UIButton *)PHProbeFind(overlay, @"padcheaper.copy");
    [copy sendActionsForControlEvents:UIControlEventTouchUpInside];
    PHProbeCheck([UIPasteboard.generalPasteboard.string containsString:@"Loaded: YES"], @"Copy diagnostics must work");
    UIButton *close = (UIButton *)PHProbeFind(overlay, @"padcheaper.close");
    [close sendActionsForControlEvents:UIControlEventTouchUpInside];
    PHProbeCheck(panel.hidden && !bubble.hidden, @"Close must restore the floating button");
    PHProbeReport(@"running");
}

- (void)verifyRejectedTarget {
    UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
    UIButton *bubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
    NSDictionary *status = PHProbeJSON(@"status.json");
    PHProbeCheck([status[@"tone"] isEqual:@"error"] && [status[@"targetMatched"] isEqual:@NO], @"Wrong UUID must show a visible error");
    PHProbeCheck([status[@"frameHook"] isEqual:@NO] && [status[@"networkHook"] isEqual:@NO] &&
                 [status[@"lifecycleHook"] isEqual:@NO] && [status[@"activeMask"] isEqual:@0],
                 @"GUI toggles must not bypass target validation");
    UILabel *label = (UILabel *)PHProbeFind(overlay, @"padcheaper.status");
    PHProbeCheck([label.text containsString:@"校驗未通過"], @"Panel must display the target failure reason");
    PHProbeCheck(self.window.isKeyWindow, @"Opening the panel must not steal key-window status");
    PHProbeReport(@"portrait");
    if (PHProbeLegacy()) {
        UIButton *retry = (UIButton *)PHProbeFind(overlay, @"padcheaper.retry");
        [retry sendActionsForControlEvents:UIControlEventTouchUpInside];
        PHProbeCheck(self.window.isKeyWindow, @"Retry must leave the legacy guest window key");
        PHProbeCheck([PHProbeJSON(@"status.json")[@"activeMask"] isEqual:@0], @"Retry cannot bypass the target gate");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 4 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ PHProbeReport(@"done"); });
    } else [self performSelector:@selector(verifyLandscape) withObject:nil afterDelay:8];
}

- (void)verifyLandscape {
    [self.window.windowScene requestGeometryUpdateWithPreferences:
     [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeLeft]
                                                    errorHandler:^(NSError *error) {
        PHProbeCheck(NO, [@"Landscape geometry: " stringByAppendingString:error.localizedDescription]);
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
        UIView *panel = PHProbeFind(overlay, @"padcheaper.panel");
        [overlay layoutIfNeeded];
        PHProbeCheck(CGRectGetWidth(overlay.bounds) > CGRectGetHeight(overlay.bounds), @"Overlay must rotate with the guest scene");
        PHProbeCheck(panel != nil && !panel.hidden && CGRectContainsRect(overlay.bounds, panel.frame), @"Landscape panel must remain on-screen");
        PHProbeCheck([overlay hitTest:CGPointMake(CGRectGetWidth(overlay.bounds) - 3, 3) withEvent:nil] == nil,
                     @"Touches outside the expanded panel must pass through");
        PHProbeReport(@"landscape");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 6 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ PHProbeReport(@"done"); });
    });
}
@end

@interface PHProbeAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) PHProbeSceneDelegate *legacyDelegate;
@end
@implementation PHProbeAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    if (PHProbeLegacy()) {
        self.legacyDelegate = [PHProbeSceneDelegate new];
        [self.legacyDelegate startWithWindowScene:nil];
        self.window = self.legacyDelegate.window;
    }
    return YES;
}
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session
                             options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
    configuration.delegateClass = PHProbeSceneDelegate.class;
    return configuration;
}
@end

int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(PHProbeAppDelegate.class)); }
}
