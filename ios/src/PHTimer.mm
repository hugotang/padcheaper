#import "PHTimer.h"
#import "PHMemory.h"
#import "PHInput.h"
#import "PHTarget.h"
#include "PHTimerModel.h"
#include "PHTargetGenerated.h"
#include "PHBattleGenerated.h"
#include <cstring>
#include <stdexcept>

namespace {
constexpr uintptr_t Pending = 0x92a00, Dialog = 0xbe9c8, Hold = 0xb3592, SceneID = 0x92a38;

static BOOL CheckTimerCode(uintptr_t header, NSString **reason) {
    if (!PHValidateGuest(reinterpret_cast<const mach_header_64 *>(header), reason)) return NO;
    for (size_t i = 0; i < PHBattleTextGuardCount; ++i) {
        const PHBattleTextGuard &guard = PHBattleTextGuards[i];
        if (guard.rva != PHBattleSet32RVA && guard.rva != PHBattleDismissDialogRVA && guard.rva != PHBattleWaitTimerRVA) continue;
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) {
            if (reason) *reason = @"計時原生函式校驗不符";
            return NO;
        }
    }
    return YES;
}

static BOOL ReadTimer(PHTimerState *state, PHBattleFrame frame) {
    *state = {};
    if (!frame.ready || !frame.header || !frame.player || !NSThread.isMainThread) return NO;
    uintptr_t header = frame.header, save = header + PHSaveRVA, game = header + PHGameMainRVA;
    uint8_t scene = 0, requested = 0;
    uint32_t player = 0;
    if (!PHReadU8(save, &scene) || !PHReadU8(save + 1, &requested) ||
        !PHReadU32(save + PHPlayerIDOffset, &player) || player != frame.player) return NO;
    if (scene != 1 || requested != scene) return YES;
    uintptr_t object = 0, impl = 0;
    if (!PHReadPointer(header + PHSceneTableRVA + 24, &object) || object != game ||
        !PHReadPointer(game + 0x70, &impl) || !impl || (impl & 7u) || impl > UINTPTR_MAX - 0xc0000 ||
        !PHMemoryHasProtection(impl + 0x92000, 0x2d000, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) return NO;
    state->identity = {header, game, impl, player, frame.epoch, 0, 0, {0}};
    uint16_t ids[2] = {}, mode = 0, limit = 0;
    uint32_t key1 = 0, key2 = 0, phase[4], pending[4];
    if (!PHReadPointer(impl + PHBattleDungeonPointerOffset, &state->countdown) || !state->countdown ||
        state->countdown > UINTPTR_MAX - 128 || !PHMemoryHasProtection(state->countdown, 128, VM_PROT_READ, 0) ||
        !PHReadU16(state->countdown + 12, &limit) ||
        !PHReadU32(impl + SceneID, &state->sceneID) ||
        !PHReadMemory(save + PHBattleDungeonOffset, ids, sizeof(ids)) || !ids[0] || !ids[1] ||
        !PHReadMemory(header + PHBattleEntryHashRVA, state->identity.entryHash, sizeof(state->identity.entryHash)) ||
        !PHReadU16(header + PHBattleModeRVA, &mode) ||
        !PHReadU32(header + PHBattleMultiplayerRVA, &state->multiplayer) ||
        !PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
        !PHReadMemory(impl + PHBattlePhaseOffset, phase, sizeof(phase)) ||
        !PHReadMemory(impl + Pending, pending, sizeof(pending)) ||
        !PHReadU8(impl + PHTimerFlagsOffset, &state->flags) ||
        !PHReadPointer(impl + Dialog, &state->dialog)) return NO;
    bool hashPresent = false;
    for (uint8_t byte : state->identity.entryHash) hashPresent |= byte != 0;
    state->present = hashPresent;
    state->identity.dungeon = ids[0]; state->identity.floor = ids[1];
    state->mode = static_cast<int16_t>(mode);
    state->limitSeconds = static_cast<int16_t>(limit);
    PHReadGameInput(header, &state->input);
    BOOL phaseValid = PHBattleDecodeProtected(phase, key1, key2, 0x766cfc6du, &state->phase);
    BOOL pendingValid = PHBattleDecodeProtected(pending, key1, key2, 0x766cfc6du, &state->pending);
    // Pending can be unused until the first popup; only the popup path needs it.
    state->valuesValid = phaseValid && (state->phase != 58 || pendingValid);
    return YES;
}

static BOOL SameTimer(const PHTimerState *a, const PHTimerState *b) {
    return PHBattleSameIdentity(&a->identity, &b->identity) && a->countdown == b->countdown &&
        a->limitSeconds == b->limitSeconds && a->sceneID == b->sceneID;
}

static void Write(uintptr_t address, const void *data, size_t length) {
    if (!PHWriteData(address, data, length)) throw std::runtime_error("timer write failed");
}

static BOOL Dismiss(const PHTimerState *expected, PHBattleFrame frame, NSString **reason) {
    PHTimerState current = {};
    if (!CheckTimerCode(frame.header, reason) || !ReadTimer(&current, frame) || !PHTimerCanDismiss(&current) ||
        !SameTimer(expected, &current) || expected->dialog != current.dialog ||
        !PHMemoryHasProtection(current.dialog, 0x1a0, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE) ||
        !PHMemoryHasProtection(frame.header + PHBattlePRNGRVA, 4, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) {
        if (reason && !*reason) *reason = @"計時彈窗或戰鬥已改變，已取消撤窗";
        return NO;
    }
    uintptr_t impl = current.identity.impl;
    auto set32 = reinterpret_cast<uint64_t (*)(uintptr_t, uint64_t)>(frame.header + PHBattleSet32RVA);
    auto close = reinterpret_cast<void (*)(uintptr_t, float)>(frame.header + PHBattleDismissDialogRVA);
    uint8_t flags = (current.flags & static_cast<uint8_t>(~0x22u)) | 0x40u, hold = 0;
    uintptr_t empty = 0;
    // sub_100589248 passes the dialog in X0 and duration in S0. Its normal
    // pending==2 branch closes the dialog and copies that phase back to current.
    // The explicit user command selects that continuation; automatic code never does.
    Write(impl + PHTimerFlagsOffset, &flags, sizeof(flags));
    close(current.dialog, 0.25f);
    Write(impl + Dialog, &empty, sizeof(empty));
    Write(impl + Hold, &hold, sizeof(hold));
    set32(impl + Pending, 2);
    set32(impl + PHBattlePhaseOffset, 2);
    PHTimerState after = {};
    if (!ReadTimer(&after, frame) || !SameTimer(&current, &after) || !after.valuesValid ||
        after.phase != 2 || after.pending != 2 || after.dialog || after.flags != flags) {
        throw std::runtime_error("timer continuation could not be verified");
    }
    return YES;
}
}

@implementation PHTimer {
    PHBattleFrame _frame;
    PHTimerState _state, _queuedState;
    uintptr_t _verifiedHeader;
    BOOL _hasState, _queued, _fault;
    NSTimeInterval _time, _lastRead, _queuedTime;
    NSUInteger _pauseClears, _flagWrites, _dismissals;
    NSString *_message, *_gate;
}

- (instancetype)init {
    if ((self = [super init])) {
        _message = @"尚未手動撤窗";
        _gate = @"等待遊戲校驗與登入";
    }
    return self;
}

- (void)onFrame:(PHBattleFrame)frame atTime:(NSTimeInterval)time force:(BOOL)force {
    NSCAssert(NSThread.isMainThread, @"Timer commands require the game main thread");
    _frame = frame; _time = time;
    if (!frame.ready || !frame.enabled) {
        if (_queued) { _queued = NO; _message = @"條件已改變，取消待執行撤窗"; }
        if (!frame.ready) { _hasState = NO; _gate = @"等待遊戲校驗、主執行緒與登入"; return; }
    }
    if (!force && !_queued && time - _lastRead < (frame.enabled ? 0.05 : 0.5)) return;
    _lastRead = time;
    if (_verifiedHeader != frame.header) {
        NSString *codeReason = nil;
        if (!CheckTimerCode(frame.header, &codeReason)) { _gate = codeReason; _hasState = NO; return; }
        _verifiedHeader = frame.header;
    }
    _hasState = ReadTimer(&_state, frame);
    _gate = !_hasState ? @"計時資料未通過檢查" : !PHTimerEligible(&_state) ? @"等待一般單人計時關卡" :
        PHTimerCanDismiss(&_state) ? @"已偵測計時退出彈窗，可手動關閉" :
        (_state.phase == 58 && _state.pending == 32 && _state.dialog && !_state.input.valid) ? @"觸控狀態未通過檢查，暫停撤窗" :
        (_state.phase == 58 && _state.pending == 32 && _state.dialog && !PHInputIsQuiet(&_state.input)) ? @"等待放開觸控或輸入處理完成後撤窗" :
        [NSString stringWithFormat:@"計時上限 %d 秒 · phase %u · flags 0x%02x", _state.limitSeconds, _state.phase, _state.flags];
    if (_fault) { _gate = @"計時操作狀態不明，已停止後續寫入"; return; }
    if (!_hasState) {
        if (_queued && time - _queuedTime > 3) { _queued = NO; _message = @"撤窗已逾時，請重新操作"; }
        return;
    }
    if (_queued) {
        _queued = NO;
        if (time - _queuedTime > 3 || !PHTimerCanDismiss(&_state) || !SameTimer(&_queuedState, &_state) ||
            _queuedState.dialog != _state.dialog) { _message = @"計時彈窗已改變或逾時，取消撤窗"; return; }
        try {
            @try {
                NSString *reason = nil;
                if (Dismiss(&_state, frame, &reason)) {
                    _dismissals++;
                    _message = @"已手動關閉計時退出彈窗並返回操作階段";
                    _hasState = NO; _lastRead = 0;
                } else _message = reason;
            } @catch (NSException *exception) {
                (void)exception;
                _fault = YES;
                _message = @"撤窗發生原生例外，已停止寫入";
            }
        } catch (...) {
            _fault = YES;
            _message = @"撤窗未能確認完成，已停止寫入；請複製診斷";
        }
        return;
    }
    if (frame.enabled) {
        uint8_t next = PHTimerAutomaticFlags(&_state);
        if (next != _state.flags) {
            PHTimerState fresh = {};
            NSString *codeReason = nil;
            if (!ReadTimer(&fresh, frame) || !SameTimer(&_state, &fresh)) return;
            if (!CheckTimerCode(frame.header, &codeReason)) { _gate = codeReason; return; }
            next = PHTimerAutomaticFlags(&fresh);
            if (next != fresh.flags) {
                if (!PHWriteData(fresh.identity.impl + PHTimerFlagsOffset, &next, sizeof(next))) {
                    _fault = YES; _message = @"計時旗標寫入失敗"; return;
                }
                _pauseClears += (fresh.flags & 0x20u) != 0;
                _flagWrites++;
                _state.flags = next;
            }
        }
    }
}

- (void)queueDismiss {
    NSCAssert(NSThread.isMainThread, @"Timer UI commands require main");
    if (!_frame.ready || !_frame.enabled || !_hasState || _fault || _queued ||
        _time - _lastRead > 1 || !PHTimerCanDismiss(&_state)) {
        _message = @"請啟用總開關與計時修正，並等待計時退出彈窗";
        return;
    }
    _queued = YES; _queuedState = _state; _queuedTime = _time;
    _message = @"已排入手動撤窗，等待遊戲畫面更新";
}

- (NSDictionary *)snapshot {
    return @{@"implemented": @YES, @"active": @(_frame.ready && _frame.enabled && _hasState && !_fault && PHTimerEligible(&_state)),
        @"canDismiss": @(_frame.ready && _frame.enabled && _hasState && !_fault && !_queued && PHTimerCanDismiss(&_state)),
        @"queued": @(_queued), @"fault": @(_fault), @"gate": _gate ?: @"", @"message": _message ?: @"",
        @"phase": @(_state.phase), @"pendingPhase": @(_state.pending), @"flags": @(_state.flags),
        @"inputValid": @(_hasState && _state.input.valid), @"inputQuiet": @(_hasState && PHInputIsQuiet(&_state.input)),
        @"rawInputCount": @(_state.input.rawCount), @"frameInputCount": @(_state.input.frameCount),
        @"rawInputMask": @(_state.input.rawActiveMask), @"frameInputMask": @(_state.input.frameActiveMask),
        @"rawInputFlags": @(_state.input.rawFlags), @"frameInputFlags": @(_state.input.frameFlags),
        @"limitSeconds": @(_state.limitSeconds), @"pauseClearCount": @(_pauseClears),
        @"flagWriteCount": @(_flagWrites), @"dismissCount": @(_dismissals), @"deviceVerified": @NO};
}
@end
