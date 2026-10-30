#import "PHDeckPersistence.h"
#import "PHMemory.h"
#include "PHBadgeModel.h"
#include "PHTargetGenerated.h"
#include <stdio.h>
#include <string.h>

static const uintptr_t Header = 0x100000000, Context = 0x200000000, Request = 0x300000000, Records = 0x400000000;
static uintptr_t context, request, records;
static uint8_t primary, secondary, editing, flags, native[12 + 32 * 96], stored[2][32 * 96];
static uint16_t maximum;
static uint32_t identifier;
static BOOL failWrite, switchRequest;
static unsigned failures, writes, observed;
static void Check(BOOL ok, const char *why) { if (!ok) { fprintf(stderr,"FAIL: %s\n",why); ++failures; } }
typedef struct { uintptr_t address; void *bytes; size_t size; } Mapping;
static void *Resolve(uintptr_t address, size_t length) {
    Mapping map[] = {
        {Header + PHSaveRVA + PHPrimaryCountOffset,&primary,1}, {Header + PHSaveRVA + PHSecondaryCountOffset,&secondary,1},
        {Header + PHSaveRVA + PHMaxDecksOffset,&maximum,2}, {Header + PHSaveRVA + PHEditDirtyOffset,&editing,1},
        {Header + PHSaveRVA + PHAPIContextPointerOffset,&context,8}, {Context + PHAPIActiveRequestOffset,&request,8},
        {Request+8,&identifier,4}, {Request+32,&flags,1}, {Request+40,&records,8}, {Records,native,sizeof(native)},
        {Header + PHSaveRVA + PHPrimaryDecksOffset,stored[0],sizeof(stored[0])},
        {Header + PHSaveRVA + PHSecondaryDecksOffset,stored[1],16*96},
    };
    for (NSUInteger i = 0; i < sizeof(map)/sizeof(map[0]); ++i)
        if (address >= map[i].address && address - map[i].address <= map[i].size && length <= map[i].size - (address-map[i].address))
            return (uint8_t *)map[i].bytes + (address-map[i].address);
    return NULL;
}
BOOL PHReadMemory(uintptr_t address, void *output, size_t length) {
    if (address == Context + PHAPIActiveRequestOffset && switchRequest && ++observed == 2) return NO;
    void *source = Resolve(address,length); if (!source) return NO;
    memcpy(output,source,length); return YES;
}
BOOL PHWriteData(uintptr_t address, const void *input, size_t length) {
    if (failWrite) return NO;
    void *destination = Resolve(address,length); if (!destination) return NO;
    memcpy(destination,input,length); ++writes; return YES;
}
static NSArray *Record(unsigned first, unsigned badge) { return @[@(first),@2,@3,@4,@5,@(badge),@77,@900008,@12]; }
static void Encode(uint8_t *bytes, NSArray *record, unsigned index) {
    memset(bytes,0,96);
    for (unsigned i = 0; i < 5; ++i) { uint32_t value = [record[i] unsignedIntValue]; memcpy(bytes + i*4,&value,4); }
    uint32_t extra = [record[6] unsignedIntValue], packed = [record[7] unsignedIntValue];
    uint16_t bgm[] = {packed%100000,packed/100000}, skin = [record[8] unsignedShortValue];
    memcpy(bytes+20,&extra,4); memcpy(bytes+24,bgm,4); memcpy(bytes+28,&skin,2);
    bytes[95] = PHBadgeEncode([record[5] unsignedCharValue],index);
}
static NSArray *Defaulted(NSArray *record) { NSMutableArray *copy = [record mutableCopy]; copy[5]=copy[7]=copy[8]=@0; return copy; }
static NSDictionary *Seed(void) {
    primary=1; secondary=1; maximum=2; editing=0; flags=1; identifier=58;
    context=Context; request=Request; records=Records; writes=observed=0; failWrite=switchRequest=NO;
    NSArray *a=Record(1,12), *b=Record(8,34);
    Encode(native+12,a,0); Encode(native+12+96,b,1);
    Encode(stored[0],Defaulted(a),0); Encode(stored[1],Defaulted(b),0);
    return @{@"decksb":@[a],@"decksbs":@[b]};
}
static NSDictionary *Drain(PHDeckRestorer *r, NSUInteger epoch, PHFeatureMask enabled, PHFeatureMask allowed, BOOL ready) {
    return [r drainHeader:Header epoch:epoch enabled:enabled allowed:allowed badgeReady:ready
        canUseBadge:^BOOL(uint8_t value) { return value == 12 || value == 34; }];
}
int main(void) { @autoreleasepool {
    NSDictionary *saved=Seed(); NSMutableDictionary *wire=[saved mutableCopy];
    NSMutableArray *a=[saved[@"decksb"][0] mutableCopy], *b=[saved[@"decksbs"][0] mutableCopy]; a[5]=b[5]=@0;
    wire[@"decksb"]=@[a]; wire[@"decksbs"]=@[b]; PHDeckRequest observation={0};
    Check([PHReadRequestedDecks(Header,wire,&observation) isEqual:saved] && observation.request==Request && observation.records==Records,
        "wire zero recovers the exact queued request with combined secondary badge index");
    identifier=122; Check(!PHReadRequestedDecks(Header,wire,NULL),"recv_team 122 must not correlate as save_decks 58"); identifier=58;
    a[0]=@999; Check(!PHReadRequestedDecks(Header,wire,NULL),"stale request with different cards cannot supply badge"); a[0]=@1;
    a[7]=@1; Check(!PHReadRequestedDecks(Header,wire,NULL),"all nonbadge serialized fields must agree"); a[7]=@900008;
    a[5]=@99; Check(!PHReadRequestedDecks(Header,wire,NULL),"nonzero wire badge cannot disagree with native request"); a[5]=@0;
    switchRequest=YES; Check(!PHReadRequestedDecks(Header,wire,NULL),"request must survive final pointer recheck"); switchRequest=NO;
    NSDictionary *merged=PHMergeBanks(saved,wire,PHFeatureBGM|PHFeatureSkin);
    Check([merged[@"decksb"][0][5] isEqual:@12],"unreliable badge observation preserves previous selection");
    native[12+95]=PHBadgeEncode(0,0);
    NSDictionary *explicitZero=PHReadRequestedDecks(Header,wire,NULL);
    Check([explicitZero[@"decksb"][0][5] isEqual:@0] &&
        [PHMergeBanks(saved,explicitZero,7)[@"decksb"][0][5] isEqual:@0],"verified explicit default removes previous badge");
    saved=Seed();
    NSDictionary *account=@{@"schema":@1,@"target":@"jp.gungho.pad-ios-23.6.0-106878",@"player":@42,@"banks":saved};
    NSData *json=[NSJSONSerialization dataWithJSONObject:account options:0 error:NULL];
    NSDictionary *loaded=PHDeckAccountBanks([NSJSONSerialization JSONObjectWithData:json options:0 error:NULL],42);
    Check([loaded isEqual:saved] && !PHDeckAccountBanks(account,43).count,"restart restores only the same account snapshot");
    for (id wrong in @[@YES,@42.5,@4294967338ULL,@"42",NSNull.null]) {
        NSMutableDictionary *bad=[account mutableCopy]; bad[@"player"]=wrong;
        Check(!PHDeckAccountBanks(bad,42).count,"account identity cannot be coerced, rounded or wrapped");
    }
    PHDeckRestorer *r=[PHDeckRestorer new]; [r enqueue:loaded mask:7 epoch:1 source:@"登入後"];
    NSDictionary *first=Drain(r,1,7,PHFeatureSkin|PHFeatureBGM,NO);
    Check([first[@"restored"] unsignedIntValue]==4 && [[r snapshot][@"pending"] unsignedIntValue]==2,
        "login restores cosmetics and retains badges until resources are ready");
    Check([Drain(r,1,7,7,YES)[@"restored"] unsignedIntValue]==2 && ![[r snapshot][@"pending"] unsignedIntValue] &&
        PHBadgeEncode(stored[0][95],0)==12 && PHBadgeEncode(stored[1][95],0)==34,
        "both banks restore badges with bank-local encoding after delayed readiness");
    unsigned before=writes; Drain(r,1,7,7,YES); Check(writes==before,"completed restore is not reapplied each frame");
    saved=Seed(); [r enqueue:saved mask:7 epoch:2 source:@"儲存後"]; editing=1;
    Check(!Drain(r,2,7,7,YES)&&!writes,"editing delays restore without discarding it"); editing=0; failWrite=YES;
    Drain(r,2,7,7,YES); Check([[r snapshot][@"pending"] unsignedIntValue]==2,"write failures remain retryable");
    failWrite=NO; Drain(r,2,7,7,YES); Check(writes==6,"all failed fields retry once writes recover");
    saved=Seed(); [r enqueue:saved mask:7 epoch:3 source:@"登入後"];
    stored[0][0]=99; Drain(r,3,7,7,YES);
    Check(PHBadgeEncode(stored[0][95],0)==0 && PHBadgeEncode(stored[1][95],0)==34,"different team identity prevents stale restore for that bank");
    saved=Seed(); [r enqueue:saved mask:7 epoch:4 source:@"登入後"]; stored[0][95]=PHBadgeEncode(55,0); Drain(r,4,7,7,YES);
    Check(PHBadgeEncode(stored[0][95],0)==55,"later nondefault native choice is preserved");
    saved=Seed(); [r enqueue:saved mask:7 epoch:5 source:@"登入後"]; Drain(r,6,7,7,YES);
    Check(!writes&&![[r snapshot][@"pending"] unsignedIntValue],"account epoch change cancels stale restoration");
    [r enqueue:saved mask:7 epoch:7 source:@"登入後"]; Drain(r,7,0,0,NO); Drain(r,7,7,7,YES);
    Check(!writes&&![[r snapshot][@"pending"] unsignedIntValue],"explicit disable cancels pending selection");
    [r enqueue:saved mask:7 epoch:8 source:@"登入後"]; [r cancel]; Drain(r,8,7,7,YES);
    Check(!writes,"new save including an explicit default cancels older delayed restore");
    printf("Deck persistence tests: %u failures\n",failures);
} return failures?1:0; }
