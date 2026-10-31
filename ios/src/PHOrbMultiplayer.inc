// PAD 23.6.0 native evidence: ios/research/orb-multiplayer-20261031.json.
// This reader never invokes the multiplayer pump, bank getters or packets.
bool ReadNativeElapsed(uintptr_t header, uint64_t started, double &seconds) {
    uint32_t numerator = 0, denominator = 0;
    if (!started || !PHReadU32(header + PHOrbMultiClockNumeratorRVA, &numerator) || !numerator ||
        !PHReadU32(header + PHOrbMultiClockDenominatorRVA, &denominator) || !denominator) return false;
    const uint64_t now = mach_absolute_time();
    if (started > now) return false;
    // Use the same float32 conversion as 0x100638F4C; retain its precision.
    const float nanos = (float(now - started) * float(numerator)) / float(denominator);
    if (!std::isfinite(nanos) || nanos < 0 || double(nanos) >= double(UINT64_MAX)) return false;
    seconds = double(float(uint64_t(nanos)) / 1000000000.0f);
    return std::isfinite(seconds) && seconds <= 86400;
}

bool ReadMultiplayerFields(PHOrbSnapshot &state) {
    auto &m = state.multiplayer;
    if (m.kind == PHOrbMultiplayerNone) return true;
    const uintptr_t h = state.identity.header, p = state.identity.impl;
    if (m.kind == PHOrbMultiplayerUnsupported ||
        !PHReadU32(p + PHBattleTurnSerialOffset, &m.turnSerial) || m.turnSerial > INT32_MAX ||
        !PHReadU32(p + PHBattleTeamCountOffset, &m.teamCount) || m.teamCount < 2 || m.teamCount > 8 ||
        !PHReadU32(p + PHBattlePlayerTeamOffset, &m.localSeat) || m.localSeat >= m.teamCount ||
        !PHReadU32(p + PHBattleCurrentTeamOffset, &m.viewTeam) || m.viewTeam >= m.teamCount ||
        !PHReadU32(p + PHBattleTeamDescriptorOffset + m.localSeat * PHBattleTeamDescriptorStride + 28, &m.localStatus) ||
        !PHReadU32(p + PHOrbMultiNetworkFlagsOffset, &m.networkFlags) ||
        !PHReadU32(p + PHOrbMultiSyncStateOffset, &m.syncState) ||
        !PHReadU32(p + PHOrbMultiSyncStepOffset, &m.syncStep) ||
        !PHReadU32(h + PHOrbMultiConnectionRVA, &m.connectionState) ||
        !PHReadU8(p + PHOrbMultiResolveOffset, &m.resolvePending)) return false;
    m.disconnectKnown = false; m.disconnectSeat = m.disconnectStatus = 0;
    if (m.kind == PHOrbMultiplayerFour && ((m.networkFlags & 0x10000u) || m.syncState == 15)) {
        // The recorded seat is an index, not the co-op turn or helper selector.
        // Never dereference an unbounded descriptor or infer a remote exit
        // solely from a nonzero sync state / retained flag.
        if (!PHReadU32(p + PHOrbMultiDisconnectSeatOffset, &m.disconnectSeat) || m.disconnectSeat >= m.teamCount ||
            !PHReadU32(p + PHBattleTeamDescriptorOffset + m.disconnectSeat * PHBattleTeamDescriptorStride + 28,
                &m.disconnectStatus)) return false;
        m.disconnectKnown = true;
    }
    m.known = true;
    if (m.kind == PHOrbMultiplayerThree) {
        // 0x10001ECB8: per-turn native clock, including paused accumulated time.
        auto &flags = m.clockFlags; uint32_t paused = 0;
        float accumulated = 0, limit = 0;
        if (!PHReadMemory(p + PHOrbMultiTurnClockOffset, &m.clockStart, sizeof(m.clockStart)) ||
            !PHReadU8(p + PHOrbMultiTurnClockOffset + 8, &flags) || (flags & ~3u) ||
            !PHReadMemory(p + PHOrbMultiTurnClockOffset + 12, &accumulated, sizeof(accumulated)) ||
            !PHReadU32(p + PHOrbMultiTurnClockOffset + 16, &paused) ||
            !PHReadMemory(p + PHOrbMultiTurnClockOffset + 20, &limit, sizeof(limit)) ||
            !std::isfinite(accumulated) || accumulated < 0 || !std::isfinite(limit) || limit <= 0 || limit > 86400) return true;
        double elapsed = 0;
        if (flags & 1u) {
            if (flags & 2u) elapsed = double(float(paused ^ 0x33c2f3a1u) / 100.0f);
            else {
                if (!ReadNativeElapsed(h, m.clockStart, elapsed)) return true;
                elapsed = double(float(elapsed) + accumulated);
            }
        }
        if (elapsed < 0 || elapsed > 86400) return true;
        m.remainingSeconds = std::max(0.0, double(limit) - elapsed);
        m.clockKnown = true;
    } else {
        // Modes 10/11 use the dungeon-wide clock, not the co-op turn timeout.
        auto &flags = m.clockFlags; uint32_t paused = 0; uintptr_t dungeon = 0; uint16_t limit = 0;
        if (!PHReadU8(p + PHOrbMultiCountdownOffset, &m.countdownReady) ||
            !PHReadU8(p + PHOrbMultiFinishedOffset, &m.finished) ||
            !PHReadMemory(p + PHOrbMultiMatchClockOffset, &m.clockStart, sizeof(m.clockStart)) ||
            !PHReadU8(p + PHOrbMultiMatchClockOffset + 24, &flags) ||
            !PHReadU32(p + PHOrbMultiMatchClockOffset + 32, &paused) ||
            !PHReadPointer(p + PHOrbSkillDungeonRulesOffset, &dungeon) || !dungeon || dungeon > UINTPTR_MAX - 34 ||
            !PHReadU16(dungeon + 12, &limit) || !limit || limit > INT16_MAX) return true;
        // 0x1005A76D4 returns the terminal value for bits 1/5. Never start then.
        if ((flags & 0x26u) != 4u) return true;
        double elapsed = 0;
        if (flags & 0x10u) elapsed = double(float(paused ^ 0x33c2f3a1u) / 100.0f);
        else if (!ReadNativeElapsed(h, m.clockStart, elapsed)) return true;
        if (elapsed < 0 || elapsed > 86400) return true;
        m.remainingSeconds = std::max(0.0, double(limit) - elapsed);
        m.clockKnown = true;
    }
    return true;
}

