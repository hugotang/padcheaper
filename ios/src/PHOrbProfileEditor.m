#import "PHOrbProfileEditor.h"
#import "PHOrbProfiles.h"
#import "PHFormationModel.h"
#import "PHOrbEight.h"
#import "PHOrbPlanning.h"
#import "PHOrbEditorUI.h"
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

@interface PHOrbProfileEditor () <UIDocumentPickerDelegate, UIAdaptivePresentationControllerDelegate>
@property(nonatomic, strong) NSMutableDictionary *draft;
@property(nonatomic, strong) NSDictionary *workingSettings;
@property(nonatomic, strong) NSDictionary *formations;
@property(nonatomic, strong) NSDictionary *context;
@property(nonatomic, copy) NSString *(^saveProfiles)(NSDictionary *, NSDictionary *);
@property(nonatomic, strong) UIStackView *rows;
@property(nonatomic, strong) NSMutableSet<NSString *> *openSections;
@property(nonatomic, strong) UILabel *notice;
@property(nonatomic, strong) UITextView *planText;
@property(nonatomic, copy) NSString *planDraftText;
@property(nonatomic, weak) UIWindow *previousWindow;
@property(nonatomic, weak) UIWindow *editorWindow;
@property(nonatomic, strong) NSURL *shareDirectory;
- (void)rebuild;
- (void)previewData:(NSData *)data;
- (void)pickFile;
- (void)share:(UIButton *)sender;
- (void)clearShare;
@end

