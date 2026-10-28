#import "PHOrbSession.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#include "PHOrbSolver.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>

namespace {
constexpr double StepSeconds = 0.065, PreviewLifetime = 45, ExecutionLimit = 4, TimeMargin = 0.40;
constexpr double AutomationSettle = 0.20, SkillTimeout = 30, TurnTimeout = 90;
bool ResolvingOrbs(uint32_t phase) { return phase >= 3 && phase <= 6; }
bool SamePosition(const PHOrbSnapshot &a, const PHOrbSnapshot &b) {
    return PHOrbSameTurn(&a, &b) && PHOrbSameLeaders(&a, &b) && a.moves == b.moves && PHOrbSameBoard(&a.board, &b.board);
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
    NSMutableArray *types = [NSMutableArray array], *enhanced = [NSMutableArray array], *locked = [NSMutableArray array];
    const unsigned size = std::min(unsigned(PHOrbMaxCells), unsigned(board.width) * board.height);
    for (unsigned i = 0; i < size; ++i) {
        [types addObject:@(board.types[i])]; [enhanced addObject:@(board.enhancement[i] != 0)];
        [locked addObject:@((board.flags[i] & PHOrbFlagLocked) != 0)];
    }
    return @{@"width":@(board.width), @"height":@(board.height), @"types":types, @"enhanced":enhanced, @"locked":locked};
}
}

