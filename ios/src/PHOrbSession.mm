#import "PHOrbSession.h"
#import "PHOrbPlanning.h"
#import "PHOrbRules.h"
#import "PHOrbProfiles.h"
#import "PHOrbSkillWindow.h"
#include "PHOrbSolver.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <chrono>

namespace {
#include "PHOrbObservationPresentation.inc"
constexpr double StepSeconds = 0.065, PreviewLifetime = 45, ExecutionLimit = 4, TimeMargin = 0.40;
constexpr double ArmDelay = 0.35;
constexpr double AutomationSettle = 0.20, SkillTimeout = 30, TurnTimeout = 90;
constexpr double SkillWindowTimeout = 3;
bool ResolvingOrbs(uint32_t phase) { return phase >= 3 && phase <= 6; }
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
    const double required = (route.count - 1) * StepDuration(state) + TimeMargin;
    if (required > std::min(TimeAvailable(state), ExecutionLimit)) return false;
    return PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer) ||
        state.multiplayer.kind == PHOrbMultiplayerThreeSolo ||
        required + delay <= state.multiplayer.remainingSeconds;
}
NSString *MultiplayerModeName(PHOrbMultiplayerKind kind) {
    switch (kind) {
        case PHOrbMultiplayerNone: return @"單人";
        case PHOrbMultiplayerThree: return @"3 人協力";
        case PHOrbMultiplayerThreeSolo: return @"3 人協力・單人續玩";
        case PHOrbMultiplayerFour: return @"4 人對戰";
        case PHOrbMultiplayerEight: return @"8 人對戰";
        case PHOrbMultiplayerUnsupported: return @"未支援的多人模式";
    }
    return @"未知模式";
}
NSString *MultiplayerWaitMessage(const PHOrbSnapshot &state) {
    // Presentation only. PHOrbCanPreview and the final canOnce gate remain
    // authoritative; no message can grant input permission.
    const auto &m = state.multiplayer;
    const bool continuation = m.kind == PHOrbMultiplayerThreeSolo;
    if (!m.known || !m.teamCount || m.localSeat >= m.teamCount || m.viewTeam >= m.teamCount)
        return @"本機席位或回合資料尚未確認";
    if (m.kind == PHOrbMultiplayerThree && m.turnSerial % m.teamCount != m.localSeat)
        return @"等待自己的回合，再按轉一次";
    if (m.viewTeam != m.localSeat) return @"目前顯示其他玩家，請返回自己的盤面";
    if (!PHOrbMultiplayerLocalReady(&m))
        return continuation ? @"等待原生單人續玩切換完成" : @"等待多人同步或連線恢復";
    if (!continuation && m.kind != PHOrbMultiplayerThree && m.finished) return @"對戰已結束，等待結算";
    if (!continuation && m.kind != PHOrbMultiplayerThree && m.countdownReady != 1) return @"等待對戰開始倒數完成";
    if (m.resolvePending || state.phase != 2) return @"等待本盤結算或下一盤";
    if (continuation && (state.fps < 15 || state.fps > 120 || !state.remainingFrames || state.remainingFrames > 30000))
        return @"單人續玩的轉珠時間尚未確認或已結束";
    if (!continuation && (!m.clockKnown || !std::isfinite(m.remainingSeconds) || m.remainingSeconds > 86400))
        return @"多人操作時間尚未確認";
    if (!continuation && m.remainingSeconds <= 0) return @"多人操作時間已結束";
    if ((m.kind == PHOrbMultiplayerThree && (m.clockFlags & 2u) && !state.dragged) ||
        (!continuation && m.kind != PHOrbMultiplayerThree && (m.clockFlags & 0x36u) != 4u))
        return @"多人計時暫停或已結束";
    if (state.boardTransition || !state.settled) return @"等待盤面穩定，再按轉一次";
    if (state.dragged || !PHInputIsQuiet(&state.input)) return @"請先放開手指，再按轉一次";
    if (!PHOrbCanPreview(&state)) return @"盤面尚未通過校驗，暫不能轉一次";
    if (TimeAvailable(state) <= ArmDelay + TimeMargin + StepDuration(state))
        return @"剩餘時間不足，暫不能轉一次";
    return @"目前不可轉一次，請稍後重試";
}
NSDictionary *MultiplayerTrace(const PHOrbSnapshot &state) {
    const auto &m = state.multiplayer;
    return @{@"mode":MultiplayerModeName(m.kind), @"kind":@(m.kind),
        @"entryMode":@(state.identity.entryMode), @"selector":@(state.identity.multiplayer),
        @"known":@(m.known), @"disabled":@(state.multiplayerDisabled), @"window":@(PHOrbMultiplayerWindow(&state)),
        @"turnSerial":@(m.turnSerial), @"teamCount":@(m.teamCount), @"localSeat":@(m.localSeat), @"viewTeam":@(m.viewTeam),
        @"localStatus":@(m.localStatus), @"networkFlags":@(m.networkFlags), @"syncState":@(m.syncState),
        @"syncStep":@(m.syncStep), @"localReady":@(PHOrbMultiplayerLocalReady(&m)),
        @"disconnectKnown":@(m.disconnectKnown), @"disconnectSeat":@(m.disconnectKnown ? int(m.disconnectSeat) : -1),
        @"disconnectStatus":@(m.disconnectStatus),
        @"continuation":@(m.kind == PHOrbMultiplayerThreeSolo), @"continuationOwnerKnown":@(m.continuationOwnerKnown),
        @"continuationOwnerFlags":@(m.continuationOwnerFlags),
        @"continuationDialog":@(m.continuationDialog), @"recoverySource":@(m.recoverySource), @"recoveryFlags":@(m.recoveryFlags),
        @"timeSource":m.kind == PHOrbMultiplayerThreeSolo ? @"move-timer" : m.kind == PHOrbMultiplayerNone ? @"none" : @"multiplayer-clock",
        @"networkReady":@(PHOrbMultiplayerNetworkReady(&m)),
        @"networkRequired":@(m.kind != PHOrbMultiplayerNone && m.kind != PHOrbMultiplayerThreeSolo),
        @"connectionState":@(m.connectionState), @"countdownReady":@(m.countdownReady),
        @"finished":@(m.finished), @"resolvePending":@(m.resolvePending), @"clockFlags":@(m.clockFlags),
        @"clockKnown":@(m.clockKnown), @"clockStart":@(m.clockStart),
        @"remainingSeconds":@(std::isfinite(m.remainingSeconds) ? m.remainingSeconds : 0)};
}
NSDictionary *LeaderReadTrace(const PHOrbSnapshot &state) {
    const auto &d = state.leaderRead;
    // These are failed-check observations, never a partially trusted pair.
    return @{@"status":@(PHOrbLeaderReadStatusName(d.status)), @"pass":@(d.pass),
        @"visibleSlot":@(d.pass == 1 || d.pass == 2 ? int(d.visibleSlot) : -1),
        @"catalogCount":@(d.catalogCount), @"catalogRecords":@(d.catalogRecords), @"skillCount":@(d.skillCount),
        @"catalogIndex":@(d.catalogIndex), @"catalogID":@(d.catalogID), @"parameterID":@(d.parameterID),
        @"skillID":@(d.skillID), @"sourceSlot":@(d.sourceSlot), @"sourceCatalogID":@(d.sourceCatalogID),
        @"formID":@(d.formID), @"temporaryID":@(d.temporaryID), @"temporaryTurns":@(d.temporaryTurns),
        @"activeFlags":@(d.activeFlags), @"extraFlags":@(d.extraFlags),
        @"extraCount":@(d.extraCount), @"extraSelection":@(d.extraSelection),
        @"extraRecord":@(d.extraRecord), @"extraSkill":@(d.extraSkill),
        @"ownerFlags":@(d.ownerFlags), @"ownerBusy":@(d.ownerBusy),
        @"ownerBankMatches":@(d.ownerBankMatches), @"parameterMatches":@(d.parameterMatches)};
}
NSDictionary *LeaderTrace(const PHOrbSnapshot &state) {
    const auto &c = state.leaderContext;
    // Deliberately omit process pointers. Sources use zero-based native slots.
    return @{@"known":@(c.known), @"ready":@(PHOrbLeaderContextReady(&state)),
        @"catalogIDs":@[@(c.catalogIDs[0]), @(c.catalogIDs[1])],
        @"skillIDs":@[@(c.skillIDs[0]), @(c.skillIDs[1])],
        @"sourceSlots":@[@(c.sourceSlots[0]), @(c.sourceSlots[1])],
        @"sourceCatalogIDs":@[@(c.sourceCatalogIDs[0]), @(c.sourceCatalogIDs[1])],
        @"formIDs":@[@(c.formIDs[0]), @(c.formIDs[1])],
        @"temporaryIDs":@[@(c.temporaryIDs[0]), @(c.temporaryIDs[1])],
        @"temporaryTurns":@[@(c.temporaryTurns[0]), @(c.temporaryTurns[1])],
        @"activeFlags":@[@(c.activeFlags[0]), @(c.activeFlags[1])],
        @"extraFlags":@[@(c.extraFlags[0]), @(c.extraFlags[1])],
        @"extraHelper":@(c.extraHelper), @"extraSelection":@(c.extraSelection), @"extraRecord":@(c.extraRecord),
        @"extraSkill":@(c.extraSkill), @"read":LeaderReadTrace(state)};
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
bool HasBoardObservation(const PHOrbSnapshot &state) {
    if (!state.valid || !state.present || state.phase != 2 || state.boardTransition ||
        !PHOrbDimensionsSupported(state.board.width, state.board.height)) return false;
    // Metadata-only reads retain dimensions, including phase-2 multiplayer
    // release acknowledgements. Only a complete token table proves that the
    // orb values were actually read; zero values alone are valid fire orbs.
    for (unsigned i = 0; i < state.board.width * state.board.height; ++i)
        if (!state.board.tokens[i]) return false;
    return true;
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
        @"mortalPoisonErased":@(value.mortalPoisonErased),
        @"unmatchedBombs":@(value.unmatchedBombs), @"heartFours":@(value.heartFours)};
}
NSString *SolveStatusName(PHOrbSolveStatus status) {
    switch (status) {
        case PHOrbSolveStatus::Solved: return @"solved";
        case PHOrbSolveStatus::GoalsUnmet: return @"goals-unmet";
        case PHOrbSolveStatus::Unsafe: return @"unsafe";
        case PHOrbSolveStatus::InsufficientOrbs: return @"insufficient-orbs";
        case PHOrbSolveStatus::IncompatibleGoals: return @"incompatible-goals";
        case PHOrbSolveStatus::NoRoute: return @"no-route";
        case PHOrbSolveStatus::Cancelled: return @"cancelled";
        case PHOrbSolveStatus::Invalid: return @"invalid";
        case PHOrbSolveStatus::Failed: return @"failed";
    }
    return @"unknown";
}
NSString *PoisonFailureContext(const PHOrbEvaluation &evaluation, const PHOrbSolveOptions &options) {
    if (!evaluation.poisonErased) return @"";
    if (!options.allowPoisonWithHeartFour) return [NSString stringWithFormat:
        @"候選路徑會消除毒／猛毒共 %u 顆；目前策略設定為避毒。\n", evaluation.poisonErased];
    return !evaluation.heartFours ? @"候選路徑會消毒／猛毒，但未在全部轉轉相位達成首消心四連；未開放執行。\n" : @"";
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
        @"assistCatalogID":@(member.assistCatalogID), @"assistState":@(member.assistState), @"orbCounts":orbCounts,
        @"boardWidth":@(state.board.width), @"boardHeight":@(state.board.height),
        @"spinnerMask":@(state.board.spinnerMask), @"observedPhase":@(state.phase),
        @"observedWave":@(state.wave), @"observedSkillUses":@(state.automation.skillUses), @"nativeAttempt":@(attempted),
        @"skillWindow":@(state.automation.window), @"skillBusy":@(state.automation.busy),
        @"skillWindowBlocks":@(state.automation.windowBlocks), @"skillWindowBlocksKnown":@(state.automation.windowBlocksKnown),
        @"skillWindowReason":PHOrbSkillWindowDescription(&state.automation)};
}
}

