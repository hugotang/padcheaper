#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "PHInputModel.h"
#include "PHBattlePreset.h"
#include "PHBattlePolicy.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PHBattleTeamCapacity = 8 };

typedef struct {
    uintptr_t header, game, impl;
    uint32_t player;
    uint64_t epoch;
    uint16_t dungeon, floor;
    uint8_t entryHash[32];
    int16_t entryMode;
    uint16_t sourceMode;
    uint32_t multiplayer;
} PHBattleIdentity;

typedef struct {
    bool active, readable;
    int8_t sourceSlot, scriptSlot;
    uint8_t sourceTeam;
    uint16_t baseID, assistID, formID, temporaryID, temporaryTurns, charge;
    uint32_t catalogID;
    uint8_t flags, radius;
    uint16_t previousID;
    uint64_t providerToken;
    // Legacy diagnostic field; multiplayer profiles are no longer retained.
    uint64_t rebindToken;
    uint16_t level, skillLevel;
    uint8_t extra[20], assist[8];
} PHBattleMember;

typedef struct {
    PHBattleIdentity identity;
    bool present, protectedValuesValid, dragActive, digestKnown, digestMatches, hpMatches;
    // Once multiplayer is observed, writes stay disabled until leaving battle.
    bool multiplayerDisabled;
    bool entryWindow, entryChargesValid;
    bool teamBanksValid, turnInfoValid;
    uint8_t teamCount, currentTeam, teamSize, playerTeam, turnTeam;
    uint32_t turnSerial;
    uintptr_t teamBanks[PHBattleTeamCapacity];
    uint32_t teamHP[PHBattleTeamCapacity];
    PHBattleMember storedMembers[PHBattleTeamCapacity][6];
    PHInputState input;
    int16_t mode;
    uint16_t sourceMode;
    uint32_t multiplayer, phase, auditFlags, catalogCount, hp;
    uintptr_t catalog;
    PHBattleMember members[6];
    PHBattlePolicy policy;
} PHBattleState;

typedef struct {
    uintptr_t header;
    uint32_t player;
    uint64_t epoch;
    bool ready, enabled;
    PHBattlePolicy policy;
    bool autoApplyTeam;
} PHBattleFrame;

typedef enum {
    PHBattleGateReady, PHBattleGateAbsent, PHBattleGateMultiplayer, PHBattleGateProtected,
    PHBattleGateMode, PHBattleGateLayout, PHBattleGatePhase, PHBattleGateTurn, PHBattleGateInputUnavailable, PHBattleGateInput,
    PHBattleGateDrag, PHBattleGateAudit,
    PHBattleGateHP, PHBattleGateDigest
} PHBattleGate;

typedef enum {
    PHBattleMutationApplied, PHBattleMutationRolledBack, PHBattleMutationRejected, PHBattleMutationUncertain
} PHBattleMutationResult;

// UI numbers follow the in-game encyclopedia. Member fields and engine calls
// use native catalog IDs; native 9900..9999 are reserved and have no UI number.
bool PHBattleParseTarget(const char *text, uint16_t *number);
bool PHBattleCatalogIDForNumber(uint32_t number, uint16_t *catalogID);
bool PHBattleNumberForCatalogID(uint32_t catalogID, uint16_t *number);
bool PHBattleDecodeProtected(const uint32_t raw[4], uint32_t key1, uint32_t key2,
                            uint32_t salt, uint32_t *value);
bool PHBattleSameIdentity(const PHBattleIdentity *a, const PHBattleIdentity *b);
bool PHBattleSameOrigin(const PHBattleMember *a, const PHBattleMember *b);
bool PHBattleSameSource(const PHBattleMember *a, const PHBattleMember *b);
bool PHBattleIsThreePlayer(const PHBattleState *state);
bool PHBattleIsEntryPhase(const PHBattleState *state);
bool PHBattleTeamLayoutValid(const PHBattleState *state);
bool PHBattleSameTeamBanks(const PHBattleState *a, const PHBattleState *b);
bool PHBattleSameTeamView(const PHBattleState *a, const PHBattleState *b);
PHBattleGate PHBattleStateGate(const PHBattleState *state);
PHBattleGate PHBattleEntryGate(const PHBattleState *state);

#ifdef __cplusplus
}
#endif
