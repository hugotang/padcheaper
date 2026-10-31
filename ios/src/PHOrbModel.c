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
    return blocked;
}

uint32_t PHOrbThornDamage(unsigned percent, uint32_t maximumHP) {
    if (!percent) return 0;
    if (percent > 127 || !maximumHP || maximumHP > INT32_MAX) return UINT32_MAX;
    // Native swap rounds float32 percent -> float64 HP. Ignore awakening
    // reductions so this is an upper bound, never optimistic damage.
    double damage = floor((double)((float)percent / 100.0f) * maximumHP + 0.5);
    return damage >= INT32_MAX ? INT32_MAX : damage < 1 ? 1 : (uint32_t)damage;
}

bool PHOrbCanEnterThorn(const PHOrbSnapshot *state, unsigned cell) {
    if (!state || cell >= state->board.width * state->board.height || cell >= PHOrbMaxCells) return false;
    if (!(state->board.flags[cell] & PHOrbFlagThorn) || !state->board.thornDamage[cell] ||
        !state->dragged || state->dragged == state->board.tokens[cell]) return true;
    if (!PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer)) return false;
    const PHOrbThornState *t = &state->thorns;
    return t->known && t->hp > 0 && t->hp <= t->maximumHP && t->maximumHP <= INT32_MAX &&
        (uint64_t)t->pendingDamage + PHOrbThornDamage(state->board.thornDamage[cell], t->maximumHP) < t->hp;
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
        PHOrbSameMultiplayerTurn(&a->multiplayer, &b->multiplayer) &&
        a->inputManager == b->inputManager && a->minErase == b->minErase && a->freeMovement == b->freeMovement &&
        a->board.width == b->board.width && a->board.height == b->board.height;
}

PHOrbMultiplayerKind PHOrbMultiplayerMode(int16_t mode, uint32_t multiplayer) {
    if (PHBattleIsSinglePlayer(mode, multiplayer)) return PHOrbMultiplayerNone;
    if (multiplayer == 1) {
        if (mode == 7) return PHOrbMultiplayerThree;
        if (mode == 11) return PHOrbMultiplayerFour;
        if (mode == 10) return PHOrbMultiplayerEight;
    }
    if (mode == 7 && multiplayer == 2) return PHOrbMultiplayerThreeSolo;
    return PHOrbMultiplayerUnsupported;
}

bool PHOrbMultiplayerTransitionAllowed(PHOrbMultiplayerKind from, PHOrbMultiplayerKind to) {
    // Native co-op continuation is one-way within an entry. It is still an
    // explicit multiplayer one-shot context, never ordinary solo automation.
    return from == to || (from == PHOrbMultiplayerThree && to == PHOrbMultiplayerThreeSolo);
}

bool PHOrbSameMultiplayerTurn(const PHOrbMultiplayerState *a, const PHOrbMultiplayerState *b) {
    if (!a || !b || a->kind != b->kind) return false;
    if (a->kind == PHOrbMultiplayerNone) return true;
    return a->known && b->known && a->kind != PHOrbMultiplayerUnsupported &&
        a->turnSerial == b->turnSerial && a->teamCount == b->teamCount &&
        a->localSeat == b->localSeat && a->viewTeam == b->viewTeam &&
        (a->kind == PHOrbMultiplayerThree || a->kind == PHOrbMultiplayerThreeSolo || a->clockStart == b->clockStart);
}

static bool PHOrbMultiplayerRemoteExit(const PHOrbMultiplayerState *state) {
    // 0x10000DD10 records the affected seat and its disconnected/retired
    // status. A local departure or an unconfirmed seat is not a peer event.
    return state->kind == PHOrbMultiplayerFour && state->teamCount == 4 &&
        state->localSeat < state->teamCount && state->viewTeam == state->localSeat &&
        state->disconnectKnown && state->disconnectSeat < state->teamCount &&
        state->disconnectSeat != state->localSeat &&
        (state->disconnectStatus == 5 || state->disconnectStatus == 6);
}

