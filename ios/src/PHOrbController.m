#import "PHOrbController.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#import "PHOrbProfileEditor.h"
#import "PHOrbRuleEditor.h"
#import "PHOrbRecoveryEditor.h"
#include "PHOrbModel.h"
#include <math.h>

@interface PHOrbGoalEditor : UIViewController
@property(nonatomic, strong) NSMutableArray<NSMutableDictionary *> *draft;
@property(nonatomic, copy) NSString *(^saveGoals)(NSArray *goals);
@property(nonatomic, strong) UIStackView *rows;
@property(nonatomic, strong) UILabel *notice;
- (instancetype)initWithGoals:(NSArray *)goals save:(NSString *(^)(NSArray *))save;
- (void)rebuild;
- (void)change:(NSUInteger)index field:(NSString *)field value:(id)value;
@end

@implementation PHOrbGoalEditor
- (instancetype)initWithGoals:(NSArray *)goals save:(NSString *(^)(NSArray *))save {
    if ((self = [super init])) {
        _draft = [NSMutableArray array];
        for (NSDictionary *goal in goals) [_draft addObject:goal.mutableCopy];
        _saveGoals = [save copy];
    }
    return self;
}
- (UILabel *)label:(NSString *)text {
    UILabel *label = [UILabel new]; label.text = text; label.numberOfLines = 0;
    label.font = [UIFont systemFontOfSize:15]; label.textColor = UIColor.labelColor; return label;
}
- (UIButton *)button:(NSString *)title {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *config = [UIButtonConfiguration tintedButtonConfiguration];
    config.title = title; button.configuration = config;
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.view.accessibilityIdentifier = @"padcheaper.orbsGoalEditor";
    UILabel *title = [self label:@"指定盤面目標"]; title.font = [UIFont boldSystemFontOfSize:22];
    UIButton *cancel = [self button:@"取消"];
    [cancel addTarget:self action:@selector(cancel) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, cancel]];
    header.spacing = 12; header.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:header];
    UIScrollView *scroll = [UIScrollView new]; scroll.translatesAutoresizingMaskIntoConstraints = NO; [self.view addSubview:scroll];
    self.rows = [UIStackView new]; self.rows.axis = UILayoutConstraintAxisVertical; self.rows.spacing = 16;
    self.rows.translatesAutoresizingMaskIntoConstraints = NO; [scroll addSubview:self.rows];
    UIButton *save = [self button:@"儲存目標"];
    save.configuration = [UIButtonConfiguration filledButtonConfiguration]; [save setTitle:@"儲存目標" forState:UIControlStateNormal];
    save.accessibilityIdentifier = @"padcheaper.orbsGoalSave";
    [save addTarget:self action:@selector(save) forControlEvents:UIControlEventTouchUpInside];
    self.notice = [self label:@""]; self.notice.textColor = UIColor.systemRedColor;
    self.notice.accessibilityIdentifier = @"padcheaper.orbsGoalNotice";
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
        [footer.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-12],
        [scroll.topAnchor constraintEqualToAnchor:header.bottomAnchor constant:16],
        [scroll.bottomAnchor constraintEqualToAnchor:footer.topAnchor constant:-12],
        [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [self.rows.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor],
        [self.rows.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],
        [self.rows.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],
        [self.rows.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],
        [self.rows.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40]
    ]];
    [self rebuild];
}
- (void)change:(NSUInteger)index field:(NSString *)field value:(id)value {
    if (index >= self.draft.count) return;
    NSMutableDictionary *goal = self.draft[index]; goal[field] = value;
    if ([field isEqual:@"kind"]) {
        goal[@"count"] = @1;
        goal[@"colors"] = [value isEqual:@"combo"] ? @0 : [goal[@"colors"] unsignedIntegerValue] ? goal[@"colors"] : @63;
        goal[@"length"] = [value isEqual:@"match"] ? @6 : @0;
    }
    if ([goal[@"kind"] isEqual:@"colors"]) {
        NSUInteger colors = 0;
        for (NSUInteger bit = 0; bit < 6; ++bit) colors += ([goal[@"colors"] unsignedIntegerValue] >> bit) & 1u;
        goal[@"count"] = @(MIN([goal[@"count"] unsignedIntegerValue], colors));
    }
    [self rebuild];
}
- (UIButton *)menu:(NSUInteger)index field:(NSString *)field values:(NSArray *)values titles:(NSArray *)titles {
    id selected = self.draft[index][field]; NSUInteger selectedIndex = [values indexOfObject:selected];
    NSString *title = selectedIndex == NSNotFound ? PHOrbColorTitle([selected unsignedIntegerValue]) : titles[selectedIndex];
    UIButton *button = [self button:title]; button.showsMenuAsPrimaryAction = YES;
    button.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsGoal%@%lu", field, (unsigned long)index];
    NSMutableArray *actions = [NSMutableArray array]; __weak PHOrbGoalEditor *weakSelf = self;
    for (NSUInteger i = 0; i < values.count; ++i) {
        id value = values[i];
        UIAction *action = [UIAction actionWithTitle:titles[i] image:nil identifier:nil handler:^(__kindof UIAction *sender) {
            [weakSelf change:index field:field value:value];
        }];
        action.state = [value isEqual:selected] ? UIMenuElementStateOn : UIMenuElementStateOff; [actions addObject:action];
    }
    button.menu = [UIMenu menuWithTitle:@"" children:actions]; return button;
}
- (UIStackView *)stepper:(NSUInteger)index field:(NSString *)field title:(NSString *)title minimum:(NSUInteger)minimum maximum:(NSUInteger)maximum {
    UIStepper *stepper = [UIStepper new]; stepper.minimumValue = minimum; stepper.maximumValue = maximum;
    stepper.value = [self.draft[index][field] doubleValue];
    stepper.accessibilityIdentifier = [NSString stringWithFormat:@"padcheaper.orbsGoal%@%lu", field, (unsigned long)index];
    stepper.accessibilityLabel = title;
    __weak PHOrbGoalEditor *weakSelf = self; __weak UIStepper *weakStepper = stepper;
    [stepper addAction:[UIAction actionWithHandler:^(__kindof UIAction *action) {
        [weakSelf change:index field:field value:@((NSUInteger)weakStepper.value)];
    }] forControlEvents:UIControlEventValueChanged];
    UILabel *label = [self label:[NSString stringWithFormat:@"%@：%@", title, self.draft[index][field]]];
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[label, stepper]]; row.spacing = 12; row.alignment = UIStackViewAlignmentCenter;
    [row.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES; return row;
}
- (void)rebuild {
    self.notice.text = @"";
    for (UIView *view in self.rows.arrangedSubviews) { [self.rows removeArrangedSubview:view]; [view removeFromSuperview]; }
    __weak PHOrbGoalEditor *weakSelf = self;
    for (NSUInteger i = 0; i < self.draft.count; ++i) {
        NSDictionary *goal = self.draft[i]; NSString *kind = goal[@"kind"];
        UIStackView *card = [UIStackView new]; card.axis = UILayoutConstraintAxisVertical; card.spacing = 8;
        card.backgroundColor = UIColor.secondarySystemBackgroundColor; card.layer.cornerRadius = 12;
        card.layoutMargins = UIEdgeInsetsMake(12, 12, 12, 12); card.layoutMarginsRelativeArrangement = YES;
        UIButton *remove = [self button:@"刪除"]; remove.tintColor = UIColor.systemRedColor;
        [remove addAction:[UIAction actionWithHandler:^(__kindof UIAction *action) {
            PHOrbGoalEditor *editor = weakSelf;
            if (editor && i < editor.draft.count) { [editor.draft removeObjectAtIndex:i]; [editor rebuild]; }
        }] forControlEvents:UIControlEventTouchUpInside];
        UIStackView *heading = [[UIStackView alloc] initWithArrangedSubviews:@[[self label:[NSString stringWithFormat:@"條件 %lu", (unsigned long)i + 1]], remove]];
        heading.spacing = 12; [card addArrangedSubview:heading];
        NSArray *kinds = @[@"l", @"cross", @"t", @"square", @"row", @"column", @"four", @"match", @"combo", @"colors"];
        NSMutableArray *titles = [NSMutableArray array]; for (NSString *value in kinds) [titles addObject:PHOrbGoalTitle(value)];
        [card addArrangedSubview:[self menu:i field:@"kind" values:kinds titles:titles]];
        if (![kind isEqual:@"combo"]) {
            NSArray *masks = @[@63, @31, @1, @2, @4, @8, @16, @32];
            NSMutableArray *colorTitles = [NSMutableArray array]; for (NSNumber *mask in masks) [colorTitles addObject:PHOrbColorTitle(mask.unsignedIntegerValue)];
            [card addArrangedSubview:[self menu:i field:@"colors" values:masks titles:colorTitles]];
        }
        NSUInteger maximum = [kind isEqual:@"combo"] || [kind isEqual:@"match"] ? 14 : 3;
        if ([kind isEqual:@"colors"]) {
            maximum = 0; for (NSUInteger bit = 0; bit < 6; ++bit) maximum += ([goal[@"colors"] unsignedIntegerValue] >> bit) & 1u;
        }
        [card addArrangedSubview:[self stepper:i field:@"count" title:[kind isEqual:@"combo"] ? @"最低 Combo" :
            [kind isEqual:@"colors"] ? @"顏色數" : @"獨立消除組數" minimum:1 maximum:MAX(maximum, [goal[@"count"] unsignedIntegerValue])]];
        if ([kind isEqual:@"match"]) [card addArrangedSubview:[self stepper:i field:@"length" title:@"每組最少珠數" minimum:3 maximum:PHOrbMaxCells]];
        [self.rows addArrangedSubview:card];
    }
    UIButton *add = [self button:@"＋ 新增條件"]; add.enabled = self.draft.count < PHOrbMaxGoals;
    add.accessibilityIdentifier = @"padcheaper.orbsGoalAdd";
    [add addTarget:self action:@selector(add) forControlEvents:UIControlEventTouchUpInside]; [self.rows addArrangedSubview:add];
    [self.rows addArrangedSubview:[self label:@"所有條件需同時達成。相同條件請增加組數，最多 6 項。\n\nL／十字／T 須獨立 5 顆，3×3 須獨立 9 顆，四連須直線 4 顆，周邊不連同色珠；形狀以首批消除為準。橫排與直行依實際盤面寬高判定。多組目標需分成不同 Combo。\n\nCombo 與多色可包含盤內連鎖，不含天降或隊長加算。"]];
}
- (void)add {
    if (self.draft.count >= PHOrbMaxGoals) return;
    [self.draft addObject:[@{@"kind":@"l", @"colors":@63, @"count":@1, @"length":@0} mutableCopy]]; [self rebuild];
}
- (void)save {
    PHOrbGoalSet goals = {0};
    if (!PHOrbReadGoals(self.draft, &goals) || !goals.count) {
        self.notice.text = @"請至少設定一項有效條件；相同形狀與珠色請合併成組數。"; return;
    }
    NSString *error = self.saveGoals(PHOrbGoalsJSON(&goals));
    if (error.length) { self.notice.text = error; return; }
    [self dismissViewControllerAnimated:YES completion:nil];
}
- (void)cancel { [self dismissViewControllerAnimated:YES completion:nil]; }
@end

