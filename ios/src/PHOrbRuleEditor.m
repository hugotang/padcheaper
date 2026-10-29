#import "PHOrbRuleEditor.h"
#import "PHOrbRules.h"

@interface PHOrbRuleEditor ()
@property(nonatomic, strong) NSMutableArray<NSMutableDictionary *> *draft;
@property(nonatomic, copy) NSString *(^saveRules)(NSArray *);
@property(nonatomic, strong) UIStackView *rows;
@property(nonatomic, strong) UILabel *notice;
@property(nonatomic, weak) UIWindow *previousWindow;
@property(nonatomic, weak) UIWindow *editorWindow;
- (void)rebuild;
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
    UIButtonConfiguration *config = [UIButtonConfiguration tintedButtonConfiguration]; config.title = title;
    button.configuration = config; [button.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = UIColor.systemBackgroundColor; self.view.accessibilityIdentifier = @"padcheaper.orbsRuleEditor";
    UILabel *title = [self label:@"自動技能規則"]; title.font = [UIFont boldSystemFontOfSize:22];
    UIButton *cancel = [self button:@"取消"]; [cancel addTarget:self action:@selector(cancel) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, cancel]]; header.spacing = 12;
    header.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:header];
    UIScrollView *scroll = [UIScrollView new]; scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive; [self.view addSubview:scroll];
    self.rows = [UIStackView new]; self.rows.axis = UILayoutConstraintAxisVertical; self.rows.spacing = 16;
    self.rows.translatesAutoresizingMaskIntoConstraints = NO; [scroll addSubview:self.rows];
    self.notice = [self label:@""]; self.notice.textColor = UIColor.systemRedColor;
    self.notice.accessibilityIdentifier = @"padcheaper.orbsRuleNotice";
    UIButton *save = [self button:@"儲存規則"]; save.accessibilityIdentifier = @"padcheaper.orbsRuleSave";
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
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[[self label:title], field]];
    row.spacing = 12; row.alignment = UIStackViewAlignmentCenter; return row;
}
- (void)rebuild {
    [self.view endEditing:YES]; self.notice.text = @"";
    for (UIView *view in self.rows.arrangedSubviews) { [self.rows removeArrangedSubview:view]; [view removeFromSuperview]; }
    __weak PHOrbRuleEditor *weakSelf = self;
    for (NSUInteger i = 0; i < self.draft.count; ++i) {
        NSDictionary *rule = self.draft[i];
        UIStackView *card = [UIStackView new]; card.axis = UILayoutConstraintAxisVertical; card.spacing = 8;
        card.backgroundColor = UIColor.secondarySystemBackgroundColor; card.layer.cornerRadius = 12;
        card.layoutMargins = UIEdgeInsetsMake(12,12,12,12); card.layoutMarginsRelativeArrangement = YES;
        UISwitch *enabled = [UISwitch new]; enabled.on = [rule[@"enabled"] boolValue];
        enabled.accessibilityLabel = [NSString stringWithFormat:@"啟用規則 %lu", (unsigned long)i + 1];
        __weak UISwitch *weakEnabled = enabled;
        [enabled addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            PHOrbRuleEditor *editor = weakSelf;
            if (editor && i < editor.draft.count) editor.draft[i][@"enabled"] = @(weakEnabled.on);
        }] forControlEvents:UIControlEventValueChanged];
        UIStackView *heading = [[UIStackView alloc] initWithArrangedSubviews:@[
            [self label:[NSString stringWithFormat:@"優先 %lu", (unsigned long)i + 1]], enabled]];
        heading.spacing = 12; [card addArrangedSubview:heading];
        [card addArrangedSubview:[self menu:i field:@"slot" values:@[@1,@2,@3,@4,@5,@6]
            titles:@[@"位置 1 · 隊長", @"位置 2", @"位置 3", @"位置 4", @"位置 5", @"位置 6 · 助人"]]];
        [card addArrangedSubview:[self number:i field:@"monster" title:@"角色圖鑑編號（0 不限定）"]];
        [card addArrangedSubview:[self menu:i field:@"source" values:@[@"auto", @"body", @"assist"]
            titles:@[@"遊戲目前技能", @"僅本體技能", @"僅武裝技能"]]];
        [card addArrangedSubview:[self number:i field:@"skill" title:@"指定技能 ID（0 不限定）"]];
        [card addArrangedSubview:[self number:i field:@"every" title:@"回合間隔（1 每回合）"]];
        UIButton *ready = [self button:@"技能就緒就放"];
        ready.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsRuleReady%lu", (unsigned long)i];
        ready.accessibilityHint = @"設為每個轉珠回合檢查；仍需符合其他條件與輪替、保留設定";
        [ready addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            PHOrbRuleEditor *editor = weakSelf; if (!editor || i >= editor.draft.count) return;
            editor.draft[i][@"every"] = @1;
            [editor rebuild];
        }] forControlEvents:UIControlEventTouchUpInside];
        [card addArrangedSubview:ready];
        [card addArrangedSubview:[self number:i field:@"floor" title:@"指定樓層（0 所有樓層）"]];
        [card addArrangedSubview:[self menu:i field:@"group" values:@[@0,@1,@2,@3,@4,@5,@6]
            titles:@[@"獨立施放", @"輪替組 1", @"輪替組 2", @"輪替組 3", @"輪替組 4", @"輪替組 5", @"輪替組 6"]]];
        [card addArrangedSubview:[self menu:i field:@"once" values:@[@NO,@YES] titles:@[@"每個符合條件的回合", @"本場只用一次"]]];
        [card addArrangedSubview:[self number:i field:@"reserveUntil" title:@"此位置保留至第幾層（0 不保留）"]];
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
        NSMutableArray *buttons = [NSMutableArray array];
        for (NSUInteger actionIndex = 0; actionIndex < 3; ++actionIndex) {
            UIButton *button = [self button:@[@"上移", @"下移", @"刪除"][actionIndex]];
            button.enabled = actionIndex == 0 ? i > 0 : actionIndex == 1 ? i + 1 < self.draft.count : YES;
            if (actionIndex == 2) button.tintColor = UIColor.systemRedColor;
            [button addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
                PHOrbRuleEditor *editor = weakSelf; if (!editor || i >= editor.draft.count) return;
                if (actionIndex == 2) [editor.draft removeObjectAtIndex:i];
                else if (actionIndex == 0 && i) [editor.draft exchangeObjectAtIndex:i withObjectAtIndex:i - 1];
                else if (actionIndex == 1 && i + 1 < editor.draft.count) [editor.draft exchangeObjectAtIndex:i withObjectAtIndex:i + 1];
                [editor rebuild];
            }] forControlEvents:UIControlEventTouchUpInside]; [buttons addObject:button];
        }
        UIStackView *order = [[UIStackView alloc] initWithArrangedSubviews:buttons]; order.spacing = 8;
        order.distribution = UIStackViewDistributionFillEqually; [card addArrangedSubview:order];
        [self.rows addArrangedSubview:card];
    }
    UIButton *add = [self button:@"＋ 新增技能規則"]; add.enabled = self.draft.count < PHOrbMaxSkillRules;
    add.accessibilityIdentifier = @"padcheaper.orbsRuleAdd";
    [add addTarget:self action:@selector(add) forControlEvents:UIControlEventTouchUpInside]; [self.rows addArrangedSubview:add];
    [self.rows addArrangedSubview:[self label:@"最多 12 條，從上到下判斷；同一條中的條件需全部成立。每個位置每回合最多放一次技能。一般規則不可用時略過；必要規則會先讓其他技能或同組替補嘗試補救，條件仍成立而未確認施放完成時停止轉珠。\n\n回合從按下開始算第 1 回合；間隔 2 會在第 1、3、5…回合判斷。樓層為遊戲顯示的第幾層（1–999）。\n\n使用該位置當前可用的技能，包含已蓄好的繼承；角色編號比對當前形態，0 表示只限定位置。敵人條件僅在效果存在且我方尚未開啟對應全隊無效化時觸發；不計個別覺醒。留空規則只會連續轉珠。"]];
    [self.rows addArrangedSubview:[self label:@"「技能就緒就放」把回合間隔設為 1：連續模式在每次轉珠前檢查，遊戲確認可用才施放；其他條件、輪替組、本場一次與保留樓層仍生效。\n\n兩個 2 回合技能要輪流用：兩條都選同一輪替組、間隔 1，關閉本場一次。冷卻由遊戲判斷；間隔 2 會讓兩條都只在第 1、3、5…回合檢查。"]];
    [self.rows addArrangedSubview:[self label:@"HP 條件以目前有效上限計算，含固定／比例關卡及臨時上限修正；無法確認就停止，不把未知當成滿血。HP 與異常條件同時設定時，兩者都要成立。\n\n暫時覺醒無效與關卡禁止覺醒分開判斷；隊員綁定條件檢查全隊。技能封印期間不能靠施放技能自行解除，必要規則會停止並提示。\n\n同組任何一條確認完成，即滿足該組本回合的必要要求。必要設定不會越過保留樓層、本場一次或每位置限制；不同必要技能請分組。確認施放不等於保證回到 HP 門檻或解除所有異常，請指定合適的技能。"]];
    [self.rows addArrangedSubview:[self label:@"同一輪替組每個轉珠回合只放一個技能；原生確認啟動後，下回合從下一條開始，未就緒者可略過。組的優先度由最上方成員決定。\n\n保留樓層會限制同位置的所有規則，避免其他規則提前用掉技能。停用規則不保留技能。\n\n本場一次與當回合施放紀錄在停止後仍保留；未能確認啟動的一次性技能也不自動重試。離開本場或完整重啟遊戲後不保留紀錄。\n\n盤面妨礙條件決定何時嘗試技能；技能後會重新讀盤，路徑仍須符合所有目標與傷害限制。已支援的效果可以留在盤面。"]];
    [self.rows addArrangedSubview:[self label:@"同一條規則的條件全部都要符合（AND）。要在「暫時覺醒無效或光不可消」時放同一隻，建立兩條相同位置、獨立施放的規則：一條只設暫時覺醒無效，另一條只設光不可消。任一符合即可嘗試，同位置每回合最多施放一次。\n\n不可消選擇多種珠色時，任一所選珠色不可消便符合；全部不選表示任意珠色。其他條件仍須同時符合。"]];
}
- (void)add {
    if (self.draft.count >= PHOrbMaxSkillRules) return;
    NSMutableDictionary *rule = [PHOrbDefaultSkillRule() mutableCopy]; rule[@"slot"] = @(self.draft.count % 6 + 1);
    [self.draft addObject:rule]; [self rebuild];
}
- (void)save {
    [self.view endEditing:YES];
    if (!PHOrbSkillRulesValid(self.draft)) { self.notice.text = @"請檢查角色、群組與條件：間隔 1–999、樓層 0–999、缺珠 1–43、HP 百分比 0–100；保留樓層不能晚於指定樓層。"; return; }
    NSMutableArray *rules = [NSMutableArray array]; for (NSDictionary *rule in self.draft) [rules addObject:rule.copy];
    NSString *error = self.saveRules([rules copy]);
    if (error.length) { self.notice.text = error; return; }
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)endEditing { [self.view endEditing:YES]; }
- (void)cancel { [self dismissViewControllerAnimated:YES completion:nil]; }
@end
