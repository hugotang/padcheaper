#import <Foundation/Foundation.h>
#import "PHSaveState.h"
#include <stdio.h>

static NSUInteger failures;
static void Check(BOOL condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

static NSDictionary *Prepare(PHSaveState *state, NSDictionary *banks, BOOL allowed, NSTimeInterval time) {
    [state recordLocalBanks:banks];
    NSDictionary *transaction = [state prepareTransactionWithBanks:banks mask:PHFeatureSkin | PHFeatureBGM
        epoch:1 context:0x1000 request:0x2000 restoreAllowed:allowed time:time];
    [state recordTaskCreated:YES transaction:transaction];
    return transaction;
}

int main(void) {
    @autoreleasepool {
        NSDictionary *selected = @{@"decksb": @[@[@1,@2,@3,@4,@5,@7,@42,@100020001,@9]]};
        NSDictionary *defaults = @{@"decksb": @[@[@1,@2,@3,@4,@5,@7,@42,@0,@0]]};
        PHSaveState *state = [PHSaveState new];
        Check(![state beginSave], "initial save has no older restore");
        NSDictionary *transaction = Prepare(state, selected, YES, 100);
        Check([state consumeForEpoch:1 context:0x1000 request:0x2000 nativeError:0] == nil,
              "native parse alone cannot authorize restore without accepted network response");
        [state completeTransaction:transaction accepted:YES httpStatus:200];
        Check([state consumeForEpoch:2 context:0x1000 request:0x2000 nativeError:0] == nil,
              "different account epoch cannot restore");
        Check([state consumeForEpoch:1 context:0x1001 request:0x2000 nativeError:0] == nil,
              "different native context cannot restore");
        Check([state consumeForEpoch:1 context:0x1000 request:0x2001 nativeError:0] == nil,
              "different native request cannot restore");
        NSDictionary *restorable = [state consumeForEpoch:1 context:0x1000 request:0x2000 nativeError:0];
        Check([restorable[@"banks"] isEqual:selected], "only the matching accepted transaction restores its saved selection");
        Check([state consumeForEpoch:1 context:0x1000 request:0x2000 nativeError:0] == nil, "restore can be consumed only once");
        [state recordRestoreCount:2 reason:@"儲存後：已回蓋 2 欄位"];
        Check([[state snapshot][@"restoredFieldCount"] isEqual:@2] && [[state snapshot][@"phase"] isEqual:@"finished"],
              "completed parse and restore produce useful counters");

        // Regression: a zero-valued new request bypasses rewriting, but it must
        // still invalidate the older request that selected nondefault fields.
        [state beginSave];
        NSDictionary *older = Prepare(state, selected, YES, 110);
        [state completeTransaction:older accepted:YES httpStatus:200];
        Check([state beginSave], "explicit default selection supersedes the old accepted save");
        [state recordLocalBanks:defaults];
        [state recordPassthrough:@"本地選擇已保存；此請求無需預設化"];
        [state completeTransaction:older accepted:YES httpStatus:200];
        Check([state consumeForEpoch:1 context:0x1000 request:0x2000 nativeError:0] == nil,
              "older response cannot restore a nondefault value after an explicit zero save");
        Check([[state snapshot][@"phase"] isEqual:@"unchanged"] && ![[state snapshot][@"pending"] boolValue],
              "stale callbacks cannot replace the newest passthrough state");

        [state beginSave];
        older = Prepare(state, selected, YES, 120);
        [state beginSave];
        [state recordStorageFailure];
        [state completeTransaction:older accepted:YES httpStatus:200];
        Check([state consumeForEpoch:1 context:0x1000 request:0x2000 nativeError:0] == nil &&
              [[state snapshot][@"phase"] isEqual:@"storage-failed"], "snapshot failure cannot revive older selections");

        [state beginSave];
        older = Prepare(state, selected, YES, 130);
        BOOL overlap = [state beginSave];
        transaction = Prepare(state, defaults, !overlap, 131);
        [state completeTransaction:older accepted:NO httpStatus:500];
        Check([[state snapshot][@"phase"] isEqual:@"submitted"], "old failure cannot cancel newer transaction");
        [state completeTransaction:transaction accepted:YES httpStatus:200];
        Check([state consumeForEpoch:1 context:0x1000 request:0x2000 nativeError:0] == nil &&
              [[state snapshot][@"phase"] isEqual:@"awaiting-login"] && [[state snapshot][@"pending"] boolValue],
              "overlapping requests preserve local banks without guessing an immediate restore");

        [state beginSave];
        transaction = Prepare(state, selected, YES, 140);
        [state completeTransaction:transaction accepted:YES httpStatus:200];
        Check([state consumeForEpoch:1 context:0x1000 request:0x2000 nativeError:7] == nil &&
              [[state snapshot][@"phase"] isEqual:@"native-failed"], "native business error refuses restore after HTTP success");

        [state beginSave];
        transaction = Prepare(state, selected, YES, 150);
        [state completeTransaction:transaction accepted:NO httpStatus:200];
        Check(![[state snapshot][@"pending"] boolValue] && [[state snapshot][@"phase"] isEqual:@"network-failed"],
              "HTTP 200 with rejected business response cannot restore");

        [state beginSave];
        [state recordLocalBanks:selected];
        transaction = [state prepareTransactionWithBanks:selected mask:PHFeatureSkin
            epoch:1 context:0x1000 request:0x2000 restoreAllowed:YES time:160];
        [state recordTaskCreated:NO transaction:transaction];
        [state completeTransaction:transaction accepted:YES httpStatus:200];
        Check([[state snapshot][@"phase"] isEqual:@"task-failed"], "task creation failure ignores late completion");

        [state beginSave];
        transaction = Prepare(state, selected, YES, 170);
        [state expireAtTime:230];
        Check([[state snapshot][@"pending"] boolValue], "restore deadline allows the full sixty seconds");
        [state expireAtTime:231];
        [state completeTransaction:transaction accepted:YES httpStatus:200];
        Check(![[state snapshot][@"pending"] boolValue] && [[state snapshot][@"phase"] isEqual:@"expired"],
              "expired transaction cannot be revived by a late callback");

        [state beginSave];
        transaction = Prepare(state, selected, YES, 240);
        [state resetWithBanks:@{} accountReady:NO];
        [state completeTransaction:transaction accepted:YES httpStatus:200];
        Check(![[state snapshot][@"pending"] boolValue] && [[state snapshot][@"primaryDeckCount"] isEqual:@0],
              "account change clears pending restore and visible local counts");
        [state resetWithBanks:selected accountReady:YES];
        Check([[state snapshot][@"primaryDeckCount"] isEqual:@1] && [[state snapshot][@"phase"] isEqual:@"loaded"],
              "login displays only the banks loaded for the current account");
        NSDictionary *snapshot = [state snapshot];
        Check(snapshot[@"banks"] == nil && snapshot[@"epoch"] == nil && snapshot[@"nativeRequest"] == nil,
              "public diagnostics do not expose deck contents or account/native identifiers");
        Check([NSJSONSerialization isValidJSONObject:snapshot], "save status can be persisted as JSON");
        printf("Save/restore state tests: %lu failures\n", (unsigned long)failures);
    }
    return failures ? 1 : 0;
}