bool SameMultiplayerRead(const PHOrbMultiplayerState &a, const PHOrbMultiplayerState &b) {
    return PHOrbSameMultiplayerTurn(&a, &b) && a.localStatus == b.localStatus &&
        a.networkFlags == b.networkFlags && a.syncState == b.syncState && a.syncStep == b.syncStep &&
        a.connectionState == b.connectionState && a.disconnectKnown == b.disconnectKnown &&
        a.disconnectSeat == b.disconnectSeat && a.disconnectStatus == b.disconnectStatus &&
        a.countdownReady == b.countdownReady && a.finished == b.finished && a.resolvePending == b.resolvePending &&
        a.clockKnown == b.clockKnown && a.clockFlags == b.clockFlags && a.clockStart == b.clockStart;
}

bool SameOrbBattleEntry(const PHBattleIdentity &a, const PHBattleIdentity &b) {
    // Modes, selectors, floors, teams and allocation addresses may change
    // within a battle. Only the account/image or entry nonce retires a latch.
    return a.header == b.header && a.player == b.player && a.epoch == b.epoch &&
        !std::memcmp(a.entryHash, b.entryHash, sizeof(a.entryHash));
}

bool ConfirmOrbEntryIdentity(const PHBattleIdentity &id) {
    const uintptr_t h = id.header, save = h + PHSaveRVA;
    uint8_t scene[2]{}, hash[32]{};
    uint16_t mode = 0, source = 0, ids[2]{};
    uint32_t player = 0, multi = 0;
    uintptr_t object = 0, impl = 0, active = 0;
    return PHReadMemory(save, scene, sizeof(scene)) && scene[0] == 1 && scene[1] == 1 &&
        PHReadU32(save + PHPlayerIDOffset, &player) && player == id.player &&
        PHReadMemory(save + PHBattleDungeonOffset, ids, sizeof(ids)) && ids[0] == id.dungeon && ids[1] == id.floor &&
        PHReadMemory(h + PHBattleEntryHashRVA, hash, sizeof(hash)) && !std::memcmp(hash, id.entryHash, sizeof(hash)) &&
        PHReadU16(h + PHBattleModeRVA, &mode) && int16_t(mode) == id.entryMode &&
        PHReadU16(save + PHBattleSourceModeOffset, &source) && source == id.sourceMode &&
        PHReadU32(h + PHBattleMultiplayerRVA, &multi) && multi == id.multiplayer &&
        PHReadPointer(h + PHSceneTableRVA + 24, &object) && object == id.game &&
        PHReadPointer(id.game + 0x70, &impl) && impl == id.impl &&
        PHReadPointer(h + PHBattleImplRVA, &active) && active == id.impl;
}

