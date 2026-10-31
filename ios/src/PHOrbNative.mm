#import "PHOrbSession.h"
#import "PHOrbSkillWindow.h"
#import "PHInput.h"
#import "PHTarget.h"
#import <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include "PHTargetGenerated.h"
#include "PHOrbGenerated.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
constexpr size_t PoolStride = 172, PoolCount = 256;
using Manager = std::array<uint8_t, PHOrbInputBytes>;
struct NativeSkillReadiness { int32_t baseSkill, assistSkill, baseCooldown, totalCooldown; };
template<typename T> T Load(const uint8_t *p) { T value; std::memcpy(&value, p, sizeof(value)); return value; }
template<typename T> void Store(uint8_t *p, T value) { std::memcpy(p, &value, sizeof(value)); }

bool Decode(uintptr_t address, uint32_t key1, uint32_t key2, uint32_t salt, uint32_t &value) {
    uint32_t raw[4];
    return PHReadMemory(address, raw, sizeof(raw)) && PHBattleDecodeProtected(raw, key1, key2, salt, &value);
}

bool CheckCode(uintptr_t header, NSString **reason) {
    if (!PHValidateGuest(reinterpret_cast<const mach_header_64 *>(header), reason)) return false;
    for (const auto &guard : PHOrbTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) {
            if (reason) *reason = @"本版讀盤／輸入程式校驗不符";
            return false;
        }
    }
    return true;
}

#include "PHOrbLeaders.inc"

bool ReadSpinner(PHOrbSnapshot &state, uintptr_t impl, uint32_t key1, uint32_t key2) {
    auto &count = state.spinnerRawCount, &period = state.spinnerRawPeriod, &colors = state.spinnerRawColors;
    state.spinnerReadStatus = PHOrbSpinnerBadCount;
    if (!Decode(impl + PHOrbSpinnerCountOffset, key1, key2, 0x6a875a8bu, count) || count > PHOrbMaxCells) return false;
    state.spinnerReadStatus = PHOrbSpinnerBadPeriod;
    if (!Decode(impl + PHOrbSpinnerPeriodOffset, key1, key2, 0x6a875a8bu, period) || !period || period > 30000) return false;
    state.spinnerReadStatus = PHOrbSpinnerBadColors;
    if (!Decode(impl + PHOrbSpinnerColorsOffset, key1, key2, 0x6a875a8bu, colors) || colors >= (1u << PHOrbTypes)) return false;
    uint64_t mask = 0;
    state.spinnerReadStatus = PHOrbSpinnerBadRows;
    for (unsigned row = 0; row < 6; ++row) {
        uint32_t columns = 0;
        if (!Decode(impl + PHOrbSpinnerRowsOffset + 16 * row, key1, key2, 0x6a875a8bu, columns) ||
            columns > 127) return false;
        state.spinnerRawRows[row] = uint8_t(columns);
        state.spinnerStoredCells += uint8_t(__builtin_popcount(columns));
        // 0x1005AB350 clips updates to current width/height after resizing.
        // The setter's count and six native row masks retain offscreen cells.
        if (row >= state.board.height) continue;
        for (unsigned column = 0; column < state.board.width; ++column) if (columns & (1u << column)) {
            mask |= UINT64_C(1) << ((state.board.height - 1 - row) * state.board.width + column);
        }
    }
    state.spinnerReadStatus = PHOrbSpinnerCountMismatch;
    if (state.spinnerStoredCells != count) return false;
    state.board.spinnerMask = mask;
    state.board.spinnerColors = mask ? uint16_t(colors ? colors : 63) : 0;
    state.spinnerPeriodFrames = uint16_t(period);
    state.spinnerReadStatus = PHOrbSpinnerSupported(&state.board) ? PHOrbSpinnerReady : PHOrbSpinnerBadPhase;
    return state.spinnerReadStatus == PHOrbSpinnerReady;
}

bool ReadBoardEffects(PHOrbSnapshot &state, uintptr_t impl, uint32_t key1, uint32_t key2, uint32_t fixedX) {
    auto &board = state.board;
    const unsigned width = board.width, height = board.height;
    if (fixedX > 255) return false;
    if (int8_t(fixedX) >= 0) {
        uint32_t fixedY = 0;
        if (!Decode(impl + PHOrbFixedStartYOffset, key1, key2, 0x6a875a8bu, fixedY) || fixedX >= width || fixedY >= height) return false;
        board.fixedStart = uint8_t((height - 1 - fixedY) * width + fixedX + 1);
    } else if (fixedX != 255) return false;
    board.minErase = state.minErase;
    uint16_t connectedA = 0, connectedB = 0;
    if (!PHReadU16(impl + PHOrbMinMatchOffset, &connectedA) || !PHReadU16(impl + PHOrbMinMatchExtraOffset, &connectedB) ||
        connectedA > width * height || connectedB > width * height) return false;
    board.minMatch = uint8_t(std::max(connectedA, connectedB));
    // All durations use signed ten-bit turns. Preserve a nonzero invalid
    // encoding as an explicit blocker; never silently treat it as inactive.
    auto duration = [&](uintptr_t offset, uint16_t &turns, uint32_t obstacle) {
        uint16_t raw = 0;
        if (!PHReadU16(impl + offset, &raw)) return false;
        turns = raw & 0x3ffu;
        if (turns) state.obstacles |= obstacle;
        return turns <= 511;
    };
    if (!duration(PHOrbUnmatchableDurationOffset, state.unmatchableTurns, PHOrbObstacleUnmatchable)) return false;
    if (state.unmatchableTurns) {
        uint32_t colors = 0;
        if (!Decode(impl + PHOrbUnmatchableColorsOffset, key1, key2, 0x6a875a8bu, colors) || !colors || colors >= (1u << PHOrbTypes)) return false;
        board.unmatchableColors = uint16_t(colors);
    }
    state.unmatchableKnown = true;
    if (!duration(PHOrbTapeRowDurationOffset, state.tapeRowTurns, PHOrbObstacleTape) ||
        !duration(PHOrbTapeColumnDurationOffset, state.tapeColumnTurns, PHOrbObstacleTape) ||
        !duration(PHOrbCloudDurationOffset, state.cloudTurns, PHOrbObstacleCloud) ||
        !duration(PHOrbSpinnerDurationOffset, state.spinnerTurns, PHOrbObstacleSpinner)) return false;
    uint32_t rows = 0, columns = 0;
    if (state.tapeRowTurns && (!Decode(impl + PHOrbTapeRowsOffset, key1, key2, 0x6a875a8bu, rows) || !rows || (rows >> height))) return false;
    if (state.tapeColumnTurns && (!Decode(impl + PHOrbTapeColumnsOffset, key1, key2, 0x6a875a8bu, columns) || !columns || (columns >> width))) return false;
    // Tape row bits are top-first (sub_100608DB8), unlike spinner/native Y.
    for (unsigned row = 0; row < height; ++row) for (unsigned column = 0; column < width; ++column)
        if ((rows & (1u << row)) || (columns & (1u << column))) board.tapeMask |= UINT64_C(1) << (row * width + column);
    if (state.cloudTurns) {
        uint32_t x = 0, y = 0, w = 0, h = 0;
        if (!Decode(impl + PHOrbCloudXOffset, key1, key2, 0x6a875a8bu, x) || x >= width ||
            !Decode(impl + PHOrbCloudYOffset, key1, key2, 0x6a875a8bu, y) || y >= height ||
            !Decode(impl + PHOrbCloudWidthOffset, key1, key2, 0x6a875a8bu, w) || !w || w > 7 ||
            !Decode(impl + PHOrbCloudHeightOffset, key1, key2, 0x6a875a8bu, h) || !h || h > 6) return false;
        // Native clouds clip at the current board bounds after a resize.
        for (unsigned nativeY = y; nativeY < std::min(y + h, height); ++nativeY)
            for (unsigned column = x; column < std::min(x + w, width); ++column)
                board.cloudMask |= UINT64_C(1) << ((height - 1 - nativeY) * width + column);
    }
    return !state.spinnerTurns || ReadSpinner(state, impl, key1, key2);
}

