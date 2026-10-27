#include "PHBattleModel.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

static uint32_t rol32(uint32_t value, unsigned shift) { return (value << shift) | (value >> (32 - shift)); }

int main(void) {
    uint16_t target = 0;
    Check(PHBattleParseTarget("12345", &target) && target == 12345, "ordinary ID parses");
    Check(PHBattleParseTarget("65535", &target) && target == 65535, "u16 bound parses");
    Check(PHBattleParseTarget("00001", &target) && target == 1, "leading zeroes are deterministic");
    const char *invalid[] = {"", "0", "65536", "9900", "9999", "-1", "+2", "1.0", " 1", "1 ", "1e2", "123456", "１２３"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        target = 77;
        Check(!PHBattleParseTarget(invalid[i], &target) && target == 77, "malformed/reserved ID cannot change output");
    }
    Check(!PHBattleParseTarget(NULL, &target), "null input is rejected");
    uint32_t key1 = 0x11223344u, key2 = 0x55667788u, value = 0;
    const uint32_t samples[] = {0, 2, 255, 65535, 0x8085a6beu, UINT32_MAX};
    const uint32_t salts[] = {0x6a875a8bu, 0x766cfc6du};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        for (size_t j = 0; j < 2; ++j) {
            // Independently build the three replicas following the IDA setter.
            uint32_t raw[] = {rol32(samples[i], 5) ^ salts[j], rol32(0x89abcdefu, 13),
                              samples[i] ^ key1 ^ key2, samples[i] ^ 0x89abcdefu};
            Check(PHBattleDecodeProtected(raw, key1, key2, salts[j], &value) && value == samples[i], "protected replicas agree");
            for (size_t word = 0; word < 4; ++word) {
                raw[word] ^= 1;
                value = 77;
                Check(!PHBattleDecodeProtected(raw, key1, key2, salts[j], &value) && value == 77,
                      "one corrupt replica is refused even when a native majority exists");
                raw[word] ^= 1;
            }
            Check(!PHBattleDecodeProtected(raw, key1 ^ 1, key2, salts[j], &value), "changed key is refused");
            Check(!PHBattleDecodeProtected(raw, key1, key2, salts[1 - j], &value), "u32 and u16 salts cannot be interchanged");
        }
    }
    PHBattleIdentity a = {.header = 1, .game = 2, .impl = 3, .player = 4, .epoch = 5, .dungeon = 6, .floor = 7, .entryHash = {1}};
    PHBattleIdentity b = a;
    Check(PHBattleSameIdentity(&a, &b), "same battle identity matches");
    b.entryHash[31] = 1;
    Check(!PHBattleSameIdentity(&a, &b), "different entry hash is a new battle");
    b = a; ++b.epoch;
    Check(!PHBattleSameIdentity(&a, &b), "relogin invalidates battle identity");
    PHBattleMember m = {.active = true, .readable = true, .sourceSlot = 0, .baseID = 100, .assistID = 200};
    PHBattleMember n = m;
    n.formID = 300;
    Check(PHBattleSameSource(&m, &n), "form change preserves source identity");
    n.sourceSlot = 1;
    Check(!PHBattleSameSource(&m, &n), "leader/member exchange changes source identity");
    PHBattleState state = {.present = true, .protectedValuesValid = true, .inputQuiet = true,
                           .digestKnown = true, .digestMatches = true, .hpMatches = true, .phase = 2};
    Check(PHBattleStateGate(&state) == PHBattleGateReady, "ordinary idle battle is eligible");
    state.multiplayer = 2;
    Check(PHBattleStateGate(&state) == PHBattleGateMode, "disconnected multiplayer is not treated as single-player");
    state.multiplayer = 0; state.mode = 7;
    Check(PHBattleStateGate(&state) == PHBattleGateMode, "special mode is refused");
    state.mode = 0; state.sourceMode = 1;
    Check(PHBattleStateGate(&state) == PHBattleGateMode, "scripted source is refused");
    state.sourceMode = 0; state.phase = 43;
    Check(PHBattleStateGate(&state) == PHBattleGateBusy, "animation phase is refused");
    state.phase = 2; state.inputQuiet = false;
    Check(PHBattleStateGate(&state) == PHBattleGateBusy, "active input is refused");
    state.inputQuiet = true; state.auditFlags = 0x10000;
    Check(PHBattleStateGate(&state) == PHBattleGateAudit, "existing audit failure is preserved and blocks writes");
    state.auditFlags = 0; state.hpMatches = false;
    Check(PHBattleStateGate(&state) == PHBattleGateHP, "HP shadow failure is refused");
    state.hpMatches = true; state.digestMatches = false;
    Check(PHBattleStateGate(&state) == PHBattleGateDigest, "member digest failure is refused");
    printf("Battle model tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
