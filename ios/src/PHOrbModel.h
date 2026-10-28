#pragma once
#include "PHBattleModel.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PHOrbMaxCells = 42, PHOrbMaxSteps = 48, PHOrbInputBytes = 2672, PHOrbMaxGoals = 6 };

typedef enum {
    PHOrbGoalCombo, PHOrbGoalColors, PHOrbGoalMatch, PHOrbGoalL,
    PHOrbGoalCross, PHOrbGoalT, PHOrbGoalSquare, PHOrbGoalRow,
    PHOrbGoalColumn, PHOrbGoalFour
} PHOrbGoalKind;

// Each goal is a conjunction. Colors is a six-bit fire/water/wood/light/dark/
// heart mask. Count means combos, distinct colors, or separate matching groups.
// Length is used only by Match (at least this many orbs in one group).
typedef struct { uint8_t kind, colors, count, length; } PHOrbGoal;
typedef struct { uint8_t count; PHOrbGoal items[PHOrbMaxGoals]; } PHOrbGoalSet;

typedef struct {
    uint8_t combos, erased, goalHits[PHOrbMaxGoals], matchedGoals;
    bool goalsMet;
} PHOrbEvaluation;

// Top row first. Coordinates belong to cells; tokens and orb data move.
typedef struct {
    uint8_t width, height, types[PHOrbMaxCells];
    uint32_t flags[PHOrbMaxCells], enhancement[PHOrbMaxCells];
    uintptr_t tokens[PHOrbMaxCells];
    int16_t x[PHOrbMaxCells], y[PHOrbMaxCells];
} PHOrbBoard;

enum {
    PHOrbEnemyDamageAbsorb = 1, PHOrbEnemyAttributeAbsorb = 2,
    PHOrbEnemyDamageVoid = 4, PHOrbEnemyComboAbsorb = 8,
    PHOrbMaxSkillRules = 12
};

typedef struct {
    uint16_t number, charge;
    uint8_t state;
    bool active, bound, overridden;
} PHOrbSkillMember;

// Populated only by the explicit automation reader. Ordinary previews do not
// call skill getters (some native getters lazily mutate cooldown caches).
typedef struct {
    PHOrbSkillMember members[6];
    uint16_t skillUses;
    uint8_t enemyEffects, playerBypass, activeEnemies;
    bool valid, window, busy, enemiesKnown, boardKnown;
} PHOrbAutomationState;

typedef struct {
    PHBattleIdentity identity;
    PHOrbBoard board;
    PHInputState input;
    uintptr_t inputManager, dragged;
    uint32_t phase, wave, moves;
    uint16_t remainingFrames;
    uint16_t leaderNumbers[2]; // Current visible leader/helper, encyclopedia IDs.
    uint8_t fps, minErase, freeMovement;
    bool present, valid, settled, ordinary, multiplayerDisabled, leadersValid;
    PHOrbAutomationState automation;
} PHOrbSnapshot;

typedef struct {
    uint8_t count, cells[PHOrbMaxSteps + 1]; // Includes the starting cell.
    uint8_t combos, erased;
    uint8_t goalHits[PHOrbMaxGoals], goalCount, matchedGoals;
    uint32_t evaluated, elapsedMS;
    bool deadlineReached, goalsMet;
} PHOrbRoute;

bool PHOrbDimensionsSupported(unsigned width, unsigned height);
bool PHOrbBoardValid(const PHOrbBoard *board);
bool PHOrbSameBoard(const PHOrbBoard *a, const PHOrbBoard *b);
bool PHOrbSameTurn(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbSameLeaders(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbGoalsValid(const PHOrbGoalSet *goals);
bool PHOrbAdjacent(unsigned a, unsigned b, unsigned width, unsigned height);
bool PHOrbReplay(const PHOrbBoard *board, const PHOrbRoute *route, unsigned through, PHOrbBoard *result);
bool PHOrbCanPreview(const PHOrbSnapshot *state);
bool PHOrbSameAutomation(const PHOrbSnapshot *a, const PHOrbSnapshot *b);

#ifdef __cplusplus
}
#endif
