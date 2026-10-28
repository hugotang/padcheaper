#import "PHOrbSession.h"
#import "PHInput.h"
#import "PHTarget.h"
#import <CoreFoundation/CoreFoundation.h>
#include "PHTargetGenerated.h"
#include "PHOrbGenerated.h"
#include <array>
#include <cmath>
#include <cstring>

namespace {
constexpr size_t PoolStride = 172, PoolCount = 256;
using Manager = std::array<uint8_t, PHOrbInputBytes>;
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

// Optional read-only context. A missing catalog must not disable ordinary
// combo/manual previews; leader mode separately requires both current IDs.
bool ReadLeaders(PHOrbSnapshot &state, uintptr_t save, uintptr_t impl) {
    uint32_t count = 0; uintptr_t catalog = 0;
    if (!PHReadU32(save + PHBattleCatalogCountOffset, &count) || !count || count > 65536 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &catalog) || !catalog ||
        catalog > UINTPTR_MAX - size_t(count) * PHBattleCatalogStride) return false;
    uint16_t numbers[2]{};
    for (unsigned i = 0; i < 2; ++i) {
        const uintptr_t member = impl + PHBattleMemberOffset + (i ? 5 : 0) * PHBattleMemberStride;
        uint8_t active = 0; uint32_t id = 0, record = 0, lastID = 0; uintptr_t parameter = 0;
        if (!PHReadU8(member, &active) || !(active & 1u) ||
            !PHReadU32(member + PHBattleCatalogIDOffset, &id) || !id || id >= count ||
            !PHBattleNumberForCatalogID(id, &numbers[i]) ||
            !PHReadPointer(member + PHBattleCardParamOffset, &parameter) ||
            parameter != catalog + id * PHBattleCatalogStride || !PHReadU32(parameter, &record) || record != id ||
            !PHReadU32(member + PHBattleCatalogIDOffset, &lastID) || lastID != id) return false;
    }
    state.leaderNumbers[0] = numbers[0]; state.leaderNumbers[1] = numbers[1];
    return true;
}

