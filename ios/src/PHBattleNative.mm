#import "PHBattleSession.h"
#import "PHMemory.h"
#import "PHTarget.h"
#include "PHTargetGenerated.h"
#include "PHBattleGenerated.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {
struct NativeCalls {
    uint64_t (*set16)(uintptr_t, uint64_t);
    uintptr_t (*rebuildMember)(uintptr_t, uintptr_t, uint64_t);
    uint64_t (*refreshTeam)(uintptr_t, uint64_t);
    uint64_t (*digest)(uintptr_t);
    uint64_t (*maxCharge)(uintptr_t, uint64_t);
    float (*radius)(void);
};

static NativeCalls Calls(uintptr_t header) {
    return {
        reinterpret_cast<uint64_t (*)(uintptr_t, uint64_t)>(header + PHBattleSet16RVA),
        reinterpret_cast<uintptr_t (*)(uintptr_t, uintptr_t, uint64_t)>(header + PHBattleRebuildMemberRVA),
        reinterpret_cast<uint64_t (*)(uintptr_t, uint64_t)>(header + PHBattleRefreshTeamRVA),
        reinterpret_cast<uint64_t (*)(uintptr_t)>(header + PHBattleDigestRVA),
        reinterpret_cast<uint64_t (*)(uintptr_t, uint64_t)>(header + PHBattleMaxChargeRVA),
        reinterpret_cast<float (*)(void)>(header + PHBattleRadiusRVA)
    };
}

static BOOL CheckCode(uintptr_t header, NSString **reason) {
    if (!PHValidateGuest(reinterpret_cast<const mach_header_64 *>(header), reason)) return NO;
    for (size_t i = 0; i < PHBattleTextGuardCount; ++i) {
        const PHBattleTextGuard &guard = PHBattleTextGuards[i];
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) ||
            std::memcmp(bytes, guard.bytes, sizeof(bytes))) {
            if (reason) *reason = [NSString stringWithFormat:@"戰鬥原生函式校驗第 %zu 項不符", i + 1];
            return NO;
        }
    }
    return YES;
}

static BOOL DecodeAt(uintptr_t address, uint32_t key1, uint32_t key2, uint32_t salt, uint32_t *value) {
    uint32_t raw[4];
    return PHReadMemory(address, raw, sizeof(raw)) && PHBattleDecodeProtected(raw, key1, key2, salt, value);
}

static BOOL Decode16(const uint8_t *bytes, uintptr_t offset, uint32_t key1, uint32_t key2, uint16_t *value) {
    uint32_t raw[4], decoded;
    std::memcpy(raw, bytes + offset, sizeof(raw));
    if (!PHBattleDecodeProtected(raw, key1, key2, 0x6a875a8bu, &decoded) || decoded > UINT16_MAX) return NO;
    *value = static_cast<uint16_t>(decoded);
    return YES;
}

static BOOL DecodeByte(const uint8_t *bytes, uintptr_t offset, uint32_t key1, uint32_t key2, int8_t *value) {
    uint32_t raw[4], decoded;
    std::memcpy(raw, bytes + offset, sizeof(raw));
    if (!PHBattleDecodeProtected(raw, key1, key2, 0x6a875a8bu, &decoded)) return NO;
    // sub_100570E50 stores an unsigned byte; sub_100570DD8 casts
    // the unanimous result back to signed char (255 means -1).
    if (decoded > UINT8_MAX) return NO;
    *value = static_cast<int8_t>(static_cast<uint8_t>(decoded));
    return YES;
}

static BOOL CatalogValid(uint32_t target, const PHBattleState *state) {
    if (!target || target >= state->catalogCount || target > UINT16_MAX || (target >= 9900 && target <= 9999)) return NO;
    uint32_t recordID = 0;
    return PHReadU32(state->catalog + target * PHBattleCatalogStride, &recordID) && recordID == target;
}

static BOOL CanReadNative(const PHBattleState *state) {
    return state->present && state->protectedValuesValid && state->mode == 0 &&
           state->sourceMode == 0 && state->multiplayer == 0 && state->phase == 2 && state->inputQuiet;
}

