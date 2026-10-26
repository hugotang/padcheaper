#import "PHOverlay.h"
#import <UIKit/UIKit.h>
#include <math.h>

@interface PHPassthroughWindow : UIWindow
@end

@implementation PHPassthroughWindow
- (BOOL)canBecomeKeyWindow { return NO; }
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit = [super hitTest:point withEvent:event];
    if (hit == self || hit == self.rootViewController.view) return nil;
    return hit;
}
@end

@interface PHOverlayController : UIViewController
@property(nonatomic, copy) PHOverlaySnapshot snapshot;
@property(nonatomic, copy) PHOverlayAction action;
@property(nonatomic, strong) NSDictionary *state;
@property(nonatomic, strong) UIButton *bubble;
@property(nonatomic, strong) UIView *panel;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UILabel *reasonLabel;
@property(nonatomic, strong) UILabel *endpointLabel;
@property(nonatomic, strong) UILabel *checksLabel;
@property(nonatomic, strong) UILabel *pathLabel;
@property(nonatomic, strong) UILabel *feedbackLabel;
@property(nonatomic, strong) UIButton *endpointButton;
@property(nonatomic, strong) NSMutableDictionary<NSString *, UISwitch *> *switches;
@property(nonatomic, strong) NSMutableDictionary<NSString *, UILabel *> *captions;
@property(nonatomic) CGPoint position;
@property(nonatomic) BOOL expanded;
- (void)updateWithSnapshot:(NSDictionary *)state;
@end

static UILabel *PHLabel(CGFloat size, UIFontWeight weight) {
    UILabel *label = [UILabel new];
    label.font = [UIFont systemFontOfSize:size weight:weight];
    label.textColor = UIColor.whiteColor;
    label.numberOfLines = 0;
    return label;
}

static CGFloat PHClamp(CGFloat value, CGFloat minimum, CGFloat maximum) {
    return MIN(MAX(value, minimum), MAX(minimum, maximum));
}

@implementation PHOverlayController

- (UIButton *)button:(NSString *)title action:(SEL)selector identifier:(NSString *)identifier {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    [button setTitleColor:[UIColor colorWithWhite:1 alpha:0.35] forState:UIControlStateDisabled];
    button.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    button.backgroundColor = [UIColor colorWithWhite:1 alpha:0.10];
    button.layer.cornerRadius = 10;
    button.accessibilityIdentifier = identifier;
    [button.heightAnchor constraintEqualToConstant:42].active = YES;
    [button addTarget:self action:selector forControlEvents:UIControlEventTouchUpInside];
    return button;
}