bool PHOrbMultiplayerNetworkReady(const PHOrbMultiplayerState *state) {
    // 0x10001C5F8 / 0x10000C5EC clear the active network flags when native
    // local continuation finishes. Connection state is no longer its clock.
    if (state && state->kind == PHOrbMultiplayerThreeSolo) return state->networkFlags == 0;
    if (!state || (state->kind != PHOrbMultiplayerThree && state->kind != PHOrbMultiplayerFour &&
        state->kind != PHOrbMultiplayerEight)) return false;
    // Native release sets bit 1 in every multiplayer mode. In 4P the
    // 0x10001343c branch returns without clearing it, and later local boards
    // remain playable. 3P must finish its send/handoff; 8P clears the flags
    // and initializes the next board. Keep those modes' existing mask.
    uint32_t allowed = state->kind == PHOrbMultiplayerFour ? 0x23u : 0x21u;
    // 0x100011BEC returns competitive sync to 0 and leaves bit 16 set.
    // 0x100013C70 applies it to the recorded seat, not every local board.
    if (PHOrbMultiplayerRemoteExit(state)) allowed |= 0x10000u;
    return (state->networkFlags & 1u) && !(state->networkFlags & ~allowed);
}

bool PHOrbMultiplayerLocalReady(const PHOrbMultiplayerState *state) {
    if (state && state->kind == PHOrbMultiplayerThreeSolo)
        return state->known && state->localStatus == 3 && state->teamCount >= 2 && state->teamCount <= 3 &&
            state->localSeat < state->teamCount && state->viewTeam == state->localSeat &&
            state->continuationOwnerKnown && !state->continuationDialog && state->recoverySource <= 1 &&
            !(state->recoveryFlags & 0x40u) && !state->syncState && !state->syncStep &&
            PHOrbMultiplayerNetworkReady(state);
    if (!state || !state->known || state->localStatus != 3 ||
        (state->connectionState != 2 && state->connectionState != 3 &&
         state->connectionState != 6 && state->connectionState != 7) ||
        !PHOrbMultiplayerNetworkReady(state)) return false;
    if (state->syncState == 0) return true;
    if (state->kind != PHOrbMultiplayerFour) return false;
    // Competitive dispatch returns nonblocking for peer recovery (5) and
    // departure completion (15). Recovery entry clears the selected orb;
    // accept only its initialized step 2, which skips the co-op modal.
    // Keep unknown, local reconnect and terminal states closed.
    if (state->syncState == 5) return state->syncStep == 2;
    return state->syncState == 15 && (state->syncStep == 0 || state->syncStep == 2) &&
        PHOrbMultiplayerRemoteExit(state);
}

bool PHOrbMultiplayerWindow(const PHOrbSnapshot *state) {
    if (!state || !state->present || !state->valid || state->multiplayerDisabled || state->phase != 2 ||
        state->identity.sourceMode != 0 || state->boardTransition) return false;
    const PHOrbMultiplayerState *m = &state->multiplayer;
    if (!m->known || m->kind != PHOrbMultiplayerMode(state->identity.entryMode, state->identity.multiplayer) ||
        m->kind == PHOrbMultiplayerNone || m->kind == PHOrbMultiplayerUnsupported ||
        !m->teamCount || m->localSeat >= m->teamCount || m->viewTeam != m->localSeat ||
        m->turnSerial > INT32_MAX || !PHOrbMultiplayerLocalReady(m) || m->resolvePending)
        return false;
    // Selector 2 bypasses the native co-op input precheck and decision clock.
    // The ordinary move timer still bounds every explicitly armed gesture.
    if (m->kind == PHOrbMultiplayerThreeSolo)
        return state->fps >= 15 && state->fps <= 120 && state->remainingFrames > 0 && state->remainingFrames <= 30000;
    if (!m->clockKnown || !isfinite(m->remainingSeconds) || m->remainingSeconds <= 0 || m->remainingSeconds > 86400)
        return false;
    if (m->kind == PHOrbMultiplayerThree)
        // Native first swap pauses the co-op decision clock; the ordinary
        // move timer then bounds this already-selected gesture.
        return !(m->clockFlags & ~3u) && (!(m->clockFlags & 2u) || state->dragged) &&
            m->teamCount >= 2 && m->teamCount <= 3 &&
            m->turnSerial % m->teamCount == m->localSeat;
    return m->teamCount == (m->kind == PHOrbMultiplayerFour ? 4u : 8u) &&
        m->countdownReady == 1 && m->finished == 0 && m->clockStart != 0 && (m->clockFlags & 0x36u) == 4u;
}

