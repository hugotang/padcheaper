#import "PHBattleSession.h"
#include <string.h>

static NSString *PHBattleGateMessage(PHBattleGate gate, const PHBattleState *state) {
    switch (gate) {
        case PHBattleGateReady: return state->entryWindow ? @"新地下城開場，可自動套用隊伍" :
            PHBattleUsesPrimaryTeam(state->multiplayer) ? @"操作階段，可套用六個位置" : @"操作階段，可套用目前隊伍與助人";
        case PHBattleGateAbsent: return @"等待進入地下城";
        case PHBattleGateProtected: return @"戰鬥保護值或隊員參數未通過檢查";
        case PHBattleGateMode: return [NSString stringWithFormat:@"入口未在目前設定的允許清單內（mode %d／source %u／multi %u）",
                                      state->mode, state->sourceMode, state->multiplayer];
        case PHBattleGateLayout: return @"隊伍陣列或隊員來源尚未通過檢查，暫停操作";
        case PHBattleGatePhase: return [NSString stringWithFormat:@"等待玩家操作階段（phase %u）", state->phase];
        case PHBattleGateInputUnavailable: return @"觸控狀態未通過檢查，暫停替換";
        case PHBattleGateInput: return [NSString stringWithFormat:@"等待放開觸控或輸入處理完成（raw 0x%02x／frame 0x%02x）",
                                       state->input.rawActiveMask, state->input.frameActiveMask];
        case PHBattleGateDrag: return @"等待本次拖珠結束（phase 2）";
        case PHBattleGateAudit: return @"遊戲已記錄戰鬥數值異常，停止替換";
        case PHBattleGateHP: return @"HP 與原生校驗值不符，停止替換";
        case PHBattleGateDigest: return @"隊員摘要未通過原生校驗";
    }
    return @"等待戰鬥檢查";
}

static uint16_t Number(uint32_t id) {
    uint16_t number = 0;
    PHBattleNumberForCatalogID(id, &number);
    return number;
}
typedef struct {
    BOOL active, restorePending, restoreFailed, visible;
    NSUInteger slot;
    PHBattleIdentity identity;
    PHBattleMember original, applied;
    PHBattlePolicy policy;
} PHBattleLease;

static BOOL MatchesLease(const PHBattleMember *expected, const PHBattleMember *m) {
    return expected->providerToken ? m->providerToken == expected->providerToken && PHBattleSameOrigin(expected, m) :
        PHBattleSameSource(expected, m);
}

static NSInteger FindMember(const PHBattleState *state, const PHBattleMember *expected) {
    NSInteger result = -1;
    for (NSUInteger i = 0; i < 6; ++i) {
        const PHBattleMember *m = &state->members[i];
        if (!MatchesLease(expected, m)) continue;
        if (result >= 0) return -2;
        result = (NSInteger)i;
    }
    return result;
}

static const PHBattleMember *FindStoredMember(const PHBattleState *state, const PHBattleMember *expected) {
    if (!PHBattleTeamLayoutValid(state)) return NULL;
    const PHBattleMember *found = NULL;
    for (NSUInteger team = 0; team < state->teamCount; ++team) for (NSUInteger slot = 0; slot < 6; ++slot) {
        const PHBattleMember *m = &state->storedMembers[team][slot];
        if (!MatchesLease(expected, m)) continue;
        if (found) return NULL;
        found = m;
    }
    return found;
}

