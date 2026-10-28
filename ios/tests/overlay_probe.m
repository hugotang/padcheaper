#import <UIKit/UIKit.h>
#import <WebKit/WebKit.h>
#import "PHOverlay.h"
#include <dlfcn.h>
#include <math.h>

static NSString *PHProbeRoot;
static NSMutableArray<NSString *> *PHProbeFailures;
static UIWindow *PHProbeLegacyOverlay;
static NSDictionary *PHProbeRestartLibrary;

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

static void PHProbeRender(UIWindow *guest, UIWindow *overlay, NSString *name) {
    UIView *gameView = guest.rootViewController.view;
    UIView *overlayView = overlay.rootViewController.view;
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
        [NSNotificationCenter.defaultCenter addObserverForName:UIWindowDidBecomeVisibleNotification object:nil
            queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {
                UIWindow *window = notification.object;
                if (window != self.window && window.windowLevel > UIWindowLevelNormal) PHProbeLegacyOverlay = window;
            }];
    }
    PHProbeRoot = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/PADHelper"];
    PHProbeFailures = [NSMutableArray array];
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
    const char *error = handle ? NULL : dlerror();
    PHProbeCheck(handle != NULL, error ? @(error) : @"Simulator dylib could not be loaded");
    self.targetDeadline = NSProcessInfo.processInfo.systemUptime + 90;
    [self performSelector:@selector(exerciseOverlay) withObject:nil afterDelay:3];
}

