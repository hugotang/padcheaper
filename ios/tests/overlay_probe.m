#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>
#import "PHOverlay.h"
#import "PHOrbController.h"
#import "PHOrbModel.h"
#import "PHOrbProfiles.h"
#import "PHConfiguration.h"
#import "PHFormationModel.h"
#import <dispatch/dispatch.h>
#include <dlfcn.h>
#include <math.h>

static NSString *PHProbeRoot;
static NSMutableArray<NSString *> *PHProbeFailures;
static UIWindow *PHProbeLegacyOverlay;
static NSHashTable<UIWindow *> *PHProbeLegacyWindows;
static NSDictionary *PHProbeRestartLibrary;
static NSMutableArray<NSDictionary *> *PHProbeGeometry;
static NSMutableArray<NSDictionary *> *PHProbeFloatingHits;
static void *PHProbeDylib;

@interface UIViewController (PHProbeOverlayState)
- (void)updateWithSnapshot:(NSDictionary *)snapshot;
@end

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

#include "orb_plan_storage_probe.inc"

static void PHProbeReport(NSString *phase) {
    NSDictionary *report = @{@"phase": phase, @"success": @(PHProbeFailures.count == 0), @"failures": PHProbeFailures,
        @"geometry":PHProbeGeometry ?: @[], @"floatingHitTests":PHProbeFloatingHits ?: @[]};
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

static void PHProbeOpenPanel(UIWindow *overlay) {
    UIView *panel = PHProbeFind(overlay, @"padcheaper.panel");
    UIButton *bubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    PHProbeCheck(panel && bubble, @"Settings entry and panel must exist");
    if (!panel || !bubble || !panel.hidden) return;
    if ([[overlay.rootViewController valueForKey:@"shortcutsCollapsed"] boolValue])
        [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
    [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
    PHProbeCheck(!panel.hidden, @"PC settings remain reachable through the folded shortcut entry");
}

static void PHProbeFloatingTargets(UIWindow *overlay) {
    NSMutableArray<UIView *> *controls = [NSMutableArray array];
    for (NSString *identifier in @[@"padcheaper.bubble", @"padcheaper.orbsOnceShortcut",
                                   @"padcheaper.orbsAutoShortcut", @"padcheaper.collapseShortcuts"]) {
        UIView *control = PHProbeFind(overlay, identifier);
        PHProbeCheck(control && !control.hidden && CGRectContainsRect(overlay.bounds, control.frame) &&
            CGRectGetWidth(control.bounds) >= 44 && CGRectGetHeight(control.bounds) >= 44,
            [NSString stringWithFormat:@"Floating control %@ must be a visible on-screen 44-point target", identifier]);
        if (!control) continue;
        CGPoint center = [control convertPoint:CGPointMake(CGRectGetMidX(control.bounds), CGRectGetMidY(control.bounds)) toView:overlay];
        UIView *hit = [overlay hitTest:center withEvent:nil];
        BOOL enabled = ![control isKindOfClass:UIControl.class] || [(UIControl *)control isEnabled];
        BOOL ownsHit = hit == control || [hit isDescendantOfView:control];
        // UIKit may omit a disabled button from hit testing. It must never
        // redirect that tap to a different control; enabled actions and Stop
        // must always retain their own on-screen target.
        PHProbeCheck(enabled ? ownsHit : (!hit || ownsHit),
            [NSString stringWithFormat:@"Floating control %@ hit mismatch: enabled=%d frame=%@ hit=%@",
                identifier, enabled, NSStringFromCGRect(control.frame), hit.accessibilityIdentifier ?: NSStringFromClass(hit.class)]);
        if (!PHProbeFloatingHits) PHProbeFloatingHits = [NSMutableArray array];
        [PHProbeFloatingHits addObject:@{@"control":identifier, @"enabled":@(enabled),
            @"frame":NSStringFromCGRect(control.frame), @"hit":hit.accessibilityIdentifier ?: NSStringFromClass(hit.class) ?: @"nil",
            @"ownsHit":@(ownsHit)}];
        for (UIView *previous in controls)
            PHProbeCheck(!CGRectIntersectsRect(previous.frame, control.frame), @"Floating controls must never overlap each other");
        [controls addObject:control];
    }
    UIView *status = PHProbeFind(overlay, @"padcheaper.orbsShortcutStatus");
    if (status && !status.hidden) {
        PHProbeCheck(CGRectContainsRect(overlay.bounds, status.frame), @"Shortcut status must remain on screen");
        for (UIView *control in controls)
            PHProbeCheck(!CGRectIntersectsRect(status.frame, control.frame), @"Shortcut status must not cover a floating control");
    }
}

static void PHProbeFloatingLayout(UIWindow *overlay) {
    NSValue *originalPosition = [overlay.rootViewController valueForKey:@"position"];
    for (NSArray<NSNumber *> *position in @[@[@0,@0], @[@1,@0], @[@0,@1], @[@1,@1],
                                           @[@0.5,@0], @[@0.5,@0.5], @[@0.5,@1]]) {
        [overlay.rootViewController setValue:[NSValue valueWithCGPoint:
            CGPointMake(position[0].doubleValue, position[1].doubleValue)] forKey:@"position"];
        [overlay.rootViewController.view setNeedsLayout];
        [overlay.rootViewController.view layoutIfNeeded];
        PHProbeFloatingTargets(overlay);
    }
    [overlay.rootViewController setValue:originalPosition forKey:@"position"];
    [overlay.rootViewController.view setNeedsLayout];
    [overlay.rootViewController.view layoutIfNeeded];
}

static UIWindow *PHProbeOverlay(UIWindowScene *scene) {
    if (PHProbeLegacy()) {
        if (!PHProbeLegacyOverlay.hidden && PHProbeFind(PHProbeLegacyOverlay, @"padcheaper.bubble")) return PHProbeLegacyOverlay;
        // Keyboard and other alert windows can arrive after the overlay. Keep
        // all observed candidates and select by the actual control hierarchy.
        for (UIWindow *window in PHProbeLegacyWindows) {
            if (!window.hidden && PHProbeFind(window, @"padcheaper.bubble")) {
                PHProbeLegacyOverlay = window;
                return window;
            }
        }
    }
    for (UIWindow *window in scene.windows) {
        if (PHProbeFind(window, @"padcheaper.bubble")) return window;
    }
    return nil;
}

static UIScrollView *PHProbeScrollParent(UIView *view) {
    UIView *parent = view.superview;
    while (parent && ![parent isKindOfClass:UIScrollView.class]) parent = parent.superview;
    return (UIScrollView *)parent;
}

static void PHProbeRevealBoard(UIWindow *overlay) {
    [overlay layoutIfNeeded];
    UIView *board = PHProbeFind(overlay, @"padcheaper.orbsBoard");
    UIScrollView *scroll = PHProbeScrollParent(board);
    PHProbeCheck(board && scroll, @"Board screenshot requires its scroll container");
    if (!board || !scroll) return;
    // The settings can push the entire board below the fixed action bar. A
    // screenshot is useful only after the board is visible, as after a swipe.
    CGRect boardRect = [board convertRect:board.bounds toView:scroll];
    CGFloat minimum = -scroll.adjustedContentInset.top;
    CGFloat maximum = MAX(minimum, scroll.contentSize.height - CGRectGetHeight(scroll.bounds) + scroll.adjustedContentInset.bottom);
    CGFloat desired = CGRectGetMinY(boardRect) - scroll.adjustedContentInset.top - 8;
    [scroll setContentOffset:CGPointMake(scroll.contentOffset.x, MIN(maximum, MAX(minimum, desired))) animated:NO];
    [overlay layoutIfNeeded];
    CGRect visible = UIEdgeInsetsInsetRect(scroll.bounds, scroll.adjustedContentInset);
    boardRect = [board convertRect:board.bounds toView:scroll];
    PHProbeCheck(!board.hidden && board.alpha > 0 && CGRectGetWidth(board.bounds) > 0 && CGRectGetHeight(board.bounds) > 0 &&
        CGRectContainsRect(CGRectInset(visible, -0.5, -0.5), boardRect),
        @"Board screenshot must show every row and column inside the scroll viewport");
    for (NSString *identifier in @[@"padcheaper.orbsClose", @"padcheaper.orbsRead", @"padcheaper.orbsExecute", @"padcheaper.orbsStart", @"padcheaper.orbsStop"]) {
        UIView *control = PHProbeFind(overlay, identifier);
        PHProbeCheck(control && CGRectContainsRect(overlay.bounds, [control convertRect:control.bounds toView:overlay]),
            @"Fixed orb actions remain visible after scrolling to the board");
    }
}

static void PHProbeRender(UIWindow *guest, UIWindow *overlay, NSString *name) {
    UIView *gameView = guest.rootViewController.view;
    // Presented sheets live beside the root view in UIKit's window hierarchy.
    // Rendering only the root silently omitted the editor being checked.
    UIView *overlayView = overlay;
    [gameView layoutIfNeeded];
    [overlayView layoutIfNeeded];
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:gameView.bounds];
    UIImage *image = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        [gameView drawViewHierarchyInRect:gameView.bounds afterScreenUpdates:YES];
        [overlayView drawViewHierarchyInRect:gameView.bounds afterScreenUpdates:YES];
    }];
    NSString *prefix = PHProbeLegacy() ? @"legacy" : @"overlay";
    NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:
                     [NSString stringWithFormat:@"Documents/%@-%@.png", prefix, name]];
    [UIImagePNGRepresentation(image) writeToFile:path atomically:YES];
}

typedef BOOL (^PHProbeCondition)(void);

static NSDictionary *PHProbeStep(NSString *name, PHProbeCondition ready, dispatch_block_t action) {
    return @{@"name":name, @"ready":[ready copy], @"action":[action copy]};
}

static BOOL PHProbePresented(UIViewController *parent, NSString *identifier, BOOL needsKeyboard) {
    UIViewController *editor = parent.presentedViewController;
    UIWindow *window = editor.viewIfLoaded.window;
    return editor && [editor.viewIfLoaded.accessibilityIdentifier isEqual:identifier] && window && !window.hidden &&
        !editor.isBeingPresented && !editor.isBeingDismissed && !editor.transitionCoordinator && !parent.transitionCoordinator &&
        (!needsKeyboard || window.isKeyWindow);
}

static BOOL PHProbeClosed(UIViewController *parent, UIWindow *guest) {
    return !parent.presentedViewController && !parent.transitionCoordinator && guest.isKeyWindow;
}

