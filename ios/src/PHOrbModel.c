#include "PHOrbModel.h"
#include <math.h>
#include <string.h>

bool PHOrbDimensionsSupported(unsigned width, unsigned height) {
    return (width == 6 && height == 5) || (width == 7 && height == 6) || (width == 5 && height == 4);
}

bool PHOrbSpinnerSupported(const PHOrbBoard *board) {
    if (!board || !PHOrbDimensionsSupported(board->width, board->height)) return false;
    uint64_t mask = board->spinnerMask;
    if (!mask) return board->spinnerColors == 0;
    if ((mask >> (board->width * board->height)) || !board->spinnerColors || board->spinnerColors >= (1u << PHOrbTypes)) return false;
    for (unsigned i = 0; i < board->width * board->height; ++i) if (mask & (UINT64_C(1) << i)) {
        if (board->types[i] >= PHOrbTypes) return false;
        if (board->flags[i] & PHOrbFlagLocked) continue;
        // Wait for an out-of-cycle orb to enter the cycle. Hazard color changes
        // clear enhancement/combo/nail data natively; accept only already-zero
        // metadata so every enumerated phase preserves the observed token.
        if (!(board->spinnerColors & (1u << board->types[i]))) return false;
        if ((board->spinnerColors & ~63u) && (board->enhancement[i] ||
            (board->flags[i] & (PHOrbFlagCombo | PHOrbFlagNail)))) return false;
    }
    return true;
}

unsigned PHOrbSpinnerVariants(const PHOrbBoard *board) {
    if (!PHOrbSpinnerSupported(board)) return 0;
    bool dynamic = false;
    for (unsigned i = 0; i < board->width * board->height; ++i)
        dynamic |= (board->spinnerMask & (UINT64_C(1) << i)) && !(board->flags[i] & PHOrbFlagLocked);
    if (!dynamic) return 1;
    unsigned count = 0;
    for (unsigned color = 0; color < PHOrbTypes; ++color) count += (board->spinnerColors >> color) & 1u;
    return count;
}

uint8_t PHOrbSpinnerType(const PHOrbBoard *board, unsigned cell, unsigned phase) {
    if (!board || cell >= board->width * board->height || cell >= PHOrbMaxCells) return 255;
    unsigned type = board->types[cell];
    if (!(board->spinnerMask & (UINT64_C(1) << cell)) || (board->flags[cell] & PHOrbFlagLocked)) return (uint8_t)type;
    if (type >= PHOrbTypes || !board->spinnerColors) return 255;
    for (unsigned step = 0; step < phase; ++step) {
        for (unsigned next = 1; next <= PHOrbTypes; ++next) {
            unsigned candidate = (type + next) % PHOrbTypes;
            if (board->spinnerColors & (1u << candidate)) { type = candidate; break; }
        }
    }
    return (uint8_t)type;
}

uint64_t PHOrbBlockedCells(const PHOrbBoard *board) {
    if (!board || !PHOrbDimensionsSupported(board->width, board->height)) return UINT64_MAX;
    uint64_t blocked = board->spinnerMask | board->tapeMask;
    for (unsigned i = 0; i < board->width * board->height; ++i)
        if (board->flags[i] & PHOrbFlagThorn) blocked |= UINT64_C(1) << i;
    return blocked;
}

bool PHOrbCanStartAt(const PHOrbBoard *board, unsigned cell) {
    return board && cell < board->width * board->height && cell < PHOrbMaxCells &&
        !(PHOrbBlockedCells(board) & (UINT64_C(1) << cell)) && (!board->fixedStart || cell + 1 == board->fixedStart);
}

void PHOrbColorCountRange(const PHOrbBoard *board, unsigned color, unsigned *minimum, unsigned *maximum) {
    if (!minimum || !maximum) return;
    *minimum = 0; *maximum = PHOrbMaxCells;
    unsigned variants = PHOrbSpinnerVariants(board);
    if (!variants || color >= PHOrbTypes) return;
    *minimum = PHOrbMaxCells; *maximum = 0;
    for (unsigned phase = 0; phase < variants; ++phase) {
        unsigned count = 0;
        for (unsigned i = 0; i < board->width * board->height; ++i) count += PHOrbSpinnerType(board, i, phase) == color;
        if (count < *minimum) *minimum = count;
        if (count > *maximum) *maximum = count;
    }
}