bool ConfirmMultiplayerIdentity(const PHOrbSnapshot &state) {
    const auto &id = state.identity;
    const uintptr_t h = id.header;
    uint8_t shape[8]{};
    uint32_t key1 = 0, key2 = 0, phase = 0, wave = 0, moves = 0;
    uintptr_t manager = 0, dragged = 0;
    return ConfirmOrbEntryIdentity(id) &&
        PHReadPointer(h + PHBattleInputRVA, &manager) && manager == state.inputManager &&
        PHReadPointer(id.impl + PHBattleDragPointerOffset, &dragged) && dragged == state.dragged &&
        PHReadU32(id.impl + PHOrbMoveSerialOffset, &moves) && moves == state.moves &&
        PHReadMemory(id.game + 88, shape, sizeof(shape)) && shape[0] == state.board.width && shape[1] == state.board.height &&
        shape[4] == state.minErase && shape[5] == state.freeMovement &&
        PHReadU32(h + PHBattleKey1RVA, &key1) && PHReadU32(h + PHBattleKey2RVA, &key2) &&
        Decode(id.impl + PHBattlePhaseOffset, key1, key2, 0x766cfc6du, phase) && phase == state.phase &&
        Decode(id.impl + PHBattleWaveOffset, key1, key2, 0x766cfc6du, wave) && wave == state.wave;
}

NSString *MultiplayerReason(const PHOrbSnapshot &state) {
    const auto &m = state.multiplayer;
    if (!m.known) return @"多人模式的本機席位或回合資料尚未確認";
    if (m.kind == PHOrbMultiplayerThree && m.teamCount && m.turnSerial % m.teamCount != m.localSeat)
        return @"3 人協力：尚未輪到自己，請在自己的回合再按轉一次";
    if (m.viewTeam != m.localSeat) return @"目前顯示其他玩家的隊伍；請返回自己的盤面";
    if (!PHOrbMultiplayerLocalReady(&m))
        return @"多人同步、交接或連線狀態尚未就緒";
    if (m.kind != PHOrbMultiplayerThree && (m.countdownReady != 1 || m.finished))
        return @"對戰尚未開始、已結束或正在結算";
    if (!m.clockKnown || m.remainingSeconds <= 0) return @"多人操作時間尚未確認或已結束";
    if ((m.kind == PHOrbMultiplayerThree && ((m.clockFlags & ~3u) || ((m.clockFlags & 2u) && !state.dragged))) ||
        (m.kind != PHOrbMultiplayerThree && (m.clockFlags & 0x36u) != 4u)) return @"多人計時暫停或已結束";
    if (m.resolvePending) return @"本盤已放開，等待遊戲結算";
    return @"多人只支援本機盤面轉一次；請確認自己的操作窗口";
}