@implementation PHOrbSession {
    id<PHOrbEngine> _engine;
    dispatch_queue_t _worker;
    std::shared_ptr<std::atomic_bool> _cancel;
    PHBattleFrame _frame;
    PHOrbSnapshot _live, _stable, _preview, _expected, _decisionState, _traceOrigin;
    PHOrbRoute _route;
    NSString *_phase, *_message, *_quality, *_goalMode;
    bool _thornComboPriority, _allowPoisonWithHeartFour, _diagonal, _assistRecoveryL;
    bool _assistAwaitingResult;
    double _assistCheckedAt;
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
    bool _openingInFlight, _openingAcknowledged, _openingStarted, _openingPuzzlePending;
    PHOrbSnapshot _openingBefore;
    NSDictionary *_searchTrace;
    NSDictionary *_lastReadIssue;
    NSString *_liveReadReason;
    double _lastReadUptime;
    PHOrbSnapshot _observationState;
    double _observationAt, _observationUptime, _observationDurationMS;
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
    bool _inputFault, _awaitingStart, _startSent, _monitoring, _oneShot, _oneShotBound;
    PHOrbSnapshot _oneShotOrigin;
    PHOrbSnapshot _autoOrigin, _skillBefore;
    NSUInteger _autoTurn, _autoMoves, _autoSkills, _skillRule, _decisionTurn, _recoveryTurn;
    NSUInteger _observedTurn, _decisionObservedTurn;
    NSUInteger _ruleLastUseTurns[PHOrbMaxSkillRules], _groupLastUseTurns[PHOrbMaxSkillGroups];
    NSDictionary *_historyTimingProfile;
    uint16_t _usedRules, _unavailableRules, _confirmedRules;
    uint8_t _usedSlots, _usedGroups, _confirmedGroups, _groupNext[PHOrbMaxSkillGroups];
    NSMutableSet<NSDictionary *> *_onceRules;
    PHBattleIdentity _historyIdentity;
    bool _historyBound, _historyResolve, _historyPuzzleStarted;
    double _autoPhaseAt, _autoStartAt;
    bool _automatic, _autoBound, _sawResolve, _skillAcknowledged, _watchPlayerState;
}
- (instancetype)initWithEngine:(id<PHOrbEngine>)engine {
    if ((self = [super init])) {
        _engine = engine;
        _worker = dispatch_queue_create("jp.padcheaper.orbs.solver", DISPATCH_QUEUE_SERIAL);
        _phase = @"idle"; _message = @"自己的操作階段可轉一次；連續轉珠與技能規則限單人";
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
- (void)refreshBattleObservation:(BOOL)read active:(BOOL)active time:(NSTimeInterval)time {
    if (!read || !_frame.ready || !_frame.enabled || !ObservationWindow(_live) || active || _oneShot ||
        (_live.automation.valid && _live.automation.busy)) {
        _observationState = {};
        _observationState.observation.status = _live.multiplayer.kind != PHOrbMultiplayerNone ? PHOrbObservationMultiplayer :
            read ? PHOrbObservationTransition : PHOrbObservationUnavailable;
        _observationUptime = _observationDurationMS = 0;
        return;
    }
    if (![_engine respondsToSelector:@selector(observeBattle:)]) return;
    if (_observationState.observation.status == PHOrbObservationReady && !ObservationContextMatches(_observationState, _live)) {
        _observationState = {};
        _observationState.observation.status = PHOrbObservationChanged;
        _observationUptime = _observationDurationMS = 0;
    }
    // Invalidating evidence must not reset the attempt clock. Rapid board,
    // skill or read-window changes cannot turn diagnostics into frame polling.
    if (_observationAt > 0 && time >= _observationAt && time - _observationAt < 0.75) return;
    const double started = NSProcessInfo.processInfo.systemUptime;
    PHOrbSnapshot captured = _live;
    [_engine observeBattle:&captured];
    _observationDurationMS = std::max(0.0, NSProcessInfo.processInfo.systemUptime - started) * 1000;
    _observationState = captured;
    _observationAt = time; _observationUptime = NSProcessInfo.processInfo.systemUptime;
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
#include "PHOrbAssistSession.inc"
- (void)recordExecution:(NSString *)status state:(const PHOrbSnapshot *)state evaluation:(NSDictionary *)evaluation {
    if (!_recordingExecution || !_executionHistory.count) return;
    NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
    record[@"status"] = status; record[@"confirmedSteps"] = @(_confirmed);
    if (state) {
        record[@"observedPhase"] = @(state->phase);
        record[@"observedMultiplayer"] = MultiplayerTrace(*state);
        const bool sameTurn = PHOrbSameTurn(&_preview, state);
        const bool boardRead = HasBoardObservation(*state) && sameTurn;
        record[@"observedRead"] = !state->valid || !state->present ? @"unreadable" :
            !sameTurn ? @"different-turn" : boardRead ? @"board" : @"metadata-only";
        record[@"observedBoardPreserved"] = @(!boardRead);
        if (boardRead) {
            record[@"observedBoard"] = BoardTrace(state->board);
            record[@"observedBoardMoves"] = @(state->moves);
        }
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
        @"goalMode":_goalMode, @"leadersValid":@(_preview.leadersValid),
        @"leaderNumbers":@[@(_preview.leaderNumbers[0]), @(_preview.leaderNumbers[1])],
        @"leaderContext":LeaderTrace(_preview),
        @"scope":_automatic ? @"continuous" : _oneShot ? @"once" : @"preview-execute",
        @"multiplayer":MultiplayerTrace(_preview),
        @"startBoard":BoardTrace(_preview.board), @"path":RouteCells(_route),
        @"search":@{@"quality":_quality, @"elapsedMS":@(_route.elapsedMS), @"budgetMS":@(_solveOptions.milliseconds),
            @"thornComboPriority":@(_solveOptions.thornComboPriority),
            @"allowPoisonWithHeartFour":@(_solveOptions.allowPoisonWithHeartFour),
            @"diagonal":@(_route.diagonal),
            @"evaluated":@(_route.evaluated), @"deadline":@(_route.deadlineReached), @"stepLimit":@(_solveOptions.steps),
            @"evaluationCalls":@(_route.evaluationCalls), @"evaluationCacheHits":@(_route.evaluationCacheHits),
            @"searchPasses":@(_route.searchPasses), @"backtracks":@(_route.backtracks), @"exploredDepth":@(_route.exploredDepth),
            @"thornContacts":@(_route.thornContacts), @"thornDamage":@(_route.thornDamage)},
        @"predictedBoard":BoardTrace(predicted), @"observedBoard":BoardTrace(state->board),
        @"observedRead":@"board", @"observedBoardPreserved":@NO, @"observedBoardMoves":@(state->moves),
        @"observedPhase":@(state->phase), @"observedMultiplayer":MultiplayerTrace(*state),
        @"confirmedSteps":@0, @"releaseEvaluation":@{}, @"releaseConfirmed":@NO};
    NSMutableDictionary *withMechanic = [record mutableCopy];
    withMechanic[@"assistRecovery"] = _assistTrace; record = [withMechanic copy];
    if (_executionHistory.count >= 3) [_executionHistory removeObjectAtIndex:0];
    [_executionHistory addObject:record]; _recordingExecution = true;
}
- (BOOL)configureQuality:(NSString *)quality {
    if (!quality) return NO;
    return [self configureSettings:@{@"quality":quality, @"mode":_goalMode, @"goals":_manualGoals, @"rules":_skillRules,
        @"recovery":_recovery, @"thornComboPriority":@(_thornComboPriority),
        @"allowPoisonWithHeartFour":@(_allowPoisonWithHeartFour), @"diagonal":@(_diagonal),
        @"assistRecoveryL":@(_assistRecoveryL)}];
}
- (BOOL)configureSettings:(NSDictionary *)settings {
    NSDictionary *canonical = PHOrbCanonicalSettings(settings);
    if (!canonical || _automatic || _oneShot || [self executing] || _inputFault) return NO;
    if ([_quality isEqual:canonical[@"quality"]] && [_goalMode isEqual:canonical[@"mode"]] &&
        [_manualGoals isEqual:canonical[@"goals"]] && [_skillRules isEqual:canonical[@"rules"]] &&
        [_recovery isEqual:canonical[@"recovery"]] && _thornComboPriority == [canonical[@"thornComboPriority"] boolValue] &&
        _allowPoisonWithHeartFour == [canonical[@"allowPoisonWithHeartFour"] boolValue] &&
        _diagonal == [canonical[@"diagonal"] boolValue] && _assistRecoveryL == [canonical[@"assistRecoveryL"] boolValue]) return YES;
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
    const bool pendingStart = [self executing] && _awaitingStart && _startSent;
    const bool skillRunning = [_phase isEqual:@"skillWaiting"] || _openingInFlight;
    const bool cancelledIntent = _automatic || _readQueued || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        [_phase isEqual:@"arming"] || [_phase isEqual:@"running"] ||
        ([_phase isEqual:@"releasing"] && _releaseComplete);
    [self cancelWorker];
    [_engine armMultiplayerOnce:NO];
    _automatic = _oneShot = _oneShotBound = false;
    _skillWindowWaitAt = 0;
    _readQueued = false; _hasStable = false;
    _message = reason ?: @"已停止";
    if (skillRunning) _message = [_message stringByAppendingString:@"；已啟動的技能由遊戲完成"];
    if (_recordingExecution) {
        [self recordExecution:@"cancelled" state:_hasLive ? &_live : nullptr evaluation:nil];
        NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
        if (!record[@"reason"]) record[@"reason"] = _message;
        record[@"assistRecovery"] = _assistTrace;
        _executionHistory[_executionHistory.count - 1] = [record copy];
    }
    if (wasAutomatic) [self logAutomation:[NSString stringWithFormat:@"連續模式停止：%@", _message]];
    _releaseComplete = false;
    if (_assistAwaitingResult) {
        NSMutableDictionary *trace = [_assistTrace mutableCopy];
        if ([trace[@"status"] isEqual:@"awaiting-next-turn"]) trace[@"status"] = @"interrupted";
        trace[@"actualConfirmed"] = @NO; trace[@"reason"] = _message;
        _assistTrace = [trace copy]; _assistAwaitingResult = false;
        if (_executionHistory.count) {
            NSMutableDictionary *record = [_executionHistory.lastObject mutableCopy];
            record[@"assistRecovery"] = _assistTrace; _executionHistory[_executionHistory.count - 1] = [record copy];
        }
    }
    if (_ownedOrb || _staged || pendingStart) _phase = @"releasing";
    else { _phase = @"stopped"; _recordingExecution = false; }
    if (cancelledIntent) ++_cancelled;
}
- (void)queueRead {
    if (_automatic || _oneShot || [self executing] || _inputFault || _openingInFlight) return;
    [self cancelWorker];
    [_engine armMultiplayerOnce:NO];
    _monitoring = _readQueued = true; _hasStable = false; _requestedAt = _now;
    [self clearRoute]; _goalPlan = @{}; _phase = @"reading"; _message = @"等待盤面穩定…";
}
- (void)queueOnce {
    if (_automatic || _oneShot || [self executing] || _inputFault || _openingInFlight || !_frame.ready || !_frame.enabled) return;
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
- (void)queueAutomatic {
    if (_automatic || _oneShot || [self executing] || _inputFault || !_frame.ready || !_frame.enabled) return;
    _monitoring = true;
    PHOrbSnapshot current{}; NSString *reason = nil;
    const BOOL read = [_engine read:&current frame:_frame reason:&reason];
    _hasLive = read; _live = current;
    _liveReadReason = [reason copy] ?: @""; _lastReadUptime = NSProcessInfo.processInfo.systemUptime;
    [_engine armMultiplayerOnce:NO];
    if (current.multiplayer.kind != PHOrbMultiplayerNone || current.multiplayerDisabled || (current.present &&
        !PHBattleIsSinglePlayer(current.identity.entryMode, current.identity.multiplayer))) {
        [self queueStop:@"多人只支援轉一次；連續轉珠、開場與技能規則限單人"]; return;
    }
    [self cancelWorker];
    _automatic = _monitoring = true; _autoBound = _sawResolve = _hasStable = _readQueued = false;
    _autoTurn = 1; _autoMoves = _autoSkills = 0; _unavailableRules = 0;
    _autoStartAt = _autoPhaseAt = _now; _autoOrigin = {}; [self clearRoute]; _goalPlan = @{};
    _assistTrace = @{}; _assistAwaitingResult = false; _assistCheckedAt = 0;
    _skillWindowWaitAt = 0;
    _boardTransitionFrames = 0; _lastReadIssue = @{};
    _activeProfile = @{};
    [_automationLog removeAllObjects];
    [_ruleDecisions removeAllObjects]; [_unavailableReasons removeAllObjects];
    [_skillAttemptContexts removeAllObjects];
    [_decisionContexts removeAllObjects];
    _decisionRules = @[]; _decisionState = {}; _decisionTurn = _decisionObservedTurn = _recoveryTurn = 0;
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
- (void)resetRuleTiming {
    _observedTurn = 0;
    _historyPuzzleStarted = false;
    std::fill(std::begin(_ruleLastUseTurns), std::end(_ruleLastUseTurns), 0);
    std::fill(std::begin(_groupLastUseTurns), std::end(_groupLastUseTurns), 0);
}
- (PHOrbRuleMatch)triggerRule:(NSUInteger)index state:(const PHOrbSnapshot *)state reason:(NSString **)reason {
    NSDictionary *rule = _skillRules[index];
    const NSUInteger group = [rule[@"group"] unsignedIntegerValue];
    NSUInteger last = group ? _groupLastUseTurns[group - 1] : _ruleLastUseTurns[index];
    // A same-turn attempt without a completion receipt must still expose a
    // required rescue. Per-turn availability stops it; the new interval must
    // not hide it merely because its uncertain start reserved the clock.
    const bool attempted = (_usedRules & (1u << index)) || (group && (_usedGroups & (1u << (group - 1))));
    if (attempted && last == _observedTurn && [rule[@"required"] boolValue]) last = 0;
    return PHOrbTriggerSkillRuleWithTiming(rule, state, _autoTurn, _observedTurn, last, reason);
}
- (BOOL)hasPendingSkillWindow:(const PHOrbSnapshot *)state {
    if (_openingInFlight || [_activeProfile[@"opening"] count] > _openingStep) return YES;
    if (_openingPuzzlePending && _activeProfile[@"formation"]) return NO;
    for (NSUInteger i = 0; i < _skillRules.count; ++i) {
        NSDictionary *rule = _skillRules[i];
        NSUInteger slot = [rule[@"slot"] unsignedIntegerValue] - 1, group = [rule[@"group"] unsignedIntegerValue];
        if (![rule[@"enabled"] boolValue] || ((_usedRules | _confirmedRules | _unavailableRules) & (1u << i)) ||
            (_usedSlots & (1u << slot)) || (group && ((_usedGroups | _confirmedGroups) & (1u << (group - 1)))) ||
            ([rule[@"once"] boolValue] && [_onceRules containsObject:rule])) continue;
        if ([rule[@"monster"] unsignedIntValue] && [rule[@"monster"] unsignedIntValue] != state->automation.members[slot].number &&
            !state->automation.members[slot].temporaryTurns) continue;
        return YES;
    }
    return NO;
}
- (void)deferSkillWindow:(NSString *)reason {
    [self waitForAutomationBoard];
    if (!_skillWindowWaitAt) _skillWindowWaitAt = _now;
    if (_now - _skillWindowWaitAt >= SkillWindowTimeout) {
        [self queueStop:[NSString stringWithFormat:@"%@；等待 3 秒後仍未就緒，已停止，未施放或轉珠", reason]];
    } else {
        _message = [NSString stringWithFormat:@"%@；此次未施放，等待重查", reason];
    }
}
- (BOOL)waitForSkillWindow {
    if (![self hasPendingSkillWindow:&_live]) { _skillWindowWaitAt = 0; return NO; }
    const auto &state = _live.automation;
    // Known lasting restrictions retain optional-rule behavior. A temporary
    // closed window is neither a native refusal nor permission to start a drag.
    const bool persistent = PersistentSkillWindow(state);
    if (!state.window && persistent && [_activeProfile[@"opening"] count] > _openingStep) {
        [self queueStop:[NSString stringWithFormat:@"開場第 %lu 步未就緒：%@；未施放或跳到下一步",
            (unsigned long)_openingStep + 1, PHOrbSkillWindowDescription(&state)]];
        return YES;
    }
    if (persistent) { _skillWindowWaitAt = 0; return NO; }
    if (state.window) return NO; // Require a stable decision/cast before clearing the deferral timer.
    if (!_skillWindowWaitAt) _skillWindowWaitAt = _now;
    if (_now - _skillWindowWaitAt >= SkillWindowTimeout) {
        [self queueStop:[NSString stringWithFormat:@"%@；等待 3 秒後仍未就緒，已停止，未施放或轉珠",
            PHOrbSkillWindowDescription(&state)]];
    } else {
        _hasStable = false;
        _message = [NSString stringWithFormat:@"%@；等待重新確認，未施放或轉珠", PHOrbSkillWindowDescription(&state)];
    }
    return YES;
}
// Keep once-per-battle and per-orb-turn history across a manual stop/start.
// Transient read failures, phase 22 and skill-cleared floors do not reset it.
- (void)observeRuleHistory:(const PHOrbSnapshot *)state read:(BOOL)read {
    if (!read) { _historyPuzzleStarted = false; return; }
    if (!state->present) {
        _openingProfile = nil; _openingStep = 0; _temporaryMask = 0;
        _openingInFlight = _openingAcknowledged = _openingStarted = _openingPuzzlePending = false;
        [_openingLog removeAllObjects];
        _historyBound = _historyResolve = false; [_onceRules removeAllObjects];
        _historyRules = nil; _historyTimingProfile = nil;
        [self resetRuleTiming];
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        [_unavailableReasons removeAllObjects];
        std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
        return;
    }
    if (!state->valid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer)) {
        _historyPuzzleStarted = false; return;
    }
    if (!_historyBound || !PHBattleSameIdentity(&_historyIdentity, &state->identity)) {
        _openingProfile = nil; _openingStep = 0; _temporaryMask = 0;
        _openingInFlight = _openingAcknowledged = _openingStarted = _openingPuzzlePending = false;
        [_openingLog removeAllObjects];
        _historyIdentity = state->identity; _historyBound = true; _historyResolve = false;
        _historyRules = nil; _historyTimingProfile = nil;
        [self resetRuleTiming]; _observedTurn = 1;
        [_onceRules removeAllObjects];
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        [_unavailableReasons removeAllObjects];
        std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
    }
    const bool timed = AcceptedSkillTiming(_historyRules);
    if (state->phase == 22 && !_historyResolve) _historyPuzzleStarted = false;
    if (timed && state->phase == 2 && state->dragged) _historyPuzzleStarted = true;
    // A zero-swap turn needs an observed drag or an explicitly pending opening
    // puzzle. A phase/floor transition alone must not advance skill intervals.
    if (ResolvingOrbs(state->phase) && (state->moves > 0 || _openingPuzzlePending || (timed && _historyPuzzleStarted)))
        _historyResolve = true;
    if (_historyResolve && state->phase == 2 && state->moves == 0 && state->settled && !state->dragged &&
        !state->boardTransition && (!(timed || _openingPuzzlePending) || (state->automation.valid && !state->automation.busy))) {
        // Count this observed resolve/idle pair once. Phase 22, a floor clear,
        // elapsed time or a stop/start never advances the timing clock.
        if (_observedTurn && _observedTurn < NSUIntegerMax) ++_observedTurn;
        _usedRules = _unavailableRules = _confirmedRules = _usedSlots = _usedGroups = _confirmedGroups = 0;
        _openingPuzzlePending = false;
        [_unavailableReasons removeAllObjects]; _historyResolve = false;
    }
    if (!_historyResolve && state->phase == 2 && !state->moves && state->settled && !state->dragged && !state->boardTransition)
        _historyPuzzleStarted = false;
}
// Called on every observed native frame while armed, even during animation.
// A skill's phase 22 is never evidence that an orb turn has completed.
- (BOOL)observeAutomation:(PHOrbSnapshot *)state read:(BOOL)read reason:(NSString *)reason {
    [self observeRuleHistory:state read:read];
    if (!_frame.ready || !_frame.enabled || _now < _autoPhaseAt) {
        [self queueStop:@"總開關、前景或登入狀態已改變，已停止連續模式"]; return NO;
    }
    if (!read || !state->present || !state->valid || !state->automation.valid || state->multiplayerDisabled ||
        !PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) || state->identity.sourceMode != 0 || state->wave >= 999) {
        _lastReadIssue = @{@"reason":reason ?: @"戰鬥資料無法確認", @"phase":@(state->phase),
            @"valid":@(state->valid), @"automationValid":@(state->automation.valid),
            @"width":@(state->board.width), @"height":@(state->board.height),
            @"resizeProgress":@(state->resizeProgress), @"resizeTarget":@(state->resizeTarget),
            @"openingStep":@(_openingStep + (_openingInFlight ? 1 : 0)),
            @"duringAutomatic":@(_automatic), @"sessionPhase":_phase,
            @"capturedSearchGeneration":_searchTrace[@"generation"] ?: @0};
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
        const bool recoverBinding = resume ? [retained[@"settings"][@"assistRecoveryL"] boolValue] :
            [resolved[@"ok"] boolValue] ? ([resolved[@"profile"] count] ?
                [resolved[@"profile"][@"settings"][@"assistRecoveryL"] boolValue] : _assistRecoveryL) : [self bindingNeedsAssistRecovery];
        const bool assistWindow = state->phase == 2 && state->settled && !state->boardTransition &&
            !state->automation.busy && !state->dragged;
        if (recoverBinding && !assistWindow && !(resume && _openingInFlight)) {
            if (!PHInputIsQuiet(&state->input) || state->dragged) {
                [self queueStop:@"偵測到手動操作，已停止連續模式"]; return NO;
            }
            if (_now - _autoPhaseAt > 12) {
                [self queueStop:@"尚未取得穩定的單人隊伍及武裝狀態，已停止"]; return NO;
            }
            _message = @"等待盤面穩定後核對隊伍及武裝狀態…"; return YES;
        }
        if (recoverBinding && assistWindow) {
            if (![self captureAssistRecovery:state]) return NO;
            if (!resume) resolved = PHOrbResolveProfile(_profileLibrary, state);
        }
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
                _openingProfile = _activeProfile; _openingStep = 0;
                _openingInFlight = _openingAcknowledged = _openingStarted = _openingPuzzlePending = false;
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
        NSDictionary *timingProfile = SkillTimingProfile(_activeProfile);
        const bool rulesChanged = ![_historyRules isEqual:_skillRules];
        const bool timingChanged = rulesChanged || ![_historyTimingProfile isEqual:timingProfile];
        if (rulesChanged || (timingChanged && AcceptedSkillTiming(_skillRules))) {
            _usedRules = _unavailableRules = _confirmedRules = _confirmedGroups = 0;
            [_unavailableReasons removeAllObjects];
            std::fill(std::begin(_groupNext), std::end(_groupNext), 0);
        }
        if (timingChanged) {
            [self resetRuleTiming]; _observedTurn = 1; _historyResolve = false;
            _historyRules = _skillRules; _historyTimingProfile = timingProfile;
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
    if (state->boardTransition) {
        ++_boardTransitionFrames; _hasStable = false;
        _lastReadIssue = @{@"reason":@"等待原生改盤動畫完成", @"phase":@(state->phase),
            @"valid":@YES, @"automationValid":@YES, @"width":@(state->board.width), @"height":@(state->board.height),
            @"resizeProgress":@(state->resizeProgress), @"resizeTarget":@(state->resizeTarget),
            @"openingStep":@(_openingStep + (_openingInFlight ? 1 : 0)),
            @"duringAutomatic":@(_automatic), @"sessionPhase":_phase,
            @"capturedSearchGeneration":_searchTrace[@"generation"] ?: @0};
        _message = _openingInFlight ? [NSString stringWithFormat:@"開場第 %lu 步：等待原生改盤動畫完成…", (unsigned long)_openingStep + 1] :
            @"等待原生改盤動畫完成…";
    }
    const bool beforeTouch = [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        ([_phase isEqual:@"arming"] && !_startSent && !state->dragged);
    // Only an outstanding preview is polled, at a bounded rate. Actual input
    // still gets an unthrottled read after native coordinate conversion.
    if (_preview.assistRecovery.valid && ([_phase isEqual:@"solving"] || [_phase isEqual:@"preview"]) &&
        (_now < _assistCheckedAt || _now - _assistCheckedAt >= 0.25) && ![self validateAssistRecovery:state]) return NO;
    if (!state->boardTransition && state->phase == 2 &&
        !state->automation.window && beforeTouch && [self hasPendingSkillWindow:state] &&
        !PersistentSkillWindow(state->automation)) {
        [self cancelWorker]; [self clearRoute];
        [self waitForAutomationBoard];
        _message = @"轉珠前技能窗口已關閉，重新確認後再判斷技能";
    }
    return YES;
}
- (BOOL)finishOpeningIfConfirmed:(const PHOrbSnapshot *)state {
    if (!_openingInFlight || !_openingStarted || !_openingAcknowledged || !state->valid || !state->automation.valid ||
        state->boardTransition || state->phase != 2 || !state->settled ||
        state->automation.busy || state->dragged ||
        (state->wave == _openingBefore.wave && state->automation.skillUses != _openingBefore.automation.skillUses + 1) ||
        !PHOrbProfileMatchesOpeningTeam(_openingProfile, state, _openingStep + 1)) return NO;
    NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"confirmed";
    record[@"usesAfter"] = @(state->automation.skillUses);
    _openingLog[_openingLog.count - 1] = [record copy];
    ++_openingStep; _openingInFlight = false;
    if (_openingStep == [_openingProfile[@"opening"] count] &&
        [_openingProfile[@"opening"][_openingStep - 1][@"puzzleAfter"] boolValue]) {
        _openingPuzzlePending = true;
        [self logAutomation:@"開場完成：先轉此盤，確認下一回合後才接技能循環"];
    }
    [self logAutomation:[NSString stringWithFormat:@"開場 %lu／%lu 已確認完成", (unsigned long)_openingStep,
        (unsigned long)[_openingProfile[@"opening"] count]]];
    return YES;
}
- (void)advanceAutomation {
    if (!_automatic || !_autoBound || [self executing] || [_phase isEqual:@"solving"] || [_phase isEqual:@"preview"] ||
        [_phase isEqual:@"skillArming"] || [_phase isEqual:@"openingArming"] || _now - _autoStartAt < 0.35) return;
    if ([_phase isEqual:@"autoWaiting"] && _live.phase == 2 && !_live.boardTransition &&
        _live.settled && [self waitForSkillWindow]) return;
    if ([_phase isEqual:@"turnWaiting"]) {
        _sawResolve |= ResolvingOrbs(_live.phase);
        if (_now - _autoPhaseAt > TurnTimeout) { [self queueStop:@"等候下一回合逾時，請檢查遊戲畫面"]; return; }
        if (!_sawResolve || _live.boardTransition || _live.phase != 2 || _live.moves != 0 || _live.automation.busy || !_live.settled) {
            _hasStable = false; return;
        }
    } else if ([_phase isEqual:@"openingWaiting"]) {
        if (_now - _autoPhaseAt > SkillTimeout) { [self queueStop:@"開場技能等候逾時；保留進度，不會重複施放"]; return; }
        if (_live.boardTransition || _live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
        if (!_openingAcknowledged) {
            _openingStarted = false;
            NSMutableDictionary *record = [_openingLog.lastObject mutableCopy]; record[@"status"] = @"unconfirmed";
            _openingLog[_openingLog.count - 1] = [record copy];
            [self queueStop:@"開場技能使用次數未確認；已停止，不能重播此步"]; return;
        }
    } else if ([_phase isEqual:@"skillWaiting"]) {
        if (_now - _autoPhaseAt > SkillTimeout) { [self queueStop:@"技能等候逾時或需要手動選擇，已停止"]; return; }
        if (_live.boardTransition || _live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
        if (!_skillAcknowledged) {
            [self queueStop:@"技能使用次數未確認，已停止以免重複施放"]; return;
        }
    } else if ([_phase isEqual:@"autoWaiting"]) {
        if (_now - _autoPhaseAt > 12) {
            [self queueStop:(PHOrbBoardObstacles(&_live) & PHOrbObstacleSpinner) ?
                @"轉轉盤面持續變色，未取得可確認的技能快照；請先手動處理後再開始" : @"尚未進入穩定的單人操作階段，已停止"]; return;
        }
        if (_live.boardTransition || _live.phase != 2 || _live.automation.busy || !_live.settled) { _hasStable = false; return; }
    } else return;
    if (!_hasStable || !PHOrbSameAutomation(&_live, &_stable)) {
        _stable = _live; _stableAt = _now; _hasStable = true; return;
    }
    if (_now - _stableAt < AutomationSettle) return;
    // Only stable, active decisions request mechanism evidence. Skills run
    // first; the final goal capture below always reads again after them.
    if (_assistRecoveryL && ![self captureAssistRecovery:&_live]) return;
    if ([_phase isEqual:@"openingWaiting"]) {
        if (![self finishOpeningIfConfirmed:&_live]) {
            [self queueStop:@"開場技能完成後的次數、角色或武裝不符合預期；保留進度，未執行下一步"]; return;
        }
        ++_autoSkills; [self waitForAutomationBoard]; return;
    }
    if ([_phase isEqual:@"turnWaiting"]) {
        if (![self confirmAssistRecoveryResult:&_live]) return;
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
    if (_openingPuzzlePending && _activeProfile[@"formation"]) {
        // The last opening skill may leave a CD1 supplier uncharged. Keep
        // this choice across stop/start; only a confirmed orb turn clears it.
        if (!PHOrbCanPreview(&_live)) {
            [self queueStop:@"開場完成後盤面或操作條件尚未確認，未轉珠或接續技能循環"]; return;
        }
        [self beginSolve]; return;
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
    [_ruleDecisions removeAllObjects]; _decisionTurn = _autoTurn; _decisionObservedTurn = _observedTurn;
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
        else if (!_live.automation.window) unavailable = PHOrbSkillWindowDescription(&_live.automation);
        // Optional rules retain the previous short circuit: unavailable
        // actors do not make unrelated, unknown condition data mandatory.
        if (unavailable && ![rule[@"required"] boolValue]) {
            _ruleDecisions[i] = [NSString stringWithFormat:@"略過 · %@", unavailable]; continue;
        }
        NSString *trigger = nil;
        PHOrbRuleMatch match = [self triggerRule:i state:&_live reason:&trigger];
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
    if (!PHBattleIsSinglePlayer(_preview.identity.entryMode, _preview.identity.multiplayer) &&
        (!_oneShot || !_oneShotBound || !PHOrbSameTurn(&_oneShotOrigin, &_preview))) {
        [self queueStop:@"多人預覽不能直接執行；請在自己的操作窗口按轉一次重新讀盤"]; return;
    }
    if (_now - _previewAt > PreviewLifetime || !_hasLive || !PHOrbCanPreview(&_live) || !SamePosition(_live, _preview)) {
        [self queueStop:@"盤面或回合已改變，請重新讀盤"];
        return;
    }
    if (!HasRouteTime(_live, _route, ArmDelay)) {
        _message = @"操作時間不足，請重新讀盤取得較短路徑";
        if (_oneShot) [self queueStop:_message];
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
    PHOrbSolveOptions options = _solveOptions; options.thorns = state->thorns;
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
    _preview.assistRecovery = {};
    PHOrbSolveOptions options = OptionsForQuality(_quality);
    options.thorns = _preview.thorns;
    options.thornComboPriority = _thornComboPriority;
    options.allowPoisonWithHeartFour = _allowPoisonWithHeartFour;
    options.diagonal = _diagonal;
    _skillWindowWaitAt = 0;
    _goalPlan = [_goalMode isEqual:@"leaders"] ? PHOrbPlanForLeaders(&_preview) :
        @{@"available":@YES, @"goals":[_goalMode isEqual:@"manual"] ? _manualGoals : @[],
          @"message":PHOrbGoalsDescription([_goalMode isEqual:@"manual"] ? _manualGoals : @[]), @"warning":@"", @"leaders":@[]};
    _goalPlan = PHOrbPlanWithRecovery(_goalPlan, _recovery, &_preview, _automatic);
    if (_automatic && _assistRecoveryL) {
        if (![self captureAssistRecovery:&_preview]) return;
        _goalPlan = PHOrbPlanWithAssistRecovery(_goalPlan, YES, &_preview, YES);
        NSDictionary *previous = [_assistTrace[@"status"] isEqual:@"observed-clear"] ?
            [_assistTrace dictionaryWithValuesForKeys:@[@"status", @"beforeTurns", @"observedTurns", @"turn",
                @"actualConfirmed", @"lOnlyAmountConfirmed"]] : nil;
        NSMutableDictionary *trace = [[self assistRecoveryTrace:&_preview.assistRecovery status:
            [_goalPlan[@"available"] boolValue] ? @"planned" : @"blocked"] mutableCopy];
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
    _recoveryTurn = _automatic ? _autoTurn : 0;
    if (![_goalPlan[@"available"] boolValue] || !PHOrbReadGoals(_goalPlan[@"goals"], &options.goals)) {
        _phase = @"blocked"; _message = _goalPlan[@"message"] ?: @"求路目標無法解析";
        if (_automatic || _oneShot) [self queueStop:_message];
        return;
    }
    const double available = TimeAvailable(_live);
    const bool multiplayer = !PHBattleIsSinglePlayer(_live.identity.entryMode, _live.identity.multiplayer);
    double remaining = available;
    if (multiplayer) {
        if (!PHOrbCanPreview(&_live) || available <= TimeMargin + StepDuration(_live)) {
            [self queueStop:@"多人本機操作窗口已關閉或時間不足，未開始求路"]; return;
        }
        if (_live.multiplayer.kind != PHOrbMultiplayerThreeSolo) {
            const double searchRoom = _live.multiplayer.remainingSeconds - ArmDelay - TimeMargin - StepDuration(_live);
            if (searchRoom < 0.05) { [self queueStop:@"多人本機操作時間不足，未開始求路"]; return; }
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
    _solveOptions = options;
    _traceOrigin = _preview;
    _searchTrace = @{@"schema":@1, @"generation":@(_generation), @"status":@"searching",
        @"goalMode":_goalMode, @"leadersValid":@(_preview.leadersValid),
        @"leaderNumbers":@[@(_preview.leaderNumbers[0]), @(_preview.leaderNumbers[1])],
        @"leaderContext":LeaderTrace(_preview),
        @"scope":_automatic ? @"continuous" : _oneShot ? @"once" : @"preview",
        @"multiplayer":MultiplayerTrace(_preview),
        @"turn":@(_automatic ? _autoTurn : 0), @"wave":@(_preview.wave),
        @"dungeon":@(_preview.identity.dungeon), @"floor":@(_preview.identity.floor),
        @"board":BoardTrace(_preview.board), @"goals":PHOrbGoalsJSON(&options.goals),
        @"quality":_quality, @"stepLimit":@(options.steps), @"budgetMS":@(options.milliseconds),
        @"thornComboPriority":@(options.thornComboPriority),
        @"allowPoisonWithHeartFour":@(options.allowPoisonWithHeartFour), @"diagonal":@(options.diagonal),
        @"assistRecovery":_assistTrace, @"assistRecoveryL":_goalPlan[@"assistRecoveryActive"] ?: @NO};
    _cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancel = _cancel;
    const PHOrbBoard board = _preview.board;
    NSArray<NSDictionary *> *const assistCandidates = _goalPlan[@"assistRecoveryCandidates"] ?: @[];
    const NSUInteger generation = _generation;
    __weak PHOrbSession *weakSelf = self;
    _phase = @"solving"; _message = @"正在求路…";
    dispatch_async(_worker, ^{
      @autoreleasepool {
        PHOrbRoute route{};
        PHOrbSolveOptions selectedOptions = options;
        PHOrbSolveStatus result = PHOrbSolveStatus::NoRoute;
        NSDictionary *selectedAssist = @{};
        NSMutableArray *attempts = [NSMutableArray array];
        const auto start = std::chrono::steady_clock::now();
        const NSUInteger choices = std::max(NSUInteger(1), assistCandidates.count);
        for (NSUInteger choice = 0; choice < choices; ++choice) {
            const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            if (cancel->load(std::memory_order_relaxed) || (assistCandidates.count && spent >= options.milliseconds)) break;
            PHOrbSolveOptions trial = options;
            if (assistCandidates.count && !PHOrbReadGoals(assistCandidates[choice][@"goals"], &trial.goals)) continue;
            if (assistCandidates.count) trial.milliseconds = unsigned(options.milliseconds - spent) / unsigned(choices - choice);
            if (!trial.milliseconds) break;
            PHOrbRoute candidate{};
            const auto status = PHOrbSolve(board, trial, *cancel, candidate);
            [attempts addObject:@{@"goals":PHOrbGoalsJSON(&trial.goals), @"status":SolveStatusName(status),
                @"budgetMS":@(trial.milliseconds), @"elapsedMS":@(candidate.elapsedMS)}];
            route = candidate; result = status; selectedOptions = trial;
            if (assistCandidates.count) selectedAssist = assistCandidates[choice];
            if (status == PHOrbSolveStatus::Solved && candidate.goalsMet && candidate.hazardsSafe) break;
        }
        selectedOptions.milliseconds = options.milliseconds; // Public budget is the shared job, not its last slice.
        const PHOrbSolveOptions checkedOptions = selectedOptions;
        const auto searchElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        dispatch_async(dispatch_get_main_queue(), ^{
            PHOrbSession *session = weakSelf;
            if (!session || generation != session->_generation || ![session->_phase isEqual:@"solving"] ||
                cancel->load(std::memory_order_relaxed)) return;
            NSMutableDictionary *trace = [session->_searchTrace mutableCopy];
            trace[@"candidateAttempts"] = [attempts copy];
            trace[@"goals"] = PHOrbGoalsJSON(&checkedOptions.goals);
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
            trace[@"status"] = SolveStatusName(result); session->_searchTrace = [trace copy];
            if ((result != PHOrbSolveStatus::Solved && result != PHOrbSolveStatus::GoalsUnmet && result != PHOrbSolveStatus::Unsafe) || route.count < 2) {
                session->_phase = @"stopped";
                session->_message = assistCandidates.count ? @"本次限時搜尋未找到兼顧原目標及武裝回復 L 的路徑；未證明盤面無解，已停止。" :
                    result == PHOrbSolveStatus::InsufficientOrbs ?
                    @"目前珠數不足以達成目標；可先使用技能改盤，或調整目標後重新讀盤。" :
                    result == PHOrbSolveStatus::IncompatibleGoals ?
                    @"目前珠數或空間無法同時排出所需的獨立形狀；可改盤或調整目標。" :
                    @"本次未取得有效路徑，請重新讀盤";
                if (session->_automatic || session->_oneShot) [session queueStop:session->_message];
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
                [session queueStop:@"路徑重播校驗失敗，已停止"]; session->_phase = @"fault";
                return;
            }
            trace[@"predictedBoard"] = BoardTrace(replay);
            trace[@"evaluation"] = EvaluationTrace(evaluation, checkedOptions.goals); session->_searchTrace = [trace copy];
            session->_route = route; session->_phase = @"preview"; session->_previewAt = session->_now;
            NSString *summary = [NSString stringWithFormat:@"預計 %u Combo（不含天降／加算）· %u 步 · 約 %.1f 秒",
                route.combos, route.count - 1, (route.count - 1) * StepDuration(session->_preview)];
            if (board.spinnerMask) summary = [summary stringByAppendingFormat:
                @"\n路徑避開轉轉；Combo 與目標按全部 %u 種可能色序的最低結果顯示。", PHOrbSpinnerVariants(&board)];
            if (board.tapeMask) summary = [summary stringByAppendingString:@"\n已繞開封條。"];
            if (route.thornContacts) summary = [summary stringByAppendingFormat:
                @"\n荊棘碰觸 %u 次 · 傷害上限 %u · 首批心四連 %u 組；未把回血與抗性計入可承受傷害。",
                route.thornContacts, route.thornDamage, route.heartFours];
            if (options.allowPoisonWithHeartFour && route.poisonErased && route.heartFours)
                summary = [summary stringByAppendingFormat:@"\n首消心四連已達成；按隊伍設定允許消毒／猛毒，合計最多 %u 顆。", route.poisonErased];
            summary = [summary stringByAppendingFormat:@"\n求路耗時 %u ms／預算 %u ms · %@ · 步數上限 %u",
                unsigned(searchElapsed), options.milliseconds, (route.deadlineReached || searchElapsed >= options.milliseconds) ?
                    @"已到候選或總時間上限" : @"未到時間上限", checkedOptions.steps];
            if (!route.goalsMet) {
                NSArray<NSDictionary *> *goals = PHOrbGoalsJSON(&checkedOptions.goals);
                NSMutableArray<NSString *> *missing = [NSMutableArray array];
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
            if ((session->_automatic || session->_oneShot) && (!route.goalsMet || !route.hazardsSafe)) [session queueStop:session->_message];
        });
      }
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
        _hasLive = read; _live = current;
        _liveReadReason = [reason copy] ?: @""; _lastReadUptime = NSProcessInfo.processInfo.systemUptime;
        if (![self observeAutomation:&current read:read reason:reason]) return;
        _live.assistRecovery = current.assistRecovery;
        if ([_phase isEqual:@"openingArming"]) {
            if (_assistRecoveryL && ![self captureAssistRecovery:&current]) return;
            NSDictionary *step = _activeProfile[@"opening"][_openingStep];
            const NSUInteger slot = [step[@"slot"] unsignedIntegerValue] - 1;
            if (current.phase == 2 && !current.automation.window &&
                PHOrbSameAutomationExceptWindow(&_openingBefore, &current) && !current.dragged && PHInputIsQuiet(&current.input)) {
                [self deferSkillWindow:PHOrbSkillWindowDescription(&current.automation)];
                return;
            }
            if (!PHOrbSameAutomation(&_openingBefore, &current) ||
                !PHOrbProfileMatchesOpeningTeam(_activeProfile, &current, _openingStep)) {
                [self queueStop:@"開場施放前盤面、隊員或武裝已改變，未施放"]; return;
            }
            PHOrbSkillResult result = [_engine useSkill:slot request:PHOrbRequestForRule(step)
                expected:&_openingBefore frame:frame reason:&reason];
            if (result == PHOrbSkillWindowClosed) {
                [self deferSkillWindow:reason ?: @"技能窗口尚未開放；開場此步未施放"];
                return;
            }
            _skillWindowWaitAt = 0;
            if (result == PHOrbSkillUnavailable) {
                [self queueStop:[NSString stringWithFormat:@"開場第 %lu 步未就緒：%@；未跳到下一步",
                    (unsigned long)_openingStep + 1, reason ?: @"遊戲判定技能不可用"]]; return;
            }
            _openingInFlight = true; _openingAcknowledged = false; _openingStarted = result == PHOrbSkillStarted;
            [_openingLog addObject:@{@"step":@(_openingStep + 1), @"slot":step[@"slot"], @"source":step[@"source"],
                @"skill":step[@"skill"], @"usesBefore":@(_openingBefore.automation.skillUses),
                @"consumeAssist":@([step[@"consumeAssist"] boolValue]), @"puzzleAfter":@([step[@"puzzleAfter"] boolValue]),
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
            if (current.phase == 2 && !current.automation.window &&
                PHOrbSameAutomationExceptWindow(&_skillBefore, &current) && !current.dragged && PHInputIsQuiet(&current.input)) {
                _decisionState = current;
                _decisionContexts[_skillRule] = SkillDecisionContext(current, slot);
                _ruleDecisions[_skillRule] = @"技能窗口尚未開放，此次未施放，等待重查";
                [self deferSkillWindow:PHOrbSkillWindowDescription(&current.automation)];
                return;
            }
            // Recheck color-count conditions on the frame that starts a skill.
            // Spinner-dependent thresholds are unknown, never guessed from a
            // previously sampled color. The native engine rechecks state again.
            if (!PHOrbSameAutomation(&_skillBefore, &current)) {
                [self queueStop:@"技能施放前盤面、隊員或戰鬥狀態已改變，已停止"]; return;
            }
            PHOrbRuleMatch trigger = [self triggerRule:_skillRule state:&current reason:&reason];
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
            if (result == PHOrbSkillWindowClosed) {
                NSMutableDictionary *attempt = [_skillAttemptContexts[@(_skillRule)] mutableCopy];
                attempt[@"nativeAttempt"] = @NO;
                attempt[@"skillWindow"] = @NO;
                attempt[@"skillWindowBlocksKnown"] = @NO;
                attempt[@"skillWindowBlocks"] = @0;
                attempt[@"skillWindowReason"] = reason ?: @"技能窗口尚未開放";
                _skillAttemptContexts[@(_skillRule)] = [attempt copy];
                _decisionContexts[_skillRule] = [attempt copy];
                _ruleDecisions[_skillRule] = @"技能窗口尚未開放，此次未呼叫原生施放，等待重查";
                [self deferSkillWindow:reason ?: @"技能窗口尚未開放"];
                return;
            }
            _skillWindowWaitAt = 0;
            // Rejection can include an uncertain native start. A once rule
            // must not be retried by stopping and immediately starting again.
            if (result != PHOrbSkillUnavailable && [rule[@"once"] boolValue]) [_onceRules addObject:rule];
            NSUInteger group = [rule[@"group"] unsignedIntegerValue];
            if (result != PHOrbSkillUnavailable) {
                _usedRules |= 1u << _skillRule; _usedSlots |= 1u << slot;
                _ruleLastUseTurns[_skillRule] = _observedTurn;
                if (group) _groupLastUseTurns[group - 1] = _observedTurn;
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
                    NSUInteger next = (_skillRule + 1) % _skillRules.count;
                    BOOL timed = NO;
                    for (NSDictionary *candidate in _skillRules) if ([candidate[@"enabled"] boolValue] &&
                        [candidate[@"group"] unsignedIntegerValue] == group &&
                        [candidate[@"intervalMode"] isEqual:@"accepted"]) timed = YES;
                    if (timed) for (NSUInteger step = 1; step < _skillRules.count; ++step) {
                        const NSUInteger index = (_skillRule + step) % _skillRules.count;
                        NSDictionary *candidate = _skillRules[index];
                        if ([candidate[@"group"] unsignedIntegerValue] == group &&
                            [candidate[@"slot"] unsignedIntegerValue] != slot + 1) { next = index; break; }
                    }
                    _groupNext[group - 1] = (uint8_t)next;
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
    BOOL read = (_automatic || _openingInFlight || _openingPuzzlePending ||
        (_historyBound && AcceptedSkillTiming(_historyRules))) ?
        [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    if (!read || !current.valid || current.multiplayerDisabled ||
        !PHOrbSameTurn(&_expected, &current) || current.phase != 2) {
        // The private manager has already been removed. Never inject stale
        // cleanup into a new scene/multiplayer; native input now owns release.
        if (![_phase isEqual:@"releasing"] || _releaseComplete)
            [self queueStop:@"回合、場景或可操作狀態已改變，已停止"];
        _ownedOrb = 0; _phase = @"stopped"; _recordingExecution = false;
        return;
    }
    const bool multiplayer = !PHBattleIsSinglePlayer(current.identity.entryMode, current.identity.multiplayer);
    if (multiplayer && !PHOrbMultiplayerWindow(&current)) {
        [self queueStop:reason ?: @"多人已交接、同步、結算或計時結束，已停止輸入"];
        _ownedOrb = 0; _phase = @"stopped"; _recordingExecution = false;
        return;
    }
    _live = current; _hasLive = true;
    if (_automatic) {
        const BOOL observed = [self observeAutomation:&current read:read reason:reason];
        // A pre-touch closure can return to rule waiting without starting a
        // gesture. A busy window must not turn that deferral into a stop.
        if (observed && ![self executing]) return;
        if (observed && current.automation.busy) [self queueStop:@"轉珠前技能或動畫狀態已改變，已停止"];
        // Fall through to the existing owned-orb release path when needed.
    }
    if (_automatic && ([_phase isEqual:@"arming"] || [_phase isEqual:@"running"]) &&
        ![self validateAssistRecovery:&current]) {
        // A failure revokes holds, while retaining the existing owned release.
        if (![_phase isEqual:@"releasing"]) return;
    }
    if ([_phase isEqual:@"arming"]) {
        if (!frame.enabled || !PHOrbCanPreview(&current) || !SamePosition(current, _preview)) {
            [self queueStop:@"執行前盤面或開關已改變，請重新讀盤"]; return;
        }
        if (![self hasThornBudget:&current]) {
            [self queueStop:@"執行前荊棘傷害預算已不符合，已停止"]; return;
        }
        if (time - _requestedAt < ArmDelay) return;
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
            multiplayer && TimeAvailable(current) <= TimeMargin ? @"多人剩餘時間不足，已停止並放開" :
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
                const bool valid = PHOrbEvaluate(current.board, _solveOptions.goals, evaluation,
                    _solveOptions.allowPoisonWithHeartFour);
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
        // Stop can arrive after began but before the native acknowledgement.
        // Only the captured starting token with unchanged board/turn is ours.
        if (!_ownedOrb && _awaitingStart && _startSent && _route.count &&
            current.dragged == _preview.board.tokens[_route.cells[0]] &&
            PHInputIsQuiet(&current.input) && SamePosition(current, _preview))
            _ownedOrb = current.dragged;
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
    if (!active && !_automatic && !_oneShot && time - _lastRead < 0.10) return;
    _lastRead = time;
    PHOrbSnapshot current{}; NSString *reason = nil;
    BOOL read = (_automatic || _openingInFlight || _openingPuzzlePending ||
        (_historyBound && AcceptedSkillTiming(_historyRules))) ?
        [_engine readAutomation:&current frame:frame reason:&reason] : [_engine read:&current frame:frame reason:&reason];
    _hasLive = read; _live = current;
    _liveReadReason = [reason copy] ?: @""; _lastReadUptime = NSProcessInfo.processInfo.systemUptime;
    [self refreshBattleObservation:read active:active time:time];
    if (!read) {
        _lastReadIssue = @{@"reason":reason ?: @"戰鬥資料未通過讀取校驗", @"phase":@(current.phase),
            @"valid":@(current.valid), @"automationValid":@(current.automation.valid),
            @"width":@(current.board.width), @"height":@(current.board.height),
            @"resizeProgress":@(current.resizeProgress), @"resizeTarget":@(current.resizeTarget),
            @"openingStep":@(_openingStep + (_openingInFlight ? 1 : 0)),
            @"duringAutomatic":@(_automatic), @"sessionPhase":_phase,
            @"multiplayer":MultiplayerTrace(current),
            @"capturedSearchGeneration":_searchTrace[@"generation"] ?: @0};
    }
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
    if (!_automatic) [self observeRuleHistory:&current read:read];
    if (!_automatic && _openingInFlight && _openingStarted && read && current.valid && current.automation.valid && !current.multiplayerDisabled &&
        PHBattleSameIdentity(&_openingBefore.identity, &current.identity)) {
        if (current.automation.skillUses == _openingBefore.automation.skillUses + 1) _openingAcknowledged = true;
        if (!_openingAcknowledged && !current.boardTransition && current.phase == 2 && current.settled && !current.automation.busy && !current.dragged) {
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
                [_engine armMultiplayerOnce:NO];
                _phase = _releaseComplete ? @"complete" : @"stopped";
                if (_releaseComplete) {
                    _oneShot = _oneShotBound = false;
                    ++_completed; _message = current.multiplayer.kind != PHOrbMultiplayerNone ?
                        @"本次路徑已執行並放開；下一盤可操作時可再按轉一次" :
                        @"本次路徑已執行並放開；下一回合請重新讀盤";
                    if (_automatic) {
                        if ([_goalPlan[@"assistRecoveryActive"] boolValue]) {
                            _assistReleased = _preview; _assistAwaitingResult = true;
                            NSMutableDictionary *trace = [_assistTrace mutableCopy];
                            trace[@"status"] = @"awaiting-next-turn"; trace[@"actualConfirmed"] = @NO;
                            _assistTrace = [trace copy];
                        }
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
        if ([_phase isEqual:@"releasing"]) {
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
        if (!HasRouteTime(_live, _route, ArmDelay)) [self queueStop:@"自動路徑超出操作時間，已停止"];
        else [self queueExecute:_generation];
    }
    [self advanceAutomation];
}
- (NSDictionary *)snapshot {
    NSMutableArray *path = [NSMutableArray array], *goalHits = [NSMutableArray array];
    NSMutableArray *ruleUses = [NSMutableArray array], *groupUses = [NSMutableArray array];
    for (NSUInteger turn : _ruleLastUseTurns) [ruleUses addObject:@(turn)];
    for (NSUInteger turn : _groupLastUseTurns) [groupUses addObject:@(turn)];
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
    const bool liveReadable = _hasLive && _live.valid && _live.present && !_live.multiplayerDisabled;
    const bool multiplayer = _live.multiplayer.kind != PHOrbMultiplayerNone || _live.multiplayerDisabled || (_live.present &&
        !PHBattleIsSinglePlayer(_live.identity.entryMode, _live.identity.multiplayer));
    NSDictionary *liveLeaderPlan = multiplayer && [_goalMode isEqual:@"leaders"] ?
        PHOrbPlanForLeaders(&_live) : @{};
    const bool leaderReady = ![_goalMode isEqual:@"leaders"] || [liveLeaderPlan[@"available"] boolValue];
    const bool previewReady = PHOrbCanPreview(&_live);
    const bool timeReady = TimeAvailable(_live) > ArmDelay + TimeMargin + StepDuration(_live);
    const bool multiplayerOnceReady = liveReadable && previewReady && leaderReady && timeReady;
    const bool busy = _automatic || _oneShot || [self executing];
    const bool canOnce = _frame.ready && _frame.enabled && (!multiplayer || multiplayerOnceReady) &&
        !busy && !_inputFault && !_openingInFlight;
    const auto &multi = _live.multiplayer;
    NSMutableArray *onceBlockers = [NSMutableArray array];
    if (!_frame.ready) [onceBlockers addObject:@"frame-not-ready"];
    if (!_frame.enabled) [onceBlockers addObject:@"feature-disabled"];
    if (busy) [onceBlockers addObject:@"busy"];
    if (_inputFault) [onceBlockers addObject:@"input-fault"];
    if (_openingInFlight) [onceBlockers addObject:@"opening-in-flight"];
    if (multiplayer) {
        if (!liveReadable) [onceBlockers addObject:@"live-unreadable"];
        if (!previewReady) [onceBlockers addObject:@"board-not-ready"];
        if (!leaderReady) [onceBlockers addObject:@"leaders-unavailable"];
        if (!timeReady) [onceBlockers addObject:@"time-unavailable"];
    }
    const double uptime = NSProcessInfo.processInfo.systemUptime;
    const double observationAge = _observationUptime > 0 ? std::max(0.0, uptime - _observationUptime) * 1000 : -1;
    const bool observationCurrent = liveReadable && _frame.ready && _frame.enabled && !_inputFault && ![self executing] &&
        !_oneShot && observationAge >= 0 && observationAge <= 1500 &&
        _observationState.observation.status == PHOrbObservationReady && ObservationContextMatches(_observationState, _live);
    NSDictionary *observation = BattleObservationTrace(_observationState, observationCurrent, observationAge, _observationDurationMS);
    NSDictionary *onceReadiness = @{@"canOnce":@(canOnce), @"blockers":onceBlockers,
        @"frameReady":@(_frame.ready), @"frameEnabled":@(_frame.enabled), @"monitoring":@(_monitoring),
        @"readSucceeded":@(_hasLive), @"liveReadable":@(liveReadable), @"present":@(_live.present),
        @"boardRead":@(HasBoardObservation(_live)), @"previewReady":@(previewReady),
        @"settled":@(_live.settled), @"ordinary":@(_live.ordinary), @"boardTransition":@(_live.boardTransition),
        @"dragActive":@(_live.dragged != 0), @"inputQuiet":@(PHInputIsQuiet(&_live.input)),
        @"remainingFrames":@(_live.remainingFrames), @"fps":@(_live.fps),
        @"leaderReady":@(!multiplayer || leaderReady), @"timeReady":@(timeReady),
        @"leaderRead":LeaderReadTrace(_live),
        @"busy":@(busy), @"inputFault":@(_inputFault), @"openingInFlight":@(_openingInFlight),
        @"sessionPhase":_phase, @"nativePhase":@(_live.phase), @"generation":@(_generation),
        @"completed":@(_completed), @"readReason":_liveReadReason,
        @"uptimeMS":@(uint64_t(uptime * 1000)),
        @"readAgeMS":@(_lastReadUptime > 0 ? std::max(0.0, uptime - _lastReadUptime) * 1000 : -1),
        @"multiplayer":MultiplayerTrace(_live)};
    // Retain changes seen by the overlay even if diagnostics are copied after
    // leaving the match. Clock ticks and route swaps do not fill the history.
    NSDictionary *readinessKey = @{@"blockers":onceBlockers, @"sessionPhase":_phase,
        @"nativePhase":@(_live.phase), @"generation":@(_generation), @"kind":@(multi.kind),
        @"turn":@(multi.turnSerial), @"seat":@(multi.localSeat), @"view":@(multi.viewTeam),
        @"dungeon":@(_live.identity.dungeon), @"floor":@(_live.identity.floor), @"wave":@(_live.wave),
        @"readReason":_liveReadReason, @"clockFlags":@(multi.clockFlags),
        @"leaderReadStatus":@(PHOrbLeaderReadStatusName(_live.leaderRead.status)),
        @"leaderReadPass":@(_live.leaderRead.pass), @"leaderReadSlot":@(_live.leaderRead.visibleSlot),
        @"networkFlags":@(multi.networkFlags), @"syncState":@(multi.syncState),
        @"syncStep":@(multi.syncStep), @"disconnectKnown":@(multi.disconnectKnown),
        @"continuationOwnerKnown":@(multi.continuationOwnerKnown), @"continuationDialog":@(multi.continuationDialog),
        @"continuationOwnerFlags":@(multi.continuationOwnerFlags),
        @"recoverySource":@(multi.recoverySource), @"recoveryFlags":@(multi.recoveryFlags),
        @"disconnectSeat":@(multi.disconnectSeat), @"disconnectStatus":@(multi.disconnectStatus),
        @"connectionState":@(multi.connectionState), @"localStatus":@(multi.localStatus),
        @"countdownReady":@(multi.countdownReady), @"finished":@(multi.finished),
        @"resolvePending":@(multi.resolvePending)};
    if (multiplayer && ![_onceReadinessKey isEqual:readinessKey]) {
        if (_onceReadinessHistory.count >= 16) [_onceReadinessHistory removeObjectAtIndex:0];
        [_onceReadinessHistory addObject:onceReadiness];
    }
    _onceReadinessKey = multiplayer ? readinessKey : nil;
    NSString *multiplayerMessage = @"";
    if (multiplayer) {
        if (_inputFault) multiplayerMessage = @"輸入狀態異常，請檢查診斷";
        else if (busy) multiplayerMessage = [_phase isEqual:@"releasing"] ?
            @"正在放開珠子，等待遊戲確認" : @"本次轉珠執行中，可按停止";
        else if (!_frame.ready) multiplayerMessage = @"遊戲校驗或前景尚未就緒";
        else if (!_frame.enabled) multiplayerMessage = @"轉珠功能未啟用，請檢查設定";
        else if (!liveReadable || !multi.known) multiplayerMessage = @"多人資料未確認，暫不能轉一次";
        else if (_openingInFlight) multiplayerMessage = @"等待已啟動的技能結束";
        else if (!PHOrbCanPreview(&_live)) multiplayerMessage = MultiplayerWaitMessage(_live);
        else if (!leaderReady) multiplayerMessage = liveLeaderPlan[@"message"] ?: @"本機隊長／助人資料尚未確認";
        else multiplayerMessage = canOnce ? @"可轉一次；完成後停止" : MultiplayerWaitMessage(_live);
        if (liveReadable && multi.known && multi.teamCount >= 2 && multi.teamCount <= 8 && multi.localSeat < multi.teamCount) {
            NSString *clock = multi.clockKnown && std::isfinite(multi.remainingSeconds) &&
                multi.remainingSeconds >= 0 && multi.remainingSeconds <= 86400 ?
                [NSString stringWithFormat:@"%.1f 秒", multi.remainingSeconds] : @"時間未確認";
            if (multi.kind == PHOrbMultiplayerThreeSolo)
                multiplayerMessage = [multiplayerMessage stringByAppendingFormat:@"\n%@ · 本機 %u／%u · 轉珠 %.1f 秒",
                    MultiplayerModeName(multi.kind), multi.localSeat + 1, multi.teamCount, TimeAvailable(_live)];
            else multiplayerMessage = [multiplayerMessage stringByAppendingFormat:@"\n%@ · 本機 %u／%u · 回合 %u · 剩餘 %@",
                MultiplayerModeName(multi.kind), multi.localSeat + 1, multi.teamCount, multi.turnSerial, clock];
        }
    }
    NSString *traceRelation = !_searchTrace.count ? @"none" : !liveReadable ? @"current-unreadable" :
        PHBattleSameIdentity(&_traceOrigin.identity, &_live.identity) ? @"captured-in-same-battle" : @"captured-in-earlier-battle";
    const bool decisionCurrent = liveReadable && _decisionTurn &&
        PHBattleSameIdentity(&_decisionState.identity, &_live.identity) && PHOrbSameLeaders(&_decisionState, &_live);
    BOOL canExecute = !multiplayer && preview && _route.goalsMet && _route.hazardsSafe && _frame.enabled && !_inputFault && _hasLive && PHOrbCanPreview(&_live) &&
        SamePosition(_live, _preview) && HasRouteTime(_live, _route) && [self hasThornBudget:&_live];
    return @{@"phase":_phase, @"message":_message, @"generation":@(_generation),
        @"canRead":@(_frame.ready && !_automatic && !_oneShot && ![self executing] && !_inputFault && !_openingInFlight), @"canExecute":@(canExecute && !_automatic && !_oneShot),
        @"canStart":@(_frame.ready && _frame.enabled && !multiplayer && !_automatic && !_oneShot && ![self executing] && !_inputFault),
        @"canOnce":@(canOnce),
        @"onceReadiness":onceReadiness, @"onceReadinessHistory":[_onceReadinessHistory copy],
        @"multiplayer":MultiplayerTrace(_live), @"multiplayerActive":@(multiplayer), @"multiplayerMessage":multiplayerMessage,
        @"liveLeaderPlan":liveLeaderPlan,
        @"once":@(_oneShot),
        @"busy":@(busy), @"canStop":@(busy || _readQueued || preview || [_phase isEqual:@"solving"]),
        @"automatic":@(_automatic), @"autoTurn":@(_autoTurn), @"autoMoves":@(_autoMoves), @"autoSkills":@(_autoSkills),
        @"rules":_skillRules, @"automationLog":[_automationLog copy], @"enemyEffects":@(_live.automation.enemyEffects),
        @"ruleDecisions":[_ruleDecisions copy], @"decisionTurn":@(_decisionTurn),
        @"decisionObservedTurn":@(_decisionObservedTurn),
        @"ruleTiming":@{@"observedTurn":@(_observedTurn), @"profileID":_historyTimingProfile[@"id"] ?: @"",
            @"ruleLastUseTurns":ruleUses, @"groupLastUseTurns":groupUses},
        @"ruleDecisionDetails":decisionDetails, @"decisionPhase":@(_decisionState.phase),
        @"executionElapsedMS":@(unsigned(_executionElapsed * 1000)), @"executionLimitMS":@(unsigned(ExecutionLimit * 1000)),
        @"decisionSkillWindow":@(_decisionState.automation.window), @"decisionSkillBusy":@(_decisionState.automation.busy),
        @"decisionSkillWindowReason":PHOrbSkillWindowDescription(&_decisionState.automation),
        @"decisionContextCurrent":@(decisionCurrent),
        @"decisionLeaderNumbers":@[@(_decisionState.leaderNumbers[0]), @(_decisionState.leaderNumbers[1])],
        @"decisionDungeon":@(_decisionState.identity.dungeon), @"decisionFloor":@(_decisionState.identity.floor),
        @"decisionWave":@(_decisionState.wave),
        @"playerStatus":PHOrbSurvivalDescription(&_live), @"confirmedSkillRules":@(_confirmedRules),
        @"battleObservation":observation,
        @"battleObservationDescription":multiplayer ? @"" : BattleObservationDescription(_observationState.observation, observationCurrent),
        @"obstacles":PHOrbObstacleDescription(&_live), @"usedSkillGroups":@(_usedGroups),
        @"activeProfile":_activeProfile,
        @"opening":@{@"profile":_openingProfile[@"id"] ?: @"", @"completed":@(_openingStep),
            @"count":@([_openingProfile[@"opening"] count]), @"inFlight":@(_openingInFlight),
            @"puzzlePending":@(_openingPuzzlePending), @"steps":[_openingLog copy]},
        @"searchTrace":_searchTrace, @"executionHistory":[_executionHistory copy],
        @"liveReadValid":@(liveReadable), @"searchTraceRelation":traceRelation,
        @"boardTransition":@(_live.boardTransition), @"boardTransitionFrames":@(_boardTransitionFrames),
        @"resizeProgress":@(_live.resizeProgress), @"resizeTarget":@(_live.resizeTarget), @"lastReadIssue":_lastReadIssue,
        @"profileContext":@{@"known":@(_hasLive && _live.valid && _live.present && !_live.multiplayerDisabled),
            @"dungeon":@(_live.identity.dungeon), @"floor":@(_live.identity.floor)},
        @"playerBypass":@(_live.automation.playerBypass), @"skillWindow":@(_live.automation.window),
        @"enabled":@(_frame.ready && _frame.enabled), @"board":BoardJSON(_route.count ? _preview.board : _live.board), @"path":path,
        @"quality":_quality, @"searchSteps":@(_solveOptions.steps), @"searchBeam":@(_solveOptions.beam),
        @"thornComboPriority":@(_thornComboPriority),
        @"allowPoisonWithHeartFour":@(_allowPoisonWithHeartFour),
        @"diagonal":@(_diagonal), @"routeDiagonal":@(_route.diagonal),
        @"assistRecoveryL":@(_assistRecoveryL), @"assistRecoveryTrace":_assistTrace,
        @"assistRecoveryStatus":_goalPlan[@"assistRecoveryMessage"] ?: @"",
        @"recovery":_recovery, @"recoveryActive":_goalPlan[@"recoveryActive"] ?: @NO,
        @"recoveryStatus":_goalPlan[@"recoveryMessage"] ?: @"", @"recoveryTurn":@(_recoveryTurn),
        @"goalMode":_goalMode, @"goals":_manualGoals, @"goalPlan":_goalPlan,
        @"goalCount":@(_route.goalCount), @"matchedGoals":@(_route.matchedGoals), @"goalsMet":@(_route.goalsMet),
        @"hazardsSafe":@(_route.hazardsSafe), @"poisonErased":@(_route.poisonErased), @"unmatchedBombs":@(_route.unmatchedBombs),
        @"mortalPoisonErased":@(_route.mortalPoisonErased),
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
        @"leaderContext":LeaderTrace(_live),
        @"searchBudgetMS":@(_solveOptions.milliseconds),
        @"combos":@(_route.combos), @"steps":@(_route.count ? _route.count - 1 : 0), @"confirmed":@(_confirmed),
        @"elapsedMS":@(_route.elapsedMS), @"evaluated":@(_route.evaluated), @"deadline":@(_route.deadlineReached),
        @"evaluationCalls":@(_route.evaluationCalls), @"evaluationCacheHits":@(_route.evaluationCacheHits),
        @"searchPasses":@(_route.searchPasses), @"backtracks":@(_route.backtracks), @"exploredDepth":@(_route.exploredDepth),
        @"completed":@(_completed), @"cancelled":@(_cancelled), @"inputFrames":@(_sentFrames), @"inputFault":@(_inputFault),
        @"startSent":@(_startSent), @"startAccepted":@(_startSent && !_awaitingStart),
        @"multiplayerDisabled":@(_live.multiplayerDisabled), @"nativePhase":@(_live.phase), @"wave":@(_live.wave),
        @"moves":@(_live.moves), @"remainingFrames":@(_live.remainingFrames), @"fps":@(_live.fps),
        @"settled":@(_live.settled), @"ordinary":@(_live.ordinary), @"deviceVerified":@NO};
}
@end
