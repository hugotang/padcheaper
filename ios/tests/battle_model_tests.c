#include "PHBattleModel.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

static uint32_t rol32(uint32_t value, unsigned shift) { return (value << shift) | (value >> (32 - shift)); }

#include "battle_script_model_tests.inc"

int main(void) {
    TestBattleScripts();
    uint16_t target = 0;
    Check(PHBattleParseTarget("12345", &target) && target == 12345, "ordinary ID parses");
    Check(PHBattleParseTarget("65435", &target) && target == 65435, "highest representable encyclopedia number parses");
    Check(PHBattleParseTarget("00001", &target) && target == 1, "leading zeroes are deterministic");
    Check(PHBattleParseTarget("9900", &target) && target == 9900, "encyclopedia No.9900 is not a reserved native ID");
    const char *invalid[] = {"", "0", "65436", "65535", "65536", "-1", "+2", "1.0", " 1", "1 ", "1e2", "123456", "１２３"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        target = 77;
        Check(!PHBattleParseTarget(invalid[i], &target) && target == 77, "malformed or overflowing number cannot change output");
    }
    Check(!PHBattleParseTarget(NULL, &target), "null input is rejected");
    const uint16_t numberPairs[][2] = {
        {1, 1}, {9899, 9899}, {9900, 10000}, {9999, 10099}, {10000, 10100},
        {14138, 14238}, {14238, 14338}, {65435, 65535}
    };
    for (size_t i = 0; i < sizeof(numberPairs) / sizeof(numberPairs[0]); ++i) {
        uint16_t nativeID = 0, number = 0;
        Check(PHBattleCatalogIDForNumber(numberPairs[i][0], &nativeID) && nativeID == numberPairs[i][1],
              "encyclopedia numbers map to native IDs at the reserved gap and reported high numbers");
        Check(PHBattleNumberForCatalogID(numberPairs[i][1], &number) && number == numberPairs[i][0],
              "member presentation uses the inverse encyclopedia mapping");
    }
    const uint32_t invalidNative[] = {0, 9900, 9950, 9999, 65536, 10000000, UINT32_MAX};
    for (size_t i = 0; i < sizeof(invalidNative) / sizeof(invalidNative[0]); ++i) {
        target = 77;
        Check(!PHBattleNumberForCatalogID(invalidNative[i], &target) && target == 77,
              "reserved or encoded native IDs never produce a misleading encyclopedia number");
    }
    target = 77;
    Check(!PHBattleCatalogIDForNumber(UINT32_MAX, &target) && target == 77 &&
          !PHBattleCatalogIDForNumber(1, NULL) && !PHBattleNumberForCatalogID(1, NULL),
          "mapping rejects overflow and missing output without writes");
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
    b = a; ++b.sourceMode;
    Check(!PHBattleSameIdentity(&a, &b), "source changes invalidate the native transaction identity");
    b = a; ++b.multiplayer;
    Check(!PHBattleSameIdentity(&a, &b), "multiplayer changes invalidate the native transaction identity");
    PHBattleMember m = {.active = true, .readable = true, .sourceSlot = 0, .baseID = 100, .assistID = 200};
    PHBattleMember n = m;
    n.formID = 300;
    Check(PHBattleSameSource(&m, &n), "form change preserves source identity");
    n.sourceSlot = 1;
    Check(!PHBattleSameSource(&m, &n), "leader/member exchange changes source identity");
    n = m; n.sourceTeam = 1;
    Check(!PHBattleSameSource(&m, &n) && !PHBattleSameOrigin(&m, &n),
          "identical characters in different stored teams have distinct origins");
    PHBattleState state = {.present = true, .protectedValuesValid = true, .input = {.valid = true},
                           .digestKnown = true, .digestMatches = true, .hpMatches = true, .phase = 2};
    Check(PHBattleStateGate(&state) == PHBattleGateReady, "ordinary idle battle is eligible");
    state.mode = 12;
    Check(PHBattleStateGate(&state) == PHBattleGateReady, "reported category 12 solo entry is eligible");
    state.mode = 1;
    Check(PHBattleStateGate(&state) == PHBattleGateReady, "reported category 1 solo entry is eligible");
    for (uint32_t mode = 0; mode <= UINT16_MAX; ++mode) {
        state.mode = (int16_t)mode;
        Check(PHBattleStateGate(&state) == ((mode == 7 || mode == 10 || mode == 11) ? PHBattleGateMultiplayer : PHBattleGateReady),
            "co-op and competitive categories never authorize battle writes even with a zero selector");
    }
    state.mode = 3;
    state.policy = (PHBattlePolicy){.configured = true, .modes = {.count = 3, .values = {0,1,12}},
        .sources = {.count = 1, .values = {0}}, .multiplayer = {.count = 1, .values = {0}}};
    Check(PHBattleStateGate(&state) == PHBattleGateMode, "an optional mode list limits single-player writes");
    state.policy.modes.allowAll = true; state.sourceMode = 3;
    Check(PHBattleStateGate(&state) == PHBattleGateMode, "source filtering remains independent in single player");
    state.policy.sources.values[0] = 3;
    Check(PHBattleStateGate(&state) == PHBattleGateReady, "configured solo sources retain their normal checks");
    state.policy.sources.count = PHBattleFilterCapacity + 1;
    Check(PHBattleStateGate(&state) == PHBattleGateMode, "invalid filter capacity fails closed");
    state.policy = (PHBattlePolicy){0};
    state.mode = 0; state.sourceMode = 1;
    Check(PHBattleStateGate(&state) == PHBattleGateMode, "scripted source is refused");
    state.sourceMode = 0; state.phase = 43;
    Check(PHBattleStateGate(&state) == PHBattleGatePhase, "animation phase is refused");
    state.phase = 2; state.input.rawActiveMask = 1;
    Check(PHBattleStateGate(&state) == PHBattleGateInput, "active input has its own gate when phase is already 2");
    state.input.rawActiveMask = 0; state.input.valid = false;
    Check(PHBattleStateGate(&state) == PHBattleGateInputUnavailable, "unreadable input cannot be mistaken for idle");
    state.input.valid = true; state.dragActive = true;
    Check(PHBattleStateGate(&state) == PHBattleGateDrag, "native orb drag blocks even between touch events");
    state.dragActive = false; state.auditFlags = 0x10000;
    Check(PHBattleStateGate(&state) == PHBattleGateAudit, "existing audit failure is preserved and blocks writes");
    state.auditFlags = 0; state.hpMatches = false;
    Check(PHBattleStateGate(&state) == PHBattleGateHP, "HP shadow failure is refused");
    state.hpMatches = true; state.digestMatches = false;
    Check(PHBattleStateGate(&state) == PHBattleGateDigest, "member digest failure is refused");
    state.digestMatches = true; state.phase = 0;
    Check(PHBattleEntryGate(&state) == PHBattleGatePhase, "phase zero alone cannot recharge an entry");
    state.entryWindow = true;
    Check(PHBattleEntryGate(&state) == PHBattleGateReady && PHBattleStateGate(&state) == PHBattleGatePhase,
          "fresh entry uses a separate gate while manual writes still require phase two");
    state.input.rawActiveMask = 1;
    Check(PHBattleEntryGate(&state) == PHBattleGateInput, "entry waits for the dungeon selection touch to finish");
    state.input.rawActiveMask = 0; state.auditFlags = 0x10000;
    Check(PHBattleEntryGate(&state) == PHBattleGateAudit, "entry preserves all normal audit checks");
    state.auditFlags = 0;
    PHBattleState solo = state;
    for (unsigned filters = 0; filters < 3; ++filters) for (uint32_t multi = 0; multi <= 5; ++multi)
    for (unsigned phase = 0; phase <= 48; ++phase) {
        state = solo; state.phase = phase; state.mode = multi ? 12 : 7; state.multiplayer = multi;
        if (filters) state.policy = (PHBattlePolicy){.configured = true, .modes = {.allowAll = true},
            .sources = {.allowAll = true}, .multiplayer = {.allowAll = true}};
        if (filters == 2) {
            state.policy.multiplayer.allowAll = false;
            state.policy.multiplayer.count = 1; state.policy.multiplayer.values[0] = multi;
        }
        state.teamBanksValid = state.turnInfoValid = true; state.teamCount = 3; state.teamSize = 6;
        state.teamBanks[0] = 10; state.teamBanks[1] = 20; state.teamBanks[2] = 30;
        for (unsigned player = 0; player < 3; ++player) {
            state.playerTeam = state.currentTeam = state.turnTeam = (uint8_t)player; state.turnSerial = player;
            Check(PHBattleStateGate(&state) == PHBattleGateMultiplayer && PHBattleEntryGate(&state) == PHBattleGateMultiplayer,
                "filters, valid banks, host/guest seats, own turns and entry phases cannot enable multiplayer writes");
        }
        state.protectedValuesValid = false; state.teamBanksValid = false;
        Check(PHBattleStateGate(&state) == PHBattleGateMultiplayer,
            "multiplayer disablement is reported before temporary protected-value or layout errors");
    }
    for (int16_t mode = 10; mode <= 11; ++mode) {
        state = solo; state.mode = mode; state.multiplayer = 0;
        state.policy = (PHBattlePolicy){.configured = true, .modes = {.allowAll = true},
            .sources = {.allowAll = true}, .multiplayer = {.allowAll = true}};
        Check(PHBattleStateGate(&state) == PHBattleGateMultiplayer && PHBattleEntryGate(&state) == PHBattleGateMultiplayer,
            "competitive selector reset rejects battle mutations under all legacy filters");
    }
    state = solo; state.multiplayerDisabled = true;
    Check(PHBattleEntryGate(&state) == PHBattleGateMultiplayer, "a detected multiplayer battle remains disabled after selector reset");
    state.present = false;
    Check(PHBattleEntryGate(&state) == PHBattleGateAbsent, "leaving battle permits the controller to clear its multiplayer latch");
    state = solo; state.phase = 46;
    Check(PHBattleEntryGate(&state) == PHBattleGatePhase, "phase 46 never broadens single-player entry eligibility");
    printf("Battle model tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