@interface PHOrbBoardView : UIView
@property(nonatomic, copy) NSDictionary *board;
@property(nonatomic, copy) NSArray<NSNumber *> *path;
@property(nonatomic) NSUInteger step;
@end

@implementation PHOrbBoardView
- (void)drawRect:(CGRect)rect {
    NSUInteger width = [self.board[@"width"] unsignedIntegerValue], height = [self.board[@"height"] unsignedIntegerValue];
    NSArray *source = self.board[@"types"], *enhanced = self.board[@"enhanced"], *locked = self.board[@"locked"];
    const NSUInteger cells = width * height;
    if (!PHOrbDimensionsSupported((unsigned)width, (unsigned)height) || source.count != cells || enhanced.count != cells) {
        NSString *text = @"讀取盤面後顯示預覽";
        [text drawAtPoint:CGPointMake(16, 24) withAttributes:@{NSFontAttributeName:[UIFont systemFontOfSize:16],
            NSForegroundColorAttributeName:UIColor.secondaryLabelColor}];
        return;
    }
    NSMutableArray *types = source.mutableCopy, *plus = enhanced.mutableCopy;
    NSMutableArray *locks = [NSMutableArray array];
    for (NSUInteger i = 0; i < cells; ++i) [locks addObject:locked.count == cells ? locked[i] : @NO];
    NSUInteger through = MIN(self.step, self.path.count ? self.path.count - 1 : 0);
    for (NSUInteger i = 1; i <= through; ++i) {
        NSUInteger a = self.path[i - 1].unsignedIntegerValue, b = self.path[i].unsignedIntegerValue;
        if (a >= cells || b >= cells) return;
        [types exchangeObjectAtIndex:a withObjectAtIndex:b]; [plus exchangeObjectAtIndex:a withObjectAtIndex:b];
        [locks exchangeObjectAtIndex:a withObjectAtIndex:b];
    }
    NSArray *colors = @[[UIColor colorWithRed:0.91 green:0.24 blue:0.27 alpha:1],
        [UIColor colorWithRed:0.15 green:0.56 blue:0.95 alpha:1], [UIColor colorWithRed:0.13 green:0.66 blue:0.40 alpha:1],
        [UIColor colorWithRed:0.98 green:0.77 blue:0.22 alpha:1], [UIColor colorWithRed:0.55 green:0.35 blue:0.80 alpha:1],
        [UIColor colorWithRed:0.94 green:0.40 blue:0.64 alpha:1]];
    NSArray *names = @[@"火", @"水", @"木", @"光", @"暗", @"心"];
    CGFloat cell = MIN(self.bounds.size.width / width, self.bounds.size.height / height);
    CGPoint origin = CGPointMake((self.bounds.size.width - width * cell) / 2, (self.bounds.size.height - height * cell) / 2);
    for (NSUInteger i = 0; i < cells; ++i) {
        NSUInteger type = [types[i] unsignedIntegerValue];
        CGRect square = CGRectMake(origin.x + i % width * cell, origin.y + i / width * cell, cell, cell);
        [[UIColor colorWithWhite:(i % width + i / width) % 2 ? 0.20 : 0.16 alpha:1] setFill]; UIRectFill(square);
        UIColor *color = type < 6 ? colors[type] : UIColor.grayColor;
        [color setFill]; [[UIBezierPath bezierPathWithOvalInRect:CGRectInset(square, 4, 4)] fill];
        NSString *label = type < 6 ? names[type] : @"?";
        NSDictionary *attributes = @{NSFontAttributeName:[UIFont systemFontOfSize:MAX(12, cell * 0.30) weight:UIFontWeightBold],
            NSForegroundColorAttributeName:type == 3 ? UIColor.blackColor : UIColor.whiteColor};
        CGSize size = [label sizeWithAttributes:attributes];
        [label drawAtPoint:CGPointMake(CGRectGetMidX(square) - size.width / 2, CGRectGetMidY(square) - size.height / 2) withAttributes:attributes];
        if ([plus[i] boolValue]) [@"+" drawAtPoint:CGPointMake(CGRectGetMaxX(square) - 15, square.origin.y + 3)
            withAttributes:@{NSFontAttributeName:[UIFont boldSystemFontOfSize:14], NSForegroundColorAttributeName:UIColor.whiteColor}];
        if ([locks[i] boolValue]) [@"鎖" drawAtPoint:CGPointMake(square.origin.x + 4, square.origin.y + 3)
            withAttributes:@{NSFontAttributeName:[UIFont boldSystemFontOfSize:11], NSForegroundColorAttributeName:UIColor.whiteColor}];
    }
    if (!self.path.count || self.path[0].unsignedIntegerValue >= cells) return;
    UIBezierPath *line = [UIBezierPath bezierPath];
    for (NSUInteger i = 0; i <= through; ++i) {
        NSUInteger index = self.path[i].unsignedIntegerValue;
        if (index >= cells) return;
        CGPoint center = CGPointMake(origin.x + (index % width + 0.5) * cell, origin.y + (index / width + 0.5) * cell);
        if (i == 0) [line moveToPoint:center]; else [line addLineToPoint:center];
    }
    line.lineWidth = 5; line.lineCapStyle = kCGLineCapRound; line.lineJoinStyle = kCGLineJoinRound;
    [[UIColor colorWithWhite:0 alpha:0.65] setStroke]; [line stroke];
    line.lineWidth = 2.5; [UIColor.whiteColor setStroke]; [line stroke];
    for (NSNumber *position in @[@0, @(through)]) {
        NSUInteger step = position.unsignedIntegerValue, index = self.path[step].unsignedIntegerValue;
        CGPoint center = CGPointMake(origin.x + (index % width + 0.5) * cell, origin.y + (index / width + 0.5) * cell);
        [UIColor.whiteColor setStroke];
        UIBezierPath *ring = [UIBezierPath bezierPathWithOvalInRect:CGRectMake(center.x - 10, center.y - 10, 20, 20)];
        ring.lineWidth = 3; [ring stroke];
    }
}
@end

