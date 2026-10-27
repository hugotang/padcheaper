#import "PHBattleSession.h"
#include <string.h>

static NSString *PHBattleGateMessage(PHBattleGate gate, const PHBattleState *state) {
    switch (gate) {
        case PHBattleGateReady: return @"單人操作階段，可替換一名隊員";
        case PHBattleGateAbsent: return @"等待進入地下城";
        case PHBattleGateProtected: return @"戰鬥保護值或隊員參數未通過檢查";
        case PHBattleGateMode: return [NSString stringWithFormat:@"此關卡分類或隊伍入口尚未支援（mode %d／source %u／multi %u）",
                                      state->mode, state->sourceMode, state->multiplayer];
        case PHBattleGateBusy: return [NSString stringWithFormat:@"等待玩家操作階段（phase %u）", state->phase];
        case PHBattleGateAudit: return @"遊戲已記錄戰鬥數值異常，停止替換";
        case PHBattleGateHP: return @"HP 與原生校驗值不符，停止替換";
        case PHBattleGateDigest: return @"隊員摘要未通過原生校驗";
    }
    return @"等待戰鬥檢查";
}

@implementation PHBattleSession {
    id<PHBattleEngine> _engine;
    PHBattleFrame _frame;
    PHBattleState _state, _queuedState, _originalState;
    BOOL _hasState, _queued, _applied, _restorePending, _restoreFailed, _fault;
    NSUInteger _slot, _queuedSlot, _applyCount, _restoreCount, _rollbackCount;
    uint16_t _target, _appliedForm;
    uint16_t _appliedPreviousID;
    NSTimeInterval _time, _lastProbe, _queuedTime;
    NSString *_message, *_gateMessage;
}

- (instancetype)initWithEngine:(id<PHBattleEngine>)engine {
    if ((self = [super init])) {
        _engine = engine;
        _message = @"尚未替換隊員";
        _gateMessage = @"等待遊戲校驗與登入成功";
    }
    return self;
}

- (void)forgetLease:(NSString *)reason {
    _queued = NO;
    _applied = NO;
    _restorePending = NO;
    _restoreFailed = NO;
    _message = reason;
}

- (void)onFrame:(PHBattleFrame)frame atTime:(NSTimeInterval)time {
    NSCAssert(NSThread.isMainThread, @"Battle commands require the game main thread");
    _frame = frame;
    _time = time;
    if ((_applied || _queued) &&
        (_originalState.identity.header != frame.header || _originalState.identity.player != frame.player ||
         _originalState.identity.epoch != frame.epoch)) {
        [self forgetLease:@"帳號或遊戲影像已變更；舊替換紀錄已失效"];
    }
    if (!frame.enabled) {
        if (_queued) { _queued = NO; _message = @"功能已關閉，取消待執行替換"; }
        if (_applied && !_restoreFailed) _restorePending = YES;
    }
    if (!frame.ready) {
        _hasState = NO;
        _gateMessage = @"等待遊戲校驗、主執行緒與登入成功";
        return;
    }
    if (!_queued && !_restorePending && time - _lastProbe < 0.25) return;
    _lastProbe = time;
    PHBattleState next = {0};
    NSString *reason = nil;
    if (![_engine readState:&next frame:frame reason:&reason]) {
        _hasState = NO;
        _gateMessage = reason ?: @"戰鬥資料暫時無法讀取";
        if (_queued && time - _queuedTime > 3) { _queued = NO; _message = @"替換已逾時，請重新套用"; }
        return;
    }
    _state = next;
    _hasState = YES;
    PHBattleGate gate = PHBattleStateGate(&_state);
    _gateMessage = PHBattleGateMessage(gate, &_state);
    if (_applied) {
        PHBattleMember *member = &_state.members[_slot];
        if (!_state.present || !PHBattleSameIdentity(&_originalState.identity, &_state.identity) ||
            (member->readable && (!PHBattleSameSource(&_originalState.members[_slot], member) ||
             member->formID != _appliedForm ||
             ((!member->temporaryID && !member->temporaryTurns) &&
              (member->catalogID != _appliedForm || member->previousID != _appliedPreviousID))))) {
            [self forgetLease:@"地下城、隊員或原生變身已變更；舊替換紀錄已失效"];
        }
    }
    if (_queued) {
        PHBattleMember *old = &_queuedState.members[_queuedSlot], *member = &_state.members[_queuedSlot];
        if (!_state.present || !PHBattleSameIdentity(&_queuedState.identity, &_state.identity) ||
            !PHBattleSameSource(old, member) || old->formID != member->formID ||
            old->catalogID != member->catalogID || member->temporaryID || member->temporaryTurns) {
            _queued = NO;
            _message = @"待執行替換的戰鬥或隊員已變更，請重新套用";
        } else if (time - _queuedTime > 3) {
            _queued = NO;
            _message = @"替換已逾時，請在操作階段重新套用";
        } else if (gate == PHBattleGateReady) {
            _queued = NO;
            if (![_engine validateTarget:_target state:&_state reason:&reason]) {
                _message = reason ?: @"目標圖鑑 ID 無效";
                return;
            }
            _originalState = _state;
            _slot = _queuedSlot;
            PHBattleMutationResult result = [_engine changeState:&_state slot:_slot form:_target catalog:_target
                                                 presentation:NULL reason:&reason];
            if (result == PHBattleMutationApplied) {
                _applied = YES;
                _appliedForm = _target;
                _appliedPreviousID = (uint16_t)_state.members[_slot].catalogID;
                _applyCount++;
                _message = [NSString stringWithFormat:@"位置 %lu 已替換為 #%u；可按還原", (unsigned long)_slot + 1, _target];
            } else {
                _rollbackCount += result == PHBattleMutationRolledBack;
                _fault = result == PHBattleMutationUncertain;
                _message = reason ?: @"替換未完成";
            }
            _hasState = NO;
            _lastProbe = 0;
        }
    }
    if (_applied && _restorePending && gate == PHBattleGateReady && !_fault) {
        PHBattleMember *member = &_state.members[_slot];
        if (member->temporaryID || member->temporaryTurns) {
            _gateMessage = @"等待遊戲的暫時變身結束後還原";
            return;
        }
        PHBattleMember *original = &_originalState.members[_slot];
        PHBattleMutationResult result = [_engine changeState:&_state slot:_slot form:original->formID
                                             catalog:original->catalogID presentation:original reason:&reason];
        if (result == PHBattleMutationApplied) {
            _applied = NO;
            _restorePending = NO;
            _restoreFailed = NO;
            _restoreCount++;
            _message = [NSString stringWithFormat:@"位置 %lu 已還原為 #%u", (unsigned long)_slot + 1, original->catalogID];
        } else {
            _rollbackCount += result == PHBattleMutationRolledBack;
            _fault = result == PHBattleMutationUncertain;
            // A failed explicit restore must not call native code each frame.
            _restorePending = NO;
            _restoreFailed = YES;
            _message = reason ?: @"還原未完成，請查看診斷";
        }
        _hasState = NO;
        _lastProbe = 0;
    }
}