bool PHOrbInputContextReady(const PHOrbSnapshot *state) {
    if (!state || state->multiplayerDisabled) return false;
    if (PHBattleIsSinglePlayer(state->identity.entryMode, state->identity.multiplayer))
        return state->multiplayer.kind == PHOrbMultiplayerNone;
    return PHOrbMultiplayerWindow(state);
}

const char *PHOrbLeaderReadStatusName(PHOrbLeaderReadStatus status) {
    static const char *const names[] = {
        "not-attempted", "ready", "input-context", "catalog-header", "catalog-capacity", "local-owner",
        "skill-table", "protected-keys", "member-header", "protected-source", "source-card", "permanent-form",
        "temporary-form", "temporary-turns", "extra-helper-state", "extra-registry", "extra-selection",
        "extra-row", "extra-active-skill", "member-state", "transform-mismatch", "parameter-chain",
        "leader-skill", "duplicate-source", "pair-changed", "catalog-changed", "owner-changed",
        "skill-table-changed", "protected-keys-changed"
    };
    return (unsigned)status < sizeof(names) / sizeof(names[0]) ? names[status] : "unknown";
}

bool PHOrbLeaderContextReady(const PHOrbSnapshot *state) {
    if (!state || !state->present || !state->valid || state->multiplayerDisabled || !state->leadersValid ||
        !state->leaderNumbers[0] || !PHOrbInputContextReady(state)) return false;
    if (state->multiplayer.kind == PHOrbMultiplayerNone) return state->leaderNumbers[1] != 0;
    const PHOrbLeaderContext *c = &state->leaderContext;
    if (!c->known || !c->teamBank || !c->parameters[0] || !c->parameters[1] ||
        !c->catalogIDs[0] || !c->catalogIDs[1] || c->sourceSlots[0] >= 6 || c->sourceSlots[1] >= 6 ||
        c->sourceSlots[0] == c->sourceSlots[1] || !c->sourceCatalogIDs[0] || !c->sourceCatalogIDs[1] ||
        c->activeFlags[0] != 1 || (c->extraFlags[0] & 2u)) return false;
    if (!c->extraHelper) return state->leaderNumbers[1] && c->activeFlags[1] == 1 && !(c->extraFlags[1] & 2u);
    return state->multiplayer.kind == PHOrbMultiplayerFour && !state->leaderNumbers[1] &&
        c->catalogIDs[1] == 10000000u && c->sourceSlots[1] == 5 && !c->activeFlags[1] &&
        (c->extraFlags[1] & 2u) && c->sourceCatalogIDs[1] == (uint16_t)10000000u &&
        !c->formIDs[1] && !c->temporaryIDs[1] && !c->temporaryTurns[1] &&
        c->extraRows && c->extraCount && c->extraSkill;
}

bool PHOrbSameLeaders(const PHOrbSnapshot *a, const PHOrbSnapshot *b) {
    if (!a || !b || a->leadersValid != b->leadersValid) return false;
    if (!a->leadersValid) return true;
    if (a->leaderNumbers[0] != b->leaderNumbers[0] || a->leaderNumbers[1] != b->leaderNumbers[1] ||
        a->multiplayer.kind != b->multiplayer.kind) return false;
    if (a->multiplayer.kind == PHOrbMultiplayerNone) return true;
    const PHOrbLeaderContext *x = &a->leaderContext, *y = &b->leaderContext;
    return PHOrbLeaderContextReady(a) && PHOrbLeaderContextReady(b) &&
        PHOrbSameMultiplayerTurn(&a->multiplayer, &b->multiplayer) && x->teamBank == y->teamBank &&
        !memcmp(x->parameters, y->parameters, sizeof(x->parameters)) &&
        !memcmp(x->catalogIDs, y->catalogIDs, sizeof(x->catalogIDs)) &&
        !memcmp(x->skillIDs, y->skillIDs, sizeof(x->skillIDs)) &&
        !memcmp(x->parameterIDs, y->parameterIDs, sizeof(x->parameterIDs)) &&
        x->extraHelper == y->extraHelper && x->extraRows == y->extraRows && x->extraCount == y->extraCount &&
        x->extraSelection == y->extraSelection && x->extraRecord == y->extraRecord && x->extraSkill == y->extraSkill &&
        !memcmp(x->activeFlags, y->activeFlags, sizeof(x->activeFlags)) &&
        !memcmp(x->extraFlags, y->extraFlags, sizeof(x->extraFlags)) &&
        !memcmp(x->sourceSlots, y->sourceSlots, sizeof(x->sourceSlots)) &&
        !memcmp(x->sourceCatalogIDs, y->sourceCatalogIDs, sizeof(x->sourceCatalogIDs)) &&
        !memcmp(x->formIDs, y->formIDs, sizeof(x->formIDs)) &&
        !memcmp(x->temporaryIDs, y->temporaryIDs, sizeof(x->temporaryIDs)) &&
        !memcmp(x->temporaryTurns, y->temporaryTurns, sizeof(x->temporaryTurns));
}

