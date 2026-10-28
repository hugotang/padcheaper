#import <Foundation/Foundation.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/PHOrbNative.mm"

static unsigned failures, writes, failWrite, guardChecks;
static bool codeValid = true;
static std::vector<uint8_t> guest(0x3000000), implBytes(0xc4000), realInput(PHOrbInputBytes), newInput(PHOrbInputBytes);
static const uintptr_t Header = reinterpret_cast<uintptr_t>(guest.data());
static const uintptr_t Impl = reinterpret_cast<uintptr_t>(implBytes.data());
static const uintptr_t Input = reinterpret_cast<uintptr_t>(realInput.data());
static void Check(bool ok, const char *message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); ++failures; } }
static bool Range(uintptr_t address, size_t length) {
    for (auto *region : {&guest, &implBytes, &realInput, &newInput}) {
        auto base = reinterpret_cast<uintptr_t>(region->data());
        if (address >= base && address - base <= region->size() && length <= region->size() - (address - base)) return true;
    }
    return false;
}
BOOL PHMemoryHasProtection(uintptr_t address, size_t length, vm_prot_t required, vm_prot_t forbidden) {
    return Range(address, length);
}
BOOL PHReadMemory(uintptr_t address, void *output, size_t length) {
    if (!Range(address, length)) return NO;
    std::memcpy(output, reinterpret_cast<void *>(address), length); return YES;
}
BOOL PHWriteData(uintptr_t address, const void *input, size_t length) {
    ++writes;
    if (writes == failWrite || address != Header + PHBattleInputRVA || length != sizeof(uintptr_t)) return NO;
    std::memcpy(reinterpret_cast<void *>(address), input, length); return YES;
}
BOOL PHValidateGuest(const mach_header_64 *header, NSString **reason) { ++guardChecks; return codeValid; }
BOOL PHAddressIsGuestCode(uintptr_t address, const mach_header_64 *header) { return address >= Header && address < Header + 0x1110000; }
template<typename T> static void Put(uintptr_t address, T value) { std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(value)); }
static void Protected(uintptr_t address, uint32_t value, uint32_t salt) {
    uint32_t raw[4] = {uint32_t((value << 5) | (value >> 27)) ^ salt, 0, value ^ 17u ^ 23u, value};
    std::memcpy(reinterpret_cast<void *>(address), raw, sizeof(raw));
}
static PHBattleFrame Frame() { PHBattleFrame frame{}; frame.header = Header; frame.player = 42; frame.epoch = 1; frame.ready = frame.enabled = true; return frame; }
static void Reset() {
    std::fill(guest.begin(), guest.end(), 0); std::fill(implBytes.begin(), implBytes.end(), 0); std::fill(realInput.begin(), realInput.end(), 0);
    writes = failWrite = guardChecks = 0; codeValid = true;
    const auto save = Header + PHSaveRVA, game = Header + PHGameMainRVA;
    Put<uint16_t>(save, 0x0101); Put<uint32_t>(save + PHPlayerIDOffset, 42);
    Put<uintptr_t>(Header + PHSceneTableRVA + 24, game); Put<uintptr_t>(game + 0x70, Impl);
    Put<uintptr_t>(Header + PHBattleImplRVA, Impl); Put<uintptr_t>(Header + PHBattleInputRVA, Input);
    Put<uint16_t>(save + PHBattleDungeonOffset, 1); Put<uint16_t>(save + PHBattleDungeonOffset + 2, 2);
    Put<uint8_t>(Header + PHBattleEntryHashRVA, 1);
    Put<uint32_t>(Header + PHBattleKey1RVA, 17); Put<uint32_t>(Header + PHBattleKey2RVA, 23);
    Protected(Impl + PHBattlePhaseOffset, 2, 0x766cfc6d); Protected(Impl + PHBattleWaveOffset, 0, 0x766cfc6d);
    Protected(Impl + PHOrbFixedStartOffset, 255, 0x6a875a8b);
    Put<uint8_t>(game + 88, 6); Put<uint8_t>(game + 89, 5); Put<uint8_t>(game + 92, 3); Put<uint8_t>(game + 93, 1);
    Put<uint16_t>(Impl + PHOrbRemainingOffset, 300); Put<uint8_t>(Header + PHBattleScaleRVA, 60);
    Put<float>(Input + 2664, 0.5); Put<float>(Input + 2668, 0.1);
    for (const auto &guard : PHOrbTextGuards) std::memcpy(reinterpret_cast<void *>(Header + guard.rva), guard.bytes, 64);
    for (unsigned row = 0; row < 5; ++row) for (unsigned column = 0; column < 6; ++column) {
        unsigned nativeIndex = (4 - row) * 16 + column;
        uintptr_t token = Impl + PHOrbPoolOffset + nativeIndex * 172;
        Put<uintptr_t>(Impl + PHOrbTableOffset + nativeIndex * 8, token);
        Put<uint8_t>(token, uint8_t((row * 6 + column) % 6));
        Put<uint16_t>(token + 18, 0);
        Put<int16_t>(token + 24, int16_t(57 + column * 105)); Put<int16_t>(token + 26, int16_t(400 + row * 105));
        Put<int16_t>(token + 28, int16_t(57 + column * 105)); Put<int16_t>(token + 30, int16_t(400 + row * 105));
    }
}
static uintptr_t GlobalInput() { return Load<uintptr_t>(guest.data() + PHBattleInputRVA); }
// Only the portions of the verified input update required by these fixtures.
static void NativeInputFrame() {
    auto p = reinterpret_cast<uint8_t *>(GlobalInput());
    std::memcpy(p + 1320, p, 264); p[1320 + 219] = p[214]; p[2641] = p[2640];
    std::memcpy(p + 1320 + 12, p + 4, 8); p[213] = 0;
}

int main() {
    @autoreleasepool {
        Reset(); id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state), "validated ordinary board readable");
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

        Reset(); engine = PHCreateOrbEngine(); [engine read:&state frame:Frame() reason:&reason];
        Put<uint32_t>(state.board.tokens[0] + 4, 0x800);
        PHOrbSnapshot changed{}; [engine read:&changed frame:Frame() reason:&reason];
        Check(!PHOrbCanPreview(&changed), "unmodeled block effect rejected");
        Put<uint32_t>(state.board.tokens[0] + 4, 0); Put<uint16_t>(Impl + 739268, 0x401);
        [engine read:&changed frame:Frame() reason:&reason]; Check(!PHOrbCanPreview(&changed), "native unmatchable effect blocks ordinary solver");
        Put<uint16_t>(Impl + 739268, 0); Put<uint16_t>(state.board.tokens[0] + 18, 3);
        [engine read:&changed frame:Frame() reason:&reason]; Check(!PHOrbCanPreview(&changed), "swap animation is not a settled board");
        Put<uint16_t>(state.board.tokens[0] + 18, 0);
        Put<uintptr_t>(Impl + PHOrbTableOffset + 65 * 8, state.board.tokens[0]);
        Check(![engine read:&changed frame:Frame() reason:&reason], "duplicate pool token rejected");

        Reset(); engine = PHCreateOrbEngine(); Put<uint32_t>(Header + PHBattleMultiplayerRVA, 1);
        [engine read:&state frame:Frame() reason:&reason];
        Check(state.multiplayerDisabled && guardChecks == 0 && writes == 0, "multiplayer is metadata only with no native calls or input writes");
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
        std::printf("Orb native tests: %s (memory fixtures, input ownership, multiplayer, effects and restoration faults)\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
