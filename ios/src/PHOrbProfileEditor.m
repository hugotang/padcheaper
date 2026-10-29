#import "PHOrbProfileEditor.h"
#import "PHOrbProfiles.h"

@interface PHOrbProfileEditor ()
@property(nonatomic, strong) NSMutableDictionary *draft;
@property(nonatomic, strong) NSDictionary *workingSettings;
@property(nonatomic, strong) NSDictionary *formations;
@property(nonatomic, strong) NSDictionary *context;
@property(nonatomic, copy) NSString *(^saveProfiles)(NSDictionary *, NSDictionary *);
@property(nonatomic, strong) UIStackView *rows;
@property(nonatomic, strong) UILabel *notice;
@property(nonatomic, strong) UITextView *planText;
@property(nonatomic, copy) NSString *planDraftText;
@property(nonatomic, weak) UIWindow *previousWindow;
@property(nonatomic, weak) UIWindow *editorWindow;
- (void)rebuild;
@end

@implementation PHOrbProfileEditor
- (instancetype)initWithLibrary:(NSDictionary *)library settings:(NSDictionary *)settings
    formations:(NSDictionary *)formations context:(NSDictionary *)context
    save:(NSString *(^)(NSDictionary *, NSDictionary *))save {
    if ((self = [super init])) {
        NSData *data = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
        _draft = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL];
        _workingSettings = PHOrbCanonicalSettings(settings); _formations = formations; _context = context;
        _saveProfiles = [save copy];
    }
    return self;
}
- (NSMutableDictionary *)selected {
    return (NSMutableDictionary *)PHOrbProfileNamed(self.draft, self.draft[@"selected"]);
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
    self.view.backgroundColor = UIColor.systemBackgroundColor; self.view.accessibilityIdentifier = @"padcheaper.orbsProfileEditor";
    UILabel *title = [self label:@"隊伍／關卡策略"]; title.font = [UIFont boldSystemFontOfSize:22];
    UIButton *cancel = [self button:@"取消"]; [cancel addTarget:self action:@selector(cancel) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, cancel]]; header.spacing = 12;
    header.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:header];
    UIScrollView *scroll = [UIScrollView new]; scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive; [self.view addSubview:scroll];
    self.rows = [UIStackView new]; self.rows.axis = UILayoutConstraintAxisVertical; self.rows.spacing = 12;
    self.rows.translatesAutoresizingMaskIntoConstraints = NO; [scroll addSubview:self.rows];
    self.notice = [self label:@""]; self.notice.textColor = UIColor.systemRedColor;
    self.notice.accessibilityIdentifier = @"padcheaper.orbsProfileNotice";
    UIButton *save = [self button:@"儲存並選用"]; save.accessibilityIdentifier = @"padcheaper.orbsProfileSave";
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
- (UIButton *)action:(NSString *)title identifier:(NSString *)identifier handler:(void (^)(PHOrbProfileEditor *))handler {
    UIButton *button = [self button:title]; button.accessibilityIdentifier = identifier;
    __weak PHOrbProfileEditor *weakSelf = self;
    [button addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
        PHOrbProfileEditor *editor = weakSelf; if (editor) handler(editor);
    }] forControlEvents:UIControlEventTouchUpInside];
    return button;
}
- (void)rebuild {
    if (self.planText) self.planDraftText = self.planText.text;
    [self.view endEditing:YES]; self.notice.text = @"";
    for (UIView *view in self.rows.arrangedSubviews) { [self.rows removeArrangedSubview:view]; [view removeFromSuperview]; }
    NSMutableDictionary *record = self.selected;
    UIButton *choose = [self button:record ? record[@"name"] : @"通用設定（不綁策略）"];
    choose.accessibilityIdentifier = @"padcheaper.orbsProfileSelect"; choose.showsMenuAsPrimaryAction = YES;
    __weak PHOrbProfileEditor *weakSelf = self;
    NSMutableArray *choices = [NSMutableArray array];
    NSMutableArray *records = [NSMutableArray arrayWithObject:@{@"id":@"", @"name":@"通用設定（不綁策略）"}];
    [records addObjectsFromArray:self.draft[@"profiles"]];
    for (NSDictionary *option in records) {
        NSString *identifier = option[@"id"];
        UIAction *action = [UIAction actionWithTitle:option[@"name"] image:nil identifier:nil handler:^(__kindof UIAction *sender) {
            PHOrbProfileEditor *editor = weakSelf; if (!editor) return;
            editor.draft[@"selected"] = identifier; [editor rebuild];
        }];
        action.state = [identifier isEqual:self.draft[@"selected"]] ? UIMenuElementStateOn : UIMenuElementStateOff;
        [choices addObject:action];
    }
    choose.menu = [UIMenu menuWithTitle:@"選擇要編輯及使用的策略" children:choices]; [self.rows addArrangedSubview:choose];
    UISwitch *autoSelect = [UISwitch new]; autoSelect.on = [self.draft[@"autoSelect"] boolValue];
    autoSelect.accessibilityIdentifier = @"padcheaper.orbsProfileAutoSelect"; __weak UISwitch *weakSwitch = autoSelect;
    [autoSelect addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
        weakSelf.draft[@"autoSelect"] = @(weakSwitch.on);
    }] forControlEvents:UIControlEventValueChanged];
    UIStackView *autoRow = [[UIStackView alloc] initWithArrangedSubviews:@[[self label:@"按開始時自動配對隊伍／關卡"], autoSelect]];
    autoRow.spacing = 12; [self.rows addArrangedSubview:autoRow];
    if (record) {
        UITextField *name = [UITextField new]; name.borderStyle = UITextBorderStyleRoundedRect;
        name.font = [UIFont systemFontOfSize:16]; name.text = record[@"name"]; name.placeholder = @"策略名稱（最多 60 字）";
        name.accessibilityIdentifier = @"padcheaper.orbsProfileName"; [name.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
        __weak UITextField *weakName = name;
        [name addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            record[@"name"] = weakName.text ?: @"";
        }] forControlEvents:UIControlEventEditingChanged]; [self.rows addArrangedSubview:name];
        NSMutableArray *numbers = [NSMutableArray array]; BOOL any = NO;
        for (NSNumber *number in record[@"team"]) { [numbers addObject:number.unsignedIntValue ? number.stringValue : @"不限"]; any |= number.unsignedIntValue != 0; }
        [self.rows addArrangedSubview:[self label:any ? [NSString stringWithFormat:@"開始時角色：%@", [numbers componentsJoinedByString:@"／"]] : @"隊伍限制：不限"]];
        UIButton *team = [self button:@"從隊伍庫綁定角色"]; team.showsMenuAsPrimaryAction = YES;
        NSMutableArray *teams = [NSMutableArray array];
        for (NSDictionary *formation in self.formations[@"teams"]) {
            NSDictionary *saved = formation[@"team"]; NSMutableArray *members = [NSMutableArray array];
            for (NSDictionary *slot in saved[@"slots"]) [members addObject:slot[@"body"][@"id"] ?: @0];
            if (members.count != 6) continue;
            NSArray *binding = [members copy];
            [teams addObject:[UIAction actionWithTitle:saved[@"title"] ?: @"未命名隊伍" image:nil identifier:nil handler:^(__kindof UIAction *sender) {
                PHOrbProfileEditor *editor = weakSelf; if (!editor.selected) return;
                editor.selected[@"team"] = [binding mutableCopy]; [editor rebuild];
            }]];
        }
        team.menu = [UIMenu menuWithTitle:@"使用開始時的角色形態核對" children:teams]; team.enabled = teams.count > 0 && !record[@"formation"];
        [self.rows addArrangedSubview:team];
        [self.rows addArrangedSubview:[self action:@"不限定助人" identifier:@"padcheaper.orbsProfileAnyHelper" handler:^(PHOrbProfileEditor *editor) {
            if (editor.selected[@"formation"]) { editor.notice.text = @"整包會核對六格本體與武裝；請在匯入 JSON 一併修改隊伍及開場。"; return; }
            NSMutableArray *members = [editor.selected[@"team"] mutableCopy]; members[5] = @0;
            editor.selected[@"team"] = members; [editor rebuild];
        }]];
        [self.rows addArrangedSubview:[self action:@"取消隊伍限制" identifier:@"padcheaper.orbsProfileAnyTeam" handler:^(PHOrbProfileEditor *editor) {
            if (editor.selected[@"formation"]) { editor.notice.text = @"整包開場需要固定隊伍；請另存通用策略。"; return; }
            editor.selected[@"team"] = [@[@0,@0,@0,@0,@0,@0] mutableCopy]; [editor rebuild];
        }]];
        [self.rows addArrangedSubview:[self label:[record[@"dungeon"] unsignedIntValue] ?
            [NSString stringWithFormat:@"關卡限制：地下城 %@／子關卡 %@", record[@"dungeon"], record[@"floor"]] : @"關卡限制：不限"]];
        UIButton *capture = [self action:@"帶入最近讀盤的關卡" identifier:@"padcheaper.orbsProfileCapture" handler:^(PHOrbProfileEditor *editor) {
            editor.selected[@"dungeon"] = editor.context[@"dungeon"]; editor.selected[@"floor"] = editor.context[@"floor"];
            [editor rebuild];
        }];
        capture.enabled = [self.context[@"known"] boolValue] && [self.context[@"dungeon"] unsignedIntValue] && [self.context[@"floor"] unsignedIntValue];
        [self.rows addArrangedSubview:capture];
        [self.rows addArrangedSubview:[self action:@"取消關卡限制" identifier:@"padcheaper.orbsProfileAnyDungeon" handler:^(PHOrbProfileEditor *editor) {
            editor.selected[@"dungeon"] = @0; editor.selected[@"floor"] = @0; [editor rebuild];
        }]];
        NSDictionary *settings = record[@"settings"];
        if (record[@"formation"]) {
            UILabel *summary = [self label:PHOrbPlanDescription(PHOrbPlanFromProfile(record))];
            summary.accessibilityIdentifier = @"padcheaper.orbsPlanSummary";
            [self.rows addArrangedSubview:summary];
            [self.rows addArrangedSubview:[self action:@"複製整包 config" identifier:@"padcheaper.orbsPlanExport" handler:^(PHOrbProfileEditor *editor) {
                NSDictionary *plan = PHOrbPlanFromProfile(editor.selected);
                NSData *data = plan ? [NSJSONSerialization dataWithJSONObject:plan options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL] : nil;
                if (!data) { editor.notice.text = @"目前設定無效，無法匯出。"; return; }
                UIPasteboard.generalPasteboard.string = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
                editor.notice.text = @"已複製隊伍、開場、循環與盤面目標。";
            }]];
        }
        [self.rows addArrangedSubview:[self label:[NSString stringWithFormat:@"已存技能規則 %lu 條、盤面目標 %lu 項。選用後可回轉珠頁修改，修改會一併儲存至這份策略。",
            (unsigned long)[settings[@"rules"] count], (unsigned long)[settings[@"goals"] count]]]];
        [self.rows addArrangedSubview:[self action:@"用轉珠頁目前設定覆蓋這份策略" identifier:@"padcheaper.orbsProfileOverwrite" handler:^(PHOrbProfileEditor *editor) {
            editor.selected[@"settings"] = editor.workingSettings; [editor rebuild];
        }]];
        [self.rows addArrangedSubview:[self action:@"複製這份策略" identifier:@"padcheaper.orbsProfileCopy" handler:^(PHOrbProfileEditor *editor) {
            [editor addProfile:YES];
        }]];
        UIButton *remove = [self action:@"刪除這份策略" identifier:@"padcheaper.orbsProfileDelete" handler:^(PHOrbProfileEditor *editor) {
            [editor.draft[@"profiles"] removeObject:editor.selected]; editor.draft[@"selected"] = @""; [editor rebuild];
        }]; remove.tintColor = UIColor.systemRedColor; [self.rows addArrangedSubview:remove];
    }
    UIButton *add = [self action:@"＋ 將目前轉珠設定另存為策略" identifier:@"padcheaper.orbsProfileAdd" handler:^(PHOrbProfileEditor *editor) {
        [editor addProfile:NO];
    }]; add.enabled = [self.draft[@"profiles"] count] < PHOrbMaxProfiles; [self.rows addArrangedSubview:add];
    [self.rows addArrangedSubview:[self label:@"匯入隊伍＋開場＋循環＋盤面目標 config"]];
    self.planText = [UITextView new]; self.planText.text = self.planDraftText ?: @"";
    self.planText.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    self.planText.backgroundColor = UIColor.secondarySystemBackgroundColor; self.planText.layer.cornerRadius = 8;
    self.planText.autocorrectionType = UITextAutocorrectionTypeNo; self.planText.spellCheckingType = UITextSpellCheckingTypeNo;
    self.planText.accessibilityIdentifier = @"padcheaper.orbsPlanJSON";
    self.planText.accessibilityLabel = @"貼入整包 JSON";
    [self.planText.heightAnchor constraintEqualToConstant:180].active = YES;
    [self.rows addArrangedSubview:self.planText];
    [self.rows addArrangedSubview:[self action:@"貼上 config" identifier:@"padcheaper.orbsPlanPaste" handler:^(PHOrbProfileEditor *editor) {
        NSString *text = UIPasteboard.generalPasteboard.string;
        if (text.length > 131072) { editor.notice.text = @"單份 config 上限 128 KiB。"; return; }
        editor.planText.text = text ?: @"";
    }]];
    [self.rows addArrangedSubview:[self action:@"驗證並預覽匯入" identifier:@"padcheaper.orbsPlanImport" handler:^(PHOrbProfileEditor *editor) {
        NSData *data = [editor.planText.text dataUsingEncoding:NSUTF8StringEncoding];
        if (!data.length || data.length > 131072) { editor.notice.text = @"請貼入完整 JSON，單份上限 128 KiB。"; return; }
        id plan = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL]; NSString *reason = nil;
        NSDictionary *library = PHOrbProfilesImportingPlan(editor.draft, plan, &reason);
        if (!library) { editor.notice.text = reason ?: @"JSON 無法解析；未更動隊伍與策略。"; return; }
        NSData *saved = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
        editor.draft = [NSJSONSerialization JSONObjectWithData:saved options:NSJSONReadingMutableContainers error:NULL];
        editor.planText.text = @""; [editor rebuild];
        editor.notice.text = @"已驗證並加入草稿。請檢查上方隊伍與流程，按「儲存並選用」才會套用設定。";
        [editor.view layoutIfNeeded];
        UIScrollView *scroll = (UIScrollView *)editor.rows.superview;
        [scroll setContentOffset:CGPointMake(0, -scroll.adjustedContentInset.top) animated:NO];
        UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, editor.rows.arrangedSubviews.firstObject);
    }]];
    [self.rows addArrangedSubview:[self label:@"最多 64 份，儲存成功才生效。一般策略包含技能規則、盤面目標與求路品質，開始時核對本體圖鑑編號。\n\n整包 config 另含配裝與開場；執行時核對六格本體、武裝及開場變身進度，不以等級與覺醒作配對條件。切層後保持本次策略。\n\n自動配對優先選更具體的關卡，再比較限定的角色數；同等候選或沒有符合者會停止。這個開關不會自行開始遊戲或進入關卡。\n\n關卡限制取自最近成功讀盤，與技能規則中的第幾層不同。"]];
}
- (void)addProfile:(BOOL)duplicate {
    NSMutableArray *profiles = self.draft[@"profiles"];
    if (profiles.count >= PHOrbMaxProfiles) { self.notice.text = @"最多保存 64 份策略。"; return; }
    NSMutableDictionary *record = duplicate && self.selected ? [self.selected mutableCopy] :
        [@{@"settings":self.workingSettings, @"team":@[@0,@0,@0,@0,@0,@0], @"dungeon":@0, @"floor":@0} mutableCopy];
    record[@"id"] = NSUUID.UUID.UUIDString.lowercaseString;
    NSString *name = duplicate ? [record[@"name"] stringByAppendingString:@" 副本"] : [NSString stringWithFormat:@"新策略 %lu", (unsigned long)profiles.count + 1];
    record[@"name"] = name.length <= 60 ? name : [name substringToIndex:60];
    [profiles addObject:record]; self.draft[@"selected"] = record[@"id"]; [self rebuild];
}
- (void)save {
    [self.view endEditing:YES];
    if (!PHOrbProfilesValid(self.draft)) { self.notice.text = @"策略名稱、角色或關卡設定無效，請檢查後再儲存。"; return; }
    NSData *data = [NSJSONSerialization dataWithJSONObject:self.draft options:0 error:NULL];
    NSDictionary *saved = [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    NSString *error = self.saveProfiles(saved, self.workingSettings);
    if (error.length) { self.notice.text = error; return; }
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)cancel { [self dismissViewControllerAnimated:YES completion:nil]; }
@end
