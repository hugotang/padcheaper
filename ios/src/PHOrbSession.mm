#import "PHOrbSession.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#include "PHOrbSolver.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>

namespace {
constexpr double StepSeconds = 0.065, PreviewLifetime = 45, ExecutionLimit = 4, TimeMargin = 0.40;
constexpr double AutomationSettle = 0.20, SkillTimeout = 30, TurnTimeout = 90;
bool ResolvingOrbs(uint32_t phase) { return phase >= 3 && phase <= 6; }
bool SamePosition(const PHOrbSnapshot &a, const PHOrbSnapshot &b) {
    return PHOrbSameTurn(&a, &b) && PHOrbSameLeaders(&a, &b) && a.moves == b.moves && PHOrbSamePlanBoard(&a.board, &b.board);
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
    NSMutableArray *spinners = [NSMutableArray array], *effects = [NSMutableArray array], *blindTurns = [NSMutableArray array];
    NSMutableArray *weakened = [NSMutableArray array], *thornDamage = [NSMutableArray array];
    const uint64_t blocked = PHOrbBlockedCells(&board);
    NSMutableArray *tape = [NSMutableArray array], *cloud = [NSMutableArray array], *blockedCells = [NSMutableArray array];
    const unsigned size = std::min(unsigned(PHOrbMaxCells), unsigned(board.width) * board.height);
    for (unsigned i = 0; i < size; ++i) {
        float enhancement = 0; std::memcpy(&enhancement, &board.enhancement[i], sizeof(enhancement));
        [types addObject:@(board.types[i])]; [enhanced addObject:@(enhancement > 0)];
        [weakened addObject:@(enhancement < 0)]; [effects addObject:@(board.flags[i])];
        [blindTurns addObject:@(board.blindTurns[i])]; [thornDamage addObject:@(board.thornDamage[i])];
        [locked addObject:@((board.flags[i] & PHOrbFlagLocked) != 0)];
        [spinners addObject:@((board.spinnerMask & (UINT64_C(1) << i)) != 0)];
        [tape addObject:@((board.tapeMask & (UINT64_C(1) << i)) != 0)];
        [cloud addObject:@((board.cloudMask & (UINT64_C(1) << i)) != 0)];
        [blockedCells addObject:@((blocked & (UINT64_C(1) << i)) != 0)];
    }
    return @{@"width":@(board.width), @"height":@(board.height), @"types":types, @"enhanced":enhanced, @"locked":locked,
        @"spinners":spinners, @"weakened":weakened, @"effects":effects, @"blindTurns":blindTurns, @"thornDamage":thornDamage,
        @"tape":tape, @"cloud":cloud, @"blocked":blockedCells, @"fixedStart":@(board.fixedStart),
        @"unmatchableColors":@(board.unmatchableColors), @"minErase":@(board.minErase ?: 3), @"minMatch":@(board.minMatch)};
}
NSDictionary *BoardTrace(const PHOrbBoard &board) {
    NSMutableArray *types = [NSMutableArray array], *flags = [NSMutableArray array], *enhancement = [NSMutableArray array];
    NSMutableArray *thorns = [NSMutableArray array], *blind = [NSMutableArray array];
    const unsigned size = std::min(unsigned(PHOrbMaxCells), unsigned(board.width) * board.height);
    for (unsigned i = 0; i < size; ++i) {
        [types addObject:@(board.types[i])]; [flags addObject:@(board.flags[i])];
        [enhancement addObject:@(board.enhancement[i])]; [thorns addObject:@(board.thornDamage[i])];
        [blind addObject:@(board.blindTurns[i])];
    }
    // Row-major zero-based cells, with no process pointers or account data.
    return @{@"width":@(board.width), @"height":@(board.height), @"types":types, @"flags":flags,
        @"enhancementBits":enhancement, @"thornPercent":thorns, @"blindTurns":blind,
        @"spinnerMask":@(board.spinnerMask), @"spinnerColors":@(board.spinnerColors),
        @"tapeMask":@(board.tapeMask), @"cloudMask":@(board.cloudMask), @"fixedStart":@(board.fixedStart),
        @"unmatchableColors":@(board.unmatchableColors), @"minErase":@(board.minErase ?: 3), @"minMatch":@(board.minMatch)};
}
NSArray *RouteCells(const PHOrbRoute &route) {
    NSMutableArray *cells = [NSMutableArray array];
    for (unsigned i = 0; i < route.count; ++i) [cells addObject:@(route.cells[i])];
    return cells;
}
NSDictionary *EvaluationTrace(const PHOrbEvaluation &value, const PHOrbGoalSet &goals) {
    NSMutableArray *hits = [NSMutableArray array];
    for (unsigned g = 0; g < goals.count; ++g) [hits addObject:@(value.goalHits[g])];
    return @{@"valid":@YES, @"scope":@"all-shared-spinner-phases", @"goalsMet":@(value.goalsMet), @"goalHits":hits, @"combos":@(value.combos),
        @"hazardsSafe":@(value.hazardsSafe), @"poisonErased":@(value.poisonErased),
        @"unmatchedBombs":@(value.unmatchedBombs), @"heartFours":@(value.heartFours)};
}
NSString *SolveStatusName(PHOrbSolveStatus status) {
    switch (status) {
        case PHOrbSolveStatus::Solved: return @"solved";
        case PHOrbSolveStatus::GoalsUnmet: return @"goals-unmet";
        case PHOrbSolveStatus::Unsafe: return @"unsafe";
        case PHOrbSolveStatus::InsufficientOrbs: return @"insufficient-orbs";
        case PHOrbSolveStatus::NoRoute: return @"no-route";
        case PHOrbSolveStatus::Cancelled: return @"cancelled";
        case PHOrbSolveStatus::Invalid: return @"invalid";
        case PHOrbSolveStatus::Failed: return @"failed";
    }
    return @"unknown";
}
NSString *PoisonFailureContext(const PHOrbBoard &board) {
    unsigned poison = 0, mortal = 0;
    for (unsigned i = 0; i < board.width * board.height && i < PHOrbMaxCells; ++i) {
        poison += board.types[i] == PHOrbPoison; mortal += board.types[i] == PHOrbMortalPoison;
    }
    return poison || mortal ? [NSString stringWithFormat:
        @"讀盤時仍有毒 %u 顆／猛毒 %u 顆；本次未找到同時達成目標且不消毒的路徑。可先使用能轉走毒珠的技能。\n", poison, mortal] : @"";
}
NSDictionary *SkillDecisionContext(const PHOrbSnapshot &state, NSUInteger slot, bool attempted = false) {
    const auto &member = state.automation.members[slot];
    unsigned counts[PHOrbTypes]{};
    bool known = state.automation.boardKnown && PHOrbDimensionsSupported(state.board.width, state.board.height);
    if (known) for (unsigned i = 0; i < state.board.width * state.board.height; ++i) {
        if (state.board.types[i] >= PHOrbTypes) known = false;
        else ++counts[state.board.types[i]];
    }
    NSMutableArray *orbCounts = [NSMutableArray array];
    if (known) for (unsigned count : counts) [orbCounts addObject:@(count)];
    return @{@"number":@(member.number), @"state":@(member.state), @"charge":@(member.charge),
        @"identityNumber":@(member.identityNumber), @"identityAssist":@(member.identityAssist),
        @"temporaryKnown":@(member.temporaryKnown), @"temporaryID":@(member.temporaryID), @"temporaryTurns":@(member.temporaryTurns),
        @"selection":@(member.selection), @"active":@(member.active), @"bound":@(member.bound),
        @"skillStages":@[@(member.skillStage[0]), @(member.skillStage[1])],
        @"cachedSkillStages":@[@(member.cachedSkillStage[0]), @(member.cachedSkillStage[1])],
        @"skillLevels":@[@(member.skillLevel[0]), @(member.skillLevel[1])],
        @"memberSkillUses":@[@(member.skillUses[0]), @(member.skillUses[1])],
        @"assistCatalogID":@(member.assistCatalogID), @"orbCounts":orbCounts,
        @"boardWidth":@(state.board.width), @"boardHeight":@(state.board.height),
        @"spinnerMask":@(state.board.spinnerMask), @"observedPhase":@(state.phase),
        @"observedWave":@(state.wave), @"observedSkillUses":@(state.automation.skillUses), @"nativeAttempt":@(attempted)};
}
}

