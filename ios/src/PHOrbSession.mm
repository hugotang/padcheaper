#import "PHOrbSession.h"
#include "PHOrbSolver.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr double StepSeconds = 0.065, PreviewLifetime = 45, ExecutionLimit = 4, TimeMargin = 0.40;
bool SamePosition(const PHOrbSnapshot &a, const PHOrbSnapshot &b) {
    return PHOrbSameTurn(&a, &b) && a.moves == b.moves && PHOrbSameBoard(&a.board, &b.board);
}
double TimeAvailable(const PHOrbSnapshot &state) {
    return state.fps >= 15 && state.fps <= 120 && state.remainingFrames <= 30000 ?
        double(state.remainingFrames) / state.fps : 0;
}
double StepDuration(const PHOrbSnapshot &state) {
    return state.fps >= 15 && state.fps <= 120 ? std::ceil(StepSeconds * state.fps) / state.fps : StepSeconds;
}
bool HasRouteTime(const PHOrbSnapshot &state, const PHOrbRoute &route) {
    if (route.count < 2) return false;
    const double required = (route.count - 1) * StepDuration(state) + TimeMargin;
    return required <= std::min(TimeAvailable(state), ExecutionLimit);
}
PHOrbSolveOptions OptionsForQuality(NSString *quality) {
    return PHOrbOptionsForQuality([quality isEqual:@"fast"] ? PHOrbSearchQuality::Fast :
        [quality isEqual:@"thorough"] ? PHOrbSearchQuality::Thorough : PHOrbSearchQuality::Balanced);
}
NSDictionary *BoardJSON(const PHOrbBoard &board) {
    NSMutableArray *types = [NSMutableArray array], *enhanced = [NSMutableArray array];
    const unsigned size = std::min(unsigned(PHOrbMaxCells), unsigned(board.width) * board.height);
    for (unsigned i = 0; i < size; ++i) {
        [types addObject:@(board.types[i])]; [enhanced addObject:@(board.enhancement[i] != 0)];
    }
    return @{@"width":@(board.width), @"height":@(board.height), @"types":types, @"enhanced":enhanced};
}
}

