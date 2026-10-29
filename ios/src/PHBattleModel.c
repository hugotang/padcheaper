#include "PHBattleModel.h"
#include <string.h>

PHBattleScriptCheck PHBattleCheckScript(const uint8_t *program, size_t length,
    const uint32_t events[PHBattleEventCount], uint32_t *failureOffset) {
    if (failureOffset) *failureOffset = 0;
    if (!program || !events || !length || length > 1024u * 1024u) return PHBattleScriptMalformed;
    uint32_t roots[PHBattleEventCount + 1] = {0};
    size_t count = 1;
    for (size_t i = 0; i < PHBattleEventCount; ++i) {
        uint32_t offset = events[i];
        if (offset == UINT32_MAX) continue;
        if (offset >= length) {
            if (failureOffset) *failureOffset = offset;
            return PHBattleScriptMalformed;
        }
        size_t at = 0;
        while (at < count && roots[at] < offset) ++at;
        if (at < count && roots[at] == offset) continue;
        memmove(roots + at + 1, roots + at, (count - at) * sizeof(*roots));
        roots[at] = offset; ++count;
    }
    size_t at = 0, nextRoot = 1;
    while (at < length) {
        if (failureOffset) *failureOffset = (uint32_t)at;
        while (nextRoot < count && roots[nextRoot] == at) ++nextRoot;
        uint8_t opcode = program[at];
        if (opcode == 26) return PHBattleScriptPartyCommand;
        size_t end = at + 1;
        if (opcode == 5) {
            // Native text: opcode, uint16 total bytes, two metadata bytes,
            // then an opaque, zero-terminated UTF-8 payload.
            if (length - at < 6) return PHBattleScriptMalformed;
            size_t bytes = program[at + 1] | ((size_t)program[at + 2] << 8);
            if (bytes < 6 || bytes > length - at || program[at + bytes - 1]) return PHBattleScriptMalformed;
            end = at + bytes;
        } else if (opcode != 0 && opcode != 19) {
            if (opcode > 25) return PHBattleScriptMalformed;
            // Other directives have newline-terminated arguments. Reject
            // embedded binary control bytes rather than interpreting them as
            // harmless text and potentially hiding another command.
            while (end < length && program[end] != 10) {
                if (program[end] < 32 && program[end] != 9) return PHBattleScriptMalformed;
                ++end;
            }
            if (end == length) return PHBattleScriptMalformed;
            ++end;
        }
        if (nextRoot < count && roots[nextRoot] < end) {
            if (failureOffset) *failureOffset = roots[nextRoot];
            return PHBattleScriptMalformed;
        }
        if (opcode == 0) {
            if (nextRoot == count) return PHBattleScriptCompatible;
            at = roots[nextRoot++];
        } else at = end;
    }
    return PHBattleScriptMalformed;
}

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
           a->floor == b->floor && a->entryMode == b->entryMode && a->sourceMode == b->sourceMode &&
           a->multiplayer == b->multiplayer && memcmp(a->entryHash, b->entryHash, sizeof(a->entryHash)) == 0;
}

bool PHBattleSameOrigin(const PHBattleMember *a, const PHBattleMember *b) {
    return a && b && a->active && b->active && a->readable && b->readable &&
           a->sourceTeam == b->sourceTeam && a->sourceSlot == b->sourceSlot;
}

bool PHBattleSameSource(const PHBattleMember *a, const PHBattleMember *b) {
    return PHBattleSameOrigin(a, b) && a->scriptSlot == b->scriptSlot &&
           a->baseID == b->baseID && a->assistID == b->assistID;
}

bool PHBattleIsThreePlayer(const PHBattleState *state) {
    return state && state->mode == 7 && state->multiplayer == 1;
}

bool PHBattleIsEntryPhase(const PHBattleState *state) {
    return state && state->phase == 0;
}

bool PHBattleTeamLayoutValid(const PHBattleState *state) {
    if (!state) return false;
    if (PHBattleUsesPrimaryTeam(state->multiplayer)) return true;
    if (!state->teamBanksValid || !state->teamCount || state->teamCount > PHBattleTeamCapacity ||
        state->currentTeam >= state->teamCount || (state->teamSize != 5 && state->teamSize != 6)) return false;
    for (unsigned i = 0; i < state->teamCount; ++i) if (!state->teamBanks[i]) return false;
    if (PHBattleIsThreePlayer(state) && (!state->turnInfoValid || state->teamSize != 6 ||
        state->teamCount < 2 || state->teamCount > 3 || state->playerTeam >= state->teamCount ||
        state->turnSerial > INT32_MAX || state->turnTeam != state->turnSerial % state->teamCount)) return false;
    return true;
}

bool PHBattleSameTeamBanks(const PHBattleState *a, const PHBattleState *b) {
    if (!a || !b || !PHBattleTeamLayoutValid(a) || !PHBattleTeamLayoutValid(b) ||
        a->multiplayer != b->multiplayer || a->mode != b->mode) return false;
    if (PHBattleIsThreePlayer(a) && (a->playerTeam != b->playerTeam || a->turnSerial != b->turnSerial)) return false;
    return PHBattleUsesPrimaryTeam(a->multiplayer) || (a->teamCount == b->teamCount &&
        a->teamSize == b->teamSize && !memcmp(a->teamBanks, b->teamBanks, sizeof(a->teamBanks)));
}

bool PHBattleSameTeamView(const PHBattleState *a, const PHBattleState *b) {
    return PHBattleSameTeamBanks(a, b) && (PHBattleUsesPrimaryTeam(a->multiplayer) || a->currentTeam == b->currentTeam);
}

static PHBattleGate StateGate(const PHBattleState *state, bool entry) {
    if (!state || !state->present) return PHBattleGateAbsent;
    if (state->multiplayerDisabled || !PHBattleIsSinglePlayer(state->mode, state->multiplayer))
        return PHBattleGateMultiplayer;
    if (!state->protectedValuesValid) return PHBattleGateProtected;
    if (!PHBattlePolicyAllows(&state->policy, state->mode, state->sourceMode, state->multiplayer))
        return PHBattleGateMode;
    if (!PHBattleTeamLayoutValid(state)) return PHBattleGateLayout;
    if (entry ? !PHBattleIsEntryPhase(state) || !state->entryWindow : state->phase != 2) return PHBattleGatePhase;
    if (!state->input.valid) return PHBattleGateInputUnavailable;
    if (!PHInputIsQuiet(&state->input)) return PHBattleGateInput;
    if (state->dragActive) return PHBattleGateDrag;
    if (state->auditFlags & 0x1e000u) return PHBattleGateAudit;
    if (!state->hpMatches) return PHBattleGateHP;
    if (!state->digestKnown || !state->digestMatches) return PHBattleGateDigest;
    return PHBattleGateReady;
}

PHBattleGate PHBattleStateGate(const PHBattleState *state) { return StateGate(state, false); }
PHBattleGate PHBattleEntryGate(const PHBattleState *state) { return StateGate(state, true); }