@implementation PHOrbSession {
    id<PHOrbEngine> _engine;
    dispatch_queue_t _worker;
    std::shared_ptr<std::atomic_bool> _cancel;
    PHBattleFrame _frame;
    PHOrbSnapshot _live, _stable, _preview, _expected;
    PHOrbRoute _route;
    NSString *_phase, *_message, *_quality, *_goalMode;
    NSArray *_manualGoals;
    NSArray *_skillRules, *_historyRules;
    NSMutableArray *_automationLog;
    NSMutableArray<NSString *> *_ruleDecisions;
    NSMutableDictionary<NSNumber *, NSString *> *_unavailableReasons;
    NSDictionary *_goalPlan;
    NSDictionary *_profileLibrary, *_activeProfile;
    PHOrbSolveOptions _solveOptions;
    NSUInteger _generation, _completed, _cancelled, _sentFrames, _confirmed;
    NSUInteger _target;
    uintptr_t _ownedOrb;
    double _now, _lastRead, _requestedAt, _stableAt, _previewAt, _startedAt, _stepAt, _sentAt;
    bool _hasLive, _hasStable, _readQueued, _staged, _releaseStaged, _releaseComplete;
    bool _inputFault, _awaitingStart, _monitoring;
    PHOrbSnapshot _autoOrigin, _skillBefore;
    NSUInteger _autoTurn, _autoMoves, _autoSkills, _skillRule, _decisionTurn;
    uint16_t _usedRules, _unavailableRules, _confirmedRules;
    uint8_t _usedSlots, _usedGroups, _confirmedGroups, _groupNext[PHOrbMaxSkillGroups];
    NSMutableSet<NSDictionary *> *_onceRules;
    PHBattleIdentity _historyIdentity;
    bool _historyBound, _historyResolve;
    double _autoPhaseAt, _autoStartAt;
    bool _automatic, _autoBound, _sawResolve, _skillAcknowledged, _watchPlayerState;
}
- (instancetype)initWithEngine:(id<PHOrbEngine>)engine {
    if ((self = [super init])) {
        _engine = engine;
        _worker = dispatch_queue_create("jp.padcheaper.orbs.solver", DISPATCH_QUEUE_SERIAL);
        _phase = @"idle"; _message = @"單人操作階段，可預覽路徑或開始連續轉珠";
        _quality = @"balanced"; _goalMode = @"combo"; _manualGoals = @[]; _goalPlan = @{};
        _skillRules = @[]; _automationLog = [NSMutableArray array]; _onceRules = [NSMutableSet set];
        _ruleDecisions = [NSMutableArray array]; _unavailableReasons = [NSMutableDictionary dictionary];
        _profileLibrary = PHOrbDefaultProfiles(); _activeProfile = @{};
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
    if (!quality) return NO;
    return [self configureSettings:@{@"quality":quality, @"mode":_goalMode, @"goals":_manualGoals, @"rules":_skillRules}];
}
- (BOOL)configureSettings:(NSDictionary *)settings {
    NSDictionary *canonical = PHOrbCanonicalSettings(settings);
    if (!canonical || _automatic || [self executing] || _inputFault) return NO;
    if ([_quality isEqual:canonical[@"quality"]] && [_goalMode isEqual:canonical[@"mode"]] &&
        [_manualGoals isEqual:canonical[@"goals"]] && [_skillRules isEqual:canonical[@"rules"]]) return YES;
    [self cancelWorker];
    [self loadSettings:canonical];
    _readQueued = _hasStable = false;
    _phase = @"idle"; _message = @"求路設定已變更，請重新讀盤";
    return YES;
}
- (void)loadSettings:(NSDictionary *)settings {
    _quality = [settings[@"quality"] copy]; _goalMode = [settings[@"mode"] copy];
    _manualGoals = settings[@"goals"]; _goalPlan = @{}; _route = {}; _confirmed = 0;
    _solveOptions = OptionsForQuality(_quality);
    _skillRules = settings[@"rules"];
    [_ruleDecisions removeAllObjects]; _decisionTurn = 0;
    _watchPlayerState = false;
    for (NSDictionary *rule in _skillRules) if ([rule[@"enabled"] boolValue])
        _watchPlayerState |= [rule[@"hpAtMost"] unsignedIntegerValue] > 0 || [rule[@"required"] boolValue] ||
            (rule[@"status"] && ![rule[@"status"] isEqual:@"none"]);
}
- (BOOL)configureProfiles:(NSDictionary *)library {
    if (!PHOrbProfilesValid(library) || _automatic || [self executing] || _inputFault) return NO;
    if ([_profileLibrary isEqual:library]) return YES;
    NSData *data = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
    NSDictionary *saved = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    if (!saved) return NO;
    [self cancelWorker]; _profileLibrary = saved; _activeProfile = @{};
    _route = {}; _readQueued = _hasStable = false;
    _phase = @"idle"; _message = @"策略已儲存；按開始時核對隊伍與關卡";
    return YES;
}
- (void)queueStop:(NSString *)reason {
    const bool wasAutomatic = _automatic;
    const bool skillRunning = [_phase isEqual:@"skillWaiting"];
    const bool cancelledIntent = _automatic || _readQueued || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        [_phase isEqual:@"arming"] || [_phase isEqual:@"running"] ||
        ([_phase isEqual:@"releasing"] && _releaseComplete);
    [self cancelWorker];
    _automatic = false;
    _readQueued = false; _hasStable = false;
    _message = reason ?: @"已停止";
    if (skillRunning) _message = [_message stringByAppendingString:@"；已啟動的技能由遊戲完成"];
    if (wasAutomatic) [self logAutomation:[NSString stringWithFormat:@"連續模式停止：%@", _message]];
    _releaseComplete = false;
    if (_ownedOrb || _staged) _phase = @"releasing";
    else _phase = @"stopped";
    if (cancelledIntent) ++_cancelled;
}
- (void)queueRead {
    if (_automatic || [self executing] || _inputFault) return;
    [self cancelWorker];
    _monitoring = _readQueued = true; _hasStable = false; _requestedAt = _now;
    _route = {}; _goalPlan = @{}; _phase = @"reading"; _message = @"等待盤面穩定…";
}
- (void)queueAutomatic {
    if (_automatic || [self executing] || _inputFault || !_frame.ready || !_frame.enabled) return;
    [self cancelWorker];
    _automatic = _monitoring = true; _autoBound = _sawResolve = _hasStable = _readQueued = false;
    _autoTurn = 1; _autoMoves = _autoSkills = 0; _unavailableRules = 0;
    _autoStartAt = _autoPhaseAt = _now; _autoOrigin = {}; _route = {}; _goalPlan = @{};
    _activeProfile = @{};
    [_automationLog removeAllObjects];
    [_ruleDecisions removeAllObjects]; [_unavailableReasons removeAllObjects]; _decisionTurn = 0;
    _phase = @"autoWaiting"; _message = @"連續模式準備中；點浮動「停止」可隨時中斷";
}
- (void)logAutomation:(NSString *)message {
    [_automationLog addObject:message];
    if (_automationLog.count > 12) [_automationLog removeObjectAtIndex:0];
}
- (void)waitForAutomationBoard {
    _phase = @"autoWaiting"; _autoPhaseAt = _now; _hasStable = false;
    _message = [NSString stringWithFormat:@"連續第 %lu 回合 · 等待盤面穩定", (unsigned long)_autoTurn];
}
// Keep once-per-battle and per-orb-turn history across a manual stop/start.
// Transient read failures, phase 22 and skill-cleared floors do not reset it.
- (void)observeRuleHistory:(const PHOrbSnapshot *)state read:(BOOL)read {
    if (!read) return;
    if (!state->present) {
        _historyBound = _historyResolve = false; [_onceRules removeAllObjects];
        _historyRules = nil;
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        [_unavailableReasons removeAllObjects];
        std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
        return;
    }
    if (!state->valid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer)) return;
    if (!_historyBound || !PHBattleSameIdentity(&_historyIdentity, &state->identity)) {
        _historyIdentity = state->identity; _historyBound = true; _historyResolve = false;
        _historyRules = nil;
        [_onceRules removeAllObjects];
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        [_unavailableReasons removeAllObjects];
        std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
    }
    if (ResolvingOrbs(state->phase) && state->moves > 0) _historyResolve = true;
    if (_historyResolve && state->phase == 2 && state->moves == 0 && state->settled && !state->dragged) {
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        [_unavailableReasons removeAllObjects]; _historyResolve = false;
    }
}
// Called on every observed native frame while armed, even during animation.
// A skill's phase 22 is never evidence that an orb turn has completed.
- (BOOL)observeAutomation:(const PHOrbSnapshot *)state read:(BOOL)read reason:(NSString *)reason {
    [self observeRuleHistory:state read:read];
    if (!_frame.ready || !_frame.enabled || _now < _autoPhaseAt) {
        [self queueStop:@"總開關、前景或登入狀態已改變，已停止連續模式"]; return NO;
    }
    if (!read || !state->present || !state->valid || !state->automation.valid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) || state->identity.sourceMode != 0 || state->wave >= 999) {
        [self queueStop:reason ?: @"關卡已結束或戰鬥資料無法確認，已停止連續模式"]; return NO;
    }
    if (!_autoBound) {
        NSDictionary *resolved = PHOrbResolveProfile(_profileLibrary, state);
        if (![resolved[@"ok"] boolValue]) { [self queueStop:resolved[@"message"]]; return NO; }
        _activeProfile = resolved[@"profile"];
        if (_activeProfile.count) [self loadSettings:PHOrbCanonicalSettings(_activeProfile[@"settings"])];
        // Idle config reloads can restore different common settings after an
        // automatically selected profile stops. Only rules chosen for a new
        // run may reset its cursor; reloading the UI must not restart a cycle.
        if (![_historyRules isEqual:_skillRules]) {
            _usedRules = _unavailableRules = _confirmedRules = _confirmedGroups = 0;
            [_unavailableReasons removeAllObjects];
            std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
            _historyRules = _skillRules;
        }
        [self logAutomation:[NSString stringWithFormat:@"本次策略：%@", resolved[@"message"]]];
        _autoOrigin = *state; _autoBound = true;
    }
    if (!PHBattleSameIdentity(&_autoOrigin.identity, &state->identity) || _autoOrigin.inputManager != state->inputManager) {
        [self queueStop:@"已離開本次戰鬥，連續模式停止"]; return NO;
    }
    if (!PHInputIsQuiet(&state->input) || (state->dragged && ![self executing])) {
        [self queueStop:@"偵測到手動操作，已停止連續模式"]; return NO;
    }
    if (_watchPlayerState && state->phase == 2 &&
        ([_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] || [_phase isEqual:@"arming"] || [_phase isEqual:@"running"]) &&
        !PHOrbSamePlayerState(&_preview, state)) {
        // A change in the native frame accepting our first touch still needs
        // an owned-orb release; it must not bypass the new rescue condition.
        if (_staged && _awaitingStart && _route.count && state->dragged == _preview.board.tokens[_route.cells[0]])
            _ownedOrb = state->dragged;
        [self queueStop:@"求路後 HP 或我方異常已改變，請重新開始判斷自救規則"]; return NO;
    }
    if ([_phase isEqual:@"skillWaiting"] && state->automation.skillUses == _skillBefore.automation.skillUses + 1)
        _skillAcknowledged = true; // Preserve acknowledgement if a skill clears the floor and the counter later resets.
    return YES;
}
- (void)advanceAutomation {
    if (!_automatic || [self executing] || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        [_phase isEqual:@"skillArming"] || _now - _autoStartAt < 0.35) return;
    if ([_phase isEqual:@"turnWaiting"]) {
        _sawResolve |= ResolvingOrbs(_live.phase);
        if (_now - _autoPhaseAt > TurnTimeout) { [self queueStop:@"等候下一回合逾時，請檢查遊戲畫面"]; return; }
        if (!_sawResolve || _live.phase != 2 || _live.moves != 0 || _live.automation.busy || !_live.settled) {
            _hasStable = false; return;
        }
    } else if ([_phase isEqual:@"skillWaiting"]) {
        if (_now - _autoPhaseAt > SkillTimeout) { [self queueStop:@"技能等候逾時或需要手動選擇，已停止"]; return; }
        if (_live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
        if (!_skillAcknowledged) {
            [self queueStop:@"技能使用次數未確認，已停止以免重複施放"]; return;
        }
    } else if ([_phase isEqual:@"autoWaiting"]) {
        if (_now - _autoPhaseAt > 12) {
            [self queueStop:(PHOrbBoardObstacles(&_live) & PHOrbObstacleSpinner) ?
                @"轉轉盤面持續變色，未取得可確認的技能快照；請先手動處理後再開始" : @"尚未進入穩定的單人操作階段，已停止"]; return;
        }
        if (_live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
    } else return;
    if (!_hasStable || !PHOrbSameAutomation(&_live, &_stable)) {
        _stable = _live; _stableAt = _now; _hasStable = true; return;
    }
    if (_now - _stableAt < AutomationSettle) return;
    if ([_phase isEqual:@"turnWaiting"]) {
        ++_autoTurn;
        [self waitForAutomationBoard]; return;
    }
    if ([_phase isEqual:@"skillWaiting"]) {
        ++_autoSkills; _unavailableRules = 0; // Haste may make a formerly unavailable member ready.
        [_unavailableReasons removeAllObjects];
        _confirmedRules |= 1u << _skillRule;
        NSUInteger group = [_skillRules[_skillRule][@"group"] unsignedIntegerValue];
        if (group) _confirmedGroups |= 1u << (group - 1);
        [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 技能完成", (unsigned long)_autoTurn, (unsigned long)_skillRule + 1]];
        [self waitForAutomationBoard]; return;
    }
    [_ruleDecisions removeAllObjects]; _decisionTurn = _autoTurn;
    for (NSUInteger i = 0; i < _skillRules.count; ++i) [_ruleDecisions addObject:@"尚未判斷"];
    uint16_t unmetRequired = 0;
    for (NSNumber *index in PHOrbSkillRuleOrder(_skillRules, _groupNext)) {
        NSUInteger i = index.unsignedIntegerValue;
        NSDictionary *rule = _skillRules[i];
        NSUInteger slot = [rule[@"slot"] unsignedIntegerValue] - 1;
        NSUInteger group = [rule[@"group"] unsignedIntegerValue];
        if (![rule[@"enabled"] boolValue]) { _ruleDecisions[i] = @"已停用"; continue; }
        if ((_confirmedRules & (1u << i)) || (group && (_confirmedGroups & (1u << (group - 1))))) {
            _ruleDecisions[i] = group ? @"同組本回合已有技能確認完成" : @"本回合已確認完成";
            continue;
        }
        const PHOrbSkillMember *member = &_live.automation.members[slot];
        NSString *unavailable = nil;
        if (_usedRules & (1u << i)) unavailable = @"本回合已嘗試，但未確認完成";
        else if (_usedSlots & (1u << slot)) unavailable = @"此位置本回合已使用，不能重複施放";
        else if (group && (_usedGroups & (1u << (group - 1)))) unavailable = @"同組本回合已嘗試，但未確認完成";
        else if ([rule[@"once"] boolValue] && [_onceRules containsObject:rule]) unavailable = @"本場一次的使用紀錄已消耗";
        else if (PHOrbSkillReservedUntil(_skillRules, slot) > _live.wave + 1)
            unavailable = [NSString stringWithFormat:@"此位置保留至第 %lu 層", (unsigned long)PHOrbSkillReservedUntil(_skillRules, slot)];
        else if (_unavailableRules & (1u << i)) unavailable = _unavailableReasons[@(i)] ?: @"遊戲判定技能不可用";
        else if (!member->active) unavailable = @"此位置沒有可使用的隊員";
        else if (member->bound) unavailable = @"施放者被綁定";
        else if (member->state) unavailable = @"施放者尚未回到待命狀態";
        else if (member->overridden) unavailable = @"此位置保留了手動技能選項";
        // Optional rules retain the previous short circuit: unavailable
        // actors do not make unrelated, unknown condition data mandatory.
        if (unavailable && ![rule[@"required"] boolValue]) {
            _ruleDecisions[i] = [NSString stringWithFormat:@"略過 · %@", unavailable]; continue;
        }
        NSString *trigger = nil;
        PHOrbRuleMatch match = PHOrbTriggerSkillRule(rule, &_live, _autoTurn, &trigger);
        _ruleDecisions[i] = trigger ?: @"條件無法確認";
        if (match == PHOrbRuleUnknown) {
            [self queueStop:[NSString stringWithFormat:@"規則 %lu：%@，已停止", (unsigned long)i + 1, _ruleDecisions[i]]]; return;
        }
        if (match != PHOrbRuleYes) continue;
        if ([rule[@"required"] boolValue]) unmetRequired |= 1u << i;
        if (unavailable) {
            _ruleDecisions[i] = [NSString stringWithFormat:@"%@ · %@", trigger, unavailable]; continue;
        }
        if ([rule[@"once"] boolValue] && _onceRules.count >= 128) {
            [self queueStop:@"本場一次性技能紀錄已達上限，請離開本場後再開始"]; return;
        }
        _skillRule = i; _skillBefore = _live; _phase = @"skillArming";
        _ruleDecisions[i] = [NSString stringWithFormat:@"觸發 · %@", trigger];
        [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 觸發 · %@",
            (unsigned long)_autoTurn, (unsigned long)i + 1, trigger]];
        _message = [NSString stringWithFormat:@"連續第 %lu 回合 · 規則 %lu：準備使用位置 %lu 技能",
            (unsigned long)_autoTurn, (unsigned long)i + 1, (unsigned long)slot + 1];
        return;
    }
    // Defer this check until other eligible skills and grouped alternatives
    // had their chance to heal, cleanse or haste, then reevaluate the trigger.
    if (unmetRequired) {
        for (NSUInteger i = 0; i < _skillRules.count; ++i) if (unmetRequired & (1u << i)) {
            [self queueStop:[NSString stringWithFormat:@"必要規則 %lu 未完成：%@；已停止轉珠",
                (unsigned long)i + 1, _ruleDecisions[i]]]; return;
        }
    }
    if (!PHOrbCanPreview(&_live)) {
        NSString *obstacles = PHOrbObstacleDescription(&_live);
        [self queueStop:[NSString stringWithFormat:@"技能規則處理後仍無法轉珠：%@。請解除效果或手動處理後再開始。",
            obstacles.length ? obstacles : @"盤面或操作條件尚未確認"]]; return;
    }
    [self beginSolve];
}
- (void)queueExecute:(NSUInteger)generation {
    if (![_phase isEqual:@"preview"] || generation != _generation || !_frame.enabled || _inputFault || !_route.goalsMet) return;
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
    _message = _automatic ? [NSString stringWithFormat:@"連續第 %lu 回合 · 準備轉珠；點浮動「停止」可中斷", (unsigned long)_autoTurn] :
        @"準備執行一次；點浮動「停止」即可中斷";
}
- (void)beginSolve {
    if (_automatic) [self cancelWorker]; // Each automatic route has its own UI/worker generation.
    _readQueued = false; _preview = _live;
    PHOrbSolveOptions options = OptionsForQuality(_quality);
    _goalPlan = [_goalMode isEqual:@"leaders"] ? PHOrbPlanForLeaders(&_preview) :
        @{@"available":@YES, @"goals":[_goalMode isEqual:@"manual"] ? _manualGoals : @[],
          @"message":PHOrbGoalsDescription([_goalMode isEqual:@"manual"] ? _manualGoals : @[]), @"warning":@"", @"leaders":@[]};
    if (![_goalPlan[@"available"] boolValue] || !PHOrbReadGoals(_goalPlan[@"goals"], &options.goals)) {
        _phase = @"blocked"; _message = _goalPlan[@"message"] ?: @"求路目標無法解析";
        if (_automatic) [self queueStop:_message];
        return;
    }
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
            if ((result != PHOrbSolveStatus::Solved && result != PHOrbSolveStatus::GoalsUnmet) || route.count < 2) {
                session->_phase = @"stopped";
                session->_message = result == PHOrbSolveStatus::InsufficientOrbs ?
                    @"目前珠數不足以達成目標；可先使用技能改盤，或調整目標後重新讀盤。" : @"本次未取得有效路徑，請重新讀盤";
                if (session->_automatic) [session queueStop:session->_message];
                return;
            }
            PHOrbBoard replay{};
            PHOrbEvaluation evaluation{};
            if (!PHOrbReplay(&board, &route, route.count - 1, &replay) || !PHOrbEvaluate(replay, options.goals, evaluation) ||
                evaluation.goalsMet != route.goalsMet || evaluation.combos != route.combos ||
                evaluation.erased != route.erased || evaluation.matchedGoals != route.matchedGoals || route.goalCount != options.goals.count ||
                !std::equal(std::begin(evaluation.goalHits), std::end(evaluation.goalHits), route.goalHits)) {
                [session queueStop:@"路徑重播校驗失敗，已停止"]; session->_phase = @"fault";
                return;
            }
            session->_route = route; session->_phase = @"preview"; session->_previewAt = session->_now;
            NSString *summary = [NSString stringWithFormat:@"預計 %u Combo（不含天降／加算）· %u 步 · 約 %.1f 秒",
                route.combos, route.count - 1, (route.count - 1) * StepDuration(session->_preview)];
            session->_message = !route.goalsMet ? [NSString stringWithFormat:@"目標僅達成 %u／%u 項，未開放執行。可提高求路品質或調整目標。\n%@",
                route.matchedGoals, route.goalCount, summary] : route.goalCount ?
                [NSString stringWithFormat:@"盤面目標已達成 %u／%u 項\n%@", route.matchedGoals, route.goalCount, summary] : summary;
            if (session->_automatic && !route.goalsMet) [session queueStop:session->_message];
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
    if (_automatic && ![self executing]) {
        PHOrbSnapshot current{}; NSString *reason = nil;
        BOOL read = [_engine readAutomation:&current frame:frame reason:&reason];
        if (![self observeAutomation:&current read:read reason:reason]) return;
        if ([_phase isEqual:@"skillArming"]) {
            NSUInteger slot = [_skillRules[_skillRule][@"slot"] unsignedIntegerValue] - 1;
            // The engine rereads once more immediately before its native call.
            PHOrbSkillResult result = [_engine useSkill:slot expected:&_skillBefore frame:frame reason:&reason];
            NSDictionary *rule = _skillRules[_skillRule];
            // Rejection can include an uncertain native start. A once rule
            // must not be retried by stopping and immediately starting again.
            if (result != PHOrbSkillUnavailable && [rule[@"once"] boolValue]) [_onceRules addObject:rule];
            NSUInteger group = [rule[@"group"] unsignedIntegerValue];
            if (result != PHOrbSkillUnavailable) {
                _usedRules |= 1u << _skillRule; _usedSlots |= 1u << slot;
                if (group) _usedGroups |= 1u << (group - 1);
            }
            if (result == PHOrbSkillRejected) { [self queueStop:reason ?: @"技能啟動校驗失敗，已停止"]; return; }
            if (result == PHOrbSkillUnavailable) {
                _unavailableRules |= 1u << _skillRule;
                _unavailableReasons[@(_skillRule)] = reason ?: @"技能不可用";
                [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 略過 · %@",
                    (unsigned long)_autoTurn, (unsigned long)_skillRule + 1, reason ?: @"技能不可用"]];
                [self waitForAutomationBoard];
            } else {
                if (group) {
                    _groupNext[group - 1] = (uint8_t)((_skillRule + 1) % _skillRules.count);
                }
                _skillAcknowledged = false;
                _phase = @"skillWaiting"; _autoPhaseAt = time; _hasStable = false;
                _message = [NSString stringWithFormat:@"位置 %lu 技能已啟動，等待遊戲完成…", (unsigned long)slot + 1];
            }
        }
        return;
    }
    if (_inputFault || ![self executing]) return;
    NSString *reason = nil;
    PHOrbSnapshot current{};
    BOOL read = _automatic ? [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    if (!read || !current.valid || current.multiplayerDisabled ||
        !PHOrbSameTurn(&_expected, &current) || current.phase != 2) {
        // The private manager has already been removed. Never inject stale
        // cleanup into a new scene/multiplayer; native input now owns release.
        [self queueStop:@"回合、場景或可操作狀態已改變，已停止"];
        _ownedOrb = 0; _phase = @"stopped";
        return;
    }
    _live = current; _hasLive = true;
    if (_automatic && (![self observeAutomation:&current read:read reason:reason] || current.automation.busy)) {
        if (_automatic) [self queueStop:@"轉珠前技能或動畫狀態已改變，已停止"];
        // Fall through to the existing owned-orb release path when needed.
    }
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
            _ownedOrb = 0; [self queueStop:@"遊戲已接管放開，已停止"]; return;
        }
        _expected = current;
        // Use the dragged orb's actual cell when a partial move was rejected.
        NSUInteger cell = 0;
        const NSUInteger cells = current.board.width * current.board.height;
        for (; cell < cells; ++cell) if (current.board.tokens[cell] == _ownedOrb) break;
        _staged = [_engine stageCell:cell down:NO beginning:NO expected:&current frame:frame reason:&reason];
        _releaseStaged = _staged;
        if (!_staged) { _ownedOrb = 0; [self queueStop:reason ?: @"已停止輸入，等待遊戲結束拖珠"]; _phase = @"stopped"; }
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
    if (!active && !_automatic && time - _lastRead < 0.10) return;
    _lastRead = time;
    PHOrbSnapshot current{}; NSString *reason = nil;
    BOOL read = _automatic ? [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    _hasLive = read; _live = current;
    if (!_automatic) [self observeRuleHistory:&current read:read];
    if (_automatic && ![self observeAutomation:&current read:read reason:reason]) return;
    if (active && _staged) {
        if (!read || !current.valid || current.multiplayerDisabled || !PHOrbSameTurn(&_expected, &current)) {
            [self queueStop:@"原生執行後場景已改變，已停止"]; _ownedOrb = 0; _phase = @"stopped"; return;
        }
        if (_releaseStaged) {
            if (current.dragged == 0) {
                _ownedOrb = 0;
                _phase = _releaseComplete ? @"complete" : @"stopped";
                if (_releaseComplete) {
                    ++_completed; _message = @"本次路徑已執行並放開；下一回合請重新讀盤";
                    if (_automatic) {
                        ++_autoMoves; _sawResolve = ResolvingOrbs(current.phase); _hasStable = false;
                        _phase = @"turnWaiting"; _autoPhaseAt = time;
                        _message = @"本回合已放開，等待消除、敵人行動與下一回合…";
                    }
                }
            } else if (time - _startedAt > ExecutionLimit + 0.5) {
                _ownedOrb = 0; [self queueStop:@"已停止輸入；遊戲尚未確認放開，請手動檢查"]; _phase = @"stopped";
            }
            return;
        }
        // The game may replace a leader inside the original native frame.
        // Check the captured plan before an accepted swap updates _expected.
        if (!PHOrbSameLeaders(&_preview, &current)) {
            if (_awaitingStart && current.dragged == _preview.board.tokens[_route.cells[0]])
                _ownedOrb = current.dragged;
            [self queueStop:@"隊長或助人已改變，已停止；請重新讀盤"];
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
    if (_automatic && [_phase isEqual:@"preview"]) {
        if (!HasRouteTime(_live, _route)) [self queueStop:@"自動路徑超出操作時間，已停止"];
        else [self queueExecute:_generation];
    }
    [self advanceAutomation];
}
- (NSDictionary *)snapshot {
    NSMutableArray *path = [NSMutableArray array], *goalHits = [NSMutableArray array];
    for (unsigned i = 0; i < _route.count; ++i) [path addObject:@(_route.cells[i])];
    for (unsigned i = 0; i < _route.goalCount && i < PHOrbMaxGoals; ++i) [goalHits addObject:@(_route.goalHits[i])];
    BOOL preview = [_phase isEqual:@"preview"];
    BOOL canExecute = preview && _route.goalsMet && _frame.enabled && !_inputFault && _hasLive && PHOrbCanPreview(&_live) &&
        SamePosition(_live, _preview) && HasRouteTime(_live, _route);
    return @{@"phase":_phase, @"message":_message, @"generation":@(_generation),
        @"canRead":@(_frame.ready && !_automatic && ![self executing] && !_inputFault), @"canExecute":@(canExecute && !_automatic),
        @"canStart":@(_frame.ready && _frame.enabled && !_automatic && ![self executing] && !_inputFault),
        @"busy":@(_automatic || [self executing]), @"canStop":@(_automatic || [self executing] || _readQueued || preview || [_phase isEqual:@"solving"]),
        @"automatic":@(_automatic), @"autoTurn":@(_autoTurn), @"autoMoves":@(_autoMoves), @"autoSkills":@(_autoSkills),
        @"rules":_skillRules, @"automationLog":[_automationLog copy], @"enemyEffects":@(_live.automation.enemyEffects),
        @"ruleDecisions":[_ruleDecisions copy], @"decisionTurn":@(_decisionTurn),
        @"playerStatus":PHOrbSurvivalDescription(&_live), @"confirmedSkillRules":@(_confirmedRules),
        @"obstacles":PHOrbObstacleDescription(&_live), @"usedSkillGroups":@(_usedGroups),
        @"activeProfile":_activeProfile,
        @"profileContext":@{@"known":@(_hasLive && _live.valid && _live.present && !_live.multiplayerDisabled),
            @"dungeon":@(_live.identity.dungeon), @"floor":@(_live.identity.floor)},
        @"playerBypass":@(_live.automation.playerBypass), @"skillWindow":@(_live.automation.window),
        @"enabled":@(_frame.ready && _frame.enabled), @"board":BoardJSON(_route.count ? _preview.board : _live.board), @"path":path,
        @"quality":_quality, @"searchSteps":@(_solveOptions.steps), @"searchBeam":@(_solveOptions.beam),
        @"goalMode":_goalMode, @"goals":_manualGoals, @"goalPlan":_goalPlan,
        @"goalCount":@(_route.goalCount), @"matchedGoals":@(_route.matchedGoals), @"goalsMet":@(_route.goalsMet),
        @"goalHits":goalHits,
        @"leadersValid":@(_live.leadersValid), @"leaderNumbers":@[@(_live.leaderNumbers[0]), @(_live.leaderNumbers[1])],
        @"searchBudgetMS":@(_solveOptions.milliseconds),
        @"combos":@(_route.combos), @"steps":@(_route.count ? _route.count - 1 : 0), @"confirmed":@(_confirmed),
        @"elapsedMS":@(_route.elapsedMS), @"evaluated":@(_route.evaluated), @"deadline":@(_route.deadlineReached),
        @"completed":@(_completed), @"cancelled":@(_cancelled), @"inputFrames":@(_sentFrames), @"inputFault":@(_inputFault),
        @"multiplayerDisabled":@(_live.multiplayerDisabled), @"nativePhase":@(_live.phase), @"wave":@(_live.wave),
        @"moves":@(_live.moves), @"remainingFrames":@(_live.remainingFrames), @"fps":@(_live.fps),
        @"settled":@(_live.settled), @"ordinary":@(_live.ordinary), @"deviceVerified":@NO};
}
@end