bool PHOrbBoardValid(const PHOrbBoard *board) {
    if (!PHOrbSpinnerSupported(board)) return false;
    const unsigned size = board->width * board->height;
    if (((board->tapeMask | board->cloudMask) >> size) || board->fixedStart > size ||
        (board->minErase && (board->minErase < 3 || board->minErase > 7)) || board->minMatch > size ||
        board->unmatchableColors >= (1u << PHOrbTypes)) return false;
    for (unsigned i = 0; i < board->width * board->height; ++i) {
        float enhancement;
        memcpy(&enhancement, &board->enhancement[i], sizeof(enhancement));
        if (board->types[i] >= PHOrbTypes || (board->flags[i] & ~PHOrbKnownFlags) || !isfinite(enhancement) ||
            enhancement < -1 || enhancement > 1 || (board->types[i] >= PHOrbBasicTypes && enhancement != 0) ||
            board->blindTurns[i] > 99 || board->thornDamage[i] > 127 ||
            ((board->flags[i] & PHOrbFlagTape) != 0) != ((board->tapeMask & (UINT64_C(1) << i)) != 0) ||
            ((board->flags[i] & PHOrbFlagStartDim) && !board->fixedStart)) return false;
    }
    return true;
}

static bool SameBoard(const PHOrbBoard *a, const PHOrbBoard *b, bool allowSpinner) {
    if (!a || !b || a->width != b->width || a->height != b->height ||
        a->width * a->height > PHOrbMaxCells || a->spinnerMask != b->spinnerMask || a->spinnerColors != b->spinnerColors ||
        a->tapeMask != b->tapeMask || a->cloudMask != b->cloudMask || a->fixedStart != b->fixedStart ||
        a->unmatchableColors != b->unmatchableColors || a->minErase != b->minErase || a->minMatch != b->minMatch) return false;
    if (allowSpinner && (!PHOrbSpinnerSupported(a) || !PHOrbSpinnerSupported(b))) return false;
    unsigned phases = allowSpinner ? PHOrbSpinnerVariants(a) : 1;
    bool phaseMatches = false;
    for (unsigned phase = 0; phase < phases && !phaseMatches; ++phase) {
        phaseMatches = true;
        for (unsigned i = 0; i < a->width * a->height; ++i)
            if ((allowSpinner ? PHOrbSpinnerType(a, i, phase) : a->types[i]) != b->types[i]) { phaseMatches = false; break; }
    }
    if (!phaseMatches) return false;
    for (unsigned i = 0; i < a->width * a->height; ++i) {
        uint32_t ignored = allowSpinner ? PHOrbFlagBlindTouch | (a->fixedStart ? PHOrbFlagStartDim : 0) : 0;
        if (a->tokens[i] != b->tokens[i] ||
            (a->flags[i] & ~ignored) != (b->flags[i] & ~ignored) || a->enhancement[i] != b->enhancement[i] ||
            a->blindTurns[i] != b->blindTurns[i] || a->thornDamage[i] != b->thornDamage[i] ||
            a->x[i] != b->x[i] || a->y[i] != b->y[i]) return false;
    }
    return true;
}