static BOOL ReadLive(PHBattleState *state, PHBattleFrame frame, NSString **reason) {
    *state = {};
    state->identity.header = frame.header;
    state->identity.player = frame.player;
    state->identity.epoch = frame.epoch;
    if (!frame.ready || !frame.header || frame.header > UINTPTR_MAX - 0x3000000u || !NSThread.isMainThread) {
        if (reason) *reason = @"戰鬥檢查需要已校驗的遊戲主執行緒";
        return NO;
    }
    uintptr_t save = frame.header + PHSaveRVA, game = frame.header + PHGameMainRVA;
    uint8_t scene = 0, requested = 0;
    uint32_t player = 0;
    if (!PHReadU8(save, &scene) || !PHReadU8(save + 1, &requested) ||
        !PHReadU32(save + PHPlayerIDOffset, &player) || player != frame.player || !player) {
        if (reason) *reason = @"玩家或場景資料未就緒";
        return NO;
    }
    if (scene != 1 || requested != scene) return YES;
    uintptr_t object = 0, impl = 0, activeImpl = 0, dungeon = 0;
    if (!PHReadPointer(frame.header + PHSceneTableRVA + 24, &object) || object != game ||
        !PHReadPointer(game + 0x70, &impl) || !impl || impl > UINTPTR_MAX - 0xc4000u || (impl & 7u) ||
        !PHReadPointer(frame.header + PHBattleImplRVA, &activeImpl) || activeImpl != impl ||
        !PHMemoryHasProtection(impl + 0x92000, 0x2c000, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE) ||
        !PHReadPointer(impl + PHBattleDungeonPointerOffset, &dungeon) || !dungeon ||
        !PHMemoryHasProtection(dungeon, 128, VM_PROT_READ, 0)) {
        if (reason) *reason = @"作用中的戰鬥物件或參數範圍未通過檢查";
        return NO;
    }
    state->present = true;
    state->identity.game = game;
    state->identity.impl = impl;
    uint32_t key1 = 0, key2 = 0, phase = 0, hp = 0, shadow = 0, touch = 0;
    uint16_t mode = 0, ids[2] = {};
    if (!PHReadMemory(save + PHBattleDungeonOffset, ids, sizeof(ids)) || !ids[0] || !ids[1] ||
        !PHReadMemory(frame.header + PHBattleEntryHashRVA, state->identity.entryHash, sizeof(state->identity.entryHash)) ||
        !PHReadU16(frame.header + PHBattleModeRVA, &mode) ||
        !PHReadU16(save + PHBattleSourceModeOffset, &state->sourceMode) ||
        !PHReadU32(frame.header + PHBattleMultiplayerRVA, &state->multiplayer) ||
        !PHReadU32(save + PHBattleAuditOffset, &state->auditFlags) ||
        !PHReadU32(save + PHBattleCatalogCountOffset, &state->catalogCount) ||
        state->catalogCount < 2 || state->catalogCount > 100000 ||
        !PHReadPointer(save + PHBattleCatalogPointerOffset, &state->catalog) || !state->catalog ||
        state->catalog > UINTPTR_MAX - state->catalogCount * PHBattleCatalogStride ||
        !PHMemoryHasProtection(state->catalog, state->catalogCount * PHBattleCatalogStride, VM_PROT_READ, 0) ||
        !PHReadU32(frame.header + PHBattleKey1RVA, &key1) || !PHReadU32(frame.header + PHBattleKey2RVA, &key2) ||
        !PHReadU32(game + 92, &touch)) {
        if (reason) *reason = @"戰鬥身分、圖鑑或保護值金鑰無法讀取";
        return NO;
    }
    bool hasHash = false;
    for (uint8_t byte : state->identity.entryHash) hasHash |= byte != 0;
    if (!hasHash) { if (reason) *reason = @"入場資料尚未就緒"; return NO; }
    state->identity.dungeon = ids[0];
    state->identity.floor = ids[1];
    state->mode = static_cast<int16_t>(mode);
    // GameMain +93 is the input trigger used by the player-input handler.
    // Refuse either input byte while an operation is about to be processed.
    state->inputQuiet = (touch & 0xffffu) == 0;
    state->protectedValuesValid = DecodeAt(impl + PHBattlePhaseOffset, key1, key2, 0x766cfc6du, &phase) &&
        DecodeAt(impl + PHBattleHPOffset, key1, key2, 0x766cfc6du, &hp);
    state->phase = phase;
    state->hp = hp;
    state->hpMatches = PHReadU32(impl + PHBattleHPShadowOffset, &shadow) && (hp ^ shadow) == 0x8085a6beu;
    for (NSUInteger slot = 0; slot < 6; ++slot) {
        PHBattleMember &member = state->members[slot];
        uintptr_t address = impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
        uint8_t bytes[0x5f0];
        if (!PHReadMemory(address, bytes, sizeof(bytes))) { state->protectedValuesValid = false; continue; }
        member.active = (bytes[0] & 1u) != 0;
        if (!member.active) { member.readable = true; continue; }
        std::memcpy(&member.baseID, bytes + PHBattleBaseIDOffset, sizeof(member.baseID));
        std::memcpy(&member.assistID, bytes + PHBattleAssistIDOffset, sizeof(member.assistID));
        std::memcpy(&member.catalogID, bytes + PHBattleCatalogIDOffset, sizeof(member.catalogID));
        std::memcpy(&member.previousID, bytes + 64, sizeof(member.previousID));
        member.flags = bytes[56];
        member.radius = bytes[63];
        uintptr_t parameter = 0;
        std::memcpy(&parameter, bytes + PHBattleCardParamOffset, sizeof(parameter));
        member.readable = DecodeByte(bytes, PHBattleSourceSlotOffset, key1, key2, &member.sourceSlot) &&
            DecodeByte(bytes, PHBattleScriptSlotOffset, key1, key2, &member.scriptSlot) &&
            Decode16(bytes, PHBattleFormOffset, key1, key2, &member.formID) &&
            Decode16(bytes, PHBattleTempIDOffset, key1, key2, &member.temporaryID) &&
            Decode16(bytes, PHBattleTempTurnsOffset, key1, key2, &member.temporaryTurns) &&
            Decode16(bytes, PHBattleChargeOffset, key1, key2, &member.charge) &&
            CatalogValid(member.baseID, state) && CatalogValid(member.catalogID, state) &&
            (!member.assistID || CatalogValid(member.assistID, state)) &&
            (!member.formID || CatalogValid(member.formID, state)) &&
            parameter == state->catalog + member.catalogID * PHBattleCatalogStride;
        state->protectedValuesValid &= member.readable;
    }
    uint32_t lastKey1 = 0, lastKey2 = 0;
    state->protectedValuesValid &= PHReadU32(frame.header + PHBattleKey1RVA, &lastKey1) && lastKey1 == key1 &&
                                  PHReadU32(frame.header + PHBattleKey2RVA, &lastKey2) && lastKey2 == key2;
    if (CanReadNative(state)) {
        try {
            uint32_t digest = static_cast<uint32_t>(Calls(frame.header).digest(game));
            state->digestKnown = PHReadU32(impl + PHBattleDigestShadowOffset, &shadow);
            state->digestMatches = state->digestKnown && (digest ^ shadow) == 0x689d8b5cu;
        } catch (...) {
            if (reason) *reason = @"原生隊員摘要讀取發生例外，停止操作";
            return NO;
        }
    }
    return YES;
}

