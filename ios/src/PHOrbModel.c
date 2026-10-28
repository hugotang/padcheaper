#include "PHOrbModel.h"
#include <math.h>
#include <string.h>

bool PHOrbBoardValid(const PHOrbBoard *board) {
    if (!board || !((board->width == 6 && board->height == 5) ||
        (board->width == 7 && board->height == 6) || (board->width == 5 && board->height == 4))) return false;
    for (unsigned i = 0; i < board->width * board->height; ++i) {
        float enhancement;
        memcpy(&enhancement, &board->enhancement[i], sizeof(enhancement));
        // 0x4 is cleared by the native swap animation. All other flags are
        // retained in the snapshot and rejected until their rules are modeled.
        if (board->types[i] > 5 || (board->flags[i] & ~4u) || !isfinite(enhancement) ||
            enhancement < 0 || enhancement > 1) return false;
    }
    return true;
}

bool PHOrbSameBoard(const PHOrbBoard *a, const PHOrbBoard *b) {
    if (!a || !b || a->width != b->width || a->height != b->height ||
        a->width * a->height > PHOrbMaxCells) return false;
    for (unsigned i = 0; i < a->width * a->height; ++i) {
        if (a->types[i] != b->types[i] || a->tokens[i] != b->tokens[i] ||
            (a->flags[i] & ~4u) != (b->flags[i] & ~4u) || a->enhancement[i] != b->enhancement[i] ||
            a->x[i] != b->x[i] || a->y[i] != b->y[i]) return false;
    }
    return true;
}

bool PHOrbSameTurn(const PHOrbSnapshot *a, const PHOrbSnapshot *b) {
    return a && b && PHBattleSameIdentity(&a->identity, &b->identity) && a->wave == b->wave &&
        a->inputManager == b->inputManager && a->minErase == b->minErase && a->freeMovement == b->freeMovement;
}

bool PHOrbAdjacent(unsigned a, unsigned b, unsigned width, unsigned height) {
    if (!width || !height || width * height > PHOrbMaxCells || a >= width * height || b >= width * height) return false;
    return (a / width == b / width && (a + 1 == b || b + 1 == a)) ||
        (a % width == b % width && (a + width == b || b + width == a));
}

bool PHOrbReplay(const PHOrbBoard *board, const PHOrbRoute *route, unsigned through, PHOrbBoard *result) {
    if (!PHOrbBoardValid(board) || !route || !result || route->count < 1 ||
        route->count > PHOrbMaxSteps + 1 || through >= route->count || route->cells[0] >= board->width * board->height) return false;
    *result = *board;
    for (unsigned i = 1; i <= through; ++i) {
        unsigned a = route->cells[i - 1], b = route->cells[i];
        if (!PHOrbAdjacent(a, b, board->width, board->height)) return false;
#define PH_ORB_SWAP(field, type) do { type temp = result->field[a]; result->field[a] = result->field[b]; result->field[b] = temp; } while (0)
        PH_ORB_SWAP(types, uint8_t);
        PH_ORB_SWAP(flags, uint32_t);
        PH_ORB_SWAP(enhancement, uint32_t);
        PH_ORB_SWAP(tokens, uintptr_t);
#undef PH_ORB_SWAP
    }
    return true;
}

bool PHOrbCanPreview(const PHOrbSnapshot *state) {
    return state && state->present && state->valid && state->settled && state->ordinary &&
        !state->multiplayerDisabled && PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) &&
        state->identity.sourceMode == 0 && state->phase == 2 && !state->dragged && PHInputIsQuiet(&state->input) &&
        state->board.width == 6 && state->board.height == 5 && PHOrbBoardValid(&state->board);
}