- (void)queueApplySlot:(NSUInteger)slot targetText:(NSString *)text {
    NSCAssert(NSThread.isMainThread, @"Battle UI actions require main");
    uint16_t target = 0;
    if (!PHBattleParseTarget(text.UTF8String, &target)) { _message = @"請輸入有效圖鑑 ID（1–65535，排除 9900–9999）"; return; }
    if (_fault) { _message = @"上次原生操作未能確認還原；請停止本場並複製診斷"; return; }
    if (!_frame.ready || !_frame.enabled || !_hasState || _time - _lastProbe > 1) {
        _message = @"請先啟用總開關與戰鬥替換，並等待戰鬥檢查"; return;
    }
    if (_applied || _queued) { _message = @"一次只能替換一個位置，請先還原"; return; }
    if (slot >= 6 || PHBattleStateGate(&_state) != PHBattleGateReady) { _message = _gateMessage; return; }
    PHBattleMember *member = &_state.members[slot];
    if (!member->active || !member->readable || member->sourceSlot < 0 || member->sourceSlot > 5 || member->scriptSlot > 0 ||
        member->temporaryID || member->temporaryTurns || !member->catalogID || member->catalogID > UINT16_MAX ||
        member->catalogID == target) { _message = @"該位置不可替換、正在暫時變身，或已是目標角色"; return; }
    _queued = YES;
    _target = target;
    _queuedSlot = slot;
    _queuedState = _state;
    _originalState = _state;
    _queuedTime = _time;
    _message = @"已排入替換，等待遊戲畫面更新";
}

- (void)queueRestore {
    NSCAssert(NSThread.isMainThread, @"Battle UI actions require main");
    if (_queued) { _queued = NO; _message = @"已取消待執行替換"; return; }
    if (!_applied) { _message = @"目前沒有可還原的替換"; return; }
    if (_fault) { _message = @"原生操作狀態不明，已停止後續寫入"; return; }
    _restorePending = YES;
    _restoreFailed = NO;
    _message = @"已排入還原，等待相同戰鬥的操作階段";
}

- (NSDictionary *)snapshot {
    NSMutableArray *members = [NSMutableArray array];
    if (_hasState && _state.present) {
        for (NSUInteger slot = 0; slot < 6; ++slot) {
            PHBattleMember member = _state.members[slot];
            [members addObject:@{@"slot": @(slot + 1), @"active": @(member.active), @"readable": @(member.readable),
                @"source": @(member.baseID), @"current": @(member.catalogID), @"form": @(member.formID),
                @"charge": @(member.charge), @"temporary": @(member.temporaryID != 0 || member.temporaryTurns != 0),
                @"replaceable": @(member.active && member.readable && member.sourceSlot >= 0 && member.sourceSlot <= 5 &&
                    member.scriptSlot <= 0 && !member.temporaryID && !member.temporaryTurns &&
                    member.catalogID > 0 && member.catalogID <= UINT16_MAX)}];
        }
    }
    BOOL canApply = _hasState && _frame.ready && _frame.enabled && !_applied && !_queued && !_fault &&
                    PHBattleStateGate(&_state) == PHBattleGateReady;
    NSString *phase = _fault ? @"fault" : _restorePending ? @"restore_pending" : _applied ? @"applied" :
                      _queued ? @"apply_pending" : @"idle";
    return @{@"implemented": @YES, @"phase": phase, @"enabled": @(_frame.enabled),
        @"canApply": @(canApply), @"canRestore": @((_applied || _queued) && !_fault), @"applied": @(_applied),
        @"restorePending": @(_restorePending), @"fault": @(_fault), @"message": _message ?: @"",
        @"gate": _gateMessage ?: @"", @"members": members,
        @"mode": @(_state.mode), @"sourceMode": @(_state.sourceMode), @"multiplayer": @(_state.multiplayer),
        @"battlePhase": @(_state.phase), @"auditFlags": @(_state.auditFlags),
        @"digestKnown": @(_hasState && _state.digestKnown), @"digestMatches": @(_hasState && _state.digestMatches),
        @"hpMatches": @(_hasState && _state.hpMatches), @"applyCount": @(_applyCount),
        @"restoreCount": @(_restoreCount), @"rollbackCount": @(_rollbackCount), @"deviceVerified": @NO};
}
@end
