// Serialization and explanatory text only. Uses supplied snapshots, never
// engine reads, action authorization, or persistent copies of live state.
NSString *SessionPhaseText(PHOrbSessionPhase phase) { return @(PHOrbSessionPhaseName(phase)); }
#include "PHOrbObservationPresentation.inc"
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
    if (PHOrbBattleEnded(&state)) return @"本場已通關或進入結算";
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
NSDictionary *ReadIssueTrace(const PHOrbSnapshot &state, NSString *reason, NSUInteger openingStep,
    bool automatic, PHOrbSessionPhase phase, NSNumber *generation, bool multiplayer) {
    NSMutableDictionary *trace = [@{@"reason":reason, @"phase":@(state.phase),
        @"valid":@(state.valid), @"automationValid":@(state.automation.valid),
        @"width":@(state.board.width), @"height":@(state.board.height),
        @"resizeProgress":@(state.resizeProgress), @"resizeTarget":@(state.resizeTarget),
        @"openingStep":@(openingStep), @"duringAutomatic":@(automatic),
        @"sessionPhase":SessionPhaseText(phase), @"capturedSearchGeneration":generation} mutableCopy];
    if (multiplayer) trace[@"multiplayer"] = MultiplayerTrace(state);
    return [trace copy];
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
NSDictionary *EightTeamTrace(const PHOrbSnapshot &state) {
    NSMutableArray *members = [NSMutableArray array];
    if (state.eight.valid) for (unsigned slot = 0; slot < 6; ++slot) {
        const auto &m = state.eight.members[slot]; const auto &a = state.automation.members[slot];
        NSMutableArray *convert = [NSMutableArray array], *generate = [NSMutableArray array];
        for (unsigned c = 0; c < 6; ++c) { [convert addObject:@(m.convert[c])]; [generate addObject:@(m.generate[c])]; }
        [members addObject:@{@"slot":@(slot + 1),@"number":@(a.number),@"catalogID":@(m.catalogID),
            @"sourceSlot":@(m.sourceSlot),@"activeSkill":@(m.activeSkill),@"rootSkill":@(m.rootSkill),
            @"leaderSkill":@(m.leaderSkill),@"cooldown":@(m.cooldown),@"charge":@(a.charge),@"limitedUses":@(a.skillUses[0]),
            @"active":@(a.active),@"bound":@(a.bound),@"state":@(a.state),
            @"skillKnown":@(m.skillKnown),@"leaderKnown":@(m.leaderKnown),@"convert":convert,@"generate":generate,
            @"boardColors":@(m.boardColors),@"unlock":@(m.unlock),@"noSkyfall":@(m.noSkyfall),
            @"attackColors":@(m.attackColors),@"attackLength":@(m.attackLength),@"attackPercent":@(m.attackPercent),
            @"comboColors":@(m.comboColors),@"comboLength":@(m.comboLength),@"addedCombos":@(m.addedCombos),
            @"skillSignature":[NSString stringWithFormat:@"%016llx",(unsigned long long)m.skillSignature],
            @"leaderSignature":[NSString stringWithFormat:@"%016llx",(unsigned long long)m.leaderSignature]}];
    }
    return @{@"valid":@(state.eight.valid),@"members":members};
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
    NSMutableArray *hits = [NSMutableArray array], *retained = [NSMutableArray array];
    for (unsigned g = 0; g < goals.count; ++g) [hits addObject:@(value.goalHits[g])];
    for (unsigned c = 0; c < 6; ++c) [retained addObject:@(value.retainedAfterConversion[c])];
    return @{@"valid":@YES, @"scope":@"all-shared-spinner-phases", @"goalsMet":@(value.goalsMet), @"goalHits":hits, @"combos":@(value.combos),
        @"firstWaveCombos":@(value.firstWaveCombos), @"comboFirstWaveRequired":@(goals.comboFirstWave),
        @"bonusTriggersFirstWaveRequired":@(goals.bonusFirstWave),
        @"activityHeuristic":@(value.activityValue), @"nativeScoreKnown":@NO,
        @"preparationReady":@(value.preparationReady),@"retainedAfterConversion":retained,
        @"rowGroups":@[@(value.rowGroups[0]),@(value.rowGroups[1]),@(value.rowGroups[2]),@(value.rowGroups[3]),@(value.rowGroups[4]),@(value.rowGroups[5])],
        @"linkGroups":@[@(value.linkGroups[0]),@(value.linkGroups[1]),@(value.linkGroups[2]),@(value.linkGroups[3]),@(value.linkGroups[4]),@(value.linkGroups[5])],
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
#include "PHOrbFailurePresentation.inc"
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