bool Read(PHOrbSnapshot &state, PHBattleFrame frame, NSString **reason) {
    state = {};
    if (!NSThread.isMainThread || !frame.ready || !frame.header || !frame.player || frame.header > UINTPTR_MAX - 0x3000000u) {
        if (reason) *reason = @"等待已校驗的遊戲與登入";
        return false;
    }
    const uintptr_t header = frame.header, save = header + PHSaveRVA, game = header + PHGameMainRVA;
    uint8_t scene[2]; uint32_t player = 0;
    if (!PHReadMemory(save, scene, sizeof(scene)) || !PHReadU32(save + PHPlayerIDOffset, &player) || player != frame.player) return false;
    if (scene[0] != 1) { if (reason) *reason = @"進入單人關卡後，輪到操作時讀盤"; return true; }
    if (scene[1] != 1) { if (reason) *reason = @"場景切換中"; return false; }
    uint16_t mode = 0, source = 0, ids[2]{};
    uint32_t multi = 0;
    if (!PHReadU16(header + PHBattleModeRVA, &mode) || !PHReadU32(header + PHBattleMultiplayerRVA, &multi) ||
        !PHReadU16(save + PHBattleSourceModeOffset, &source)) return false;
    state.present = true;
    state.identity = {header, game, 0, frame.player, frame.epoch, 0, 0, {}, int16_t(mode), source, multi};
    if (!PHBattleIsSinglePlayer(int16_t(mode), multi)) {
        state.multiplayerDisabled = true;
        if (reason) *reason = @"多人模式不支援自動轉珠";
        return true;
    }
    uintptr_t object = 0, impl = 0, active = 0;
    if (!PHReadPointer(header + PHSceneTableRVA + 24, &object) || object != game ||
        !PHReadPointer(game + 0x70, &impl) || !impl || (impl & 7u) || impl > UINTPTR_MAX - 0xc4000 ||
        !PHReadPointer(header + PHBattleImplRVA, &active) || active != impl ||
        !PHMemoryHasProtection(impl + 0x92000, 0x2e000, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) return false;
    state.identity.impl = impl;
    uint32_t key1 = 0, key2 = 0, fixedX = 0, audit = 0;
    uint8_t shape[8]{};
    uintptr_t dialog = 0;
    uint16_t specialDrag = 0;
    uint8_t moveBlocked = 0, intro = 0;
    if (!PHReadMemory(save + PHBattleDungeonOffset, ids, sizeof(ids)) || !ids[0] || !ids[1] ||
        !PHReadMemory(header + PHBattleEntryHashRVA, state.identity.entryHash, sizeof(state.identity.entryHash)) ||
        !PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
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
    state.identity.dungeon = ids[0]; state.identity.floor = ids[1];
    bool hashPresent = false;
    for (uint8_t byte : state.identity.entryHash) hashPresent |= byte != 0;
    if (!hashPresent) return false;
    state.minErase = shape[4]; state.freeMovement = shape[5];
    state.board.width = shape[0]; state.board.height = shape[1];
    // Skills may remove unmatchable/locked/special orbs. Their window is
    // separate from ordinary drag eligibility, never a relaxation of it.
    state.automation.window = source == 0 && !audit && !dialog && !specialDrag && !moveBlocked && !intro;
    // Release may enter the resolve phase and start removing orb objects in
    // this same native frame. Identity + drag metadata is sufficient to
    // acknowledge release; a resolving board must never be used for a route.
    if (state.phase != 2) {
        state.valid = true;
        if (reason) *reason = @"等待自己的操作階段";
        return true;
    }
    state.ordinary = source == 0 && state.minErase == 3 && state.freeMovement == 1 &&
        int8_t(fixedX) == -1 && !audit && !dialog && !specialDrag && !moveBlocked && !intro;
    // Active board effects examined by the native player/match routines.
    // In particular +739268 gates the unmatchable-color mask at +739272.
    for (uintptr_t offset : {uintptr_t(739268), uintptr_t(739360), uintptr_t(738980), uintptr_t(739000)}) {
        uint16_t effect = 0;
        if (!PHReadU16(impl + offset, &effect)) return false;
        if (effect & 0x3ffu) state.ordinary = false;
    }
    uint16_t connectedA = 0, connectedB = 0;
    if (!PHReadU16(impl + 746068, &connectedA) || !PHReadU16(impl + 746076, &connectedB)) return false;
    if (connectedA || connectedB) state.ordinary = false;
    const unsigned width = state.board.width, height = state.board.height;
    if (!PHOrbDimensionsSupported(width, height)) {
        if (reason) *reason = @"本版支援 6×5、7×6 及 5×4 盤面";
        return true;
    }
    std::array<uint8_t, PoolStride * PoolCount> pool;
    std::array<uintptr_t, PoolCount> table, lastTable;
    if (!PHReadMemory(impl + PHOrbPoolOffset, pool.data(), pool.size()) ||
        !PHReadMemory(impl + PHOrbTableOffset, table.data(), sizeof(table))) return false;
    const uintptr_t start = impl + PHOrbPoolOffset, end = start + pool.size();
    state.settled = true;
    for (unsigned row = 0; row < height; ++row) for (unsigned col = 0; col < width; ++col) {
        const unsigned i = row * width + col;
        const uintptr_t token = table[(height - 1 - row) * 16 + col];
        if (token < start || token >= end || (token - start) % PoolStride) return false;
        for (unsigned previous = 0; previous < i; ++previous) if (state.board.tokens[previous] == token) return false;
        const uint8_t *block = pool.data() + token - start;
        const uint16_t motion = Load<uint16_t>(block + 18);
        state.board.tokens[i] = token; state.board.types[i] = block[0];
        state.board.flags[i] = Load<uint32_t>(block + 4); state.board.enhancement[i] = Load<uint32_t>(block + 8);
        state.board.x[i] = Load<int16_t>(block + 28); state.board.y[i] = Load<int16_t>(block + 30);
        state.settled &= motion == 0 && Load<int16_t>(block + 24) == state.board.x[i] && Load<int16_t>(block + 26) == state.board.y[i];
        if (motion != 0 && motion != 3 && motion != 4) state.ordinary = false;
        if (state.board.x[i] < 0 || state.board.x[i] > 4096 || state.board.y[i] < 0 || state.board.y[i] > 4096) return false;
    }
    const int spacing = state.board.x[1] - state.board.x[0];
    if (spacing < 20 || spacing > 160) return false;
    for (unsigned row = 0; row < height; ++row) for (unsigned col = 0; col < width; ++col) {
        unsigned i = row * width + col;
        if (state.board.x[i] != state.board.x[0] + int(col) * spacing ||
            state.board.y[i] != state.board.y[0] + int(row) * spacing) return false;
    }
    uint32_t lastKey1 = 0, lastKey2 = 0; uintptr_t lastImpl = 0;
    if (!PHReadMemory(impl + PHOrbTableOffset, lastTable.data(), sizeof(lastTable)) || table != lastTable ||
        !PHReadPointer(game + 0x70, &lastImpl) || lastImpl != impl ||
        !PHReadU32(header + PHBattleKey1RVA, &lastKey1) || lastKey1 != key1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || lastKey2 != key2) return false;
    state.leadersValid = ReadLeaders(state, save, impl);
    state.valid = true;
    if (!state.ordinary) { if (reason) *reason = @"特殊規則、固定起點、彈窗或入場動畫尚未結束"; }
    else if (!PHOrbBoardValid(&state.board)) { if (reason) *reason = @"盤面含未支援的珠型或效果；本版限六種基本珠與強化珠"; }
    else if (!state.settled && !state.dragged) { if (reason) *reason = @"等待盤面動畫停止"; }
    else if (!PHInputIsQuiet(&state.input)) { if (reason) *reason = @"請先放開手指"; }
    else if (reason) *reason = @"可以讀盤及求路";
    return true;
}

#include "PHOrbSkills.inc"

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
- (BOOL)nativeCanUseGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header;
- (void)nativeStartGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header;
@end
@implementation PHNativeOrbEngine {
    Manager _manager;
    uintptr_t _header, _realManager, _verifiedHeader;
    bool _staged, _multiplayerDisabled;
}
- (BOOL)read:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!state || _staged) return NO;
    if (!Read(*state, frame, reason)) return NO;
    if (!state->present) _multiplayerDisabled = false;
    if (state->multiplayerDisabled) _multiplayerDisabled = true;
    state->multiplayerDisabled |= _multiplayerDisabled;
    if (state->valid && !state->multiplayerDisabled && _verifiedHeader != frame.header) {
        if (!CheckCode(frame.header, reason)) { state->valid = false; return NO; }
        _verifiedHeader = frame.header;
    }
    if (_multiplayerDisabled && reason) *reason = @"本場曾進入多人；離開關卡後才可讀盤";
    return YES;
}
- (BOOL)readAutomation:(PHOrbSnapshot *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (![self read:state frame:frame reason:reason]) return NO;
    if (!state->present || !state->valid || state->multiplayerDisabled) return YES;
    if (!ReadAutomationFields(*state)) {
        if (reason) *reason = @"技能／敵人資料未通過讀取校驗，已停止";
        return NO;
    }
    return YES;
}
- (BOOL)nativeCanUseGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    return (reinterpret_cast<int (*)(uintptr_t, uintptr_t, int)>(header + PHOrbCanUseSkillRVA)(game, member, -1) & 1) != 0;
}
- (void)nativeStartGame:(uintptr_t)game member:(uintptr_t)member header:(uintptr_t)header {
    reinterpret_cast<void (*)(uintptr_t, uintptr_t)>(header + PHOrbStartSkillRVA)(game, member);
}
- (PHOrbSkillResult)useSkill:(NSUInteger)slot expected:(const PHOrbSnapshot *)expected
    frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!NSThread.isMainThread || _staged || _multiplayerDisabled || !expected || slot >= 6 || !frame.ready || !frame.enabled)
        return PHOrbSkillRejected;
    PHOrbSnapshot live{};
    if (![self readAutomation:&live frame:frame reason:reason] || !live.valid || live.multiplayerDisabled) return PHOrbSkillRejected;
    if (!PHBattleIsSinglePlayer(live.identity.entryMode, live.identity.multiplayer) ||
        !PHOrbSameAutomation(expected, &live) || live.phase != 2 || !live.settled || live.dragged ||
        !PHInputIsQuiet(&live.input)) {
        if (reason) *reason = @"施放前盤面、隊員或戰鬥狀態已改變，已停止";
        return PHOrbSkillRejected;
    }
    if (!CheckCode(frame.header, reason)) return PHOrbSkillRejected;
    const auto &member = live.automation.members[slot];
    if (member.overridden) {
        if (reason) *reason = @"此位置保留了手動技能選項，未自動改動選擇";
        return PHOrbSkillUnavailable;
    }
    if (!live.automation.window || live.automation.busy || !member.active || member.bound || member.state ||
        live.automation.skillUses >= 255) {
        if (reason) *reason = @"技能目前不可使用";
        return PHOrbSkillUnavailable;
    }
    const uintptr_t address = live.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    // Native can-use updates skill-stage and assist selection caches. It is
    // deliberately called only here, after every read-only gate, never while
    // polling. Selector -1 preserves the game's base/charged-assist choice.
    BOOL available = [self nativeCanUseGame:live.identity.game member:address header:frame.header];
    PHOrbSnapshot checked{};
    if (![self readAutomation:&checked frame:frame reason:reason] || checked.multiplayerDisabled) return PHOrbSkillRejected;
    if (!checked.automation.window || checked.automation.busy || checked.dragged || !checked.settled ||
        !PHInputIsQuiet(&checked.input) || !PHOrbSameAutomation(&live, &checked)) {
        if (reason) *reason = @"技能檢查後狀態已改變，未啟動技能";
        return PHOrbSkillRejected;
    }
    if (!available) { if (reason) *reason = @"冷卻尚未完成或遊戲判定技能無效"; return PHOrbSkillUnavailable; }
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
    if (_staged || _multiplayerDisabled || !expected || !frame.ready || (down && !frame.enabled) ||
        (beginning && !CheckCode(frame.header, reason))) return NO;
    PHOrbSnapshot live{}; NSString *readReason = nil;
    if (![self read:&live frame:frame reason:&readReason] || !live.valid || live.multiplayerDisabled) {
        if (reason) *reason = readReason ?: @"輸入前狀態無法確認";
        return NO;
    }
    if (!PHOrbSameTurn(expected, &live) || live.phase != 2 || live.moves != expected->moves ||
        cell >= live.board.width * live.board.height || !PHOrbSameBoard(&live.board, &expected->board) ||
        (down && !PHOrbSameLeaders(expected, &live)) ||
        (down && (!live.ordinary || !PHOrbBoardValid(&live.board) || !PHInputIsQuiet(&live.input))) ||
        (beginning && (live.dragged || !live.settled)) || (!beginning && live.dragged != expected->dragged)) {
        if (reason) *reason = @"輸入前盤面或操作條件已改變";
        return NO;
    }
    uintptr_t real = 0;
    if (!PHReadPointer(frame.header + PHBattleInputRVA, &real) || real != live.inputManager || (real & 7u) ||
        !PHMemoryHasProtection(real, PHOrbInputBytes, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) return NO;
    float raw[2];
    if (!RawPoint(frame.header, live.board.x[cell], live.board.y[cell], raw)) {
        if (reason) *reason = @"觸控座標轉換未通過校驗"; return NO;
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