@implementation PHOrbSession {
    id<PHOrbEngine> _engine;
    dispatch_queue_t _worker;
    std::shared_ptr<std::atomic_bool> _cancel;
    PHBattleFrame _frame;
    PHOrbSnapshot _live, _stable, _preview, _expected, _decisionState;
    PHOrbRoute _route;
    NSString *_phase, *_message, *_quality, *_goalMode;
    NSArray *_manualGoals;
    NSArray *_skillRules, *_historyRules, *_decisionRules;
    NSMutableArray *_automationLog;
    NSMutableArray<NSString *> *_ruleDecisions;
    NSMutableDictionary<NSNumber *, NSString *> *_unavailableReasons;
    NSMutableDictionary<NSNumber *, NSDictionary *> *_skillAttemptContexts;
    NSMutableArray<NSDictionary *> *_decisionContexts;
    NSDictionary *_goalPlan, *_recovery;
    NSDictionary *_profileLibrary, *_activeProfile;
    NSDictionary *_openingProfile;
    NSMutableArray<NSDictionary *> *_openingLog;
    NSUInteger _openingStep;
    uint8_t _temporaryMask;
    bool _openingInFlight, _openingAcknowledged, _openingStarted;
    PHOrbSnapshot _openingBefore;
    NSDictionary *_searchTrace;
    NSMutableArray<NSDictionary *> *_executionHistory;
    bool _recordingExecution;
    PHOrbSolveOptions _solveOptions;
    NSUInteger _generation, _completed, _cancelled, _sentFrames, _confirmed;
    NSUInteger _target;
    uintptr_t _ownedOrb;
    double _now, _lastRead, _requestedAt, _stableAt, _previewAt, _startedAt, _stepAt, _sentAt, _frameAt, _executionElapsed;
    bool _hasLive, _hasStable, _readQueued, _staged, _releaseStaged, _releaseComplete;
    bool _inputFault, _awaitingStart, _startSent, _monitoring, _oneShot, _oneShotBound;
    PHBattleIdentity _oneShotIdentity;
    uint32_t _oneShotWave;
    PHOrbSnapshot _autoOrigin, _skillBefore;
    NSUInteger _autoTurn, _autoMoves, _autoSkills, _skillRule, _decisionTurn, _recoveryTurn;
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
        _recovery = PHOrbDefaultRecovery();
        _ruleDecisions = [NSMutableArray array]; _unavailableReasons = [NSMutableDictionary dictionary]; _decisionRules = @[];
        _skillAttemptContexts = [NSMutableDictionary dictionary];
        _decisionContexts = [NSMutableArray array];
        _profileLibrary = PHOrbDefaultProfiles(); _activeProfile = @{};
        _openingLog = [NSMutableArray array];
        _searchTrace = @{}; _executionHistory = [NSMutableArray array];
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
- (void)clearRoute {
    _route = {}; _confirmed = _target = 0;
    _startSent = _awaitingStart = false;
    _executionElapsed = 0;
    _recordingExecution = false;
}
- (void)recordExecution:(NSString *)status state:(const PHOrbSnapshot *)state evaluation:(NSDictionary *)evaluation {
    if (!_recordingExecution || !_executionHistory.count) return;
    NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
    record[@"status"] = status; record[@"confirmedSteps"] = @(_confirmed);
    if (state) {
        record[@"observedPhase"] = @(state->phase);
        // Phase 3+ readers deliberately omit orb objects. Keep the captured
        // pre-release board rather than replacing it with an empty read.
        if (state->valid && state->phase == 2 && PHOrbSameTurn(&_preview, state))
            record[@"observedBoard"] = BoardTrace(state->board);
    }
    if (evaluation) record[@"releaseEvaluation"] = evaluation;
    record[@"elapsedMS"] = @(unsigned(_executionElapsed * 1000));
    _executionHistory[_executionHistory.count - 1] = [record copy];
}
- (void)beginExecutionRecord:(const PHOrbSnapshot *)state {
    PHOrbBoard predicted{};
    if (!PHOrbReplay(&_preview.board, &_route, _route.count - 1, &predicted)) return;
    NSDictionary *record = @{@"schema":@1, @"generation":@(_generation),
        @"turn":@(_automatic ? _autoTurn : 0), @"wave":@(_preview.wave),
        @"dungeon":@(_preview.identity.dungeon), @"floor":@(_preview.identity.floor),
        @"status":@"drag-accepted", @"goals":PHOrbGoalsJSON(&_solveOptions.goals),
        @"startBoard":BoardTrace(_preview.board), @"path":RouteCells(_route),
        @"predictedBoard":BoardTrace(predicted), @"observedBoard":BoardTrace(state->board),
        @"confirmedSteps":@0, @"releaseEvaluation":@{}, @"releaseConfirmed":@NO};
    if (_executionHistory.count >= 3) [_executionHistory removeObjectAtIndex:0];
    [_executionHistory addObject:record]; _recordingExecution = true;
}
- (BOOL)configureQuality:(NSString *)quality {
    if (!quality) return NO;
    return [self configureSettings:@{@"quality":quality, @"mode":_goalMode, @"goals":_manualGoals, @"rules":_skillRules, @"recovery":_recovery}];
}
- (BOOL)configureSettings:(NSDictionary *)settings {
    NSDictionary *canonical = PHOrbCanonicalSettings(settings);
    if (!canonical || _automatic || _oneShot || [self executing] || _inputFault) return NO;
    if ([_quality isEqual:canonical[@"quality"]] && [_goalMode isEqual:canonical[@"mode"]] &&
        [_manualGoals isEqual:canonical[@"goals"]] && [_skillRules isEqual:canonical[@"rules"]] &&
        [_recovery isEqual:canonical[@"recovery"]]) return YES;
    [self cancelWorker];
    [self loadSettings:canonical];
    _readQueued = _hasStable = false;
    _phase = @"idle"; _message = @"求路設定已變更，請重新讀盤";
    return YES;
}
- (void)loadSettings:(NSDictionary *)settings {
    _quality = [settings[@"quality"] copy]; _goalMode = [settings[@"mode"] copy];
    _manualGoals = settings[@"goals"]; _goalPlan = @{}; [self clearRoute];
    _solveOptions = OptionsForQuality(_quality);
    _skillRules = settings[@"rules"];
    _recovery = settings[@"recovery"];
    // Keep the last run's decisions with their captured rules. Idle runtime
    // reloads can restore common settings after an automatically chosen
    // profile stops; they must not erase the reason the run stopped.
    _recoveryTurn = 0;
    _watchPlayerState = [_recovery[@"hpAtMost"] unsignedIntegerValue] > 0;
    for (NSDictionary *rule in _skillRules) if ([rule[@"enabled"] boolValue])
        _watchPlayerState |= [rule[@"hpAtMost"] unsignedIntegerValue] > 0 || [rule[@"required"] boolValue] ||
            (rule[@"status"] && ![rule[@"status"] isEqual:@"none"]);
}
- (BOOL)configureProfiles:(NSDictionary *)library {
    if (!PHOrbProfilesValid(library) || _automatic || _oneShot || [self executing] || _inputFault) return NO;
    if ([_profileLibrary isEqual:library]) return YES;
    NSData *data = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
    NSDictionary *saved = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    if (!saved) return NO;
    [self cancelWorker]; _profileLibrary = saved; _activeProfile = @{};
    [self clearRoute]; _readQueued = _hasStable = false;
    _phase = @"idle"; _message = @"策略已儲存；按開始時核對隊伍與關卡";
    return YES;
}
- (void)stopForLifecycle:(NSString *)reason {
    const bool intent = _automatic || _oneShot || _readQueued || [_phase isEqual:@"solving"] ||
        [_phase isEqual:@"preview"] || [_phase isEqual:@"arming"] || [_phase isEqual:@"running"] ||
        ([_phase isEqual:@"releasing"] && _releaseComplete);
    if (intent) [self queueStop:reason];
    // An aborted gesture may still await release. Keep that state and its
    // original error; the next native frame will complete the release.
}
- (void)queueStop:(NSString *)reason {
    const bool wasAutomatic = _automatic;
    const bool skillRunning = [_phase isEqual:@"skillWaiting"] || _openingInFlight;
    const bool cancelledIntent = _automatic || _readQueued || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        [_phase isEqual:@"arming"] || [_phase isEqual:@"running"] ||
        ([_phase isEqual:@"releasing"] && _releaseComplete);
    [self cancelWorker];
    _automatic = _oneShot = _oneShotBound = false;
    _readQueued = false; _hasStable = false;
    _message = reason ?: @"已停止";
    if (skillRunning) _message = [_message stringByAppendingString:@"；已啟動的技能由遊戲完成"];
    if (_recordingExecution) {
        [self recordExecution:@"cancelled" state:_hasLive ? &_live : nullptr evaluation:nil];
        NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
        if (!record[@"reason"]) record[@"reason"] = _message;
        _executionHistory[_executionHistory.count - 1] = [record copy];
    }
    if (wasAutomatic) [self logAutomation:[NSString stringWithFormat:@"連續模式停止：%@", _message]];
    _releaseComplete = false;
    if (_ownedOrb || _staged) _phase = @"releasing";
    else { _phase = @"stopped"; _recordingExecution = false; }
    if (cancelledIntent) ++_cancelled;
}
- (void)queueRead {
    if (_automatic || _oneShot || [self executing] || _inputFault || _openingInFlight) return;
    [self cancelWorker];
    _monitoring = _readQueued = true; _hasStable = false; _requestedAt = _now;
    [self clearRoute]; _goalPlan = @{}; _phase = @"reading"; _message = @"等待盤面穩定…";
}
- (void)queueOnce {
    if (_automatic || _oneShot || [self executing] || _inputFault || _openingInFlight || !_frame.ready || !_frame.enabled) return;
    [self queueRead];
    _oneShot = true; _oneShotBound = false;
    if (_hasLive && _live.valid && _live.present) {
        _oneShotIdentity = _live.identity; _oneShotWave = _live.wave; _oneShotBound = true;
    }
    _message = @"轉一次：等待盤面穩定，求路成功後執行";
}
- (void)queueAutomatic {
    if (_automatic || _oneShot || [self executing] || _inputFault || !_frame.ready || !_frame.enabled) return;
    [self cancelWorker];
    _automatic = _monitoring = true; _autoBound = _sawResolve = _hasStable = _readQueued = false;
    _autoTurn = 1; _autoMoves = _autoSkills = 0; _unavailableRules = 0;
    _autoStartAt = _autoPhaseAt = _now; _autoOrigin = {}; [self clearRoute]; _goalPlan = @{};
    _activeProfile = @{};
    [_automationLog removeAllObjects];
    [_ruleDecisions removeAllObjects]; [_unavailableReasons removeAllObjects];
    [_skillAttemptContexts removeAllObjects];
    [_decisionContexts removeAllObjects];
    _decisionRules = @[]; _decisionState = {}; _decisionTurn = _recoveryTurn = 0;
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
        _openingProfile = nil; _openingStep = 0; _temporaryMask = 0; _openingInFlight = _openingAcknowledged = _openingStarted = false;
        [_openingLog removeAllObjects];
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
        _openingProfile = nil; _openingStep = 0; _temporaryMask = 0; _openingInFlight = _openingAcknowledged = _openingStarted = false;
        [_openingLog removeAllObjects];
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
        NSDictionary *retained = _openingProfile ? PHOrbProfileNamed(_profileLibrary, _openingProfile[@"id"]) : nil;
        const bool resume = retained && [retained[@"formation"] isEqual:_openingProfile[@"formation"]] &&
            [retained[@"opening"] isEqual:_openingProfile[@"opening"]] &&
            [retained[@"dungeon"] isEqual:_openingProfile[@"dungeon"]] && [retained[@"floor"] isEqual:_openingProfile[@"floor"]] &&
            ([_profileLibrary[@"autoSelect"] boolValue] || [_profileLibrary[@"selected"] isEqual:retained[@"id"]]);
        NSDictionary *resolved = resume ? @{@"ok":@YES, @"profile":retained, @"message":retained[@"name"]} :
            PHOrbResolveProfile(_profileLibrary, state);
        if (![resolved[@"ok"] boolValue]) { [self queueStop:resolved[@"message"]]; return NO; }
        _activeProfile = resolved[@"profile"];
        if (_activeProfile[@"formation"]) {
            if (!resume && _openingInFlight) {
                [self queueStop:@"前一次開場技能尚未確認完成，不能切換流程或跳過此步"]; return NO;
            }
            if (!resume && [_activeProfile[@"opening"] count] && _openingProfile && _openingStep) {
                [self queueStop:@"本場已有不同的開場紀錄，不能以另一份流程重播開場；請使用不含開場的策略或下一場再開始"]; return NO;
            }
            if (!resume) {
                if ([_activeProfile[@"opening"] count] && (state->wave || state->moves || state->phase != 2 || state->automation.skillUses)) {
                    [self queueStop:@"開場只可在首層、尚未轉珠及施放技能時開始；中途加入請使用不含開場的策略"]; return NO;
                }
                _openingProfile = _activeProfile; _openingStep = 0; _openingInFlight = _openingAcknowledged = _openingStarted = false;
                [_openingLog removeAllObjects];
            }
            const bool openingDone = resume && _openingStep == [_openingProfile[@"opening"] count];
            if (!_openingInFlight && !(openingDone ? PHOrbProfileMatchesRunningTeam(_openingProfile, state) :
                PHOrbProfileMatchesOpeningTeam(_openingProfile, state, _openingStep))) {
                [self queueStop:@"目前本體／武裝與已確認的開場進度不符，未重播技能"]; return NO;
            }
            if (_openingInFlight && !_openingStarted) {
                [self queueStop:@"前一次開場技能啟動結果不明，本場不能重試或跳過此步；請手動完成本場"]; return NO;
            }
            if (_openingInFlight) _phase = @"openingWaiting";
        } else if (_openingInFlight) {
            [self queueStop:@"前一次開場技能尚未確認完成，請等待原生技能結束；不會啟動另一份策略"]; return NO;
        }
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
    if (_openingInFlight && _openingStarted && state->automation.skillUses == _openingBefore.automation.skillUses + 1)
        _openingAcknowledged = true;
    return YES;
}
- (BOOL)finishOpeningIfConfirmed:(const PHOrbSnapshot *)state {
    if (!_openingInFlight || !_openingStarted || !_openingAcknowledged || state->phase != 2 || !state->settled ||
        state->automation.busy || state->dragged ||
        (state->wave == _openingBefore.wave && state->automation.skillUses != _openingBefore.automation.skillUses + 1) ||
        !PHOrbProfileMatchesOpeningTeam(_openingProfile, state, _openingStep + 1)) return NO;
    NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"confirmed";
    record[@"usesAfter"] = @(state->automation.skillUses);
    _openingLog[_openingLog.count - 1] = [record copy];
    ++_openingStep; _openingInFlight = false;
    [self logAutomation:[NSString stringWithFormat:@"開場 %lu／%lu 已確認完成", (unsigned long)_openingStep,
        (unsigned long)[_openingProfile[@"opening"] count]]];
    return YES;
}
- (void)advanceAutomation {
    if (!_automatic || [self executing] || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        [_phase isEqual:@"skillArming"] || [_phase isEqual:@"openingArming"] || _now - _autoStartAt < 0.35) return;
    if ([_phase isEqual:@"turnWaiting"]) {
        _sawResolve |= ResolvingOrbs(_live.phase);
        if (_now - _autoPhaseAt > TurnTimeout) { [self queueStop:@"等候下一回合逾時，請檢查遊戲畫面"]; return; }
        if (!_sawResolve || _live.phase != 2 || _live.moves != 0 || _live.automation.busy || !_live.settled) {
            _hasStable = false; return;
        }
    } else if ([_phase isEqual:@"openingWaiting"]) {
        if (_now - _autoPhaseAt > SkillTimeout) { [self queueStop:@"開場技能等候逾時；保留進度，不會重複施放"]; return; }
        if (_live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
        if (!_openingAcknowledged) {
            _openingStarted = false;
            NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"unconfirmed";
            _openingLog[_openingLog.count - 1] = [record copy];
            [self queueStop:@"開場技能使用次數未確認；已停止，不能重播此步"]; return;
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
    if ([_phase isEqual:@"openingWaiting"]) {
        if (![self finishOpeningIfConfirmed:&_live]) {
            [self queueStop:@"開場技能完成後的次數、角色或武裝不符合預期；保留進度，未執行下一步"]; return;
        }
        ++_autoSkills; [self waitForAutomationBoard]; return;
    }
    if ([_phase isEqual:@"turnWaiting"]) {
        ++_autoTurn;
        [self waitForAutomationBoard]; return;
    }
    if ([_phase isEqual:@"skillWaiting"]) {
        ++_autoSkills; _unavailableRules = 0; // Haste may make a formerly unavailable member ready.
        [_unavailableReasons removeAllObjects];
        _confirmedRules |= 1u << _skillRule;
        _ruleDecisions[_skillRule] = @"本回合已確認完成";
        NSUInteger group = [_skillRules[_skillRule][@"group"] unsignedIntegerValue];
        if (group) _confirmedGroups |= 1u << (group - 1);
        [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 技能完成", (unsigned long)_autoTurn, (unsigned long)_skillRule + 1]];
        [self waitForAutomationBoard]; return;
    }
    if (_activeProfile[@"formation"]) {
        const bool openingDone = _openingStep == [_activeProfile[@"opening"] count];
        if (!(openingDone ? PHOrbProfileMatchesRunningTeam(_activeProfile, &_live) :
              PHOrbProfileMatchesOpeningTeam(_activeProfile, &_live, _openingStep))) {
            [self queueStop:@"隊伍形態或武裝與本次流程不符，已停止"]; return;
        }
        if (_openingStep < [_activeProfile[@"opening"] count]) {
            _openingBefore = _live; _phase = @"openingArming";
            _message = [NSString stringWithFormat:@"開場 %lu／%lu：準備使用位置 %@ %@技能",
                (unsigned long)_openingStep + 1, (unsigned long)[_activeProfile[@"opening"] count],
                _activeProfile[@"opening"][_openingStep][@"slot"],
                [_activeProfile[@"opening"][_openingStep][@"source"] isEqual:@"body"] ? @"本體" : @"武裝"];
            return;
        }
    }
    uint8_t temporaryMask = 0;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const auto &member = _live.automation.members[slot];
        if (member.temporaryTurns) {
            temporaryMask |= uint8_t(1u << slot);
            if (!(_temporaryMask & (1u << slot))) [self logAutomation:[NSString stringWithFormat:
                @"位置 %u 暫時變身：#%u → #%u；保留流程，略過原形態技能", slot + 1, member.identityNumber, member.number]];
        } else if (_temporaryMask & (1u << slot)) [self logAutomation:[NSString stringWithFormat:
            @"位置 %u 暫時變身已結束，恢復原技能循環", slot + 1]];
    }
    _temporaryMask = temporaryMask;
    if (_decisionTurn != _autoTurn) [_skillAttemptContexts removeAllObjects];
    [_ruleDecisions removeAllObjects]; _decisionTurn = _autoTurn;
    _decisionRules = _skillRules; _decisionState = _live;
    [_decisionContexts removeAllObjects];
    for (NSUInteger i = 0; i < _skillRules.count; ++i) {
        [_ruleDecisions addObject:@"尚未判斷"];
        NSUInteger slot = [_skillRules[i][@"slot"] unsignedIntegerValue] - 1;
        NSDictionary *attempt = ((_unavailableRules | _usedRules) & (1u << i)) ? _skillAttemptContexts[@(i)] : nil;
        [_decisionContexts addObject:attempt ?: SkillDecisionContext(_live, slot)];
    }
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
        else if (member->temporaryTurns && (!member->temporaryKnown ||
                 [rule[@"monster"] unsignedIntValue] != member->number))
            unavailable = [NSString stringWithFormat:@"暫時變身為 #%u，原形態技能暫停", member->number];
        else if (member->bound) unavailable = @"施放者被綁定";
        else if (member->state) unavailable = @"施放者尚未回到待命狀態";
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
    if (![_phase isEqual:@"preview"] || generation != _generation || !_frame.enabled || _inputFault || !_route.goalsMet || !_route.hazardsSafe) return;
    if (_now - _previewAt > PreviewLifetime || !_hasLive || !PHOrbCanPreview(&_live) || !SamePosition(_live, _preview)) {
        [self queueStop:@"盤面或回合已改變，請重新讀盤"];
        return;
    }
    if (!HasRouteTime(_live, _route)) {
        _message = @"操作時間不足，請重新讀盤取得較短路徑";
        return;
    }
    if (![self hasThornBudget:&_live]) {
        [self queueStop:@"荊棘路徑的 HP 或待結算傷害已改變，請重新求路"]; return;
    }
    _expected = _preview; _target = _confirmed = 0; _ownedOrb = 0;
    _awaitingStart = true; _startSent = false; _phase = @"arming"; _requestedAt = _now;
    _message = _automatic ? [NSString stringWithFormat:@"連續第 %lu 回合 · 準備轉珠；點浮動「停止」可中斷", (unsigned long)_autoTurn] :
        @"準備執行一次；點浮動「停止」即可中斷";
}
- (BOOL)hasThornBudget:(const PHOrbSnapshot *)state {
    if (!_route.thornContacts) return YES;
    PHOrbSolveOptions options; options.thorns = state->thorns;
    if (!PHOrbReadGoals(_goalPlan[@"goals"], &options.goals)) return NO;
    PHOrbEvaluation evaluation{}; uint32_t damage = 0; uint8_t contacts = 0;
    return PHOrbEvaluateRoute(state->board, _route, options, evaluation, damage, contacts) &&
        evaluation.goalsMet && evaluation.hazardsSafe && contacts == _route.thornContacts;
}
- (void)beginSolve {
    if (_automatic) [self cancelWorker]; // Each automatic route has its own UI/worker generation.
    // Progress belongs to the candidate below, not the previously executed
    // route. Clear it even when planning or the new search fails.
    [self clearRoute];
    _readQueued = false; _preview = _live;
    PHOrbSolveOptions options = OptionsForQuality(_quality);
    options.thorns = _preview.thorns;
    _goalPlan = [_goalMode isEqual:@"leaders"] ? PHOrbPlanForLeaders(&_preview) :
        @{@"available":@YES, @"goals":[_goalMode isEqual:@"manual"] ? _manualGoals : @[],
          @"message":PHOrbGoalsDescription([_goalMode isEqual:@"manual"] ? _manualGoals : @[]), @"warning":@"", @"leaders":@[]};
    _goalPlan = PHOrbPlanWithRecovery(_goalPlan, _recovery, &_preview, _automatic);
    if ([_goalMode isEqual:@"manual"] && _preview.automation.valid &&
        (_preview.automation.members[0].temporaryTurns || _preview.automation.members[5].temporaryTurns)) {
        NSMutableDictionary *plan = [_goalPlan mutableCopy];
        NSString *warning = @"隊長／助人正在暫時變身；保留指定盤面目標，這些目標不代表目前隊長的倍率或減傷已發動。";
        plan[@"warning"] = [plan[@"warning"] length] ? [plan[@"warning"] stringByAppendingFormat:@"\n%@", warning] : warning;
        _goalPlan = [plan copy];
    }
    _recoveryTurn = _automatic ? _autoTurn : 0;
    if (![_goalPlan[@"available"] boolValue] || !PHOrbReadGoals(_goalPlan[@"goals"], &options.goals)) {
        _phase = @"blocked"; _message = _goalPlan[@"message"] ?: @"求路目標無法解析";
        if (_automatic || _oneShot) [self queueStop:_message];
        return;
    }
    const double available = TimeAvailable(_live);
    // Quantize to native frames as well as the hard execution deadline. A
    // longer thorough route must remain feasible even at a lower frame rate.
    const double budget = std::min(available > 0 ? available : ExecutionLimit, ExecutionLimit);
    options.steps = budget > TimeMargin ? std::max(1u, std::min(options.steps,
        unsigned((budget - TimeMargin) / StepDuration(_live)))) : 1;
    _solveOptions = options;
    _searchTrace = @{@"schema":@1, @"generation":@(_generation), @"status":@"searching",
        @"turn":@(_automatic ? _autoTurn : 0), @"wave":@(_preview.wave),
        @"dungeon":@(_preview.identity.dungeon), @"floor":@(_preview.identity.floor),
        @"board":BoardTrace(_preview.board), @"goals":PHOrbGoalsJSON(&options.goals),
        @"quality":_quality, @"stepLimit":@(options.steps), @"budgetMS":@(options.milliseconds)};
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
            NSMutableDictionary *trace = [session->_searchTrace mutableCopy];
            trace[@"path"] = RouteCells(route); trace[@"elapsedMS"] = @(route.elapsedMS);
            trace[@"evaluated"] = @(route.evaluated); trace[@"deadline"] = @(route.deadlineReached);
            trace[@"searchPasses"] = @(route.searchPasses); trace[@"backtracks"] = @(route.backtracks);
            trace[@"exploredDepth"] = @(route.exploredDepth);
            trace[@"status"] = SolveStatusName(result); session->_searchTrace = [trace copy];
            if ((result != PHOrbSolveStatus::Solved && result != PHOrbSolveStatus::GoalsUnmet && result != PHOrbSolveStatus::Unsafe) || route.count < 2) {
                session->_phase = @"stopped";
                session->_message = result == PHOrbSolveStatus::InsufficientOrbs ?
                    @"目前珠數不足以達成目標；可先使用技能改盤，或調整目標後重新讀盤。" : @"本次未取得有效路徑，請重新讀盤";
                session->_message = [PoisonFailureContext(board) stringByAppendingString:session->_message];
                if (session->_automatic || session->_oneShot) [session queueStop:session->_message];
                return;
            }
            PHOrbBoard replay{};
            PHOrbEvaluation evaluation{};
            uint32_t thornDamage = 0; uint8_t thornContacts = 0;
            if (!PHOrbReplay(&board, &route, route.count - 1, &replay) ||
                !PHOrbEvaluateRoute(board, route, options, evaluation, thornDamage, thornContacts) ||
                route.thornDamage != thornDamage || route.thornContacts != thornContacts || route.heartFours != evaluation.heartFours ||
                evaluation.goalsMet != route.goalsMet || evaluation.combos != route.combos ||
                evaluation.hazardsSafe != route.hazardsSafe || evaluation.poisonErased != route.poisonErased ||
                evaluation.unmatchedBombs != route.unmatchedBombs ||
                evaluation.erased != route.erased || evaluation.matchedGoals != route.matchedGoals || route.goalCount != options.goals.count ||
                !std::equal(std::begin(evaluation.goalHits), std::end(evaluation.goalHits), route.goalHits)) {
                [session queueStop:@"路徑重播校驗失敗，已停止"]; session->_phase = @"fault";
                return;
            }
            trace[@"predictedBoard"] = BoardTrace(replay);
            trace[@"evaluation"] = EvaluationTrace(evaluation, options.goals); session->_searchTrace = [trace copy];
            session->_route = route; session->_phase = @"preview"; session->_previewAt = session->_now;
            NSString *summary = [NSString stringWithFormat:@"預計 %u Combo（不含天降／加算）· %u 步 · 約 %.1f 秒",
                route.combos, route.count - 1, (route.count - 1) * StepDuration(session->_preview)];
            if (board.spinnerMask) summary = [summary stringByAppendingFormat:
                @"\n路徑避開轉轉；Combo 與目標按全部 %u 種可能色序的最低結果顯示。", PHOrbSpinnerVariants(&board)];
            if (board.tapeMask) summary = [summary stringByAppendingString:@"\n已繞開封條。"];
            if (route.thornContacts) summary = [summary stringByAppendingFormat:
                @"\n荊棘碰觸 %u 次 · 傷害上限 %u · 首批心四連 %u 組；未把回血與抗性計入可承受傷害。",
                route.thornContacts, route.thornDamage, route.heartFours];
            summary = [summary stringByAppendingFormat:@"\n求路耗時 %u ms／預算 %u ms · %@ · 步數上限 %u",
                route.elapsedMS, options.milliseconds, route.deadlineReached ? @"已到時間上限" : @"未到時間上限", options.steps];
            if (!route.goalsMet) {
                NSArray<NSDictionary *> *goals = PHOrbGoalsJSON(&options.goals);
                NSMutableArray<NSString *> *missing = [NSMutableArray array];
                for (unsigned g = 0; g < options.goals.count; ++g) if (route.goalHits[g] < options.goals.items[g].count)
                    [missing addObject:[NSString stringWithFormat:@"未達成：%@（%u／%u）",
                        PHOrbGoalDescription(goals[g]), route.goalHits[g], options.goals.items[g].count]];
                summary = [NSString stringWithFormat:@"%@\n%@", [missing componentsJoinedByString:@"\n"], summary];
            }
            session->_message = !route.hazardsSafe ? [NSString stringWithFormat:
                @"未找到符合妨礙處理條件的路徑：毒／猛毒 %u 顆、首批未消爆彈 %u 顆%@。未開放執行；可先用技能改盤。\n%@",
                route.poisonErased, route.unmatchedBombs, route.thornContacts && !route.heartFours ? @"、荊棘路徑未消出心四連" : @"", summary] : !route.goalsMet ? [NSString stringWithFormat:@"目標僅達成 %u／%u 項，未開放執行。本次搜尋未找到完整路徑，不代表盤面無解。\n%@",
                route.matchedGoals, route.goalCount, summary] : route.goalCount ?
                [NSString stringWithFormat:@"盤面目標已達成 %u／%u 項\n%@", route.matchedGoals, route.goalCount, summary] : summary;
            if (!route.goalsMet || !route.hazardsSafe)
                session->_message = [PoisonFailureContext(board) stringByAppendingString:session->_message];
            if ((session->_automatic || session->_oneShot) && (!route.goalsMet || !route.hazardsSafe)) [session queueStop:session->_message];
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
    _frame = frame; _now = _frameAt = time;
    _staged = _releaseStaged = false;
    if (_oneShot && (!frame.ready || !frame.enabled)) [self queueStop:@"功能狀態已改變，已取消轉一次"];
    if (_automatic && ![self executing]) {
        PHOrbSnapshot current{}; NSString *reason = nil;
        BOOL read = [_engine readAutomation:&current frame:frame reason:&reason];
        if (![self observeAutomation:&current read:read reason:reason]) return;
        if ([_phase isEqual:@"openingArming"]) {
            NSDictionary *step = _activeProfile[@"opening"][_openingStep];
            const NSUInteger slot = [step[@"slot"] unsignedIntegerValue] - 1;
            if (!PHOrbSameAutomation(&_openingBefore, &current) ||
                !PHOrbProfileMatchesOpeningTeam(_activeProfile, &current, _openingStep)) {
                [self queueStop:@"開場施放前盤面、隊員或武裝已改變，未施放"]; return;
            }
            PHOrbSkillResult result = [_engine useSkill:slot request:PHOrbRequestForRule(step)
                expected:&_openingBefore frame:frame reason:&reason];
            if (result == PHOrbSkillUnavailable) {
                [self queueStop:[NSString stringWithFormat:@"開場第 %lu 步未就緒：%@；未跳到下一步",
                    (unsigned long)_openingStep + 1, reason ?: @"遊戲判定技能不可用"]]; return;
            }
            _openingInFlight = true; _openingAcknowledged = false; _openingStarted = result == PHOrbSkillStarted;
            [_openingLog addObject:@{@"step":@(_openingStep + 1), @"slot":step[@"slot"], @"source":step[@"source"],
                @"skill":step[@"skill"], @"usesBefore":@(_openingBefore.automation.skillUses),
                @"status":result == PHOrbSkillStarted ? @"started" : @"uncertain"}];
            if (result != PHOrbSkillStarted) {
                [self queueStop:reason ?: @"開場啟動未確認；保留此步，不會重複施放"]; return;
            }
            _phase = @"openingWaiting"; _autoPhaseAt = time; _hasStable = false;
            _message = [NSString stringWithFormat:@"開場第 %lu 步已啟動，等待完成…", (unsigned long)_openingStep + 1];
            return;
        }
        if ([_phase isEqual:@"skillArming"]) {
            NSUInteger slot = [_skillRules[_skillRule][@"slot"] unsignedIntegerValue] - 1;
            // Recheck color-count conditions on the frame that starts a skill.
            // Spinner-dependent thresholds are unknown, never guessed from a
            // previously sampled color. The native engine rechecks state again.
            if (!PHOrbSameAutomation(&_skillBefore, &current)) {
                [self queueStop:@"技能施放前盤面、隊員或戰鬥狀態已改變，已停止"]; return;
            }
            PHOrbRuleMatch trigger = PHOrbTriggerSkillRule(_skillRules[_skillRule], &current, _autoTurn, &reason);
            if (trigger != PHOrbRuleYes) {
                if (trigger == PHOrbRuleUnknown) [self queueStop:reason ?: @"技能觸發條件無法確認"];
                else [self waitForAutomationBoard];
                return;
            }
            // The engine rereads once more immediately before its native call.
            _skillAttemptContexts[@(_skillRule)] = SkillDecisionContext(current, slot, true);
            _decisionContexts[_skillRule] = _skillAttemptContexts[@(_skillRule)];
            PHOrbSkillResult result = [_engine useSkill:slot request:PHOrbRequestForRule(_skillRules[_skillRule])
                expected:&_skillBefore frame:frame reason:&reason];
            NSDictionary *rule = _skillRules[_skillRule];
            // Rejection can include an uncertain native start. A once rule
            // must not be retried by stopping and immediately starting again.
            if (result != PHOrbSkillUnavailable && [rule[@"once"] boolValue]) [_onceRules addObject:rule];
            NSUInteger group = [rule[@"group"] unsignedIntegerValue];
            if (result != PHOrbSkillUnavailable) {
                _usedRules |= 1u << _skillRule; _usedSlots |= 1u << slot;
                if (group) _usedGroups |= 1u << (group - 1);
            }
            if (result == PHOrbSkillRejected) {
                _ruleDecisions[_skillRule] = [NSString stringWithFormat:@"停止 · %@", reason ?: @"技能啟動校驗失敗"];
                [self queueStop:reason ?: @"技能啟動校驗失敗，已停止"]; return;
            }
            if (result == PHOrbSkillUnavailable) {
                _unavailableRules |= 1u << _skillRule;
                _unavailableReasons[@(_skillRule)] = reason ?: @"技能不可用";
                _ruleDecisions[_skillRule] = [NSString stringWithFormat:@"略過 · %@", reason ?: @"技能不可用"];
                [self logAutomation:[NSString stringWithFormat:@"第 %lu 回合：規則 %lu 略過 · %@",
                    (unsigned long)_autoTurn, (unsigned long)_skillRule + 1, reason ?: @"技能不可用"]];
                [self waitForAutomationBoard];
            } else {
                _ruleDecisions[_skillRule] = @"技能已啟動，等待遊戲完成";
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
    BOOL read = (_automatic || _openingInFlight) ? [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    if (!read || !current.valid || current.multiplayerDisabled ||
        !PHOrbSameTurn(&_expected, &current) || current.phase != 2) {
        // The private manager has already been removed. Never inject stale
        // cleanup into a new scene/multiplayer; native input now owns release.
        if (![_phase isEqual:@"releasing"] || _releaseComplete)
            [self queueStop:@"回合、場景或可操作狀態已改變，已停止"];
        _ownedOrb = 0; _phase = @"stopped"; _recordingExecution = false;
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
        if (![self hasThornBudget:&current]) {
            [self queueStop:@"執行前荊棘傷害預算已不符合，已停止"]; return;
        }
        if (time - _requestedAt < 0.35) return;
        if (time - _requestedAt > 2 || !HasRouteTime(current, _route)) {
            [self queueStop:@"開始逾時或操作時間不足"]; return;
        }
        _phase = @"running"; _startedAt = _stepAt = _sentAt = time;
        _expected = current;
    }
    if ([_phase isEqual:@"running"]) {
        _executionElapsed = std::max(0.0, time - _startedAt);
        NSString *stopReason = !frame.enabled ? @"總開關或前景狀態已改變，已停止" :
            !PHInputIsQuiet(&current.input) ? @"偵測到手動操作，已停止並放開" :
            !current.ordinary ? @"盤面操作限制已改變，已停止" :
            time < _startedAt ? @"執行時鐘異常，已停止" :
            time - _startedAt > ExecutionLimit ? [NSString stringWithFormat:@"轉珠執行超過 %.1f 秒上限（已確認 %lu／%u 步），已停止",
                ExecutionLimit, (unsigned long)_confirmed, _route.count ? _route.count - 1 : 0] :
            !current.dragged && !_awaitingStart ? @"遊戲已結束拖珠，已停止" : nil;
        if (stopReason) {
            [self queueStop:stopReason];
        } else if (!_awaitingStart && current.dragged != _ownedOrb) {
            [self queueStop:@"拖珠已由遊戲接管，已停止"]; _ownedOrb = 0; _phase = @"stopped"; _recordingExecution = false; return;
        } else if (!SamePosition(current, _expected)) {
            [self queueStop:@"盤面與已確認步驟不同，已停止"];
        } else if (!_awaitingStart && _target == _confirmed && time - _stepAt >= StepSeconds) {
            if (_confirmed + 1 == _route.count) {
                // Re-evaluate the actual board at the release boundary. Do
                // not reuse a solver score or credit the dragged orb twice.
                PHOrbEvaluation evaluation{};
                const bool valid = PHOrbEvaluate(current.board, _solveOptions.goals, evaluation);
                [self recordExecution:@"release-check" state:&current evaluation:valid ?
                    EvaluationTrace(evaluation, _solveOptions.goals) : @{@"valid":@NO}];
                if (!valid || !evaluation.goalsMet || !evaluation.hazardsSafe ||
                    (_route.thornContacts && !evaluation.heartFours)) {
                    [self queueStop:@"放手前實際盤面未通過目標或毒／爆彈檢查；已中止連續模式並放開"];
                } else {
                    _phase = @"releasing"; _releaseComplete = true; _message = @"路徑完成，正在放開…";
                }
            } else { _target = _confirmed + 1; _stepAt = _sentAt = time; }
        }
    }
    if ([_phase isEqual:@"releasing"]) {
        if (!current.dragged || !_ownedOrb || current.dragged != _ownedOrb) {
            if (_releaseComplete) [self queueStop:@"遊戲已接管放開，已停止"];
            _ownedOrb = 0; _phase = @"stopped"; _recordingExecution = false;
            return;
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
    // Tall phone layouts defer selection from began to the following held
    // frame. Send began once, then retain the private input bank while waiting.
    // Every native frame still needs a hold, including between route cells.
    _staged = [_engine stageCell:_route.cells[_target] down:YES beginning:(_awaitingStart && !_startSent)
        expected:&current frame:frame reason:&reason];
    if (!_staged) [self queueStop:reason ?: @"輸入前校驗失敗，已停止"];
    else { _startSent = true; ++_sentFrames; }
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
    BOOL read = (_automatic || _openingInFlight) ? [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    _hasLive = read; _live = current;
    if (_oneShot && read) {
        if (!current.present || current.multiplayerDisabled ||
            (_oneShotBound && (!PHBattleSameIdentity(&_oneShotIdentity, &current.identity) || current.wave != _oneShotWave))) {
            [self queueStop:@"場景或關卡已改變，已取消轉一次"]; return;
        }
        if (!_oneShotBound && current.valid) {
            _oneShotIdentity = current.identity; _oneShotWave = current.wave; _oneShotBound = true;
        }
    }
    if (!_automatic) [self observeRuleHistory:&current read:read];
    if (!_automatic && _openingInFlight && _openingStarted && read && current.valid && current.automation.valid && !current.multiplayerDisabled &&
        PHBattleSameIdentity(&_openingBefore.identity, &current.identity)) {
        if (current.automation.skillUses == _openingBefore.automation.skillUses + 1) _openingAcknowledged = true;
        if (!_openingAcknowledged && current.phase == 2 && current.settled && !current.automation.busy && !current.dragged) {
            _openingStarted = false;
            NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"unconfirmed";
            _openingLog[_openingLog.count - 1] = [record copy];
        }
        [self finishOpeningIfConfirmed:&current];
    }
    if (_automatic && ![self observeAutomation:&current read:read reason:reason]) return;
    if (active && _staged) {
        if (!read || !current.valid || current.multiplayerDisabled || !PHOrbSameTurn(&_expected, &current)) {
            if (![_phase isEqual:@"releasing"] || _releaseComplete)
                [self queueStop:@"原生執行後場景已改變，已停止"];
            _ownedOrb = 0; _phase = @"stopped"; _recordingExecution = false; return;
        }
        if (_releaseStaged) {
            if (current.dragged == 0) {
                [self recordExecution:_releaseComplete ? @"released" : @"cancelled-released" state:&current evaluation:nil];
                if (_recordingExecution) {
                    NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy]; record[@"releaseConfirmed"] = @YES;
                    _executionHistory[_executionHistory.count - 1] = [record copy]; _recordingExecution = false;
                }
                _ownedOrb = 0;
                _phase = _releaseComplete ? @"complete" : @"stopped";
                if (_releaseComplete) {
                    _oneShot = false;
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
                _ownedOrb = current.dragged; _awaitingStart = false; _expected = current; _stepAt = _frameAt;
                [self beginExecutionRecord:&current];
                _message = @"正在執行；點浮動「停止」即可中斷";
            } else if (current.dragged || time - _startedAt > 0.20) {
                _ownedOrb = current.dragged == _preview.board.tokens[_route.cells[0]] ? current.dragged : 0;
                [self queueStop:@"遊戲未接受起點，已停止"];
            }
            return;
        }
        PHOrbBoard expectedBoard{};
        PHOrbReplay(&_preview.board, &_route, unsigned(_target), &expectedBoard);
        if (current.dragged == _ownedOrb && current.phase == 2 && PHOrbSamePlanBoard(&current.board, &expectedBoard) &&
            current.moves == _preview.moves + _target) {
            // Pace from the frame that submitted this target. Restarting at
            // its afterFrame acknowledgement adds native render/read time to
            // every step and can turn two 30-fps frames into three. A delayed
            // acknowledgement still requires a later frame before advancing;
            // _target == _confirmed above prevents skipping unconfirmed swaps.
            const bool advanced = _confirmed != _target;
            _confirmed = _target; _expected = current;
            if (advanced) [self recordExecution:@"dragging" state:&current evaluation:nil];
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
            if (time - _requestedAt > 2) {
                _readQueued = false; _phase = @"blocked";
                if (_oneShot) [self queueStop:_message];
            }
        } else if (!_hasStable || !SamePosition(current, _stable)) {
            _stable = current; _stableAt = time; _hasStable = true;
        } else if (time - _stableAt >= 0.15) [self beginSolve];
    }
    if ((_automatic || _oneShot) && [_phase isEqual:@"preview"]) {
        if (!HasRouteTime(_live, _route)) [self queueStop:@"自動路徑超出操作時間，已停止"];
        else [self queueExecute:_generation];
    }
    [self advanceAutomation];
}
- (NSDictionary *)snapshot {
    NSMutableArray *path = [NSMutableArray array], *goalHits = [NSMutableArray array];
    NSMutableArray *decisionDetails = [NSMutableArray array];
    // A retained native refusal must keep its own observation, even when a
    // later scan sees another spinner phase or charge. Unattempted decisions
    // use the current scan; successful attempts keep their pre-cast context.
    for (NSUInteger i = 0; i < _ruleDecisions.count && i < _decisionRules.count; ++i) {
        NSUInteger slot = [_decisionRules[i][@"slot"] unsignedIntegerValue] - 1;
        if (slot >= 6) continue;
        NSMutableDictionary *detail = [(i < _decisionContexts.count ? _decisionContexts[i] :
            SkillDecisionContext(_decisionState, slot)) mutableCopy];
        [detail addEntriesFromDictionary:@{@"rule":@(i + 1), @"slot":@(slot + 1),
            @"decision":_ruleDecisions[i]}];
        [decisionDetails addObject:detail];
    }
    for (unsigned i = 0; i < _route.count; ++i) [path addObject:@(_route.cells[i])];
    for (unsigned i = 0; i < _route.goalCount && i < PHOrbMaxGoals; ++i) [goalHits addObject:@(_route.goalHits[i])];
    BOOL preview = [_phase isEqual:@"preview"];
    BOOL canExecute = preview && _route.goalsMet && _route.hazardsSafe && _frame.enabled && !_inputFault && _hasLive && PHOrbCanPreview(&_live) &&
        SamePosition(_live, _preview) && HasRouteTime(_live, _route) && [self hasThornBudget:&_live];
    return @{@"phase":_phase, @"message":_message, @"generation":@(_generation),
        @"canRead":@(_frame.ready && !_automatic && !_oneShot && ![self executing] && !_inputFault && !_openingInFlight), @"canExecute":@(canExecute && !_automatic && !_oneShot),
        @"canStart":@(_frame.ready && _frame.enabled && !_automatic && !_oneShot && ![self executing] && !_inputFault),
        @"canOnce":@(_frame.ready && _frame.enabled && !_automatic && !_oneShot && ![self executing] && !_inputFault && !_openingInFlight),
        @"once":@(_oneShot),
        @"busy":@(_automatic || _oneShot || [self executing]), @"canStop":@(_automatic || _oneShot || [self executing] || _readQueued || preview || [_phase isEqual:@"solving"]),
        @"automatic":@(_automatic), @"autoTurn":@(_autoTurn), @"autoMoves":@(_autoMoves), @"autoSkills":@(_autoSkills),
        @"rules":_skillRules, @"automationLog":[_automationLog copy], @"enemyEffects":@(_live.automation.enemyEffects),
        @"ruleDecisions":[_ruleDecisions copy], @"decisionTurn":@(_decisionTurn),
        @"ruleDecisionDetails":decisionDetails, @"decisionPhase":@(_decisionState.phase),
        @"executionElapsedMS":@(unsigned(_executionElapsed * 1000)), @"executionLimitMS":@(unsigned(ExecutionLimit * 1000)),
        @"decisionSkillWindow":@(_decisionState.automation.window), @"decisionSkillBusy":@(_decisionState.automation.busy),
        @"playerStatus":PHOrbSurvivalDescription(&_live), @"confirmedSkillRules":@(_confirmedRules),
        @"obstacles":PHOrbObstacleDescription(&_live), @"usedSkillGroups":@(_usedGroups),
        @"activeProfile":_activeProfile,
        @"opening":@{@"profile":_openingProfile[@"id"] ?: @"", @"completed":@(_openingStep),
            @"count":@([_openingProfile[@"opening"] count]), @"inFlight":@(_openingInFlight), @"steps":[_openingLog copy]},
        @"searchTrace":_searchTrace, @"executionHistory":[_executionHistory copy],
        @"profileContext":@{@"known":@(_hasLive && _live.valid && _live.present && !_live.multiplayerDisabled),
            @"dungeon":@(_live.identity.dungeon), @"floor":@(_live.identity.floor)},
        @"playerBypass":@(_live.automation.playerBypass), @"skillWindow":@(_live.automation.window),
        @"enabled":@(_frame.ready && _frame.enabled), @"board":BoardJSON(_route.count ? _preview.board : _live.board), @"path":path,
        @"quality":_quality, @"searchSteps":@(_solveOptions.steps), @"searchBeam":@(_solveOptions.beam),
        @"recovery":_recovery, @"recoveryActive":_goalPlan[@"recoveryActive"] ?: @NO,
        @"recoveryStatus":_goalPlan[@"recoveryMessage"] ?: @"", @"recoveryTurn":@(_recoveryTurn),
        @"goalMode":_goalMode, @"goals":_manualGoals, @"goalPlan":_goalPlan,
        @"goalCount":@(_route.goalCount), @"matchedGoals":@(_route.matchedGoals), @"goalsMet":@(_route.goalsMet),
        @"hazardsSafe":@(_route.hazardsSafe), @"poisonErased":@(_route.poisonErased), @"unmatchedBombs":@(_route.unmatchedBombs),
        @"thornContacts":@(_route.thornContacts), @"thornDamage":@(_route.thornDamage), @"heartFours":@(_route.heartFours),
        @"thornHPKnown":@(_live.thorns.known), @"thornHP":@(_live.thorns.hp), @"thornMaximumHP":@(_live.thorns.maximumHP),
        @"pendingOrbDamage":@(_live.thorns.pendingDamage),
        @"spinnerMask":@(_live.board.spinnerMask), @"spinnerColors":@(_live.board.spinnerColors),
        @"spinnerTurns":@(_live.spinnerTurns), @"spinnerPeriodFrames":@(_live.spinnerPeriodFrames),
        @"spinnerReadStatus":PHOrbSpinnerReadDescription(&_live),
        @"spinnerRawCount":@(_live.spinnerRawCount), @"spinnerRawPeriod":@(_live.spinnerRawPeriod),
        @"spinnerRawColors":@(_live.spinnerRawColors), @"spinnerStoredCells":@(_live.spinnerStoredCells),
        @"spinnerRawRows":@[@(_live.spinnerRawRows[0]), @(_live.spinnerRawRows[1]), @(_live.spinnerRawRows[2]),
            @(_live.spinnerRawRows[3]), @(_live.spinnerRawRows[4]), @(_live.spinnerRawRows[5])],
        @"spinnerVariants":@(PHOrbSpinnerVariants(&_live.board)), @"blockedMask":@(PHOrbBlockedCells(&_live.board)),
        @"tapeMask":@(_live.board.tapeMask), @"cloudMask":@(_live.board.cloudMask), @"fixedStart":@(_live.board.fixedStart),
        @"unmatchableColors":@(_live.board.unmatchableColors), @"unmatchableTurns":@(_live.unmatchableTurns),
        @"unmatchableKnown":@(_live.unmatchableKnown),
        @"tapeRowTurns":@(_live.tapeRowTurns), @"tapeColumnTurns":@(_live.tapeColumnTurns), @"cloudTurns":@(_live.cloudTurns),
        @"minErase":@(_live.minErase), @"minMatch":@(_live.board.minMatch),
        @"goalHits":goalHits,
        @"leadersValid":@(_live.leadersValid), @"leaderNumbers":@[@(_live.leaderNumbers[0]), @(_live.leaderNumbers[1])],
        @"searchBudgetMS":@(_solveOptions.milliseconds),
        @"combos":@(_route.combos), @"steps":@(_route.count ? _route.count - 1 : 0), @"confirmed":@(_confirmed),
        @"elapsedMS":@(_route.elapsedMS), @"evaluated":@(_route.evaluated), @"deadline":@(_route.deadlineReached),
        @"searchPasses":@(_route.searchPasses), @"backtracks":@(_route.backtracks), @"exploredDepth":@(_route.exploredDepth),
        @"completed":@(_completed), @"cancelled":@(_cancelled), @"inputFrames":@(_sentFrames), @"inputFault":@(_inputFault),
        @"startSent":@(_startSent), @"startAccepted":@(_startSent && !_awaitingStart),
        @"multiplayerDisabled":@(_live.multiplayerDisabled), @"nativePhase":@(_live.phase), @"wave":@(_live.wave),
        @"moves":@(_live.moves), @"remainingFrames":@(_live.remainingFrames), @"fps":@(_live.fps),
        @"settled":@(_live.settled), @"ordinary":@(_live.ordinary), @"deviceVerified":@NO};
}
@end
