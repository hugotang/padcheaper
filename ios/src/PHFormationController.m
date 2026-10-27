#import "PHFormationController.h"
#import "PHFormationModel.h"
#import <WebKit/WebKit.h>
#include "PHFormationGenerated.h"

@interface PHFormationController () <WKScriptMessageHandler, WKNavigationDelegate>
@property(nonatomic, strong) WKWebView *web;
@property(nonatomic, copy) NSDictionary *team;
@property(nonatomic, copy) NSDictionary *battle;
@property(nonatomic, copy) PHOverlayAction action;
@property(nonatomic, copy) dispatch_block_t close;
@end

@implementation PHFormationController
- (instancetype)initWithTeam:(NSDictionary *)team battle:(NSDictionary *)battle
    action:(PHOverlayAction)action close:(dispatch_block_t)close {
    if ((self = [super init])) { _team = team; _battle = battle; _action = action; _close = close; }
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
        [self send:@"receive" value:initial];
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
    } else if ([action isEqual:@"save"] || [action isEqual:@"close"] || [action isEqual:@"apply"]) {
        id team = [action isEqual:@"apply"] && [value isKindOfClass:NSDictionary.class] ? value[@"team"] : value;
        NSString *reason = nil;
        if (!PHFormationReadTeam(team, NULL, &reason)) { [self send:@"notify" value:reason]; return; }
        if ([action isEqual:@"apply"] && !PHFormationReadApply(value, NULL, NULL, &reason)) {
            [self send:@"notify" value:reason]; return;
        }
        self.team = team;
        self.action(@"formationSave", team);
        if ([action isEqual:@"apply"]) { self.action(@"battleTeamApply", value); [self finish]; }
        else if ([action isEqual:@"close"]) [self finish];
        else [self send:@"notify" value:@"已保留編隊草稿"];
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