@interface PHOrbController ()
@property(nonatomic, copy) PHOverlaySnapshot snapshot;
@property(nonatomic, copy) PHOverlayAction action;
@property(nonatomic, copy) void (^close)(void);
@property(nonatomic, strong) NSDictionary *state;
@property(nonatomic, strong) PHOrbBoardView *boardView;
@property(nonatomic, strong) UILabel *status;
@property(nonatomic, strong) UILabel *position;
@property(nonatomic, strong) UILabel *availability;
@property(nonatomic, strong) UISegmentedControl *qualityControl;
@property(nonatomic, strong) UILabel *qualityDescription;
@property(nonatomic, strong) UISegmentedControl *goalMode;
@property(nonatomic, strong) UIButton *editGoals;
@property(nonatomic, strong) UILabel *goalDescription;
@property(nonatomic, strong) NSLayoutConstraint *boardAspect;
@property(nonatomic) NSUInteger boardWidth, boardHeight;
@property(nonatomic, strong) UISlider *slider;
@property(nonatomic, strong) UIButton *readButton;
@property(nonatomic, strong) UIButton *executeButton;
@property(nonatomic, strong) UIButton *stopButton;
@property(nonatomic, strong) UIButton *startButton;
@property(nonatomic, strong) UIButton *editRules;
@property(nonatomic, strong) UILabel *ruleDescription;
@property(nonatomic, strong) UILabel *playerDescription;
@property(nonatomic, strong) UILabel *decisionDescription;
@property(nonatomic, strong) UIButton *editRecovery;
@property(nonatomic, strong) UILabel *recoveryDescription;
@property(nonatomic, strong) UIButton *editProfiles;
@property(nonatomic, strong) UILabel *profileDescription;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic) NSUInteger generation;
- (void)editGoalSettings;
- (void)changeGoalMode;
@end

