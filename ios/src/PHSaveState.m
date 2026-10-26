#import "PHSaveState.h"

@implementation PHSaveState {
    NSMutableDictionary *_pending;
    NSUInteger _sequence;
    NSUInteger _localSaveCount;
    NSUInteger _primaryDeckCount;
    NSUInteger _secondaryDeckCount;
    NSUInteger _submittedCount;
    NSUInteger _acceptedCount;
    NSUInteger _failedCount;
    NSUInteger _supersededCount;
    NSUInteger _restoredFieldCount;
    NSString *_phase;
    NSString *_message;
    NSString *_restoreMessage;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _phase = @"idle";
        _message = @"尚未儲存隊伍";
        _restoreMessage = @"尚未觀察回蓋";
    }
    return self;
}

- (BOOL)beginSave {
    @synchronized (self) {
        BOOL superseded = _pending != nil;
        if (superseded) _supersededCount++;
        _pending = nil;
        _sequence++;
        _phase = @"saving";
        _message = superseded ? @"新儲存已取消舊回蓋" : @"正在保存本地選擇";
        return superseded;
    }
}

- (void)recordLocalBanks:(NSDictionary *)banks {
    @synchronized (self) {
        _localSaveCount++;
        _primaryDeckCount = [banks[@"decksb"] count];
        _secondaryDeckCount = [banks[@"decksbs"] count];
        _phase = @"local-saved";
        _message = @"本地選擇已原子保存";
    }
}

- (void)recordPassthrough:(NSString *)reason {
    @synchronized (self) {
        _pending = nil;
        _phase = @"unchanged";
        _message = [reason copy];
    }
}

- (void)recordStorageFailure {
    @synchronized (self) {
        _pending = nil;
        _phase = @"storage-failed";
        _message = @"本地保存失敗；此請求保持原樣，舊回蓋已取消";
    }
}

- (NSDictionary *)prepareTransactionWithBanks:(NSDictionary *)banks
                                        mask:(PHFeatureMask)mask
                                       epoch:(NSUInteger)epoch
                                     context:(uintptr_t)context
                                     request:(uintptr_t)request
                              restoreAllowed:(BOOL)restoreAllowed
                                        time:(NSTimeInterval)time {
    @synchronized (self) {
        _pending = [@{@"banks": banks, @"mask": @(mask), @"epoch": @(epoch),
                     @"sequence": @(_sequence), @"time": @(time),
                     @"nativeContext": @(context), @"nativeRequest": @(request),
                     @"restoreAllowed": @(restoreAllowed)} mutableCopy];
        _phase = @"prepared";
        _message = @"已保存選擇，正在建立儲存請求";
        return _pending;
    }
}

- (void)recordTaskCreated:(BOOL)created transaction:(NSDictionary *)transaction {
    @synchronized (self) {
        if (_pending != transaction) return;
        if (!created) {
            _pending = nil;
            _phase = @"task-failed";
            _message = @"請求建立失敗；本地選擇仍保留";
            _failedCount++;
            return;
        }
        _submittedCount++;
        _phase = @"submitted";
        _message = @"已送出預設化請求，等待伺服器回應";
    }
}

- (void)completeTransaction:(NSDictionary *)transaction
                   accepted:(BOOL)accepted
                 httpStatus:(NSInteger)httpStatus {
    @synchronized (self) {
        if (_pending != transaction) return;
        if (!accepted) {
            _pending = nil;
            _failedCount++;
            _phase = @"network-failed";
            _message = httpStatus > 0 ?
                [NSString stringWithFormat:@"回應未通過（HTTP %ld）；本地選擇仍保留", (long)httpStatus] :
                @"網路失敗或已取消；本地選擇仍保留";
            return;
        }
        _acceptedCount++;
        _pending[@"success"] = @YES;
        if (![_pending[@"restoreAllowed"] boolValue]) {
            _phase = @"awaiting-login";
            _message = @"伺服器已接受；交易無法唯一對應，等待重新登入回蓋";
        } else {
            _phase = @"awaiting-parse";
            _message = @"伺服器已接受，等待遊戲原生解析";
        }
    }
}

- (NSDictionary *)consumeForEpoch:(NSUInteger)epoch
                         context:(uintptr_t)context
                         request:(uintptr_t)request
                     nativeError:(uint32_t)nativeError {
    @synchronized (self) {
        if (!_pending || [_pending[@"epoch"] unsignedIntegerValue] != epoch ||
            [_pending[@"nativeContext"] unsignedLongLongValue] != context ||
            [_pending[@"nativeRequest"] unsignedLongLongValue] != request) return nil;
        if (nativeError != 0) {
            _pending = nil;
            _failedCount++;
            _phase = @"native-failed";
            _message = @"遊戲原生解析失敗，已跳過回蓋；本地選擇仍保留";
            return nil;
        }
        if (![_pending[@"success"] isEqual:@YES] || ![_pending[@"restoreAllowed"] isEqual:@YES]) return nil;
        NSDictionary *transaction = [_pending copy];
        _pending = nil;
        _phase = @"parsed";
        _message = @"已觀察同一儲存請求解析成功";
        return transaction;
    }
}

- (void)recordRestoreCount:(NSUInteger)count reason:(NSString *)reason {
    @synchronized (self) {
        _restoredFieldCount += count;
        _restoreMessage = [reason copy];
        if ([_phase isEqualToString:@"parsed"]) {
            _phase = @"finished";
            _message = @"儲存及原生解析完成";
        }
    }
}

- (void)resetWithBanks:(NSDictionary *)banks accountReady:(BOOL)ready {
    @synchronized (self) {
        _pending = nil;
        _sequence++;
        _primaryDeckCount = [banks[@"decksb"] count];
        _secondaryDeckCount = [banks[@"decksbs"] count];
        _phase = ready ? @"loaded" : @"waiting-login";
        _message = !ready ? @"帳號已變更，等待登入成功" :
            (_primaryDeckCount + _secondaryDeckCount ? @"登入後已讀取目前帳號的本地選擇" : @"登入成功；目前沒有有效本地選擇");
        _restoreMessage = @"等待登入後比對隊伍";
    }
}

- (void)expireAtTime:(NSTimeInterval)time {
    @synchronized (self) {
        if (!_pending || time - [_pending[@"time"] doubleValue] <= 60) return;
        _pending = nil;
        _phase = @"expired";
        _message = @"等待回蓋逾時；本地選擇仍保留至下次登入";
    }
}

- (NSDictionary *)snapshot {
    @synchronized (self) {
        return @{@"phase": _phase, @"message": _message, @"restoreMessage": _restoreMessage,
                 @"localSaveCount": @(_localSaveCount), @"primaryDeckCount": @(_primaryDeckCount),
                 @"secondaryDeckCount": @(_secondaryDeckCount), @"submittedCount": @(_submittedCount),
                 @"acceptedCount": @(_acceptedCount), @"failedCount": @(_failedCount),
                 @"supersededCount": @(_supersededCount), @"restoredFieldCount": @(_restoredFieldCount),
                 @"pending": @(_pending != nil)};
    }
}
@end