#include "PHOrbMultiplayer.inc"

bool Read(PHOrbSnapshot &state, PHBattleFrame frame, NSString **reason,
    PHOrbMultiplayerKind observedKind, bool multiplayerDisabled,
    const PHBattleIdentity *observedEntry, bool &entryConfirmed) {
    state = {};
    entryConfirmed = false;
    if (!NSThread.isMainThread || !frame.ready || !frame.header || !frame.player || frame.header > UINTPTR_MAX - 0x3000000u) {
        if (reason) *reason = @"等待已校驗的遊戲與登入";
        return false;
    }
    const uintptr_t header = frame.header, save = header + PHSaveRVA, game = header + PHGameMainRVA;
    uint8_t scene[2]; uint32_t player = 0;
    if (reason) *reason = @"登入或場景資料未通過讀取校驗";
    if (!PHReadMemory(save, scene, sizeof(scene)) || !PHReadU32(save + PHPlayerIDOffset, &player) || player != frame.player) return false;
    if (scene[0] != 1) { if (reason) *reason = @"進入關卡後，在自己的操作窗口讀盤"; return true; }
    if (scene[1] != 1) { if (reason) *reason = @"場景切換中"; return false; }
    uint16_t mode = 0, source = 0, ids[2]{};
    uint32_t multi = 0;
    if (reason) *reason = @"遊戲模式資料未通過讀取校驗";
    if (!PHReadU16(header + PHBattleModeRVA, &mode) || !PHReadU32(header + PHBattleMultiplayerRVA, &multi) ||
        !PHReadU16(save + PHBattleSourceModeOffset, &source)) return false;
    state.present = true;
    state.identity = {header, game, 0, frame.player, frame.epoch, 0, 0, {}, int16_t(mode), source, multi};
    state.multiplayer.kind = PHOrbMultiplayerMode(int16_t(mode), multi);
    uintptr_t object = 0, impl = 0, active = 0;
    if (!PHReadPointer(header + PHSceneTableRVA + 24, &object) || object != game) {
        if (reason) *reason = @"場景表的戰鬥物件未通過讀取校驗"; return false;
    }
    if (!PHReadPointer(game + 0x70, &impl) || !impl || (impl & 7u) || impl > UINTPTR_MAX - 0xc4000) {
        if (reason) *reason = @"戰鬥實作指標未通過讀取校驗"; return false;
    }
    if (!PHReadPointer(header + PHBattleImplRVA, &active) || active != impl) {
        if (reason) *reason = @"目前戰鬥實作與場景物件不一致"; return false;
    }
    if (!PHMemoryHasProtection(impl + 0x92000, 0x2e000, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) {
        if (reason) *reason = @"戰鬥記憶體範圍未通過讀寫保護校驗"; return false;
    }
    state.identity.impl = impl;
    if (reason) *reason = @"關卡入場識別未通過讀取校驗";
    if (!PHReadMemory(save + PHBattleDungeonOffset, ids, sizeof(ids)) || !ids[0] || !ids[1] ||
        !PHReadMemory(header + PHBattleEntryHashRVA, state.identity.entryHash, sizeof(state.identity.entryHash))) return false;
    state.identity.dungeon = ids[0]; state.identity.floor = ids[1];
    bool hashPresent = false;
    for (uint8_t byte : state.identity.entryHash) hashPresent |= byte != 0;
    if (!hashPresent) return false;
    if (reason) *reason = @"讀取期間關卡入場識別或場景已改變";
    if (!ConfirmOrbEntryIdentity(state.identity)) return false;
    entryConfirmed = true;
    const bool newEntry = observedEntry && !SameOrbBattleEntry(*observedEntry, state.identity);
    if ((!newEntry && multiplayerDisabled) || state.multiplayer.kind == PHOrbMultiplayerUnsupported ||
        (!newEntry && observedKind != PHOrbMultiplayerNone && state.multiplayer.kind != observedKind)) {
        // A missed menu frame must not carry the previous match's latch into
        // a verified new entry. Keep same-entry selector changes closed before
        // any board/leader access, including a temporary return to solo mode.
        state.multiplayerDisabled = true;
        if (reason) *reason = @"此多人模式或連線過渡狀態尚不支援轉一次";
        return true;
    }
    uint32_t key1 = 0, key2 = 0, fixedX = 0, audit = 0;
    uint8_t shape[8]{};
    uintptr_t dialog = 0;
    uint16_t specialDrag = 0;
    uint8_t moveBlocked = 0, intro = 0;
    if (reason) *reason = @"戰鬥身分、受保護階段或輸入資料未通過讀取校驗";
    if (!PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
        !Decode(impl + PHBattlePhaseOffset, key1, key2, 0x766cfc6du, state.phase) ||
        !Decode(impl + PHBattleWaveOffset, key1, key2, 0x766cfc6du, state.wave) ||
        !Decode(impl + PHOrbFixedStartOffset, key1, key2, 0x6a875a8bu, fixedX) ||
        !PHReadMemory(game + 88, shape, sizeof(shape)) || !PHReadU32(impl + PHOrbMoveSerialOffset, &state.moves) ||
        !PHReadPointer(impl + PHBattleDragPointerOffset, &state.dragged) ||
        !PHReadPointer(header + PHBattleInputRVA, &state.inputManager) || !state.inputManager ||
        !PHReadU16(impl + PHOrbRemainingOffset, &state.remainingFrames) || !PHReadU8(header + PHBattleScaleRVA, &state.fps) ||
        !PHReadU32(save + PHBattleAuditOffset, &audit) || !PHReadPointer(impl + PHOrbDialogOffset, &dialog) ||
        !PHReadU16(impl + PHOrbSpecialDragOffset, &specialDrag) || !PHReadU8(impl + PHOrbMoveBlockedOffset, &moveBlocked) ||
        !PHReadU8(impl + PHBattleIntroOffset, &intro) || !PHReadGameInput(header, &state.input)) return false;
    if (reason) *reason = @"改盤動畫資料未通過讀取校驗";
    float resize[2]{};
    if (!PHReadMemory(impl + PHOrbResizeProgressOffset, resize, sizeof(resize)) ||
        !std::isfinite(resize[0]) || !std::isfinite(resize[1]) ||
        resize[0] < 0 || resize[0] > 1 || resize[1] < 0 || resize[1] > 1) return false;
    state.resizeProgress = resize[0]; state.resizeTarget = resize[1];
    // 0x1005AB350 finalizes dimensions/centers on the frame after progress
    // reaches one, then resets this interpolator to zero. Phase may still be 2.
    state.boardTransition = resize[0] != resize[1] || resize[0] >= 1;
    state.minErase = shape[4]; state.freeMovement = shape[5];
    state.board.width = shape[0]; state.board.height = shape[1];
    if (!ReadMultiplayerFields(state)) {
        if (reason) *reason = @"多人本機席位、回合或連線資料未通過讀取校驗";
        return false;
    }
    PHOrbSnapshot window = state; window.valid = true;
    const bool multiplayerMetadataOnly = state.multiplayer.kind != PHOrbMultiplayerNone && !PHOrbMultiplayerWindow(&window);
    // Skills may remove unmatchable/locked/special orbs. Their window is
    // separate from ordinary drag eligibility, never a relaxation of it.
    state.automation.window = state.multiplayer.kind == PHOrbMultiplayerNone &&
        source == 0 && !audit && !dialog && !specialDrag && !moveBlocked && !intro;
    if (!state.automation.window) state.automation.windowBlocks |= PHOrbSkillWindowScene;
    // Release may enter the resolve phase and start removing orb objects in
    // this same native frame. Identity + drag metadata is sufficient to
    // acknowledge release; a resolving board must never be used for a route.
    if (state.phase != 2 || state.boardTransition || multiplayerMetadataOnly) {
        uint32_t lastKey1 = 0, lastKey2 = 0, lastPhase = 0, lastWave = 0;
        uintptr_t lastImpl = 0, lastActive = 0, lastInput = 0;
        uint8_t lastScene[2]{}, lastHash[32]{}; uint16_t lastMode = 0, lastSource = 0, lastIDs[2]{};
        uint32_t lastPlayer = 0, lastMulti = 0;
        if (reason) *reason = @"動畫期間戰鬥身分或階段已改變，未採用快照";
        if (!PHReadMemory(save, lastScene, sizeof(lastScene)) || std::memcmp(scene,lastScene,sizeof(scene)) ||
            !PHReadU32(save + PHPlayerIDOffset, &lastPlayer) || lastPlayer != player ||
            !PHReadMemory(save + PHBattleDungeonOffset, lastIDs, sizeof(lastIDs)) || std::memcmp(ids,lastIDs,sizeof(ids)) ||
            !PHReadU16(header + PHBattleModeRVA, &lastMode) || lastMode != mode ||
            !PHReadU16(save + PHBattleSourceModeOffset, &lastSource) || lastSource != source ||
            !PHReadU32(header + PHBattleMultiplayerRVA, &lastMulti) || lastMulti != multi ||
            !PHReadMemory(header + PHBattleEntryHashRVA, lastHash, sizeof(lastHash)) ||
            std::memcmp(lastHash,state.identity.entryHash,sizeof(lastHash)) ||
            !PHReadPointer(game + 0x70, &lastImpl) || lastImpl != impl ||
            !PHReadPointer(header + PHBattleImplRVA, &lastActive) || lastActive != impl ||
            !PHReadPointer(header + PHBattleInputRVA, &lastInput) || lastInput != state.inputManager ||
            !PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
            !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2 ||
            !Decode(impl + PHBattlePhaseOffset,key1,key2,0x766cfc6du,lastPhase) || lastPhase != state.phase ||
            !Decode(impl + PHBattleWaveOffset,key1,key2,0x766cfc6du,lastWave) || lastWave != state.wave) return false;
        state.valid = true;
        if (state.boardTransition) {
            state.automation.window = false; state.automation.busy = true;
            state.automation.windowBlocks |= PHOrbSkillWindowResize;
        }
        if (reason) *reason = multiplayerMetadataOnly ? MultiplayerReason(state) : state.boardTransition ?
            @"等待原生改盤動畫完成；尚未讀取珠子與座標" : @"等待自己的操作階段";
        return true;
    }
    state.ordinary = source == 0 && state.minErase >= 3 && state.minErase <= 7 && state.freeMovement == 1 &&
        !audit && !dialog && !specialDrag && !moveBlocked && !intro;
    const unsigned width = state.board.width, height = state.board.height;
    if (!PHOrbDimensionsSupported(width, height)) {
        if (reason) *reason = @"本版支援 6×5、7×6 及 5×4 盤面";
        return true;
    }
    std::array<uint8_t, PoolStride * PoolCount> pool;
    std::array<uintptr_t, PoolCount> table, lastTable;
    if (reason) *reason = @"盤面物件池或索引表無法讀取";
    if (!PHReadMemory(impl + PHOrbPoolOffset, pool.data(), pool.size()) ||
        !PHReadMemory(impl + PHOrbTableOffset, table.data(), sizeof(table))) return false;
    const uintptr_t start = impl + PHOrbPoolOffset, end = start + pool.size();
    state.settled = true;
    for (unsigned row = 0; row < height; ++row) for (unsigned col = 0; col < width; ++col) {
        const unsigned i = row * width + col;
        const uintptr_t token = table[(height - 1 - row) * 16 + col];
        if (reason) *reason = @"盤面珠子指標不在物件池內或指標重複";
        if (token < start || token >= end || (token - start) % PoolStride) return false;
        for (unsigned previous = 0; previous < i; ++previous) if (state.board.tokens[previous] == token) return false;
        const uint8_t *block = pool.data() + token - start;
        const uint16_t motion = Load<uint16_t>(block + 18);
        state.board.tokens[i] = token; state.board.types[i] = block[0];
        state.board.flags[i] = Load<uint32_t>(block + 4); state.board.enhancement[i] = Load<uint32_t>(block + 8);
        state.board.blindTurns[i] = block[1] & 0x7fu;
        state.board.thornDamage[i] = block[12] & 0x7fu;
        state.board.x[i] = Load<int16_t>(block + 28); state.board.y[i] = Load<int16_t>(block + 30);
        state.settled &= motion == 0 && Load<int16_t>(block + 24) == state.board.x[i] && Load<int16_t>(block + 26) == state.board.y[i];
        if (motion != 0 && motion != 3 && motion != 4) state.ordinary = false;
        if (state.board.x[i] < 0 || state.board.x[i] > 4096 || state.board.y[i] < 0 || state.board.y[i] > 4096) {
            if (reason) *reason = @"珠子座標超出支援範圍"; return false;
        }
    }
    const int spacing = state.board.x[1] - state.board.x[0];
    if (reason) *reason = @"盤面格心或間距不一致，未採用此盤面";
    if (spacing < 20 || spacing > 160) return false;
    for (unsigned row = 0; row < height; ++row) for (unsigned col = 0; col < width; ++col) {
        unsigned i = row * width + col;
        if (state.board.x[i] != state.board.x[0] + int(col) * spacing ||
            state.board.y[i] != state.board.y[0] + int(row) * spacing) return false;
    }
    if (!ReadBoardEffects(state, impl, key1, key2, fixedX)) {
        state.ordinary = false; state.obstacles |= PHOrbObstacleBoardEffect;
    }
    uint32_t lastKey1 = 0, lastKey2 = 0; uintptr_t lastImpl = 0;
    if (reason) *reason = @"讀盤期間索引表、戰鬥物件或解碼金鑰已改變";
    if (!PHReadMemory(impl + PHOrbTableOffset, lastTable.data(), sizeof(lastTable)) || table != lastTable ||
        !PHReadPointer(game + 0x70, &lastImpl) || lastImpl != impl ||
        !PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2) return false;
    state.leadersValid = ReadLeaders(state, save, impl);
    state.valid = true;
    if (state.multiplayer.kind != PHOrbMultiplayerNone && !PHOrbMultiplayerWindow(&state)) {
        state.ordinary = false;
        if (reason) *reason = MultiplayerReason(state);
        return true;
    }
    if (!state.ordinary) { if (reason) *reason = @"盤面效果未通過校驗、特殊操作規則、彈窗或入場動畫尚未結束"; }
    else if (!PHOrbBoardValid(&state.board)) { if (reason) *reason = @"盤面含未支援的珠型、旗標或轉轉模式"; }
    else if (!state.settled && !state.dragged) { if (reason) *reason = @"等待盤面動畫停止"; }
    else if (!PHInputIsQuiet(&state.input)) { if (reason) *reason = @"請先放開手指"; }
    else if (reason) *reason = state.board.spinnerMask ? @"轉轉：避開變色格並檢查全部可能色序後求路" : @"可以讀盤及求路";
    return true;
}

#include "PHOrbSkills.inc"
#include "PHOrbSkillChoice.inc"
#include "PHOrbSkillDiagnostics.inc"

void ReadThornBudget(PHOrbSnapshot &state) {
    if (!state.valid || state.boardTransition || state.multiplayerDisabled || state.phase != 2) return;
    bool needed = false;
    for (unsigned i = 0; i < state.board.width * state.board.height; ++i)
        needed |= (state.board.flags[i] & PHOrbFlagThorn) && state.board.thornDamage[i];
    if (!needed) return;
    uint32_t key1 = 0, key2 = 0, hp = 0, shadow = 0, maximum = 0, pending = 0, thornPending = 0;
    uintptr_t dungeon = 0;
    const auto header = state.identity.header, impl = state.identity.impl;
    if (!PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
        !Decode(impl + PHBattleHPOffset, key1, key2, 0x766cfc6du, hp) ||
        !PHReadU32(impl + PHBattleHPShadowOffset, &shadow) || (shadow ^ hp) != 0x8085a6beu ||
        !PHReadPointer(impl + PHOrbSkillDungeonRulesOffset, &dungeon) ||
        !ReadMaximumHP(state, dungeon, key1, key2, maximum) || !hp || hp > maximum ||
        !PHReadU32(impl + PHOrbPendingDamageOffset, &pending) || pending > INT32_MAX ||
        !PHReadU32(impl + PHOrbPendingThornDamageOffset, &thornPending) || thornPending > pending) return;
    state.thorns = {hp, maximum, pending, true};
}

// sub_10006924C is a pure affine raw-to-frame transform. Invert its measured
// basis; never assume UIKit screen points equal the native 640-wide board.
bool RawPoint(uintptr_t header, float x, float y, float *raw) {
    uint8_t transform = 0;
    if (!PHReadU8(header + PHOrbTransformEnabledRVA, &transform) || transform > 1) return false;
    if (!transform) { raw[0] = x; raw[1] = y; return true; }
    auto convert = reinterpret_cast<uintptr_t (*)(float *, const float *)>(header + PHOrbInputTransformRVA);
    const float zero[2]{}, ex[2]{1, 0}, ey[2]{0, 1};
    float a[2]{}, b[2]{}, c[2]{};
    convert(a, zero); convert(b, ex); convert(c, ey);
    const float bx = b[0] - a[0], by = b[1] - a[1], cx = c[0] - a[0], cy = c[1] - a[1];
    const float det = bx * cy - by * cx;
    if (!std::isfinite(det) || std::fabs(std::fabs(det) - 1) > 0.001f) return false;
    raw[0] = ((x - a[0]) * cy - (y - a[1]) * cx) / det;
    raw[1] = (bx * (y - a[1]) - by * (x - a[0])) / det;
    float check[2]{}; convert(check, raw);
    return std::isfinite(raw[0]) && std::isfinite(raw[1]) && std::fabs(raw[0]) < 8192 && std::fabs(raw[1]) < 8192 &&
        std::fabs(check[0] - x) < 0.01 && std::fabs(check[1] - y) < 0.01;
}
}

