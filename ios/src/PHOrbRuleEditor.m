#import "PHOrbRuleEditor.h"
#import "PHOrbRules.h"
#import "PHOrbEditorUI.h"

@interface PHOrbRuleEditor ()
@property(nonatomic, strong) NSMutableArray<NSMutableDictionary *> *draft;
@property(nonatomic, copy) NSString *(^saveRules)(NSArray *);
@property(nonatomic, strong) UIStackView *rows;
@property(nonatomic, strong) UILabel *notice;
@property(nonatomic, strong) NSMutableDictionary *expandedRule;
@property(nonatomic) BOOL advancedExpanded;
@property(nonatomic) BOOL helpExpanded;
@property(nonatomic, weak) UIWindow *previousWindow;
@property(nonatomic, weak) UIWindow *editorWindow;
- (void)rebuild;
- (void)revealRule:(NSUInteger)index;
@end

@implementation PHOrbRuleEditor
- (instancetype)initWithRules:(NSArray *)rules save:(NSString *(^)(NSArray *))save {
    if ((self = [super init])) {
        _draft = [NSMutableArray array];
        for (NSDictionary *rule in rules) [_draft addObject:PHOrbNormalizedSkillRule(rule).mutableCopy];
        _saveRules = [save copy];
    }
    return self;
}
- (UILabel *)label:(NSString *)text {
    UILabel *label = [UILabel new]; label.text = text; label.numberOfLines = 0;
    label.textColor = UIColor.labelColor; label.font = [UIFont systemFontOfSize:15]; return label;
}
- (UIButton *)button:(NSString *)title {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    PHOrbStyleButton(button, title, NO); [button.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = UIColor.systemBackgroundColor; self.view.accessibilityIdentifier = @"padcheaper.orbsRuleEditor";
    UILabel *title = [self label:@"自動技能規則"]; title.font = [UIFont boldSystemFontOfSize:22];
    UIButton *cancel = [self button:@"取消"]; [cancel addTarget:self action:@selector(cancel) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, cancel]]; header.spacing = 12;
    PHOrbStyleHeader(header, title, cancel);
    header.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:header];
    UIScrollView *scroll = [UIScrollView new]; scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive; [self.view addSubview:scroll];
    self.rows = [UIStackView new]; self.rows.axis = UILayoutConstraintAxisVertical; self.rows.spacing = 16;
    self.rows.translatesAutoresizingMaskIntoConstraints = NO; [scroll addSubview:self.rows];
    self.notice = [self label:@""]; self.notice.textColor = UIColor.systemRedColor;
    self.notice.accessibilityIdentifier = @"padcheaper.orbsRuleNotice";
    UIButton *save = [self button:@"儲存規則"]; save.accessibilityIdentifier = @"padcheaper.orbsRuleSave";
    PHOrbStyleButton(save, @"儲存規則", YES);
    [save addTarget:self action:@selector(save) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *footer = [[UIStackView alloc] initWithArrangedSubviews:@[self.notice, save]];
    footer.axis = UILayoutConstraintAxisVertical; footer.spacing = 8; footer.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:footer];
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
        [self.rows.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [self.rows.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],
        [self.rows.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],
        [self.rows.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],
        [self.rows.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40]
    ]];
    [self rebuild];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    self.editorWindow = self.view.window;
    for (UIWindow *window in self.editorWindow.windowScene.windows)
        if (window.isKeyWindow && window != self.editorWindow) { self.previousWindow = window; break; }
    if (!self.previousWindow && [UIApplication.sharedApplication.delegate respondsToSelector:@selector(window)])
        self.previousWindow = UIApplication.sharedApplication.delegate.window;
    // Only the text editor borrows key status; the ordinary preview leaves the
    // game's window key. This is also needed by legacy AppDelegate guests.
    [self.editorWindow makeKeyWindow];
}
- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    [self.view endEditing:YES];
    if (self.editorWindow.isKeyWindow && self.previousWindow && !self.previousWindow.hidden) [self.previousWindow makeKeyWindow];
    self.previousWindow = nil; self.editorWindow = nil;
}
- (UIButton *)menu:(NSUInteger)index field:(NSString *)field values:(NSArray *)values titles:(NSArray *)titles {
    id selected = self.draft[index][field]; NSUInteger current = [values indexOfObject:selected];
    UIButton *button = [self button:current == NSNotFound ? @"請選擇" : titles[current]];
    UIButtonConfiguration *menuStyle = button.configuration;
    menuStyle.image = [UIImage systemImageNamed:@"chevron.down"]; menuStyle.imagePlacement = NSDirectionalRectEdgeTrailing;
    menuStyle.imagePadding = 8; button.configuration = menuStyle;
    button.showsMenuAsPrimaryAction = YES;
    button.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRule%@%lu", field, (unsigned long)index];
    NSMutableArray *actions = [NSMutableArray array]; __weak PHOrbRuleEditor *weakSelf = self;
    for (NSUInteger i = 0; i < values.count; ++i) {
        id value = values[i];
        UIAction *action = [UIAction actionWithTitle:titles[i] image:nil identifier:nil handler:^(__kindof UIAction *sender) {
            PHOrbRuleEditor *editor = weakSelf; if (!editor || index >= editor.draft.count) return;
            editor.draft[index][field] = value;
            if ([field isEqual:@"color"]) {
                editor.draft[index][@"below"] = [value integerValue] < 0 ? @0 : @3;
                editor.draft[index][@"atLeast"] = @0;
            }
            if ([field isEqual:@"obstacle"] && ![value isEqual:@"unmatchable"]) editor.draft[index][@"unmatchableColors"] = @0;
            [editor rebuild];
        }];
        action.state = [selected isEqual:value] ? UIMenuElementStateOn : UIMenuElementStateOff; [actions addObject:action];
    }
    button.menu = [UIMenu menuWithTitle:@"" children:actions]; return button;
}
- (UIView *)number:(NSUInteger)index field:(NSString *)key title:(NSString *)title {
    UITextField *field = [UITextField new]; field.borderStyle = UITextBorderStyleRoundedRect;
    field.font = [UIFont systemFontOfSize:16]; field.keyboardType = UIKeyboardTypeNumberPad;
    field.text = [self.draft[index][key] description]; field.accessibilityLabel = title;
    field.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRule%@%lu", key, (unsigned long)index];
    [field.widthAnchor constraintEqualToConstant:88].active = YES; [field.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    UIToolbar *toolbar = [UIToolbar new]; [toolbar sizeToFit];
    toolbar.items = @[[[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil],
        [[UIBarButtonItem alloc] initWithTitle:@"完成" style:UIBarButtonItemStyleDone target:self action:@selector(endEditing)]];
    field.inputAccessoryView = toolbar;
    __weak PHOrbRuleEditor *weakSelf = self; __weak UITextField *weakField = field;
    [field addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
        PHOrbRuleEditor *editor = weakSelf; if (!editor || index >= editor.draft.count) return;
        NSString *text = weakField.text ?: @"";
        BOOL digits = text.length > 0 && text.length <= 6 &&
            [text rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789"].invertedSet].location == NSNotFound;
        editor.draft[index][key] = digits ? (id)@(text.integerValue) : (id)text;
    }] forControlEvents:UIControlEventEditingChanged];
    UILabel *label = [self label:title];
    [label setContentCompressionResistancePriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[label, field]];
    row.spacing = 12; row.alignment = UIStackViewAlignmentCenter; return row;
}
- (NSString *)summaryForRule:(NSDictionary *)rule {
    if (!PHOrbSkillRulesValid(@[rule])) return @"設定未完整，請展開檢查。";
    NSMutableArray *parts = [NSMutableArray array];
    // Put triggers first; long interval/reservation explanations otherwise hide
    // the condition that distinguishes adjacent rules in the collapsed list.
    if (![rule[@"status"] isEqual:@"none"]) [parts addObject:PHOrbPlayerConditionTitle(rule[@"status"])];
    if (![rule[@"enemy"] isEqual:@"none"]) [parts addObject:PHOrbEnemyConditionTitle(rule[@"enemy"])];
    if (![rule[@"obstacle"] isEqual:@"none"]) [parts addObject:[rule[@"obstacle"] isEqual:@"unmatchable"] ?
        PHOrbUnmatchableConditionTitle([rule[@"unmatchableColors"] unsignedIntValue]) : PHOrbObstacleConditionTitle(rule[@"obstacle"])];
    if ([rule[@"hpAtMost"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"HP ≤ %@%%", rule[@"hpAtMost"]]];
    if ([rule[@"color"] integerValue] >= 0) [parts addObject:[NSString stringWithFormat:@"%@珠%@ %@",
        @[@"火",@"水",@"木",@"光",@"暗",@"心"][[rule[@"color"] unsignedIntegerValue]],
        [rule[@"atLeast"] unsignedIntValue] ? @"至少" : @"少於", [rule[@"atLeast"] unsignedIntValue] ? rule[@"atLeast"] : rule[@"below"]]];
    if (!parts.count) [parts addObject:@"無額外觸發條件"];
    [parts addObject:[rule[@"every"] unsignedIntValue] == 1 ? @"每回合" : [NSString stringWithFormat:@"每 %@ 回合", rule[@"every"]]];
    if ([rule[@"floor"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"第 %@ 層", rule[@"floor"]]];
    if ([rule[@"group"] unsignedIntValue]) [parts addObject:[NSString stringWithFormat:@"輪替 %@", rule[@"group"]]];
    if ([rule[@"once"] boolValue]) [parts addObject:@"本場一次"];
    if (![rule[@"enabled"] boolValue]) [parts addObject:@"已停用"];
    return [parts componentsJoinedByString:@" · "];
}
- (void)rebuild {
    [self.view endEditing:YES]; self.notice.text = @"";
    for (UIView *view in self.rows.arrangedSubviews) { [self.rows removeArrangedSubview:view]; [view removeFromSuperview]; }
    __weak PHOrbRuleEditor *weakSelf = self;
    UILabel *intro = [self label:self.draft.count ? @"由上至下判斷；點規則展開編輯，同一條的條件須全部成立。" : @"尚無技能規則。新增後設定位置與觸發條件；留空只會連續轉珠。"];
    intro.font = [UIFont systemFontOfSize:13]; intro.textColor = UIColor.secondaryLabelColor;
    [self.rows addArrangedSubview:intro];
    for (NSUInteger i = 0; i < self.draft.count; ++i) {
        NSDictionary *rule = self.draft[i];
        UIStackView *card = [UIStackView new]; card.axis = UILayoutConstraintAxisVertical; card.spacing = 8;
        card.backgroundColor = UIColor.secondarySystemBackgroundColor; card.layer.cornerRadius = 12;
        card.layoutMargins = UIEdgeInsetsMake(12,12,12,12); card.layoutMarginsRelativeArrangement = YES;
        UISwitch *enabled = [UISwitch new]; enabled.on = [rule[@"enabled"] boolValue];
        enabled.accessibilityLabel = [NSString stringWithFormat:@"啟用規則 %lu", (unsigned long)i + 1];
        __weak UISwitch *weakEnabled = enabled; __weak UIStackView *weakCard = card;
        [enabled addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            PHOrbRuleEditor *editor = weakSelf;
            if (editor && i < editor.draft.count) {
                editor.draft[i][@"enabled"] = @(weakEnabled.on);
                for (UIView *view in weakCard.arrangedSubviews) if ([view isKindOfClass:UILabel.class])
                    { ((UILabel *)view).text = editor.expandedRule == editor.draft[i] ? PHOrbSkillRuleDescription(editor.draft[i]) : [editor summaryForRule:editor.draft[i]];
                      view.accessibilityLabel = PHOrbSkillRuleDescription(editor.draft[i]); }
            }
        }] forControlEvents:UIControlEventValueChanged];
        UIButton *disclosure = [self button:[NSString stringWithFormat:@"%lu · 位置 %@ · %@ · %@",
            (unsigned long)i + 1, rule[@"slot"], [rule[@"source"] isEqual:@"body"] ? @"本體" :
            [rule[@"source"] isEqual:@"assist"] ? @"武裝" : [rule[@"source"] isEqual:@"body_or_assist"] ? @"本體優先" :
            @"目前技能", [rule[@"required"] boolValue] ? @"必要" : @"可略過"]];
        disclosure.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleExpand%lu", (unsigned long)i];
        BOOL expanded = self.expandedRule == rule;
        disclosure.accessibilityValue = expanded ? @"已展開" : @"已收起";
        UIButtonConfiguration *config = disclosure.configuration;
        config.image = [UIImage systemImageNamed:expanded ? @"chevron.up" : @"chevron.down"];
        config.imagePlacement = NSDirectionalRectEdgeTrailing; config.imagePadding = 6; disclosure.configuration = config;
        [disclosure addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            PHOrbRuleEditor *editor = weakSelf; if (!editor || i >= editor.draft.count) return;
            editor.expandedRule = editor.expandedRule == editor.draft[i] ? nil : editor.draft[i];
            editor.advancedExpanded = NO; [editor rebuild]; [editor revealRule:i];
        }] forControlEvents:UIControlEventTouchUpInside];
        [card addArrangedSubview:PHOrbSwitchRow(disclosure, enabled)];
        UILabel *summary = [self label:expanded ? PHOrbSkillRuleDescription(rule) : [self summaryForRule:rule]];
        summary.numberOfLines = expanded ? 0 : 3; summary.lineBreakMode = NSLineBreakByTruncatingTail;
        summary.accessibilityLabel = PHOrbSkillRuleDescription(rule);
        summary.font = [UIFont systemFontOfSize:13]; summary.textColor = UIColor.secondaryLabelColor;
        summary.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleSummary%lu", (unsigned long)i];
        [card addArrangedSubview:summary];
        card.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleCard%lu", (unsigned long)i];
        [self.rows addArrangedSubview:card];
        if (!expanded) continue;
        UIStackView *advanced = [UIStackView new];
        [card addArrangedSubview:[self menu:i field:@"slot" values:@[@1,@2,@3,@4,@5,@6]
            titles:@[@"位置 1 · 隊長", @"位置 2", @"位置 3", @"位置 4", @"位置 5", @"位置 6 · 助人"]]];
        [advanced addArrangedSubview:[self number:i field:@"monster" title:@"角色圖鑑編號（0 不限定）"]];
        [card addArrangedSubview:[self menu:i field:@"source" values:@[@"auto", @"body", @"assist", @"body_or_assist"]
            titles:@[@"遊戲目前技能", @"僅本體技能", @"僅武裝技能", @"本體優先，滿蓄改用武裝"]]];
        if ([rule[@"source"] isEqual:@"body_or_assist"]) [card addArrangedSubview:[self label:
            @"遊戲不能選本體時才用滿蓄武裝。指定 ID 核對本體；武裝不算完成本體規則，必要效果仍須解除，同組其他位置可接著使用。"]];
        [advanced addArrangedSubview:[self number:i field:@"skill" title:@"指定技能 ID（0 不限定）"]];
        [card addArrangedSubview:[self number:i field:@"every" title:@"回合間隔（1 每回合）"]];
        [advanced addArrangedSubview:[self menu:i field:@"intervalMode" values:@[@"run", @"accepted"]
            titles:@[@"從每次開始計算", @"從上次施放計算（同組共享）"]]];
        [advanced addArrangedSubview:[self number:i field:@"startAfter" title:@"先轉幾回合再啟用（0 立即）"]];
        if ([rule[@"intervalMode"] isEqual:@"accepted"]) [advanced addArrangedSubview:[self label:
            @"保留同場已觀察回合與施放間隔，停止重開不重算。先轉盤期間仍會處理其他必要規則。"]];
        UIButton *ready = [self button:@"技能就緒就放"];
        ready.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleReady%lu", (unsigned long)i];
        ready.accessibilityHint = @"設為每個轉珠回合檢查；仍需符合先轉回合、其他條件與輪替、保留設定";
        [ready addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            PHOrbRuleEditor *editor = weakSelf; if (!editor || i >= editor.draft.count) return;
            editor.draft[i][@"every"] = @1;
            [editor rebuild];
        }] forControlEvents:UIControlEventTouchUpInside];
        [card addArrangedSubview:ready];
        [card addArrangedSubview:[self number:i field:@"floor" title:@"指定樓層（0 所有樓層）"]];
        [advanced addArrangedSubview:[self menu:i field:@"group" values:@[@0,@1,@2,@3,@4,@5,@6]
            titles:@[@"獨立施放", @"輪替組 1", @"輪替組 2", @"輪替組 3", @"輪替組 4", @"輪替組 5", @"輪替組 6"]]];
        [advanced addArrangedSubview:[self menu:i field:@"once" values:@[@NO,@YES] titles:@[@"每個符合條件的回合", @"本場只用一次"]]];
        [advanced addArrangedSubview:[self number:i field:@"reserveUntil" title:@"此位置保留至第幾層（0 不保留）"]];
        [card addArrangedSubview:[self number:i field:@"hpAtMost" title:@"HP 百分比 ≤（1–100，0 不限定）"]];
        NSMutableArray *playerTitles = [NSMutableArray array];
        for (NSString *condition in PHOrbPlayerConditions()) [playerTitles addObject:PHOrbPlayerConditionTitle(condition)];
        [card addArrangedSubview:[self menu:i field:@"status" values:PHOrbPlayerConditions() titles:playerTitles]];
        [card addArrangedSubview:[self menu:i field:@"required" values:@[@NO,@YES]
            titles:@[@"不可用時略過", @"必要：未確認完成就停止"]]];
        [card addArrangedSubview:[self menu:i field:@"color" values:@[@-1,@0,@1,@2,@3,@4,@5]
            titles:@[@"不限定盤面", @"火珠數量", @"水珠數量", @"木珠數量", @"光珠數量", @"暗珠數量", @"心珠數量"]]];
        if ([rule[@"color"] integerValue] >= 0) {
            [card addArrangedSubview:[self number:i field:@"below" title:@"珠數少於（0 停用）"]];
            [card addArrangedSubview:[self number:i field:@"atLeast" title:@"珠數至少（與少於擇一）"]];
        }
        NSMutableArray *titles = [NSMutableArray array]; for (NSString *effect in PHOrbEnemyConditions()) [titles addObject:PHOrbEnemyConditionTitle(effect)];
        [card addArrangedSubview:[self menu:i field:@"enemy" values:PHOrbEnemyConditions() titles:titles]];
        NSMutableArray *obstacleTitles = [NSMutableArray array];
        for (NSString *condition in PHOrbObstacleConditions()) [obstacleTitles addObject:PHOrbObstacleConditionTitle(condition)];
        [card addArrangedSubview:[self menu:i field:@"obstacle" values:PHOrbObstacleConditions() titles:obstacleTitles]];
        if ([rule[@"obstacle"] isEqual:@"unmatchable"]) {
            const unsigned colors = [rule[@"unmatchableColors"] unsignedIntValue];
            UILabel *chosen = [self label:PHOrbUnmatchableConditionTitle(colors)];
            chosen.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleUnmatchableTitle%lu", (unsigned long)i];
            [card addArrangedSubview:chosen];
            for (unsigned row = 0; row < 2; ++row) {
                NSMutableArray *choices = [NSMutableArray array];
                for (unsigned column = 0; column < 3; ++column) {
                    const unsigned color = row * 3 + column, bit = 1u << color;
                    NSString *name = @[@"火", @"水", @"木", @"光", @"暗", @"心"][color];
                    UIButton *choice = [self button:[NSString stringWithFormat:@"%@%@", colors & bit ? @"✓ " : @"", name]];
                    choice.accessibilityLabel = [NSString stringWithFormat:@"%@不可消時觸發", name];
                    choice.accessibilityTraits |= colors & bit ? UIAccessibilityTraitSelected : 0;
                    choice.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleUnmatchable%u-%lu", color, (unsigned long)i];
                    [choice addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
                        PHOrbRuleEditor *editor = weakSelf; if (!editor || i >= editor.draft.count) return;
                        editor.draft[i][@"unmatchableColors"] = @([editor.draft[i][@"unmatchableColors"] unsignedIntValue] ^ bit);
                        [editor rebuild];
                    }] forControlEvents:UIControlEventTouchUpInside];
                    [choices addObject:choice];
                }
                UIStackView *line = [[UIStackView alloc] initWithArrangedSubviews:choices];
                line.spacing = 8; line.distribution = UIStackViewDistributionFillEqually; [card addArrangedSubview:line];
            }
            [card addArrangedSubview:[self label:@"可複選，任一所選珠色不可消就觸發；全部不選表示任意珠色。其他條件仍須同時成立。"]];
        }
        UIStackView *advancedCard = PHOrbEditorSection(@"進階：指定角色、技能與輪替", advanced, YES,
            [NSString stringWithFormat:@"padcheaper.orbsRuleAdvanced%lu", (unsigned long)i]);
        UIButton *advancedButton = (UIButton *)advancedCard.arrangedSubviews.firstObject;
        if (self.advancedExpanded) [advancedButton sendActionsForControlEvents:UIControlEventTouchUpInside];
        [advancedButton addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            PHOrbRuleEditor *editor = weakSelf; editor.advancedExpanded = !editor.advancedExpanded;
        }] forControlEvents:UIControlEventTouchUpInside];
        [card addArrangedSubview:advancedCard];
        NSMutableArray *buttons = [NSMutableArray array];
        for (NSUInteger actionIndex = 0; actionIndex < 3; ++actionIndex) {
            UIButton *button = [self button:@[@"上移", @"下移", @"刪除"][actionIndex]];
            button.enabled = actionIndex == 0 ? i > 0 : actionIndex == 1 ? i + 1 < self.draft.count : YES;
            button.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleOrder%lu-%lu", (unsigned long)actionIndex, (unsigned long)i];
            if (actionIndex == 2) button.tintColor = UIColor.systemRedColor;
            [button addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
                PHOrbRuleEditor *editor = weakSelf; if (!editor || i >= editor.draft.count) return;
                if (actionIndex == 2) { [editor.draft removeObjectAtIndex:i]; editor.expandedRule = nil; editor.advancedExpanded = NO; }
                else if (actionIndex == 0 && i) [editor.draft exchangeObjectAtIndex:i withObjectAtIndex:i - 1];
                else if (actionIndex == 1 && i + 1 < editor.draft.count) [editor.draft exchangeObjectAtIndex:i withObjectAtIndex:i + 1];
                [editor rebuild];
                if (editor.expandedRule) [editor revealRule:[editor.draft indexOfObjectIdenticalTo:editor.expandedRule]];
            }] forControlEvents:UIControlEventTouchUpInside]; [buttons addObject:button];
        }
        UIStackView *order = [[UIStackView alloc] initWithArrangedSubviews:buttons]; order.spacing = 8;
        order.distribution = UIStackViewDistributionFillEqually; [card addArrangedSubview:order];
    }
    UIButton *add = [self button:@"＋ 新增技能規則"]; add.enabled = self.draft.count < PHOrbMaxSkillRules;
    add.accessibilityIdentifier = @"padcheaper.orbsRuleAdd";
    [add addTarget:self action:@selector(add) forControlEvents:UIControlEventTouchUpInside]; [self.rows addArrangedSubview:add];
    UIStackView *help = [UIStackView new];
    [help addArrangedSubview:[self label:@"最多 12 條，從上到下判斷；同一條中的條件需全部成立。每個位置每回合最多放一次技能。一般規則不可用時略過；必要規則會先讓其他技能或同組替補嘗試補救，條件仍成立而未確認施放完成時停止轉珠。\n\n「從每次開始計算」以按下開始為第 1 回合，間隔 2 在第 1、3、5…回合判斷。「從上次施放計算」保留同場同策略的已觀察回合；同組共用上次施放，停止重開不重算。先轉 2 回合表示第三盤才啟用，其他必要規則照常判斷。樓層為遊戲顯示的第幾層（1–999）。\n\n使用該位置當前可用的技能，包含已蓄好的繼承；角色編號比對當前形態，0 表示只限定位置。敵人條件僅在效果存在且我方尚未開啟對應全隊無效化時觸發；不計個別覺醒。留空規則只會連續轉珠。"]];
    [help addArrangedSubview:[self label:@"「技能就緒就放」把回合間隔設為 1：連續模式在每次轉珠前檢查，遊戲確認可用才施放；其他條件、輪替組、本場一次與保留樓層仍生效。\n\n兩個 2 回合技能要輪流用：兩條都選同一輪替組、間隔 1，關閉本場一次。冷卻由遊戲判斷。「從每次開始計算」的間隔 2 只在第 1、3、5…回合檢查；「從上次施放計算」則需距離同組上次啟動至少兩回合，錯過時會在下一回合重查。"]];
    [help addArrangedSubview:[self label:@"HP 條件以目前有效上限計算，含固定／比例關卡及臨時上限修正；無法確認就停止，不把未知當成滿血。HP 與異常條件同時設定時，兩者都要成立。\n\n暫時覺醒無效與關卡禁止覺醒分開判斷；隊員綁定條件檢查全隊。技能封印期間不能靠施放技能自行解除，必要規則會停止並提示。\n\n同組任何一條確認完成，即滿足該組本回合的必要要求。必要設定不會越過保留樓層、本場一次或每位置限制；不同必要技能請分組。確認施放不等於保證回到 HP 門檻或解除所有異常，請指定合適的技能。"]];
    [help addArrangedSubview:[self label:@"同一輪替組每個轉珠回合只放一個技能；原生確認啟動後，下一回合繼續輪替，未就緒者可略過。保留施放間隔的组會先檢查下一位置的各條規則，吸收救援也推進輪替。組的優先度由最上方成員決定。\n\n保留樓層會限制同位置的所有規則，避免其他規則提前用掉技能。停用規則不保留技能。\n\n本場一次與當回合施放紀錄在停止後仍保留；未能確認啟動的一次性技能也不自動重試。離開本場或完整重啟遊戲後不保留紀錄。\n\n盤面妨礙條件決定何時嘗試技能；技能後會重新讀盤，路徑仍須符合所有目標與傷害限制。已支援的效果可以留在盤面。"]];
    [help addArrangedSubview:[self label:@"同一條規則的條件全部都要符合（AND）。要在「暫時覺醒無效或光不可消」時放同一隻，建立兩條相同位置、獨立施放的規則：一條只設暫時覺醒無效，另一條只設光不可消。任一符合即可嘗試，同位置每回合最多施放一次。\n\n不可消選擇多種珠色時，任一所選珠色不可消便符合；全部不選表示任意珠色。其他條件仍須同時符合。"]];
    UIStackView *helpCard = PHOrbEditorSection(@"規則說明與範例", help, YES, @"padcheaper.orbsRuleHelp");
    UIButton *helpButton = (UIButton *)helpCard.arrangedSubviews.firstObject;
    if (self.helpExpanded) [helpButton sendActionsForControlEvents:UIControlEventTouchUpInside];
    [helpButton addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
        PHOrbRuleEditor *editor = weakSelf; editor.helpExpanded = !editor.helpExpanded;
    }] forControlEvents:UIControlEventTouchUpInside];
    [self.rows addArrangedSubview:helpCard];
}
- (void)add {
    if (self.draft.count >= PHOrbMaxSkillRules) return;
    NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy]; rule[@"slot"] = @(self.draft.count % 6 + 1);
    [self.draft addObject:rule]; self.expandedRule = rule; self.advancedExpanded = NO;
    [self rebuild]; [self revealRule:self.draft.count - 1];
}
- (void)revealRule:(NSUInteger)index {
    [self.view layoutIfNeeded];
    NSString *identifier = [NSString stringWithFormat:@"padcheaper.orbsRuleCard%lu", (unsigned long)index];
    for (UIView *card in self.rows.arrangedSubviews) if ([card.accessibilityIdentifier isEqual:identifier]) {
        UIScrollView *scroll = (UIScrollView *)self.rows.superview;
        CGRect rect = [card convertRect:card.bounds toView:scroll]; rect.size.height = MIN(100, rect.size.height);
        [scroll scrollRectToVisible:rect animated:NO];
        UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, ((UIStackView *)card).arrangedSubviews.firstObject);
        break;
    }
}
- (void)save {
    [self.view endEditing:YES];
    if (!PHOrbSkillRulesValid(self.draft)) {
        for (NSUInteger i = 0; i < self.draft.count; ++i) if (!PHOrbSkillRulesValid(@[self.draft[i]])) {
            if (self.expandedRule != self.draft[i]) {
                self.expandedRule = self.draft[i]; self.advancedExpanded = YES; [self rebuild];
            }
            [self revealRule:i]; break;
        }
        self.notice.text = @"請檢查角色、群組與條件：間隔 1–999、先轉回合 0–999、樓層 0–999、缺珠 1–43、HP 百分比 0–100；保留樓層不能晚於指定樓層。"; return; }
    NSMutableArray *rules = [NSMutableArray array]; for (NSDictionary *rule in self.draft) [rules addObject:rule.copy];
    NSString *error = self.saveRules([rules copy]);
    if (error.length) { self.notice.text = error; return; }
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)endEditing { [self.view endEditing:YES]; }
- (void)cancel { [self dismissViewControllerAnimated:YES completion:nil]; }
@end
