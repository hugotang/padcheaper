#import "PHFormationController.h"
#import "PHFormationModel.h"
#import <WebKit/WebKit.h>
#include "PHFormationGenerated.h"

@interface PHFormationController () <WKScriptMessageHandler, WKNavigationDelegate>
@property(nonatomic, strong) WKWebView *web;
@property(nonatomic, copy) NSDictionary *team;
@property(nonatomic, copy) NSDictionary *library;
@property(nonatomic, copy) NSDictionary *battle;
@property(nonatomic, copy) PHOverlaySnapshot snapshot;
@property(nonatomic, copy) PHOverlayAction action;
@property(nonatomic, copy) dispatch_block_t close;
@end

@implementation PHFormationController
- (instancetype)initWithTeam:(NSDictionary *)team library:(NSDictionary *)library battle:(NSDictionary *)battle
    snapshot:(PHOverlaySnapshot)snapshot action:(PHOverlayAction)action close:(dispatch_block_t)close {
    if ((self = [super init])) {
        _library = PHFormationReadLibrary(library, NULL, NULL) ? library : PHFormationLibraryFromTeam(team);
        NSDictionary *selected = nil;
        PHFormationReadLibrary(_library, &selected, NULL);
        _team = selected;
        _battle = battle; _snapshot = snapshot; _action = action; _close = close;
    }
    return self;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.055 green:0.086 blue:0.106 alpha:1];
    self.view.accessibilityIdentifier = @"padcheaper.formation";
    WKWebViewConfiguration *configuration = [WKWebViewConfiguration new];
    [configuration.userContentController addScriptMessageHandler:self name:@"formation"];
    self.web = [[WKWebView alloc] initWithFrame:CGRectZero configuration:configuration];
    self.web.navigationDelegate = self;
    self.web.accessibilityIdentifier = @"padcheaper.formationWeb";
    self.web.opaque = NO;
    self.web.backgroundColor = self.view.backgroundColor;
    self.web.scrollView.backgroundColor = self.view.backgroundColor;
    self.web.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:self.web];
    [NSLayoutConstraint activateConstraints:@[
        [self.web.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor],
        [self.web.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [self.web.leadingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.leadingAnchor],
        [self.web.trailingAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.trailingAnchor]
    ]];
    [self.web loadHTMLString:[NSString stringWithUTF8String:(const char *)PHFormationHTML] baseURL:nil];
}
- (void)send:(NSString *)method value:(id)value {
    NSData *data = [NSJSONSerialization dataWithJSONObject:@[value ?: NSNull.null] options:0 error:NULL];
    if (!data) return;
    NSString *argument = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    NSString *script = [NSString stringWithFormat:@"window.PHFormationUI && window.PHFormationUI.%@(%@[0])", method, argument];
    [self.web evaluateJavaScript:script completionHandler:nil];
}
- (void)updateBattle:(NSDictionary *)battle {
    if ([self.battle isEqual:battle]) return;
    self.battle = battle;
    [self send:@"setState" value:battle ?: @{}];
}
- (void)finish {
    [self.web endEditing:YES];
    // Break WKUserContentController's strong reference to its message handler.
    [self.web.configuration.userContentController removeScriptMessageHandlerForName:@"formation"];
    if (self.close) self.close();
}
- (void)userContentController:(WKUserContentController *)controller didReceiveScriptMessage:(WKScriptMessage *)message {
    if (message.webView != self.web || !message.frameInfo.isMainFrame || ![message.body isKindOfClass:NSDictionary.class]) return;
    NSString *action = message.body[@"action"];
    id value = message.body[@"value"];
    if (![action isKindOfClass:NSString.class]) return;
    if ([action isEqual:@"ready"]) {
        NSMutableDictionary *initial = [@{@"battle": self.battle ?: @{}} mutableCopy];
        if (self.team && PHFormationReadTeam(self.team, NULL, NULL)) initial[@"team"] = self.team;
        initial[@"library"] = self.library;
        NSDictionary *current = self.snapshot ? self.snapshot() : nil;
        initial[@"stored"] = current[@"formationLibraryStored"] ?: @NO;
        initial[@"recent"] = [NSUserDefaults.standardUserDefaults arrayForKey:@"PADCheaper.formationRecent"] ?: @[];
        [self send:@"receive" value:initial];
    } else if ([action isEqual:@"persist"] && [value isKindOfClass:NSDictionary.class]) {
        NSString *requestID = value[@"requestID"];
        if (![requestID isKindOfClass:NSString.class] || !requestID.length || requestID.length > 80) return;
        NSDictionary *library = value[@"library"];
        NSDictionary *selected = nil;
        NSString *reason = nil;
        if (!PHFormationReadLibrary(library, &selected, &reason)) {
            [self send:@"saveResult" value:@{@"requestID":requestID, @"ok":@NO, @"message":reason}]; return;
        }
        self.action(@"formationLibrarySave", value);
        NSDictionary *state = self.snapshot ? self.snapshot() : nil;
        NSDictionary *receipt = state[@"formationSaveResult"];
        if (![receipt[@"requestID"] isEqual:requestID]) {
            [self send:@"saveResult" value:@{@"requestID":requestID, @"ok":@NO,
                @"message":@"未收到儲存確認；草稿仍保留在編輯器"}]; return;
        }
        if ([receipt[@"ok"] isEqual:@YES]) { self.library = library; self.team = selected; }
        NSMutableDictionary *result = [receipt mutableCopy];
        result[@"battle"] = state[@"battleState"] ?: @{};
        [self send:@"saveResult" value:result];
    } else if ([action isEqual:@"recent"] && [value isKindOfClass:NSArray.class] && [value count] <= 24) {
        NSMutableArray *recent = [NSMutableArray array];
        for (id number in value) {
            if (![number isKindOfClass:NSNumber.class] || [number doubleValue] != [number unsignedIntValue] ||
                [number unsignedIntValue] < 1 || [number unsignedIntValue] > 65435) return;
            if (![recent containsObject:number]) [recent addObject:number];
        }
        [NSUserDefaults.standardUserDefaults setObject:recent forKey:@"PADCheaper.formationRecent"];
    } else if ([action isEqual:@"paste"]) {
        [self send:@"importText" value:UIPasteboard.generalPasteboard.string ?: @""];
    } else if ([action isEqual:@"copy"] && [value isKindOfClass:NSString.class] && [value length] <= 65536 &&
               [value hasPrefix:@"https://mapaler.github.io/PADDashFormation/solo.html?"]) {
        UIPasteboard.generalPasteboard.string = value;
        [self send:@"notify" value:@"已複製，可在 PADDashFormation 開啟此隊伍"];
    } else if ([action isEqual:@"reference"]) {
        [UIApplication.sharedApplication openURL:[NSURL URLWithString:@"https://mapaler.github.io/PADDashFormation/solo.html?l=zh-hant&s=ja"] options:@{} completionHandler:nil];
    } else if ([action isEqual:@"restore"] && PHFormationReadRestoreSlot(value, NULL)) {
        self.action(@"battleRestoreSlot", value);
        [self finish];
    } else if ([action isEqual:@"dismiss"]) {
        [self finish];
    } else if ([action isEqual:@"applySaved"]) {
        NSString *reason = nil;
        if (!PHFormationReadApply(value, NULL, NULL, &reason)) {
            [self send:@"notify" value:reason]; return;
        }
        if (![value[@"team"] isEqual:self.team]) { [self send:@"notify" value:@"隊伍有新變更，請先儲存再套用"]; return; }
        self.action(@"battleTeamApply", value);
        [self finish];
    }
}
- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)navigationAction
    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    // No external page gets the native command bridge. The reference button
    // opens Safari through its explicit native action.
    BOOL allowed = [navigationAction.request.URL.absoluteString isEqualToString:@"about:blank"];
    decisionHandler(allowed ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
}
@end