@implementation PHOrbSession {
    id<PHOrbEngine> _engine;
    dispatch_queue_t _worker;
    std::shared_ptr<std::atomic_bool> _cancel;
    PHBattleFrame _frame;
    PHOrbSnapshot _live, _stable, _preview, _expected;
    PHOrbRoute _route;
    NSString *_phase, *_message, *_quality;
    PHOrbSolveOptions _solveOptions;
    NSUInteger _generation, _completed, _cancelled, _sentFrames, _confirmed;
    NSUInteger _target;
    uintptr_t _ownedOrb;
    double _now, _lastRead, _requestedAt, _stableAt, _previewAt, _startedAt, _stepAt, _sentAt;
    bool _hasLive, _hasStable, _readQueued, _staged, _releaseStaged, _releaseComplete;
    bool _inputFault, _awaitingStart, _monitoring;
}
- (instancetype)initWithEngine:(id<PHOrbEngine>)engine {
    if ((self = [super init])) {
        _engine = engine;
        _worker = dispatch_queue_create("jp.padcheaper.orbs.solver", DISPATCH_QUEUE_SERIAL);
        _phase = @"idle"; _message = @"單人操作階段，按「讀盤並求路」開始";
        _quality = @"balanced";
    }
    return self;
}
- (BOOL)executing {
    return [_phase isEqual:@"arming"] || [_phase isEqual:@"running"] || [_phase isEqual:@"releasing"];
}
- (void)cancelWorker {
    ++_generation;
    if (_cancel) _cancel->store(true, std::memory_order_relaxed);
    _cancel.reset();
}
- (BOOL)configureQuality:(NSString *)quality {
    if (![quality isKindOfClass:NSString.class] || ![@[@"fast", @"balanced", @"thorough"] containsObject:quality] ||
        [self executing] || _inputFault) return NO;
    if ([_quality isEqual:quality]) return YES;
    const bool hadPreview = _readQueued || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"];
    [self cancelWorker];
    _quality = [quality copy]; _route = {}; _confirmed = 0; _solveOptions = OptionsForQuality(_quality);
    _readQueued = _hasStable = false;
    if (hadPreview) { _phase = @"idle"; _message = @"求路品質已變更，請重新讀盤"; }
    return YES;
}
- (void)queueStop:(NSString *)reason {
    const bool cancelledIntent = _readQueued || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        [_phase isEqual:@"arming"] || [_phase isEqual:@"running"] ||
        ([_phase isEqual:@"releasing"] && _releaseComplete);
    [self cancelWorker];
    _readQueued = false; _hasStable = false;
    _message = reason ?: @"已停止";
    _releaseComplete = false;
    if (_ownedOrb || _staged) _phase = @"releasing";
    else _phase = @"stopped";
    if (cancelledIntent) ++_cancelled;
}
- (void)queueRead {
    if ([self executing] || _inputFault) return;
    [self cancelWorker];
    _monitoring = _readQueued = true; _hasStable = false; _requestedAt = _now;
    _route = {}; _phase = @"reading"; _message = @"等待盤面穩定…";
}
- (void)queueExecute:(NSUInteger)generation {
    if (![_phase isEqual:@"preview"] || generation != _generation || !_frame.enabled || _inputFault) return;
    if (_now - _previewAt > PreviewLifetime || !_hasLive || !PHOrbCanPreview(&_live) || !SamePosition(_live, _preview)) {
        [self queueStop:@"盤面或回合已改變，請重新讀盤"];
        return;
    }
    if (!HasRouteTime(_live, _route)) {
        _message = @"操作時間不足，請重新讀盤取得較短路徑";
        return;
    }
    _expected = _preview; _target = _confirmed = 0; _ownedOrb = 0;
    _awaitingStart = true; _phase = @"arming"; _requestedAt = _now;
    _message = @"準備執行一次；點浮動「停止」即可中斷";
}
- (void)beginSolve {
    _readQueued = false; _preview = _live;
    PHOrbSolveOptions options = OptionsForQuality(_quality);
    const double available = TimeAvailable(_live);
    // Quantize to native frames as well as the hard execution deadline. A
    // longer thorough route must remain feasible even at a lower frame rate.
    const double budget = std::min(available > 0 ? available : ExecutionLimit, ExecutionLimit);
    options.steps = budget > TimeMargin ? std::max(1u, std::min(options.steps,
        unsigned((budget - TimeMargin) / StepDuration(_live)))) : 1;
    _solveOptions = options;
    _cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = _cancel;
    const PHOrbBoard board = _preview.board;
    const NSUInteger generation = _generation;
    __weak PHOrbSession *weakSelf = self;
    _phase = @"solving"; _message = @"正在求路…";
    dispatch_async(_worker, ^{
        PHOrbRoute route{};
        const auto result = PHOrbSolve(board, options, *cancel, route);
        dispatch_async(dispatch_get_main_queue(), ^{
            PHOrbSession *session = weakSelf;
            if (!session || generation != session->_generation || ![session->_phase isEqual:@"solving"] ||
                cancel->load(std::memory_order_relaxed)) return;
            if (result != PHOrbSolveStatus::Solved || route.count < 2) {
                session->_phase = @"stopped"; session->_message = @"本次未取得有效路徑，請重新讀盤";
                return;
            }
            PHOrbBoard replay{};
            if (!PHOrbReplay(&board, &route, route.count - 1, &replay)) {
                session->_phase = @"fault"; session->_message = @"路徑重播校驗失敗，已停止";
                return;
            }
            session->_route = route; session->_phase = @"preview"; session->_previewAt = session->_now;
            session->_message = [NSString stringWithFormat:@"預計 %u Combo（不含天降）· %u 步 · 約 %.1f 秒",
                route.combos, route.count - 1, (route.count - 1) * StepDuration(session->_preview)];
        });
    });
}
- (void)finishNativeFrame {
    NSString *reason = nil;
    if (![_engine finishInput:&reason]) {
        _inputFault = true;
        [self queueStop:reason ?: @"輸入恢復失敗，已停止"];
        _phase = @"fault"; _ownedOrb = 0; _staged = false;
    }
}
- (void)beforeFrame:(PHBattleFrame)frame time:(NSTimeInterval)time {
    [self finishNativeFrame]; // Also retries an earlier restoration failure.
    _frame = frame; _now = time;
    _staged = _releaseStaged = false;
    if (_inputFault || ![self executing]) return;
    NSString *reason = nil;
    PHOrbSnapshot current{};
    if (![_engine read:&current frame:frame reason:&reason] || !current.valid || current.multiplayerDisabled ||
        !PHOrbSameTurn(&_expected, &current) || current.phase != 2) {
        // The private manager has already been removed. Never inject stale
        // cleanup into a new scene/multiplayer; native input now owns release.
        [self queueStop:@"回合、場景或可操作狀態已改變，已停止"];
        _ownedOrb = 0; _phase = @"stopped";
        return;
    }
    _live = current; _hasLive = true;
    if ([_phase isEqual:@"arming"]) {
        if (!frame.enabled || !PHOrbCanPreview(&current) || !SamePosition(current, _preview)) {
            [self queueStop:@"執行前盤面或開關已改變，請重新讀盤"]; return;
        }
        if (time - _requestedAt < 0.35) return;
        if (time - _requestedAt > 2 || !HasRouteTime(current, _route)) {
            [self queueStop:@"開始逾時或操作時間不足"]; return;
        }
        _phase = @"running"; _startedAt = _stepAt = _sentAt = time;
        _expected = current;
    }
    if ([_phase isEqual:@"running"]) {
        if (!frame.enabled || !PHInputIsQuiet(&current.input) || !current.ordinary ||
            time - _startedAt > ExecutionLimit || time < _startedAt || (!current.dragged && !_awaitingStart)) {
            [self queueStop:!PHInputIsQuiet(&current.input) ? @"偵測到手動操作，已停止並放開" : @"操作條件已改變，已停止"];
        } else if (!_awaitingStart && current.dragged != _ownedOrb) {
            [self queueStop:@"拖珠已由遊戲接管，已停止"]; _ownedOrb = 0; _phase = @"stopped"; return;
        } else if (!SamePosition(current, _expected)) {
            [self queueStop:@"盤面與已確認步驟不同，已停止"];
        } else if (!_awaitingStart && _target == _confirmed && time - _stepAt >= StepSeconds) {
            if (_confirmed + 1 == _route.count) {
                _phase = @"releasing"; _releaseComplete = true; _message = @"路徑完成，正在放開…";
            } else { _target = _confirmed + 1; _sentAt = time; }
        }
    }
    if ([_phase isEqual:@"releasing"]) {
        if (!current.dragged || !_ownedOrb || current.dragged != _ownedOrb) {
            _ownedOrb = 0; _phase = @"stopped"; return;
        }
        _expected = current;
        // Use the dragged orb's actual cell when a partial move was rejected.
        NSUInteger cell = 0;
        const NSUInteger cells = current.board.width * current.board.height;
        for (; cell < cells; ++cell) if (current.board.tokens[cell] == _ownedOrb) break;
        _staged = [_engine stageCell:cell down:NO beginning:NO expected:&current frame:frame reason:&reason];
        _releaseStaged = _staged;
        if (!_staged) { _ownedOrb = 0; _phase = @"stopped"; _message = reason ?: @"已停止輸入，等待遊戲結束拖珠"; }
        return;
    }
    if (![_phase isEqual:@"running"]) return;
    // Every native frame needs a hold, even when waiting for the next cell.
    _staged = [_engine stageCell:_route.cells[_target] down:YES beginning:_awaitingStart
        expected:&current frame:frame reason:&reason];
    if (!_staged) [self queueStop:reason ?: @"輸入前校驗失敗，已停止"];
    else ++_sentFrames;
}
- (void)afterFrame:(PHBattleFrame)frame time:(NSTimeInterval)time {
    _frame = frame; _now = time;
    if (_inputFault) return;
    const BOOL active = [self executing];
    // Keep observing scene exits after transient read failures. Otherwise a
    // multiplayer latch could survive into the next solo battle forever.
    // Monitoring starts only after the user first asks to read a board.
    if (!active && !_monitoring) return;
    if (!active && time - _lastRead < 0.10) return;
    _lastRead = time;
    PHOrbSnapshot current{}; NSString *reason = nil;
    BOOL read = [_engine read:&current frame:frame reason:&reason];
    _hasLive = read; _live = current;
    if (active && _staged) {
        if (!read || !current.valid || current.multiplayerDisabled || !PHOrbSameTurn(&_expected, &current)) {
            [self queueStop:@"原生執行後場景已改變，已停止"]; _ownedOrb = 0; _phase = @"stopped"; return;
        }
        if (_releaseStaged) {
            if (current.dragged == 0) {
                _ownedOrb = 0;
                _phase = _releaseComplete ? @"complete" : @"stopped";
                if (_releaseComplete) { ++_completed; _message = @"本次路徑已執行並放開；下一回合請重新讀盤"; }
            } else if (time - _startedAt > ExecutionLimit + 0.5) {
                _phase = @"stopped"; _ownedOrb = 0; _message = @"已停止輸入；遊戲尚未確認放開，請手動檢查";
            }
            return;
        }
        if (_awaitingStart) {
            if (current.dragged == _preview.board.tokens[_route.cells[0]] && SamePosition(current, _preview)) {
                _ownedOrb = current.dragged; _awaitingStart = false; _expected = current; _stepAt = time;
                _message = @"正在執行；點浮動「停止」即可中斷";
            } else if (current.dragged || time - _startedAt > 0.20) {
                _ownedOrb = current.dragged == _preview.board.tokens[_route.cells[0]] ? current.dragged : 0;
                [self queueStop:@"遊戲未接受起點，已停止"];
            }
            return;
        }
        PHOrbBoard expectedBoard{};
        PHOrbReplay(&_preview.board, &_route, unsigned(_target), &expectedBoard);
        if (current.dragged == _ownedOrb && current.phase == 2 && PHOrbSameBoard(&current.board, &expectedBoard) &&
            current.moves == _preview.moves + _target) {
            if (_confirmed != _target) _stepAt = time;
            _confirmed = _target; _expected = current;
        } else if (SamePosition(current, _expected) && time - _sentAt <= 0.20 && current.dragged == _ownedOrb) {
            // Keep the same target; never skip an unacknowledged swap.
        } else {
            [self queueStop:@"原生交換結果與路徑不符，已停止"];
        }
        return;
    }
    if ([_phase isEqual:@"preview"] || [_phase isEqual:@"solving"]) {
        if (!read || !PHOrbCanPreview(&current) || !SamePosition(current, _preview) ||
            ([_phase isEqual:@"preview"] && time - _previewAt > PreviewLifetime)) {
            [self queueStop:(!read || !PHOrbCanPreview(&current)) ? (reason ?: @"盤面已無法讀取，請重新讀盤") :
                @"盤面、回合或預覽有效期已改變，請重新讀盤"];
        }
    }
    if (_readQueued) {
        if (!read || !PHOrbCanPreview(&current)) {
            _hasStable = false; _message = reason ?: @"盤面未通過讀取校驗";
            if (time - _requestedAt > 2) { _readQueued = false; _phase = @"blocked"; }
        } else if (!_hasStable || !SamePosition(current, _stable)) {
            _stable = current; _stableAt = time; _hasStable = true;
        } else if (time - _stableAt >= 0.15) [self beginSolve];
    }
}
- (NSDictionary *)snapshot {
    NSMutableArray *path = [NSMutableArray array];
    for (unsigned i = 0; i < _route.count; ++i) [path addObject:@(_route.cells[i])];
    BOOL preview = [_phase isEqual:@"preview"];
    BOOL canExecute = preview && _frame.enabled && !_inputFault && _hasLive && PHOrbCanPreview(&_live) &&
        SamePosition(_live, _preview) && HasRouteTime(_live, _route);
    return @{@"phase":_phase, @"message":_message, @"generation":@(_generation),
        @"canRead":@(_frame.ready && ![self executing] && !_inputFault), @"canExecute":@(canExecute),
        @"busy":@([self executing]), @"canStop":@([self executing] || _readQueued || [_phase isEqual:@"solving"]),
        @"enabled":@(_frame.ready && _frame.enabled), @"board":BoardJSON(_route.count ? _preview.board : _live.board), @"path":path,
        @"quality":_quality, @"searchSteps":@(_solveOptions.steps), @"searchBeam":@(_solveOptions.beam),
        @"searchBudgetMS":@(_solveOptions.milliseconds),
        @"combos":@(_route.combos), @"steps":@(_route.count ? _route.count - 1 : 0), @"confirmed":@(_confirmed),
        @"elapsedMS":@(_route.elapsedMS), @"evaluated":@(_route.evaluated), @"deadline":@(_route.deadlineReached),
        @"completed":@(_completed), @"cancelled":@(_cancelled), @"inputFrames":@(_sentFrames), @"inputFault":@(_inputFault),
        @"multiplayerDisabled":@(_live.multiplayerDisabled), @"nativePhase":@(_live.phase), @"wave":@(_live.wave),
        @"moves":@(_live.moves), @"remainingFrames":@(_live.remainingFrames), @"fps":@(_live.fps),
        @"settled":@(_live.settled), @"ordinary":@(_live.ordinary), @"deviceVerified":@NO};
}
@end