static void RequireWrite(uintptr_t address, const void *data, size_t length) {
    if (!PHWriteData(address, data, length)) throw std::runtime_error("Battle data write failed");
}

// One synchronous native transformation transaction, also used for rollback.
// It never restores resource pointers or clears the game's audit flags.
static BOOL Perform(const PHBattleState *state, NSUInteger slot, uint16_t form, uint32_t catalog,
                    const PHBattleMember *presentation, uint16_t charge, BOOL capCharge,
                    BOOL *didMutate, uint16_t *finalCharge, NSString **reason) {
    uintptr_t header = state->identity.header, game = state->identity.game, impl = state->identity.impl;
    uintptr_t member = impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    NativeCalls native = Calls(header);
    try {
        @try {
            uint8_t flags = state->members[slot].flags & static_cast<uint8_t>(~2u);
            uint8_t radius = 0;
            uint16_t previous = static_cast<uint16_t>(state->members[slot].catalogID);
            if (presentation) {
                flags |= presentation->flags & 2u;
                radius = presentation->radius;
                previous = presentation->previousID;
            } else {
                uint8_t scale = 0;
                float factor = native.radius();
                if (!PHReadU8(header + PHBattleScaleRVA, &scale) || !scale || !std::isfinite(factor) || factor <= 0 || factor > 1) {
                    throw std::runtime_error("Invalid transformation presentation");
                }
                radius = static_cast<uint8_t>(factor * 0.5f * scale + 0.5f);
            }
            *didMutate = YES;
            native.set16(member + PHBattleFormOffset, form);
            RequireWrite(member + 56, &flags, sizeof(flags));
            RequireWrite(member + 63, &radius, sizeof(radius));
            RequireWrite(member + 64, &previous, sizeof(previous));
            if (!native.rebuildMember(game, member, UINT32_MAX)) throw std::runtime_error("Member rebuild failed");
            uint64_t maximum = native.maxCharge(member, 0);
            if (maximum > UINT16_MAX) throw std::runtime_error("Invalid skill charge bound");
            uint16_t nextCharge = capCharge ? static_cast<uint16_t>(std::min<uint64_t>(charge, maximum)) : charge;
            native.set16(member + PHBattleChargeOffset, nextCharge);
            // Match ordinary transformation case 236: rebuild this member once,
            // cap its existing charge, then refresh team totals. Rebuilding all
            // six members here could consume unrelated pending member state.
            native.refreshTeam(game, 0);
            uint32_t digestShadow = static_cast<uint32_t>(native.digest(game)) ^ 0x689d8b5cu;
            RequireWrite(impl + PHBattleDigestShadowOffset, &digestShadow, sizeof(digestShadow));
            uint32_t key1 = 0, key2 = 0, hp = 0;
            if (!PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2) ||
                !DecodeAt(impl + PHBattleHPOffset, key1, key2, 0x766cfc6du, &hp)) throw std::runtime_error("HP decode failed");
            uint32_t hpShadow = hp ^ 0x8085a6beu;
            RequireWrite(impl + PHBattleHPShadowOffset, &hpShadow, sizeof(hpShadow));
            PHBattleFrame frame = {header, state->identity.player, state->identity.epoch, true, true};
            PHBattleState after = {};
            if (!ReadLive(&after, frame, reason) || !CanReadNative(&after) || !after.hpMatches || !after.digestMatches ||
                !PHBattleSameIdentity(&state->identity, &after.identity)) throw std::runtime_error("Post-mutation battle check failed");
            const PHBattleMember &changed = after.members[slot];
            if (!PHBattleSameSource(&state->members[slot], &changed) || changed.formID != form || changed.catalogID != catalog ||
                changed.charge != nextCharge || changed.previousID != previous || changed.radius != radius ||
                (changed.flags & 2u) != (flags & 2u)) throw std::runtime_error("Post-mutation member check failed");
            for (NSUInteger i = 0; i < 6; ++i) {
                if (i == slot) continue;
                const PHBattleMember &old = state->members[i], &current = after.members[i];
                if (old.active != current.active || (old.active && (!PHBattleSameSource(&old, &current) ||
                    old.formID != current.formID || old.catalogID != current.catalogID))) throw std::runtime_error("Other member changed");
            }
            *finalCharge = nextCharge;
            return YES;
        } @catch (NSException *exception) {
            (void)exception;
            if (reason) *reason = @"原生重建發生 Objective-C 例外";
        }
    } catch (...) {
        if (reason && !*reason) *reason = @"原生重建或操作後校驗未通過";
    }
    return NO;
}
}

