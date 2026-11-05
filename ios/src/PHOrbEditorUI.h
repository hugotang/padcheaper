#pragma once
#import <UIKit/UIKit.h>

// Shared presentation only; editors still own their drafts and save callbacks.
static inline void PHOrbStyleButton(UIButton *button, NSString *title, BOOL primary) {
    UIButtonConfiguration *config = primary ? [UIButtonConfiguration filledButtonConfiguration] :
        [UIButtonConfiguration plainButtonConfiguration];
    config.title = title;
    config.cornerStyle = UIButtonConfigurationCornerStyleMedium;
    if (primary) config.baseForegroundColor = [UIColor colorWithRed:0.07 green:0.14 blue:0.11 alpha:1];
    config.contentInsets = NSDirectionalEdgeInsetsMake(10, 12, 10, 12);
    button.configuration = config;
    button.tintColor = [UIColor colorWithRed:0.53 green:0.89 blue:0.72 alpha:1];
    button.titleLabel.numberOfLines = 0;
    button.titleLabel.lineBreakMode = NSLineBreakByWordWrapping;
    button.contentHorizontalAlignment = primary ? UIControlContentHorizontalAlignmentCenter : UIControlContentHorizontalAlignmentLeading;
}

static inline void PHOrbStyleHeader(UIStackView *header, UILabel *title, UIButton *cancel) {
    header.alignment = UIStackViewAlignmentCenter;
    [title setContentCompressionResistancePriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
    [cancel setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [cancel setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
}

static inline UIStackView *PHOrbSwitchRow(UIView *text, UISwitch *toggle) {
    [text setContentCompressionResistancePriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisHorizontal];
    [toggle setContentCompressionResistancePriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    [toggle setContentHuggingPriority:UILayoutPriorityRequired forAxis:UILayoutConstraintAxisHorizontal];
    UIStackView *row = [[UIStackView alloc] initWithArrangedSubviews:@[text, toggle]];
    row.alignment = UIStackViewAlignmentCenter; row.spacing = 12;
    [row.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    return row;
}

static inline UIStackView *PHOrbEditorSection(NSString *title, UIStackView *content, BOOL collapsible, NSString *identifier) {
    content.axis = UILayoutConstraintAxisVertical; content.spacing = 10;
    UIStackView *card = [UIStackView new]; card.axis = UILayoutConstraintAxisVertical; card.spacing = 10;
    card.backgroundColor = UIColor.secondarySystemBackgroundColor; card.layer.cornerRadius = 12;
    card.layoutMargins = UIEdgeInsetsMake(12, 12, 12, 12); card.layoutMarginsRelativeArrangement = YES;
    if (collapsible) {
        UIButton *disclosure = [UIButton buttonWithType:UIButtonTypeSystem];
        PHOrbStyleButton(disclosure, title, NO);
        UIButtonConfiguration *config = disclosure.configuration;
        config.image = [UIImage systemImageNamed:@"chevron.down"]; config.imagePlacement = NSDirectionalRectEdgeTrailing;
        config.imagePadding = 8; disclosure.configuration = config;
        disclosure.accessibilityIdentifier = identifier;
        disclosure.accessibilityValue = @"已收起";
        [disclosure.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
        content.hidden = YES;
        __weak UIStackView *weakCard = card; __weak UIButton *weakButton = disclosure;
        [disclosure addAction:[UIAction actionWithHandler:^(__kindof UIAction *action) {
            (void)action;
            UIStackView *body = content; UIButton *button = weakButton; UIStackView *container = weakCard;
            [body endEditing:YES]; body.hidden = !body.hidden;
            // Remove folded stacks instead of forcing their fixed-height text
            // fields to zero through UIStackView's required hiding constraint.
            if (body.hidden) { [container removeArrangedSubview:body]; [body removeFromSuperview]; }
            else [container addArrangedSubview:body];
            UIButtonConfiguration *changed = button.configuration;
            changed.image = [UIImage systemImageNamed:body.hidden ? @"chevron.down" : @"chevron.up"];
            button.configuration = changed; button.accessibilityValue = body.hidden ? @"已收起" : @"已展開";
            UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, button);
        }] forControlEvents:UIControlEventTouchUpInside];
        [card addArrangedSubview:disclosure];
    } else {
        UILabel *label = [UILabel new]; label.text = title; label.numberOfLines = 0;
        label.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold]; label.textColor = UIColor.secondaryLabelColor;
        label.accessibilityTraits |= UIAccessibilityTraitHeader;
        [card addArrangedSubview:label];
    }
    if (!collapsible) [card addArrangedSubview:content];
    return card;
}
