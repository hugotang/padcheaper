#import "PHOrbSession.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#import "PHOrbEight.h"
#import "PHOrbSkillWindow.h"
#include "PHOrbSolver.h"
#include "PHOrbSearchAttempt.h"
#include "PHOrbSessionState.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#import "PHOrbDungeonReport.h"
#include <chrono>

namespace {
constexpr double StepSeconds = 0.065, PreviewLifetime = 45, ExecutionLimit = 4, TimeMargin = 0.40;
constexpr double ArmDelay = 0.35;
constexpr double AutomationSettle = 0.20, SkillTimeout = 30, TurnTimeout = 90;
constexpr double SkillWindowTimeout = 3;
bool ResolvingOrbs(uint32_t phase) { return phase >= 3 && phase <= 6; }
NSString *BattleEndMessage(const PHOrbSnapshot &state) {
    return state.phase >= 28 && state.phase <= 31 ? @"Dungeon Clear，Auto 已停止" : @"本場已進入結算，Auto 已停止";
}
bool AcceptedSkillTiming(NSArray *rules) {
    for (NSDictionary *rule in rules) if ([rule[@"enabled"] boolValue] && [rule[@"intervalMode"] isEqual:@"accepted"]) return true;
    return false;
}
NSDictionary *SkillTimingProfile(NSDictionary *profile) {
    if (!profile.count) return @{};
    return @{@"id":profile[@"id"], @"team":profile[@"team"], @"dungeon":profile[@"dungeon"], @"floor":profile[@"floor"],
        @"formation":profile[@"formation"] ?: @{}, @"opening":profile[@"opening"] ?: @[]};
}
bool PersistentSkillWindow(const PHOrbAutomationState &state) {
    return (state.windowBlocksKnown && (state.windowBlocks & PHOrbSkillWindowPersistent)) ||
        (state.playerEffects & PHOrbPlayerSkillSeal);
}
bool SamePosition(const PHOrbSnapshot &a, const PHOrbSnapshot &b) {
    return PHOrbSameTurn(&a, &b) && PHOrbSameLeaders(&a, &b) && a.moves == b.moves && PHOrbSamePlanBoard(&a.board, &b.board);
}
// Fishing takes one colour out of a spinner cell mid-gesture (see PHOrbRoute).
// Spinners keep cycling during a drag, so the step into the cell waits until
// that colour shows with frames left. The orb is already moving by then, so the
// route must afford a full colour cycle before it starts.
constexpr unsigned FishMinCountdown = 6, FishMinPeriod = 10;
double FishWaitSeconds(const PHOrbSnapshot &state) {
    return state.fps >= 15 && state.fps <= 120 ?
        double(PHOrbSpinnerVariants(&state.board)) * state.spinnerPeriodFrames / state.fps : 30;
}
double TimeAvailable(const PHOrbSnapshot &state) {
    const double move = state.fps >= 15 && state.fps <= 120 && state.remainingFrames <= 30000 ?
        double(state.remainingFrames) / state.fps : 0;
    if (PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer)) return move;
    if (state.multiplayer.kind == PHOrbMultiplayerThreeSolo)
        return PHOrbMultiplayerWindow(&state) ? move : 0;
    return PHOrbMultiplayerWindow(&state) ? std::min(move, state.multiplayer.remainingSeconds) : 0;
}
double StepDuration(const PHOrbSnapshot &state) {
    return state.fps >= 15 && state.fps <= 120 ? std::ceil(StepSeconds * state.fps) / state.fps : StepSeconds;
}
bool HasRouteTime(const PHOrbSnapshot &state, const PHOrbRoute &route, double delay = 0) {
    if (route.count < 2) return false;
    const double wait = route.fishing ? FishWaitSeconds(state) : 0;
    const double required = (route.count - 1) * StepDuration(state) + wait + TimeMargin;
    if (required > std::min(TimeAvailable(state), ExecutionLimit + wait)) return false;
    return PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer) ||
        state.multiplayer.kind == PHOrbMultiplayerThreeSolo ||
        required + delay <= state.multiplayer.remainingSeconds;
}
PHOrbSolveOptions OptionsForQuality(NSString *quality) {
    return PHOrbOptionsForQuality([quality isEqual:@"fast"] ? PHOrbSearchQuality::Fast :
        [quality isEqual:@"thorough"] ? PHOrbSearchQuality::Thorough : PHOrbSearchQuality::Balanced);
}
#include "PHOrbDiagnosticPresentation.inc"

NSMutableDictionary *SearchAttemptTrace(const PHOrbSolveOptions &options, PHOrbSolveStatus status,
                                       const PHOrbRoute &route, uint64_t wallElapsedMS) {
    const bool statisticsAvailable = route.count >= 2 && (status == PHOrbSolveStatus::Solved ||
        status == PHOrbSolveStatus::GoalsUnmet || status == PHOrbSolveStatus::Unsafe);
    return [@{@"goals":PHOrbGoalsJSON(&options.goals), @"status":SolveStatusName(status),
        @"budgetMS":@(options.milliseconds), @"elapsedMS":@(route.elapsedMS), @"wallElapsedMS":@(wallElapsedMS),
        @"statisticsAvailable":@(statisticsAvailable), @"deadlineKnown":@(statisticsAvailable),
        @"deadline":@(route.deadlineReached), @"evaluated":@(route.evaluated),
        @"evaluationCalls":@(route.evaluationCalls), @"evaluationCacheHits":@(route.evaluationCacheHits),
        @"searchPasses":@(route.searchPasses), @"backtracks":@(route.backtracks),
        @"exploredDepth":@(route.exploredDepth), @"stepLimit":@(options.steps), @"beam":@(options.beam)} mutableCopy];
}

}

// 8P Auto state, grouped by lifetime. Each group is reset only by assigning
// {} (see -resetEightPlan, -resetEightTurn and the new-match branch of
// -startEightAutomatic:), so new fields are reset without touching call sites.
struct EightPlanState {
    NSArray *sequence = nil;     // ordered slots (1-based) to cast before solving
    NSUInteger cursor = 0;       // next rescue branch to evaluate on this board
    NSUInteger step = 0;         // confirmed casts within sequence
    bool guidePrepared = false;  // floor guide decision already made for this board
    bool guide = false;          // sequence came from the floor guide (not rescue)
};
struct EightTurnState {
    uint8_t attemptedSlots = 0;        // slots already cast this native turn
    // A preferred reserve search failed on this turn's board: output and
    // rescue keep the whole budget and the reserve turn is not retried.
    bool preparationDeclined = false;
};
struct EightPreparationState {
    NSDictionary *plan = nil, *trace = nil;
    bool active = false;    // the current search is the reserve (three-orb) route
    bool used = false;      // the single per-match reserve turn was committed
    bool awaiting = false;  // waiting for the next native turn to confirm it
    bool priority = false;  // pre-empted output on this board (preferBeforeOutput)
    unsigned searchMS = 0;  // budget reserved for the reserve search
};

