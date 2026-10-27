#include "PHBattleModel.h"
#include <string.h>

bool PHBattleCatalogIDForNumber(uint32_t number, uint16_t *catalogID) {
    // PAD 23.6.0 numeric search: 0x1004CADFC adds 100 above No.9899.
    if (!catalogID || !number || number > UINT16_MAX - 100u) return false;
    *catalogID = (uint16_t)(number >= 9900u ? number + 100u : number);
    return true;
}

bool PHBattleNumberForCatalogID(uint32_t catalogID, uint16_t *number) {
    if (!number || !catalogID || catalogID > UINT16_MAX ||
        (catalogID >= 9900u && catalogID <= 9999u)) return false;
    *number = (uint16_t)(catalogID >= 10000u ? catalogID - 100u : catalogID);
    return true;
}

bool PHBattleParseTarget(const char *text, uint16_t *value) {
    if (!text || !value || !*text) return false;
    uint32_t result = 0;
    size_t length = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (++length > 5 || *p < '0' || *p > '9') return false;
        result = result * 10 + *p - '0';
    }
    uint16_t catalogID;
    if (!PHBattleCatalogIDForNumber(result, &catalogID)) return false;
    *value = (uint16_t)result;
    return true;
}

static uint32_t ror32(uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32 - shift));
}

bool PHBattleDecodeProtected(const uint32_t raw[4], uint32_t key1, uint32_t key2,
                            uint32_t salt, uint32_t *value) {
    if (!raw || !value) return false;
    uint32_t a = ror32(raw[0] ^ salt, 5);
    uint32_t b = raw[2] ^ key1 ^ key2;
    uint32_t c = raw[3] ^ ror32(raw[1], 13);
    // Native getters use a majority fallback. Mutations require unanimity.
    if (a != b || a != c) return false;
    *value = a;
    return true;
}

bool PHBattleSameIdentity(const PHBattleIdentity *a, const PHBattleIdentity *b) {
    return a && b && a->header == b->header && a->game == b->game && a->impl == b->impl &&
           a->player == b->player && a->epoch == b->epoch && a->dungeon == b->dungeon &&
           a->floor == b->floor && a->sourceMode == b->sourceMode &&
           a->multiplayer == b->multiplayer && memcmp(a->entryHash, b->entryHash, sizeof(a->entryHash)) == 0;
}

bool PHBattleSameSource(const PHBattleMember *a, const PHBattleMember *b) {
    return a && b && a->active && b->active && a->readable && b->readable &&
           a->sourceSlot == b->sourceSlot && a->scriptSlot == b->scriptSlot &&
           a->baseID == b->baseID && a->assistID == b->assistID;
}

PHBattleGate PHBattleStateGate(const PHBattleState *state) {
    if (!state || !state->present) return PHBattleGateAbsent;
    if (!state->protectedValuesValid) return PHBattleGateProtected;
    if (!PHBattlePolicyAllows(&state->policy, state->mode, state->sourceMode, state->multiplayer))
        return PHBattleGateMode;
    if (!PHBattleUsesPrimaryTeam(state->multiplayer)) return PHBattleGateLayout;
    if (state->phase != 2) return PHBattleGatePhase;
    if (!state->input.valid) return PHBattleGateInputUnavailable;
    if (!PHInputIsQuiet(&state->input)) return PHBattleGateInput;
    if (state->dragActive) return PHBattleGateDrag;
    if (state->auditFlags & 0x1e000u) return PHBattleGateAudit;
    if (!state->hpMatches) return PHBattleGateHP;
    if (!state->digestKnown || !state->digestMatches) return PHBattleGateDigest;
    return PHBattleGateReady;
}
