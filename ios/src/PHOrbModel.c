#include "PHOrbModel.h"
#include <math.h>
#include <string.h>

bool PHOrbDimensionsSupported(unsigned width, unsigned height) {
    return (width == 6 && height == 5) || (width == 7 && height == 6) || (width == 5 && height == 4);
}

bool PHOrbBoardValid(const PHOrbBoard *board) {
    if (!board || !PHOrbDimensionsSupported(board->width, board->height)) return false;
    for (unsigned i = 0; i < board->width * board->height; ++i) {
        float enhancement;
        memcpy(&enhancement, &board->enhancement[i], sizeof(enhancement));
        // Lock blocks native color-changing skills, not ordinary swaps or
        // matching. Preserve it with the orb. Other effects remain rejected.
        if (board->types[i] > 5 || (board->flags[i] & ~(PHOrbFlagSwapping | PHOrbFlagLocked)) || !isfinite(enhancement) ||
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
        a->inputManager == b->inputManager && a->minErase == b->minErase && a->freeMovement == b->freeMovement &&
        a->board.width == b->board.width && a->board.height == b->board.height;
}

bool PHOrbSameLeaders(const PHOrbSnapshot *a, const PHOrbSnapshot *b) {
    return a && b && a->leadersValid == b->leadersValid &&
        (!a->leadersValid || (a->leaderNumbers[0] == b->leaderNumbers[0] && a->leaderNumbers[1] == b->leaderNumbers[1]));
}

bool PHOrbSameAutomation(const PHOrbSnapshot *a, const PHOrbSnapshot *b) {
    if (!PHOrbSameTurn(a, b) || !PHOrbSameLeaders(a, b) || a->phase != b->phase ||
        a->moves != b->moves || a->obstacles != b->obstacles || a->ordinary != b->ordinary ||
        !PHOrbSameBoard(&a->board, &b->board) || !PHOrbSamePlayerState(a, b)) return false;
    const PHOrbAutomationState *x = &a->automation, *y = &b->automation;
    if (!x->valid || !y->valid || x->window != y->window || x->busy != y->busy ||
        x->skillUses != y->skillUses || x->enemiesKnown != y->enemiesKnown || x->boardKnown != y->boardKnown ||
        x->enemyEffects != y->enemyEffects || x->playerBypass != y->playerBypass ||
        x->activeEnemies != y->activeEnemies) return false;
    for (unsigned i = 0; i < 6; ++i) {
        const PHOrbSkillMember *m = &x->members[i], *n = &y->members[i];
        if (m->active != n->active || m->number != n->number || m->charge != n->charge ||
            m->state != n->state || m->bound != n->bound || m->overridden != n->overridden) return false;
    }
    return true;
}

bool PHOrbSamePlayerState(const PHOrbSnapshot *a, const PHOrbSnapshot *b) {
    if (!a || !b) return false;
    const PHOrbAutomationState *x = &a->automation, *y = &b->automation;
    return x->hpKnown == y->hpKnown && x->hp == y->hp && x->maximumHP == y->maximumHP &&
        x->statusKnown == y->statusKnown && x->playerEffects == y->playerEffects &&
        x->awakeningRestricted == y->awakeningRestricted;
}

uint8_t PHOrbBoardObstacles(const PHOrbSnapshot *state) {
    if (!state) return 0;
    uint8_t result = state->obstacles;
    if (!PHOrbDimensionsSupported(state->board.width, state->board.height)) return result;
    for (unsigned i = 0; i < state->board.width * state->board.height; ++i) {
        float enhancement;
        memcpy(&enhancement, &state->board.enhancement[i], sizeof(enhancement));
        if (isfinite(enhancement) && enhancement < 0) result |= PHOrbObstacleWeakened;
        if (state->board.types[i] > 5) result |= PHOrbObstacleSpecialOrbs;
        if (state->board.flags[i] & PHOrbFlagLocked) result |= PHOrbObstacleLocked;
        if (state->board.flags[i] & ~(PHOrbFlagSwapping | PHOrbFlagLocked)) result |= PHOrbObstacleOrbEffect;
    }
    return result;
}

bool PHOrbGoalsValid(const PHOrbGoalSet *goals) {
    if (!goals || goals->count > PHOrbMaxGoals) return false;
    for (unsigned i = 0; i < goals->count; ++i) {
        const PHOrbGoal *g = &goals->items[i];
        if (g->kind > PHOrbGoalFour || !g->count || g->count > PHOrbMaxCells / 3) return false;
        if (g->kind == PHOrbGoalCombo) {
            if (g->colors || g->length) return false;
        } else {
            if (!g->colors || g->colors > 63) return false;
            if (g->kind == PHOrbGoalColors) {
                unsigned colors = 0;
                for (unsigned bit = 0; bit < 6; ++bit) colors += (g->colors >> bit) & 1u;
                if (g->count > colors) return false;
            }
            if (g->kind == PHOrbGoalMatch) {
                if (g->length < 3 || g->length > PHOrbMaxCells) return false;
            } else if (g->length) return false;
        }
        // Repeated requirements use a count, avoiding ambiguous double counting.
        for (unsigned j = 0; j < i; ++j) if (goals->items[j].kind == g->kind &&
            goals->items[j].colors == g->colors && goals->items[j].length == g->length) return false;
    }
    return true;
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
        PHOrbBoardValid(&state->board);
}