@implementation PHOrbSession {
    id<PHOrbEngine> _engine;
    dispatch_queue_t _worker;
    std::shared_ptr<std::atomic_bool> _cancel;
    PHBattleFrame _frame;
    PHOrbSnapshot _live, _stable, _preview, _expected, _decisionState, _traceOrigin;
    PHOrbRoute _route;
    PHOrbSessionPhase _phase;
    NSString *_message, *_quality, *_goalMode;
    bool _thornComboPriority, _allowPoisonWithHeartFour, _diagonal, _assistRecoveryL;
    bool _assistAwaitingResult;
    double _assistCheckedAt, _enemyComboCheckedAt;
    PHOrbSnapshot _assistReleased;
    NSDictionary *_assistTrace;
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
    bool _openingInFlight, _openingAcknowledged, _openingStarted, _openingPuzzlePending, _openingManual;
    PHOrbSnapshot _openingBefore;
    NSDictionary *_searchTrace;
    NSDictionary *_lastReadIssue;
    NSString *_liveReadReason;
    double _lastReadUptime;
    PHOrbSnapshot _observationState;
    NSDictionary *_enemyActionObservation;
    double _observationAt, _observationUptime, _observationDurationMS;
    PHBattleIdentity _dungeonIdentity;
    bool _dungeonBound;
    double _dungeonCaptureAt;
    NSUInteger _dungeonCursor, _dungeonRefreshCursor, _dungeonDropped;
    NSArray *_dungeonFloors, *_dungeonMetadata;
    NSMutableDictionary *_dungeonPages, *_dungeonDefinitionSets;
    NSDictionary *_dungeonLastStop;
    NSMutableArray *_dungeonSamples;
    NSDictionary *_dungeonSampleKey;
    NSString *_dungeonTableSHA, *_dungeonStatus;
    NSMutableArray<NSDictionary *> *_onceReadinessHistory;
    NSDictionary *_onceReadinessKey;
    NSUInteger _boardTransitionFrames;
    NSMutableArray<NSDictionary *> *_executionHistory;
    bool _recordingExecution;
    PHOrbSolveOptions _solveOptions;
    NSUInteger _generation, _completed, _cancelled, _sentFrames, _confirmed;
    NSUInteger _target;
    uintptr_t _ownedOrb;
    double _now, _lastRead, _requestedAt, _stableAt, _previewAt, _startedAt, _stepAt, _sentAt, _frameAt, _executionElapsed;
    double _skillWindowWaitAt;
    bool _hasLive, _hasStable, _readQueued, _staged, _releaseStaged, _releaseComplete;
    bool _inputFault, _awaitingStart, _startSent, _monitoring, _readinessMonitoring, _oneShot, _oneShotBound;
    PHOrbSnapshot _oneShotOrigin;
    PHOrbAutoRunState _auto;
    PHOrbSnapshot _skillBefore;
    NSUInteger _skillRule, _decisionTurn, _recoveryTurn;
    NSUInteger _observedTurn, _decisionObservedTurn;
    NSUInteger _ruleLastUseTurns[PHOrbMaxSkillRules], _groupLastUseTurns[PHOrbMaxSkillGroups];
    NSDictionary *_historyTimingProfile;
    uint16_t _usedRules, _unavailableRules, _confirmedRules;
    uint8_t _usedSlots, _usedGroups, _confirmedGroups, _groupNext[PHOrbMaxSkillGroups];
    NSMutableSet<NSDictionary *> *_onceRules;
    PHBattleIdentity _historyIdentity;
    bool _historyBound, _historyResolve, _historyPuzzleStarted;
    bool _skillAcknowledged, _watchPlayerState;
    bool _eightTeamBound, _eightSkillPending, _eightSkillStarted, _eightBoardSubmitted;
    bool _eightSkillAcknowledged, _eightSkillSettled, _eightResuming, _eightDiscardSequence, _eightPausedResolve;
    PHOrbSnapshot _eightHistory, _eightSubmission, _eightSkillBefore, _eightSkillAfter, _eightLastObserved;
    NSDictionary *_eightHistoryProfile, *_eightLastStop;
    NSUInteger _eightResumes;
    PHOrbEightAutoStartState _eightAutoStart; // survives Stop and settings reloads
    bool _fishEntered;
    NSUInteger _eightSlot;
    // Grouped by lifetime: assigning {} resets every field, including ones
    // added later, so a reset site cannot silently miss new state.
    EightPlanState _eightPlan;       // current board's skill plan
    EightTurnState _eightTurn;       // one native turn
    EightPreparationState _eightPrep; // one match (single reserve turn)
    std::array<uint8_t,100> _eightFloorSkillMask;
    std::array<bool,100> _eightFloorSubmitted;
    NSDictionary *_eightFirstPuzzle, *_eightFirstBlocked, *_eightLastBlocked;
}
- (instancetype)initWithEngine:(id<PHOrbEngine>)engine {
    if ((self = [super init])) {
        _eightFloorSkillMask = {}; _eightFloorSubmitted = {};
        _engine = engine;
        _worker = dispatch_queue_create("jp.padcheaper.orbs.solver", DISPATCH_QUEUE_SERIAL);
        _phase = PHOrbSessionPhase::Idle; _message = @"自己的操作階段可轉一次；單人與 8 人支援本場 Auto";
        _quality = @"balanced"; _goalMode = @"combo"; _manualGoals = @[]; _goalPlan = @{};
        _skillRules = @[]; _automationLog = [NSMutableArray array]; _onceRules = [NSMutableSet set];
        _recovery = PHOrbDefaultRecovery();
        _ruleDecisions = [NSMutableArray array]; _unavailableReasons = [NSMutableDictionary dictionary]; _decisionRules = @[];
        _skillAttemptContexts = [NSMutableDictionary dictionary];
        _decisionContexts = [NSMutableArray array];
        _profileLibrary = PHOrbDefaultProfiles(); _activeProfile = @{};
        _openingLog = [NSMutableArray array];
        _searchTrace = @{}; _executionHistory = [NSMutableArray array];
        _assistTrace = @{};
        _lastReadIssue = @{};
        _liveReadReason = @""; _onceReadinessHistory = [NSMutableArray array];
    }
    return self;
}
#include "PHOrbDungeonSession.inc"
- (void)refreshBattleObservation:(BOOL)read active:(BOOL)active time:(NSTimeInterval)time {
    if (!read || !_frame.ready || !_frame.enabled || _live.identity.player != _frame.player ||
        _live.identity.epoch != _frame.epoch || !ObservationWindow(_live) || active || _oneShot ||
        (_live.automation.valid && _live.automation.busy)) {
        _observationState = {};
        _enemyActionObservation = nil;
        _observationState.observation.status = _live.multiplayer.kind != PHOrbMultiplayerNone ? PHOrbObservationMultiplayer :
            read ? PHOrbObservationTransition : PHOrbObservationUnavailable;
        _observationUptime = _observationDurationMS = 0;
        return;
    }
    if (![_engine respondsToSelector:@selector(observeBattle:)]) return;
    if (_observationState.observation.status == PHOrbObservationReady && !ObservationContextMatches(_observationState, _live)) {
        _observationState = {};
        _enemyActionObservation = nil;
        _observationState.observation.status = PHOrbObservationChanged;
        _observationUptime = _observationDurationMS = 0;
    }
    // Invalidating evidence must not reset the attempt clock. Rapid board,
    // skill or read-window changes cannot turn diagnostics into frame polling.
    if (_observationAt > 0 && time >= _observationAt && time - _observationAt < 0.75) return;
    [self captureDungeonPage:&_live time:time];
    const double started = NSProcessInfo.processInfo.systemUptime;
    PHOrbSnapshot captured = _live;
    [_engine observeBattle:&captured];
    _enemyActionObservation = nil;
    if (captured.observation.status == PHOrbObservationReady && [_engine respondsToSelector:@selector(observeEnemyActions:)])
        _enemyActionObservation = [_engine observeEnemyActions:&captured];
    _observationDurationMS = std::max(0.0, NSProcessInfo.processInfo.systemUptime - started) * 1000;
    _observationState = captured;
    _observationAt = time; _observationUptime = NSProcessInfo.processInfo.systemUptime;
    [self retainDungeonSample:&captured];
}
- (BOOL)executing {
    return PHOrbSessionExecuting(_phase);
}
- (void)cancelWorker {
    ++_generation;
    if (_cancel) _cancel->store(true, std::memory_order_relaxed);
    _cancel.reset();
}
// A fishing route opens its one spinner cell to native input; -1 closes it.
- (void)openSpinnerCell:(NSInteger)cell {
    if ([_engine respondsToSelector:@selector(allowSpinnerEntry:)]) [_engine allowSpinnerEntry:cell];
}
- (void)clearRoute {
    _route = {}; _confirmed = _target = 0; _fishEntered = false;
    [self openSpinnerCell:-1];
    _startSent = _awaitingStart = false;
    _executionElapsed = 0;
    _recordingExecution = false;
}
#include "PHOrbSessionDiagnostics.inc"
#include "PHOrbAssistSession.inc"
#include "PHOrbEnemyComboSession.inc"
#include "PHOrbEightSession.inc"
#include "PHOrbAutoLifecycle.inc"
#include "PHOrbAutomationSession.inc"
- (BOOL)configureQuality:(NSString *)quality {
    if (!quality) return NO;
    return [self configureSettings:@{@"quality":quality, @"mode":_goalMode, @"goals":_manualGoals, @"rules":_skillRules,
        @"recovery":_recovery, @"thornComboPriority":@(_thornComboPriority),
        @"allowPoisonWithHeartFour":@(_allowPoisonWithHeartFour), @"diagonal":@(_diagonal),
        @"assistRecoveryL":@(_assistRecoveryL)}];
}
- (BOOL)configureSettings:(NSDictionary *)settings {
    NSDictionary *canonical = PHOrbCanonicalSettings(settings);
    if (!canonical || _auto.active() || _oneShot || [self executing] || _inputFault) return NO;
    if ([_quality isEqual:canonical[@"quality"]] && [_goalMode isEqual:canonical[@"mode"]] &&
        [_manualGoals isEqual:canonical[@"goals"]] && [_skillRules isEqual:canonical[@"rules"]] &&
        [_recovery isEqual:canonical[@"recovery"]] && _thornComboPriority == [canonical[@"thornComboPriority"] boolValue] &&
        _allowPoisonWithHeartFour == [canonical[@"allowPoisonWithHeartFour"] boolValue] &&
        _diagonal == [canonical[@"diagonal"] boolValue] && _assistRecoveryL == [canonical[@"assistRecoveryL"] boolValue]) return YES;
    [self cancelWorker];
    [self loadSettings:canonical];
    _readQueued = _hasStable = false;
    _phase = PHOrbSessionPhase::Idle; _message = @"求路設定已變更，請重新讀盤";
    return YES;
}
- (void)loadSettings:(NSDictionary *)settings {
    _quality = [settings[@"quality"] copy]; _goalMode = [settings[@"mode"] copy];
    _manualGoals = settings[@"goals"]; _goalPlan = @{}; [self clearRoute];
    _solveOptions = OptionsForQuality(_quality);
    _thornComboPriority = [settings[@"thornComboPriority"] boolValue];
    _solveOptions.thornComboPriority = _thornComboPriority;
    _allowPoisonWithHeartFour = [settings[@"allowPoisonWithHeartFour"] boolValue];
    _solveOptions.allowPoisonWithHeartFour = _allowPoisonWithHeartFour;
    _diagonal = [settings[@"diagonal"] boolValue];
    _solveOptions.diagonal = _diagonal;
    _assistRecoveryL = [settings[@"assistRecoveryL"] boolValue];
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
    if (!PHOrbProfilesValid(library) || _auto.active() || _oneShot || [self executing] || _inputFault) return NO;
    if ([_profileLibrary isEqual:library]) return YES;
    NSData *data = [NSJSONSerialization dataWithJSONObject:library options:0 error:NULL];
    NSDictionary *saved = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    if (!saved) return NO;
    [self cancelWorker]; _profileLibrary = saved; _activeProfile = @{};
    // Countdown auto-start needs board reads even while the orb page is closed.
    if ([saved[@"eightAutoStart"] boolValue]) _readinessMonitoring = true;
    [self clearRoute]; _readQueued = _hasStable = false;
    _phase = PHOrbSessionPhase::Idle; _message = @"策略已儲存；按開始時核對隊伍與關卡";
    return YES;
}
- (void)monitorReadiness {
    _readinessMonitoring = true;
}
- (void)queueRead {
    if (_auto.active() || _oneShot || [self executing] || _inputFault || _openingInFlight) return;
    [self cancelWorker];
    [_engine armMultiplayerOnce:NO];
    _monitoring = _readQueued = true; _hasStable = false; _requestedAt = _now;
    [self clearRoute]; _goalPlan = @{}; _phase = PHOrbSessionPhase::Reading; _message = @"等待盤面穩定…";
}
- (void)queueOnce {
    if (_auto.active() || _oneShot || [self executing] || _inputFault || _openingInFlight || !_frame.ready || !_frame.enabled) return;
    _monitoring = true;
    // Bind at the tap, using a fresh read even when the UI status is old.
    // An ineligible multiplayer tap must never wait for another player's turn.
    PHOrbSnapshot current{}; NSString *reason = nil;
    const BOOL read = [_engine read:&current frame:_frame reason:&reason];
    _hasLive = read; _live = current;
    _liveReadReason = [reason copy] ?: @""; _lastReadUptime = NSProcessInfo.processInfo.systemUptime;
    if (!read || !current.valid || !current.present || current.multiplayerDisabled) {
        [self queueStop:reason ?: @"目前盤面無法確認，未排入轉一次"]; return;
    }
    const bool multiplayer = !PHBattleIsSinglePlayer(current.identity.entryMode, current.identity.multiplayer);
    if (multiplayer && !PHOrbCanPreview(&current)) {
        [self queueStop:reason ?: @"尚未輪到自己或盤面未穩定；請在自己的操作窗口再按轉一次"]; return;
    }
    if (multiplayer && TimeAvailable(current) <= ArmDelay + TimeMargin + StepDuration(current)) {
        [self queueStop:@"多人剩餘時間不足，未排入轉一次"]; return;
    }
    if (multiplayer && [_goalMode isEqual:@"leaders"]) {
        NSDictionary *plan = PHOrbPlanForLeaders(&current);
        if (![plan[@"available"] boolValue]) { [self queueStop:plan[@"message"]]; return; }
    }
    [self queueRead];
    _oneShot = _oneShotBound = true; _oneShotOrigin = current;
    [_engine armMultiplayerOnce:multiplayer];
    _message = multiplayer ? [NSString stringWithFormat:@"%@：只執行本機這一盤，求路完成後轉一次", MultiplayerModeName(current.multiplayer.kind)] :
        @"轉一次：等待盤面穩定，求路成功後執行";
}
- (void)queueExecute:(NSUInteger)generation {
    if (_phase != PHOrbSessionPhase::Preview || generation != _generation || !_frame.enabled || _inputFault || !_route.goalsMet || !_route.hazardsSafe) return;
    if (!PHBattleIsSinglePlayer(_preview.identity.entryMode, _preview.identity.multiplayer) &&
        !(_auto.eight() && PHOrbSameEightMatch(&_auto.origin,&_preview)) &&
        (!_oneShot || !_oneShotBound || !PHOrbSameTurn(&_oneShotOrigin, &_preview))) {
        [self queueStop:@"多人預覽不能直接執行；請在自己的操作窗口按轉一次重新讀盤"]; return;
    }
    if (_now - _previewAt > PreviewLifetime || !_hasLive || !PHOrbCanPreview(&_live) || !SamePosition(_live, _preview)) {
        [self queueStop:@"盤面或回合已改變，請重新讀盤"];
        return;
    }
    if (!HasRouteTime(_live, _route, ArmDelay)) {
        _message = @"操作時間不足，請重新讀盤取得較短路徑";
        if (_oneShot || _auto.active()) [self queueStop:_message];
        return;
    }
    if (![self hasThornBudget:&_live]) {
        [self queueStop:@"荊棘路徑的 HP 或待結算傷害已改變，請重新求路"]; return;
    }
    if (_auto.eight()) {
        if (!PHOrbSameEightTeam(&_live.eight,&_preview.eight) || !PHOrbSameEnemyCombo(&_live.enemyCombo,&_preview.enemyCombo)) {
            [self queueStop:@"8 人求路後技能或吸收狀態已改變，已停止"]; return;
        }
        [_engine armMultiplayerOnce:YES]; // Fresh lease for this board only.
    }
    _expected = _preview; _target = _confirmed = 0; _ownedOrb = 0; _fishEntered = false;
    [self openSpinnerCell:_route.fishing ? NSInteger(_route.fishCell) : -1];
    _awaitingStart = true; _startSent = false; _phase = PHOrbSessionPhase::Arming; _requestedAt = _now;
    _message = _auto.active() ? [NSString stringWithFormat:@"連續第 %lu 回合 · 準備轉珠；點浮動「停止」可中斷", (unsigned long)_auto.turn] :
        @"準備執行一次；點浮動「停止」即可中斷";
}
- (BOOL)hasThornBudget:(const PHOrbSnapshot *)state {
    if (!_route.thornContacts) return YES;
    PHOrbSolveOptions options = _solveOptions; options.thorns = state->thorns;
    if (!PHOrbReadGoals(_goalPlan[@"goals"], &options.goals)) return NO;
    options.goals.comboFirstWave = [_goalPlan[@"enemyComboFirstWave"] boolValue];
    options.goals.bonusFirstWave = [_goalPlan[@"enemyComboBonusFirstWave"] boolValue];
    PHOrbEvaluation evaluation{}; uint32_t damage = 0; uint8_t contacts = 0;
    return PHOrbEvaluateRoute(state->board, _route, options, evaluation, damage, contacts) &&
        evaluation.goalsMet && evaluation.hazardsSafe && contacts == _route.thornContacts;
}
- (void)beginSolve {
    if (_auto.active()) [self cancelWorker]; // Each automatic route has its own UI/worker generation.
    // Progress belongs to the candidate below, not the previously executed
    // route. Clear it even when planning or the new search fails.
    [self clearRoute];
    _readQueued = false; _preview = _live;
    _preview.assistRecovery = {};
    if (!_auto.eight()) _preview.enemyCombo = {};
    PHOrbSolveOptions options = OptionsForQuality(_quality);
    options.thorns = _preview.thorns;
    options.thornComboPriority = _thornComboPriority;
    options.allowPoisonWithHeartFour = _allowPoisonWithHeartFour;
    options.diagonal = _diagonal;
    _solveOptions = options;
    _traceOrigin = _preview;
    _searchTrace = @{@"schema":@2, @"generation":@(_generation), @"status":@"planning", @"solverStarted":@NO,
        @"goalMode":_goalMode, @"leadersValid":@(_preview.leadersValid),
        @"leaderNumbers":@[@(_preview.leaderNumbers[0]), @(_preview.leaderNumbers[1])],
        @"leaderContext":LeaderTrace(_preview),
        @"scope":_auto.active() ? @"continuous" : _oneShot ? @"once" : @"preview",
        @"multiplayer":MultiplayerTrace(_preview),
        @"turn":@(_auto.active() ? _auto.turn : 0), @"observedTurn":@(_observedTurn), @"wave":@(_preview.wave),
        @"dungeon":@(_preview.identity.dungeon), @"floor":@(_preview.identity.floor),
        @"board":BoardTrace(_preview.board), @"quality":_quality,
        @"thornComboPriority":@(options.thornComboPriority),
        @"allowPoisonWithHeartFour":@(options.allowPoisonWithHeartFour), @"diagonal":@(options.diagonal)};
    _skillWindowWaitAt = 0;
    _goalPlan = [_goalMode isEqual:@"leaders"] ? PHOrbPlanForLeaders(&_preview) :
        @{@"available":@YES, @"goals":[_goalMode isEqual:@"manual"] ? _manualGoals : @[],
          @"message":PHOrbGoalsDescription([_goalMode isEqual:@"manual"] ? _manualGoals : @[]), @"warning":@"", @"leaders":@[]};
    if (_auto.eight()) {
        _goalPlan = [self planEightBoard:&_preview options:&options];
    } else _goalPlan = PHOrbPlanWithRecovery(_goalPlan, _recovery, &_preview, _auto.active());
    if (!_auto.eight() && _preview.multiplayer.kind == PHOrbMultiplayerEight) {
        // A manual one-shot retains the user's goals and never spends skills.
        // Match 8P Auto's conservative first-removal accounting.
        options.activity.firstWaveOnly = true;
        NSMutableDictionary *plan = [_goalPlan mutableCopy]; plan[@"enemyComboFirstWave"] = @YES;
        _goalPlan = [plan copy];
    }
    if (_auto.active() && !_auto.eight()) {
        if (![self captureEnemyCombo:&_preview]) {
            NSMutableDictionary *plan = [_goalPlan mutableCopy];
            plan[@"available"] = @NO; plan[@"message"] = _message;
            plan[@"enemyCounterMessage"] = _message; plan[@"enemyCounterBlocker"] = @"state-unavailable";
            _goalPlan = [plan copy];
            [self recordPlanningStatus:@"enemy-counter-unavailable" reason:_message]; return;
        }
        _goalPlan = PHOrbPlanWithEnemyCombo(_goalPlan, &_preview, YES);
    }
    if (_auto.active() && !_auto.eight() && _assistRecoveryL) {
        if (![self captureAssistRecovery:&_preview]) {
            NSMutableDictionary *plan = [_goalPlan mutableCopy];
            plan[@"available"] = @NO; plan[@"message"] = _message;
            plan[@"assistRecoveryMessage"] = _message; plan[@"assistRecoveryBlocker"] = @"state-unavailable";
            _goalPlan = [plan copy];
            [self recordPlanningStatus:@"assist-unavailable" reason:_message]; return;
        }
        _goalPlan = PHOrbPlanWithAssistRecovery(_goalPlan, YES, &_preview, YES);
        NSDictionary *previous = [_assistTrace[@"status"] isEqual:@"observed-clear"] ?
            [_assistTrace dictionaryWithValuesForKeys:@[@"status", @"beforeTurns", @"observedTurns", @"turn",
                @"actualConfirmed", @"lOnlyAmountConfirmed"]] : nil;
        NSMutableDictionary *trace = [[self assistRecoveryTrace:&_preview.assistRecovery status:
            [_goalPlan[@"available"] boolValue] ? @"planned" : @"blocked"] mutableCopy];
        trace[@"baseGoals"] = _goalPlan[@"assistRecoveryBaseGoals"] ?: _goalPlan[@"goals"] ?: @[];
        trace[@"inventory"] = _goalPlan[@"assistRecoveryInventory"] ?: @[];
        trace[@"blocker"] = _goalPlan[@"assistRecoveryBlocker"] ?: @"";
        trace[@"reason"] = _goalPlan[@"assistRecoveryMessage"] ?: @"";
        if (previous) trace[@"previousOutcome"] = previous;
        _assistTrace = [trace copy];
    } else _assistTrace = @{};
    if ([_goalMode isEqual:@"manual"] && _preview.automation.valid &&
        (_preview.automation.members[0].temporaryTurns || _preview.automation.members[5].temporaryTurns)) {
        NSMutableDictionary *plan = [_goalPlan mutableCopy];
        NSString *warning = @"隊長／助人正在暫時變身；保留指定盤面目標，這些目標不代表目前隊長的倍率或減傷已發動。";
        plan[@"warning"] = [plan[@"warning"] length] ? [plan[@"warning"] stringByAppendingFormat:@"\n%@", warning] : warning;
        _goalPlan = [plan copy];
    }
    _recoveryTurn = _auto.active() ? _auto.turn : 0;
    if (![_goalPlan[@"available"] boolValue] || !PHOrbReadGoals(_goalPlan[@"goals"], &options.goals)) {
        _phase = PHOrbSessionPhase::Blocked; _message = _goalPlan[@"message"] ?: @"求路目標無法解析";
        [self recordPlanningStatus:@"planning-blocked" reason:_message];
        if (_auto.eight()) [self rememberEightBlock:_goalPlan];
        if (_auto.active() || _oneShot) [self queueStop:_message];
        return;
    }
    const double available = TimeAvailable(_live);
    options.goals.comboFirstWave = [_goalPlan[@"enemyComboFirstWave"] boolValue];
    options.goals.bonusFirstWave = [_goalPlan[@"enemyComboBonusFirstWave"] boolValue];
    const bool multiplayer = !PHBattleIsSinglePlayer(_live.identity.entryMode, _live.identity.multiplayer);
    double remaining = available;
    if (multiplayer) {
        if (!PHOrbCanPreview(&_live) || available <= TimeMargin + StepDuration(_live)) {
            NSString *reason = @"多人本機操作窗口已關閉或時間不足，未開始求路";
            [self recordPlanningStatus:@"window-closed" reason:reason]; [self queueStop:reason]; return;
        }
        if (_live.multiplayer.kind != PHOrbMultiplayerThreeSolo) {
            const double searchRoom = _live.multiplayer.remainingSeconds - ArmDelay - TimeMargin - StepDuration(_live);
            if (searchRoom < 0.05) {
                NSString *reason = @"多人本機操作時間不足，未開始求路";
                [self recordPlanningStatus:@"window-closed" reason:reason]; [self queueStop:reason]; return;
            }
            options.milliseconds = std::min(options.milliseconds, unsigned(std::min(1.0, searchRoom) * 1000));
            remaining = std::min(available, _live.multiplayer.remainingSeconds - ArmDelay - double(options.milliseconds) / 1000);
        }
        // Until multiplayer HP derivation is validated, no damaging thorn contact.
        options.thorns = {};
    }
    // Quantize to native frames as well as the hard execution deadline. A
    // longer thorough route must remain feasible even at a lower frame rate.
    const double budget = std::min(remaining > 0 ? remaining : multiplayer ? 0 : ExecutionLimit, ExecutionLimit);
    options.steps = budget > TimeMargin ? std::max(1u, std::min(options.steps,
        unsigned((budget - TimeMargin) / StepDuration(_live)))) : 1;
    if (_auto.eight() && !_eightPrep.active) {
        _eightPrep.searchMS = 0;
        if (!_eightPrep.used && !_eightTurn.preparationDeclined && _live.wave < _eightFloorSubmitted.size() && !_eightFloorSubmitted[_live.wave] &&
            [PHOrbEightPreparationDecision(_activeProfile[@"activity"],&_live,_eightTurn.attemptedSlots)[@"available"] boolValue]) {
            // Both searches on this unchanged board share its original budget.
            _eightPrep.searchMS = std::min(150u,options.milliseconds / 3);
            options.milliseconds -= _eightPrep.searchMS;
        }
    }
    _solveOptions = options;
    [self recordPlanningStatus:@"searching" reason:nil];
    _cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = _cancel;
    const PHOrbBoard board = _preview.board;
    const bool solo = PHBattleIsSinglePlayer(_preview.identity.entryMode, _preview.identity.multiplayer) &&
        !_auto.eight() && !_preview.multiplayerDisabled;
    NSArray<NSDictionary *> *const assistCandidates = _goalPlan[@"assistRecoveryCandidates"] ?: @[];
    // Fishing is a solo fallback for one unlocked spinner. Its colour must be in
    // the spinner's own table: some spinners cycle only some colours.
    int fishCell = -1; unsigned fishSteps = 0; std::vector<unsigned> fishColors;
    if (solo && !assistCandidates.count && PHOrbSpinnerVariants(&board) > 1 && _preview.spinnerCountdownKnown &&
        _preview.spinnerPeriodFrames >= FishMinPeriod && _preview.fps >= 15 && _preview.fps <= 120 && !(board.spinnerColors & ~63u)) {
        unsigned dynamic = 0; bool thorns = false;
        for (unsigned i = 0; i < unsigned(board.width) * board.height; ++i) {
            thorns |= (board.flags[i] & PHOrbFlagThorn) != 0;
            if ((board.spinnerMask & (UINT64_C(1) << i)) && !(board.flags[i] & PHOrbFlagLocked)) { ++dynamic; fishCell = int(i); }
        }
        const double wait = FishWaitSeconds(_preview);
        const double moving = std::min(TimeAvailable(_preview), ExecutionLimit + wait) - wait - TimeMargin;
        if (moving > 0) fishSteps = std::min(options.steps, unsigned(moving / StepDuration(_preview)));
        if (dynamic != 1 || thorns || fishSteps < 2) fishCell = -1;
    }
    if (fishCell >= 0) {
        unsigned wanted = 0, counts[PHOrbBasicTypes] = {};
        for (unsigned g = 0; g < options.goals.count; ++g) wanted |= options.goals.items[g].colors;
        for (unsigned i = 0; i < unsigned(board.width) * board.height; ++i)
            if (int(i) != fishCell && board.types[i] < PHOrbBasicTypes) ++counts[board.types[i]];
        for (unsigned color = 0; color < PHOrbBasicTypes; ++color)
            if ((wanted & board.spinnerColors) & (1u << color)) fishColors.push_back(color);
        std::stable_sort(fishColors.begin(), fishColors.end(), [&](unsigned x, unsigned y) {
            return int(PHOrbRequiredShapeOrbs(&options.goals, x)) - int(counts[x]) >
                int(PHOrbRequiredShapeOrbs(&options.goals, y)) - int(counts[y]);
        });
        if (fishColors.size() > 2) fishColors.resize(2);
    }
    const NSUInteger generation = _generation;
    __weak PHOrbSession *weakSelf = self;
    _phase = PHOrbSessionPhase::Solving; _message = @"正在求路…";
    dispatch_async(_worker, ^{
      @autoreleasepool {
        PHOrbRoute route{};
        PHOrbSolveOptions selectedOptions = options;
        PHOrbSolveStatus result = PHOrbSolveStatus::NoRoute;
        NSDictionary *selectedAssist = @{};
        NSMutableArray<NSMutableDictionary *> *attempts = [NSMutableArray array];
        NSInteger selectedAttemptIndex = -1;
        NSString *soloRetrySkipped = @"";
        const auto start = std::chrono::steady_clock::now();
        const auto solveAttempt = [&](const PHOrbSolveOptions &trial, PHOrbRoute &candidate, NSDictionary *details) {
            const auto attemptStart = std::chrono::steady_clock::now();
            const auto status = PHOrbSolve(board, trial, *cancel, candidate);
            const auto wallElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - attemptStart).count();
            NSMutableDictionary *attempt = SearchAttemptTrace(trial, status, candidate, uint64_t(wallElapsed));
            [attempt addEntriesFromDictionary:details]; [attempts addObject:attempt];
            return status;
        };
        const NSUInteger choices = std::max(NSUInteger(1), assistCandidates.count);
        for (NSUInteger choice = 0; choice < choices; ++choice) {
            const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (cancel->load(std::memory_order_relaxed) || (assistCandidates.count && spent >= options.milliseconds)) break;
            PHOrbSolveOptions trial = options;
            if (assistCandidates.count && !PHOrbReadGoals(assistCandidates[choice][@"goals"], &trial.goals)) continue;
            trial.goals.comboFirstWave = options.goals.comboFirstWave;
            trial.goals.bonusFirstWave = options.goals.bonusFirstWave;
            if (assistCandidates.count) trial.milliseconds = unsigned(options.milliseconds - spent) / unsigned(choices - choice);
            if (!trial.milliseconds) break;
            PHOrbRoute candidate{};
            const auto status = solveAttempt(trial, candidate, @{});
            route = candidate; result = status; selectedOptions = trial;
            selectedAttemptIndex = NSInteger(attempts.count) - 1;
            if (assistCandidates.count) selectedAssist = assistCandidates[choice];
            if (status == PHOrbSolveStatus::Solved && candidate.goalsMet && candidate.hazardsSafe) break;
        }
        // A longer solo budget helps only if the failed candidate was cut off.
        // A completed deterministic search would otherwise repeat the same
        // candidates. NoRoute/Failed have no retained deadline statistics.
        const auto retryDecision = PHOrbDecideSoloRetry(solo, assistCandidates.count != 0,
            cancel->load(std::memory_order_relaxed), options.milliseconds, result, route);
        if (retryDecision == PHOrbSoloRetryDecision::SearchCompleted)
            soloRetrySkipped = @"search-completed-without-deadline";
        if (retryDecision == PHOrbSoloRetryDecision::Retry) {
            PHOrbSolveOptions trial = selectedOptions; trial.milliseconds = PHOrbSoloRetryMilliseconds;
            PHOrbRoute candidate{};
            const auto status = solveAttempt(trial, candidate, @{@"soloRetry":@YES});
            if (status == PHOrbSolveStatus::Solved && candidate.goalsMet && candidate.hazardsSafe) {
                route = candidate; result = status; selectedAttemptIndex = NSInteger(attempts.count) - 1;
            } else if (candidate.matchedGoals > route.matchedGoals) {
                route = candidate; result = status; selectedAttemptIndex = NSInteger(attempts.count) - 1;
            }
        }
        // Last resort before stopping: take the needed colour out of the spinner.
        for (unsigned color : fishColors) {
            if (cancel->load(std::memory_order_relaxed) ||
                (result == PHOrbSolveStatus::Solved && route.goalsMet && route.hazardsSafe)) break;
            PHOrbSolveOptions trial = selectedOptions;
            trial.fishCell = fishCell; trial.fishColor = color; trial.steps = fishSteps;
            trial.milliseconds = options.milliseconds;
            PHOrbRoute candidate{};
            const auto status = solveAttempt(trial, candidate, @{@"fishColor":@(color)});
            NSMutableDictionary *fishingAttempt = attempts.lastObject;
            fishingAttempt[@"fishing"] = @(candidate.fishing);
            if (status == PHOrbSolveStatus::Solved && candidate.goalsMet && candidate.hazardsSafe) {
                route = candidate; result = status; selectedAttemptIndex = NSInteger(attempts.count) - 1;
            }
        }
        // Retain the original configured/shared budget for existing callers.
        // The selected attempt separately records its actual slice/retry budget.
        selectedOptions.milliseconds = options.milliseconds;
        const PHOrbSolveOptions checkedOptions = selectedOptions;
        NSDictionary *selectedAttempt = selectedAttemptIndex >= 0 ? attempts[NSUInteger(selectedAttemptIndex)] : @{};
        uint64_t totalEvaluated = 0, totalEvaluationCalls = 0, totalEvaluationCacheHits = 0;
        unsigned totalSearchPasses = 0, totalBacktracks = 0, maximumExploredDepth = 0;
        bool anyAttemptDeadlineReached = false, totalStatisticsComplete = true;
        for (NSDictionary *attempt in attempts) {
            totalEvaluated += [attempt[@"evaluated"] unsignedLongLongValue];
            totalEvaluationCalls += [attempt[@"evaluationCalls"] unsignedLongLongValue];
            totalEvaluationCacheHits += [attempt[@"evaluationCacheHits"] unsignedLongLongValue];
            totalSearchPasses += [attempt[@"searchPasses"] unsignedIntValue];
            totalBacktracks += [attempt[@"backtracks"] unsignedIntValue];
            maximumExploredDepth = std::max(maximumExploredDepth, [attempt[@"exploredDepth"] unsignedIntValue]);
            anyAttemptDeadlineReached |= [attempt[@"deadline"] boolValue];
            totalStatisticsComplete &= [attempt[@"statisticsAvailable"] boolValue];
        }
        const auto searchElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        dispatch_async(dispatch_get_main_queue(), ^{
            PHOrbSession *session = weakSelf;
            if (!session || generation != session->_generation || session->_phase != PHOrbSessionPhase::Solving ||
                cancel->load(std::memory_order_relaxed)) return;
            NSMutableDictionary *trace = [session->_searchTrace mutableCopy];
            trace[@"candidateAttempts"] = [attempts copy];
            trace[@"selectedAttemptIndex"] = @(selectedAttemptIndex);
            trace[@"selectedStatus"] = selectedAttempt[@"status"] ?: @"none";
            trace[@"soloRetrySkipped"] = soloRetrySkipped;
            trace[@"statisticsScope"] = @"selected-attempt";
            trace[@"initialBudgetMS"] = @(options.milliseconds);
            trace[@"selectedBudgetMS"] = selectedAttempt[@"budgetMS"] ?: @0;
            trace[@"selectedWallElapsedMS"] = selectedAttempt[@"wallElapsedMS"] ?: @0;
            trace[@"statisticsAvailable"] = selectedAttempt[@"statisticsAvailable"] ?: @NO;
            trace[@"deadlineKnown"] = selectedAttempt[@"deadlineKnown"] ?: @NO;
            trace[@"stepLimit"] = selectedAttempt[@"stepLimit"] ?: @(checkedOptions.steps);
            trace[@"beam"] = selectedAttempt[@"beam"] ?: @(checkedOptions.beam);
            trace[@"totalEvaluated"] = @(totalEvaluated);
            trace[@"totalEvaluationCalls"] = @(totalEvaluationCalls);
            trace[@"totalEvaluationCacheHits"] = @(totalEvaluationCacheHits);
            trace[@"totalSearchPasses"] = @(totalSearchPasses); trace[@"totalBacktracks"] = @(totalBacktracks);
            trace[@"maximumExploredDepth"] = @(maximumExploredDepth);
            trace[@"anyAttemptDeadlineReached"] = @(anyAttemptDeadlineReached);
            trace[@"totalStatisticsComplete"] = @(totalStatisticsComplete && attempts.count != 0);
            trace[@"goals"] = PHOrbGoalsJSON(&checkedOptions.goals);
            trace[@"goalCount"] = @(checkedOptions.goals.count);
            trace[@"totalElapsedMS"] = @(searchElapsed);
            session->_solveOptions = checkedOptions;
            if (selectedAssist.count) {
                NSMutableDictionary *plan = [session->_goalPlan mutableCopy];
                plan[@"goals"] = selectedAssist[@"goals"]; plan[@"assistRecoveryChoice"] = selectedAssist;
                plan[@"message"] = PHOrbGoalsDescription(plan[@"goals"]); session->_goalPlan = [plan copy];
                NSMutableDictionary *decision = [session->_assistTrace mutableCopy];
                decision[@"choice"] = selectedAssist; decision[@"actualConfirmed"] = @NO;
                session->_assistTrace = [decision copy]; trace[@"assistRecovery"] = session->_assistTrace;
            }
            trace[@"path"] = RouteCells(route); trace[@"elapsedMS"] = @(route.elapsedMS);
            trace[@"evaluated"] = @(route.evaluated); trace[@"deadline"] = @(route.deadlineReached);
            trace[@"evaluationCalls"] = @(route.evaluationCalls); trace[@"evaluationCacheHits"] = @(route.evaluationCacheHits);
            trace[@"searchPasses"] = @(route.searchPasses); trace[@"backtracks"] = @(route.backtracks);
            trace[@"exploredDepth"] = @(route.exploredDepth);
            trace[@"status"] = SolveStatusName(result);
            if (result != PHOrbSolveStatus::Solved) trace[@"reason"] = SolveFailureMessage(result, board,
                checkedOptions, session->_goalPlan, assistCandidates.count != 0);
            session->_searchTrace = [trace copy];
            // A failed preferred preparation is not an output failure: output
            // was never searched on this board, so fall back instead of stopping.
            const bool priorityPreparation = session->_eightPrep.active && session->_eightPrep.priority;
            if (session->_auto.eight() && result != PHOrbSolveStatus::Solved && result != PHOrbSolveStatus::Cancelled && !priorityPreparation)
                [session rememberEightBlock:@{@"message":@"本次求路未取得完整輸出路徑",@"search":session->_searchTrace}];
            if (session->_auto.eight() && (result == PHOrbSolveStatus::InsufficientOrbs ||
                result == PHOrbSolveStatus::GoalsUnmet || result == PHOrbSolveStatus::NoRoute ||
                (priorityPreparation && result != PHOrbSolveStatus::Solved && result != PHOrbSolveStatus::Cancelled)) &&
                [session recoverEightSolve]) return;
            if ((result != PHOrbSolveStatus::Solved && result != PHOrbSolveStatus::GoalsUnmet && result != PHOrbSolveStatus::Unsafe) || route.count < 2) {
                session->_phase = PHOrbSessionPhase::Stopped;
                session->_message = SolveFailureMessage(result, board, checkedOptions, session->_goalPlan, assistCandidates.count != 0);
                if (session->_auto.active() || session->_oneShot) [session queueStop:session->_message];
                return;
            }
            PHOrbBoard replay{};
            PHOrbEvaluation evaluation{};
            uint32_t thornDamage = 0; uint8_t thornContacts = 0;
            if (!PHOrbReplay(&board, &route, route.count - 1, &replay) ||
                !PHOrbEvaluateRoute(board, route, checkedOptions, evaluation, thornDamage, thornContacts) ||
                route.thornDamage != thornDamage || route.thornContacts != thornContacts || route.heartFours != evaluation.heartFours ||
                evaluation.goalsMet != route.goalsMet || evaluation.combos != route.combos ||
                evaluation.hazardsSafe != route.hazardsSafe || evaluation.poisonErased != route.poisonErased ||
                evaluation.mortalPoisonErased != route.mortalPoisonErased ||
                evaluation.unmatchedBombs != route.unmatchedBombs ||
                evaluation.erased != route.erased || evaluation.matchedGoals != route.matchedGoals || route.goalCount != checkedOptions.goals.count ||
                !std::equal(std::begin(evaluation.goalHits), std::end(evaluation.goalHits), route.goalHits)) {
                [session queueStop:@"路徑重播校驗失敗，已停止"]; session->_phase = PHOrbSessionPhase::Fault;
                return;
            }
            trace[@"predictedBoard"] = BoardTrace(replay);
            trace[@"evaluation"] = EvaluationTrace(evaluation, checkedOptions.goals); session->_searchTrace = [trace copy];
            session->_route = route; session->_phase = PHOrbSessionPhase::Preview; session->_previewAt = session->_now;
            trace[@"fishing"] = @(route.fishing); trace[@"fishCell"] = @(route.fishCell); trace[@"fishColor"] = @(route.fishColor);
            session->_searchTrace = [trace copy];
            NSString *summary = [NSString stringWithFormat:@"預計 %u Combo（不含天降／加算）· %u 步 · 約 %.1f 秒",
                route.combos, route.count - 1, (route.count - 1) * StepDuration(session->_preview)];
            if (session->_auto.eight()) summary = [NSString stringWithFormat:
                @"首消 %u Combo＋已核對隊長加算 %@ · %u 步 · 約 %.1f 秒",
                evaluation.firstWaveCombos,session->_goalPlan[@"leaderAddedCombos"] ?: @0,
                route.count - 1,(route.count - 1) * StepDuration(session->_preview)];
            else if (checkedOptions.activity.firstWaveOnly) summary = [NSString stringWithFormat:
                @"首消 %u Combo（不含加算）· %u 步 · 約 %.1f 秒",evaluation.firstWaveCombos,
                route.count - 1,(route.count - 1) * StepDuration(session->_preview)];
            if (session->_eightPrep.active) summary = [session->_eightPrep.plan[@"scoreTurn"] boolValue] ?
                [NSString stringWithFormat:@"等待回合：首消 %u Combo · %u 步；下回合核對技能後轉色輸出",
                    evaluation.firstWaveCombos,route.count - 1] :
                [NSString stringWithFormat:@"準備回合：只消一組三珠 · %u 步；下回合核對技能後輸出",route.count - 1];
            if (route.fishing) summary = [summary stringByAppendingFormat:
                @"\n路徑會從轉轉格撈出%@珠：走到旁邊後等該色出現才進入，最多多等 %.1f 秒。",
                @[@"火",@"水",@"木",@"光",@"闇",@"心"][route.fishColor], FishWaitSeconds(session->_preview)];
            else if (board.spinnerMask && std::all_of(board.flags, board.flags + board.width * board.height,
                [](uint32_t flags) { return (flags & PHOrbFlagLocked) != 0; }))
                summary = [summary stringByAppendingString:@"\n全盤珠子已鎖定，路徑可穿過轉轉格；封條仍不可穿越。"];
            else if (board.spinnerMask) summary = [summary stringByAppendingFormat:
                @"\n路徑避開轉轉；Combo 與目標按全部 %u 種可能色序的最低結果顯示。", PHOrbSpinnerVariants(&board)];
            if (board.tapeMask) summary = [summary stringByAppendingString:@"\n已繞開封條。"];
            if (route.thornContacts) summary = [summary stringByAppendingFormat:
                @"\n荊棘碰觸 %u 次 · 傷害上限 %u · 首批心四連 %u 組；未把回血與抗性計入可承受傷害。",
                route.thornContacts, route.thornDamage, route.heartFours];
            if (options.allowPoisonWithHeartFour && route.poisonErased && route.heartFours)
                summary = [summary stringByAppendingFormat:@"\n首消心四連已達成；按隊伍設定允許消毒／猛毒，合計最多 %u 顆。", route.poisonErased];
            summary = [summary stringByAppendingFormat:@"\n求路耗時 %u ms（全部候選）\n選中候選 %u ms／預算 %u ms · %@ · 步數上限 %u",
                unsigned(searchElapsed), route.elapsedMS, [selectedAttempt[@"budgetMS"] unsignedIntValue],
                route.deadlineReached ? @"候選已到時間上限" : @"候選未到時間上限",
                [selectedAttempt[@"stepLimit"] unsignedIntValue]];
            if (!route.goalsMet) {
                NSArray<NSDictionary *> *goals = PHOrbGoalsJSON(&checkedOptions.goals);
                NSMutableArray<NSString *> *missing = [NSMutableArray array];
                if (checkedOptions.activity.preparation) [missing addObject:
                    @"未找到只消一組三珠、無後續連鎖且保留轉色珠量足夠的路徑"];
                for (unsigned g = 0; g < checkedOptions.goals.count; ++g) if (route.goalHits[g] < checkedOptions.goals.items[g].count)
                    [missing addObject:[NSString stringWithFormat:@"未達成：%@（%u／%u）",
                        PHOrbGoalDescription(goals[g]), route.goalHits[g], checkedOptions.goals.items[g].count]];
                summary = [NSString stringWithFormat:@"%@\n%@", [missing componentsJoinedByString:@"\n"], summary];
            }
            session->_message = !route.hazardsSafe ? [NSString stringWithFormat:
                @"未找到符合妨礙處理條件的路徑：毒／猛毒消除最多 %u 顆（猛毒最多 %u 顆）、首批未消爆彈最多 %u 顆%@。未開放執行；可先用技能改盤。\n%@",
                route.poisonErased, route.mortalPoisonErased, route.unmatchedBombs,
                route.thornContacts && !route.heartFours ? @"、荊棘路徑未消出心四連" : @"", summary] : !route.goalsMet ? [NSString stringWithFormat:@"目標僅達成 %u／%u 項，未開放執行。本次搜尋未找到完整路徑，不代表盤面無解。\n%@",
                route.matchedGoals, route.goalCount, summary] : route.goalCount ?
                [NSString stringWithFormat:@"盤面目標已達成 %u／%u 項\n%@", route.matchedGoals, route.goalCount, summary] : summary;
            if (!route.hazardsSafe)
                session->_message = [PoisonFailureContext(evaluation, options) stringByAppendingString:session->_message];
            if (!route.goalsMet || !route.hazardsSafe) {
                trace[@"reason"] = session->_message; session->_searchTrace = [trace copy];
            }
            if ((session->_auto.active() || session->_oneShot) && (!route.goalsMet || !route.hazardsSafe)) [session queueStop:session->_message];
        });
      }
    });
}
- (void)finishNativeFrame {
    NSString *reason = nil;
    if (![_engine finishInput:&reason]) {
        _inputFault = true;
        [self queueStop:reason ?: @"輸入恢復失敗，已停止"];
        _phase = PHOrbSessionPhase::Fault; _ownedOrb = 0; _staged = false;
    }
}
- (void)beforeFrame:(PHBattleFrame)frame time:(NSTimeInterval)time {
    [self finishNativeFrame]; // Also retries an earlier restoration failure.
    _frame = frame; _now = _frameAt = time;
    _staged = _releaseStaged = false;
    if (_oneShot && (!frame.ready || !frame.enabled)) [self queueStop:@"功能狀態已改變，已取消轉一次"];
    if (_auto.active() && ![self executing]) {
        [self beforeAutomationFrame:frame time:time]; return;
    }
    if (_inputFault || ![self executing]) return;
    NSString *reason = nil;
    PHOrbSnapshot current{};
    BOOL read = (_auto.active() || _openingInFlight || _openingPuzzlePending ||
        (_historyBound && AcceptedSkillTiming(_historyRules))) ?
        [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    if (read && PHOrbBattleEnded(&current)) {
        _hasLive = true; _live = current;
        [self queueStop:BattleEndMessage(current)];
        // The private manager is already removed; native result handling owns
        // any former drag. Do not send a release into this terminal scene.
        _ownedOrb = 0; _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false;
        return;
    }
    if (!read || !current.valid || current.multiplayerDisabled ||
        !PHOrbSameTurn(&_expected, &current) || current.phase != 2) {
        // The private manager has already been removed. Never inject stale
        // cleanup into a new scene/multiplayer; native input now owns release.
        if (_phase != PHOrbSessionPhase::Releasing || _releaseComplete)
            [self queueStop:@"回合、場景或可操作狀態已改變，已停止"];
        _ownedOrb = 0; _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false;
        return;
    }
    const bool multiplayer = !PHBattleIsSinglePlayer(current.identity.entryMode, current.identity.multiplayer);
    if (multiplayer && !PHOrbMultiplayerWindow(&current)) {
        [self queueStop:reason ?: @"多人已交接、同步、結算或計時結束，已停止輸入"];
        _ownedOrb = 0; _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false;
        return;
    }
    _live = current; _hasLive = true;
    if (_auto.active()) {
        const BOOL observed = [self observeAutomation:&current read:read reason:reason];
        // A pre-touch closure can return to rule waiting without starting a
        // gesture. A busy window must not turn that deferral into a stop.
        if (observed && ![self executing]) return;
        if (observed && current.automation.busy) [self queueStop:@"轉珠前技能或動畫狀態已改變，已停止"];
        // Fall through to the existing owned-orb release path when needed.
    }
    if (_auto.active() && !_auto.eight() && ((_phase == PHOrbSessionPhase::Arming) || (_phase == PHOrbSessionPhase::Running)) &&
        (![self validateAssistRecovery:&current] || ![self validateEnemyCombo:&current])) {
        // A failure revokes holds, while retaining the existing owned release.
        if (_phase != PHOrbSessionPhase::Releasing) return;
    }
    if (_auto.eight() && ((_phase == PHOrbSessionPhase::Arming) || (_phase == PHOrbSessionPhase::Running)) &&
        (!PHOrbSameEightTeam(&_preview.eight,&current.eight) || !PHOrbSameEnemyCombo(&_preview.enemyCombo,&current.enemyCombo))) {
        [self queueStop:@"8 人輸入前固定隊伍或吸收狀態已改變，已停止"];
    }
    if (_phase == PHOrbSessionPhase::Arming) {
        if (!frame.enabled || !PHOrbCanPreview(&current) || !SamePosition(current, _preview)) {
            [self queueStop:@"執行前盤面或開關已改變，請重新讀盤"]; return;
        }
        if (![self hasThornBudget:&current]) {
            [self queueStop:@"執行前荊棘傷害預算已不符合，已停止"]; return;
        }
        if (time - _requestedAt < ArmDelay) return;
        if (time - _requestedAt > 2 || !HasRouteTime(current, _route)) {
            [self queueStop:time - _requestedAt > 2 ? @"開始轉珠等候超過 2 秒，已停止" :
                @"剩餘操作時間不足以完成路徑及必要等待，未開始轉珠"]; return;
        }
        _phase = PHOrbSessionPhase::Running; _startedAt = _stepAt = _sentAt = time;
        _expected = current;
    }
    if (_phase == PHOrbSessionPhase::Running) {
        _executionElapsed = std::max(0.0, time - _startedAt);
        const double executionLimit = ExecutionLimit + (_route.fishing ? FishWaitSeconds(_preview) : 0);
        NSString *stopReason = !frame.enabled ? @"總開關或前景狀態已改變，已停止" :
            !PHInputIsQuiet(&current.input) ? @"偵測到手動操作，已停止並放開" :
            !current.ordinary ? @"盤面操作限制已改變，已停止" :
            multiplayer && TimeAvailable(current) <= TimeMargin ? @"多人剩餘時間不足，已停止並放開" :
            time < _startedAt ? @"執行時鐘異常，已停止" :
            time - _startedAt > executionLimit ? [NSString stringWithFormat:@"轉珠執行超過 %.1f 秒上限（已確認 %lu／%u 步），已停止",
                executionLimit, (unsigned long)_confirmed, _route.count ? _route.count - 1 : 0] :
            !current.dragged && !_awaitingStart ? @"遊戲已結束拖珠，已停止" : nil;
        if (stopReason) {
            [self queueStop:stopReason];
        } else if (!_awaitingStart && current.dragged != _ownedOrb) {
            [self queueStop:@"拖珠已由遊戲接管，已停止"]; _ownedOrb = 0; _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false; return;
        } else if (!SamePosition(current, _expected)) {
            [self queueStop:@"盤面與已確認步驟不同，已停止"];
        } else if (!_awaitingStart && _target == _confirmed && time - _stepAt >= StepSeconds) {
            if (_confirmed + 1 == _route.count) {
                // Re-evaluate the actual board at the release boundary. Do
                // not reuse a solver score or credit the dragged orb twice.
                PHOrbEvaluation evaluation{};
                const bool valid = PHOrbEvaluateWithOptions(current.board, _solveOptions, evaluation);
                [self recordExecution:@"release-check" state:&current evaluation:valid ?
                    EvaluationTrace(evaluation, _solveOptions.goals) : @{@"valid":@NO}];
                if (!valid || !evaluation.goalsMet || !evaluation.hazardsSafe ||
                    (_route.thornContacts && !evaluation.heartFours)) {
                    NSString *failure = !valid ? @"放手前無法核對實際盤面" :
                        !evaluation.goalsMet ? @"放手前實際盤面未達成全部目標" :
                        !evaluation.hazardsSafe ? @"放手前實際盤面未通過毒／爆彈處理條件" :
                        @"放手前荊棘路徑缺少首消心四連";
                    [self queueStop:[failure stringByAppendingString:@"；已停止並放開"]];
                } else {
                    if (_auto.eight() && _eightPrep.active) {
                        // Consume the one-turn allowance when release is committed,
                        // even if Stop interrupts receipt of the native release.
                        _eightPrep.used = _eightPrep.awaiting = true;
                        NSMutableDictionary *trace = [_eightPrep.trace mutableCopy];
                        trace[@"status"] = @"release-committed"; trace[@"releaseEvaluation"] = EvaluationTrace(evaluation,_solveOptions.goals);
                        _eightPrep.trace = [trace copy];
                    }
                    _phase = PHOrbSessionPhase::Releasing; _releaseComplete = true; _message = @"路徑完成，正在放開…";
                }
            } else if (_route.fishing && !_fishEntered && _route.cells[_confirmed + 1] == _route.fishCell &&
                !(current.spinnerCountdownKnown && current.spinnerCountdown >= FishMinCountdown &&
                  current.board.types[_route.fishCell] == _route.fishColor)) {
                _message = @"等待轉轉轉出路徑需要的顏色…"; // Keep holding the orb next to the cell.
            } else {
                const bool entering = _route.fishing && _route.cells[_confirmed + 1] == _route.fishCell;
                if (entering && _recordingExecution && _executionHistory.count) {
                    // Device evidence: how long the hold took and how many frames were left.
                    NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
                    record[@"fishEntry"] = @{@"step":@(_confirmed + 1), @"cell":@(_route.fishCell),
                        @"color":@(current.board.types[_route.fishCell]), @"countdown":@(current.spinnerCountdown),
                        @"elapsedMS":@(unsigned(_executionElapsed * 1000)),
                        @"sinceLastStepMS":@(unsigned(std::max(0.0, time - _stepAt) * 1000))};
                    _executionHistory[_executionHistory.count - 1] = [record copy];
                }
                _fishEntered |= entering;
                _target = _confirmed + 1; _stepAt = _sentAt = time;
            }
        }
    }
    if (_phase == PHOrbSessionPhase::Releasing) {
        // Stop can arrive after began but before the native acknowledgement.
        // Only the captured starting token with unchanged board/turn is ours.
        if (!_ownedOrb && _awaitingStart && _startSent && _route.count &&
            current.dragged == _preview.board.tokens[_route.cells[0]] &&
            PHInputIsQuiet(&current.input) && SamePosition(current, _preview))
            _ownedOrb = current.dragged;
        if (!current.dragged || !_ownedOrb || current.dragged != _ownedOrb) {
            if (_releaseComplete) [self queueStop:@"遊戲已接管放開，已停止"];
            _ownedOrb = 0; _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false;
            return;
        }
        _expected = current;
        // Use the dragged orb's actual cell when a partial move was rejected.
        NSUInteger cell = 0;
        const NSUInteger cells = current.board.width * current.board.height;
        for (; cell < cells; ++cell) if (current.board.tokens[cell] == _ownedOrb) break;
        _staged = [_engine stageCell:cell down:NO beginning:NO expected:&current frame:frame reason:&reason];
        _releaseStaged = _staged;
        if (!_staged) { _ownedOrb = 0; [self queueStop:reason ?: @"已停止輸入，等待遊戲結束拖珠"]; _phase = PHOrbSessionPhase::Stopped; }
        return;
    }
    if (_phase != PHOrbSessionPhase::Running) return;
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
    if (_dungeonBound && frame.player &&
        (frame.player != _dungeonIdentity.player || frame.epoch != _dungeonIdentity.epoch)) {
        _dungeonBound = false; _dungeonFloors = nil; _dungeonPages = nil; _dungeonSamples = nil;
        _dungeonDefinitionSets = nil; _dungeonSampleKey = nil; _dungeonMetadata = nil; _dungeonLastStop = nil;
        _dungeonTableSHA = nil; _dungeonStatus = @"not-requested"; _dungeonIdentity = {};
        _dungeonCursor = _dungeonRefreshCursor = _dungeonDropped = 0;
    }
    if (_inputFault) return;
    const BOOL active = [self executing];
    // Keep observing scene exits after transient read failures. Otherwise a
    // multiplayer latch could survive into the next solo battle forever.
    // Opening the orb UI also needs a first read before multiplayer buttons
    // can become available. This observation alone never creates an intent.
    if (!active && !_monitoring && !_readinessMonitoring) return;
    const bool readinessOnly = !active && !_monitoring;
    if (readinessOnly && (!frame.ready || !frame.enabled)) return;
    if (!active && !_auto.active() && !_oneShot && time >= _lastRead &&
        time - _lastRead < (readinessOnly ? 0.25 : 0.10)) return;
    _lastRead = time;
    PHOrbSnapshot current{}; NSString *reason = nil;
    const bool eightProgress = !_auto.active() && !_oneShot && !active && !_readQueued &&
        ((_phase == PHOrbSessionPhase::Stopped) || (_phase == PHOrbSessionPhase::Idle)) &&
        _eightSkillPending && _eightSkillStarted && (!_eightSkillAcknowledged || !_eightSkillSettled) &&
        [_engine respondsToSelector:@selector(readEightProgress:expected:frame:reason:)];
    BOOL read = eightProgress ? [_engine readEightProgress:&current expected:&_eightHistory frame:frame reason:&reason] :
        (_auto.active() || _openingInFlight || _openingPuzzlePending ||
        (_historyBound && AcceptedSkillTiming(_historyRules))) ?
        [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    if (!_auto.active() && read && PHOrbSameEightMatch(&_eightHistory,&current)) {
        [self rememberEightSkill:&current];
        if (ResolvingOrbs(current.phase) || current.multiplayer.resolvePending) {
            if (_eightBoardSubmitted) _auto.sawResolve = true;
            else _eightPausedResolve = true;
        }
    }
    _hasLive = read; _live = current;
    _liveReadReason = [reason copy] ?: @""; _lastReadUptime = NSProcessInfo.processInfo.systemUptime;
    if (!readinessOnly) [self refreshBattleObservation:read active:active time:time];
    if (!read) {
        [self recordReadIssue:&current reason:reason ?: @"戰鬥資料未通過讀取校驗" multiplayer:YES];
    }
    if ([self tryCountdownAutomatic:&current read:read active:active]) return;
    if (readinessOnly) return;
    if (_oneShot) {
        const bool multiplayer = _oneShotOrigin.multiplayer.kind != PHOrbMultiplayerNone;
        if (!read || !current.valid || !current.present || current.multiplayerDisabled || !_oneShotBound ||
            !PHBattleSameIdentity(&_oneShotOrigin.identity, &current.identity) || current.wave != _oneShotOrigin.wave ||
            !PHOrbSameMultiplayerTurn(&_oneShotOrigin.multiplayer, &current.multiplayer)) {
            [self queueStop:!read ? (reason ?: @"讀盤失敗，已取消轉一次") :
                @"場景、本機席位或回合已改變，已取消轉一次"];
        } else if (multiplayer && !active &&
            (!PHOrbCanPreview(&current) || !SamePosition(current, _oneShotOrigin) || TimeAvailable(current) <= TimeMargin)) {
            [self queueStop:!PHOrbCanPreview(&current) ? (reason ?: @"多人操作窗口已關閉") :
                @"求路前後盤面或剩餘時間已改變，請重新按轉一次"];
        } else if (multiplayer && active && !_releaseStaged && !PHOrbMultiplayerWindow(&current)) {
            [self queueStop:reason ?: @"多人操作窗口已關閉，已停止本次輸入"];
        }
    }
    if (!_auto.active()) [self observeRuleHistory:&current read:read];
    if (!_auto.active() && _openingInFlight && _openingStarted && read && current.valid && current.automation.valid && !current.multiplayerDisabled &&
        PHBattleSameIdentity(&_openingBefore.identity, &current.identity)) {
        if (current.automation.skillUses == _openingBefore.automation.skillUses + 1) _openingAcknowledged = true;
        if (!_openingAcknowledged && !current.boardTransition && current.phase == 2 && current.settled && !current.automation.busy && !current.dragged) {
            _openingStarted = false;
            NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"unconfirmed";
            _openingLog[_openingLog.count - 1] = [record copy];
        }
        [self finishOpeningIfConfirmed:&current];
    }
    if (_auto.active() && ![self observeAutomation:&current read:read reason:reason]) return;
    if (active && _staged) {
        if (!read || !current.valid || current.multiplayerDisabled ||
            (!PHOrbSameTurn(&_expected, &current) && !(_auto.eight() && _releaseStaged && !current.dragged &&
                PHOrbSameEightMatch(&_auto.origin,&current)))) {
            if (_phase != PHOrbSessionPhase::Releasing || _releaseComplete)
                [self queueStop:@"原生執行後場景已改變，已停止"];
            _ownedOrb = 0; _phase = PHOrbSessionPhase::Stopped; _recordingExecution = false; return;
        }
        if (_releaseStaged) {
            if (current.dragged == 0) {
                [self recordExecution:_releaseComplete ? @"released" : @"cancelled-released" state:&current evaluation:nil];
                if (_recordingExecution) {
                    NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy]; record[@"releaseConfirmed"] = @YES;
                    _executionHistory[_executionHistory.count - 1] = [record copy]; _recordingExecution = false;
                }
                _ownedOrb = 0;
                // The native frame has acknowledged this release. A Stop tap
                // before the next frame must not resurrect its staged intent.
                _staged = _releaseStaged = false;
                [_engine armMultiplayerOnce:NO];
                [self openSpinnerCell:-1];
                _phase = _releaseComplete ? PHOrbSessionPhase::Complete : PHOrbSessionPhase::Stopped;
                if (_releaseComplete) {
                    _oneShot = _oneShotBound = false;
                    ++_completed; _message = current.multiplayer.kind != PHOrbMultiplayerNone ?
                        @"本次路徑已執行並放開；下一盤可操作時可再按轉一次" :
                        @"本次路徑已執行並放開；下一回合請重新讀盤";
                    if (_auto.active()) {
                        if ([_goalPlan[@"assistRecoveryActive"] boolValue]) {
                            _assistReleased = _preview; _assistAwaitingResult = true;
                            NSMutableDictionary *trace = [_assistTrace mutableCopy];
                            trace[@"status"] = @"awaiting-next-turn"; trace[@"actualConfirmed"] = @NO;
                            _assistTrace = [trace copy];
                        }
                        ++_auto.moves; _auto.sawResolve = ResolvingOrbs(current.phase) ||
                            (_auto.eight() && current.multiplayer.resolvePending); _hasStable = false;
                        if (_auto.eight()) {
                            if (_preview.wave < _eightFloorSubmitted.size()) _eightFloorSubmitted[_preview.wave] = true;
                            if (!_eightFirstPuzzle && _preview.wave == 0) _eightFirstPuzzle = [_executionHistory.lastObject copy];
                            _eightSubmission = _expected;
                            _eightSubmission.moves = std::max(current.moves,_expected.moves);
                            _eightBoardSubmitted = true;
                        }
                        _phase = PHOrbSessionPhase::TurnWaiting; _auto.phaseAt = time;
                        _message = @"本回合已放開，等待消除、敵人行動與下一回合…";
                    }
                }
            } else if (time - _startedAt > ExecutionLimit + (_route.fishing ? FishWaitSeconds(_preview) : 0) + 0.5) {
                _ownedOrb = 0; [self queueStop:@"已停止輸入；遊戲尚未確認放開，請手動檢查"]; _phase = PHOrbSessionPhase::Stopped;
            }
            return;
        }
        if (_phase == PHOrbSessionPhase::Releasing) {
            // Cancellation during native began may still produce an owned
            // token. Capture it for release without replacing the stop reason.
            if (!_ownedOrb && _awaitingStart && _startSent && _route.count &&
                current.dragged == _preview.board.tokens[_route.cells[0]] &&
                PHInputIsQuiet(&current.input) && SamePosition(current, _preview))
                _ownedOrb = current.dragged;
            _expected = current;
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
            const bool multiplayer = current.multiplayer.kind != PHOrbMultiplayerNone;
            if (current.dragged == _preview.board.tokens[_route.cells[0]] && SamePosition(current, _preview) &&
                (!multiplayer || PHInputIsQuiet(&current.input))) {
                _ownedOrb = current.dragged; _awaitingStart = false; _expected = current; _stepAt = _frameAt;
                [self beginExecutionRecord:&current];
                _message = @"正在執行；點浮動「停止」即可中斷";
            } else if (current.dragged || time - _startedAt > 0.20) {
                _ownedOrb = current.dragged == _preview.board.tokens[_route.cells[0]] &&
                    (!multiplayer || PHInputIsQuiet(&current.input)) ? current.dragged : 0;
                [self queueStop:@"遊戲未接受起點，已停止"];
            }
            return;
        }
        PHOrbBoard expectedBoard{};
        const bool replayed = PHOrbReplay(&_preview.board, &_route, unsigned(_target), &expectedBoard);
        if (replayed && current.dragged == _ownedOrb && current.phase == 2 && PHOrbSamePlanBoard(&current.board, &expectedBoard) &&
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
            [self queueStop:SamePosition(current, _expected) && current.dragged == _ownedOrb ?
                @"等待遊戲確認換珠超過 0.20 秒，已停止並放開" : @"原生交換結果與路徑不符，已停止"];
        }
        return;
    }
    if ((_phase == PHOrbSessionPhase::Preview) || (_phase == PHOrbSessionPhase::Solving)) {
        if (!read || !PHOrbCanPreview(&current) || !SamePosition(current, _preview) ||
            ((_phase == PHOrbSessionPhase::Preview) && time - _previewAt > PreviewLifetime)) {
            [self queueStop:(!read || !PHOrbCanPreview(&current)) ? (reason ?: @"盤面已無法讀取，請重新讀盤") :
                @"盤面、回合或預覽有效期已改變，請重新讀盤"];
        }
    }
    if (_readQueued) {
        if (!read || !PHOrbCanPreview(&current)) {
            _hasStable = false; _message = reason ?: @"盤面未通過讀取校驗";
            if (time - _requestedAt > 2) {
                _readQueued = false; _phase = PHOrbSessionPhase::Blocked;
                if (_oneShot) [self queueStop:_message];
            }
        } else if (!_hasStable || !SamePosition(current, _stable)) {
            _stable = current; _stableAt = time; _hasStable = true;
        } else if (time - _stableAt >= 0.15) [self beginSolve];
    }
    if ((_auto.active() || _oneShot) && (_phase == PHOrbSessionPhase::Preview)) {
        if (!HasRouteTime(_live, _route, ArmDelay)) [self queueStop:@"自動路徑超出操作時間，已停止"];
        else [self queueExecute:_generation];
    }
    [self advanceAutomation];
}

@end