- (UIView *)switchRow:(NSString *)title key:(NSString *)key {
    UILabel *label = PHLabel(15, UIFontWeightMedium);
    label.text = title;
    UILabel *caption = PHLabel(11, UIFontWeightRegular);
    caption.textColor = [UIColor colorWithWhite:1 alpha:0.65];
    self.captions[key] = caption;
    UIStackView *text = [[UIStackView alloc] initWithArrangedSubviews:@[label, caption]];
    text.axis = UILayoutConstraintAxisVertical;
    text.spacing = 3;
    UISwitch *toggle = [UISwitch new];
    toggle.onTintColor = [UIColor colorWithRed:0.12 green:0.76 blue:0.59 alpha:1];
    toggle.accessibilityLabel = title;
    toggle.accessibilityIdentifier = [@"padcheaper." stringByAppendingString:key];
    [toggle addTarget:self action:@selector(toggleFeature:) forControlEvents:UIControlEventValueChanged];
    self.switches[key] = toggle;
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[text, toggle]];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.alignment = UIStackViewAlignmentCenter;
    row.spacing = 12;
    [row.heightAnchor constraintGreaterThanOrEqualToConstant:48].active = YES;
    return row;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.clearColor;
    self.view.accessibilityIdentifier = @"padcheaper.overlay";
    self.position = CGPointMake(1, 0.20);
    self.switches = [NSMutableDictionary dictionary];
    self.captions = [NSMutableDictionary dictionary];

    self.bubble = [UIButton buttonWithType:UIButtonTypeCustom];
    self.bubble.backgroundColor = [UIColor colorWithRed:0.08 green:0.11 blue:0.16 alpha:0.97];
    self.bubble.layer.cornerRadius = 28;
    self.bubble.layer.borderWidth = 2;
    self.bubble.layer.shadowColor = UIColor.blackColor.CGColor;
    self.bubble.layer.shadowOpacity = 0.35;
    self.bubble.layer.shadowRadius = 6;
    self.bubble.layer.shadowOffset = CGSizeMake(0, 2);
    self.bubble.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    self.bubble.titleLabel.numberOfLines = 2;
    self.bubble.titleLabel.textAlignment = NSTextAlignmentCenter;
    self.bubble.accessibilityIdentifier = @"padcheaper.bubble";
    [self.bubble addTarget:self action:@selector(openPanel) forControlEvents:UIControlEventTouchUpInside];
    UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(drag:)];
    [self.bubble addGestureRecognizer:pan];
    [self.view addSubview:self.bubble];

    self.panel = [UIView new];
    self.panel.hidden = YES;
    self.panel.accessibilityIdentifier = @"padcheaper.panel";
    self.panel.layer.cornerRadius = 20;
    self.panel.layer.shadowColor = UIColor.blackColor.CGColor;
    self.panel.layer.shadowOpacity = 0.40;
    self.panel.layer.shadowRadius = 18;
    self.panel.layer.shadowOffset = CGSizeMake(0, 6);
    [self.view addSubview:self.panel];
    UIVisualEffectView *material = [[UIVisualEffectView alloc] initWithEffect:
                                   [UIBlurEffect effectWithStyle:UIBlurEffectStyleSystemMaterialDark]];
    material.frame = self.panel.bounds;
    material.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    material.layer.cornerRadius = 20;
    material.clipsToBounds = YES;
    [self.panel addSubview:material];
    UIView *content = material.contentView;
    content.backgroundColor = [UIColor colorWithRed:0.05 green:0.08 blue:0.12 alpha:0.55];

    UILabel *title = PHLabel(20, UIFontWeightBold);
    title.text = @"PADCheaper";
    UILabel *version = PHLabel(11, UIFontWeightRegular);
    version.text = [@"LiveContainer · " stringByAppendingString:PH_RUNTIME_VERSION];
    version.textColor = [UIColor colorWithWhite:1 alpha:0.60];
    UIStackView *heading = [[UIStackView alloc] initWithArrangedSubviews:@[title, version]];
    heading.axis = UILayoutConstraintAxisVertical;
    heading.spacing = 3;
    UIButton *close = [UIButton buttonWithType:UIButtonTypeSystem];
    [close setImage:[UIImage systemImageNamed:@"xmark"] forState:UIControlStateNormal];
    close.tintColor = UIColor.whiteColor;
    close.accessibilityLabel = @"收起 PADCheaper";
    close.accessibilityIdentifier = @"padcheaper.close";
    [close.widthAnchor constraintEqualToConstant:36].active = YES;
    [close.heightAnchor constraintEqualToConstant:36].active = YES;
    [close addTarget:self action:@selector(closePanel) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[heading, close]];
    header.axis = UILayoutConstraintAxisHorizontal;
    header.alignment = UIStackViewAlignmentCenter;
    header.spacing = 12;
    header.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:header];

    UIScrollView *scroll = [UIScrollView new];
    scroll.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
    scroll.alwaysBounceVertical = YES;
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [content addSubview:scroll];
    self.statusLabel = PHLabel(15, UIFontWeightSemibold);
    self.statusLabel.accessibilityIdentifier = @"padcheaper.status";
    self.reasonLabel = PHLabel(12, UIFontWeightRegular);
    self.reasonLabel.textColor = [UIColor colorWithWhite:1 alpha:0.80];
    self.endpointLabel = PHLabel(12, UIFontWeightRegular);
    self.endpointLabel.lineBreakMode = NSLineBreakByCharWrapping;
    self.endpointButton = [self button:@"套用偵測到的 API" action:@selector(adoptEndpoint)
                            identifier:@"padcheaper.adoptEndpoint"];
    self.checksLabel = PHLabel(11, UIFontWeightRegular);
    self.checksLabel.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
    self.checksLabel.textColor = [UIColor colorWithWhite:1 alpha:0.72];
    self.pathLabel = PHLabel(10, UIFontWeightRegular);
    self.pathLabel.lineBreakMode = NSLineBreakByCharWrapping;
    self.pathLabel.textColor = [UIColor colorWithWhite:1 alpha:0.50];
    UIButton *retry = [self button:@"重新檢查" action:@selector(retry) identifier:@"padcheaper.retry"];
    UIButton *copy = [self button:@"複製診斷" action:@selector(copyDiagnostics) identifier:@"padcheaper.copy"];
    UIStackView *actions = [[UIStackView alloc] initWithArrangedSubviews:@[retry, copy]];
    actions.axis = UILayoutConstraintAxisHorizontal;
    actions.distribution = UIStackViewDistributionFillEqually;
    actions.spacing = 8;
    self.feedbackLabel = PHLabel(12, UIFontWeightMedium);
    self.feedbackLabel.textColor = [UIColor colorWithRed:0.55 green:0.90 blue:0.78 alpha:1];
    UILabel *note = PHLabel(11, UIFontWeightRegular);
    note.text = @"PC 按鈕代表 dylib 已載入；綠色代表功能已通過啟用條件。Skin／BGM 仍需遊戲已有資源。";
    note.textColor = [UIColor colorWithWhite:1 alpha:0.55];
    UIStackView *body = [[UIStackView alloc] initWithArrangedSubviews:@[
        self.statusLabel, self.reasonLabel,
        [self switchRow:@"功能總開關" key:@"experimentalWrites"],
        [self switchRow:@"Skin" key:@"skin"], [self switchRow:@"BGM" key:@"bgm"],
        [self switchRow:@"背景計時修正" key:@"timerFix"],
        self.endpointLabel, self.endpointButton, self.checksLabel, actions,
        self.feedbackLabel, self.pathLabel, note
    ]];
    body.axis = UILayoutConstraintAxisVertical;
    body.spacing = 12;
    body.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:body];
    [NSLayoutConstraint activateConstraints:@[
        [header.topAnchor constraintEqualToAnchor:content.topAnchor constant:16],
        [header.leadingAnchor constraintEqualToAnchor:content.leadingAnchor constant:16],
        [header.trailingAnchor constraintEqualToAnchor:content.trailingAnchor constant:-12],
        [scroll.topAnchor constraintEqualToAnchor:header.bottomAnchor constant:14],
        [scroll.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:content.bottomAnchor],
        [body.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [body.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:16],
        [body.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-16],
        [body.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-18],
        [body.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-32]
    ]];
    if (self.state) [self updateWithSnapshot:self.state];
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
- (BOOL)shouldAutorotate { return YES; }

- (CGRect)usableBounds {
    CGRect rect = UIEdgeInsetsInsetRect(self.view.bounds, self.view.safeAreaInsets);
    if (CGRectGetWidth(rect) < 72 || CGRectGetHeight(rect) < 72) rect = self.view.bounds;
    return CGRectInset(rect, 8, 8);
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGRect safe = [self usableBounds];
    CGFloat width = CGRectGetWidth(safe), height = CGRectGetHeight(safe);
    CGFloat x = CGRectGetMinX(safe) + 28 + MAX(0, width - 56) * self.position.x;
    CGFloat y = CGRectGetMinY(safe) + 28 + MAX(0, height - 56) * self.position.y;
    self.bubble.frame = CGRectMake(x - 28, y - 28, 56, 56);
    CGFloat panelWidth = MIN(352, width), panelHeight = MIN(600, height);
    CGFloat panelX = PHClamp(x - panelWidth / 2, CGRectGetMinX(safe), CGRectGetMaxX(safe) - panelWidth);
    CGFloat panelY = PHClamp(y - 90, CGRectGetMinY(safe), CGRectGetMaxY(safe) - panelHeight);
    self.panel.frame = CGRectMake(panelX, panelY, panelWidth, panelHeight);
    self.panel.layer.shadowPath = [UIBezierPath bezierPathWithRoundedRect:self.panel.bounds cornerRadius:20].CGPath;
}

- (void)drag:(UIPanGestureRecognizer *)pan {
    CGPoint translation = [pan translationInView:self.view];
    CGRect safe = [self usableBounds];
    CGPoint center = self.bubble.center;
    center.x = PHClamp(center.x + translation.x, CGRectGetMinX(safe) + 28, CGRectGetMaxX(safe) - 28);
    center.y = PHClamp(center.y + translation.y, CGRectGetMinY(safe) + 28, CGRectGetMaxY(safe) - 28);
    self.position = CGPointMake((center.x - CGRectGetMinX(safe) - 28) / MAX(1, CGRectGetWidth(safe) - 56),
                               (center.y - CGRectGetMinY(safe) - 28) / MAX(1, CGRectGetHeight(safe) - 56));
    [pan setTranslation:CGPointZero inView:self.view];
    if (pan.state == UIGestureRecognizerStateEnded || pan.state == UIGestureRecognizerStateCancelled) {
        self.position = CGPointMake(center.x < CGRectGetMidX(safe) ? 0 : 1, self.position.y);
        [UIView animateWithDuration:0.18 animations:^{
            [self.view setNeedsLayout];
            [self.view layoutIfNeeded];
        }];
    } else {
        [self.view setNeedsLayout];
        [self.view layoutIfNeeded];
    }
}

- (void)openPanel {
    self.expanded = YES;
    self.bubble.hidden = YES;
    self.panel.hidden = NO;
    [self updateWithSnapshot:self.snapshot()];
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
}

- (void)closePanel {
    self.expanded = NO;
    self.panel.hidden = YES;
    self.bubble.hidden = NO;
}

- (void)toggleFeature:(UISwitch *)toggle {
    for (NSString *key in self.switches) {
        if (self.switches[key] == toggle) {
            self.action(key, @(toggle.on));
            break;
        }
    }
    [self updateWithSnapshot:self.snapshot()];
}

- (void)adoptEndpoint {
    self.action(@"adoptEndpoint", nil);
    [self updateWithSnapshot:self.snapshot()];
}

- (void)retry {
    self.action(@"retry", nil);
    [self updateWithSnapshot:self.snapshot()];
}

- (void)copyDiagnostics {
    NSDictionary *state = self.snapshot();
    UIPasteboard.generalPasteboard.string = state[@"diagnostics"] ?: @"PADCheaper 已載入，尚無診斷資料。";
    self.action(@"copied", nil);
    [self updateWithSnapshot:self.snapshot()];
}

- (void)updateWithSnapshot:(NSDictionary *)state {
    self.state = state;
    if (!self.isViewLoaded) return;
    NSString *tone = state[@"tone"];
    BOOL active = [tone isEqualToString:@"active"], error = [tone isEqualToString:@"error"];
    BOOL ready = [tone isEqualToString:@"ready"];
    UIColor *color = active ? [UIColor colorWithRed:0.22 green:0.89 blue:0.65 alpha:1] :
                     error ? [UIColor colorWithRed:1 green:0.40 blue:0.42 alpha:1] :
                     ready ? [UIColor colorWithRed:0.42 green:0.73 blue:1 alpha:1] :
                             [UIColor colorWithRed:1 green:0.75 blue:0.32 alpha:1];
    NSString *shortStatus = active ? @"啟用" : error ? @"錯誤" : ready ? @"就緒" : @"載入";
    [self.bubble setTitle:[@"PC\n" stringByAppendingString:shortStatus] forState:UIControlStateNormal];
    [self.bubble setTitleColor:color forState:UIControlStateNormal];
    self.bubble.layer.borderColor = color.CGColor;
    self.bubble.accessibilityLabel = [NSString stringWithFormat:@"PADCheaper 已載入，%@，點擊開啟面板", shortStatus];
    self.statusLabel.text = state[@"title"];
    self.statusLabel.textColor = color;
    self.reasonLabel.text = state[@"reason"];
    NSDictionary *config = state[@"config"];
    BOOL canSave = [state[@"canSaveConfig"] boolValue];
    for (NSString *key in self.switches) {
        UISwitch *toggle = self.switches[key];
        toggle.on = [key isEqualToString:@"experimentalWrites"] ? [config[key] boolValue] :
                                                                 [config[@"features"][key] boolValue];
        toggle.enabled = canSave;
        self.captions[key].text = [key isEqualToString:@"experimentalWrites"] ?
                                 (canSave ? @"切換後自動儲存" : @"設定無法寫入，請重新檢查") : state[@"featureStates"][key];
    }
    NSDictionary *endpoint = state[@"endpoint"];
    NSString *host = endpoint[@"host"], *path = endpoint[@"path"];
    BOOL observed = host.length > 0 && [path hasPrefix:@"/"];
    self.endpointLabel.text = observed ? [NSString stringWithFormat:@"偵測到 API：%@%@\n目前設定：%@", host, path,
                                          [state[@"endpointConfigured"] boolValue] ? @"已套用" : @"未套用"] :
                                         @"API 尚未偵測。請在遊戲內儲存一次隊伍，再按下方按鈕。";
    self.endpointButton.enabled = observed && canSave;
    self.checksLabel.text = state[@"checks"];
    self.pathLabel.text = [@"設定檔：\n" stringByAppendingString:(state[@"configPath"] ?: @"初始化中")];
    self.feedbackLabel.text = state[@"feedback"];
    self.feedbackLabel.hidden = self.feedbackLabel.text.length == 0;
}
@end

@interface PHOverlayHost : NSObject
@property(nonatomic, copy) PHOverlaySnapshot snapshot;
@property(nonatomic, copy) PHOverlayAction action;
@property(nonatomic, strong) PHPassthroughWindow *window;
@property(nonatomic, strong) PHOverlayController *controller;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic) BOOL visible;
- (void)start;
- (void)refresh;
@end