@implementation PHOrbController
- (instancetype)initWithSnapshot:(PHOverlaySnapshot)snapshot action:(PHOverlayAction)action close:(void (^)(void))close {
    if ((self = [super init])) { _snapshot = snapshot; _action = action; _close = close; _generation = NSUIntegerMax; }
    return self;
}
- (UILabel *)label:(CGFloat)size weight:(UIFontWeight)weight {
    UILabel *label = [UILabel new]; label.font = [UIFont systemFontOfSize:size weight:weight];
    label.textColor = UIColor.labelColor; label.numberOfLines = 0; return label;
}
- (UIButton *)button:(NSString *)title selector:(SEL)selector identifier:(NSString *)identifier {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *configuration = [UIButtonConfiguration tintedButtonConfiguration];
    configuration.title = title; configuration.cornerStyle = UIButtonConfigurationCornerStyleMedium;
    button.configuration = configuration; button.accessibilityIdentifier = identifier;
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:48].active = YES;
    [button addTarget:self action:selector forControlEvents:UIControlEventTouchUpInside];
    return button;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    self.view.backgroundColor = UIColor.systemBackgroundColor;
    self.view.accessibilityIdentifier = @"padcheaper.orbs";
    UIScrollView *scroll = [UIScrollView new]; scroll.translatesAutoresizingMaskIntoConstraints = NO;
    scroll.contentInsetAdjustmentBehavior = UIScrollViewContentInsetAdjustmentNever;
    [self.view addSubview:scroll];
    UILabel *title = [self label:26 weight:UIFontWeightBold]; title.text = @"自動轉珠";
    UILabel *subtitle = [self label:14 weight:UIFontWeightRegular];
    subtitle.text = @"單人 · 6×5／7×6／5×4 · 連續轉珠與技能規則";
    self.status = [self label:17 weight:UIFontWeightSemibold]; self.status.accessibilityIdentifier = @"padcheaper.orbsStatus";
    self.boardView = [PHOrbBoardView new];
    self.boardView.layer.cornerRadius = 12; self.boardView.clipsToBounds = YES;
    self.boardView.accessibilityIdentifier = @"padcheaper.orbsBoard";
    self.boardView.isAccessibilityElement = YES;
    self.boardWidth = 6; self.boardHeight = 5;
    self.boardAspect = [self.boardView.heightAnchor constraintEqualToAnchor:self.boardView.widthAnchor multiplier:5.0 / 6.0];
    self.boardAspect.active = YES;
    self.slider = [UISlider new]; self.slider.accessibilityIdentifier = @"padcheaper.orbsStep";
    self.slider.accessibilityLabel = @"路徑預覽步驟";
    [self.slider addTarget:self action:@selector(changeStep) forControlEvents:UIControlEventValueChanged];
    [self.slider.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    self.position = [self label:14 weight:UIFontWeightMedium];
    self.readButton = [self button:@"讀盤並求路" selector:@selector(readBoard) identifier:@"padcheaper.orbsRead"];
    self.executeButton = [self button:@"執行一次" selector:@selector(execute) identifier:@"padcheaper.orbsExecute"];
    self.executeButton.configuration = [UIButtonConfiguration filledButtonConfiguration];
    [self.executeButton setTitle:@"執行一次" forState:UIControlStateNormal];
    self.stopButton = [self button:@"停止" selector:@selector(stop) identifier:@"padcheaper.orbsStop"];
    self.stopButton.tintColor = UIColor.systemRedColor;
    [self.stopButton.widthAnchor constraintEqualToConstant:72].active = YES;
    self.startButton = [self button:@"開始連續轉珠" selector:@selector(startAutomatic) identifier:@"padcheaper.orbsStart"];
    self.editRules = [self button:@"編輯自動技能規則" selector:@selector(editSkillRules) identifier:@"padcheaper.orbsRuleEdit"];
    self.ruleDescription = [self label:13 weight:UIFontWeightRegular];
    self.ruleDescription.accessibilityIdentifier = @"padcheaper.orbsRuleDescription";
    self.playerDescription = [self label:13 weight:UIFontWeightMedium];
    self.playerDescription.accessibilityIdentifier = @"padcheaper.orbsPlayerStatus";
    self.decisionDescription = [self label:13 weight:UIFontWeightRegular];
    self.decisionDescription.accessibilityIdentifier = @"padcheaper.orbsDecisions";
    self.editRecovery = [self button:@"低血時的心珠目標" selector:@selector(editRecoverySettings) identifier:@"padcheaper.orbsRecoveryEdit"];
    self.recoveryDescription = [self label:13 weight:UIFontWeightRegular];
    self.recoveryDescription.accessibilityIdentifier = @"padcheaper.orbsRecoveryDescription";
    self.editProfiles = [self button:@"隊伍／關卡策略" selector:@selector(editProfileSettings) identifier:@"padcheaper.orbsProfileEdit"];
    self.profileDescription = [self label:13 weight:UIFontWeightRegular];
    self.profileDescription.accessibilityIdentifier = @"padcheaper.orbsProfileDescription";
    UILabel *qualityTitle = [self label:15 weight:UIFontWeightMedium]; qualityTitle.text = @"求路品質";
    self.qualityControl = [[UISegmentedControl alloc] initWithItems:@[@"快速", @"均衡", @"深入"]];
    self.qualityControl.accessibilityIdentifier = @"padcheaper.orbsQuality";
    self.qualityControl.accessibilityLabel = @"求路品質";
    [self.qualityControl.heightAnchor constraintGreaterThanOrEqualToConstant:36].active = YES;
    [self.qualityControl addTarget:self action:@selector(changeQuality) forControlEvents:UIControlEventValueChanged];
    self.qualityDescription = [self label:13 weight:UIFontWeightRegular];
    self.qualityDescription.accessibilityIdentifier = @"padcheaper.orbsQualityDescription";
    self.goalMode = [[UISegmentedControl alloc] initWithItems:@[@"高 Combo", @"指定形狀", @"隊長盤面"]];
    self.goalMode.accessibilityIdentifier = @"padcheaper.orbsGoalMode"; self.goalMode.accessibilityLabel = @"求路目標";
    [self.goalMode.heightAnchor constraintGreaterThanOrEqualToConstant:36].active = YES;
    [self.goalMode addTarget:self action:@selector(changeGoalMode) forControlEvents:UIControlEventValueChanged];
    self.editGoals = [self button:@"編輯形狀與珠色" selector:@selector(editGoalSettings) identifier:@"padcheaper.orbsGoalEdit"];
    self.goalDescription = [self label:13 weight:UIFontWeightRegular];
    self.goalDescription.accessibilityIdentifier = @"padcheaper.orbsGoalDescription";
    self.availability = [self label:13 weight:UIFontWeightRegular];
    self.availability.accessibilityIdentifier = @"padcheaper.orbsAvailability";
    UILabel *note = [self label:13 weight:UIFontWeightRegular]; note.textColor = UIColor.secondaryLabelColor;
    note.text = @"連續模式會先依規則使用技能，再讀盤求路；每次放完技能都重新判斷。只處理目前這場單人關卡，離開關卡或手動操作即停止。\n\n浮動按鈕在求路、技能動畫與等待回合時都可停止；已啟動的技能由遊戲完成。\n\n可先用「讀盤並求路／執行一次」核對。盤面需符合所選目標；技能處理後仍有未支援的珠型、轉轉或特殊規則會停止。預計 Combo 不含天降。";
    UIButton *close = [self button:@"返回遊戲" selector:@selector(closePage) identifier:@"padcheaper.orbsClose"];
    [close.widthAnchor constraintEqualToConstant:100].active = YES;
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, close]];
    header.axis = UILayoutConstraintAxisHorizontal; header.spacing = 12; header.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:header];
    UIStackView *manualActions = [[UIStackView alloc] initWithArrangedSubviews:@[self.readButton, self.executeButton]];
    manualActions.axis = UILayoutConstraintAxisHorizontal; manualActions.spacing = 10;
    manualActions.distribution = UIStackViewDistributionFillEqually;
    UIStackView *autoActions = [[UIStackView alloc] initWithArrangedSubviews:@[self.startButton, self.stopButton]];
    autoActions.spacing = 10;
    UIStackView *actions = [[UIStackView alloc] initWithArrangedSubviews:@[autoActions, manualActions]];
    actions.axis = UILayoutConstraintAxisVertical; actions.spacing = 10; actions.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:actions];
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[subtitle, self.editProfiles, self.profileDescription, qualityTitle, self.qualityControl,
        self.qualityDescription, self.goalMode, self.editGoals, self.goalDescription,
        self.editRules, self.ruleDescription, self.editRecovery, self.recoveryDescription, self.playerDescription, self.decisionDescription,
        self.status, self.boardView, self.slider, self.position, self.availability, note]];
    stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 12; stack.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:stack];
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [header.topAnchor constraintEqualToAnchor:safe.topAnchor constant:12],
        [header.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [header.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [actions.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:20],
        [actions.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-20],
        [actions.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-12],
        [scroll.topAnchor constraintEqualToAnchor:header.bottomAnchor constant:10],
        [scroll.bottomAnchor constraintEqualToAnchor:actions.topAnchor constant:-12],
        [scroll.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor], [scroll.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:8],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-24],
        [stack.centerXAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.centerXAnchor],
        [stack.leadingAnchor constraintGreaterThanOrEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],
        [stack.trailingAnchor constraintLessThanOrEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],
        [stack.widthAnchor constraintLessThanOrEqualToConstant:440]
    ]];
    NSLayoutConstraint *width = [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40];
    width.priority = UILayoutPriorityDefaultHigh; width.active = YES;
    [self refresh];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    self.timer = [NSTimer timerWithTimeInterval:0.15 target:self selector:@selector(refresh) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSRunLoopCommonModes];
}
- (void)viewWillDisappear:(BOOL)animated { [super viewWillDisappear:animated]; [self.timer invalidate]; self.timer = nil; }
- (void)refresh {
    NSDictionary *state = self.snapshot()[@"orbState"] ?: @{};
    self.state = state;
    if (!self.isViewLoaded) return;
    self.status.text = state[@"message"] ?: @"等待初始化";
    self.readButton.enabled = [state[@"canRead"] boolValue]; self.executeButton.enabled = [state[@"canExecute"] boolValue];
    self.stopButton.enabled = [state[@"canStop"] boolValue];
    self.startButton.enabled = [state[@"canStart"] boolValue];
    self.editProfiles.enabled = [state[@"canSetProfiles"] boolValue] && ![state[@"busy"] boolValue] && ![state[@"inputFault"] boolValue];
    NSDictionary *profiles = state[@"profiles"] ?: PHOrbDefaultProfiles();
    NSDictionary *profile = [state[@"automatic"] boolValue] ? state[@"activeProfile"] : PHOrbProfileNamed(profiles, profiles[@"selected"]);
    NSString *profileMessage = state[@"profileMessage"];
    self.profileDescription.text = profileMessage.length ? profileMessage : [NSString stringWithFormat:@"%@%@",
        profile[@"name"] ?: @"通用設定", [profiles[@"autoSelect"] boolValue] ? @" · 開始時自動配對" : @""];
    self.profileDescription.textColor = profileMessage.length ? UIColor.systemRedColor : UIColor.secondaryLabelColor;
    self.editRules.enabled = [state[@"canSetRules"] boolValue] && ![state[@"busy"] boolValue] && ![state[@"inputFault"] boolValue];
    NSMutableArray *ruleLines = [NSMutableArray array];
    for (NSDictionary *rule in state[@"rules"]) [ruleLines addObject:PHOrbSkillRuleDescription(rule)];
    if (!ruleLines.count) [ruleLines addObject:@"尚無技能規則；開始後只會連續轉珠。"];
    if ([state[@"automatic"] boolValue]) [ruleLines addObject:[NSString stringWithFormat:@"連續第 %@ 回合 · 已轉珠 %@ 次 · 已確認技能 %@ 次",
        state[@"autoTurn"], state[@"autoMoves"], state[@"autoSkills"]]];
    if ([state[@"obstacles"] length]) [ruleLines addObject:[NSString stringWithFormat:@"目前盤面：%@", state[@"obstacles"]]];
    NSString *ruleMessage = state[@"ruleMessage"];
    self.ruleDescription.text = ruleMessage.length ? ruleMessage : [ruleLines componentsJoinedByString:@"\n"];
    self.ruleDescription.textColor = ruleMessage.length ? UIColor.systemRedColor : UIColor.secondaryLabelColor;
    self.playerDescription.text = state[@"playerStatus"] ?: @"";
    self.playerDescription.hidden = !self.playerDescription.text.length;
    NSMutableArray *decisions = [NSMutableArray array];
    NSArray *rows = state[@"ruleDecisions"] ?: @[];
    if (rows.count) [decisions addObject:[NSString stringWithFormat:@"最近技能判斷 · 第 %@ 回合", state[@"decisionTurn"] ?: @0]];
    for (NSUInteger i = 0; i < rows.count; ++i)
        [decisions addObject:[NSString stringWithFormat:@"規則 %lu：%@", (unsigned long)i + 1, rows[i]]];
    if ([state[@"automationLog"] count]) {
        [decisions addObject:@"近期事件"];
        [decisions addObjectsFromArray:state[@"automationLog"]];
    }
    self.decisionDescription.text = [decisions componentsJoinedByString:@"\n"];
    self.decisionDescription.hidden = !decisions.count;
    self.editRecovery.enabled = [state[@"canSetRecovery"] boolValue] && ![state[@"busy"] boolValue] && ![state[@"inputFault"] boolValue];
    NSString *recoveryMessage = state[@"recoveryMessage"];
    NSMutableArray *recoveryLines = [NSMutableArray arrayWithObject:PHOrbRecoveryDescription(state[@"recovery"] ?: PHOrbDefaultRecovery())];
    if ([state[@"recoveryStatus"] length]) {
        if ([state[@"recoveryTurn"] unsignedIntegerValue])
            [recoveryLines addObject:[NSString stringWithFormat:@"最近心珠判斷 · 第 %@ 回合", state[@"recoveryTurn"]]];
        [recoveryLines addObject:state[@"recoveryStatus"]];
    }
    self.recoveryDescription.text = recoveryMessage.length ? recoveryMessage : [recoveryLines componentsJoinedByString:@"\n"];
    self.recoveryDescription.textColor = recoveryMessage.length ? UIColor.systemRedColor : UIColor.secondaryLabelColor;
    NSArray *qualities = @[@"fast", @"balanced", @"thorough"];
    NSUInteger quality = [qualities indexOfObject:state[@"quality"] ?: @"balanced"];
    if (quality == NSNotFound) quality = 1;
    self.qualityControl.selectedSegmentIndex = (NSInteger)quality;
    self.qualityControl.enabled = [state[@"canSetQuality"] boolValue] && ![state[@"busy"] boolValue] && ![state[@"inputFault"] boolValue];
    NSString *qualityMessage = state[@"qualityMessage"];
    self.qualityDescription.text = qualityMessage.length ? qualityMessage :
        @[@"較快取得路徑，適合一般回合。", @"兼顧求路速度與搜尋範圍。", @"搜尋更多候選路徑，需要等候較久；不保證 Combo 一定較高。"][quality];
    self.qualityDescription.textColor = qualityMessage.length ? UIColor.systemRedColor : UIColor.secondaryLabelColor;
    NSString *mode = state[@"goalMode"] ?: @"combo";
    NSUInteger modeIndex = [@[@"combo", @"manual", @"leaders"] indexOfObject:mode];
    self.goalMode.selectedSegmentIndex = modeIndex == NSNotFound ? 0 : (NSInteger)modeIndex;
    self.goalMode.enabled = [state[@"canSetGoals"] boolValue] && ![state[@"busy"] boolValue] && ![state[@"inputFault"] boolValue];
    NSDictionary *plan = state[@"goalPlan"] ?: @{};
    BOOL leaderMode = [mode isEqual:@"leaders"];
    NSArray *editableGoals = plan[@"baseGoals"] ?: plan[@"goals"];
    self.editGoals.hidden = ![mode isEqual:@"manual"] && !(leaderMode && [plan[@"available"] boolValue] && editableGoals.count);
    self.editGoals.enabled = self.goalMode.enabled;
    UIButtonConfiguration *editConfig = self.editGoals.configuration;
    editConfig.title = leaderMode ? @"用這些條件自訂" : @"編輯形狀與珠色"; self.editGoals.configuration = editConfig;
    NSArray *activeGoals = plan[@"goals"], *hits = state[@"goalHits"];
    BOOL hasGoalResults = [state[@"path"] count] && activeGoals.count && hits.count == activeGoals.count;
    NSMutableArray *goalLines = [NSMutableArray array];
    if (leaderMode) {
        [goalLines addObjectsFromArray:plan[@"leaders"] ?: @[]];
        if (!hasGoalResults) [goalLines addObject:plan[@"message"] ?: @"讀盤後，從目前隊長／助人帶入可辨識的盤面條件。"];
        [goalLines addObjectsFromArray:plan[@"unsupported"] ?: @[]];
    } else if (!hasGoalResults) [goalLines addObject:PHOrbGoalsDescription([mode isEqual:@"manual"] ? state[@"goals"] : @[])];
    if (hasGoalResults) {
        for (NSUInteger i = 0; i < activeGoals.count; ++i) {
            BOOL met = [hits[i] unsignedIntegerValue] >= [activeGoals[i][@"count"] unsignedIntegerValue];
            [goalLines addObject:[NSString stringWithFormat:@"%@ %@（%@／%@）", met ? @"✓" : @"未達成：",
                PHOrbGoalDescription(activeGoals[i]), hits[i], activeGoals[i][@"count"]]];
        }
    }
    if (leaderMode) [goalLines addObject:plan[@"warning"] ?: @"僅規劃珠型與最低門檻，不代表完整隊長技已發動。"];
    NSString *goalMessage = state[@"goalMessage"];
    self.goalDescription.text = goalMessage.length ? goalMessage : [goalLines componentsJoinedByString:@"\n"];
    self.goalDescription.textColor = goalMessage.length ? UIColor.systemRedColor : UIColor.secondaryLabelColor;
    self.availability.text = [state[@"inputFault"] boolValue] ? @"輸入恢復未通過，請完整重啟遊戲後再試。" :
        ![state[@"canRead"] boolValue] && ![state[@"busy"] boolValue] ? @"等待遊戲校驗與登入後即可讀盤。" :
        ![state[@"enabled"] boolValue] ? @"預覽可使用；執行需要先開啟 PC 面板的功能總開關。" :
        [state[@"phase"] isEqual:@"preview"] && [state[@"goalCount"] unsignedIntegerValue] && ![state[@"goalsMet"] boolValue] ?
            @"這條路徑未達成全部目標，只供預覽；提高品質或調整條件後重新讀盤。" :
        [state[@"phase"] isEqual:@"preview"] && ![state[@"canExecute"] boolValue] ? @"目前無法執行，請重新讀盤並確認操作時間。" : @"";
    self.boardView.board = state[@"board"]; self.boardView.path = state[@"path"];
    NSUInteger width = [state[@"board"][@"width"] unsignedIntegerValue], height = [state[@"board"][@"height"] unsignedIntegerValue];
    if (PHOrbDimensionsSupported((unsigned)width, (unsigned)height) && (width != self.boardWidth || height != self.boardHeight)) {
        self.boardAspect.active = NO;
        self.boardAspect = [self.boardView.heightAnchor constraintEqualToAnchor:self.boardView.widthAnchor multiplier:(CGFloat)height / width];
        self.boardAspect.active = YES; self.boardWidth = width; self.boardHeight = height;
    }
    NSArray *path = state[@"path"];
    self.slider.maximumValue = path.count > 0 ? path.count - 1 : 0;
    self.slider.enabled = path.count > 1;
    NSUInteger generation = [state[@"generation"] unsignedIntegerValue];
    if (generation != self.generation) { self.generation = generation; self.slider.value = 0; }
    [self changeStep];
}
- (void)changeStep {
    self.slider.value = roundf(self.slider.value);
    self.boardView.step = (NSUInteger)self.slider.value;
    NSArray *path = self.boardView.path;
    NSUInteger width = [self.boardView.board[@"width"] unsignedIntegerValue], height = [self.boardView.board[@"height"] unsignedIntegerValue];
    if (PHOrbDimensionsSupported((unsigned)width, (unsigned)height) && path.count && self.boardView.step < path.count) {
        NSUInteger start = [path[0] unsignedIntegerValue], current = [path[self.boardView.step] unsignedIntegerValue];
        self.position.text = [NSString stringWithFormat:@"第 %lu／%lu 步 · 起點：上起 %lu 列、左起 %lu 格\n目前：上起 %lu 列、左起 %lu 格",
            (unsigned long)self.boardView.step, (unsigned long)(path.count - 1), (unsigned long)(start / width + 1),
            (unsigned long)(start % width + 1), (unsigned long)(current / width + 1), (unsigned long)(current % width + 1)];
    } else self.position.text = @"由畫面頂部向下、左邊向右讀取";
    self.boardView.accessibilityLabel = self.position.text;
    [self.boardView setNeedsDisplay];
}
- (void)readBoard { self.action(@"orbsRead", nil); [self refresh]; }
- (void)changeQuality {
    NSInteger index = self.qualityControl.selectedSegmentIndex;
    if (index >= 0 && index < 3 && self.qualityControl.enabled) self.action(@"orbsQuality", @[@"fast", @"balanced", @"thorough"][(NSUInteger)index]);
    [self refresh];
}
- (void)changeGoalMode {
    NSInteger index = self.goalMode.selectedSegmentIndex;
    if (index < 0 || index > 2 || !self.goalMode.enabled) return;
    NSString *mode = @[@"combo", @"manual", @"leaders"][(NSUInteger)index];
    NSArray *goals = self.state[@"goals"] ?: @[];
    if ([mode isEqual:@"manual"] && !goals.count) { [self editGoalSettings]; [self refresh]; return; }
    self.action(@"orbsGoals", @{@"mode":mode, @"goals":goals}); [self refresh];
}
- (void)editGoalSettings {
    if (!self.goalMode.enabled || self.presentedViewController) return;
    NSArray *baseGoals = self.state[@"goalPlan"][@"baseGoals"] ?: self.state[@"goalPlan"][@"goals"];
    NSArray *goals = [self.state[@"goalMode"] isEqual:@"leaders"] ? baseGoals : self.state[@"goals"];
    if (!goals.count) goals = baseGoals;
    if (!goals.count) goals = @[@{@"kind":@"l", @"colors":@63, @"count":@1, @"length":@0}];
    __weak PHOrbController *weakSelf = self;
    PHOrbGoalEditor *editor = [[PHOrbGoalEditor alloc] initWithGoals:goals save:^NSString *(NSArray *saved) {
        PHOrbController *controller = weakSelf;
        if (!controller) return @"頁面已關閉，請重新開啟後儲存。";
        controller.action(@"orbsGoals", @{@"mode":@"manual", @"goals":saved}); [controller refresh];
        NSDictionary *state = controller.state;
        if ([state[@"canSetGoals"] boolValue] && [state[@"goalMode"] isEqual:@"manual"] && [state[@"goals"] isEqual:saved]) return nil;
        NSString *message = state[@"goalMessage"];
        return message.length ? message : @"目標尚未確認儲存，請稍後重試。";
    }];
    editor.modalPresentationStyle = UIModalPresentationPageSheet;
    editor.sheetPresentationController.detents = @[[UISheetPresentationControllerDetent largeDetent]];
    [self presentViewController:editor animated:YES completion:nil];
}
- (void)execute {
    if (![self.state[@"canExecute"] boolValue]) return;
    self.action(@"orbsExecute", self.state[@"generation"]); self.close();
}
- (void)startAutomatic {
    if (![self.state[@"canStart"] boolValue]) return;
    self.action(@"orbsStart", nil); [self refresh];
    if ([self.state[@"automatic"] boolValue]) self.close();
}
- (void)editSkillRules {
    if (!self.editRules.enabled || self.presentedViewController) return;
    __weak PHOrbController *weakSelf = self;
    PHOrbRuleEditor *editor = [[PHOrbRuleEditor alloc] initWithRules:self.state[@"rules"] ?: @[] save:^NSString *(NSArray *rules) {
        PHOrbController *controller = weakSelf;
        if (!controller) return @"頁面已關閉，請重新開啟後儲存。";
        controller.action(@"orbsRules", rules); [controller refresh];
        NSDictionary *state = controller.state;
        if ([state[@"canSetRules"] boolValue] && [state[@"rules"] isEqual:rules]) return nil;
        NSString *message = state[@"ruleMessage"];
        return message.length ? message : @"規則尚未確認儲存，請稍後重試。";
    }];
    editor.modalPresentationStyle = UIModalPresentationPageSheet;
    editor.sheetPresentationController.detents = @[[UISheetPresentationControllerDetent largeDetent]];
    [self presentViewController:editor animated:YES completion:nil];
}
- (void)stop { self.action(@"orbsStop", nil); [self refresh]; }
- (void)editRecoverySettings {
    if (!self.editRecovery.enabled || self.presentedViewController) return;
    __weak PHOrbController *weakSelf = self;
    PHOrbRecoveryEditor *editor = [[PHOrbRecoveryEditor alloc] initWithSettings:self.state[@"recovery"] ?: PHOrbDefaultRecovery()
        save:^NSString *(NSDictionary *settings) {
            PHOrbController *controller = weakSelf; if (!controller) return @"頁面已關閉，請重新開啟後儲存。";
            controller.action(@"orbsRecovery", settings); [controller refresh];
            NSDictionary *state = controller.state;
            if ([state[@"canSetRecovery"] boolValue] && [state[@"recovery"] isEqual:settings]) return nil;
            NSString *message = state[@"recoveryMessage"];
            return message.length ? message : @"心珠設定尚未確認儲存，請稍後重試。";
        }];
    editor.modalPresentationStyle = UIModalPresentationPageSheet;
    editor.sheetPresentationController.detents = @[[UISheetPresentationControllerDetent largeDetent]];
    [self presentViewController:editor animated:YES completion:nil];
}
- (void)editProfileSettings {
    if (!self.editProfiles.enabled || self.presentedViewController) return;
    NSDictionary *state = self.state;
    NSDictionary *settings = @{@"quality":state[@"quality"], @"mode":state[@"goalMode"],
        @"goals":state[@"goals"], @"rules":state[@"rules"], @"recovery":state[@"recovery"] ?: PHOrbDefaultRecovery()};
    __weak PHOrbController *weakSelf = self;
    PHOrbProfileEditor *editor = [[PHOrbProfileEditor alloc] initWithLibrary:state[@"profiles"] ?: PHOrbDefaultProfiles()
        settings:settings formations:self.snapshot()[@"formationLibrary"] ?: @{} context:state[@"profileContext"] ?: @{}
        save:^NSString *(NSDictionary *library, NSDictionary *working) {
            PHOrbController *controller = weakSelf; if (!controller) return @"頁面已關閉，請重新開啟後儲存。";
            controller.action(@"orbsProfiles", @{@"library":library, @"settings":working}); [controller refresh];
            NSDictionary *saved = controller.state;
            NSDictionary *selected = PHOrbProfileNamed(library,library[@"selected"]);
            NSDictionary *wanted = PHOrbCanonicalSettings(selected ? selected[@"settings"] : working);
            NSDictionary *actual = PHOrbCanonicalSettings(@{@"quality":saved[@"quality"] ?: @"", @"mode":saved[@"goalMode"] ?: @"",
                @"goals":saved[@"goals"] ?: @[], @"rules":saved[@"rules"] ?: @[], @"recovery":saved[@"recovery"] ?: PHOrbDefaultRecovery()});
            if ([saved[@"canSetProfiles"] boolValue] && [saved[@"profiles"] isEqual:library] && [actual isEqual:wanted]) return nil;
            NSString *message = saved[@"profileMessage"];
            return message.length ? message : @"策略尚未確認儲存，請稍後重試。";
        }];
    editor.modalPresentationStyle = UIModalPresentationPageSheet;
    editor.sheetPresentationController.detents = @[[UISheetPresentationControllerDetent largeDetent]];
    [self presentViewController:editor animated:YES completion:nil];
}
- (void)closePage { self.close(); }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
@end