bool PHOrbSameAutomation(const PHOrbSnapshot *a, const PHOrbSnapshot *b) {
    if (!PHOrbSameTurn(a, b) || a->boardTransition || b->boardTransition || !PHOrbSameLeaders(a, b) || a->phase != b->phase ||
        a->moves != b->moves || a->obstacles != b->obstacles || a->ordinary != b->ordinary || a->unmatchableKnown != b->unmatchableKnown ||
        !PHOrbSamePlanBoard(&a->board, &b->board) || !PHOrbSamePlayerState(a, b)) return false;
    const PHOrbAutomationState *x = &a->automation, *y = &b->automation;
    if (!x->valid || !y->valid || x->window != y->window || x->busy != y->busy ||
        x->windowBlocksKnown != y->windowBlocksKnown || x->windowBlocks != y->windowBlocks ||
        x->skillUses != y->skillUses || x->enemiesKnown != y->enemiesKnown || x->boardKnown != y->boardKnown ||
        x->enemyEffects != y->enemyEffects || x->playerBypass != y->playerBypass ||
        x->activeEnemies != y->activeEnemies) return false;
    for (unsigned i = 0; i < 6; ++i) {
        const PHOrbSkillMember *m = &x->members[i], *n = &y->members[i];
        if (m->active != n->active || m->number != n->number || m->charge != n->charge ||
            m->state != n->state || m->bound != n->bound || m->selection != n->selection ||
            m->assistCatalogID != n->assistCatalogID || m->assistState != n->assistState ||
            memcmp(m->sourceCatalogIDs, n->sourceCatalogIDs, sizeof(m->sourceCatalogIDs)) ||
            m->identityNumber != n->identityNumber || m->identityAssist != n->identityAssist ||
            m->temporaryID != n->temporaryID || m->temporaryTurns != n->temporaryTurns ||
            m->temporaryKnown != n->temporaryKnown ||
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

bool PHOrbSameAutomationExceptWindow(const PHOrbSnapshot *a, const PHOrbSnapshot *b) {
    if (!a || !b) return false;
    PHOrbSnapshot comparison = *b;
    comparison.automation.window = a->automation.window;
    comparison.automation.busy = a->automation.busy;
    comparison.automation.windowBlocks = a->automation.windowBlocks;
    comparison.automation.windowBlocksKnown = a->automation.windowBlocksKnown;
    return PHOrbSameAutomation(a, &comparison);
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
        if (g->kind > PHOrbGoalExact || !g->count || g->count > PHOrbMaxCells / 3) return false;
        if (g->kind == PHOrbGoalCombo) {
            if (g->colors || g->length) return false;
        } else {
            if (!g->colors || g->colors > 63) return false;
            if (g->kind == PHOrbGoalColors) {
                unsigned colors = 0;
                for (unsigned bit = 0; bit < 6; ++bit) colors += (g->colors >> bit) & 1u;
                if (g->count > colors) return false;
            }
            if (g->kind == PHOrbGoalMatch || g->kind == PHOrbGoalExact) {
                if (g->length < 3 || g->length > PHOrbMaxCells) return false;
            } else if (g->length) return false;
        }
        // Repeated requirements use a count, avoiding ambiguous double counting.
        for (unsigned j = 0; j < i; ++j) if (goals->items[j].kind == g->kind &&
            goals->items[j].colors == g->colors && goals->items[j].length == g->length) return false;
    }
    return true;
}

unsigned PHOrbRequiredShapeOrbs(const PHOrbGoalSet *goals, unsigned color) {
    if (!PHOrbGoalsValid(goals) || color >= PHOrbBasicTypes) return 0;
    unsigned result = 0;
    for (unsigned i = 0; i < goals->count; ++i) {
        const PHOrbGoal *g = &goals->items[i];
        if (g->colors != (1u << color)) continue;
        if (g->kind == PHOrbGoalL || g->kind == PHOrbGoalT || g->kind == PHOrbGoalCross) result += 5u * g->count;
        else if (g->kind == PHOrbGoalSquare) result += 9u * g->count;
        else if (g->kind == PHOrbGoalFour) result += 4u * g->count;
    }
    return result;
}

bool PHOrbShapeInventoryAvailable(const PHOrbBoard *board, const PHOrbGoalSet *goals) {
    if (!PHOrbBoardValid(board) || !PHOrbGoalsValid(goals)) return false;
    for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
        unsigned minimum = 0, maximum = 0;
        PHOrbColorCountRange(board, color, &minimum, &maximum);
        if (PHOrbRequiredShapeOrbs(goals, color) > minimum) return false;
    }
    return true;
}

bool PHOrbSameAssistRecovery(const PHOrbAssistRecoveryState *a, const PHOrbAssistRecoveryState *b) {
    if (!a || !b || !a->valid || !b->valid || a->turns != b->turns || a->fresh != b->fresh ||
        a->dungeonFlags != b->dungeonFlags || a->retainedAssists != b->retainedAssists ||
        a->unknownMembers != b->unknownMembers) return false;
    return !memcmp(a->perColor, b->perColor, sizeof(a->perColor)) &&
        !memcmp(a->providers, b->providers, sizeof(a->providers)) &&
        !memcmp(a->blockers, b->blockers, sizeof(a->blockers)) &&
        !memcmp(a->numbers, b->numbers, sizeof(a->numbers)) &&
        !memcmp(a->sourceAssists, b->sourceAssists, sizeof(a->sourceAssists)) &&
        !memcmp(a->activeAssists, b->activeAssists, sizeof(a->activeAssists)) &&
        !memcmp(a->assistStates, b->assistStates, sizeof(a->assistStates)) &&
        !memcmp(a->temporaryIDs, b->temporaryIDs, sizeof(a->temporaryIDs)) &&
        !memcmp(a->temporaryTurns, b->temporaryTurns, sizeof(a->temporaryTurns)) &&
        !memcmp(a->colors, b->colors, sizeof(a->colors)) &&
        !memcmp(a->awakenings, b->awakenings, sizeof(a->awakenings)) &&
        !memcmp(a->latents, b->latents, sizeof(a->latents));
}

bool PHOrbAdjacent(unsigned a, unsigned b, unsigned width, unsigned height) {
    if (!width || !height || width * height > PHOrbMaxCells || a >= width * height || b >= width * height) return false;
    return (a / width == b / width && (a + 1 == b || b + 1 == a)) ||
        (a % width == b % width && (a + width == b || b + width == a));
}

bool PHOrbMoveAdjacent(unsigned a, unsigned b, unsigned width, unsigned height, bool diagonal) {
    if (!diagonal) return PHOrbAdjacent(a, b, width, height);
    if (!width || !height || width > PHOrbMaxCells || height > PHOrbMaxCells ||
        width * height > PHOrbMaxCells || a >= width * height || b >= width * height || a == b) return false;
    unsigned dx = a % width > b % width ? a % width - b % width : b % width - a % width;
    unsigned dy = a / width > b / width ? a / width - b / width : b / width - a / width;
    return dx <= 1 && dy <= 1;
}

bool PHOrbReplay(const PHOrbBoard *board, const PHOrbRoute *route, unsigned through, PHOrbBoard *result) {
    if (!PHOrbBoardValid(board) || !route || !result || route->count < 1 ||
        route->count > PHOrbMaxSteps + 1 || through >= route->count || route->cells[0] >= board->width * board->height) return false;
    if (!PHOrbCanStartAt(board, route->cells[0])) return false;
    const uint64_t blocked = PHOrbBlockedCells(board);
    *result = *board;
    for (unsigned i = 1; i <= through; ++i) {
        unsigned a = route->cells[i - 1], b = route->cells[i];
        if (!PHOrbMoveAdjacent(a, b, board->width, board->height, route->diagonal)) return false;
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
        !state->boardTransition &&
        PHOrbInputContextReady(state) &&
        state->identity.sourceMode == 0 && state->phase == 2 && !state->dragged && PHInputIsQuiet(&state->input) &&
        (!(state->obstacles & PHOrbObstacleSpinner) || state->board.spinnerMask || state->spinnerReadStatus == PHOrbSpinnerReady) &&
        PHOrbBoardValid(&state->board);
}
