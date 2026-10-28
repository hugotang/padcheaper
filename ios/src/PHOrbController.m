#import "PHOrbController.h"
#include "PHOrbModel.h"
#include <math.h>

@interface PHOrbBoardView : UIView
@property(nonatomic, copy) NSDictionary *board;
@property(nonatomic, copy) NSArray<NSNumber *> *path;
@property(nonatomic) NSUInteger step;
@end

@implementation PHOrbBoardView
- (void)drawRect:(CGRect)rect {
    NSUInteger width = [self.board[@"width"] unsignedIntegerValue], height = [self.board[@"height"] unsignedIntegerValue];
    NSArray *source = self.board[@"types"], *enhanced = self.board[@"enhanced"];
    const NSUInteger cells = width * height;
    if (!PHOrbDimensionsSupported((unsigned)width, (unsigned)height) || source.count != cells || enhanced.count != cells) {
        NSString *text = @"讀取盤面後顯示預覽";
        [text drawAtPoint:CGPointMake(16, 24) withAttributes:@{NSFontAttributeName:[UIFont systemFontOfSize:16],
            NSForegroundColorAttributeName:UIColor.secondaryLabelColor}];
        return;
    }
    NSMutableArray *types = source.mutableCopy, *plus = enhanced.mutableCopy;
    NSUInteger through = MIN(self.step, self.path.count ? self.path.count - 1 : 0);
    for (NSUInteger i = 1; i <= through; ++i) {
        NSUInteger a = self.path[i - 1].unsignedIntegerValue, b = self.path[i].unsignedIntegerValue;
        if (a >= cells || b >= cells) return;
        [types exchangeObjectAtIndex:a withObjectAtIndex:b]; [plus exchangeObjectAtIndex:a withObjectAtIndex:b];
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
@property(nonatomic, strong) NSLayoutConstraint *boardAspect;
@property(nonatomic) NSUInteger boardWidth, boardHeight;
@property(nonatomic, strong) UISlider *slider;
@property(nonatomic, strong) UIButton *readButton;
@property(nonatomic, strong) UIButton *executeButton;
@property(nonatomic, strong) UIButton *stopButton;
@property(nonatomic, strong) NSTimer *timer;
@property(nonatomic) NSUInteger generation;
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
    subtitle.text = @"單人 · 6×5／7×6／5×4 · 每次一條路徑";
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
    UILabel *qualityTitle = [self label:15 weight:UIFontWeightMedium]; qualityTitle.text = @"求路品質";
    UIStackView *qualityRow = [[UIStackView alloc] initWithArrangedSubviews:@[qualityTitle, self.stopButton]];
    qualityRow.axis = UILayoutConstraintAxisHorizontal; qualityRow.spacing = 12;
    self.qualityControl = [[UISegmentedControl alloc] initWithItems:@[@"快速", @"均衡", @"深入"]];
    self.qualityControl.accessibilityIdentifier = @"padcheaper.orbsQuality";
    self.qualityControl.accessibilityLabel = @"求路品質";
    [self.qualityControl.heightAnchor constraintGreaterThanOrEqualToConstant:36].active = YES;
    [self.qualityControl addTarget:self action:@selector(changeQuality) forControlEvents:UIControlEventValueChanged];
    self.qualityDescription = [self label:13 weight:UIFontWeightRegular];
    self.qualityDescription.accessibilityIdentifier = @"padcheaper.orbsQualityDescription";
    self.availability = [self label:13 weight:UIFontWeightRegular];
    self.availability.accessibilityIdentifier = @"padcheaper.orbsAvailability";
    UILabel *note = [self label:13 weight:UIFontWeightRegular]; note.textColor = UIColor.secondaryLabelColor;
    note.text = @"先核對珠色與起點。拖動滑桿可看每一步；執行時回到遊戲，浮動按鈕會變成「停止」。\n\n支援六種基本珠及強化珠；多人、轉轉、不可消與特殊消除規則會停用。預計 Combo 不含天降。";
    UIButton *close = [self button:@"返回遊戲" selector:@selector(closePage) identifier:@"padcheaper.orbsClose"];
    [close.widthAnchor constraintEqualToConstant:100].active = YES;
    UIStackView *header = [[UIStackView alloc] initWithArrangedSubviews:@[title, close]];
    header.axis = UILayoutConstraintAxisHorizontal; header.spacing = 12; header.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:header];
    UIStackView *actions = [[UIStackView alloc] initWithArrangedSubviews:@[self.readButton, self.executeButton]];
    actions.axis = UILayoutConstraintAxisHorizontal; actions.spacing = 10;
    actions.distribution = UIStackViewDistributionFillEqually; actions.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:actions];
    UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[subtitle, qualityRow, self.qualityControl,
        self.qualityDescription, self.status, self.boardView, self.slider, self.position, self.availability, note]];
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
    NSArray *qualities = @[@"fast", @"balanced", @"thorough"];
    NSUInteger quality = [qualities indexOfObject:state[@"quality"] ?: @"balanced"];
    if (quality == NSNotFound) quality = 1;
    self.qualityControl.selectedSegmentIndex = (NSInteger)quality;
    self.qualityControl.enabled = [state[@"canSetQuality"] boolValue] && ![state[@"busy"] boolValue] && ![state[@"inputFault"] boolValue];
    NSString *qualityMessage = state[@"qualityMessage"];
    self.qualityDescription.text = qualityMessage.length ? qualityMessage :
        @[@"較快取得路徑，適合一般回合。", @"兼顧求路速度與搜尋範圍。", @"搜尋更多候選路徑，需要等候較久；不保證 Combo 一定較高。"][quality];
    self.qualityDescription.textColor = qualityMessage.length ? UIColor.systemRedColor : UIColor.secondaryLabelColor;
    self.availability.text = [state[@"inputFault"] boolValue] ? @"輸入恢復未通過，請完整重啟遊戲後再試。" :
        ![state[@"canRead"] boolValue] && ![state[@"busy"] boolValue] ? @"等待遊戲校驗與登入後即可讀盤。" :
        ![state[@"enabled"] boolValue] ? @"預覽可使用；執行需要先開啟 PC 面板的功能總開關。" :
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
- (void)execute { self.action(@"orbsExecute", self.state[@"generation"]); self.close(); }
- (void)stop { self.action(@"orbsStop", nil); [self refresh]; }
- (void)closePage { self.close(); }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskAll; }
@end