@implementation PHOrbProfileEditor
- (instancetype)initWithLibrary:(NSDictionary *)library settings:(NSDictionary *)settings
    formations:(NSDictionary *)formations context:(NSDictionary *)context
    save:(NSString *(^)(NSDictionary *, NSDictionary *))save {
    if ((self = [super init])) {
        NSData *data = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
        _draft = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:NULL];
        _workingSettings = PHOrbCanonicalSettings(settings); _formations = formations; _context = context;
        _saveProfiles = [save copy]; _openSections = [NSMutableSet set];
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
- (UIStackView *)switchRow:(NSString *)title toggle:(UISwitch *)toggle {
    toggle.accessibilityLabel = title;
    return PHOrbSwitchRow([self label:title], toggle);
}
- (UIStackView *)section:(NSString *)title content:(UIStackView *)content key:(NSString *)key {
    UIStackView *section = PHOrbEditorSection(title, content, key != nil,
        key ? [@"padcheaper.orbsProfileSection-" stringByAppendingString:key] : nil);
    if (key) {
        UIButton *toggle = (UIButton *)section.arrangedSubviews.firstObject;
        if ([self.openSections containsObject:key]) [toggle sendActionsForControlEvents:UIControlEventTouchUpInside];
        __weak PHOrbProfileEditor *weakSelf = self;
        [toggle addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            PHOrbProfileEditor *editor = weakSelf;
            if ([editor.openSections containsObject:key]) [editor.openSections removeObject:key];
            else [editor.openSections addObject:key];
        }] forControlEvents:UIControlEventTouchUpInside];
    }
    return section;
}
- (UIButton *)button:(NSString *)title {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    PHOrbStyleButton(button, title, NO); [button.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = UIColor.systemBackgroundColor; self.view.accessibilityIdentifier = @"padcheaper.orbsProfileEditor";
    UILabel *title = [self label:@"隊伍／關卡策略"]; title.font = [UIFont boldSystemFontOfSize:22];
    UIButton *cancel = [self button:@"取消"]; [cancel addTarget:self action:@selector(cancel) forControlEvents:UIControlEventTouchUpInside];
    cancel.accessibilityIdentifier = @"padcheaper.orbsProfileCancel";
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, cancel]]; header.spacing = 12;
    PHOrbStyleHeader(header, title, cancel);
    header.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:header];
    UIScrollView *scroll = [UIScrollView new]; scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.keyboardDismissMode = UIScrollViewKeyboardDismissModeInteractive; [self.view addSubview:scroll];
    self.rows = [UIStackView new]; self.rows.axis = UILayoutConstraintAxisVertical; self.rows.spacing = 12;
    self.rows.translatesAutoresizingMaskIntoConstraints = NO; [scroll addSubview:self.rows];
    self.notice = [self label:@""]; self.notice.textColor = UIColor.systemRedColor;
    self.notice.accessibilityIdentifier = @"padcheaper.orbsProfileNotice";
    UIButton *save = [self button:@"儲存並選用"]; save.accessibilityIdentifier = @"padcheaper.orbsProfileSave";
    PHOrbStyleButton(save, @"儲存並選用", YES);
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
    if (self.presentedViewController && !self.isBeingDismissed) return;
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
    UIStackView *selection = [UIStackView new];
    UIStackView *basic = [UIStackView new];
    UIButton *choose = [self button:record ? record[@"name"] : @"通用設定（不綁策略）"];
    UIButtonConfiguration *choiceStyle = choose.configuration;
    choiceStyle.image = [UIImage systemImageNamed:@"chevron.down"]; choiceStyle.imagePlacement = NSDirectionalRectEdgeTrailing;
    choiceStyle.imagePadding = 8; choose.configuration = choiceStyle;
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
    choose.menu = [UIMenu menuWithTitle:@"選擇要編輯及使用的策略" children:choices]; [selection addArrangedSubview:choose];
    if (record[@"activity"]) {
        NSDictionary *guide = record[@"activity"][@"guide"];
        UISwitch *guideSwitch = [UISwitch new]; guideSwitch.on = PHOrbEightGuideEnabled(record[@"activity"]);
        guideSwitch.enabled = guide != nil;
        guideSwitch.accessibilityIdentifier = @"padcheaper.orbsEightGuideSkills";
        __weak UISwitch *weakGuideSwitch = guideSwitch;
        [guideSwitch addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            NSMutableDictionary *activity = [record[@"activity"] mutableCopy];
            NSMutableDictionary *changed = [activity[@"guide"] mutableCopy];
            if (!changed) return;
            changed[@"enabled"] = @(weakGuideSwitch.on); activity[@"guide"] = changed; record[@"activity"] = activity;
        }] forControlEvents:UIControlEventValueChanged];
        [basic addArrangedSubview:[self switchRow:@"按 GameWith 樓層攻略開技" toggle:guideSwitch]];
        UILabel *guideHelp = [self label:guide ?
            @"開啟：先完成本層指定技能再求路，可減少攻擊珠不足造成的多回合。關閉：現盤達標就轉，失敗才選救盤技能。儲存並選用後生效。B1 一律優先高 Combo。" :
            @"本策略未含攻略開技分支；匯入新版活動 JSON 後可啟用。B1 仍優先高 Combo。"];
        if (guide[@"preparation"]) guideHelp.text = [guideHelp.text stringByAppendingString:
            @" 尾層缺珠時可依攻略先消一組三珠，保留可轉攻擊色的珠，下一回合開技輸出；每場最多一次。"];
        guideHelp.font = [UIFont systemFontOfSize:13]; guideHelp.textColor = UIColor.secondaryLabelColor;
        guideHelp.accessibilityIdentifier = @"padcheaper.orbsEightGuideHelp";
        [basic addArrangedSubview:guideHelp];
    }
    UISwitch *autoSelect = [UISwitch new]; autoSelect.on = [self.draft[@"autoSelect"] boolValue];
    autoSelect.accessibilityIdentifier = @"padcheaper.orbsProfileAutoSelect"; __weak UISwitch *weakSwitch = autoSelect;
    [autoSelect addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
        weakSelf.draft[@"autoSelect"] = @(weakSwitch.on);
    }] forControlEvents:UIControlEventValueChanged];
    [selection addArrangedSubview:[self switchRow:@"開始時自動配對" toggle:autoSelect]];
    UILabel *matchingHelp = [self label:@"全域選用方式：按開始時配對隊伍與關卡，不會自行進入關卡。儲存後生效。"];
    matchingHelp.font = [UIFont systemFontOfSize:13]; matchingHelp.textColor = UIColor.secondaryLabelColor;
    [selection addArrangedSubview:matchingHelp];
    [self.rows addArrangedSubview:[self section:@"策略選用" content:selection key:nil]];
    if (record) {
        UITextField *name = [UITextField new]; name.borderStyle = UITextBorderStyleRoundedRect;
        name.font = [UIFont systemFontOfSize:16]; name.text = record[@"name"]; name.placeholder = @"策略名稱（最多 60 字）";
        name.accessibilityIdentifier = @"padcheaper.orbsProfileName"; [name.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
        __weak UITextField *weakName = name;
        [name addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) {
            record[@"name"] = weakName.text ?: @"";
        }] forControlEvents:UIControlEventEditingChanged]; [basic insertArrangedSubview:name atIndex:0];
        [self.rows addArrangedSubview:[self section:@"目前策略" content:basic key:nil]];
        UIStackView *binding = [UIStackView new];
        UIStackView *details = [UIStackView new];
        UIStackView *management = [UIStackView new];
        NSMutableArray *numbers = [NSMutableArray array]; BOOL any = NO;
        for (NSNumber *number in record[@"team"]) { [numbers addObject:number.unsignedIntValue ? number.stringValue : @"不限"]; any |= number.unsignedIntValue != 0; }
        [binding addArrangedSubview:[self label:any ? [NSString stringWithFormat:@"開始時角色：%@", [numbers componentsJoinedByString:@"／"]] : @"隊伍限制：不限"]];
        BOOL fullPlan = record[@"formation"] != nil;
        NSString *teamTitle = fullPlan ? (record[@"formationID"] ?
            [@"使用的隊伍：" stringByAppendingString:PHFormationLibraryTeamLabel(self.formations, record[@"formationID"])] :
            @"使用的隊伍：選擇既有配裝…") : @"從隊伍庫綁定角色";
        UIButton *team = [self button:teamTitle]; team.showsMenuAsPrimaryAction = YES;
        team.accessibilityIdentifier = @"padcheaper.orbsProfileFormation";
        NSMutableArray *teams = [NSMutableArray array];
        for (NSDictionary *formation in self.formations[@"teams"]) {
            NSDictionary *saved = formation[@"team"]; NSMutableArray *members = [NSMutableArray array];
            for (NSDictionary *slot in saved[@"slots"]) [members addObject:slot[@"body"][@"id"] ?: @0];
            if (members.count != 6) continue;
            NSArray *teamBinding = [members copy];
            UIAction *choice = [UIAction actionWithTitle:PHFormationLibraryTeamLabel(self.formations, formation[@"id"]) image:nil identifier:nil handler:^(__kindof UIAction *sender) {
                PHOrbProfileEditor *editor = weakSelf; if (!editor.selected) return;
                if (editor.selected[@"formation"]) {
                    NSDictionary *updated = PHOrbProfileUpdatingFormation(editor.selected, saved);
                    if (!updated) { editor.notice.text = @"這份隊伍的本體／武裝與開場不相容。"; return; }
                    editor.selected[@"formation"] = updated[@"formation"];
                    editor.selected[@"formationID"] = formation[@"id"];
                } else editor.selected[@"team"] = [teamBinding mutableCopy];
                [editor rebuild];
            }];
            if (fullPlan && !PHOrbProfileUpdatingFormation(record, saved)) choice.attributes = UIMenuElementAttributesDisabled;
            if ([record[@"formationID"] isEqual:formation[@"id"]]) choice.state = UIMenuElementStateOn;
            [teams addObject:choice];
        }
        team.menu = [UIMenu menuWithTitle:fullPlan ? @"選擇要保留的隊伍配裝" : @"使用開始時的角色形態核對" children:teams];
        team.enabled = teams.count > 0 && !record[@"activity"];
        [binding addArrangedSubview:team];
        if (fullPlan) {
            UILabel *loadoutHelp = [self label:@"選用後記住這份隊伍。日後在隊伍編成儲存的潛覺、等級與加值會同步到使用它的策略及匯出；本體與武裝必須符合原開場。有同名隊伍時，可在隊伍編成查看各份潛覺後選回修改版。"];
            loadoutHelp.font = [UIFont systemFontOfSize:13]; loadoutHelp.textColor = UIColor.secondaryLabelColor;
            [binding addArrangedSubview:loadoutHelp];
            if (record[@"formationID"] && !PHOrbProfileUpdatingFormation(record,
                PHFormationLibraryTeam(self.formations, record[@"formationID"]))) {
                UILabel *warning = [self label:@"原隊伍已刪除或更換本體／武裝；請重新選擇相容隊伍後儲存。"];
                warning.textColor = UIColor.systemOrangeColor; [binding addArrangedSubview:warning];
            }
        }
        [binding addArrangedSubview:[self action:@"不限定助人" identifier:@"padcheaper.orbsProfileAnyHelper" handler:^(PHOrbProfileEditor *editor) {
            if (editor.selected[@"activity"]) { editor.notice.text = @"8 人活動需核對完整固定隊伍；請在活動 JSON 一併更新。"; return; }
            if (editor.selected[@"formation"]) { editor.notice.text = @"整包會核對六格本體與武裝；請在匯入 JSON 一併修改隊伍及開場。"; return; }
            NSMutableArray *members = [editor.selected[@"team"] mutableCopy]; members[5] = @0;
            editor.selected[@"team"] = members; [editor rebuild];
        }]];
        [binding addArrangedSubview:[self action:@"取消隊伍限制" identifier:@"padcheaper.orbsProfileAnyTeam" handler:^(PHOrbProfileEditor *editor) {
            if (editor.selected[@"activity"]) { editor.notice.text = @"8 人活動需核對完整固定隊伍；請另選通用設定。"; return; }
            if (editor.selected[@"formation"]) { editor.notice.text = @"整包開場需要固定隊伍；請另存通用策略。"; return; }
            editor.selected[@"team"] = [@[@0,@0,@0,@0,@0,@0] mutableCopy]; [editor rebuild];
        }]];
        [binding addArrangedSubview:[self label:[record[@"dungeon"] unsignedIntValue] ?
            [NSString stringWithFormat:@"關卡限制：地下城 %@／子關卡 %@", record[@"dungeon"], record[@"floor"]] : @"關卡限制：不限"]];
        UIButton *capture = [self action:@"帶入最近讀盤的關卡" identifier:@"padcheaper.orbsProfileCapture" handler:^(PHOrbProfileEditor *editor) {
            editor.selected[@"dungeon"] = editor.context[@"dungeon"]; editor.selected[@"floor"] = editor.context[@"floor"];
            [editor rebuild];
        }];
        capture.enabled = [self.context[@"known"] boolValue] && [self.context[@"dungeon"] unsignedIntValue] && [self.context[@"floor"] unsignedIntValue];
        [binding addArrangedSubview:capture];
        [binding addArrangedSubview:[self action:@"取消關卡限制" identifier:@"padcheaper.orbsProfileAnyDungeon" handler:^(PHOrbProfileEditor *editor) {
            editor.selected[@"dungeon"] = @0; editor.selected[@"floor"] = @0; [editor rebuild];
        }]];
        [self.rows addArrangedSubview:[self section:@"適用隊伍與關卡" content:binding key:nil]];
        NSDictionary *settings = record[@"settings"];
        UIButton *share = [self button:@"分享策略檔案…"];
        share.accessibilityIdentifier = @"padcheaper.orbsProfileShare";
        [share addTarget:self action:@selector(share:) forControlEvents:UIControlEventTouchUpInside];
        [management addArrangedSubview:share];
        if (record[@"formation"]) {
            UILabel *summary = [self label:PHOrbPlanDescription(PHOrbPlanFromProfile(record))];
            summary.accessibilityIdentifier = @"padcheaper.orbsPlanSummary";
            [details addArrangedSubview:summary];
            [management addArrangedSubview:[self action:@"複製整包 config" identifier:@"padcheaper.orbsPlanExport" handler:^(PHOrbProfileEditor *editor) {
                NSDictionary *plan = PHOrbPlanFromProfile(editor.selected);
                NSData *data = plan ? [NSJSONSerialization dataWithJSONObject:plan options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:NULL] : nil;
                if (!data) { editor.notice.text = @"目前設定無效，無法匯出。"; return; }
                UIPasteboard.generalPasteboard.string = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
                editor.notice.text = @"已複製隊伍、開場、循環與盤面目標。";
            }]];
        }
        if (record[@"activity"]) {
            NSMutableArray *lines = [NSMutableArray arrayWithObject:@"8 人活動 Auto：本場技能、轉珠與換層，結算後停止。"];
            NSUInteger wave = 0;
            for (NSDictionary *floor in record[@"activity"][@"floors"]) [lines addObject:
                [NSString stringWithFormat:@"B%lu：%@",(unsigned long)++wave,PHOrbGoalsDescription(floor[@"goals"])]];
            [lines addObject:@"開始時會核對六格固定版本技能；技能與吸收狀態變更後重新讀盤。"];
            [details addArrangedSubview:[self label:[lines componentsJoinedByString:@"\n"]]];
        }
        [details addArrangedSubview:[self label:record[@"activity"] ?
            @"活動技能及樓層目標請更新活動 JSON；轉珠頁可調整求路品質。" :
            [NSString stringWithFormat:@"已存技能規則 %lu 條、盤面目標 %lu 項。選用後可回轉珠頁修改，修改會一併儲存至這份策略。",
            (unsigned long)[settings[@"rules"] count], (unsigned long)[settings[@"goals"] count]]]];
        [management addArrangedSubview:[self action:@"用轉珠頁目前設定覆蓋這份策略" identifier:@"padcheaper.orbsProfileOverwrite" handler:^(PHOrbProfileEditor *editor) {
            NSMutableDictionary *updated = [editor.selected mutableCopy]; updated[@"settings"] = editor.workingSettings;
            if (!PHOrbProfilesValid(@{@"schema":@1,@"selected":@"",@"autoSelect":@NO,@"profiles":@[updated]})) {
                editor.notice.text = @"目前設定與這份策略不相容；8 人活動不使用單人技能、低血補心或武裝 L 規則。"; return;
            }
            editor.selected[@"settings"] = editor.workingSettings; [editor rebuild];
        }]];
        [management addArrangedSubview:[self action:@"複製這份策略" identifier:@"padcheaper.orbsProfileCopy" handler:^(PHOrbProfileEditor *editor) {
            [editor addProfile:YES];
        }]];
        UIButton *remove = [self action:@"刪除這份策略" identifier:@"padcheaper.orbsProfileDelete" handler:^(PHOrbProfileEditor *editor) {
            [editor.draft[@"profiles"] removeObject:editor.selected]; editor.draft[@"selected"] = @""; [editor rebuild];
        }]; remove.tintColor = UIColor.systemRedColor; [management addArrangedSubview:remove];
        [self.rows addArrangedSubview:[self section:@"策略內容" content:details key:@"details"]];
        [self.rows addArrangedSubview:[self section:@"分享與管理" content:management key:@"management"]];
    }
    UIButton *add = [self action:@"＋ 將目前轉珠設定另存為策略" identifier:@"padcheaper.orbsProfileAdd" handler:^(PHOrbProfileEditor *editor) {
        [editor addProfile:NO];
    }]; add.enabled = [self.draft[@"profiles"] count] < PHOrbMaxProfiles; [self.rows addArrangedSubview:add];
    UIStackView *imports = [UIStackView new];
    [imports addArrangedSubview:[self action:@"從檔案匯入…" identifier:@"padcheaper.orbsProfileFileImport" handler:^(PHOrbProfileEditor *editor) {
        [editor pickFile];
    }]];
    UIStackView *paste = [UIStackView new];
    self.planText = [UITextView new]; self.planText.text = self.planDraftText ?: @"";
    self.planText.font = [UIFont monospacedSystemFontOfSize:14 weight:UIFontWeightRegular];
    self.planText.backgroundColor = UIColor.secondarySystemBackgroundColor; self.planText.layer.cornerRadius = 8;
    self.planText.autocorrectionType = UITextAutocorrectionTypeNo; self.planText.spellCheckingType = UITextSpellCheckingTypeNo;
    self.planText.accessibilityIdentifier = @"padcheaper.orbsPlanJSON";
    self.planText.accessibilityLabel = @"貼入整包 JSON";
    [self.planText.heightAnchor constraintEqualToConstant:180].active = YES;
    [paste addArrangedSubview:self.planText];
    [paste addArrangedSubview:[self action:@"貼上 config" identifier:@"padcheaper.orbsPlanPaste" handler:^(PHOrbProfileEditor *editor) {
        NSString *text = UIPasteboard.generalPasteboard.string;
        if (text.length > 131072) { editor.notice.text = @"單份 config 上限 128 KiB。"; return; }
        editor.planText.text = text ?: @"";
    }]];
    [paste addArrangedSubview:[self action:@"驗證並預覽匯入" identifier:@"padcheaper.orbsPlanImport" handler:^(PHOrbProfileEditor *editor) {
        NSData *data = [editor.planText.text dataUsingEncoding:NSUTF8StringEncoding];
        [editor previewData:data];
    }]];
    [imports addArrangedSubview:[self section:@"貼上 JSON 匯入" content:paste key:@"paste"]];
    [self.rows addArrangedSubview:[self section:@"匯入策略" content:imports key:nil]];
    UIStackView *help = [UIStackView new];
    [help addArrangedSubview:[self label:@"最多 64 份，儲存成功才生效。一般策略包含技能規則、盤面目標與求路品質，開始時核對本體圖鑑編號。\n\n整包 config 另含配裝與開場；執行時核對六格本體、武裝及開場變身進度，不以等級與覺醒作配對條件。切層後保持本次策略。\n\n自動配對優先選更具體的關卡，再比較限定的角色數；同等候選或沒有符合者會停止。這個開關不會自行開始遊戲或進入關卡。\n\n關卡限制取自最近成功讀盤，與技能規則中的第幾層不同。"]];
    [self.rows addArrangedSubview:[self section:@"策略配對與匯入說明" content:help key:@"help"]];
}
- (void)previewData:(NSData *)data {
    if (!data.length || data.length > 131072) { self.notice.text = @"請提供完整 JSON，單份上限 128 KiB。"; return; }
    NSString *reason = nil;
    NSDictionary *library = PHOrbProfilesImportingData(self.draft,data,&reason);
    if (!library) { self.notice.text = reason; return; }
    NSData *saved = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
    self.draft = [NSJSONSerialization JSONObjectWithData:saved options:NSJSONReadingMutableContainers error:NULL];
    self.planText.text = @""; [self.openSections addObject:@"details"]; [self rebuild];
    self.notice.text = @"已驗證並加入草稿。檢查上方內容，按「儲存並選用」才會套用設定。";
    [self.view layoutIfNeeded];
    UIScrollView *scroll = (UIScrollView *)self.rows.superview;
    [scroll setContentOffset:CGPointMake(0,-scroll.adjustedContentInset.top) animated:NO];
    UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification,self.rows.arrangedSubviews.firstObject);
}
- (void)clearShare {
    if (self.shareDirectory) [NSFileManager.defaultManager removeItemAtURL:self.shareDirectory error:NULL];
    self.shareDirectory = nil;
}
- (void)dealloc {
    if (_shareDirectory) [NSFileManager.defaultManager removeItemAtURL:_shareDirectory error:NULL];
}
- (void)share:(UIButton *)sender {
    [self.view endEditing:YES];
    if (self.presentedViewController) return;
    NSString *reason = nil; NSData *data = PHOrbExportProfileData(self.selected,&reason);
    if (!data) { self.notice.text = reason; return; }
    [self clearShare];
    NSURL *directory = [[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
        URLByAppendingPathComponent:[@"PADCheaper-share-" stringByAppendingString:NSUUID.UUID.UUIDString] isDirectory:YES];
    NSCharacterSet *unsafe = [[NSCharacterSet alphanumericCharacterSet] invertedSet];
    NSString *name = [[self.selected[@"name"] componentsSeparatedByCharactersInSet:unsafe] componentsJoinedByString:@"-"];
    if (name.length > 48) name = [name substringToIndex:48];
    if (!name.length) name = @"PADCheaper-strategy";
    NSURL *file = [directory URLByAppendingPathComponent:[name stringByAppendingPathExtension:@"json"]];
    if (![NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:NO
            attributes:@{NSFilePosixPermissions:@0700} error:NULL] ||
        ![data writeToURL:file options:NSDataWritingAtomic error:NULL]) {
        [NSFileManager.defaultManager removeItemAtURL:directory error:NULL];
        self.notice.text = @"無法建立分享檔案，請檢查儲存空間。"; return;
    }
    self.shareDirectory = directory;
    UIActivityViewController *activity = [[UIActivityViewController alloc] initWithActivityItems:@[file] applicationActivities:nil];
    activity.view.accessibilityIdentifier = @"padcheaper.orbsProfileShareSheet";
    UIPopoverPresentationController *popover = activity.popoverPresentationController;
    popover.sourceView = sender; popover.sourceRect = sender.bounds;
    __weak PHOrbProfileEditor *weakSelf = self;
    activity.completionWithItemsHandler = ^(UIActivityType type, BOOL completed, NSArray *items, NSError *error) {
        (void)type; (void)items;
        PHOrbProfileEditor *editor = weakSelf; [editor clearShare];
        editor.notice.text = error ? @"分享未完成，可重新嘗試。" : completed ? @"分享已完成。" : @"已取消分享。";
    };
    activity.presentationController.delegate = self;
    [self presentViewController:activity animated:YES completion:nil];
}
- (void)presentationControllerDidDismiss:(UIPresentationController *)presentationController {
    if ([presentationController.presentedViewController isKindOfClass:UIActivityViewController.class]) [self clearShare];
}
- (void)pickFile {
    [self.view endEditing:YES];
    if (self.presentedViewController) return;
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initForOpeningContentTypes:@[UTTypeJSON] asCopy:NO];
    picker.allowsMultipleSelection = NO; picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}
- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    (void)controller; self.notice.text = @"已取消檔案匯入。";
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
    NSURL *url = urls.count == 1 ? urls.firstObject : nil;
    if (!url.isFileURL) { self.notice.text = @"請選擇一個 JSON 檔案。"; return; }
    BOOL scoped = [url startAccessingSecurityScopedResource];
    __block NSData *data = nil; NSError *error = nil;
    NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
    [coordinator coordinateReadingItemAtURL:url options:0 error:&error byAccessor:^(NSURL *readURL) {
        NSNumber *size = nil; [readURL getResourceValue:&size forKey:NSURLFileSizeKey error:NULL];
        if (!size || size.unsignedLongLongValue > 131072) return;
        NSFileHandle *handle = [NSFileHandle fileHandleForReadingFromURL:readURL error:NULL];
        data = [handle readDataUpToLength:131073 error:NULL]; [handle closeAndReturnError:NULL];
    }];
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (error || !data.length || data.length > 131072) { self.notice.text = @"檔案無法讀取或超過 128 KiB。"; return; }
    [self previewData:data];
}
- (void)addProfile:(BOOL)duplicate {
    NSMutableArray *profiles = self.draft[@"profiles"];
    if (profiles.count >= PHOrbMaxProfiles) { self.notice.text = @"最多保存 64 份策略。"; return; }
    NSMutableDictionary *record = duplicate && self.selected ? [self.selected mutableCopy] :
        [@{@"settings":self.workingSettings, @"team":@[@0,@0,@0,@0,@0,@0], @"dungeon":@0, @"floor":@0} mutableCopy];
    record[@"id"] = NSUUID.UUID.UUIDString.lowercaseString;
    [record removeObjectForKey:@"formationID"]; // A copy starts with its own loadout unless explicitly rebound.
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