static void PHProbeRunSteps(NSArray<NSDictionary *> *steps, NSUInteger index, NSTimeInterval deadline,
                           UIWindow *guest, UIWindow *overlay) {
    NSDictionary *step = steps[index];
    PHProbeCondition ready = step[@"ready"];
    if (!ready()) {
        if (NSProcessInfo.processInfo.systemUptime >= deadline) {
            PHProbeCheck(NO, [NSString stringWithFormat:@"Timed out waiting for UI state: %@", step[@"name"]]);
            PHProbeRender(guest, overlay, @"orbs-transition-timeout");
            PHProbeReport(@"done");
            return;
        }
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 100 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            PHProbeRunSteps(steps, index, deadline, guest, overlay);
        });
        return;
    }
    PHProbeReport([@"orbs-" stringByAppendingString:step[@"name"]]);
    dispatch_block_t action = step[@"action"];
    action();
    if (index + 1 < steps.count) {
        // Return to UIKit between actions so presentation/dismissal completion
        // and viewDidAppear can run before the next control is exercised.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            PHProbeRunSteps(steps, index + 1, NSProcessInfo.processInfo.systemUptime + 10, guest, overlay);
        });
    }
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
@property(nonatomic) NSUInteger landscapeAttempts;
@property(nonatomic, copy) NSString *landscapeError;
@property(nonatomic) NSTimeInterval targetDeadline;
@property(nonatomic) NSTimeInterval overlayDeadline;
@property(nonatomic) NSUInteger formationAttempts;
@property(nonatomic, strong) WKWebView *formationWeb;
- (void)startWithWindowScene:(UIWindowScene * _Nullable)scene;
- (void)exerciseOverlay;
- (void)finishPanelChecks;
- (void)verifyRejectedTarget;
- (void)verifyLandscape;
- (void)checkFormation;
- (void)verifyFormation;
- (void)exerciseLibrary;
- (void)verifyLibrary;
- (void)verifyFocusedEditor;
- (void)checkRestartedFormation;
- (void)finishPortrait;
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
        PHProbeLegacyOverlay = nil;
        PHProbeLegacyWindows = [NSHashTable weakObjectsHashTable];
        [NSNotificationCenter.defaultCenter addObserverForName:UIWindowDidBecomeVisibleNotification object:nil
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {
                UIWindow *window = notification.object;
                if (window != self.window && window.windowLevel > UIWindowLevelNormal)
                    [PHProbeLegacyWindows addObject:window];
            }];
    }
    PHProbeRoot = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/PADHelper"];
    PHProbeFailures = [NSMutableArray array];
    PHProbeGeometry = [NSMutableArray array];
    [NSFileManager.defaultManager createDirectoryAtPath:PHProbeRoot withIntermediateDirectories:YES attributes:nil error:NULL];
    PHProbeRestartLibrary = PHProbeJSON(@"formation-restart.json");
    if (!PHProbeRestartLibrary) {
        NSDictionary *body = @{@"id":@14238, @"level":@0, @"skill":@0, @"awakenings":@-1,
            @"superAwakening":@-1, @"plus":@[@297,@297,@297], @"latents":@[]};
        NSDictionary *assist = @{@"id":@14132, @"level":@0, @"skill":@1, @"awakenings":@-1,
            @"superAwakening":@0, @"plus":@[@99,@99,@99], @"latents":@[]};
        NSMutableArray *slots = [NSMutableArray array];
        for (unsigned i = 0; i < 6; ++i) [slots addObject:@{@"body":body, @"assist":assist}];
        NSDictionary *legacy = @{@"schema":@1, @"title":@"舊版隊伍", @"slots":slots};
        [[NSJSONSerialization dataWithJSONObject:legacy options:0 error:NULL]
            writeToFile:[PHProbeRoot stringByAppendingPathComponent:@"formation.json"] atomically:YES];
    }
    NSDictionary *endpoint = @{@"schema": @1, @"host": @"example.invalid", @"path": @"/api.php", @"queryIncluded": @NO};
    [[NSJSONSerialization dataWithJSONObject:endpoint options:0 error:NULL]
     writeToFile:[PHProbeRoot stringByAppendingPathComponent:@"endpoint.json"] atomically:YES];
    NSString *library = [NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Frameworks/PADCheaperTest.dylib"];
    void *handle = dlopen(library.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    PHProbeDylib = handle;
    const char *error = handle ? NULL : dlerror();
    PHProbeCheck(handle != NULL, error ? @(error) : @"Simulator dylib could not be loaded");
    self.targetDeadline = NSProcessInfo.processInfo.systemUptime + 90;
    self.overlayDeadline = NSProcessInfo.processInfo.systemUptime + 15;
    [self performSelector:@selector(exerciseOverlay) withObject:nil afterDelay:0.1];
}

- (void)exerciseOverlay {
    UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
    [self.window layoutIfNeeded];
    [overlay layoutIfNeeded];
    UIButton *readyBubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    if (!overlay || overlay.hidden || readyBubble.window != overlay ||
        CGRectGetWidth(readyBubble.bounds) < 44 || CGRectGetHeight(readyBubble.bounds) < 44) {
        if (NSProcessInfo.processInfo.systemUptime < self.overlayDeadline) {
            PHProbeReport(@"waiting_overlay");
            [self performSelector:@selector(exerciseOverlay) withObject:nil afterDelay:0.1];
        } else {
            PHProbeCheck(NO, @"Timed out waiting for the initialized floating overlay");
            PHProbeRender(self.window, overlay, @"startup-timeout");
            PHProbeReport(@"done");
        }
        return;
    }
    if (PHProbeRestartLibrary) {
        PHProbeCheck([PHProbeJSON(@"formations.json") isEqual:PHProbeRestartLibrary], @"Restart keeps the saved library file intact");
        PHProbeCheck([PHProbeJSON(@"config.json")[@"battle"][@"autoApplyTeam"] isEqual:@YES], @"Automatic entry option survives a process restart");
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"quality"] isEqual:@"thorough"], @"Orb quality survives a process restart");
        PHProbeCheck([[overlay.rootViewController valueForKey:@"shortcutsCollapsed"] boolValue],
            @"A restarted overlay begins as a single folded button");
        PHProbeOpenPanel(overlay);
        [(UIButton *)PHProbeFind(overlay, @"padcheaper.formationOpen") sendActionsForControlEvents:UIControlEventTouchUpInside];
        UIWindow *formationWindow = [overlay.rootViewController valueForKey:@"formationWindow"];
        self.formationWeb = (WKWebView *)PHProbeFind(formationWindow, @"padcheaper.formationWeb");
        [self checkRestartedFormation];
        return;
    }
    PHProbeCheck(overlay != nil && !overlay.hidden, @"Floating GUI must appear before the target gate finishes");
    PHProbeCheck(!overlay.isKeyWindow && self.window.isKeyWindow, @"Overlay must leave the guest window key");
    NSDictionary *config = PHProbeJSON(@"config.json");
    PHProbeCheck(config != nil && [config[@"experimentalWrites"] isEqual:@NO], @"Config must exist before UUID validation");
    NSDictionary *migrated = PHProbeJSON(@"formations.json");
    PHProbeCheck([migrated[@"selected"] isEqual:@"legacy"] && [migrated[@"teams"] count] == 1 &&
        [migrated[@"teams"][0][@"team"] isEqual:PHProbeJSON(@"formation.json")], @"Old single-team drafts migrate without losing member settings");
    PHProbeCheck([config[@"battle"][@"autoApplyTeam"] isEqual:@NO], @"Automatic entry replacement starts disabled");
    PHProbeCheck([config[@"orbs"][@"quality"] isEqual:@"balanced"], @"New installs default to balanced orb quality");
    NSDictionary *status = PHProbeJSON(@"status.json");
    PHProbeCheck([status[@"loaded"] isEqual:@YES] && [status[@"targetMatched"] isEqual:@NO], @"Loaded status must be independent of feature activation");
    PHProbeCheck([status[@"saveState"][@"localSaveCount"] isEqual:@0] &&
                 [status[@"saveState"][@"acceptedCount"] isEqual:@0], @"GUI must not invent successful saves before a real request");
    UIButton *bubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    CGPoint point = [bubble convertPoint:CGPointMake(CGRectGetMidX(bubble.bounds), CGRectGetMidY(bubble.bounds)) toView:overlay];
    UIView *bubbleHit = [overlay hitTest:point withEvent:nil];
    PHProbeCheck(bubbleHit == bubble, [NSString stringWithFormat:
        @"Floating button must receive touches (frame=%@ bounds=%@ point=%@ hit=%@)",
        NSStringFromCGRect(bubble.frame), NSStringFromCGRect(bubble.bounds), NSStringFromCGPoint(point),
        NSStringFromClass(bubbleHit.class) ?: @"nil"]);
    PHProbeCheck([overlay hitTest:CGPointMake(20, CGRectGetHeight(overlay.bounds) - 50) withEvent:nil] == nil,
                 @"Touches outside the button must pass through");
    UIButton *onceShortcut = (UIButton *)PHProbeFind(overlay, @"padcheaper.orbsOnceShortcut");
    UIButton *autoShortcut = (UIButton *)PHProbeFind(overlay, @"padcheaper.orbsAutoShortcut");
    UIButton *collapseShortcut = (UIButton *)PHProbeFind(overlay, @"padcheaper.collapseShortcuts");
    PHProbeCheck(onceShortcut && autoShortcut && collapseShortcut &&
        onceShortcut.hidden && autoShortcut.hidden && collapseShortcut.hidden &&
        PHProbeFind(overlay, @"padcheaper.orbsShortcutStatus").hidden,
        @"Initial overlay hides all shortcut controls and status behind one PC button");
    PHProbeRender(self.window, overlay, @"bubble");
    [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIView *panel = PHProbeFind(overlay, @"padcheaper.panel");
    PHProbeCheck(panel.hidden && !onceShortcut.hidden && !autoShortcut.hidden && !collapseShortcut.hidden,
        @"First PC tap expands shortcuts without opening settings");
    PHProbeCheck(!onceShortcut.enabled && !autoShortcut.enabled,
        @"An unvalidated guest keeps both input shortcuts disabled after unfolding");
    PHProbeFloatingTargets(overlay);
    CGPoint oldShortcutCenter = [onceShortcut convertPoint:CGPointMake(CGRectGetMidX(onceShortcut.bounds), CGRectGetMidY(onceShortcut.bounds)) toView:overlay];
    [collapseShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
    PHProbeCheck(!bubble.hidden && onceShortcut.hidden && autoShortcut.hidden && collapseShortcut.hidden &&
        [overlay hitTest:oldShortcutCenter withEvent:nil] == nil,
        @"Collapsed shortcut area passes touches through to the guest");
    PHProbeOpenPanel(overlay);
    PHProbeCheck(panel != nil && !panel.hidden, @"Expanded PC entry must still open the settings panel");
    UILabel *persistence = (UILabel *)PHProbeFind(overlay, @"padcheaper.persistence");
    PHProbeCheck([persistence.text containsString:@"尚未儲存隊伍"] &&
                 [persistence.text containsString:@"伺服器接受：0"], @"Panel must separate load/feature status from actual save and response status");
    for (NSString *key in @[@"experimentalWrites", @"skin", @"bgm", @"bgmUncleared", @"badge", @"timerFix", @"battleSwap"]) {
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
                 [config[@"features"][@"bgm"] isEqual:@YES] && [config[@"features"][@"timerFix"] isEqual:@YES] &&
                 [config[@"features"][@"bgmUncleared"] isEqual:@YES] &&
                 [config[@"features"][@"battleSwap"] isEqual:@YES] && [config[@"features"][@"badge"] isEqual:@YES],
                 @"GUI switches must persist to config.json");
    PHProbeCheck([config[@"apiHosts"] isEqual:@[@"example.invalid"]] && [config[@"apiPaths"] isEqual:@[@"/api.php"]],
                 @"GUI must persist only the endpoint host and path");
    UIButton *restore = (UIButton *)PHProbeFind(overlay, @"padcheaper.battleRestore");
    PHProbeCheck(PHProbeFind(overlay, @"padcheaper.formationOpen") != nil && restore != nil,
                 @"Panel provides formation editing and all-member restore");
    for (NSString *identifier in @[@"battleSlot", @"battleTarget", @"battleApply", @"keypad"])
        PHProbeCheck(PHProbeFind(overlay, [@"padcheaper." stringByAppendingString:identifier]) == nil,
                     @"Obsolete quick-replacement controls must not occupy the panel");
    [self performSelector:@selector(finishPanelChecks) withObject:nil afterDelay:0.6];
}

- (void)finishPanelChecks {
    UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
    UIButton *restore = (UIButton *)PHProbeFind(overlay, @"padcheaper.battleRestore");
    PHProbeCheck(restore != nil && !restore.enabled, @"Unmatched UUID must keep battle restore unavailable");
    // Direct action dispatch also exercises the runtime guard behind disabled UI.
    [restore sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIButton *timerDismiss = (UIButton *)PHProbeFind(overlay, @"padcheaper.timerDismiss");
    PHProbeCheck(timerDismiss != nil && !timerDismiss.enabled, @"Manual timer control requires a verified timed exit popup");
    [timerDismiss sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIButton *copy = (UIButton *)PHProbeFind(overlay, @"padcheaper.copy");
    [copy sendActionsForControlEvents:UIControlEventTouchUpInside];
    PHProbeCheck([UIPasteboard.generalPasteboard.string containsString:@"Loaded: YES"], @"Copy diagnostics must work");
    PHProbeCheck([UIPasteboard.generalPasteboard.string containsString:@"Battle input:"] &&
                 [UIPasteboard.generalPasteboard.string containsString:@"Timer input:"] &&
                 [UIPasteboard.generalPasteboard.string containsString:@"Battle drag active:"],
                 @"Copied diagnostics must include native input and drag state");
    UIScrollView *scroll = (UIScrollView *)PHProbeFind(overlay, @"padcheaper.scroll");
    UIView *badgeRow = PHProbeFind(overlay, @"padcheaper.badge").superview;
    UIView *timerStatus = PHProbeFind(overlay, @"padcheaper.timerStatus");
    [overlay layoutIfNeeded];
    CGRect features = CGRectUnion([badgeRow convertRect:badgeRow.bounds toView:scroll],
                                  [timerStatus convertRect:timerStatus.bounds toView:scroll]);
    [scroll scrollRectToVisible:features animated:NO];
    PHProbeRender(self.window, overlay, @"features");
    UIButton *filters = (UIButton *)PHProbeFind(overlay, @"padcheaper.filtersOpen");
    [filters sendActionsForControlEvents:UIControlEventTouchUpInside];
    PHProbeCheck(!PHProbeFind(overlay, @"padcheaper.filters").hidden, @"single-player entry settings must open from the GUI");
    PHProbeCheck(PHProbeFind(overlay, @"padcheaper.allowAllMulti") == nil &&
        PHProbeFind(overlay, @"padcheaper.multiModes") == nil && PHProbeFind(overlay, @"padcheaper.singlePlayerOnly") != nil,
        @"the GUI explains single-player-only operation and offers no multiplayer override");
    for (NSString *key in @[@"allowAllModes", @"allowAllSources", @"autoApplyTeam"]) {
        UISwitch *toggle = (UISwitch *)PHProbeFind(overlay, [@"padcheaper." stringByAppendingString:key]);
        PHProbeCheck(toggle != nil && toggle.enabled, @"eligibility filters must have editable GUI switches");
        for (NSNumber *value in @[@NO,@YES]) {
            toggle.on = value.boolValue; [toggle sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck([PHProbeJSON(@"config.json")[@"battle"][key] isEqual:value], @"filter toggles must save to the same config immediately");
        }
    }
    NSDictionary *config = PHProbeJSON(@"config.json");
    PHProbeCheck([config[@"battle"][@"entryModes"] isEqual:@[@0,@1,@12]] &&
                 [config[@"battle"][@"sourceModes"] isEqual:@[@0]] && [config[@"battle"][@"multiModes"] isEqual:@[@0]],
                 @"allow-all switches preserve each user's optional list");
    UIView *filterView = PHProbeFind(overlay, @"padcheaper.filters");
    [overlay layoutIfNeeded]; [scroll scrollRectToVisible:[filterView convertRect:filterView.bounds toView:scroll] animated:NO];
    PHProbeRender(self.window, overlay, @"filters");
    [filters sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIButton *close = (UIButton *)PHProbeFind(overlay, @"padcheaper.close");
    [close sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIView *panel = PHProbeFind(overlay, @"padcheaper.panel");
    UIButton *bubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    PHProbeCheck(panel.hidden && !bubble.hidden, @"Close must restore the floating button");
    PHProbeReport(@"running");
    [self performSelector:@selector(verifyRejectedTarget) withObject:nil afterDelay:1];
}

- (void)verifyRejectedTarget {
    NSDictionary *status = PHProbeJSON(@"status.json");
    // Bootstrap performs twenty scheduled discovery attempts. On a busy hosted
    // simulator those callbacks take longer than twenty wall-clock seconds.
    // Observe completion, while retaining a deadline and every gate assertion.
    if ((!status || [status[@"tone"] isEqual:@"pending"]) &&
        NSProcessInfo.processInfo.systemUptime < self.targetDeadline) {
        PHProbeReport(@"waiting_target");
        [self performSelector:@selector(verifyRejectedTarget) withObject:nil afterDelay:1];
        return;
    }
    UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
    UIButton *bubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    PHProbeOpenPanel(overlay);
    status = PHProbeJSON(@"status.json");
    PHProbeCheck([status[@"tone"] isEqual:@"error"] && [status[@"targetMatched"] isEqual:@NO], @"Wrong UUID must show a visible error");
    PHProbeCheck([status[@"validation"] containsString:@"UUID"], @"Target rejection must identify the UUID mismatch");
    PHProbeCheck([status[@"frameHook"] isEqual:@NO] && [status[@"networkHook"] isEqual:@NO] &&
                 [status[@"lifecycleHook"] isEqual:@NO] && [status[@"activeMask"] isEqual:@0],
                 @"GUI toggles must not bypass target validation");
    PHProbeCheck([status[@"battleState"][@"implemented"] isEqual:@YES] &&
                 [status[@"battleState"][@"canApply"] isEqual:@NO] && [status[@"battleState"][@"applied"] isEqual:@NO] &&
                 [status[@"battleState"][@"applyCount"] isEqual:@0] && [status[@"battleState"][@"restoreCount"] isEqual:@0],
                 @"Battle GUI actions cannot bypass UUID validation or invent writes");
    PHProbeCheck([status[@"badgeImplemented"] isEqual:@YES] && [status[@"badgeState"][@"ready"] isEqual:@NO] &&
                 [status[@"timerState"][@"canDismiss"] isEqual:@NO] && [status[@"timerState"][@"flagWriteCount"] isEqual:@0] &&
                 [status[@"timerState"][@"dismissCount"] isEqual:@0], @"Badge/timer controls cannot bypass validation or mutate an unmatched guest");
    UILabel *label = (UILabel *)PHProbeFind(overlay, @"padcheaper.status");
    PHProbeCheck([label.text containsString:@"校驗未通過"], @"Panel must display the target failure reason");
    PHProbeCheck(self.window.isKeyWindow, @"Opening the panel must not steal key-window status");
    PHProbeRender(self.window, overlay, @"portrait");
    PHProbeReport(@"portrait");
    UIButton *orbs = (UIButton *)PHProbeFind(overlay, @"padcheaper.orbsOpen");
    PHProbeCheck(orbs != nil, @"Native auto-orb entry is visible");
    [orbs sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIWindow *orbWindow = [overlay.rootViewController valueForKey:@"orbWindow"];
    PHOrbController *orbController = [overlay.rootViewController valueForKey:@"orbController"];
    PHProbeCheck(orbWindow != nil && PHProbeFind(orbWindow, @"padcheaper.orbsBoard") != nil && self.window.isKeyWindow,
        @"Orb preview uses native controls and preserves guest key window");
    PHProbeCheck(![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") isEnabled],
        @"Unvalidated guest cannot execute automatic input");
    UISegmentedControl *qualityControl = (UISegmentedControl *)PHProbeFind(orbWindow, @"padcheaper.orbsQuality");
    PHProbeCheck(qualityControl.enabled && qualityControl.numberOfSegments == 3, @"Orb quality can be configured without enabling game input");
    UISwitch *thornPriority = (UISwitch *)PHProbeFind(orbWindow, @"padcheaper.orbsThornPriority");
    PHProbeCheck(thornPriority.enabled, @"Thorn Combo preference is configurable without enabling game input");
    for (NSNumber *enabled in @[@YES, @NO]) {
        thornPriority.on = enabled.boolValue; [thornPriority sendActionsForControlEvents:UIControlEventValueChanged];
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"thornComboPriority"] isEqual:enabled] && thornPriority.on == enabled.boolValue,
            @"Thorn Combo switch follows the acknowledged real configuration write");
    }
    PHProbeRender(self.window, orbWindow, @"orbs-thorn-priority");
    UISwitch *poisonWithHeart = (UISwitch *)PHProbeFind(orbWindow, @"padcheaper.orbsPoisonWithHeartFour");
    PHProbeCheck(poisonWithHeart.enabled, @"Normal poison with heart four is configurable without enabling game input");
    for (NSNumber *enabled in @[@YES, @NO]) {
        poisonWithHeart.on = enabled.boolValue; [poisonWithHeart sendActionsForControlEvents:UIControlEventValueChanged];
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"allowPoisonWithHeartFour"] isEqual:enabled] &&
            poisonWithHeart.on == enabled.boolValue && ![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") isEnabled],
            @"Poison switch follows the acknowledged disk write without bypassing game validation");
    }
    PHProbeRender(self.window, orbWindow, @"orbs-poison-heart-four");
    UISwitch *diagonal = (UISwitch *)PHProbeFind(orbWindow, @"padcheaper.orbsDiagonal");
    PHProbeCheck(diagonal.enabled && !diagonal.on, @"Diagonal movement defaults off and can be configured without enabling input");
    for (NSNumber *enabled in @[@YES,@NO]) {
        diagonal.on = enabled.boolValue; [diagonal sendActionsForControlEvents:UIControlEventValueChanged];
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"diagonal"] isEqual:enabled] && diagonal.on == enabled.boolValue &&
            ![(UIButton *)PHProbeFind(orbWindow,@"padcheaper.orbsExecute") isEnabled],
            @"Diagonal switch follows the acknowledged file save while unvalidated game input remains disabled");
    }
    NSArray *qualityIDs = @[@"fast", @"balanced", @"thorough"];
    for (NSUInteger index = 0; index < qualityIDs.count; ++index) {
        qualityControl.selectedSegmentIndex = (NSInteger)index;
        [qualityControl sendActionsForControlEvents:UIControlEventValueChanged];
        PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"quality"] isEqual:qualityIDs[index]],
            @"Quality selection is acknowledged by an actual config file write");
    }
    UISegmentedControl *goalMode = (UISegmentedControl *)PHProbeFind(orbWindow, @"padcheaper.orbsGoalMode");
    PHProbeCheck(goalMode.enabled && goalMode.numberOfSegments == 3, @"Native goal modes are available without enabling game input");
    goalMode.selectedSegmentIndex = 2; [goalMode sendActionsForControlEvents:UIControlEventValueChanged];
    PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"mode"] isEqual:@"leaders"], @"Leader board mode is saved explicitly");
    __block NSArray *savedGoals = nil, *rescuePayload = nil;
    __block NSDictionary *orbFixture = nil, *baseOrbFixture = nil, *wantedHearts = nil, *profilePayload = nil;
    __block NSMutableDictionary *goalFailure = nil, *ruleFailure = nil, *recoveryFailure = nil, *profileFailure = nil;
    __block NSString *orbAction = nil;
    __block id orbValue = nil;
    __block NSDictionary *shareDraft = nil, *shareConfig = nil;
    __block NSURL *shareDirectory = nil;
    __block UIDocumentPickerViewController *profilePicker = nil;
    __block PHOverlaySnapshot restoreOriginalSnapshot = nil;
    __block PHOverlayAction restoreOriginalAction = nil;
    __block NSMutableDictionary *restoreFixture = nil;
    __block NSString *restoreAction = nil;
    __block NSTimeInterval restoreReadyAt = 0;
    NSArray<NSDictionary *> *steps = @[
        PHProbeStep(@"open-goal-editor", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            goalMode.selectedSegmentIndex = 1; [goalMode sendActionsForControlEvents:UIControlEventValueChanged];
            if (!orbController.presentedViewController)
                [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsGoalEdit") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"goal-editor-visible", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsGoalEditor", NO); }, ^{
            UIViewController *goalEditor = orbController.presentedViewController;
            [goalEditor loadViewIfNeeded];
            PHProbeCheck([goalEditor.view.accessibilityIdentifier isEqual:@"padcheaper.orbsGoalEditor"], @"First manual selection opens an editable native goal sheet");
            NSMutableArray *goalDraft = [goalEditor valueForKey:@"draft"];
            [goalDraft removeAllObjects];
            [goalDraft addObject:[@{@"kind":@"column", @"colors":@4, @"count":@1, @"length":@0} mutableCopy]];
            [(UIButton *)PHProbeFind(goalEditor.view, @"padcheaper.orbsGoalSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"goal-save-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            savedGoals = PHProbeJSON(@"config.json")[@"orbs"][@"goals"] ?: @[];
            PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"mode"] isEqual:@"manual"] && savedGoals.count == 1 &&
                [savedGoals[0][@"kind"] isEqual:@"column"] && [savedGoals[0][@"colors"] isEqual:@4] && orbController.presentedViewController == nil,
                @"Goal sheet closes only after shape and color are acknowledged in the config file");
            qualityControl.selectedSegmentIndex = 0; [qualityControl sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"goals"] isEqual:savedGoals], @"Changing quality preserves saved goal conjunctions");
            qualityControl.selectedSegmentIndex = 2; [qualityControl sendActionsForControlEvents:UIControlEventValueChanged];
            goalMode.selectedSegmentIndex = 0; [goalMode sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck([PHProbeJSON(@"config.json")[@"orbs"][@"goals"] isEqual:savedGoals], @"Combo mode retains the manual profile for later use");
            UIButton *heartEdit = (UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsRecoveryEdit");
            PHProbeCheck(heartEdit.enabled, @"Recovery policy can be configured without enabling unvalidated game input");
            [heartEdit sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"heart-editor-keyboard-ready", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsRecoveryEditor", YES); }, ^{
            UIViewController *heartEditor = orbController.presentedViewController; [heartEditor loadViewIfNeeded];
            UITextField *heartHP = (UITextField *)PHProbeFind(heartEditor.view, @"padcheaper.orbsRecoveryHP");
            UISegmentedControl *heartGroups = (UISegmentedControl *)PHProbeFind(heartEditor.view, @"padcheaper.orbsRecoveryGroups");
            UISegmentedControl *heartShape = (UISegmentedControl *)PHProbeFind(heartEditor.view, @"padcheaper.orbsRecoveryShape");
            PHProbeCheck(orbWindow.isKeyWindow && [heartHP becomeFirstResponder] && heartGroups.numberOfSegments == 3 &&
                heartShape.numberOfSegments == 2, @"Heart policy provides a working numeric keyboard and group/shape controls");
            heartHP.text = @"40"; [heartHP sendActionsForControlEvents:UIControlEventEditingChanged]; [heartHP resignFirstResponder];
            heartGroups.selectedSegmentIndex = 1; [heartGroups sendActionsForControlEvents:UIControlEventValueChanged];
            heartShape.selectedSegmentIndex = 1; [heartShape sendActionsForControlEvents:UIControlEventValueChanged];
            [(UIButton *)PHProbeFind(heartEditor.view, @"padcheaper.orbsRecoverySave") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"heart-save-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            NSDictionary *savedHeartPolicy = PHProbeJSON(@"config.json")[@"orbs"][@"recovery"];
            PHProbeCheck(!orbController.presentedViewController && self.window.isKeyWindow &&
                [savedHeartPolicy isEqual:@{@"hpAtMost":@40,@"groups":@2,@"shape":@"four"}] &&
                [PHProbeJSON(@"config.json")[@"orbs"][@"goals"] isEqual:savedGoals],
                @"Heart policy closes only after all fields reach config.json while preserving ordinary goals");
            PHProbeRender(self.window, orbWindow, @"orbs-recovery-saved");
            NSMutableArray *orbTypes = [NSMutableArray array], *orbEnhanced = [NSMutableArray array];
            for (unsigned i = 0; i < 30; ++i) { [orbTypes addObject:@(i % 6)]; [orbEnhanced addObject:@(i % 7 == 0)]; }
            orbFixture = @{@"orbState":@{@"phase":@"preview", @"message":@"預計 5 Combo（不含天降）· 6 步",
                @"generation":@19, @"canRead":@YES, @"canExecute":@YES, @"canStop":@NO, @"enabled":@YES,
                @"quality":@"balanced", @"canSetQuality":@YES, @"goalMode":@"combo", @"canSetGoals":@YES, @"goals":savedGoals,
                @"thornComboPriority":@NO, @"canSetThornPolicy":@YES,
                @"allowPoisonWithHeartFour":@NO, @"canSetPoisonPolicy":@YES,
                @"diagonal":@NO, @"canSetDiagonal":@YES,
                @"rules":@[], @"canSetRules":@YES, @"canStart":@YES, @"automatic":@NO,
                @"recovery":savedHeartPolicy ?: @{}, @"canSetRecovery":@YES,
                @"profiles":@{@"schema":@1,@"selected":@"",@"autoSelect":@NO,@"profiles":@[]}, @"canSetProfiles":@YES,
                @"board":@{@"width":@6, @"height":@5, @"types":orbTypes, @"enhanced":orbEnhanced},
                @"path":@[@0,@1,@7,@8,@14,@15,@21]}};
            baseOrbFixture = orbFixture;
            [orbController setValue:[^{ return orbFixture; } copy] forKey:@"snapshot"];
            [orbController setValue:[^(NSString *action, id value) { orbAction = action; orbValue = value; } copy] forKey:@"action"];
            [orbController refresh];
            UISlider *orbSlider = (UISlider *)PHProbeFind(orbWindow, @"padcheaper.orbsStep");
            PHProbeCheck(orbSlider.maximumValue == 6 && orbSlider.enabled, @"Native path slider covers every route step");
            orbSlider.value = 3; [orbSlider sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck([PHProbeFind(orbWindow, @"padcheaper.orbsBoard").accessibilityLabel containsString:@"第 3／6 步"],
                @"Native preview describes the selected step and starting cell");
            PHProbeRevealBoard(orbWindow);
            PHProbeRender(self.window, orbWindow, @"orbs-preview");
            for (NSArray<NSNumber *> *dimensions in @[@[@7,@6], @[@5,@4]]) {
                const NSUInteger width = dimensions[0].unsignedIntegerValue, height = dimensions[1].unsignedIntegerValue;
                NSMutableArray *types = [NSMutableArray array], *enhanced = [NSMutableArray array];
                for (NSUInteger i = 0; i < width * height; ++i) { [types addObject:@(i % 6)]; [enhanced addObject:@(i % 7 == 0)]; }
                NSMutableDictionary *state = [baseOrbFixture[@"orbState"] mutableCopy];
                state[@"board"] = @{@"width":@(width), @"height":@(height), @"types":types, @"enhanced":enhanced};
                state[@"path"] = @[@0, @1, @(width + 1), @(width + 2), @(width * 2 + 2), @(width * 2 + 3), @(width * 3 + 3)];
                orbFixture = @{@"orbState":state}; [orbController refresh];
                orbSlider.value = 3; [orbSlider sendActionsForControlEvents:UIControlEventValueChanged];
                [orbController.view layoutIfNeeded];
                UIView *boardView = PHProbeFind(orbWindow, @"padcheaper.orbsBoard");
                PHProbeCheck([boardView.accessibilityLabel containsString:@"目前：上起 2 列、左起 3 格"] &&
                    fabs(CGRectGetHeight(boardView.bounds) - CGRectGetWidth(boardView.bounds) * height / width) < 1,
                    @"Expanded board preview uses its actual columns and aspect ratio");
                PHProbeRevealBoard(orbWindow);
                PHProbeRender(self.window, orbWindow, [NSString stringWithFormat:@"orbs-%lux%lu-preview", (unsigned long)width, (unsigned long)height]);
                for (NSString *identifier in @[@"padcheaper.orbsClose", @"padcheaper.orbsRead", @"padcheaper.orbsExecute", @"padcheaper.orbsStart", @"padcheaper.orbsStop"]) {
                    UIView *control = PHProbeFind(orbWindow, identifier);
                    CGRect bounds = [control convertRect:control.bounds toView:orbWindow];
                    PHProbeCheck(control && CGRectContainsRect(orbWindow.bounds, bounds), @"Fixed return and action buttons remain on screen for every board size");
                }
            }
            for (NSArray<NSNumber *> *dimensions in @[@[@6,@5], @[@7,@6], @[@5,@4]]) {
                const NSUInteger width = dimensions[0].unsignedIntegerValue, height = dimensions[1].unsignedIntegerValue, cells = width * height;
                NSMutableArray *types = [NSMutableArray array], *enhanced = [NSMutableArray array], *locks = [NSMutableArray array];
                NSMutableArray *weak = [NSMutableArray array], *effects = [NSMutableArray array], *blind = [NSMutableArray array];
                NSMutableArray *spinners = [NSMutableArray array], *tape = [NSMutableArray array], *cloud = [NSMutableArray array];
                for (NSUInteger i = 0; i < cells; ++i) {
                    [types addObject:@(i % PHOrbTypes)]; [enhanced addObject:@(i == 0)]; [locks addObject:@(i == 1)];
                    [weak addObject:@(i == 1)]; [blind addObject:@(i == 3 ? 5 : 0)];
                    unsigned flags = i == 2 ? PHOrbFlagBlindTouch : i == 3 ? PHOrbFlagBlind :
                        i == 4 ? PHOrbFlagThorn : i == 5 ? PHOrbFlagNail | PHOrbFlagCombo : 0;
                    if (i == 1) flags |= PHOrbFlagLocked;
                    if (i < width) flags |= PHOrbFlagTape;
                    [effects addObject:@(flags)]; [spinners addObject:@(i == width || i == width * 2)];
                    [tape addObject:@(i < width)]; [cloud addObject:@(i >= cells - width)];
                }
                NSMutableDictionary *state = [baseOrbFixture[@"orbState"] mutableCopy];
                state[@"board"] = @{@"width":@(width), @"height":@(height), @"types":types, @"enhanced":enhanced,
                    @"locked":locks, @"weakened":weak, @"effects":effects, @"blindTurns":blind,
                    @"spinners":spinners, @"tape":tape, @"cloud":cloud, @"fixedStart":@(cells - 1), @"unmatchableColors":@32};
                state[@"path"] = @[@(cells - 2), @(cells - 1)]; state[@"generation"] = @(100 + cells);
                state[@"obstacles"] = @"有雲、封條、普通暗闇、超暗闇、弱化珠、轉轉、毒珠、爆彈";
                state[@"hazardsSafe"] = @NO; state[@"canExecute"] = @NO;
                state[@"poisonErased"] = @3; state[@"unmatchedBombs"] = @1;
                state[@"message"] = @"未找到符合妨礙處理條件的路徑：毒／猛毒 3 顆、首批未消爆彈 1 顆。可先用技能改盤。";
                orbFixture = @{@"orbState":state}; [orbController refresh];
                UIView *boardView = PHProbeFind(orbWindow, @"padcheaper.orbsBoard");
                PHProbeCheck([boardView.accessibilityValue containsString:@"超暗闇"] &&
                    [((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsAvailability")).text containsString:@"毒"],
                    @"Effect previews expose their annotations and the reason hazardous routes cannot execute");
                orbAction = nil;
                [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") sendActionsForControlEvents:UIControlEventTouchUpInside];
                PHProbeCheck(orbAction == nil && ![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") isEnabled],
                    @"Hazard previews remain visible without dispatching an execute action");
                PHProbeRevealBoard(orbWindow);
                PHProbeRender(self.window, orbWindow, [NSString stringWithFormat:@"orbs-effects-%lux%lu", (unsigned long)width, (unsigned long)height]);
                orbSlider.value = 1; [orbSlider sendActionsForControlEvents:UIControlEventValueChanged];
                PHProbeCheck([boardView.accessibilityLabel containsString:@"第 1／1 步"], @"Effect previews support ordinary step navigation");
            }
            NSMutableDictionary *qualityFailure = [baseOrbFixture[@"orbState"] mutableCopy];
            qualityFailure[@"qualityMessage"] = @"品質未能儲存，仍保留原設定";
            orbFixture = @{@"orbState":qualityFailure}; [orbController refresh];
            qualityControl.selectedSegmentIndex = 0; [qualityControl sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck([orbAction isEqual:@"orbsQuality"] && [orbValue isEqual:@"fast"] && qualityControl.selectedSegmentIndex == 1 &&
                [((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsQualityDescription")).text containsString:@"未能儲存"],
                @"Unacknowledged quality selection restores the last saved segment and shows the failure");
            qualityFailure[@"thornPolicyMessage"] = @"荊棘設定未能儲存，仍保留原設定"; [orbController refresh];
            thornPriority.on = YES; [thornPriority sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck([orbAction isEqual:@"orbsThornPriority"] && [orbValue isEqual:@YES] && !thornPriority.on &&
                [((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsThornDescription")).text containsString:@"未能儲存"],
                @"Unacknowledged thorn switch restores the saved value and displays its own error");
            qualityFailure[@"diagonalMessage"] = @"斜向轉珠設定未能儲存，仍保留原設定"; [orbController refresh];
            diagonal.on = YES; [diagonal sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck([orbAction isEqual:@"orbsDiagonal"] && [orbValue isEqual:@YES] && !diagonal.on &&
                [((UILabel *)PHProbeFind(orbWindow,@"padcheaper.orbsDiagonalDescription")).text containsString:@"未能儲存"],
                @"Unacknowledged diagonal save restores the previous switch and displays the error");
            qualityFailure[@"busy"] = @YES; orbAction = nil; [orbController refresh];
            qualityControl.selectedSegmentIndex = 0; [qualityControl sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck(!qualityControl.enabled && orbAction == nil, @"Busy session cannot change quality through the control");
            thornPriority.on = YES; [thornPriority sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck(!thornPriority.enabled && orbAction == nil, @"Busy session cannot change thorn ranking for a running route");
            diagonal.on = YES; [diagonal sendActionsForControlEvents:UIControlEventValueChanged];
            PHProbeCheck(!diagonal.enabled && orbAction == nil, @"Busy session cannot change the captured movement policy");
            qualityFailure[@"busy"] = @NO; qualityFailure[@"inputFault"] = @YES; qualityFailure[@"canRead"] = @NO;
            [orbController refresh];
            PHProbeCheck([((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsAvailability")).text containsString:@"重啟"],
                @"A terminal input fault explains how to recover");
            goalFailure = [baseOrbFixture[@"orbState"] mutableCopy];
            goalFailure[@"goalMode"] = @"manual"; goalFailure[@"goalMessage"] = @"目標未能儲存，仍保留原設定";
            orbFixture = @{@"orbState":goalFailure}; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsGoalEdit") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"failed-goal-editor-visible", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsGoalEditor", NO); }, ^{
            UIViewController *goalEditor = orbController.presentedViewController; [goalEditor loadViewIfNeeded];
            NSMutableArray *goalDraft = [goalEditor valueForKey:@"draft"];
            if (goalDraft.count) goalDraft[0][@"colors"] = @2;
            [(UIButton *)PHProbeFind(goalEditor.view, @"padcheaper.orbsGoalSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == goalEditor && [orbAction isEqual:@"orbsGoals"] && goalDraft.count &&
                [goalDraft[0][@"colors"] isEqual:@2] &&
                [((UILabel *)PHProbeFind(goalEditor.view, @"padcheaper.orbsGoalNotice")).text containsString:@"未能儲存"],
                @"Failed goal saves retain the editable draft and do not close the sheet or publish new settings");
            PHProbeRender(self.window, orbWindow, @"orbs-goals-failed-draft");
            [goalEditor dismissViewControllerAnimated:NO completion:nil];
        }),
        PHProbeStep(@"failed-goal-editor-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            [goalFailure removeObjectForKey:@"goalMessage"];
            goalFailure[@"goalCount"] = @1; goalFailure[@"goalHits"] = @[@0]; goalFailure[@"goalsMet"] = @NO;
            goalFailure[@"canExecute"] = @NO;
            goalFailure[@"goalPlan"] = @{@"available":@YES, @"goals":savedGoals, @"message":@"木 整直行 × 1 組", @"warning":@""};
            orbAction = nil; [orbController refresh];
            UIScrollView *orbScroll = PHProbeScrollParent(PHProbeFind(orbWindow, @"padcheaper.orbsBoard"));
            [orbScroll setContentOffset:CGPointZero animated:NO]; [orbController.view layoutIfNeeded];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbAction == nil && ![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") isEnabled] &&
                [((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsGoalDescription")).text containsString:@"未達成"],
                @"Unmet goals show per-condition progress and cannot dispatch execution or close the preview");
            PHProbeRender(self.window, orbWindow, @"orbs-goals");
            ruleFailure = [baseOrbFixture[@"orbState"] mutableCopy];
            ruleFailure[@"ruleMessage"] = @"規則未能儲存，仍保留原設定";
            ruleFailure[@"playerStatus"] = @"HP 100／1000（10.0%） · 有暫時覺醒無效";
            ruleFailure[@"ruleDecisions"] = @[@"HP 10.0% ≤ 40% · 施放者被綁定"]; ruleFailure[@"decisionTurn"] = @1;
            orbFixture = @{@"orbState":ruleFailure}; [orbController refresh];
            PHProbeCheck([((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsPlayerStatus")).text containsString:@"HP"] &&
                [((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsDecisions")).text containsString:@"施放者被綁定"],
                @"Rescue status and latest per-rule explanation have dedicated visible labels");
            PHProbeRender(self.window, orbWindow, @"orbs-survival-decisions");
            ruleFailure[@"ruleDecisionDetails"] = @[@{@"rule":@6, @"slot":@2,
                @"decision":@"HP 10.0% ≤ 40% · 施放者被綁定"}];
            [orbController refresh];
            PHProbeCheck([((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsDecisions")).text containsString:@"規則 6（位置 2）"],
                @"Captured decisions identify the real position independently of rule order");
            PHProbeRender(self.window, orbWindow, @"orbs-skill-position-decisions");
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsRuleEdit") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"rule-editor-keyboard-ready", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsRuleEditor", YES); }, ^{
            UIViewController *ruleEditor = orbController.presentedViewController; [ruleEditor loadViewIfNeeded];
            [(UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleAdd") sendActionsForControlEvents:UIControlEventTouchUpInside];
            [ruleEditor.view layoutIfNeeded];
            NSMutableArray *ruleDraft = [ruleEditor valueForKey:@"draft"];
            UITextField *ruleFloor = (UITextField *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRulefloor0");
            PHProbeCheck(orbWindow.isKeyWindow && [ruleFloor becomeFirstResponder], @"Skill rule editor accepts numeric keyboard input");
            ruleFloor.text = @"3"; [ruleFloor sendActionsForControlEvents:UIControlEventEditingChanged]; [ruleFloor resignFirstResponder];
            UITextField *ruleHP = (UITextField *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRulehpAtMost0");
            PHProbeCheck([ruleHP becomeFirstResponder] && PHProbeFind(ruleEditor.view, @"padcheaper.orbsRulestatus0") &&
                PHProbeFind(ruleEditor.view, @"padcheaper.orbsRulerequired0"), @"Rescue threshold uses the numeric keyboard beside status and required controls");
            ruleHP.text = @"101"; [ruleHP sendActionsForControlEvents:UIControlEventEditingChanged];
            [(UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == ruleEditor &&
                [((UILabel *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleNotice")).text containsString:@"HP"],
                @"Out-of-range rescue percentage keeps the draft and a fixed validation notice");
            ruleHP.text = @"40"; [ruleHP sendActionsForControlEvents:UIControlEventEditingChanged]; [ruleHP resignFirstResponder];
            if (ruleDraft.count) {
                ruleDraft[0][@"status"] = @"awoken_bind"; ruleDraft[0][@"required"] = @YES;
                ruleDraft[0][@"group"] = @1; ruleDraft[0][@"reserveUntil"] = @2;
                ruleDraft[0][@"obstacle"] = @"unmatchable";
            }
            UITextField *ruleEvery = (UITextField *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleevery0");
            ruleEvery.text = @"2"; [ruleEvery sendActionsForControlEvents:UIControlEventEditingChanged];
            UIButton *timingMode = (UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleintervalMode0");
            PHProbeCheck(timingMode.menu.children.count == 2, @"Timing mode offers run and retained accepted-use intervals");
            if (ruleDraft.count) ruleDraft[0][@"intervalMode"] = @"accepted";
            UITextField *warmup = (UITextField *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRulestartAfter0");
            PHProbeCheck(warmup && [warmup becomeFirstResponder], @"Warmup accepts input through the numeric keyboard");
            warmup.text = @"1000"; [warmup sendActionsForControlEvents:UIControlEventEditingChanged]; [warmup resignFirstResponder];
            [(UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == ruleEditor &&
                [((UILabel *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleNotice")).text containsString:@"先轉"],
                @"Out-of-range warmup keeps the draft visible instead of saving");
            warmup.text = @"2"; [warmup sendActionsForControlEvents:UIControlEventEditingChanged];
            NSMutableDictionary *readyExpected = ruleDraft.count ? [ruleDraft[0] mutableCopy] : nil;
            readyExpected[@"every"] = @1;
            UIButton *readyButton = (UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleReady0");
            [readyButton sendActionsForControlEvents:UIControlEventTouchUpInside];
            ruleEvery = (UITextField *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleevery0");
            PHProbeCheck(readyButton && ruleDraft.count && [ruleDraft[0] isEqual:readyExpected] && [ruleEvery.text isEqual:@"1"],
                @"Ready-cast shortcut sets every turn while retaining all rescue, group and reservation conditions");
            timingMode = (UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleintervalMode0");
            PHProbeCheck(timingMode.menu.children.count == 2 &&
                ((UIAction *)timingMode.menu.children[1]).state == UIMenuElementStateOn,
                @"Rebuilding the editor retains accepted timing in the selected menu state");
            [ruleEditor.view layoutIfNeeded];
            for (NSUInteger color = 0; color < 6; ++color) {
                UIButton *choice = (UIButton *)PHProbeFind(ruleEditor.view,
                    [NSString stringWithFormat:@"padcheaper.orbsRuleUnmatchable%lu-0", (unsigned long)color]);
                PHProbeCheck(choice && CGRectGetHeight(choice.bounds) >= 44, @"Every unmatchable color has a tappable selector");
            }
            [(UIButton *)PHProbeFind(ruleEditor.view,@"padcheaper.orbsRuleUnmatchable3-0") sendActionsForControlEvents:UIControlEventTouchUpInside];
            [(UIButton *)PHProbeFind(ruleEditor.view,@"padcheaper.orbsRuleUnmatchable5-0") sendActionsForControlEvents:UIControlEventTouchUpInside];
            [ruleEditor.view layoutIfNeeded];
            UILabel *colorTitle = (UILabel *)PHProbeFind(ruleEditor.view,@"padcheaper.orbsRuleUnmatchableTitle0");
            PHProbeCheck([ruleDraft[0][@"unmatchableColors"] isEqual:@40] &&
                [colorTitle.text containsString:@"光／心"] && [colorTitle.text containsString:@"任一"],
                @"Real color button actions select light or heart with ANY semantics");
            UIScrollView *colorScroll = PHProbeScrollParent(colorTitle);
            [colorScroll scrollRectToVisible:CGRectInset([colorTitle convertRect:colorTitle.bounds toView:colorScroll],0,-90) animated:NO];
            PHProbeRender(self.window,orbWindow,@"orbs-unmatchable-colors");
            [(UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == ruleEditor && [orbAction isEqual:@"orbsRules"] &&
                [orbValue count] == 1 && ruleDraft.count && [ruleDraft[0][@"floor"] isEqual:@3] &&
                [orbValue[0][@"hpAtMost"] isEqual:@40] && [orbValue[0][@"status"] isEqual:@"awoken_bind"] && [orbValue[0][@"required"] boolValue] &&
                [orbValue[0][@"every"] isEqual:@1] && [orbValue[0][@"group"] isEqual:@1] && [orbValue[0][@"reserveUntil"] isEqual:@2] &&
                [orbValue[0][@"intervalMode"] isEqual:@"accepted"] && [orbValue[0][@"startAfter"] isEqual:@2] &&
                [orbValue[0][@"unmatchableColors"] isEqual:@40] && [orbValue[0][@"obstacle"] isEqual:@"unmatchable"] &&
                [((UILabel *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleNotice")).text containsString:@"未能儲存"],
                @"Failed skill-rule saves preserve the draft and editor with a fixed error notice");
            UIScrollView *ruleScroll = (UIScrollView *)((UIStackView *)[ruleEditor valueForKey:@"rows"]).superview;
            [ruleScroll setContentOffset:CGPointZero animated:NO]; [ruleEditor.view layoutIfNeeded];
            PHProbeRender(self.window, orbWindow, @"orbs-skill-rules");
            rescuePayload = orbValue;
            [orbController setValue:[^(NSString *action, id value) {
                orbAction = action; orbValue = value;
                if ([action isEqual:@"orbsRules"]) {
                    NSMutableDictionary *state = [ruleFailure mutableCopy]; state[@"rules"] = value; state[@"ruleMessage"] = @"";
                    orbFixture = @{@"orbState":state};
                }
            } copy] forKey:@"action"];
            [(UIButton *)PHProbeFind(ruleEditor.view, @"padcheaper.orbsRuleSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"rule-save-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            PHProbeCheck(!orbController.presentedViewController && self.window.isKeyWindow && [orbValue isEqual:rescuePayload],
                @"Acknowledged rescue-rule save retains all conditions and returns keyboard focus to the game");
            [orbController setValue:[^(NSString *action, id value) { orbAction = action; orbValue = value; } copy] forKey:@"action"];
            recoveryFailure = [baseOrbFixture[@"orbState"] mutableCopy];
            recoveryFailure[@"recoveryStatus"] = @"本回合 HP 30.0% ≤ 40%：要求心珠四顆直線 × 2 組";
            orbFixture = @{@"orbState":recoveryFailure}; [orbController refresh];
            PHProbeCheck([((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsRecoveryDescription")).text containsString:@"本回合 HP 30.0%"],
                @"Recovery settings and the latest conditional-goal decision remain visible in the main page");
            recoveryFailure[@"recoveryMessage"] = @"心珠設定未能儲存，仍保留原設定";
            orbFixture = @{@"orbState":recoveryFailure}; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsRecoveryEdit") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"recovery-editor-keyboard-ready", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsRecoveryEditor", YES); }, ^{
            UIViewController *recoveryEditor = orbController.presentedViewController; [recoveryEditor loadViewIfNeeded];
            UITextField *recoveryHP = (UITextField *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoveryHP");
            recoveryHP.text = @"101"; [recoveryHP sendActionsForControlEvents:UIControlEventEditingChanged]; orbAction = nil;
            [(UIButton *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoverySave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(!orbAction && orbController.presentedViewController == recoveryEditor &&
                [((UILabel *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoveryNotice")).text containsString:@"0–100"],
                @"Invalid recovery percentages remain in the draft without publishing an action");
            recoveryHP.text = @"35"; [recoveryHP sendActionsForControlEvents:UIControlEventEditingChanged];
            UISegmentedControl *recoveryGroups = (UISegmentedControl *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoveryGroups");
            UISegmentedControl *recoveryShape = (UISegmentedControl *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoveryShape");
            recoveryGroups.selectedSegmentIndex = 2; [recoveryGroups sendActionsForControlEvents:UIControlEventValueChanged];
            recoveryShape.selectedSegmentIndex = 0; [recoveryShape sendActionsForControlEvents:UIControlEventValueChanged];
            wantedHearts = @{@"hpAtMost":@35,@"groups":@3,@"shape":@"match"};
            [(UIButton *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoverySave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == recoveryEditor && [orbAction isEqual:@"orbsRecovery"] &&
                [orbValue isEqual:wantedHearts] && [recoveryHP.text isEqual:@"35"] &&
                [((UILabel *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoveryNotice")).text containsString:@"未能儲存"],
                @"A failed recovery save preserves numeric, group and shape edits with a fixed error notice");
            PHProbeRender(self.window, orbWindow, @"orbs-recovery-failed-draft");
            [orbController setValue:[^(NSString *action, id value) {
                orbAction = action; orbValue = value;
                if ([action isEqual:@"orbsRecovery"]) {
                    NSMutableDictionary *state = [recoveryFailure mutableCopy]; state[@"recovery"] = value; state[@"recoveryMessage"] = @"";
                    orbFixture = @{@"orbState":state};
                }
            } copy] forKey:@"action"];
            [(UIButton *)PHProbeFind(recoveryEditor.view, @"padcheaper.orbsRecoverySave") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"recovery-retry-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            PHProbeCheck(!orbController.presentedViewController && self.window.isKeyWindow && [orbValue isEqual:wantedHearts],
                @"Acknowledged recovery retry saves the retained policy and returns keyboard focus to the game");
            [orbController setValue:[^(NSString *action, id value) { orbAction = action; orbValue = value; } copy] forKey:@"action"];
            profileFailure = [baseOrbFixture[@"orbState"] mutableCopy];
            profileFailure[@"recovery"] = wantedHearts;
            profileFailure[@"profileMessage"] = @"策略未能儲存，仍保留原設定";
            orbFixture = @{@"orbState":profileFailure}; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsProfileEdit") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"profile-editor-keyboard-ready", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsProfileEditor", YES); }, ^{
            UIViewController *profileEditor = orbController.presentedViewController; [profileEditor loadViewIfNeeded];
            UITextView *planInput = (UITextView *)PHProbeFind(profileEditor.view, @"padcheaper.orbsPlanJSON");
            PHProbeCheck(planInput && PHProbeFind(profileEditor.view, @"padcheaper.orbsPlanImport"), @"Complete-plan JSON input and validation action are available");
            planInput.text = @"{\"schema\":99}";
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsPlanImport") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck([[[profileEditor valueForKey:@"draft"] objectForKey:@"profiles"] count] == 0,
                @"Unsupported plan import changes no strategy draft");
            NSString *path = [NSBundle.mainBundle pathForResource:@"team-plan" ofType:@"json"];
            planInput.text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:NULL];
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsPlanImport") sendActionsForControlEvents:UIControlEventTouchUpInside];
            NSDictionary *planDraft = [profileEditor valueForKey:@"draft"];
            PHProbeCheck([planDraft[@"profiles"] count] == 1 && [planDraft[@"profiles"][0][@"opening"] count] == 6 &&
                PHProbeFind(profileEditor.view, @"padcheaper.orbsPlanExport"), @"Valid Uryu import previews all six opening steps and enables export");
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsPlanExport") sendActionsForControlEvents:UIControlEventTouchUpInside];
            NSData *exported = [UIPasteboard.generalPasteboard.string dataUsingEncoding:NSUTF8StringEncoding];
            NSDictionary *exportedPlan = exported ? [NSJSONSerialization JSONObjectWithData:exported options:0 error:NULL] : nil;
            PHProbeCheck([exportedPlan[@"format"] isEqual:@"padcheaper-team-plan"] && [exportedPlan[@"opening"] count] == 6 &&
                [exportedPlan[@"rotation"] count] == 11 &&
                [exportedPlan[@"rotation"][1][@"intervalMode"] isEqual:@"accepted"] &&
                [exportedPlan[@"rotation"][1][@"startAfter"] isEqual:@1] &&
                [exportedPlan[@"rotation"][5][@"color"] isEqual:@5] &&
                [exportedPlan[@"rotation"][5][@"below"] isEqual:@4],
                @"UI export preserves opening, first-puzzle Byakuya timing and independent shortage/rescue alternatives");
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == profileEditor && [orbValue[@"library"][@"profiles"][0][@"formation"] isEqual:exportedPlan[@"formation"]],
                @"Failed full-plan save keeps the reviewable formation and opening draft open");
            PHProbeRender(self.window, orbWindow, @"orbs-team-plan-import");
            NSDictionary *previousFixture = orbFixture;
            NSMutableDictionary *matchingPlan = [profileFailure mutableCopy];
            matchingPlan[@"profiles"] = orbValue[@"library"]; matchingPlan[@"profileMessage"] = @"";
            matchingPlan[@"quality"] = exportedPlan[@"board"][@"quality"];
            matchingPlan[@"goalMode"] = exportedPlan[@"board"][@"mode"];
            matchingPlan[@"goals"] = exportedPlan[@"board"][@"goals"];
            matchingPlan[@"recovery"] = exportedPlan[@"board"][@"recovery"];
            matchingPlan[@"rules"] = exportedPlan[@"rotation"];
            orbFixture = @{@"orbState":matchingPlan, @"formationDraft":@{}};
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == profileEditor &&
                [((UILabel *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileNotice")).text containsString:@"尚未確認"],
                @"Existing matching strategy settings cannot acknowledge a missing formation selection");
            matchingPlan[@"profileMessage"] = @"隊伍庫已滿；整包未儲存";
            orbFixture = @{@"orbState":matchingPlan, @"formationDraft":exportedPlan[@"formation"]};
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == profileEditor &&
                [((UILabel *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileNotice")).text containsString:@"隊伍庫已滿"],
                @"Explicit save failure cannot be masked by old matching formation and strategy settings");
            orbFixture = previousFixture; [orbController refresh];
            UILabel *planSummary = (UILabel *)PHProbeFind(profileEditor.view, @"padcheaper.orbsPlanSummary");
            UIScrollView *planScroll = PHProbeScrollParent(planSummary);
            [profileEditor.view layoutIfNeeded];
            CGRect summaryFrame = [planSummary convertRect:planSummary.bounds toView:planScroll];
            [planScroll setContentOffset:CGPointMake(0, MAX(0, CGRectGetMinY(summaryFrame))) animated:NO];
            PHProbeCheck(CGRectContainsPoint(planScroll.bounds, CGPointMake(CGRectGetMidX(summaryFrame), CGRectGetMinY(summaryFrame) + 12)) &&
                [planSummary.text containsString:@"開場（依序完成）"] && [planSummary.text containsString:@"→ #14038"],
                @"The imported six-member/opening summary can scroll into view and shows the expected transformation");
            PHProbeRender(self.window, orbWindow, @"orbs-team-plan-summary");
            shareDraft = [NSJSONSerialization JSONObjectWithData:[NSJSONSerialization dataWithJSONObject:
                [profileEditor valueForKey:@"draft"] options:0 error:NULL] options:0 error:NULL];
            shareConfig = PHProbeJSON(@"config.json");
            UIButton *share = (UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileShare");
            PHProbeCheck(share.enabled, @"Selected complete plan exposes a native file share action");
            UIScrollView *shareScroll = PHProbeScrollParent(share);
            [shareScroll scrollRectToVisible:[share convertRect:share.bounds toView:shareScroll] animated:NO];
            [share sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"profile-share-sheet-visible", ^BOOL {
            return PHProbePresented(orbController.presentedViewController, @"padcheaper.orbsProfileShareSheet", NO);
        }, ^{
            UIViewController *profileEditor = orbController.presentedViewController;
            UIActivityViewController *sheet = (UIActivityViewController *)profileEditor.presentedViewController;
            PHProbeCheck([sheet isKindOfClass:UIActivityViewController.class], @"File sharing presents UIKit's native activity controller");
            shareDirectory = [profileEditor valueForKey:@"shareDirectory"];
            NSArray<NSURL *> *files = [NSFileManager.defaultManager contentsOfDirectoryAtURL:shareDirectory
                includingPropertiesForKeys:nil options:0 error:NULL];
            NSData *data = files.count == 1 ? [NSData dataWithContentsOfURL:files.firstObject] : nil;
            NSDictionary *package = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
            PHProbeCheck(files.count == 1 && [files.firstObject.pathExtension isEqual:@"json"] &&
                [package[@"format"] isEqual:@"padcheaper-team-plan"] && [package[@"opening"] count] == 6 &&
                !package[@"pid"] && !package[@"apiHosts"], @"Native share uses a real JSON file with the selected plan only");
            PHProbeCheck([[profileEditor valueForKey:@"draft"] isEqual:shareDraft] &&
                [PHProbeJSON(@"config.json") isEqual:shareConfig], @"Opening a share sheet cannot save or alter the strategy");
            PHProbeRender(self.window, orbWindow, @"orbs-strategy-share");
            [sheet dismissViewControllerAnimated:YES completion:^{
                // UIKit's completion contract is exercised without sending a file to another app.
                if (sheet.completionWithItemsHandler) sheet.completionWithItemsHandler(nil, NO, nil, nil);
            }];
        }),
        PHProbeStep(@"profile-share-cancelled", ^BOOL {
            return PHProbePresented(orbController, @"padcheaper.orbsProfileEditor", YES) &&
                !orbController.presentedViewController.presentedViewController &&
                !orbController.presentedViewController.transitionCoordinator;
        }, ^{
            UIViewController *profileEditor = orbController.presentedViewController;
            PHProbeCheck(![NSFileManager.defaultManager fileExistsAtPath:shareDirectory.path] &&
                ![profileEditor valueForKey:@"shareDirectory"] && [[profileEditor valueForKey:@"draft"] isEqual:shareDraft],
                @"Cancelling sharing removes the temporary file and preserves the complete draft");
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileFileImport") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"profile-file-picker-visible", ^BOOL {
            UIViewController *editor = orbController.presentedViewController, *picker = editor.presentedViewController;
            return [picker isKindOfClass:UIDocumentPickerViewController.class] && picker.viewIfLoaded.window &&
                !picker.isBeingPresented && !picker.transitionCoordinator && !editor.transitionCoordinator;
        }, ^{
            UIViewController *profileEditor = orbController.presentedViewController;
            UIDocumentPickerViewController *picker = (UIDocumentPickerViewController *)profileEditor.presentedViewController;
            profilePicker = picker;
            PHProbeCheck(!picker.allowsMultipleSelection && picker.delegate == (id)profileEditor,
                @"JSON import presents the single-document native file picker");
            PHProbeRender(self.window, orbWindow, @"orbs-strategy-file-picker");
            [picker dismissViewControllerAnimated:YES completion:^{
                [picker.delegate documentPickerWasCancelled:picker];
            }];
        }),
        PHProbeStep(@"profile-file-picker-cancelled", ^BOOL {
            return PHProbePresented(orbController, @"padcheaper.orbsProfileEditor", YES) &&
                !orbController.presentedViewController.presentedViewController &&
                !orbController.presentedViewController.transitionCoordinator;
        }, ^{
            UIViewController *profileEditor = orbController.presentedViewController;
            PHProbeCheck([[profileEditor valueForKey:@"draft"] isEqual:shareDraft] &&
                [PHProbeJSON(@"config.json") isEqual:shareConfig], @"File-picker cancellation preserves saved settings and unsaved changes");
            NSURL *fixture = [NSBundle.mainBundle URLForResource:@"team-plan" withExtension:@"json"];
            [(id<UIDocumentPickerDelegate>)profileEditor documentPicker:profilePicker didPickDocumentsAtURLs:@[fixture]];
            PHProbeCheck([[profileEditor valueForKey:@"draft"] isEqual:shareDraft] &&
                [((UILabel *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileNotice")).text containsString:@"ID"],
                @"A duplicate file import does not replace the reviewed plan");
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileDelete") sendActionsForControlEvents:UIControlEventTouchUpInside];
            [(id<UIDocumentPickerDelegate>)profileEditor documentPicker:profilePicker didPickDocumentsAtURLs:@[fixture]];
            PHProbeCheck([[[profileEditor valueForKey:@"draft"] objectForKey:@"profiles"] count] == 1 &&
                [PHProbeJSON(@"config.json") isEqual:shareConfig] &&
                [((UILabel *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileNotice")).text containsString:@"已驗證並加入草稿"] &&
                [((UILabel *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileNotice")).text containsString:@"儲存並選用"],
                @"A file delegate reads real JSON into preview without publishing the draft");
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileDelete") sendActionsForControlEvents:UIControlEventTouchUpInside];
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileAdd") sendActionsForControlEvents:UIControlEventTouchUpInside];
            [profileEditor.view layoutIfNeeded];
            UITextField *profileName = (UITextField *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileName");
            PHProbeCheck(orbWindow.isKeyWindow && [profileName becomeFirstResponder], @"Strategy sheet accepts its name with the keyboard");
            profileName.text = @"循環策略"; [profileName sendActionsForControlEvents:UIControlEventEditingChanged]; [profileName resignFirstResponder];
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbController.presentedViewController == profileEditor && [orbAction isEqual:@"orbsProfiles"] &&
                [orbValue[@"library"][@"profiles"] count] == 1 &&
                [orbValue[@"library"][@"profiles"][0][@"settings"][@"recovery"] isEqual:wantedHearts] &&
                [((UILabel *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileNotice")).text containsString:@"未能儲存"],
                @"Failed strategy save retains its draft, selection and fixed notice");
            PHProbeRender(self.window, orbWindow, @"orbs-strategy-profiles");
            profilePayload = orbValue;
            [orbController setValue:[^(NSString *action, id value) {
                orbAction = action; orbValue = value;
                if ([action isEqual:@"orbsProfiles"]) {
                    NSMutableDictionary *state = [profileFailure mutableCopy];
                    state[@"profiles"] = value[@"library"]; state[@"profileMessage"] = @"";
                    orbFixture = @{@"orbState":state};
                }
            } copy] forKey:@"action"];
            [(UIButton *)PHProbeFind(profileEditor.view, @"padcheaper.orbsProfileSave") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"restore-retry-prepared", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            restoreOriginalSnapshot = [overlay.rootViewController valueForKey:@"snapshot"];
            restoreOriginalAction = [overlay.rootViewController valueForKey:@"action"];
            restoreFixture = [restoreOriginalSnapshot() mutableCopy];
            NSMutableDictionary *battle = [restoreFixture[@"battleState"] mutableCopy];
            battle[@"canRestore"] = @YES; battle[@"canRetryRestore"] = @YES;
            battle[@"message"] = @"還原未完成：位置 1、2、3、4、5；請按「重試未完成還原」";
            restoreFixture[@"battleState"] = battle;
            [overlay.rootViewController setValue:[^{ return restoreFixture; } copy] forKey:@"snapshot"];
            [overlay.rootViewController setValue:[^(NSString *action, id value) { restoreAction = action; } copy] forKey:@"action"];
            [overlay.rootViewController updateWithSnapshot:restoreFixture];
            UIButton *retry = (UIButton *)PHProbeFind(overlay, @"padcheaper.battleRestore");
            UIScrollView *scroll = PHProbeScrollParent(retry);
            [overlay layoutIfNeeded];
            [scroll scrollRectToVisible:[retry convertRect:retry.bounds toView:scroll] animated:NO];
            // UIButton fades its title when enabled changes. Let UIKit finish
            // that real transition before inspecting or rendering the label.
            restoreReadyAt = NSProcessInfo.processInfo.systemUptime + 0.35;
        }),
        PHProbeStep(@"restore-retry-visible", ^BOOL {
            UIButton *retry = (UIButton *)PHProbeFind(overlay, @"padcheaper.battleRestore");
            // The real host refreshes diagnostic state once per second. A
            // tick may overwrite this isolated GUI fixture while we yield.
            if (!retry.enabled || ![retry.currentTitle isEqual:@"重試未完成還原"]) {
                [overlay.rootViewController updateWithSnapshot:restoreFixture];
                restoreReadyAt = NSProcessInfo.processInfo.systemUptime + 0.35;
                return NO;
            }
            return NSProcessInfo.processInfo.systemUptime >= restoreReadyAt &&
                [retry.titleLabel.text isEqual:@"重試未完成還原"] && !retry.titleLabel.layer.animationKeys.count;
        }, ^{
            UIButton *retry = (UIButton *)PHProbeFind(overlay, @"padcheaper.battleRestore");
            CGRect title = [retry.titleLabel convertRect:retry.titleLabel.bounds toView:overlay];
            PHProbeCheck(retry.enabled && !retry.titleLabel.hidden && retry.titleLabel.alpha > 0.9 &&
                CGRectGetWidth(title) > 0 && CGRectContainsRect(overlay.bounds, title),
                @"Restore retry title is visibly laid out after the button state transition");
            PHProbeRender(self.window, overlay, @"battle-restore-retry");
            [retry sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck([restoreAction isEqual:@"battleRestore"], @"Visible restore retry invokes the existing restoration action");
            [overlay.rootViewController setValue:restoreOriginalSnapshot forKey:@"snapshot"];
            [overlay.rootViewController setValue:restoreOriginalAction forKey:@"action"];
            [overlay.rootViewController updateWithSnapshot:restoreOriginalSnapshot()];
            PHProbeCheck(!retry.enabled && [retry.currentTitle isEqual:@"全部還原／取消"],
                @"A later idle snapshot clears the restore retry title and availability");
        }),
        PHProbeStep(@"profile-save-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            PHProbeCheck(!orbController.presentedViewController && self.window.isKeyWindow && [orbValue isEqual:profilePayload],
                @"Acknowledged strategy save publishes the retained draft and restores game focus");
            [orbController setValue:[^(NSString *action, id value) { orbAction = action; orbValue = value; } copy] forKey:@"action"];
            orbFixture = baseOrbFixture; orbAction = nil; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsStart") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck([orbAction isEqual:@"orbsStart"] && [overlay.rootViewController valueForKey:@"orbWindow"] == orbWindow,
                @"Unacknowledged continuous start leaves the page open to show its status");
            PHOverlaySnapshot originalSnapshot = [overlay.rootViewController valueForKey:@"snapshot"];
            PHOverlayAction originalAction = [overlay.rootViewController valueForKey:@"action"];
            NSNumber *originalExpanded = [overlay.rootViewController valueForKey:@"expanded"];
            NSNumber *originalCollapsed = [overlay.rootViewController valueForKey:@"shortcutsCollapsed"];
            UIButton *onceShortcut = (UIButton *)PHProbeFind(overlay, @"padcheaper.orbsOnceShortcut");
            UIButton *autoShortcut = (UIButton *)PHProbeFind(overlay, @"padcheaper.orbsAutoShortcut");
            UIButton *collapseShortcut = (UIButton *)PHProbeFind(overlay, @"padcheaper.collapseShortcuts");
            UILabel *shortcutStatus = (UILabel *)PHProbeFind(overlay, @"padcheaper.orbsShortcutStatus");
            NSMutableDictionary *shortcutState = [originalSnapshot() mutableCopy];
            shortcutState[@"orbState"] = @{@"phase":@"complete", @"message":@"轉珠完成，可再次操作",
                @"canOnce":@YES, @"canStart":@YES, @"busy":@NO};
            [overlay.rootViewController setValue:[^{ return shortcutState; } copy] forKey:@"snapshot"];
            __block NSString *shortcutAction = nil;
            __block NSUInteger shortcutActionCount = 0;
            [overlay.rootViewController setValue:[^(NSString *action, id value) {
                shortcutAction = action; shortcutActionCount++;
            } copy] forKey:@"action"];
            [overlay.rootViewController setValue:@NO forKey:@"expanded"];
            [overlay.rootViewController setValue:@YES forKey:@"shortcutsCollapsed"];
            [overlay.rootViewController updateWithSnapshot:shortcutState];
            [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(shortcutActionCount == 0 && !shortcutStatus.hidden && PHProbeFind(overlay, @"padcheaper.panel").hidden,
                @"Unfolding idle shortcuts restores status without dispatching a game or configuration action");
            PHProbeFloatingLayout(overlay);
            [onceShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(shortcutActionCount == 1 && [shortcutAction isEqual:@"orbsOnce"] && PHProbeFind(overlay, @"padcheaper.panel").hidden,
                @"One-shot shortcut queues read/solve/execute without opening settings");
            shortcutAction = nil; [autoShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck([shortcutAction isEqual:@"orbsStart"], @"Continuous shortcut uses the existing explicit-start action");
            shortcutState[@"orbState"] = @{@"phase":@"idle", @"message":@"3 人協力：自己的回合", @"multiplayerActive":@YES,
                @"canOnce":@YES, @"canStart":@NO, @"canExecute":@NO, @"busy":@NO};
            [overlay.rootViewController updateWithSnapshot:shortcutState];
            shortcutAction = nil; [autoShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(onceShortcut.enabled && !autoShortcut.enabled && shortcutAction == nil,
                @"Multiplayer leaves explicit once available while rejecting continuous shortcut actions");
            [onceShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck([shortcutAction isEqual:@"orbsOnce"], @"Multiplayer shortcut invokes only explicit once");
            for (NSString *mode in @[@"3 人協力", @"4 人對戰", @"8 人對戰"]) {
                NSMutableDictionary *multiplayerStatus = [shortcutState[@"orbState"] mutableCopy];
                NSString *reason = [mode hasPrefix:@"3"] ? @"等待自己的回合，再按轉一次" : @"等待對戰開始倒數完成";
                multiplayerStatus[@"phase"] = @"stopped";
                multiplayerStatus[@"message"] = @"上次盤面未達目標";
                multiplayerStatus[@"canOnce"] = @NO;
                multiplayerStatus[@"multiplayerMessage"] = [reason stringByAppendingFormat:@"\n%@ · 本機 1 · 回合 2", mode];
                shortcutState[@"orbState"] = multiplayerStatus;
                [overlay.rootViewController updateWithSnapshot:shortcutState];
                shortcutAction = nil; shortcutActionCount = 0;
                [onceShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
                [autoShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
                PHProbeCheck(!onceShortcut.enabled && !autoShortcut.enabled && shortcutActionCount == 0 &&
                    [shortcutStatus.text isEqual:reason] && [shortcutStatus.accessibilityLabel containsString:mode] &&
                    [shortcutStatus.accessibilityLabel containsString:@"上次盤面未達目標"] &&
                    [onceShortcut.accessibilityHint containsString:reason],
                    @"Multiplayer idle shortcuts show the current gate first and retain the last result for accessibility");
                [collapseShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
                multiplayerStatus[@"canOnce"] = @YES;
                multiplayerStatus[@"multiplayerMessage"] = [NSString stringWithFormat:@"可轉一次；完成後停止\n%@ · 本機 1 · 回合 3", mode];
                [overlay.rootViewController updateWithSnapshot:shortcutState];
                PHProbeCheck(onceShortcut.hidden && shortcutStatus.hidden && shortcutActionCount == 0,
                    @"A newly eligible multiplayer turn cannot unfold the overlay or start input");
                [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
                PHProbeCheck(onceShortcut.enabled && !autoShortcut.enabled && !shortcutStatus.hidden && shortcutActionCount == 0 &&
                    [shortcutStatus.text isEqual:@"可轉一次；完成後停止"] &&
                    [shortcutStatus.accessibilityLabel containsString:@"上次盤面未達目標"],
                    @"Unfolding after the turn changes shows current readiness without losing the previous result");
                multiplayerStatus[@"canOnce"] = @NO;
                multiplayerStatus[@"multiplayerMessage"] = @"隊伍正在套用或還原，完成後再按轉一次";
                [overlay.rootViewController updateWithSnapshot:shortcutState];
                PHProbeCheck(!onceShortcut.enabled && [shortcutStatus.text containsString:@"隊伍正在套用或還原"],
                    @"The runtime team-operation gate has a matching disabled-button explanation");
            }
            NSMutableDictionary *nextBoardStatus = [shortcutState[@"orbState"] mutableCopy];
            nextBoardStatus[@"phase"] = @"complete"; nextBoardStatus[@"completed"] = @1;
            nextBoardStatus[@"message"] = @"本次路徑已執行並放開；下一盤可操作時可再按轉一次";
            nextBoardStatus[@"multiplayerMessage"] = @"等待本盤結算或下一盤\n4 人對戰 · 本機 3／4";
            nextBoardStatus[@"canOnce"] = @NO;
            shortcutState[@"orbState"] = nextBoardStatus;
            [overlay.rootViewController updateWithSnapshot:shortcutState];
            shortcutAction = nil; shortcutActionCount = 0;
            PHProbeCheck(!onceShortcut.enabled && [shortcutStatus.text containsString:@"等待本盤結算"],
                @"Successful first release still waits for the next playable board");
            nextBoardStatus[@"canOnce"] = @YES;
            nextBoardStatus[@"multiplayerMessage"] = @"可轉一次；完成後停止\n4 人對戰 · 本機 3／4";
            [overlay.rootViewController updateWithSnapshot:shortcutState];
            PHProbeCheck(onceShortcut.enabled && !autoShortcut.enabled && shortcutActionCount == 0 &&
                [shortcutStatus.text isEqual:@"可轉一次；完成後停止"],
                @"A completed session's next-board refresh reenables once without submitting a gesture");
            [onceShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(shortcutActionCount == 1 && [shortcutAction isEqual:@"orbsOnce"],
                @"Only a second explicit shortcut tap submits the next-board action");
            shortcutState[@"orbState"] = @{@"phase":@"stopped",@"message":@"目標未達成，未開始轉珠",@"canOnce":@NO,@"canStart":@NO,@"busy":@NO};
            [overlay.rootViewController updateWithSnapshot:shortcutState];
            PHProbeCheck(!onceShortcut.enabled && !autoShortcut.enabled &&
                !shortcutStatus.hidden && collapseShortcut.enabled,
                @"Disabled shortcuts retain a visible result without requiring the settings page");
            NSString *stoppedMessage = shortcutStatus.text;
            NSMutableArray<NSValue *> *oldShortcutCenters = [NSMutableArray array];
            for (UIView *control in @[onceShortcut, autoShortcut, collapseShortcut, shortcutStatus])
                [oldShortcutCenters addObject:[NSValue valueWithCGPoint:[control convertPoint:
                    CGPointMake(CGRectGetMidX(control.bounds), CGRectGetMidY(control.bounds)) toView:overlay]]];
            shortcutAction = nil; shortcutActionCount = 0;
            [collapseShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
            [overlay.rootViewController updateWithSnapshot:shortcutState];
            PHProbeCheck(shortcutActionCount == 0 && !bubble.hidden && onceShortcut.hidden && autoShortcut.hidden &&
                collapseShortcut.hidden && shortcutStatus.hidden &&
                [[overlay.rootViewController valueForKey:@"shortcutsCollapsed"] boolValue],
                @"Folded controls and status stay hidden after refresh without changing execution or configuration");
            for (NSValue *point in oldShortcutCenters)
                PHProbeCheck([overlay hitTest:point.CGPointValue withEvent:nil] == nil,
                    @"All formerly visible shortcut and status areas pass touches through when folded");
            [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(shortcutActionCount == 0 && !shortcutStatus.hidden && [shortcutStatus.text isEqual:stoppedMessage] &&
                !onceShortcut.enabled && !autoShortcut.enabled,
                @"Unfolding restores the last result and preserves disabled input gates");
            [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(!PHProbeFind(overlay, @"padcheaper.panel").hidden && onceShortcut.hidden &&
                autoShortcut.hidden && collapseShortcut.hidden && shortcutStatus.hidden,
                @"Opening settings hides the complete floating row");
            [(UIButton *)PHProbeFind(overlay, @"padcheaper.close") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(shortcutActionCount == 0 && !collapseShortcut.hidden && !shortcutStatus.hidden &&
                ![[overlay.rootViewController valueForKey:@"shortcutsCollapsed"] boolValue],
                @"Closing settings retains the expanded shortcut preference without a game action");
            [overlay.rootViewController.view layoutIfNeeded];
            // Render these two windows directly. Hiding and immediately
            // reattaching the live editor window solely for a screenshot
            // starts a UIKit scene transition unrelated to the user action.
            PHProbeRender(self.window, overlay, @"orb-shortcuts");
            for (NSNumber *automatic in @[@NO, @YES]) {
              NSArray<NSString *> *phases = automatic.boolValue ? @[@"autoWaiting", @"solving", @"skillWaiting", @"turnWaiting"] :
                  @[@"reading", @"solving", @"arming", @"running", @"releasing"];
              for (NSString *phase in phases) {
                NSMutableDictionary *busyOrbs = [baseOrbFixture[@"orbState"] mutableCopy];
                busyOrbs[@"phase"] = phase; busyOrbs[@"automatic"] = automatic; busyOrbs[@"busy"] = @YES;
                busyOrbs[@"once"] = @(!automatic.boolValue); busyOrbs[@"canOnce"] = @NO;
                busyOrbs[@"canStart"] = @NO; busyOrbs[@"canStop"] = @YES; busyOrbs[@"canRead"] = @NO; busyOrbs[@"canExecute"] = @NO;
                orbFixture = @{@"orbState":busyOrbs}; orbAction = nil; [orbController refresh];
                [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsStop") sendActionsForControlEvents:UIControlEventTouchUpInside];
                PHProbeCheck([orbAction isEqual:@"orbsStop"] && ![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsRuleEdit") isEnabled] &&
                    ![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsProfileEdit") isEnabled] &&
                    ![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsStart") isEnabled],
                    @"Busy one-shot and automatic phases expose stop and lock editing and duplicate start");
                shortcutState[@"orbState"] = busyOrbs;
                [overlay.rootViewController setValue:@NO forKey:@"shortcutsCollapsed"];
                [overlay.rootViewController updateWithSnapshot:shortcutState];
                [overlay.rootViewController.view setNeedsLayout]; [overlay.rootViewController.view layoutIfNeeded];
                shortcutAction = nil; shortcutActionCount = 0;
                [autoShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
                PHProbeCheck(!autoShortcut.hidden && [autoShortcut.currentTitle isEqual:@"停止"] &&
                    shortcutActionCount == 1 && [shortcutAction isEqual:@"orbsStop"] && !onceShortcut.enabled,
                    @"The continuous shortcut becomes stop and prevents a competing one-shot route while busy");
                shortcutAction = nil; shortcutActionCount = 0;
                [collapseShortcut sendActionsForControlEvents:UIControlEventTouchUpInside];
                [overlay.rootViewController updateWithSnapshot:shortcutState];
                PHProbeCheck(shortcutActionCount == 0 && !bubble.hidden && [bubble.currentTitle containsString:@"停止"] &&
                    [bubble.accessibilityHint containsString:@"不必展開"] && onceShortcut.hidden && autoShortcut.hidden &&
                    collapseShortcut.hidden && shortcutStatus.hidden,
                    @"Folding an active job retains a visible stop button without starting or stopping anything");
                [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
                PHProbeCheck(shortcutActionCount == 1 && [shortcutAction isEqual:@"orbsStop"] &&
                    [[overlay.rootViewController valueForKey:@"shortcutsCollapsed"] boolValue] && PHProbeFind(overlay, @"padcheaper.panel").hidden,
                    @"A folded busy bubble dispatches only one immediate stop and remains folded");
              }
            }
            shortcutState[@"orbState"] = @{@"phase":@"complete", @"message":@"完成", @"busy":@NO, @"canOnce":@YES, @"canStart":@YES};
            shortcutAction = nil; shortcutActionCount = 0;
            [overlay.rootViewController updateWithSnapshot:shortcutState];
            // A late editor-close callback must not undo folding or dispatch input.
            [(UIButton *)PHProbeFind(overlay, @"padcheaper.close") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(shortcutActionCount == 0 && !bubble.hidden && onceShortcut.hidden && autoShortcut.hidden &&
                collapseShortcut.hidden && shortcutStatus.hidden &&
                [[overlay.rootViewController valueForKey:@"shortcutsCollapsed"] boolValue],
                @"Job completion and panel close retain the folded preference");
            // The next tap must consult the current job even before the periodic UI refresh.
            shortcutState[@"orbState"] = @{@"phase":@"running", @"busy":@YES, @"automatic":@NO,
                @"canOnce":@NO, @"canStart":@NO};
            [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(shortcutActionCount == 1 && [shortcutAction isEqual:@"orbsStop"] &&
                [[overlay.rootViewController valueForKey:@"shortcutsCollapsed"] boolValue] && onceShortcut.hidden,
                @"A stale idle bubble rechecks the live job and stops it without unfolding");
            [overlay.rootViewController setValue:originalSnapshot forKey:@"snapshot"];
            [overlay.rootViewController setValue:originalAction forKey:@"action"];
            [overlay.rootViewController setValue:originalExpanded forKey:@"expanded"];
            [overlay.rootViewController setValue:originalCollapsed forKey:@"shortcutsCollapsed"];
            [overlay.rootViewController updateWithSnapshot:originalSnapshot()];
            [overlay.rootViewController.view setNeedsLayout]; [overlay.rootViewController.view layoutIfNeeded];
            NSMutableDictionary *conditionalLeader = [baseOrbFixture[@"orbState"] mutableCopy];
            conditionalLeader[@"goalMode"] = @"leaders";
            conditionalLeader[@"goalPlan"] = @{@"available":@YES, @"baseGoals":@[],
                @"goals":@[@{@"kind":@"match",@"colors":@32,@"count":@1,@"length":@3}], @"leaders":@[], @"message":@"心珠要求", @"warning":@""};
            orbFixture = @{@"orbState":conditionalLeader}; [orbController refresh];
            PHProbeCheck(PHProbeFind(orbWindow, @"padcheaper.orbsGoalEdit").hidden,
                @"A purely conditional heart goal does not offer to copy nonexistent leader requirements");
            conditionalLeader[@"goalPlan"] = @{@"available":@YES, @"baseGoals":savedGoals,
                @"goals":[savedGoals arrayByAddingObject:@{@"kind":@"match",@"colors":@32,@"count":@1,@"length":@3}],
                @"leaders":@[], @"message":@"本回合含心珠要求", @"warning":@""};
            orbFixture = @{@"orbState":conditionalLeader}; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsGoalEdit") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"leader-goal-editor-visible", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsGoalEditor", NO); }, ^{
            UIViewController *baseGoalEditor = orbController.presentedViewController; [baseGoalEditor loadViewIfNeeded];
            PHProbeCheck([[baseGoalEditor valueForKey:@"draft"] isEqual:savedGoals],
                @"Customizing leader goals copies the base plan without making a transient heart condition permanent");
            [baseGoalEditor dismissViewControllerAnimated:NO completion:nil];
        }),
        PHProbeStep(@"leader-goal-editor-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            NSMutableDictionary *multiLeader = [baseOrbFixture[@"orbState"] mutableCopy];
            multiLeader[@"multiplayerActive"] = @YES; multiLeader[@"busy"] = @NO;
            multiLeader[@"goalMode"] = @"leaders"; multiLeader[@"canExecute"] = @NO;
            multiLeader[@"goalPlan"] = @{@"available":@YES,
                @"goals":@[@{@"kind":@"cross",@"colors":@32,@"count":@1,@"length":@0}],
                @"leaders":@[@"舊隊長"],@"message":@"舊條件"};
            multiLeader[@"path"] = @[@0,@1]; multiLeader[@"goalHits"] = @[@9];
            multiLeader[@"liveLeaderPlan"] = @{@"available":@YES,@"goals":savedGoals,
                @"leaders":@[@"隊長 #14242",@"助人 · 4 人額外技能（不提供助人隊長技）"],
                @"message":@"目前本機盤面條件",@"warning":@"多人只規劃最低盤面條件"};
            orbFixture = @{@"orbState":multiLeader}; orbAction = nil; [orbController refresh];
            NSString *multiText = ((UILabel *)PHProbeFind(orbWindow,@"padcheaper.orbsGoalDescription")).text;
            PHProbeCheck([multiText containsString:@"#14242"] && [multiText containsString:@"額外技能"] &&
                [multiText containsString:@"目前本機"] && ![multiText containsString:@"舊"] && ![multiText containsString:@"✓"] &&
                !PHProbeFind(orbWindow,@"padcheaper.orbsGoalEdit").hidden && !orbAction,
                @"Idle multiplayer displays the live local leaders without attaching old route hits or submitting input");
            multiLeader[@"liveLeaderPlan"] = @{@"available":@NO,@"goals":@[],@"leaders":@[],
                @"message":@"未能確認本機回合的隊長／助人"};
            orbFixture = @{@"orbState":multiLeader}; [orbController refresh];
            PHProbeCheck(PHProbeFind(orbWindow,@"padcheaper.orbsGoalEdit").hidden &&
                [((UILabel *)PHProbeFind(orbWindow,@"padcheaper.orbsGoalDescription")).text containsString:@"未能確認"],
                @"Missing current multiplayer pair hides stale customization and explains unavailable conditions");
            multiLeader[@"liveLeaderPlan"] = @{@"available":@YES,@"goals":savedGoals,
                @"leaders":@[@"隊長 #14242"],@"message":@"目前本機盤面條件"};
            orbFixture = @{@"orbState":multiLeader}; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow,@"padcheaper.orbsGoalEdit") sendActionsForControlEvents:UIControlEventTouchUpInside];
        }),
        PHProbeStep(@"multiplayer-leader-goal-editor-visible", ^BOOL { return PHProbePresented(orbController, @"padcheaper.orbsGoalEditor", NO); }, ^{
            UIViewController *editor = orbController.presentedViewController; [editor loadViewIfNeeded];
            PHProbeCheck([[editor valueForKey:@"draft"] isEqual:savedGoals],
                @"Multiplayer customization copies the currently displayed local leader conditions");
            [editor dismissViewControllerAnimated:NO completion:nil];
        }),
        PHProbeStep(@"multiplayer-leader-goal-editor-dismissed", ^BOOL { return PHProbeClosed(orbController, self.window); }, ^{
            NSMutableDictionary *temporaryLeader = [baseOrbFixture[@"orbState"] mutableCopy];
            temporaryLeader[@"goalMode"] = @"manual"; temporaryLeader[@"path"] = @[];
            temporaryLeader[@"goalPlan"] = @{@"available":@YES, @"goals":savedGoals,
                @"warning":@"隊長／助人正在暫時變身；保留指定盤面目標，這些目標不代表目前隊長的倍率或減傷已發動。"};
            orbFixture = @{@"orbState":temporaryLeader}; [orbController refresh];
            UILabel *temporaryWarning = (UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsGoalDescription");
            PHProbeCheck([temporaryWarning.text containsString:@"暫時變身"] && [temporaryWarning.text containsString:@"減傷"],
                @"Manual goals display the temporary leader warning instead of hiding all non-leader-mode warnings");
            [orbController.view layoutIfNeeded];
            UIScrollView *warningScroll = PHProbeScrollParent(temporaryWarning);
            [warningScroll scrollRectToVisible:[temporaryWarning convertRect:temporaryWarning.bounds toView:warningScroll] animated:NO];
            [orbController.view layoutIfNeeded];
            PHProbeRender(self.window, orbWindow, @"orbs-temporary-leader");
            NSMutableDictionary *unsupportedLeader = [baseOrbFixture[@"orbState"] mutableCopy];
            unsupportedLeader[@"phase"] = @"blocked"; unsupportedLeader[@"goalMode"] = @"leaders";
            unsupportedLeader[@"canExecute"] = @NO; unsupportedLeader[@"path"] = @[];
            unsupportedLeader[@"goalPlan"] = @{@"available":@NO, @"goals":@[], @"leaders":@[@"隊長 #3230"],
                @"message":@"請改用指定形狀。", @"warning":@"僅規劃盤面條件。",
                @"unsupported":@[@"尚未支援的盤面規則（技能類型 119）"]};
            orbFixture = @{@"orbState":unsupportedLeader}; orbAction = nil; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck(orbAction == nil && ![(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") isEnabled] &&
                [((UILabel *)PHProbeFind(orbWindow, @"padcheaper.orbsGoalDescription")).text containsString:@"技能類型 119"],
                @"Unsupported leader rules show their specific reason and cannot dispatch input");
            orbFixture = baseOrbFixture; [orbController refresh];
            [(UIButton *)PHProbeFind(orbWindow, @"padcheaper.orbsExecute") sendActionsForControlEvents:UIControlEventTouchUpInside];
            PHProbeCheck([orbAction isEqual:@"orbsExecute"] && [orbValue isEqual:@19] &&
                [overlay.rootViewController valueForKey:@"orbWindow"] == nil && self.window.isKeyWindow,
                @"Execute passes the displayed preview generation and returns to game");
            if (PHProbeLegacy()) PHProbeLegacyOverlay = overlay;
            PHProbePlanStorage(overlay);
            PHProbeOpenPanel(overlay);
            UIButton *formation = (UIButton *)PHProbeFind(overlay, @"padcheaper.formationOpen");
            [formation sendActionsForControlEvents:UIControlEventTouchUpInside];
            UIWindow *formationWindow = [overlay.rootViewController valueForKey:@"formationWindow"];
            self.formationWeb = (WKWebView *)PHProbeFind(formationWindow, @"padcheaper.formationWeb");
            PHProbeCheck(formationWindow.isKeyWindow && self.formationWeb != nil, @"formation editor has an editable web view and keyboard-capable window");
            [self checkFormation];
        }),
    ];
    dispatch_async(dispatch_get_main_queue(), ^{
        PHProbeRunSteps(steps, 0, NSProcessInfo.processInfo.systemUptime + 10, self.window, orbWindow);
    });
}

- (void)checkFormation {
    self.formationAttempts++;
    if (!self.formationWeb) { [self finishPortrait]; return; }
    [self.formationWeb evaluateJavaScript:@"Boolean(window.PHFormationUI && window.PHFormationUI.initialized)" completionHandler:^(id result, NSError *error) {
        if (error || ![result boolValue]) {
            if (self.formationAttempts < 60) [self performSelector:@selector(checkFormation) withObject:nil afterDelay:0.5];
            else { PHProbeCheck(NO, @"Offline formation editor did not finish its native bridge handshake"); [self finishPortrait]; }
            return;
        }
        NSString *script = @"(()=>{const m=PHFormation.createModel(PH_CATALOG);let t=m.empty();"
            "t.title='隊伍 A';"
            "const ids=[[14238,14132,153],[14246,13334,142],[14240,8484,111],[14244,13711,142],[14234,13535,142],[14234,12460,142]];"
            "ids.forEach(([b,a,s],i)=>{t=m.selectCard(t,i,'body',b);t=m.selectCard(t,i,'assist',a);t.slots[i].body.superAwakening=s;});"
            "PHFormationUI.receive({team:t});document.getElementById('save').click();"
            "return {bodies:document.querySelectorAll('#body-grid .card').length,assists:document.querySelectorAll('#assist-grid .card').length,"
            "name:document.getElementById('selected-name').textContent,applyDisabled:document.getElementById('apply-all').disabled,"
            "fits:document.documentElement.scrollWidth<=innerWidth};})()";
        [self.formationWeb evaluateJavaScript:script completionHandler:^(id value, NSError *failure) {
            PHProbeCheck(!failure && [value[@"bodies"] isEqual:@6] && [value[@"assists"] isEqual:@6], @"editor renders all six bodies and all six weapons");
            PHProbeCheck([value[@"name"] containsString:@"14238 猫猫"] && [value[@"fits"] boolValue], @"portrait editor uses the correct encyclopedia identity and fits the viewport");
            PHProbeCheck([value[@"applyDisabled"] boolValue], @"formation apply remains disabled for an unmatched target");
            [self performSelector:@selector(verifyFormation) withObject:nil afterDelay:0.5];
        }];
    }];
}

- (void)verifyFormation {
    NSDictionary *team = PHProbeJSON(@"formation.json");
    PHProbeCheck([team[@"slots"] count] == 6 && [team[@"slots"][0][@"body"][@"id"] isEqual:@14238] &&
        [team[@"slots"][5][@"assist"][@"skill"] isEqual:@1], @"formation save reaches the native validator and persists all six drafts");
    [self.formationWeb takeSnapshotWithConfiguration:nil completionHandler:^(UIImage *snapshot, NSError *error) {
        PHProbeCheck(snapshot != nil && !error, @"formation portrait snapshot must render");
        NSString *prefix = PHProbeLegacy() ? @"legacy" : @"overlay";
        NSString *path = [NSHomeDirectory() stringByAppendingPathComponent:[NSString stringWithFormat:@"Documents/%@-formation-portrait.png", prefix]];
        [UIImagePNGRepresentation(snapshot) writeToFile:path atomically:YES];
        [self exerciseFailedFormationSave];
    }];
}

- (void)exerciseFailedFormationSave {
    NSFileManager *files = NSFileManager.defaultManager;
    NSString *folder = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/PADHelper"];
    NSString *path = [folder stringByAppendingPathComponent:@"config.json"];
    NSString *backup = [folder stringByAppendingPathComponent:@"config.probe-backup.json"];
    BOOL moved = [files moveItemAtPath:path toPath:backup error:NULL];
    BOOL blocked = moved && [files createDirectoryAtPath:path withIntermediateDirectories:NO attributes:nil error:NULL];
    PHProbeCheck(blocked, @"Save-failure fixture must block the canonical team/config commit file");
    if (!blocked) {
        if (moved) [files moveItemAtPath:backup toPath:path error:NULL];
        [self exerciseLibrary]; return;
    }
    NSString *script = @"document.getElementById('team-title').value='尚未落盤的隊伍';"
        "document.getElementById('close').click();const receipt=await PHFormationUI.whenSaved();"
        "return {ok:receipt.ok,save:PHFormationUI.getSaveState(),title:PHFormationUI.getTeam().title,"
        "label:document.getElementById('save-status').textContent};";
    [self.formationWeb callAsyncJavaScript:script arguments:@{} inFrame:nil inContentWorld:WKContentWorld.pageWorld completionHandler:^(id value, NSError *failure) {
        PHProbeCheck(!failure && [value[@"ok"] isEqual:@NO] && [value[@"save"][@"dirty"] boolValue] &&
            [value[@"title"] isEqual:@"尚未落盤的隊伍"] && [value[@"label"] containsString:@"未完成"],
            @"A real failed disk write must retain the draft and never acknowledge it as saved");
        PHProbeCheck(!self.window.isKeyWindow && self.formationWeb.window.isKeyWindow,
            @"Save-and-close must keep the editor open after a failed write");
        NSDictionary *status = PHProbeJSON(@"status.json");
        PHProbeCheck([status[@"formationDraft"][@"title"] isEqual:@"隊伍 A"] &&
            [status[@"battleState"][@"automaticTeamTitle"] isEqual:@"隊伍 A"],
            @"Failed saves must not publish a new automatic entry team");
        BOOL repaired = [files removeItemAtPath:path error:NULL] && [files moveItemAtPath:backup toPath:path error:NULL];
        PHProbeCheck(repaired, @"Save-failure fixture must restore the original library");
        NSString *retry = @"document.getElementById('team-title').value='隊伍 A';document.getElementById('save').click();"
            "return await PHFormationUI.whenSaved();";
        [self.formationWeb callAsyncJavaScript:retry arguments:@{} inFrame:nil inContentWorld:WKContentWorld.pageWorld completionHandler:^(id receipt, NSError *retryError) {
            PHProbeCheck(!retryError && [receipt[@"ok"] isEqual:@YES], @"Retry must acknowledge the successful canonical write");
            [self exerciseLibrary];
        }];
    }];
}

- (void)exerciseLibrary {
    NSString *script = @"const $=id=>document.getElementById(id),ui=PHFormationUI;"
        "const original=ui.getLibrary().selected;"
        "$('team-title').value='未儲存的副本';$('team-copy').click();await ui.whenSaved();const copy=ui.getLibrary().selected;"
        "const independent=copy!==original&&ui.getLibrary().teams[0].team.title==='隊伍 A';"
        "$('team-title').value='隊伍 B';$('skill').value='1';$('skill').dispatchEvent(new Event('change'));$('save').click();await ui.whenSaved();"
        "$('team-new').click();await ui.whenSaved();const empty=ui.getLibrary().teams.length===3&&ui.getTeam().slots.every(s=>!s.body.id);"
        "$('team-delete').click();const confirm=!$('delete-confirm').hidden;$('delete-cancel').click();"
        "const cancelled=ui.getLibrary().teams.length===3&&$('delete-confirm').hidden;"
        "$('team-delete').click();$('delete-yes').click();await ui.whenSaved();const deleted=ui.getLibrary().teams.length===2;"
        "$('team-choice').value=original;$('team-choice').dispatchEvent(new Event('change'));await ui.whenSaved();"
        "const originalSaved=ui.getTeam().title==='隊伍 A'&&ui.getTeam().slots[0].body.skill===0;"
        "$('team-choice').value=copy;$('team-choice').dispatchEvent(new Event('change'));await ui.whenSaved();"
        "const copySaved=ui.getTeam().title==='隊伍 B'&&ui.getTeam().slots[0].body.skill===1;"
        "return {independent,empty,confirm,cancelled,deleted,originalSaved,copySaved,error:$('error').textContent};";
    [self.formationWeb callAsyncJavaScript:script arguments:@{} inFrame:nil inContentWorld:WKContentWorld.pageWorld completionHandler:^(id value, NSError *error) {
        PHProbeCheck(!error && [value[@"error"] length] == 0, @"Library management completes through the native bridge");
        for (NSString *key in @[@"independent", @"empty", @"confirm", @"cancelled", @"deleted", @"originalSaved", @"copySaved"])
            PHProbeCheck([value[key] boolValue], [@"Formation library operation failed: " stringByAppendingString:key]);
        [self performSelector:@selector(verifyLibrary) withObject:nil afterDelay:0.5];
    }];
}

- (void)verifyLibrary {
    NSDictionary *library = PHProbeJSON(@"formations.json"), *team = PHProbeJSON(@"formation.json");
    PHProbeCheck([library[@"teams"] count] == 2 && [team[@"title"] isEqual:@"隊伍 B"] &&
        [team[@"slots"][0][@"body"][@"skill"] isEqual:@1] &&
        [library[@"teams"][0][@"team"][@"title"] isEqual:@"隊伍 A"], @"Native library saves retain both independent teams and the selected backup");
    NSString *script = @"(()=>{document.querySelector('#body-grid .card').click();window.PHProbeInitialScale=visualViewport.scale;document.getElementById('search').focus();"
        "return [...document.querySelectorAll('input,select')].every(e=>parseFloat(getComputedStyle(e).fontSize)>=16);})()";
    [self.formationWeb evaluateJavaScript:script completionHandler:^(id value, NSError *error) {
        PHProbeCheck(!error && [value boolValue], @"All editor input and select fonts prevent iOS focus zoom");
        [self performSelector:@selector(verifyFocusedEditor) withObject:nil afterDelay:0.8];
    }];
}

- (void)verifyFocusedEditor {
    // WKWebView's client rects already include its native keyboard pan and
    // correspond to the snapshot's visible origin. Adding offsetTop again
    // falsely rejects the visible header and accepts content below the keyboard.
    NSString *script = @"(()=>{const vv=visualViewport;const ids=['editor-done','kind-body','kind-assist','previous-slot','next-slot','search'];"
        "const controls=ids.map(id=>{const el=document.getElementById(id),r=el.getBoundingClientRect();"
        "const hit=document.elementFromPoint(r.left+r.width/2,r.top+r.height/2);return {id,top:r.top,bottom:r.bottom,hit:el.contains(hit)};});"
        "return {focused:document.activeElement.id==='search',scale:vv.scale,initial:PHProbeInitialScale,"
        "fits:document.documentElement.scrollWidth<=innerWidth,top:vv.offsetTop,height:vv.height,scroll:scrollY,controls,"
        "controlsVisible:controls.every(r=>r.top>=-1&&r.bottom<=vv.height+1&&r.hit)};})()";
    [self.formationWeb evaluateJavaScript:script completionHandler:^(id value, NSError *error) {
        PHProbeCheck(!error && [value[@"focused"] boolValue] && [value[@"fits"] boolValue] &&
            fabs([value[@"scale"] doubleValue] - [value[@"initial"] doubleValue]) < 0.01 &&
            fabs(self.formationWeb.scrollView.zoomScale - 1.0) < 0.01, @"Focused editor input keeps its viewport and native scroll zoom stable");
        PHProbeCheck([value[@"controlsVisible"] boolValue],
            [NSString stringWithFormat:@"Focused input must leave editor navigation and search visible: %@", value]);
        [self.formationWeb takeSnapshotWithConfiguration:nil completionHandler:^(UIImage *snapshot, NSError *failure) {
            PHProbeCheck(snapshot != nil && !failure, @"Focused editor snapshot must render");
            NSString *prefix = PHProbeLegacy() ? @"legacy" : @"overlay";
            [UIImagePNGRepresentation(snapshot) writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:
                [NSString stringWithFormat:@"Documents/%@-formation-focused.png", prefix]] atomically:YES];
            NSString *close = @"document.activeElement.blur();document.getElementById('editor-done').click();"
                "document.getElementById('close').click();return await PHFormationUI.whenSaved();";
            [self.formationWeb callAsyncJavaScript:close arguments:@{} inFrame:nil inContentWorld:WKContentWorld.pageWorld completionHandler:^(id result, NSError *closeError) {
                PHProbeCheck(!closeError && [result[@"ok"] isEqual:@YES], @"Closing the editor waits for its saved receipt");
                [self performSelector:@selector(finishFormationClose) withObject:nil afterDelay:0.3];
            }];
        }];
    }];
}

- (void)checkRestartedFormation {
    self.formationAttempts++;
    if (!self.formationWeb) { PHProbeCheck(NO, @"Restarted editor must open"); PHProbeReport(@"done"); return; }
    NSString *script = @"window.PHFormationUI&&PHFormationUI.initialized?PHFormationUI.getLibrary():null";
    [self.formationWeb evaluateJavaScript:script completionHandler:^(id value, NSError *error) {
        if (error || ![value isKindOfClass:NSDictionary.class]) {
            if (self.formationAttempts < 60) [self performSelector:@selector(checkRestartedFormation) withObject:nil afterDelay:0.5];
            else { PHProbeCheck(NO, @"Restarted editor bridge did not initialize"); PHProbeReport(@"done"); }
            return;
        }
        PHProbeCheck([value isEqual:PHProbeRestartLibrary], @"Process restart reloads every saved team and the selected draft through the bridge");
        [self.formationWeb takeSnapshotWithConfiguration:nil completionHandler:^(UIImage *snapshot, NSError *failure) {
            PHProbeCheck(snapshot != nil && !failure, @"Restarted library snapshot must render");
            NSString *prefix = PHProbeLegacy() ? @"legacy" : @"overlay";
            [UIImagePNGRepresentation(snapshot) writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:
                [NSString stringWithFormat:@"Documents/%@-formation-restart.png", prefix]] atomically:YES];
            NSString *close = @"document.getElementById('close').click();return await PHFormationUI.whenSaved();";
            [self.formationWeb callAsyncJavaScript:close arguments:@{} inFrame:nil inContentWorld:WKContentWorld.pageWorld completionHandler:^(id result, NSError *closeError) {
                PHProbeCheck(!closeError && [result[@"ok"] isEqual:@YES], @"Restarted editor waits for save confirmation");
                [self performSelector:@selector(finishRestartedFormationClose) withObject:nil afterDelay:0.3];
            }];
        }];
    }];
}

- (void)finishFormationClose {
    self.formationWeb = nil;
    PHProbeCheck(self.window.isKeyWindow, @"Closing formation restores the scene or legacy guest key window");
    [self finishPortrait];
}

- (void)finishRestartedFormationClose {
    self.formationWeb = nil;
    PHProbeCheck(self.window.isKeyWindow, @"Restarted editor restores guest focus when closed");
    PHProbeReport(@"done");
}

- (void)finishPortrait {
    if (PHProbeLegacy()) {
        UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
        UIButton *retry = (UIButton *)PHProbeFind(overlay, @"padcheaper.retry");
        [retry sendActionsForControlEvents:UIControlEventTouchUpInside];
        PHProbeCheck(self.window.isKeyWindow, @"Retry must leave the legacy guest window key");
        PHProbeCheck([PHProbeJSON(@"status.json")[@"activeMask"] isEqual:@0], @"Retry cannot bypass the target gate");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 4 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ PHProbeReport(@"done"); });
    } else [self performSelector:@selector(verifyLandscape) withObject:nil afterDelay:8];
}

- (void)verifyLandscape {
    self.landscapeAttempts++;
    UIWindowScene *scene = self.window.windowScene;
    NSMutableArray *windows = [NSMutableArray array];
    BOOL transitioning = NO;
    for (UIWindow *window in scene.windows) {
        UIViewController *root = window.rootViewController;
        BOOL transition = root.transitionCoordinator != nil || root.presentedViewController != nil;
        transitioning |= !window.hidden && transition;
        [windows addObject:@{@"class":NSStringFromClass(window.class), @"hidden":@(window.hidden),
            @"key":@(window.isKeyWindow), @"bounds":NSStringFromCGRect(window.bounds),
            @"controller":root ? NSStringFromClass(root.class) : @"nil",
            @"orientations":@(root.supportedInterfaceOrientations), @"transition":@(transition)}];
        if (!window.hidden) [root setNeedsUpdateOfSupportedInterfaceOrientations];
    }
    [PHProbeGeometry addObject:@{@"attempt":@(self.landscapeAttempts), @"appState":@(UIApplication.sharedApplication.applicationState),
        @"sceneState":@(scene.activationState), @"windows":windows}];
    if (UIApplication.sharedApplication.applicationState == UIApplicationStateActive &&
        scene.activationState == UISceneActivationStateForegroundActive && !transitioning) {
        [scene requestGeometryUpdateWithPreferences:
         [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeLeft]
                                        errorHandler:^(NSError *error) {
            self.landscapeError = error.localizedDescription;
            [PHProbeGeometry addObject:@{@"error":error.description}];
        }];
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
        UIView *panel = PHProbeFind(overlay, @"padcheaper.panel");
        [overlay layoutIfNeeded];
        if (CGRectGetWidth(overlay.bounds) <= CGRectGetHeight(overlay.bounds) && self.landscapeAttempts < 8) {
            // Geometry requests can be transiently rejected during scene/SDK
            // transitions. Retry finitely; keep the actual layout assertions.
            [self verifyLandscape];
            return;
        }
        PHProbeCheck(CGRectGetWidth(self.window.bounds) > CGRectGetHeight(self.window.bounds),
                     [@"Guest landscape geometry did not settle: " stringByAppendingString:self.landscapeError ?: @"no reported error"]);
        PHProbeCheck(CGRectGetWidth(overlay.bounds) > CGRectGetHeight(overlay.bounds), @"Overlay must rotate with the guest scene");
        PHProbeCheck(panel != nil && !panel.hidden && CGRectContainsRect(overlay.bounds, panel.frame), @"Landscape panel must remain on-screen");
        PHProbeCheck([overlay hitTest:CGPointMake(CGRectGetWidth(overlay.bounds) - 3, 3) withEvent:nil] == nil,
                     @"Touches outside the expanded panel must pass through");
        PHProbeRender(self.window, overlay, @"landscape");
        PHOverlaySnapshot originalSnapshot = [overlay.rootViewController valueForKey:@"snapshot"];
        PHOverlayAction originalAction = [overlay.rootViewController valueForKey:@"action"];
        NSNumber *originalExpanded = [overlay.rootViewController valueForKey:@"expanded"];
        NSNumber *originalCollapsed = [overlay.rootViewController valueForKey:@"shortcutsCollapsed"];
        NSMutableDictionary *floatingState = [originalSnapshot() mutableCopy];
        floatingState[@"orbState"] = @{@"phase":@"complete", @"message":@"轉珠完成", @"busy":@NO, @"canOnce":@YES, @"canStart":@YES};
        [overlay.rootViewController setValue:[^{ return floatingState; } copy] forKey:@"snapshot"];
        __block NSUInteger floatingActions = 0;
        [overlay.rootViewController setValue:[^(NSString *action, id value) { floatingActions++; } copy] forKey:@"action"];
        [overlay.rootViewController setValue:@NO forKey:@"expanded"];
        [overlay.rootViewController setValue:@NO forKey:@"shortcutsCollapsed"];
        [overlay.rootViewController updateWithSnapshot:floatingState];
        PHProbeFloatingLayout(overlay);
        PHProbeRender(self.window, overlay, @"orb-shortcuts-landscape");
        UIButton *once = (UIButton *)PHProbeFind(overlay, @"padcheaper.orbsOnceShortcut");
        CGPoint oldCenter = [once convertPoint:CGPointMake(CGRectGetMidX(once.bounds), CGRectGetMidY(once.bounds)) toView:overlay];
        [(UIButton *)PHProbeFind(overlay, @"padcheaper.collapseShortcuts") sendActionsForControlEvents:UIControlEventTouchUpInside];
        PHProbeCheck(floatingActions == 0 && once.hidden && [overlay hitTest:oldCenter withEvent:nil] == nil &&
            !PHProbeFind(overlay, @"padcheaper.bubble").hidden && self.window.isKeyWindow,
            @"Landscape collapse passes through unused space and retains guest focus");
        PHProbeRender(self.window, overlay, @"orb-folded-landscape");
        [overlay.rootViewController setValue:originalSnapshot forKey:@"snapshot"];
        [overlay.rootViewController setValue:originalAction forKey:@"action"];
        [overlay.rootViewController setValue:originalExpanded forKey:@"expanded"];
        [overlay.rootViewController setValue:originalCollapsed forKey:@"shortcutsCollapsed"];
        [overlay.rootViewController updateWithSnapshot:originalSnapshot()];
        [overlay.rootViewController.view setNeedsLayout]; [overlay.rootViewController.view layoutIfNeeded];
        PHProbeReport(@"landscape");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 6 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{ PHProbeReport(@"done"); });
    });
}
@end

@interface PHProbeAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation PHProbeAppDelegate
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session
                             options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *configuration = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
    configuration.delegateClass = PHProbeSceneDelegate.class;
    return configuration;
}
@end

@interface PHProbeLegacyAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@property(nonatomic, strong) PHProbeSceneDelegate *legacyDelegate;
@end
@implementation PHProbeLegacyAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.legacyDelegate = [PHProbeSceneDelegate new];
    [self.legacyDelegate startWithWindowScene:nil];
    self.window = self.legacyDelegate.window;
    return YES;
}
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        Class delegate = PHProbeLegacy() ? PHProbeLegacyAppDelegate.class : PHProbeAppDelegate.class;
        return UIApplicationMain(argc, argv, nil, NSStringFromClass(delegate));
    }
}