@interface PHBattleNative : NSObject <PHBattleEngine>
@end

@implementation PHBattleNative {
    uintptr_t _verifiedHeader;
    BOOL _busy, _faulted;
}

- (BOOL)readState:(PHBattleState *)state frame:(PHBattleFrame)frame reason:(NSString **)reason {
    if (!state || !frame.ready || !NSThread.isMainThread) return NO;
    if (_verifiedHeader != frame.header) {
        if (!CheckCode(frame.header, reason)) return NO;
        _verifiedHeader = frame.header;
    }
    return ReadLive(state, frame, reason);
}

- (BOOL)validateTarget:(uint16_t)target state:(const PHBattleState *)state reason:(NSString **)reason {
    BOOL valid = state && CatalogValid(target, state);
    if (!valid && reason) *reason = @"目標不在目前有效圖鑑中，或角色參數尚未載入";
    return valid;
}

- (PHBattleMutationResult)changeState:(const PHBattleState *)expected slot:(NSUInteger)slot
    form:(uint16_t)form catalog:(uint32_t)catalog presentation:(const PHBattleMember *)presentation
    reason:(NSString **)reason {
    if (_busy || _faulted || !expected || slot >= 6 || !NSThread.isMainThread) {
        if (reason) *reason = @"原生戰鬥操作已停止或正在執行";
        return PHBattleMutationUncertain;
    }
    PHBattleFrame frame = {expected->identity.header, expected->identity.player, expected->identity.epoch, true, true};
    PHBattleState before = {};
    if (!CheckCode(frame.header, reason) || !ReadLive(&before, frame, reason) ||
        PHBattleStateGate(&before) != PHBattleGateReady || !PHBattleSameIdentity(&expected->identity, &before.identity) ||
        expected->catalog != before.catalog || expected->catalogCount != before.catalogCount ||
        !PHBattleSameSource(&expected->members[slot], &before.members[slot]) ||
        expected->members[slot].formID != before.members[slot].formID ||
        expected->members[slot].catalogID != before.members[slot].catalogID ||
        before.members[slot].sourceSlot < 0 || before.members[slot].sourceSlot > 5 || before.members[slot].scriptSlot > 0 ||
        before.members[slot].temporaryID || before.members[slot].temporaryTurns ||
        !CatalogValid(catalog, &before) || (form && !CatalogValid(form, &before)) ||
        !PHMemoryHasProtection(frame.header + PHBattlePRNGRVA, 4, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) {
        if (reason && !*reason) *reason = @"執行前的戰鬥身分或狀態已變更，已取消操作";
        return PHBattleMutationRejected;
    }
    _busy = YES;
    BOOL mutated = NO;
    uint16_t charge = 0;
    NSString *failure = nil;
    BOOL success = Perform(&before, slot, form, catalog, presentation, before.members[slot].charge, YES,
                           &mutated, &charge, &failure);
    uint32_t audit = UINT32_MAX;
    success &= PHReadU32(frame.header + PHSaveRVA + PHBattleAuditOffset, &audit) && !(audit & ~before.auditFlags);
    if (success) {
        _busy = NO;
        NSLog(@"[PADCheaper] Battle member %lu -> #%u, charge %u; native checks passed", (unsigned long)slot + 1, catalog, charge);
        return PHBattleMutationApplied;
    }
    if (!mutated) {
        _busy = NO;
        if (reason) *reason = failure ?: @"替換在寫入前被取消";
        return PHBattleMutationRejected;
    }
    PHBattleState current = {};
    BOOL canRollback = ReadLive(&current, frame, NULL) && CanReadNative(&current) &&
        PHBattleSameIdentity(&before.identity, &current.identity) &&
        PHBattleSameSource(&before.members[slot], &current.members[slot]) &&
        (current.members[slot].formID == form || current.members[slot].formID == before.members[slot].formID);
    BOOL rollbackMutation = NO;
    BOOL restored = canRollback && Perform(&current, slot, before.members[slot].formID, before.members[slot].catalogID,
        &before.members[slot], before.members[slot].charge, NO, &rollbackMutation, &charge, NULL);
    _busy = NO;
    _faulted = !restored;
    if (reason) *reason = restored ? @"替換未通過校驗；已還原原有形態與技能充能" :
                                     @"原生操作未能確認還原，已停止後續寫入；請複製診斷";
    NSLog(@"[PADCheaper] Battle mutation failed; rollback %@", restored ? @"verified" : @"unconfirmed");
    return restored ? PHBattleMutationRolledBack : PHBattleMutationUncertain;
}
@end

id<PHBattleEngine> PHCreateBattleEngine(void) { return [PHBattleNative new]; }