- (void)exerciseOverlay {
    UIWindow *overlay = PHProbeOverlay(self.window.windowScene);
    if (PHProbeRestartLibrary) {
        PHProbeCheck([PHProbeJSON(@"formations.json") isEqual:PHProbeRestartLibrary], @"Restart keeps the saved library file intact");
        PHProbeCheck([PHProbeJSON(@"config.json")[@"battle"][@"autoApplyTeam"] isEqual:@YES], @"Automatic entry option survives a process restart");
        [(UIButton *)PHProbeFind(overlay, @"padcheaper.bubble") sendActionsForControlEvents:UIControlEventTouchUpInside];
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
    NSDictionary *status = PHProbeJSON(@"status.json");
    PHProbeCheck([status[@"loaded"] isEqual:@YES] && [status[@"targetMatched"] isEqual:@NO], @"Loaded status must be independent of feature activation");
    PHProbeCheck([status[@"saveState"][@"localSaveCount"] isEqual:@0] &&
                 [status[@"saveState"][@"acceptedCount"] isEqual:@0], @"GUI must not invent successful saves before a real request");
    UIButton *bubble = (UIButton *)PHProbeFind(overlay, @"padcheaper.bubble");
    CGPoint point = [bubble convertPoint:CGPointMake(28, 28) toView:overlay];
    PHProbeCheck([overlay hitTest:point withEvent:nil] == bubble, @"Floating button must receive touches");
    PHProbeCheck([overlay hitTest:CGPointMake(20, CGRectGetHeight(overlay.bounds) - 50) withEvent:nil] == nil,
                 @"Touches outside the button must pass through");
    PHProbeRender(self.window, overlay, @"bubble");
    [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIView *panel = PHProbeFind(overlay, @"padcheaper.panel");
    PHProbeCheck(panel != nil && !panel.hidden, @"Floating button must open the panel");
    UILabel *persistence = (UILabel *)PHProbeFind(overlay, @"padcheaper.persistence");
    PHProbeCheck([persistence.text containsString:@"尚未儲存隊伍"] &&
                 [persistence.text containsString:@"伺服器接受：0"], @"Panel must separate load/feature status from actual save and response status");
    for (NSString *key in @[@"experimentalWrites", @"skin", @"bgm", @"badge", @"timerFix", @"battleSwap"]) {
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
    PHProbeCheck(!PHProbeFind(overlay, @"padcheaper.filters").hidden, @"mode/source/multi settings must open from the GUI");
    for (NSString *key in @[@"allowAllModes", @"allowAllSources", @"allowAllMulti", @"autoApplyTeam"]) {
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
    [bubble sendActionsForControlEvents:UIControlEventTouchUpInside];
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
    UIButton *formation = (UIButton *)PHProbeFind(overlay, @"padcheaper.formationOpen");
    [formation sendActionsForControlEvents:UIControlEventTouchUpInside];
    UIWindow *formationWindow = [overlay.rootViewController valueForKey:@"formationWindow"];
    self.formationWeb = (WKWebView *)PHProbeFind(formationWindow, @"padcheaper.formationWeb");
    PHProbeCheck(formationWindow.isKeyWindow && self.formationWeb != nil, @"formation editor has an editable web view and keyboard-capable window");
    [self checkFormation];
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
        [self exerciseLibrary];
    }];
}

- (void)exerciseLibrary {
    NSString *script = @"(()=>{const $=id=>document.getElementById(id),ui=PHFormationUI;"
        "const original=ui.getLibrary().selected;"
        "$('team-title').value='未儲存的副本';$('team-copy').click();const copy=ui.getLibrary().selected;"
        "const independent=copy!==original&&ui.getLibrary().teams[0].team.title==='隊伍 A';"
        "$('team-title').value='隊伍 B';$('skill').value='1';$('skill').dispatchEvent(new Event('change'));$('save').click();"
        "$('team-new').click();const empty=ui.getLibrary().teams.length===3&&ui.getTeam().slots.every(s=>!s.body.id);"
        "$('team-delete').click();const confirm=!$('delete-confirm').hidden;$('delete-cancel').click();"
        "const cancelled=ui.getLibrary().teams.length===3&&$('delete-confirm').hidden;"
        "$('team-delete').click();$('delete-yes').click();const deleted=ui.getLibrary().teams.length===2;"
        "$('team-choice').value=original;$('team-choice').dispatchEvent(new Event('change'));"
        "const originalSaved=ui.getTeam().title==='隊伍 A'&&ui.getTeam().slots[0].body.skill===0;"
        "$('team-choice').value=copy;$('team-choice').dispatchEvent(new Event('change'));"
        "const copySaved=ui.getTeam().title==='隊伍 B'&&ui.getTeam().slots[0].body.skill===1;"
        "return {independent,empty,confirm,cancelled,deleted,originalSaved,copySaved,error:$('error').textContent};})()";
    [self.formationWeb evaluateJavaScript:script completionHandler:^(id value, NSError *error) {
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
    NSString *script = @"(()=>{window.PHProbeInitialScale=visualViewport.scale;document.getElementById('search').focus();"
        "return [...document.querySelectorAll('input,select')].every(e=>parseFloat(getComputedStyle(e).fontSize)>=16);})()";
    [self.formationWeb evaluateJavaScript:script completionHandler:^(id value, NSError *error) {
        PHProbeCheck(!error && [value boolValue], @"All editor input and select fonts prevent iOS focus zoom");
        [self performSelector:@selector(verifyFocusedEditor) withObject:nil afterDelay:0.8];
    }];
}

- (void)verifyFocusedEditor {
    NSString *script = @"({focused:document.activeElement.id==='search',scale:visualViewport.scale,"
        "initial:PHProbeInitialScale,fits:document.documentElement.scrollWidth<=innerWidth})";
    [self.formationWeb evaluateJavaScript:script completionHandler:^(id value, NSError *error) {
        PHProbeCheck(!error && [value[@"focused"] boolValue] && [value[@"fits"] boolValue] &&
            fabs([value[@"scale"] doubleValue] - [value[@"initial"] doubleValue]) < 0.01 &&
            fabs(self.formationWeb.scrollView.zoomScale - 1.0) < 0.01, @"Focused editor input keeps its viewport and native scroll zoom stable");
        [self.formationWeb takeSnapshotWithConfiguration:nil completionHandler:^(UIImage *snapshot, NSError *failure) {
            PHProbeCheck(snapshot != nil && !failure, @"Focused editor snapshot must render");
            NSString *prefix = PHProbeLegacy() ? @"legacy" : @"overlay";
            [UIImagePNGRepresentation(snapshot) writeToFile:[NSHomeDirectory() stringByAppendingPathComponent:
                [NSString stringWithFormat:@"Documents/%@-formation-focused.png", prefix]] atomically:YES];
            [self.formationWeb evaluateJavaScript:@"document.activeElement.blur();document.getElementById('close').click()" completionHandler:^(id result, NSError *closeError) {
                self.formationWeb = nil;
                PHProbeCheck(!closeError && self.window.isKeyWindow, @"Closing formation restores the scene or legacy guest key window");
                [self finishPortrait];
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
            [self.formationWeb evaluateJavaScript:@"document.getElementById('close').click()" completionHandler:^(id result, NSError *closeError) {
                PHProbeCheck(!closeError && self.window.isKeyWindow, @"Restarted editor restores guest focus when closed");
                PHProbeReport(@"done");
            }];
        }];
    }];
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
    [self.window.rootViewController setNeedsUpdateOfSupportedInterfaceOrientations];
    UIWindow *overlayWindow = PHProbeOverlay(self.window.windowScene);
    [overlayWindow.rootViewController setNeedsUpdateOfSupportedInterfaceOrientations];
    [self.window.windowScene requestGeometryUpdateWithPreferences:
     [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeLeft]
                                                    errorHandler:^(NSError *error) {
        self.landscapeError = error.localizedDescription;
    }];
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
