#import <Foundation/Foundation.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>
#include "../src/PHOrbNative.mm"

static unsigned failures, writes, failWrite, guardChecks;
static bool codeValid = true;
static bool ignoreSelectionWrite, switchMultiInSelectionWrite, changeSourceInSelectionWrite;
static bool replaceHashDuringResizeRead;
static bool changeTurnDuringMultiRead, changeHashDuringBoardRead;
static bool changeHashDuringEntryRead;
static bool changeTurnDuringCoordinateRead;
static unsigned changePeerDuringMultiRead;
static bool changePeerDuringCoordinateRead;
static bool changeLeaderDuringPairRead, changeLeaderDuringCoordinateRead;
static bool changeLeaderCatalogDuringPairRead;
static bool clearImplAfterSceneRead;
static bool rejectBattleProtection;
static std::vector<uint8_t> guest(0x3000000), implBytes(0xc4000), realInput(PHOrbInputBytes), newInput(PHOrbInputBytes);
static std::vector<uint8_t> catalogBytes(14730 * PHBattleCatalogStride);
static const uintptr_t Header = reinterpret_cast<uintptr_t>(guest.data());
static const uintptr_t Impl = reinterpret_cast<uintptr_t>(implBytes.data());
static const uintptr_t Input = reinterpret_cast<uintptr_t>(realInput.data());
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); ++failures; } }
static bool Range(uintptr_t address, size_t length) {
    for (auto *region : {&guest, &implBytes, &realInput, &newInput, &catalogBytes}) {
        auto base = reinterpret_cast<uintptr_t>(region->data());
        if (address >= base && address - base <= region->size() && length <= region->size() - (address - base)) return true;
    }
    return false;
}
BOOL PHMemoryHasProtection(uintptr_t address, size_t length, vm_prot_t required, vm_prot_t forbidden) {
    if (rejectBattleProtection && address == Impl + 0x92000 && length == 0x2e000) return NO;
    return Range(address, length);
}
BOOL PHReadMemory(uintptr_t address, void *output, size_t length) {
    if (!Range(address, length)) return NO;
    std::memcpy(output, reinterpret_cast<void *>(address), length);
    if (changeHashDuringEntryRead && address == Header + PHBattleEntryHashRVA) {
        changeHashDuringEntryRead = false;
        reinterpret_cast<uint8_t *>(address)[0] = 3;
    }
    if (clearImplAfterSceneRead && address == Header + PHBattleImplRVA) {
        clearImplAfterSceneRead = false;
        Store<uintptr_t>(reinterpret_cast<uint8_t *>(address), 0);
    }
    if (replaceHashDuringResizeRead && address == Impl + PHOrbResizeProgressOffset) {
        replaceHashDuringResizeRead = false;
        reinterpret_cast<uint8_t *>(Header + PHBattleEntryHashRVA)[0] = 2;
    }
    if (changeTurnDuringMultiRead && address == Impl + PHOrbMultiResolveOffset) {
        changeTurnDuringMultiRead = false;
        Store<uint32_t>(reinterpret_cast<uint8_t *>(Impl + PHBattleTurnSerialOffset), 3);
    }
    if (changeHashDuringBoardRead && address == Impl + PHOrbTableOffset) {
        changeHashDuringBoardRead = false;
        reinterpret_cast<uint8_t *>(Header + PHBattleEntryHashRVA)[0] = 2;
    }
    if (changeTurnDuringCoordinateRead && address == Header + PHOrbTransformEnabledRVA) {
        changeTurnDuringCoordinateRead = false;
        Store<uint32_t>(reinterpret_cast<uint8_t *>(Impl + PHBattleTurnSerialOffset), 3);
    }
    if (changePeerDuringMultiRead == 1 && address == Impl + PHOrbMultiDisconnectSeatOffset) {
        changePeerDuringMultiRead = 0;
        Store<uint32_t>(reinterpret_cast<uint8_t *>(Impl + PHOrbMultiDisconnectSeatOffset), 1);
    }
    if (changePeerDuringMultiRead == 2 && address == Impl + PHBattleTeamDescriptorOffset + 28) {
        changePeerDuringMultiRead = 0;
        Store<uint32_t>(reinterpret_cast<uint8_t *>(address), 6);
    }
    if (changePeerDuringMultiRead == 3 && address == Impl + PHOrbMultiSyncStepOffset) {
        changePeerDuringMultiRead = 0;
        Store<uint32_t>(reinterpret_cast<uint8_t *>(address), 0);
    }
    if (changePeerDuringCoordinateRead && address == Header + PHOrbTransformEnabledRVA) {
        changePeerDuringCoordinateRead = false;
        const uint32_t seat = Load<uint32_t>(reinterpret_cast<uint8_t *>(Impl + PHBattlePlayerTeamOffset));
        Store<uint32_t>(reinterpret_cast<uint8_t *>(Impl + PHOrbMultiDisconnectSeatOffset), seat);
        Store<uint32_t>(reinterpret_cast<uint8_t *>(Impl + PHBattleTeamDescriptorOffset + seat * PHBattleTeamDescriptorStride + 28), 5);
    }
    const uintptr_t helper = Impl + PHBattleMemberOffset + 5 * PHBattleMemberStride;
    if (changeLeaderCatalogDuringPairRead && address == helper + PHBattleCardParamOffset) {
        changeLeaderCatalogDuringPairRead = false;
        const uintptr_t records = Header + PHSaveRVA + PHOrbLeaderCatalogRecordsOffset;
        Store<uint32_t>(reinterpret_cast<uint8_t *>(records), Load<uint32_t>(reinterpret_cast<uint8_t *>(records)) + 1);
    }
    if ((changeLeaderDuringPairRead && address == helper + PHBattleCardParamOffset) ||
        (changeLeaderDuringCoordinateRead && address == Header + PHOrbTransformEnabledRVA)) {
        changeLeaderDuringPairRead = changeLeaderDuringCoordinateRead = false;
        const uintptr_t parameter = Load<uintptr_t>(reinterpret_cast<uint8_t *>(Impl + PHBattleMemberOffset + PHBattleCardParamOffset));
        Store<uint32_t>(reinterpret_cast<uint8_t *>(parameter + PHOrbLeaderSkillOffset), 51);
    }
    return YES;
}
BOOL PHWriteData(uintptr_t address, const void *input, size_t length) {
    ++writes;
    if (writes == failWrite) return NO;
    bool selectionWrite = false;
    for (unsigned slot = 0; slot < 6; ++slot)
        selectionWrite |= address == Impl + PHBattleMemberOffset + slot * PHBattleMemberStride + PHOrbSkillOverrideOffset &&
            length == sizeof(uint32_t);
    if (!selectionWrite && (address != Header + PHBattleInputRVA || length != sizeof(uintptr_t))) return NO;
    if (selectionWrite && ignoreSelectionWrite) return YES;
    std::memcpy(reinterpret_cast<void *>(address), input, length);
    if (selectionWrite && switchMultiInSelectionWrite)
        Store<uint32_t>(reinterpret_cast<uint8_t *>(Header + PHBattleMultiplayerRVA), 1);
    if (selectionWrite && changeSourceInSelectionWrite)
        Store<uint8_t>(reinterpret_cast<uint8_t *>(Impl + PHOrbSkillSelectedSourceOffset), 1);
    return YES;
}
BOOL PHValidateGuest(const mach_header_64 *header, NSString **reason) { ++guardChecks; return codeValid; }
BOOL PHAddressIsGuestCode(uintptr_t address, const mach_header_64 *header) { return address >= Header && address < Header + 0x1110000; }
template<typename T> static void Put(uintptr_t address, T value) { std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(value)); }
static void Protected(uintptr_t address, uint32_t value, uint32_t salt) {
    uint32_t raw[4] = {uint32_t((value << 5) | (value >> 27)) ^ salt, 0, value ^ 17u ^ 42u, value};
    std::memcpy(reinterpret_cast<void *>(address), raw, sizeof(raw));
}
static PHBattleFrame Frame() { PHBattleFrame frame{}; frame.header = Header; frame.player = 42; frame.epoch = 1; frame.ready = frame.enabled = true; return frame; }
static void Reset(unsigned width = 6, unsigned height = 5) {
    std::fill(guest.begin(), guest.end(), 0); std::fill(implBytes.begin(), implBytes.end(), 0); std::fill(realInput.begin(), realInput.end(), 0);
    writes = failWrite = guardChecks = 0; codeValid = true;
    ignoreSelectionWrite = switchMultiInSelectionWrite = changeSourceInSelectionWrite = false;
    replaceHashDuringResizeRead = false;
    changeTurnDuringMultiRead = changeHashDuringBoardRead = false;
    changeHashDuringEntryRead = false;
    changeTurnDuringCoordinateRead = false;
    changePeerDuringMultiRead = 0; changePeerDuringCoordinateRead = false;
    changeLeaderDuringPairRead = changeLeaderDuringCoordinateRead = false;
    changeLeaderCatalogDuringPairRead = false;
    clearImplAfterSceneRead = false;
    rejectBattleProtection = false;
    const auto save = Header + PHSaveRVA, game = Header + PHGameMainRVA;
    Put<uint16_t>(save, 0x0101); Put<uint32_t>(save + PHPlayerIDOffset, 42);
    Put<uintptr_t>(Header + PHSceneTableRVA + 24, game); Put<uintptr_t>(game + 0x70, Impl);
    Put<uintptr_t>(Header + PHBattleImplRVA, Impl); Put<uintptr_t>(Header + PHBattleInputRVA, Input);
    Put<uint16_t>(save + PHBattleDungeonOffset, 1); Put<uint16_t>(save + PHBattleDungeonOffset + 2, 2);
    Put<uint8_t>(Header + PHBattleEntryHashRVA, 1);
    // The second native key aliases the saved player ID.
    Put<uint32_t>(Header + PHBattleKey1RVA, 17); Put<uint32_t>(Header + PHBattleKey2RVA, 42);
    Protected(Impl + PHBattlePhaseOffset, 2, 0x766cfc6d); Protected(Impl + PHBattleWaveOffset, 0, 0x766cfc6d);
    Protected(Impl + PHOrbFixedStartOffset, 255, 0x6a875a8b);
    Put<uint8_t>(game + 88, uint8_t(width)); Put<uint8_t>(game + 89, uint8_t(height));
    Put<uint8_t>(game + 92, 3); Put<uint8_t>(game + 93, 1);
    Put<uint16_t>(Impl + PHOrbRemainingOffset, 300); Put<uint8_t>(Header + PHBattleScaleRVA, 60);
    Put<float>(Input + 2664, 0.5); Put<float>(Input + 2668, 0.1);
    for (const auto &guard : PHOrbTextGuards) std::memcpy(reinterpret_cast<void *>(Header + guard.rva), guard.bytes, 64);
    const unsigned spacing = width == 7 ? 90 : width == 5 ? 126 : 105;
    for (unsigned row = 0; row < height; ++row) for (unsigned column = 0; column < width; ++column) {
        unsigned nativeIndex = (height - 1 - row) * 16 + column;
        uintptr_t token = Impl + PHOrbPoolOffset + nativeIndex * 172;
        Put<uintptr_t>(Impl + PHOrbTableOffset + nativeIndex * 8, token);
        Put<uint8_t>(token, uint8_t((row * width + column) % 6));
        Put<uint16_t>(token + 18, 0);
        Put<int16_t>(token + 24, int16_t(57 + column * spacing)); Put<int16_t>(token + 26, int16_t(400 + row * spacing));
        Put<int16_t>(token + 28, int16_t(57 + column * spacing)); Put<int16_t>(token + 30, int16_t(400 + row * spacing));
    }
}
static uintptr_t GlobalInput() { return Load<uintptr_t>(guest.data() + PHBattleInputRVA); }
static void SetLeader(unsigned slot, uint16_t number) {
    uint16_t id = 0;
    if (!PHBattleCatalogIDForNumber(number, &id) || id >= 14600) { Check(false, "leader fixture number"); return; }
    uintptr_t catalog = reinterpret_cast<uintptr_t>(catalogBytes.data());
    Put<uint32_t>(Header + PHSaveRVA + PHBattleCatalogCountOffset, 14600);
    Put<uint32_t>(Header + PHSaveRVA + PHOrbLeaderCatalogRecordsOffset, 14600);
    Put<uintptr_t>(Header + PHSaveRVA + PHBattleCatalogPointerOffset, catalog);
    Put<uint32_t>(catalog + id * PHBattleCatalogStride, id);
    uintptr_t member = Impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    Put<uint8_t>(member, 1); Put<uint32_t>(member + PHBattleCatalogIDOffset, id);
    Put<uintptr_t>(member + PHBattleCardParamOffset, catalog + id * PHBattleCatalogStride);
}
// Only the portions of the verified input update required by these fixtures.
static void NativeInputFrame() {
    auto p = reinterpret_cast<uint8_t *>(GlobalInput());
    std::memcpy(p + 1320, p, 264); p[1320 + 219] = p[214]; p[2641] = p[2640];
    std::memcpy(p + 1320 + 12, p + 4, 8); p[213] = 0;
}
static void ReadIssueNativeTests() {
    NSArray<NSString *> *messages = @[@"場景表的戰鬥物件未通過讀取校驗", @"戰鬥實作指標未通過讀取校驗",
        @"目前戰鬥實作與場景物件不一致", @"戰鬥記憶體範圍未通過讀寫保護校驗"];
    for (unsigned guard = 0; guard < 4; ++guard) {
        Reset(); id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot captured{}, failed{}; NSString *reason = nil;
        Check([engine read:&captured frame:Frame() reason:&reason], "read-issue fixture captures a valid board before each failure");
        if (guard == 0) Put<uintptr_t>(Header + PHSceneTableRVA + 24, 0);
        if (guard == 1) Put<uintptr_t>(Header + PHGameMainRVA + 0x70, Impl + 1);
        // GameMain + 0x70 aliases PHBattleImplRVA in this target. Invalidate
        // after its first read to reach the second, consistency-check read.
        if (guard == 2) clearImplAfterSceneRead = true;
        if (guard == 3) rejectBattleProtection = true;
        const BOOL read = [engine read:&failed frame:Frame() reason:&reason];
        if (read || failed.valid || ![reason isEqual:messages[guard]])
            std::fprintf(stderr, "Read guard %u: read=%d valid=%d reason=%s\n", guard, int(read),
                int(failed.valid), reason.UTF8String ?: "nil");
        Check(!read && !failed.valid && [reason isEqual:messages[guard]],
            "each original battle-read guard names its specific failed check");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&captured frame:Frame() reason:&reason] && !writes,
            "a read-guard failure cannot stage input from a previously captured board");
    }
    Reset();
}

