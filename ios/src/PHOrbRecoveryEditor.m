#import "PHOrbRecoveryEditor.h"
#import "PHOrbPlanning.h"

@interface PHOrbRecoveryEditor ()
@property(nonatomic, strong) NSMutableDictionary *draft;
@property(nonatomic, copy) NSString *(^saveSettings)(NSDictionary *);
@property(nonatomic, strong) UITextField *hp;
@property(nonatomic, strong) UISegmentedControl *groups;
@property(nonatomic, strong) UISegmentedControl *shape;
@property(nonatomic, strong) UILabel *notice;
@property(nonatomic, weak) UIWindow *previousWindow;
@property(nonatomic, weak) UIWindow *editorWindow;
@end

@implementation PHOrbRecoveryEditor
- (instancetype)initWithSettings:(NSDictionary *)settings save:(NSString *(^)(NSDictionary *))save {
    if ((self = [super init])) {
        _draft = [(PHOrbRecoveryValid(settings) ? settings : PHOrbDefaultRecovery()) mutableCopy];
        _saveSettings = [save copy];
    }
    return self;
}
- (UILabel *)label:(NSString *)text {
    UILabel *label = [UILabel new]; label.text = text; label.numberOfLines = 0;
    label.font = [UIFont systemFontOfSize:15]; label.textColor = UIColor.labelColor; return label;
}
- (UIButton *)button:(NSString *)title selector:(SEL)selector identifier:(NSString *)identifier {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *config = [UIButtonConfiguration tintedButtonConfiguration]; config.title = title;
    button.configuration = config; button.accessibilityIdentifier = identifier;
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    [button addTarget:self action:selector forControlEvents:UIControlEventTouchUpInside]; return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = UIColor.systemBackgroundColor; self.view.accessibilityIdentifier = @"padcheaper.orbsRecoveryEditor";
    UILabel *title = [self label:@"低血時的心珠目標"]; title.font = [UIFont boldSystemFontOfSize:22];
    UIButton *cancel = [self button:@"取消" selector:@selector(cancel) identifier:@"padcheaper.orbsRecoveryCancel"];
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, cancel]]; header.spacing = 12;
    header.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:header];
    UIScrollView *scroll = [UIScrollView new]; scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive; [self.view addSubview:scroll];
    self.hp = [UITextField new]; self.hp.borderStyle = UITextBorderStyleRoundedRect;
    self.hp.font = [UIFont systemFontOfSize:16]; self.hp.keyboardType = UIKeyboardTypeNumberPad;
    self.hp.text = [self.draft[@"hpAtMost"] description];
    self.hp.accessibilityIdentifier = @"padcheaper.orbsRecoveryHP"; self.hp.accessibilityLabel = @"HP 百分比門檻；0 關閉，1 至 100 啟用";
    [self.hp addTarget:self action:@selector(changeHP) forControlEvents:UIControlEventEditingChanged];
    [self.hp.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    UIToolbar *toolbar = [UIToolbar new]; [toolbar sizeToFit];
    toolbar.items = @[[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil],
        [[UIBarButtonItem alloc] initWithTitle:@"完成" style:UIBarButtonItemStyleDone target:self action:@selector(endEditing)]];
    self.hp.inputAccessoryView = toolbar;
    self.groups = [[UISegmentedControl alloc] initWithItems:@[@"1 組", @"2 組", @"3 組"]];
    self.groups.selectedSegmentIndex = [self.draft[@"groups"] integerValue] - 1;
    self.groups.accessibilityIdentifier = @"padcheaper.orbsRecoveryGroups"; self.groups.accessibilityLabel = @"心珠獨立消除組數";
    [self.groups addTarget:self action:@selector(changeGroups) forControlEvents:UIControlEventValueChanged];
    [self.groups.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    self.shape = [[UISegmentedControl alloc] initWithItems:@[@"至少三連", @"四顆直線"]];
    self.shape.selectedSegmentIndex = [self.draft[@"shape"] isEqual:@"four"] ? 1 : 0;
    self.shape.accessibilityIdentifier = @"padcheaper.orbsRecoveryShape"; self.shape.accessibilityLabel = @"心珠消除形狀";
    [self.shape addTarget:self action:@selector(changeShape) forControlEvents:UIControlEventValueChanged];
    [self.shape.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    UIStackView *rows = [[UIStackView alloc] initWithArrangedSubviews:@[
        [self label:@"HP 百分比 ≤（1–100；0 關閉）"], self.hp,
        [self label:@"每回合要求的心珠組數"], self.groups, [self label:@"每組形狀"], self.shape,
        [self label:@"只在連續轉珠啟用。每回合先處理技能，再用目前 HP 與有效上限判斷；高於門檻就沿用原有目標。"],
        [self label:@"心珠要求會與形狀或隊長目標一起求解；相同條件取較高組數。心珠不足、合共超過 6 項或路徑未達標都會停止。必要技能仍須先確認完成。"],
        [self label:@"至少三連可計入現有珠子的盤內連鎖；四顆直線須在首批實際消除，未消同色鄰珠可保留，合併成五顆以上不符合。不計隨機天降。"],
        [self label:@"這是心珠消除要求，不估算回復量或有效覺醒，也不保證能承受下一次攻擊。手動讀盤與執行一次維持原有目標。"]]];
    rows.axis = UILayoutConstraintAxisVertical; rows.spacing = 12; rows.translatesAutoresizingMaskIntoConstraints = NO; [scroll addSubview:rows];
    self.notice = [self label:@""]; self.notice.textColor = UIColor.systemRedColor;
    self.notice.accessibilityIdentifier = @"padcheaper.orbsRecoveryNotice";
    UIButton *save = [self button:@"儲存心珠設定" selector:@selector(save) identifier:@"padcheaper.orbsRecoverySave"];
    UIStackView *footer = [[UIStackView alloc] initWithArrangedSubviews:@[self.notice, save]];
    footer.axis = UILayoutConstraintAxisVertical; footer.spacing = 8; footer.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:footer];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [header.topAnchor constraintEqualToAnchor:safe.topAnchor constant:16],
        [header.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [header.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [footer.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [footer.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [footer.bottomAnchor constraintEqualToAnchor:self.view.keyboardLayoutGuide.topAnchor constant:-12],
        [scroll.topAnchor constraintEqualToAnchor:header.bottomAnchor constant:12],
        [scroll.bottomAnchor constraintEqualToAnchor:footer.topAnchor constant:-12],
        [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor], [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [rows.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [rows.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],
        [rows.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],
        [rows.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],
        [rows.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40]
    ]];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated]; self.editorWindow = self.view.window;
    for (UIWindow *window in self.editorWindow.windowScene.windows)
        if (window.isKeyWindow && window != self.editorWindow) { self.previousWindow = window; break; }
    if (!self.previousWindow && [UIApplication.sharedApplication.delegate respondsToSelector:@selector(window)])
        self.previousWindow = UIApplication.sharedApplication.delegate.window;
    [self.editorWindow makeKeyWindow];
}
- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated]; [self.view endEditing:YES];
    if (self.editorWindow.isKeyWindow && self.previousWindow && !self.previousWindow.hidden) [self.previousWindow makeKeyWindow];
    self.previousWindow = nil; self.editorWindow = nil;
}
- (void)changeHP {
    NSString *text = self.hp.text ?: @"";
    BOOL digits = text.length && text.length <= 3 &&
        [text rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789"].invertedSet].location == NSNotFound;
    self.draft[@"hpAtMost"] = digits ? (id)@(text.integerValue) : (id)text;
}
- (void)changeGroups {
    NSInteger index = self.groups.selectedSegmentIndex;
    if (index >= 0 && index < 3) self.draft[@"groups"] = @(index + 1);
}
- (void)changeShape {
    NSInteger index = self.shape.selectedSegmentIndex;
    if (index >= 0 && index < 2) self.draft[@"shape"] = @[@"match", @"four"][(NSUInteger)index];
}
- (void)endEditing { [self.view endEditing:YES]; }
- (void)save {
    [self.view endEditing:YES];
    if (!PHOrbRecoveryValid(self.draft)) { self.notice.text = @"HP 百分比需為 0–100 的整數；心珠需選 1–3 組。"; return; }
    NSString *error = self.saveSettings([self.draft copy]);
    if (error.length) { self.notice.text = error; return; }
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)cancel { [self dismissViewControllerAnimated:YES completion:nil]; }
@end
