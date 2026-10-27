#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uintptr_t header, game, impl;
    uint32_t player;
    uint64_t epoch;
    uint16_t dungeon, floor;
    uint8_t entryHash[32];
} PHBattleIdentity;

typedef struct {
    bool active, readable;
    int8_t sourceSlot, scriptSlot;
    uint16_t baseID, assistID, formID, temporaryID, temporaryTurns, charge;
    uint32_t catalogID;
    uint8_t flags, radius;
    uint16_t previousID;
} PHBattleMember;

typedef struct {
    PHBattleIdentity identity;
    bool present, protectedValuesValid, inputQuiet, digestKnown, digestMatches, hpMatches;
    int16_t mode;
    uint16_t sourceMode;
    uint32_t multiplayer, phase, auditFlags, catalogCount, hp;
    uintptr_t catalog;
    PHBattleMember members[6];
} PHBattleState;

typedef struct {
    uintptr_t header;
    uint32_t player;
    uint64_t epoch;
    bool ready, enabled;
} PHBattleFrame;

typedef enum {
    PHBattleGateReady, PHBattleGateAbsent, PHBattleGateProtected,
    PHBattleGateMode, PHBattleGateBusy, PHBattleGateAudit,
    PHBattleGateHP, PHBattleGateDigest
} PHBattleGate;

typedef enum {
    PHBattleMutationApplied, PHBattleMutationRolledBack, PHBattleMutationRejected, PHBattleMutationUncertain
} PHBattleMutationResult;

bool PHBattleParseTarget(const char *text, uint16_t *value);
bool PHBattleDecodeProtected(const uint32_t raw[4], uint32_t key1, uint32_t key2,
                            uint32_t salt, uint32_t *value);
bool PHBattleSameIdentity(const PHBattleIdentity *a, const PHBattleIdentity *b);
bool PHBattleSameSource(const PHBattleMember *a, const PHBattleMember *b);
PHBattleGate PHBattleStateGate(const PHBattleState *state);

#ifdef __cplusplus
}
#endif