#include "orb_skill_native_tests.inc"
#include "orb_effect_native_tests.inc"
#include "orb_thorn_native_tests.inc"
#include "orb_diagonal_native_tests.inc"
#include "orb_skill_window_native_tests.inc"
#include "orb_multiplayer_native_tests.inc"

int main() {
    @autoreleasepool {
        MultiplayerNativeTests();
        Reset(); id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        BOOL initialRead = [engine read:&state frame:Frame() reason:&reason];
        Check(initialRead && PHOrbCanPreview(&state), "validated ordinary board readable");
        if (!initialRead || !state.valid) { std::fprintf(stderr, "Fixture read failed: %s\n", reason.UTF8String ?: "no reason"); return 1; }
        Check(state.board.tokens[0] == Impl + PHOrbPoolOffset + 64 * 172 && state.board.y[24] > state.board.y[0], "native bottom-first pointers become top-first cells");
        const auto original = realInput;
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason], "first native input staged");
        Check(GlobalInput() != Input && Load<float>(reinterpret_cast<uint8_t *>(GlobalInput()) + 4) == state.board.x[0], "native coordinates and separate input manager");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
        Check([engine finishInput:&reason] && GlobalInput() == Input && realInput == original, "original UIKit input exactly preserved after frame");
        Check([engine read:&state frame:Frame() reason:&reason], "owned drag can be observed");
        Put<uint8_t>(Input + 214, 1); // A real finger arrives between native frames.
        const auto manual = realInput;
        Check(![engine stageCell:1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason], "manual input refuses another move");
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason], "owned release can be delivered while preserving manual input");
        Check(reinterpret_cast<uint8_t *>(GlobalInput())[1320 + 219] == 1, "private frame held tracking survives between draws");
        NativeInputFrame(); Check([engine finishInput:&reason] && realInput == manual, "release does not consume real finger events");

        Reset(); engine = PHCreateOrbEngine();
        Check([engine read:&state frame:Frame() reason:&reason], "deferred native start setup");
        const auto originalInput = realInput;
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
            "deferred start sends one began");
        const uintptr_t heldManager = GlobalInput();
        NativeInputFrame(); // Tall phone layouts leave the orb unselected on began.
        Check([engine finishInput:&reason] && GlobalInput() == Input && realInput == originalInput,
            "deferred began restores the entire original input without selecting an orb");
        Check([engine read:&state frame:Frame() reason:&reason] && !state.dragged,
            "held continuation may wait for selection without an owned orb");
        Check([engine stageCell:0 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] &&
            GlobalInput() == heldManager && reinterpret_cast<uint8_t *>(heldManager)[213] == 0 &&
            reinterpret_cast<uint8_t *>(heldManager)[214] == 1 &&
            reinterpret_cast<uint8_t *>(heldManager)[1320 + 219] == 1,
            "held start reuses its private input bank with no repeated began and retained frame tracking");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[0]);
        Check([engine finishInput:&reason] && GlobalInput() == Input && realInput == originalInput,
            "accepted held start restores every byte of real input");
        Check([engine read:&state frame:Frame() reason:&reason] && state.dragged == state.board.tokens[0],
            "native selection is visible after the held frame");

        Reset(); SetLeader(0, 14242); SetLeader(5, 13834); engine = PHCreateOrbEngine();
        Check([engine read:&state frame:Frame() reason:&reason] && state.leadersValid &&
              state.leaderNumbers[0] == 14242 && state.leaderNumbers[1] == 13834 && writes == 0,
              "current visible leader/helper IDs are read without native calls or writes and use encyclopedia numbering");
        PHOrbSnapshot oldLeaders = state;
        SetLeader(0, 12572);
        Check([engine read:&state frame:Frame() reason:&reason] && state.leadersValid && state.leaderNumbers[0] == 12572 &&
              !PHOrbSameLeaders(&oldLeaders, &state), "transformation or leader exchange changes the observed current ID");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&oldLeaders frame:Frame() reason:&reason] && writes == 0,
              "a route planned for former leaders cannot stage input even on an identical board");
        Put<uintptr_t>(Impl + PHBattleMemberOffset + 5 * PHBattleMemberStride + PHBattleCardParamOffset, 0);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && !state.leadersValid &&
              !state.leaderNumbers[0] && !state.leaderNumbers[1],
              "invalid helper catalog pointer withholds both leader IDs while ordinary manual previews remain readable");

        for (auto dimensions : {std::pair<unsigned, unsigned>{7,6}, {5,4}}) {
            const unsigned width = dimensions.first, height = dimensions.second, last = width * height - 1;
            Reset(width, height); engine = PHCreateOrbEngine();
            BOOL readable = [engine read:&state frame:Frame() reason:&reason];
            Check(readable && PHOrbCanPreview(&state), "expanded native board is readable");
            if (!readable || !state.valid) return 1;
            Check(state.board.tokens[0] == Impl + PHOrbPoolOffset + (height - 1) * 16 * 172 &&
                state.board.tokens[last] == Impl + PHOrbPoolOffset + (width - 1) * 172,
                "expanded boards preserve top-to-bottom order and include their final cell");
            const auto originalInput = realInput;
            Check([engine stageCell:last down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
                "expanded board can begin at the bottom-right native cell");
            Check(Load<float>(reinterpret_cast<uint8_t *>(GlobalInput()) + 4) == state.board.x[last] &&
                Load<float>(reinterpret_cast<uint8_t *>(GlobalInput()) + 8) == state.board.y[last],
                "last-cell input uses the validated geometry rather than six-column coordinates");
            NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[last]);
            Check([engine finishInput:&reason], "expanded-board start restores input");
            [engine read:&state frame:Frame() reason:&reason];
            Check([engine stageCell:last down:NO beginning:NO expected:&state frame:Frame() reason:&reason],
                "expanded board can release at its last cell");
            NativeInputFrame(); Check([engine finishInput:&reason] && realInput == originalInput, "expanded release preserves real input");
            Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
            Put<uint32_t>(state.board.tokens[last] + 4, 0x40000000);
            PHOrbSnapshot effect{}; [engine read:&effect frame:Frame() reason:&reason];
            Check(!PHOrbCanPreview(&effect), "unsupported effects in the extra row remain blocked");
        }
        Reset(); engine = PHCreateOrbEngine(); [engine read:&state frame:Frame() reason:&reason];
        Reset(7,6);
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == 0,
            "a resized board rejects input from the old preview even with the same battle identity");
        Reset(8,6); engine = PHCreateOrbEngine();
        [engine read:&state frame:Frame() reason:&reason];
        Check(!state.valid && !PHOrbCanPreview(&state) && writes == 0, "unknown geometry never reaches the input path");

        Reset(); engine = PHCreateOrbEngine(); [engine read:&state frame:Frame() reason:&reason];
        Put<uint32_t>(state.board.tokens[0] + 4, 0x800);
        PHOrbSnapshot changed{}; [engine read:&changed frame:Frame() reason:&reason];
        Check(PHOrbCanPreview(&changed) && (PHOrbBoardObstacles(&changed) & PHOrbObstacleLocked),
            "verified locked basic orb keeps its flag and remains matchable");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
            "changed lock state still invalidates an older preview");
        Check([engine stageCell:0 down:YES beginning:YES expected:&changed frame:Frame() reason:&reason] &&
            [engine finishInput:&reason], "fresh locked snapshot may start a guarded native drag");
        Put<uint32_t>(state.board.tokens[0] + 4, 0x40000800);
        [engine read:&changed frame:Frame() reason:&reason];
        Check(!PHOrbCanPreview(&changed), "lock acceptance does not authorize other simultaneous orb effects");
        Put<uint32_t>(state.board.tokens[0] + 4, 0); Put<uint16_t>(Impl + 739268, 0x401);
        [engine read:&changed frame:Frame() reason:&reason]; Check(!PHOrbCanPreview(&changed) &&
            (PHOrbBoardObstacles(&changed) & PHOrbObstacleUnmatchable), "unmatchable duration with an invalid protected mask remains blocked and identifiable");
        Put<uint16_t>(Impl + 739268, 0); Put<uint16_t>(Impl + 739360, 0x401);
        [engine read:&changed frame:Frame() reason:&reason]; Check(!PHOrbCanPreview(&changed) &&
            (PHOrbBoardObstacles(&changed) & PHOrbObstacleSpinner), "spinner duration without a validated layout cannot authorize a route");
        Put<uint16_t>(Impl + 739360, 0); Put<uint16_t>(state.board.tokens[0] + 18, 3);
        [engine read:&changed frame:Frame() reason:&reason]; Check(!PHOrbCanPreview(&changed), "swap animation is not a settled board");
        Put<uint16_t>(state.board.tokens[0] + 18, 0);
        Put<uintptr_t>(Impl + PHOrbTableOffset + 65 * 8, state.board.tokens[0]);
        Check(![engine read:&changed frame:Frame() reason:&reason], "duplicate pool token rejected");
        Protected(Impl + PHBattlePhaseOffset, 3, 0x766cfc6d);
        Put<uintptr_t>(Impl + PHBattleDragPointerOffset, 0);
        Check([engine read:&changed frame:Frame() reason:&reason] && changed.valid && !changed.dragged && !PHOrbCanPreview(&changed),
            "release metadata remains readable after native resolution invalidates the board table");

        Reset(); engine = PHCreateOrbEngine(); Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
        [engine read:&state frame:Frame() reason:&reason];
        Check(state.multiplayerDisabled && guardChecks == 0 && writes == 0, "unsupported multiplayer selectors remain metadata only without native calls or writes");
        Put<uint32_t>(Header + PHBattleMultiplayerRVA, 0); [engine read:&state frame:Frame() reason:&reason];
        Check(state.multiplayerDisabled, "multiplayer latch cannot be bypassed by selector transitions");
        Put<uint16_t>(Header + PHSaveRVA, 0); [engine read:&state frame:Frame() reason:&reason];
        Put<uint16_t>(Header + PHSaveRVA, 0x0101); [engine read:&state frame:Frame() reason:&reason];
        Check(PHOrbCanPreview(&state), "leaving battle clears latch");
        Put<uint16_t>(Header + PHBattleModeRVA, 7);
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == 0, "mode 7 rejects stale prepared execution");

        Reset(); engine = PHCreateOrbEngine(); [engine read:&state frame:Frame() reason:&reason];
        failWrite = 2;
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason], "restore failure setup");
        Check(![engine finishInput:&reason] && GlobalInput() != Input, "failed pointer restore retains lease and memory");
        Check(reinterpret_cast<uint8_t *>(GlobalInput())[214] == 0, "failed restore cannot leave a held input");
        Check([engine finishInput:&reason] && GlobalInput() == Input, "pointer restoration can recover next frame");
        failWrite = 0; [engine read:&state frame:Frame() reason:&reason];
        [engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason];
        uintptr_t replacement = reinterpret_cast<uintptr_t>(newInput.data()); Put<uintptr_t>(Header + PHBattleInputRVA, replacement);
        Check(![engine finishInput:&reason] && GlobalInput() == replacement, "new game manager never overwritten by stale restoration");

        Reset(); codeValid = false; engine = PHCreateOrbEngine();
        Check(![engine read:&state frame:Frame() reason:&reason] && writes == 0, "wrong target rejects native access");
        SkillNativeTests();
        SurvivalNativeTests();
        OrbEffectNativeTests();
        ThornNativeTests();
        ReadIssueNativeTests();
        DiagonalNativeTests();
        SkillWindowNativeTests();
        std::printf("Orb native tests: %s (memory fixtures, input ownership, skill gates, enemy effects and restoration faults)\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