bool PHOrbSameBoard(const PHOrbBoard *a, const PHOrbBoard *b) { return SameBoard(a, b, false); }
bool PHOrbSamePlanBoard(const PHOrbBoard *a, const PHOrbBoard *b) {
    // Unsupported spinners can still obtain an exact, stable skill-remedy
    // snapshot. Only the supported variant may ignore a changing color.
    return SameBoard(a, b, PHOrbSpinnerSupported(a) && PHOrbSpinnerSupported(b));
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
        !PHOrbSamePlanBoard(&a->board, &b->board) || !PHOrbSamePlayerState(a, b)) return false;
    const PHOrbAutomationState *x = &a->automation, *y = &b->automation;
    if (!x->valid || !y->valid || x->window != y->window || x->busy != y->busy ||
        x->skillUses != y->skillUses || x->enemiesKnown != y->enemiesKnown || x->boardKnown != y->boardKnown ||
        x->enemyEffects != y->enemyEffects || x->playerBypass != y->playerBypass ||
        x->activeEnemies != y->activeEnemies) return false;
    for (unsigned i = 0; i < 6; ++i) {
        const PHOrbSkillMember *m = &x->members[i], *n = &y->members[i];
        if (m->active != n->active || m->number != n->number || m->charge != n->charge ||
            m->state != n->state || m->bound != n->bound || m->selection != n->selection ||
            m->assistCatalogID != n->assistCatalogID ||
            memcmp(m->skillLevel, n->skillLevel, sizeof(m->skillLevel)) ||
            memcmp(m->skillUses, n->skillUses, sizeof(m->skillUses)) ||
            memcmp(m->skillStage, n->skillStage, sizeof(m->skillStage))) return false;
        // Native can-use refreshes the derived stage bytes on explicit use.
        // Compare the protected stages above, not their mutable cache.
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

uint32_t PHOrbBoardObstacles(const PHOrbSnapshot *state) {
    if (!state) return 0;
    uint32_t result = state->obstacles;
    if (!PHOrbDimensionsSupported(state->board.width, state->board.height)) return result;
    if (state->board.spinnerMask) result |= PHOrbObstacleSpinner;
    if (state->board.unmatchableColors) result |= PHOrbObstacleUnmatchable;
    if (state->board.cloudMask) result |= PHOrbObstacleCloud;
    if (state->board.tapeMask) result |= PHOrbObstacleTape;
    if (state->board.fixedStart) result |= PHOrbObstacleFixedStart;
    if (state->board.minErase > 3 || state->board.minMatch > 3) result |= PHOrbObstacleMatchRule;
    for (unsigned i = 0; i < state->board.width * state->board.height; ++i) {
        float enhancement;
        memcpy(&enhancement, &state->board.enhancement[i], sizeof(enhancement));
        if (isfinite(enhancement) && enhancement < 0) result |= PHOrbObstacleWeakened;
        if (state->board.types[i] > 5) result |= PHOrbObstacleSpecialOrbs;
        if (state->board.types[i] == PHOrbJammer) result |= PHOrbObstacleJammer;
        if (state->board.types[i] == PHOrbPoison) result |= PHOrbObstaclePoison;
        if (state->board.types[i] == PHOrbMortalPoison) result |= PHOrbObstacleMortalPoison;
        if (state->board.types[i] == PHOrbBomb) result |= PHOrbObstacleBomb;
        if (state->board.flags[i] & PHOrbFlagLocked) result |= PHOrbObstacleLocked;
        if (state->board.flags[i] & PHOrbFlagBlindTouch) result |= PHOrbObstacleBlind;
        if (state->board.flags[i] & PHOrbFlagBlind)
            result |= state->board.blindTurns[i] ? PHOrbObstacleSuperBlind : PHOrbObstacleBlind;
        if (state->board.flags[i] & PHOrbFlagNail) result |= PHOrbObstacleNail;
        if (state->board.flags[i] & PHOrbFlagThorn) result |= PHOrbObstacleThorn;
        if (state->board.flags[i] & PHOrbFlagCombo) result |= PHOrbObstacleCombo;
        if (state->board.flags[i] & ~PHOrbKnownFlags) result |= PHOrbObstacleOrbEffect;
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
    if (!PHOrbCanStartAt(board, route->cells[0])) return false;
    const uint64_t blocked = PHOrbBlockedCells(board);
    *result = *board;
    for (unsigned i = 1; i <= through; ++i) {
        unsigned a = route->cells[i - 1], b = route->cells[i];
        if (!PHOrbAdjacent(a, b, board->width, board->height)) return false;
        if (blocked & (UINT64_C(1) << b)) return false;
#define PH_ORB_SWAP(field, type) do { type temp = result->field[a]; result->field[a] = result->field[b]; result->field[b] = temp; } while (0)
        PH_ORB_SWAP(types, uint8_t);
        PH_ORB_SWAP(flags, uint32_t);
        PH_ORB_SWAP(enhancement, uint32_t);
        PH_ORB_SWAP(tokens, uintptr_t);
        PH_ORB_SWAP(blindTurns, uint8_t);
        PH_ORB_SWAP(thornDamage, uint8_t);
#undef PH_ORB_SWAP
        result->flags[a] &= ~PHOrbFlagBlindTouch;
        result->flags[b] &= ~PHOrbFlagBlindTouch;
    }
    return true;
}

bool PHOrbCanPreview(const PHOrbSnapshot *state) {
    return state && state->present && state->valid && state->settled && state->ordinary &&
        !state->multiplayerDisabled && PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer) &&
        state->identity.sourceMode == 0 && state->phase == 2 && !state->dragged && PHInputIsQuiet(&state->input) &&
        (!(state->obstacles & PHOrbObstacleSpinner) || state->board.spinnerMask) &&
        PHOrbBoardValid(&state->board);
}