@implementation PHOverlayHost
- (void)start {
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    for (NSString *name in @[UIApplicationDidBecomeActiveNotification, UIApplicationDidEnterBackgroundNotification,
                            UIWindowDidBecomeVisibleNotification, UISceneDidActivateNotification,
                            UISceneDidDisconnectNotification]) {
        [center addObserver:self selector:@selector(scheduleRefresh:) name:name object:nil];
    }
    self.timer = [NSTimer timerWithTimeInterval:1 target:self selector:@selector(refresh) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSRunLoopCommonModes];
    [self refresh];
}

- (void)scheduleRefresh:(NSNotification *)notification {
    // UIKit may notify synchronously while a window is being attached.
    dispatch_async(dispatch_get_main_queue(), ^{ [self refresh]; });
}

- (void)refresh {
    UIApplication *app = UIApplication.sharedApplication;
    UIWindow *host = nil;
    UIWindowScene *scene = nil;
    if (app.applicationState == UIApplicationStateActive) {
        for (UIScene *candidate in app.connectedScenes) {
            if (![candidate isKindOfClass:UIWindowScene.class] ||
                candidate.activationState != UISceneActivationStateForegroundActive) continue;
            UIWindowScene *windowScene = (UIWindowScene *)candidate;
            for (UIWindow *window in windowScene.windows) {
                if ([window isKindOfClass:PHPassthroughWindow.class] || window.hidden ||
                    !window.rootViewController || window.windowLevel != UIWindowLevelNormal) continue;
                host = window;
                scene = windowScene;
                if (window.isKeyWindow) break;
            }
            if (host) break;
        }
        if (!host && [app.delegate respondsToSelector:@selector(window)]) {
            host = app.delegate.window;
            if (host.hidden || !host.rootViewController || [host isKindOfClass:PHPassthroughWindow.class]) host = nil;
            scene = host.windowScene;
        }
    }
    if (host) {
        if (!self.window || self.window.windowScene != scene) {
            self.window.hidden = YES;
            self.window = scene ? [[PHPassthroughWindow alloc] initWithWindowScene:scene] :
                                  [[PHPassthroughWindow alloc] initWithFrame:host.bounds];
            self.controller = [PHOverlayController new];
            self.controller.snapshot = self.snapshot;
            self.controller.action = self.action;
            self.window.rootViewController = self.controller;
            self.window.windowLevel = UIWindowLevelAlert + 1;
            self.window.backgroundColor = UIColor.clearColor;
        }
        self.window.frame = scene ? scene.coordinateSpace.bounds : host.bounds;
        // Deliberately show without makeKeyAndVisible; the game stays key.
        self.window.hidden = NO;
        [self.controller loadViewIfNeeded];
    } else {
        self.window.hidden = YES;
    }
    BOOL visible = host != nil;
    if (visible != self.visible) {
        self.visible = visible;
        self.action(@"overlayVisible", @(visible));
    }
    [self.controller updateWithSnapshot:self.snapshot()];
}
@end

void PHShowOverlay(PHOverlaySnapshot snapshot, PHOverlayAction action) {
    NSCAssert(NSThread.isMainThread, @"PADCheaper overlay must initialize on main");
    static PHOverlayHost *host;
    if (host) return;
    host = [PHOverlayHost new];
    host.snapshot = snapshot;
    host.action = action;
    [host start];
}
