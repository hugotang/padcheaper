#import <Foundation/Foundation.h>
#import "PHCodec.h"
#include <stdio.h>

static NSUInteger failures;

static void Check(BOOL condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static NSData *Body(NSString *text) {
    return [text dataUsingEncoding:NSUTF8StringEncoding];
}

static NSDictionary *DecodeBank(NSData *body, NSString *bank) {
    NSString *text = [[NSString alloc] initWithData:body encoding:NSUTF8StringEncoding];
    for (NSString *part in [text componentsSeparatedByString:@"&"]) {
        NSString *prefix = [bank stringByAppendingString:@"="];
        if ([part hasPrefix:prefix]) {
            return [NSJSONSerialization JSONObjectWithData:Body([part substringFromIndex:prefix.length])
                                                    options:0 error:NULL];
        }
    }
    return nil;
}

int main(void) {
    @autoreleasepool {
        NSString *primary = @"decksb={\"fmt\":1,\"decks\":[[101,102,103,104,105,7,42,100020001,9]],\"unknown\":3}";
        NSString *secondary = @"decksbs={\"fmt\":1,\"decks\":[[201,202,203,204,205,8,43,200030002,10]]}";
        NSData *input = Body([NSString stringWithFormat:@"opaque=%%2B%%2f+unchanged&%@&%@&tail=keep", primary, secondary]);
        PHRewriteResult *all = PHRewriteDeckBody(input, PHFeatureSkin | PHFeatureBGM | PHFeatureBadge, NULL);
        Check(all != nil && all.changed, "both banks rewrite");
        Check([DecodeBank(all.body, @"decksb")[@"decks"][0]
                isEqual:@[@101,@102,@103,@104,@105,@0,@42,@0,@0]], "primary fields preserved/defaulted");
        Check([DecodeBank(all.body, @"decksbs")[@"decks"][0]
                isEqual:@[@201,@202,@203,@204,@205,@0,@43,@0,@0]], "secondary fields preserved/defaulted");
        Check([DecodeBank(all.body, @"decksb")[@"unknown"] isEqual:@3], "unknown JSON fields preserved");
        NSString *output = [[NSString alloc] initWithData:all.body encoding:NSUTF8StringEncoding];
        Check([output hasPrefix:@"opaque=%2B%2f+unchanged&"] && [output hasSuffix:@"&tail=keep"],
              "unknown form bytes preserved");
        Check([all.banks[@"decksb"][0][5] isEqual:@7], "original selection retained");

        PHRewriteResult *skin = PHRewriteDeckBody(input, PHFeatureSkin, NULL);
        NSArray *skinDeck = DecodeBank(skin.body, @"decksb")[@"decks"][0];
        Check([skinDeck[5] isEqual:@7] && [skinDeck[7] isEqual:@100020001] && [skinDeck[8] isEqual:@0],
              "skin toggle independent from BGM/badge");
        PHRewriteResult *off = PHRewriteDeckBody(input, 0, NULL);
        Check(!off.changed && [off.body isEqualToData:input], "disabled mode byte-for-byte passthrough");

        NSArray<NSString *> *invalid = @[
            @"decksb={\"fmt\":2,\"decks\":[]}",
            @"decksb={\"fmt\":true,\"decks\":[]}",
            @"decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,6,7,8]]}",
            @"decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,6,7,8,65536]]}",
            @"decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,6,7,8,-1]]}",
            @"decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,6,7,8,1.5]]}",
            @"decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,6,7,99999,8]]}",
            @"decksbs={\"fmt\":1,\"decks\":[]}",
            @"decksb=%7B%22fmt%22%3A1%7D",
            @"decksb={\"fmt\":1,\"decks\":[]} &decksb={\"fmt\":1,\"decks\":[]}",
            @"decksb={\"fmt\":1,\"decks\":[]}&decksbs=broken"
        ];
        for (NSString *text in invalid) {
            Check(PHRewriteDeckBody(Body(text), PHFeatureSkin, NULL) == nil,
                  "invalid request rejected atomically");
        }
        NSMutableArray *many = [NSMutableArray array];
        for (NSUInteger i = 0; i < 33; i++) [many addObject:@[@1,@2,@3,@4,@5,@0,@0,@0,@0]];
        NSData *json = [NSJSONSerialization dataWithJSONObject:@{@"fmt":@1,@"decks":many} options:0 error:NULL];
        NSString *large = [@"decksb=" stringByAppendingString:[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding]];
        Check(PHRewriteDeckBody(Body(large), PHFeatureSkin, NULL) == nil, "capacity bound");

        NSDictionary *previous = @{@"decksb": @[@[@1,@2,@3,@4,@5,@7,@42,@100020001,@9]]};
        NSDictionary *observed = @{@"decksb": @[@[@1,@2,@3,@4,@5,@0,@42,@0,@0]]};
        NSArray *merged = PHMergeBanks(previous, observed, PHFeatureSkin)[@"decksb"][0];
        Check([merged[5] isEqual:@7] && [merged[7] isEqual:@100020001] && [merged[8] isEqual:@0],
              "explicit zero selection recorded without destroying disabled fields");
        observed = @{@"decksb": @[@[@9,@2,@3,@4,@5,@0,@42,@0,@0]]};
        merged = PHMergeBanks(previous, observed, PHFeatureSkin)[@"decksb"][0];
        Check([merged[5] isEqual:@0] && [merged[7] isEqual:@0], "changed team does not inherit old selections");
        // Disabled Skin/BGM: only unowned selections left in native memory are
        // defaulted; owned native choices and other fields stay byte-identical.
        NSData *cosmetic = Body(@"a=1&decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,7,42,1001310012,9],"
                                 "[1,2,3,4,5,7,42,10011,3]]}&z=2");
        NSSet *ownedSkins = [NSSet setWithArray:@[@3]], *ownedMusic = [NSSet setWithArray:@[@10011, @10012]];
        PHCosmeticOwnership native = ^BOOL(PHFeatureMask feature, uint16_t identifier) {
            return [(feature == PHFeatureSkin ? ownedSkins : ownedMusic) containsObject:@(identifier)];
        };
        PHRewriteResult *guardedSkin = PHRewriteDeckBodyGuarded(cosmetic, 0, PHFeatureSkin, native, NULL);
        NSArray *guardedDecks = DecodeBank(guardedSkin.body, @"decksb")[@"decks"];
        Check(guardedSkin.changed && [guardedDecks[0] isEqual:@[@1,@2,@3,@4,@5,@7,@42,@1001310012,@0]] &&
              [guardedDecks[1] isEqual:@[@1,@2,@3,@4,@5,@7,@42,@10011,@3]],
              "guard defaults unowned skin and keeps owned skin");
        Check([guardedSkin.banks[@"decksb"][0][8] isEqual:@9], "guard keeps observed selection for local merge");
        PHRewriteResult *guardedMusic = PHRewriteDeckBodyGuarded(cosmetic, 0, PHFeatureBGM, native, NULL);
        guardedDecks = DecodeBank(guardedMusic.body, @"decksb")[@"decks"];
        Check([guardedDecks[0][7] isEqual:@0] && [guardedDecks[0][8] isEqual:@9] &&
              [guardedDecks[1][7] isEqual:@10011], "packed BGM defaults when either half is unowned");
        NSData *ownedOnly = Body(@"decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,7,42,1001210011,3]]}");
        PHRewriteResult *untouched = PHRewriteDeckBodyGuarded(ownedOnly, 0, PHFeatureSkin | PHFeatureBGM, native, NULL);
        Check(!untouched.changed && [untouched.body isEqualToData:ownedOnly], "owned selections pass byte-for-byte");
        PHRewriteResult *unknown = PHRewriteDeckBodyGuarded(ownedOnly, 0, PHFeatureSkin | PHFeatureBGM, nil, NULL);
        Check([DecodeBank(unknown.body, @"decksb")[@"decks"][0] isEqual:@[@1,@2,@3,@4,@5,@7,@42,@0,@0]],
              "unreadable ownership fails closed");
        PHRewriteResult *badgeOnly = PHRewriteDeckBodyGuarded(ownedOnly, 0, PHFeatureBadge, nil, NULL);
        Check(!badgeOnly.changed, "guard ignores badge");
        PHRewriteResult *activeSkin = PHRewriteDeckBodyGuarded(cosmetic, PHFeatureSkin, PHFeatureSkin | PHFeatureBGM, native, NULL);
        guardedDecks = DecodeBank(activeSkin.body, @"decksb")[@"decks"];
        Check([guardedDecks[1][8] isEqual:@0] && [guardedDecks[0][7] isEqual:@0] && [guardedDecks[1][7] isEqual:@10011],
              "active mask still defaults owned values while guard covers the other feature");
        NSData *wrongBank = Body(@"decksb={\"fmt\":1,\"decks\":[[1,2,3,4,5,7,42,3,10011]]}");
        Check([DecodeBank(PHRewriteDeckBodyGuarded(wrongBank, 0, PHFeatureSkin | PHFeatureBGM,
                ^BOOL(PHFeatureMask f, uint16_t i) { (void)f; (void)i; return YES; }, NULL).body, @"decksb")[@"decks"][0]
                isEqual:@[@1,@2,@3,@4,@5,@7,@42,@3,@10011]], "ownership callback decides each identifier");
        printf("Codec tests: %lu failures\n", (unsigned long)failures);
    }
    return failures ? 1 : 0;
}