@interface PHNativeOrbEngine : NSObject <PHOrbEngine>
- (BOOL)nativeCanUseGame:(uintptr_t)game member:(uintptr_t)member selector:(int)selector header:(uintptr_t)header;
- (BOOL)nativeCanChooseSkill:(uintptr_t)member header:(uintptr_t)header;
- (void)nativeStartGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header;
@end
@implementation PHNativeOrbEngine {
    Manager _manager;
    uintptr_t _header, _realManager, _verifiedHeader;
    bool _staged, _multiplayerDisabled, _sawMultiplayer, _multiplayerOnce;
    bool _multiplayerBegan, _multiplayerReleaseSent;
    bool _multiplayerEntryKnown;
    PHBattleIdentity _multiplayerEntry;
    PHOrbMultiplayerKind _multiplayerKind;
    uintptr_t _multiplayerOwnedOrb, _multiplayerStartOrb;
    NSUInteger _multiplayerStartCell;
    PHOrbSnapshot _multiplayerGesture, _multiplayerArmCandidate;
}
- (void)forgetMultiplayerGesture {
    _multiplayerOnce = _multiplayerBegan = _multiplayerReleaseSent = false;
    _multiplayerOwnedOrb = _multiplayerStartOrb = 0;
    _multiplayerStartCell = 0; _multiplayerGesture = {};
}
- (void)armMultiplayerOnce:(BOOL)armed {
    if (!armed) { _multiplayerOnce = false; return; }
    // Repeated taps cannot reset an outstanding began/hold/release lease.
    // Revocation preserves only its possible acknowledgement and release.
    if (!_staged && !_multiplayerDisabled && !_multiplayerBegan && !_multiplayerOnce &&
        _multiplayerArmCandidate.multiplayer.kind != PHOrbMultiplayerNone && PHOrbCanPreview(&_multiplayerArmCandidate)) {
        _multiplayerGesture = _multiplayerArmCandidate;
        _multiplayerOnce = true;
    }
}
- (BOOL)read:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!state || _staged) return NO;
    _multiplayerArmCandidate = {};
    bool entryConfirmed = false;
    BOOL read = Read(*state, frame, reason,
        _sawMultiplayer ? _multiplayerKind : PHOrbMultiplayerNone, _multiplayerDisabled,
        _multiplayerEntryKnown ? &_multiplayerEntry : nullptr, entryConfirmed);
    if (read && state->valid && !state->multiplayerDisabled && _verifiedHeader != frame.header) {
        if (!CheckCode(frame.header, reason)) { state->valid = false; read = NO; }
        else _verifiedHeader = frame.header;
    }
    if (read && state->valid && !state->multiplayerDisabled && state->multiplayer.kind != PHOrbMultiplayerNone) {
        PHOrbSnapshot confirmed{}; confirmed.identity = state->identity; confirmed.multiplayer.kind = state->multiplayer.kind;
        if (!ConfirmMultiplayerIdentity(*state) ||
            !ReadMultiplayerFields(confirmed) || !SameMultiplayerRead(state->multiplayer, confirmed.multiplayer)) {
            state->valid = false; read = NO;
            if (reason) *reason = @"讀盤期間多人席位、回合或連線狀態已改變";
        } else {
            state->multiplayer = confirmed.multiplayer;
            const bool window = PHOrbMultiplayerWindow(state);
            state->ordinary &= window;
            if (!window && reason) *reason = MultiplayerReason(*state);
        }
    }
    // Publish the new entry only after the entire read agrees. An interrupted
    // read must not retire an old latch or grant a new gesture candidate.
    if (read && entryConfirmed && !ConfirmOrbEntryIdentity(state->identity)) {
        state->valid = false; read = NO;
        if (reason) *reason = @"讀取期間關卡入場識別或場景已改變";
    }
    const bool exited = read && !state->present;
    const bool newEntry = read && entryConfirmed && _multiplayerEntryKnown &&
        !SameOrbBattleEntry(_multiplayerEntry, state->identity);
    if (exited || newEntry) {
        _multiplayerDisabled = _sawMultiplayer = false;
        _multiplayerKind = PHOrbMultiplayerNone;
        [self forgetMultiplayerGesture];
    }
    if (exited) { _multiplayerEntryKnown = false; _multiplayerEntry = {}; }
    else if (read && entryConfirmed) { _multiplayerEntryKnown = true; _multiplayerEntry = state->identity; }
    if (read && entryConfirmed && state->multiplayerDisabled) _multiplayerDisabled = true;
    if (read && entryConfirmed && state->present) {
        const auto kind = state->multiplayer.kind;
        if (kind != PHOrbMultiplayerNone) {
            if (_sawMultiplayer && kind != _multiplayerKind) _multiplayerDisabled = true;
            if (!_sawMultiplayer) { _sawMultiplayer = true; _multiplayerKind = kind; }
        } else if (_sawMultiplayer) _multiplayerDisabled = true;
    }
    state->multiplayerDisabled |= _multiplayerDisabled;
    if (!read) {
        // Preserve only the observed mode for unavailable-state UI. A read
        // failure before mode capture must not make a known multiplayer game
        // appear eligible for continuous input. This is never a valid board.
        if (_sawMultiplayer && state->multiplayer.kind == PHOrbMultiplayerNone)
            state->multiplayer.kind = _multiplayerKind;
        [self forgetMultiplayerGesture]; return NO;
    }
    if (_multiplayerBegan) {
        if (!state->valid || state->multiplayerDisabled || !PHOrbSameTurn(&_multiplayerGesture, state) ||
            !PHOrbMultiplayerWindow(state)) {
            // A different turn, disconnect or native resolve owns its own
            // cleanup. Never carry an old private gesture into that context.
            [self forgetMultiplayerGesture];
        } else if (_multiplayerOwnedOrb) {
            if (state->dragged != _multiplayerOwnedOrb) [self forgetMultiplayerGesture];
        } else if (state->dragged) {
            if (state->dragged == _multiplayerStartOrb && PHInputIsQuiet(&state->input) &&
                state->moves == _multiplayerGesture.moves &&
                PHOrbSamePlanBoard(&state->board, &_multiplayerGesture.board))
                _multiplayerOwnedOrb = state->dragged;
            else [self forgetMultiplayerGesture];
        } else if (!_multiplayerOnce || !PHInputIsQuiet(&state->input) ||
            state->moves != _multiplayerGesture.moves ||
            !PHOrbSamePlanBoard(&state->board, &_multiplayerGesture.board)) {
            [self forgetMultiplayerGesture];
        }
    }
    else if (_multiplayerOnce && (!PHOrbCanPreview(state) || !PHOrbSameTurn(&_multiplayerGesture, state) ||
        !PHOrbSameLeaders(&_multiplayerGesture, state) ||
        state->moves != _multiplayerGesture.moves || !PHOrbSamePlanBoard(&_multiplayerGesture.board, &state->board))) {
        [self forgetMultiplayerGesture];
    }
    if (_multiplayerDisabled) [self forgetMultiplayerGesture];
    if (PHOrbCanPreview(state)) _multiplayerArmCandidate = *state;
    // HP derivation for multiplayer thorn damage needs its own validation.
    // Unknown thorn budgets make the solver avoid contacts in this first batch.
    if (state->valid && !state->multiplayerDisabled && state->multiplayer.kind == PHOrbMultiplayerNone)
        ReadThornBudget(*state);
    if (_multiplayerDisabled && reason) *reason = @"本場多人模式或連線狀態已改變；離開關卡後才可重新操作";
    return YES;
}
- (BOOL)readAutomation:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (![self read:state frame:frame reason:reason]) return NO;
    if (!state->present || !state->valid || state->multiplayerDisabled) return YES;
    if (!PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer)) {
        if (reason) *reason = @"多人僅支援轉一次；連續轉珠、開場及技能規則仍限單人";
        return YES;
    }
    if (!ReadAutomationFields(*state)) {
        if (reason) *reason = @"技能／敵人資料未通過讀取校驗，已停止";
        return NO;
    }
    return YES;
}
- (BOOL)nativeCanUseGame:(uintptr_t)game member:(uintptr_t)member selector:(int)selector header:(uintptr_t)header {
    return (reinterpret_cast<int (*)(uintptr_t, uintptr_t, int)>(header + PHOrbCanUseSkillRVA)(game, member, selector) & 1) != 0;
}
- (BOOL)nativeCanChooseSkill:(uintptr_t)member header:(uintptr_t)header {
    return (reinterpret_cast<int (*)(uintptr_t)>(header + PHOrbCanChooseSkillRVA)(member) & 1) != 0;
}
- (BOOL)nativeSkillReadiness:(NativeSkillReadiness *)out member:(uintptr_t)member header:(uintptr_t)header {
    // These getters may update a native cooldown cache. Call only during a
    // guarded skill attempt (diagnosis or source/ID constraint), never polling.
    uint16_t assist = 0;
    if (!out || !PHReadU16(member + 1438, &assist)) return NO;
    out->baseSkill = reinterpret_cast<int32_t (*)(uintptr_t)>(header + PHOrbBaseSkillRVA)(member + 1248);
    out->assistSkill = int16_t(assist) > 0 ? reinterpret_cast<int32_t (*)(uintptr_t)>(header + PHOrbAssistSkillRVA)(member + 1248) : 0;
    out->baseCooldown = reinterpret_cast<int32_t (*)(uintptr_t, int)>(header + PHOrbBaseCooldownRVA)(member, 0);
    out->totalCooldown = reinterpret_cast<int32_t (*)(uintptr_t, int)>(header + PHOrbTotalCooldownRVA)(member, 0);
    return out->baseSkill >= 0 && out->baseSkill <= 500000 && out->assistSkill >= 0 && out->assistSkill <= 500000 &&
        out->baseCooldown >= 0 && out->baseCooldown <= UINT16_MAX &&
        out->totalCooldown >= out->baseCooldown && out->totalCooldown <= UINT16_MAX;
}
- (void)nativeStartGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(header + PHOrbStartSkillRVA)(game, member);
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    return [self useSkill:slot request:PHOrbSkillRequest{PHOrbSkillAutomatic, 0}
        expected:expected frame:frame reason:reason];
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot request:(PHOrbSkillRequest)request expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!NSThread.isMainThread || _staged || _multiplayerDisabled || !expected || slot >= 6 || !frame.ready || !frame.enabled)
        return PHOrbSkillRejected;
    if (request.source < PHOrbSkillAutomatic || request.source > PHOrbSkillAssist || request.skill >= 0x20000)
        return PHOrbSkillRejected;
    PHOrbSnapshot live{};
    if (![self readAutomation:&live frame:frame reason:reason] || !live.valid || live.multiplayerDisabled) return PHOrbSkillRejected;
    if (!live.automation.window && live.phase == 2 && live.settled && !live.dragged && PHInputIsQuiet(&live.input) &&
        PHBattleIsSinglePlayer(live.identity.entryMode, live.identity.multiplayer) &&
        PHOrbSameAutomationExceptWindow(expected, &live)) {
        if (reason) *reason = PHOrbSkillWindowDescription(&live.automation);
        return PHOrbSkillWindowClosed;
    }
    if (!PHBattleIsSinglePlayer(live.identity.entryMode, live.identity.multiplayer) ||
        !PHOrbSameAutomation(expected, &live) || live.phase != 2 || !live.settled || live.dragged ||
        !PHInputIsQuiet(&live.input)) {
        if (reason) *reason = @"施放前盤面、隊員或戰鬥狀態已改變，已停止";
        return PHOrbSkillRejected;
    }
    if (!CheckCode(frame.header, reason)) return PHOrbSkillRejected;
    const auto &member = live.automation.members[slot];
    if (!live.automation.window) {
        if (reason) *reason = PHOrbSkillWindowDescription(&live.automation);
        return PHOrbSkillWindowClosed;
    }
    if (live.automation.busy || !member.active || member.bound || member.state ||
        live.automation.skillUses >= 255) {
        if (reason) *reason = @"技能目前不可使用";
        return PHOrbSkillUnavailable;
    }
    const uintptr_t address = live.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    SkillSourceState source;
    if (!ReadSkillSource(live, source)) return PHOrbSkillRejected;
    PHOrbSnapshot checked = live;
    auto recheck = [&](NSString *message) {
        PHOrbSnapshot next{}; SkillSourceState current;
        if (![self readAutomation:&next frame:frame reason:reason] || !next.valid || next.multiplayerDisabled ||
            !next.automation.window || next.automation.busy || next.dragged || !next.settled ||
            !PHInputIsQuiet(&next.input) || !PHOrbSameAutomation(&checked, &next) ||
            !ReadSkillSource(next, current) || !(source == current)) {
            if (reason) *reason = message;
            return false;
        }
        checked = next;
        return true;
    };
    // Like the dialog, first refresh the native stages with selector -1.
    // Its refusal must not suppress a separately permitted body choice.
    BOOL available = [self nativeCanUseGame:live.identity.game member:address selector:-1 header:frame.header];
    if (!recheck(@"技能檢查後狀態已改變，未啟動技能")) return PHOrbSkillRejected;
    const bool constrained = request.source != PHOrbSkillAutomatic || request.skill;
    NativeSkillReadiness readiness{};
    BOOL known = NO;
    if (constrained || !available) {
        known = [self nativeSkillReadiness:&readiness member:address header:frame.header];
        if (!recheck(@"核對技能與冷卻後狀態已改變，未施放")) return PHOrbSkillRejected;
        if (constrained && !known) {
            if (reason) *reason = @"無法核對指定的本體／武裝技能，未施放";
            return PHOrbSkillRejected;
        }
    }
    bool chosenAssist = known && !(source.flags & 8u) && int16_t(member.assistCatalogID) > 0 &&
        readiness.totalCooldown > 0 && member.charge >= readiness.totalCooldown;
    bool chooseBody = false;
    SkillChoiceState choice;
    if (request.source == PHOrbSkillBody && chosenAssist) {
        if (!ReadSkillChoice(checked, unsigned(slot), choice)) {
            if (reason) *reason = @"無法確認本體／武裝二選一資格，未施放";
            return PHOrbSkillRejected;
        }
        const BOOL allowed = [self nativeCanChooseSkill:address header:frame.header];
        SkillChoiceState current;
        if (!recheck(@"核對二選一資格後狀態已改變，未施放") ||
            !ReadSkillChoice(checked, unsigned(slot), current) || !(choice == current)) {
            if (reason) *reason = @"本體／武裝二選一資料已改變，未施放";
            return PHOrbSkillRejected;
        }
        if (!allowed || choice.family[0] != choice.family[1]) {
            if (reason) *reason = choice.family[0] != choice.family[1] ?
                @"武裝已蓄滿且與本體不同進化鏈，只能使用武裝；本規則指定本體，已略過" :
                @"遊戲未提供本體／武裝二選一；本規則指定本體，已略過";
            return PHOrbSkillUnavailable;
        }
        chooseBody = true;
        chosenAssist = false;
    }
    const uint32_t chosenSkill = chosenAssist ? readiness.assistSkill : readiness.baseSkill;
    if (constrained && (!chosenSkill || chosenSkill >= 0x20000 ||
        (request.source == PHOrbSkillAssist && !chosenAssist) || (request.skill && request.skill != chosenSkill))) {
        if (reason) *reason = [NSString stringWithFormat:@"指定技能不符：目前為%@技能 %u；未施放其他技能",
            chosenAssist ? @"武裝" : @"本體", chosenSkill];
        return PHOrbSkillUnavailable;
    }
    if (chooseBody) {
        // Only the native same-family dialog offers this choice. Selector 0
        // still checks body cooldown, orb requirements and all native effects.
        available = [self nativeCanUseGame:live.identity.game member:address selector:0 header:frame.header];
        if (!recheck(@"選擇本體後狀態已改變，未啟動技能")) return PHOrbSkillRejected;
        NativeSkillReadiness current{}; SkillChoiceState currentChoice;
        const BOOL currentKnown = [self nativeSkillReadiness:&current member:address header:frame.header];
        if (!recheck(@"確認本體技能後狀態已改變，未啟動技能") || !currentKnown ||
            current.baseSkill != readiness.baseSkill || current.assistSkill != readiness.assistSkill ||
            current.baseCooldown != readiness.baseCooldown || current.totalCooldown != readiness.totalCooldown ||
            !ReadSkillChoice(checked, unsigned(slot), currentChoice) || !(choice == currentChoice)) {
            if (reason) *reason = @"二選一的當前技能或進化鏈已改變，未啟動技能";
            return PHOrbSkillRejected;
        }
    }
    if (!available) {
        const auto &observed = checked.automation.members[slot];
        NSString *cause = !known ? @"遊戲判定技能不可用，冷卻明細未取得" :
            !readiness.baseSkill || !readiness.totalCooldown ? @"未取得可使用的本體技能" :
            observed.charge < readiness.baseCooldown ? @"冷卻尚未完成" : @"冷卻已足，遊戲仍判定技能不可用";
        if (known && readiness.baseSkill > 0 && readiness.totalCooldown > 0 && observed.charge >= readiness.baseCooldown) {
            NSString *condition = SkillOrbCondition(checked, unsigned(slot), readiness,
                PHOrbSameBoard(&live.board, &checked.board), chooseBody ? 0 : -1);
            if (condition) cause = condition;
        }
        NSString *cooldown = known ? [NSString stringWithFormat:@"；本體技能 %d／CD %d，武裝技能 %d／合計 CD %d",
            readiness.baseSkill, readiness.baseCooldown, readiness.assistSkill, readiness.totalCooldown] : @"";
        if (reason) *reason = [NSString stringWithFormat:@"%@（充能 %u%@；階段 %u／%u，快取 %u／%u）", cause,
            observed.charge, cooldown, observed.skillStage[0], observed.skillStage[1],
            observed.cachedSkillStage[0], observed.cachedSkillStage[1]];
        return PHOrbSkillUnavailable;
    }
    // can-use writes this source byte itself. Confirm it instead of forcing
    // it; effects use it independently of the retained skill ID.
    uint8_t selectedSource = 0;
    if (!PHReadU8(live.identity.impl + PHOrbSkillSelectedSourceOffset, &selectedSource) || selectedSource > 1 ||
        (constrained && selectedSource != unsigned(chosenAssist))) {
        if (reason) *reason = @"遊戲的本體／武裝選擇未能確認，未啟動技能";
        return PHOrbSkillRejected;
    }
    const uint32_t selection = chooseBody ? chosenSkill : UINT32_MAX;
    if (member.selection != selection) {
        // Match the idle tap's reset, or the same-family body choice at
        // 0x1005CD058: store the native current body ID, never the config ID.
        // A positive ID preserves can-use(0)'s source through later effects.
        uint32_t written = 0;
        if (!PHWriteData(address + PHOrbSkillOverrideOffset, &selection, sizeof(selection)) ||
            !PHReadU32(address + PHOrbSkillOverrideOffset, &written) || written != selection) {
            if (reason) *reason = @"技能選擇未能設定或確認，未啟動技能";
            return PHOrbSkillRejected;
        }
        checked.automation.members[slot].selection = selection;
        if (!recheck(@"準備技能選擇後狀態已改變，未啟動技能")) return PHOrbSkillRejected;
    }
    uint8_t finalSource = 0;
    SkillSourceState finalRules;
    SkillChoiceState finalChoice;
    if (!ReadSkillSource(checked, finalRules) || !(source == finalRules) ||
        !PHReadU8(live.identity.impl + PHOrbSkillSelectedSourceOffset, &finalSource) || finalSource != selectedSource ||
        (chooseBody && (!ReadSkillChoice(checked, unsigned(slot), finalChoice) || !(choice == finalChoice)))) {
        if (reason) *reason = @"啟動前本體／武裝選擇已改變，未啟動技能";
        return PHOrbSkillRejected;
    }
    [self nativeStartGame:live.identity.game member:address header:frame.header];
    PHOrbSnapshot accepted{};
    if (![self readAutomation:&accepted frame:frame reason:reason] || accepted.multiplayerDisabled ||
        !PHBattleSameIdentity(&live.identity, &accepted.identity) || accepted.phase != 22 ||
        accepted.automation.members[slot].state != 2) {
        if (reason) *reason = @"遊戲未確認技能啟動，已停止；請檢查遊戲畫面";
        return PHOrbSkillRejected;
    }
    return PHOrbSkillStarted;
}
- (BOOL)stageCell:(NSUInteger)cell down:(BOOL)down beginning:(BOOL)beginning
    expected:(const PHOrbSnapshot *)expected frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!NSThread.isMainThread || _staged || _multiplayerDisabled || !expected || (beginning && !down) ||
        cell >= PHOrbMaxCells || !frame.ready || (down && !frame.enabled) ||
        (beginning && !CheckCode(frame.header, reason))) return NO;
    PHOrbSnapshot live{}; NSString *readReason = nil;
    if (![self read:&live frame:frame reason:&readReason] || !live.valid || live.multiplayerDisabled) {
        if (reason) *reason = readReason ?: @"輸入前狀態無法確認";
        return NO;
    }
    const bool multiplayer = live.multiplayer.kind != PHOrbMultiplayerNone;
    if (multiplayer) {
        const bool sameGesture = _multiplayerOwnedOrb && live.dragged == _multiplayerOwnedOrb &&
            PHOrbSameTurn(&_multiplayerGesture, &live);
        const bool deferredStart = _multiplayerBegan && !_multiplayerOwnedOrb && !live.dragged &&
            cell == _multiplayerStartCell && PHOrbSameTurn(&_multiplayerGesture, &live) &&
            live.moves == _multiplayerGesture.moves && PHOrbSamePlanBoard(&_multiplayerGesture.board, &live.board);
        if (!PHOrbMultiplayerWindow(&live) ||
            (down && (!_multiplayerOnce || _multiplayerReleaseSent)) ||
            (down && !PHOrbSameLeaders(&_multiplayerGesture, &live)) ||
            (!down && (!sameGesture || live.board.tokens[cell] != _multiplayerOwnedOrb)) ||
            (down && beginning && (_multiplayerBegan || live.dragged ||
                !PHOrbSameTurn(&_multiplayerGesture, &live) || live.moves != _multiplayerGesture.moves ||
                !PHOrbSamePlanBoard(&_multiplayerGesture.board, &live.board))) ||
            (down && !beginning && !sameGesture && !deferredStart)) {
            if (reason) *reason = @"多人輸入僅限本次轉一次及同一個本機操作窗口";
            return NO;
        }
    }
    if (!PHOrbSameTurn(expected, &live) || live.boardTransition || live.phase != 2 || live.moves != expected->moves ||
        cell >= live.board.width * live.board.height || !PHOrbSamePlanBoard(&live.board, &expected->board) ||
        (down && (PHOrbBlockedCells(&live.board) & (UINT64_C(1) << cell))) ||
        (down && !live.dragged && !PHOrbCanStartAt(&live.board, unsigned(cell))) ||
        (down && !PHOrbSameLeaders(expected, &live)) ||
        (down && (!live.ordinary || !PHOrbBoardValid(&live.board) || !PHInputIsQuiet(&live.input))) ||
        (beginning && (live.dragged || !live.settled)) || (!beginning && live.dragged != expected->dragged)) {
        if (reason) *reason = @"輸入前盤面或操作條件已改變";
        return NO;
    }
    if (down && !PHOrbCanEnterThorn(&live, unsigned(cell))) {
        if (reason) *reason = @"目前 HP 或荊棘傷害無法確認，或這一步會超過可承受傷害";
        return NO;
    }
    if (down && live.dragged) {
        unsigned from = 0, size = live.board.width * live.board.height;
        for (; from < size; ++from) if (live.board.tokens[from] == live.dragged) break;
        if (from == size || (from != cell && !PHOrbMoveAdjacent(from, unsigned(cell),
            live.board.width, live.board.height, live.freeMovement == 1))) {
            if (reason) *reason = @"輸入目標不是持珠的相鄰格，未交換"; return NO;
        }
    }
    uintptr_t real = 0;
    if (!PHReadPointer(frame.header + PHBattleInputRVA, &real) || real != live.inputManager || (real & 7u) ||
        !PHMemoryHasProtection(real, PHOrbInputBytes, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) return NO;
    float raw[2];
    if (!RawPoint(frame.header, live.board.x[cell], live.board.y[cell], raw)) {
        if (reason) *reason = @"觸控座標轉換未通過校驗"; return NO;
    }
    // Coordinate conversion is a native call. Recheck multiplayer ownership
    // after it and immediately before leasing the global input pointer.
    if (multiplayer) {
        PHOrbSnapshot final{};
        if (![self read:&final frame:frame reason:reason] || !final.valid || final.multiplayerDisabled ||
            !PHOrbSameTurn(&live, &final) || final.phase != 2 || final.dragged != live.dragged ||
            final.moves != live.moves || !PHOrbSamePlanBoard(&live.board, &final.board) ||
            !PHOrbMultiplayerWindow(&final) ||
            (down && !PHOrbSameLeaders(&live, &final)) ||
            (down && (!_multiplayerOnce || !final.ordinary || !PHInputIsQuiet(&final.input))) ||
            (!down && (!_multiplayerOwnedOrb || final.dragged != _multiplayerOwnedOrb))) return NO;
        live = final;
    }
    if (beginning) {
        _manager.fill(0);
        // Only immutable timing settings are copied. No real touch is copied.
        if (!PHReadMemory(real + 2664, _manager.data() + 2664, 8)) return NO;
        Store<float>(_manager.data() + 2648, -1); Store<float>(_manager.data() + 2652, -1);
        Store<double>(_manager.data() + 232, CFAbsoluteTimeGetCurrent());
        Store<double>(_manager.data() + 240, CFAbsoluteTimeGetCurrent() + 3600);
        Store<float>(_manager.data() + 224, raw[0]); Store<float>(_manager.data() + 228, raw[1]);
        Store<float>(_manager.data() + 36, live.board.x[cell]); Store<float>(_manager.data() + 40, live.board.y[cell]);
        Store<float>(_manager.data() + 176, live.board.x[cell]); Store<float>(_manager.data() + 180, live.board.y[cell]);
        Store<uint32_t>(_manager.data() + 208, 4);
    }
    Store<float>(_manager.data() + 4, raw[0]); Store<float>(_manager.data() + 8, raw[1]);
    _manager[213] = beginning; _manager[214] = down; _manager[215] = !down; _manager[216] = 0;
    _manager[2640] = down ? 1 : 0;
    // Keep the frame bank between native frames so held/release tracking is
    // performed by the game's own update. No UIKit object is fabricated.
    _header = frame.header; _realManager = real;
    uintptr_t privateManager = reinterpret_cast<uintptr_t>(_manager.data());
    if (!PHWriteData(frame.header + PHBattleInputRVA, &privateManager, sizeof(privateManager))) return NO;
    _staged = true;
    if (multiplayer && beginning) {
        _multiplayerGesture = live; _multiplayerStartCell = cell;
        _multiplayerStartOrb = live.board.tokens[cell]; _multiplayerBegan = true;
    }
    if (multiplayer && !down) {
        _multiplayerOnce = false; _multiplayerReleaseSent = true;
        // Retain ownership until read observes release or a context change,
        // so a refused release can be retried without permitting another hold.
    }
    return YES;
}
- (BOOL)finishInput:(NSString **)reason {
    if (!_staged) return YES;
    uintptr_t current = 0;
    if (!PHReadPointer(_header + PHBattleInputRVA, &current)) {
        if (reason) *reason = @"輸入指標無法讀取；停止自動操作";
        return NO;
    }
    if (current != reinterpret_cast<uintptr_t>(_manager.data())) {
        _staged = false;
        if (reason) *reason = @"遊戲已更換輸入管理器；停止自動操作";
        return NO; // Never overwrite a manager created by the game.
    }
    if (!PHWriteData(_header + PHBattleInputRVA, &_realManager, sizeof(_realManager))) {
        // Retain the allocation and lease; subsequent frames retry restoration.
        // If the game runs before recovery it sees a release, never a stuck hold.
        _manager[213] = _manager[214] = 0; _manager[215] = 1; _manager[2640] = 0;
        if (reason) *reason = @"無法恢復原本輸入；已停止並等待恢復";
        return NO;
    }
    _staged = false;
    return YES;
}
@end

id<PHOrbEngine> PHCreateOrbEngine(void) { return [PHNativeOrbEngine new]; }