@implementation PHBattleSession {
    id<PHBattleEngine> _engine;
    PHBattleFrame _frame;
    PHBattleState _state;
    PHBattleIdentity _queueIdentity;
    PHBattleMember _pendingMembers[6];
    PHBattleLoadout _loadouts[6];
    PHBattleLoadout _automaticLoadouts[6], _entryLoadouts[6];
    PHBattleIdentity _automaticIdentity;
    PHBattleLease _leases[6];
    uint8_t _pending;
    uint8_t _automaticMask, _entryMask;
    BOOL _hasState, _prepared, _fault;
    BOOL _automaticHasIdentity, _autoArmed, _autoWaiting;
    NSUInteger _applyCount, _restoreCount, _rollbackCount;
    NSUInteger _automaticCount;
    uint16_t _initialBoost;
    NSTimeInterval _time, _lastProbe, _queuedTime;
    NSString *_message, *_gateMessage;
    NSString *_automaticMessage;
}
- (instancetype)initWithEngine:(id<PHBattleEngine>)engine {
    if ((self = [super init])) {
        _engine = engine; _message = @"尚未替換隊員"; _gateMessage = @"等待遊戲校驗與登入成功";
    }
    return self;
}
- (NSUInteger)activeCount {
    NSUInteger count = 0;
    for (NSUInteger i = 0; i < 6; ++i) count += _leases[i].active;
    return count;
}
- (BOOL)restorePending {
    for (NSUInteger i = 0; i < 6; ++i) if (_leases[i].active && _leases[i].restorePending) return YES;
    return NO;
}
- (BOOL)hasLeaseAtSlot:(NSUInteger)slot {
    for (NSUInteger i = 0; i < 6; ++i) if (_leases[i].active && _leases[i].visible && _leases[i].slot == slot) return YES;
    return NO;
}
- (void)cancelQueue:(NSString *)message { _pending = 0; _prepared = NO; _message = message; }
- (void)didMutate { _hasState = NO; _lastProbe = 0; }
- (void)recordFailure:(PHBattleMutationResult)result reason:(NSString *)reason {
    _rollbackCount += result == PHBattleMutationRolledBack;
    _fault |= result == PHBattleMutationUncertain;
    _message = reason ?: @"操作未完成，剩餘位置已取消；已套用的位置仍可還原";
}
- (void)configureAutomaticLoadouts:(const PHBattleLoadout *)loadouts mask:(uint8_t)mask {
    NSCAssert(NSThread.isMainThread, @"Automatic team config requires main");
    _automaticMask = loadouts && !(mask & ~0x3fu) ? mask : 0;
    if (_automaticMask) memcpy(_automaticLoadouts, loadouts, sizeof(_automaticLoadouts));
    else memset(_automaticLoadouts, 0, sizeof(_automaticLoadouts));
}
- (void)onFrame:(PHBattleFrame)frame atTime:(NSTimeInterval)time {
    NSCAssert(NSThread.isMainThread, @"Battle commands require the game main thread");
    BOOL accountChanged = _frame.header != frame.header || _frame.player != frame.player || _frame.epoch != frame.epoch;
    _frame = frame; _time = time;
    if (accountChanged) {
        _automaticHasIdentity = NO; _autoArmed = NO; _autoWaiting = NO;
    }
    if (!frame.enabled || !frame.autoApplyTeam) {
        if (_autoWaiting) _message = @"入場自動套用已取消";
        _autoArmed = NO; _autoWaiting = NO;
    }
    if (_pending && (_queueIdentity.header != frame.header || _queueIdentity.player != frame.player ||
        _queueIdentity.epoch != frame.epoch)) [self cancelQueue:@"帳號或遊戲影像已變更，取消待執行隊伍"];
    for (NSUInteger i = 0; i < 6; ++i) {
        PHBattleLease *lease = &_leases[i];
        if (lease->active && (lease->identity.header != frame.header || lease->identity.player != frame.player ||
            lease->identity.epoch != frame.epoch)) {
            lease->active = NO; _message = @"帳號或遊戲影像已變更；舊替換紀錄已失效";
        }
        if (!frame.enabled && lease->active && !lease->restoreFailed) lease->restorePending = YES;
    }
    if (!frame.enabled && _pending) [self cancelQueue:@"功能已關閉，取消待執行替換"];
    if (!frame.ready) {
        _hasState = NO; _autoArmed = NO; _autoWaiting = NO;
        _gateMessage = @"等待遊戲校驗、主執行緒與登入成功"; return;
    }
    BOOL watchEntry = frame.enabled && frame.autoApplyTeam && (_autoArmed || _autoWaiting || !_automaticHasIdentity);
    if (!_pending && ![self restorePending] && !watchEntry && _hasState && time - _lastProbe < 0.25) return;
    _lastProbe = time;
    PHBattleState next = {0};
    NSString *reason = nil;
    PHBattleFrame readFrame = frame;
    // A restrictive config edit must still allow an owned team to be read and
    // restored using the policy under which its mutation was validated.
    for (NSUInteger i = 0; i < 6; ++i) if (_leases[i].active) { readFrame.policy = _leases[i].policy; break; }
    if (![_engine readState:&next frame:readFrame reason:&reason]) {
        _hasState = NO; _gateMessage = reason ?: @"戰鬥資料暫時無法讀取";
        if (_pending && time - _queuedTime > 3) [self cancelQueue:@"替換已逾時，請重新套用"];
        return;
    }
    _state = next; _state.policy = frame.policy; _hasState = YES;
    PHBattleGate gate = PHBattleStateGate(&_state);
    _gateMessage = PHBattleGateMessage(gate, &_state);
    if (!_state.present) {
        // Enabling the option in an existing battle never arms that battle.
        // Observe the menu/scene transition first, then consume this arm once.
        _autoArmed = frame.enabled && frame.autoApplyTeam;
        _autoWaiting = NO; _automaticHasIdentity = NO;
        if (_autoArmed) _automaticMessage = _automaticMask ? @"等待下次入場自動套用儲存隊伍" : @"請先儲存包含本體的隊伍";
    } else if (!_automaticHasIdentity || !PHBattleSameIdentity(&_automaticIdentity, &_state.identity)) {
        _automaticIdentity = _state.identity; _automaticHasIdentity = YES;
        _autoWaiting = _autoArmed && frame.enabled && frame.autoApplyTeam;
        _autoArmed = NO;
        if (_autoWaiting) {
            memcpy(_entryLoadouts, _automaticLoadouts, sizeof(_entryLoadouts));
            _entryMask = _automaticMask;
            _automaticMessage = _message = @"已排程入場自動套用，等待開場動畫檢查";
        }
    }
    for (NSUInteger i = 0; i < 6; ++i) {
        PHBattleLease *lease = &_leases[i];
        if (!lease->active) continue;
        if (!_state.present || !PHBattleSameIdentity(&lease->identity, &_state.identity)) {
            lease->active = NO; _message = @"地下城已變更；舊替換紀錄已失效"; continue;
        }
        if (!_state.protectedValuesValid || !PHBattleTeamLayoutValid(&_state)) continue;
        NSInteger position = FindMember(&_state, &lease->applied);
        const PHBattleMember *m = position >= 0 ? &_state.members[position] :
            position == -1 ? FindStoredMember(&_state, &lease->applied) : NULL;
        BOOL superseded = !m;
        if (!superseded && !lease->applied.providerToken) {
            superseded = m->formID != lease->applied.formID ||
                ((!m->temporaryID && !m->temporaryTurns) &&
                 (m->catalogID != lease->applied.catalogID || m->previousID != lease->applied.previousID));
        }
        if (superseded) {
            lease->active = NO; _message = @"其中一名隊員由遊戲接管；其餘位置仍可還原";
        } else {
            lease->visible = position >= 0;
            lease->slot = lease->visible ? (NSUInteger)position : 6;
            if (!lease->restoreFailed && !PHBattlePolicyAllows(&frame.policy, _state.mode, _state.sourceMode, _state.multiplayer))
                lease->restorePending = YES;
        }
    }
    if (_pending && gate == PHBattleGateMode) [self cancelQueue:@"入口設定已變更，取消剩餘替換；已套用位置將逐格還原"];
    if (_fault) return;
    if (_autoWaiting) {
        if (!_entryMask || _pending || [self activeCount] || [self restorePending]) {
            _autoWaiting = NO;
            _automaticMessage = _message = !_entryMask ? @"儲存隊伍沒有本體，本次未自動套用" : @"已有隊伍操作，本次自動套用已取消";
        } else if (_state.protectedValuesValid && !_state.entryWindow) {
            _autoWaiting = NO;
            _automaticMessage = _message = @"本次不是新的入場動畫，保留目前技能充能；等待下次入場";
        } else {
            PHBattleGate entryGate = PHBattleEntryGate(&_state);
            if (entryGate == PHBattleGateReady) {
                // Finish the immutable entry batch in this frame, before the
                // original game can leave its intro and process preemptives.
                _autoWaiting = NO;
                PHBattleMember originals[6], applied[6] = {0};
                memcpy(originals, _state.members, sizeof(originals));
                uint16_t boost = 0;
                PHBattleMutationResult result = [_engine applyEntryLoadouts:_entryLoadouts mask:_entryMask state:&_state
                    members:applied initialBoost:&boost reason:&reason];
                if (result == PHBattleMutationApplied) {
                    NSUInteger count = 0;
                    for (NSUInteger i = 0; i < 6; ++i) if (_entryMask & (1u << i)) {
                        _leases[i] = (PHBattleLease){.active = YES, .visible = YES, .slot = i, .identity = _state.identity,
                            .original = originals[i], .applied = applied[i], .policy = _state.policy};
                        ++count;
                    }
                    _applyCount += count; ++_automaticCount; _initialBoost = boost;
                    _message = [NSString stringWithFormat:@"入場已自動套用 %lu 名隊員 · SB %u（已重算）", (unsigned long)count, boost];
                } else [self recordFailure:result reason:reason];
                _automaticMessage = _message;
                [self didMutate]; return;
            }
            if (entryGate == PHBattleGateMode || entryGate == PHBattleGateLayout || entryGate == PHBattleGateAudit ||
                entryGate == PHBattleGateHP || entryGate == PHBattleGateDigest) {
                _autoWaiting = NO;
                _automaticMessage = _message = [@"本次未自動套用：" stringByAppendingString:PHBattleGateMessage(entryGate, &_state)];
            } else { _gateMessage = PHBattleGateMessage(entryGate, &_state); return; }
        }
    }
    // Manual Apply/Restore still performs exactly one member per fresh frame.
    if ([self restorePending]) {
        BOOL temporary = NO, offscreen = NO;
        for (NSUInteger i = 0; i < 6; ++i) {
            PHBattleLease *lease = &_leases[i];
            if (!lease->active || !lease->restorePending) continue;
            if (!lease->visible) { offscreen = YES; continue; }
            PHBattleState restoreState = _state;
            restoreState.policy = PHBattlePolicyAllows(&_state.policy, _state.mode, _state.sourceMode, _state.multiplayer) ?
                _state.policy : lease->policy;
            PHBattleGate restoreGate = PHBattleStateGate(&restoreState);
            if (restoreGate != PHBattleGateReady) { _gateMessage = PHBattleGateMessage(restoreGate, &restoreState); continue; }
            PHBattleMember *current = &_state.members[lease->slot];
            if (current->temporaryID || current->temporaryTurns) { temporary = YES; continue; }
            PHBattleMutationResult result = lease->applied.providerToken ?
                [_engine restoreLoadout:&lease->original state:&restoreState slot:lease->slot reason:&reason] :
                [_engine changeState:&restoreState slot:lease->slot form:lease->original.formID
                    catalog:lease->original.catalogID presentation:&lease->original reason:&reason];
            lease->restorePending = NO;
            if (result == PHBattleMutationApplied) {
                lease->active = NO; lease->restoreFailed = NO; _restoreCount++;
                _message = [NSString stringWithFormat:@"位置 %lu 已還原為 #%u", (unsigned long)lease->slot + 1, Number(lease->original.catalogID)];
            } else { lease->restoreFailed = YES; [self recordFailure:result reason:reason]; }
            [self didMutate]; return;
        }
        if (temporary) _gateMessage = @"等待遊戲的暫時變身結束後還原";
        else if (offscreen) _gateMessage = @"等待切回包含待還原隊員的隊伍";
        return;
    }
    if (!_pending) return;
    if (!_state.present || !PHBattleSameIdentity(&_queueIdentity, &_state.identity)) {
        [self cancelQueue:@"待執行隊伍的戰鬥已變更，請重新套用"]; return;
    }
    if (time - _queuedTime > 3) { [self cancelQueue:@"替換已逾時，請在操作階段重新套用"]; return; }
    if (!_state.protectedValuesValid) return;
    NSInteger positions[6] = {0};
    for (NSUInteger i = 0; i < 6; ++i) {
        if (!(_pending & (1u << i))) continue;
        positions[i] = FindMember(&_state, &_pendingMembers[i]);
        PHBattleMember *m = positions[i] < 0 ? NULL : &_state.members[positions[i]];
        if (!m || m->formID != _pendingMembers[i].formID || m->catalogID != _pendingMembers[i].catalogID ||
            m->temporaryID || m->temporaryTurns) {
            [self cancelQueue:@"待執行隊員已變更，剩餘位置已取消；已套用的位置仍可還原"]; return;
        }
    }
    if (gate != PHBattleGateReady) return;
    if (!_prepared) {
        // Validate the whole immutable batch before its first write.
        for (NSUInteger i = 0; i < 6; ++i) {
            if (!(_pending & (1u << i))) continue;
            uint16_t catalog = 0;
            BOOL valid = PHBattleCatalogIDForNumber(_loadouts[i].bodyNumber, &catalog) &&
                (_loadouts[i].parameters ? [_engine validateLoadout:&_loadouts[i] state:&_state reason:&reason] :
                                          [_engine validateTarget:catalog state:&_state reason:&reason]);
            if (!valid) {
                [self cancelQueue:[NSString stringWithFormat:@"位置 %lu：%@", (unsigned long)i + 1, reason ?: @"角色或武裝設定無效"]];
                return;
            }
        }
        _prepared = YES;
    }
    for (NSUInteger i = 0; i < 6; ++i) {
        if (!(_pending & (1u << i))) continue;
        NSUInteger slot = (NSUInteger)positions[i], freeLease = 6;
        for (NSUInteger j = 0; j < 6; ++j) if (!_leases[j].active) { freeLease = j; break; }
        if (freeLease == 6) { [self cancelQueue:@"六個位置均已套用，請先還原要修改的位置"]; return; }
        PHBattleMember original = _state.members[slot], applied = original;
        uint16_t catalog = 0;
        PHBattleCatalogIDForNumber(_loadouts[i].bodyNumber, &catalog);
        PHBattleMutationResult result;
        if (_loadouts[i].parameters) {
            result = [_engine applyLoadout:&_loadouts[i] state:&_state slot:slot member:&applied reason:&reason];
        } else {
            result = [_engine changeState:&_state slot:slot form:catalog catalog:catalog presentation:NULL reason:&reason];
            applied.formID = catalog; applied.catalogID = catalog; applied.previousID = (uint16_t)original.catalogID;
        }
        _pending &= (uint8_t)~(1u << i);
        if (result == PHBattleMutationApplied) {
            _leases[freeLease] = (PHBattleLease){.active = YES, .visible = YES, .slot = slot, .identity = _state.identity,
                                                .original = original, .applied = applied, .policy = _state.policy};
            _applyCount++;
            _message = [NSString stringWithFormat:@"位置 %lu 已套用 #%u；共 %lu 名，可分別還原%@", (unsigned long)slot + 1,
                _loadouts[i].bodyNumber, (unsigned long)[self activeCount], _pending ? @"，繼續套用其餘位置" : @""];
        } else { _pending = 0; [self recordFailure:result reason:reason]; }
        [self didMutate]; return;
    }
}
- (void)queueApplySlot:(NSUInteger)slot targetText:(NSString *)text {
    uint16_t target;
    if (!PHBattleParseTarget(text.UTF8String, &target)) { _message = @"請輸入有效圖鑑編號（1–65435）"; return; }
    if (slot >= 6) { _message = @"位置必須為 1–6"; return; }
    PHBattleLoadout loadouts[6] = {0};
    loadouts[slot].bodyNumber = target;
    [self queueLoadouts:loadouts mask:(uint8_t)(1u << slot)];
}
- (void)queueLoadouts:(const PHBattleLoadout *)loadouts mask:(uint8_t)mask {
    NSCAssert(NSThread.isMainThread, @"Battle UI actions require main");
    if (!loadouts || !mask || (mask & ~0x3fu)) { _message = @"請選擇要套用的位置"; return; }
    if (_fault) { _message = @"上次原生操作未能確認還原；請停止本場並複製診斷"; return; }
    if (!_frame.ready || !_frame.enabled || !_hasState || _time - _lastProbe > 1) {
        _message = @"請先啟用總開關與戰鬥替換，並等待戰鬥檢查"; return;
    }
    if (_pending || [self restorePending]) { _message = @"隊伍操作正在排程，請稍候"; return; }
    if (PHBattleStateGate(&_state) != PHBattleGateReady) { _message = _gateMessage; return; }
    for (NSUInteger i = 0; i < 6; ++i) {
        if (!(mask & (1u << i))) continue;
        const PHBattleMember *m = &_state.members[i];
        uint16_t catalog;
        if ([self hasLeaseAtSlot:i]) { _message = @"要修改的位置已套用，請先還原該位置"; return; }
        if (!PHBattleCatalogIDForNumber(loadouts[i].bodyNumber, &catalog) || !m->active || !m->readable ||
            m->sourceSlot < 0 || m->sourceSlot > 5 || m->scriptSlot > 0 || m->providerToken ||
            m->temporaryID || m->temporaryTurns || !Number(m->catalogID) ||
            FindMember(&_state, m) != (NSInteger)i || (!loadouts[i].parameters && m->catalogID == catalog)) {
            _message = [NSString stringWithFormat:@"位置 %lu 不可替換、正在暫時變身，或已是目標角色", (unsigned long)i + 1]; return;
        }
    }
    memcpy(_loadouts, loadouts, sizeof(_loadouts));
    _autoWaiting = NO;
    memcpy(_pendingMembers, _state.members, sizeof(_pendingMembers));
    _queueIdentity = _state.identity; _pending = mask; _prepared = NO; _queuedTime = _time;
    _message = @"已排入隊伍設定，等待遊戲畫面更新";
}
- (void)queueRestoreSlot:(NSUInteger)slot {
    NSCAssert(NSThread.isMainThread, @"Battle UI actions require main");
    if (_fault) { _message = @"原生操作狀態不明，已停止後續寫入"; return; }
    BOOL cancelledEntry = _autoWaiting;
    _autoWaiting = NO; _autoArmed = NO;
    [self cancelQueue:@"已取消待執行隊伍"];
    BOOL found = NO;
    for (NSUInteger i = 0; i < 6; ++i) {
        if (_leases[i].active && (slot >= 6 || (_leases[i].visible && _leases[i].slot == slot))) {
            _leases[i].restorePending = YES; _leases[i].restoreFailed = NO; found = YES;
        }
    }
    _message = found ? @"已排入還原，等待相同戰鬥的操作階段" :
        cancelledEntry ? @"已取消本次入場自動套用" : @"該位置沒有可還原的替換；待執行隊伍已取消";
    if (cancelledEntry) _automaticMessage = _message;
}
- (void)queueRestore { [self queueRestoreSlot:6]; }
- (NSDictionary *)snapshot {
    NSUInteger activeCount = [self activeCount];
    NSUInteger offscreenCount = 0;
    for (NSUInteger i = 0; i < 6; ++i) offscreenCount += _leases[i].active && !_leases[i].visible;
    BOOL restoring = [self restorePending];
    NSMutableArray *members = [NSMutableArray array];
    if (_hasState && _state.present) for (NSUInteger slot = 0; slot < 6; ++slot) {
        PHBattleMember m = _state.members[slot];
        BOOL leased = [self hasLeaseAtSlot:slot];
        [members addObject:@{@"slot": @(slot + 1), @"active": @(m.active), @"readable": @(m.readable),
            @"source": @(Number(m.baseID)), @"current": @(Number(m.catalogID)), @"form": @(Number(m.formID)),
            @"assist": @(Number(m.assistID)), @"nativeID": @(m.catalogID), @"charge": @(m.charge),
            @"sourceTeam": @(m.sourceTeam + 1), @"sourceSlot": @(m.sourceSlot + 1),
            @"level": @(m.level), @"skillLevel": @(m.skillLevel), @"leased": @(leased),
            @"temporary": @(m.temporaryID != 0 || m.temporaryTurns != 0),
            @"replaceable": @(!leased && m.active && m.readable && m.sourceSlot >= 0 && m.sourceSlot <= 5 &&
                m.scriptSlot <= 0 && !m.temporaryID && !m.temporaryTurns && Number(m.catalogID) != 0)}];
    }
    BOOL canApply = _hasState && _frame.ready && _frame.enabled && activeCount < 6 && !_pending && !restoring && !_fault &&
                    PHBattleStateGate(&_state) == PHBattleGateReady;
    NSString *phase = _fault ? @"fault" : restoring ? @"restore_pending" : _autoWaiting ? @"entry_pending" :
        _pending ? @"apply_pending" : activeCount ? @"applied" : @"idle";
    return @{@"implemented": @YES, @"phase": phase, @"enabled": @(_frame.enabled),
        @"canApply": @(canApply), @"canRestore": @((activeCount || _pending || _autoWaiting) && !_fault), @"applied": @(activeCount > 0),
        @"activeCount": @(activeCount), @"pendingMask": @(_pending), @"restorePending": @(restoring), @"fault": @(_fault),
        @"message": _message ?: @"", @"gate": _gateMessage ?: @"", @"members": members,
        @"mode": @(_state.mode), @"sourceMode": @(_state.sourceMode), @"multiplayer": @(_state.multiplayer),
        @"teamCount": @(_state.teamCount), @"currentTeam": @(_state.currentTeam + 1),
        @"teamBanksValid": @(_state.teamBanksValid), @"offscreenCount": @(offscreenCount),
        @"battlePhase": @(_state.phase), @"auditFlags": @(_state.auditFlags),
        @"inputValid": @(_hasState && _state.input.valid), @"inputQuiet": @(_hasState && PHInputIsQuiet(&_state.input)),
        @"rawInputCount": @(_state.input.rawCount), @"frameInputCount": @(_state.input.frameCount),
        @"rawInputMask": @(_state.input.rawActiveMask), @"frameInputMask": @(_state.input.frameActiveMask),
        @"rawInputFlags": @(_state.input.rawFlags), @"frameInputFlags": @(_state.input.frameFlags),
        @"dragActive": @(_hasState && _state.dragActive), @"digestKnown": @(_hasState && _state.digestKnown),
        @"digestMatches": @(_hasState && _state.digestMatches), @"hpMatches": @(_hasState && _state.hpMatches),
        @"applyCount": @(_applyCount), @"restoreCount": @(_restoreCount), @"rollbackCount": @(_rollbackCount), @"deviceVerified": @NO,
        @"automaticEnabled": @(_frame.autoApplyTeam), @"automaticWaiting": @(_autoWaiting),
        @"automaticCount": @(_automaticCount), @"initialBoost": @(_initialBoost),
        @"entryWindow": @(_hasState && _state.entryWindow), @"entryChargesValid": @(_hasState && _state.entryChargesValid),
        @"automaticMessage": _automaticMessage ?: @""};
}
@end
