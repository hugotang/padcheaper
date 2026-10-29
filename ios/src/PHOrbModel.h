#pragma once
#include "PHBattleModel.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PHOrbMaxCells = 42, PHOrbMaxSteps = 48, PHOrbInputBytes = 2672, PHOrbMaxGoals = 6 };
enum {
    PHOrbFlagBlindTouch = 0x4, PHOrbFlagEffectFresh = 0x8, PHOrbFlagLocked = 0x800, PHOrbFlagBlind = 0x1000,
    PHOrbFlagStartDim = 0x2000, PHOrbFlagTape = 0x4000, PHOrbFlagCombo = 0x8000,
    PHOrbFlagBlindFresh = 0x10000, PHOrbFlagNail = 0x20000, PHOrbFlagThorn = 0x80000,
    PHOrbKnownFlags = PHOrbFlagBlindTouch | PHOrbFlagEffectFresh | PHOrbFlagLocked | PHOrbFlagBlind | PHOrbFlagStartDim |
        PHOrbFlagTape | PHOrbFlagCombo | PHOrbFlagBlindFresh | PHOrbFlagNail | PHOrbFlagThorn
};
enum { PHOrbBasicTypes = 6, PHOrbJammer = 6, PHOrbPoison = 7, PHOrbMortalPoison = 8, PHOrbBomb = 9, PHOrbTypes = 10 };

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
    uint32_t hp, maximumHP, pendingDamage;
    bool known;
} PHOrbThornState;

typedef struct {
    uint8_t combos, erased, goalHits[PHOrbMaxGoals], matchedGoals;
    uint8_t poisonErased, unmatchedBombs, heartFours;
    bool goalsMet, hazardsSafe;
} PHOrbEvaluation;

// Top row first. Coordinates belong to cells; tokens and orb data move.
typedef struct {
    uint8_t width, height, types[PHOrbMaxCells];
    uint32_t flags[PHOrbMaxCells], enhancement[PHOrbMaxCells];
    uintptr_t tokens[PHOrbMaxCells];
    int16_t x[PHOrbMaxCells], y[PHOrbMaxCells];
    uint8_t blindTurns[PHOrbMaxCells], thornDamage[PHOrbMaxCells];
    // Cell-anchored restrictions never move with tokens. Start is one-based;
    // zero means unrestricted. minErase zero means the default of three.
    uint64_t spinnerMask, tapeMask, cloudMask;
    uint16_t spinnerColors, unmatchableColors;
    uint8_t fixedStart, minErase, minMatch;
} PHOrbBoard;

enum {
    PHOrbEnemyDamageAbsorb = 1, PHOrbEnemyAttributeAbsorb = 2,
    PHOrbEnemyDamageVoid = 4, PHOrbEnemyComboAbsorb = 8,
    PHOrbMaxSkillRules = 12, PHOrbMaxSkillGroups = 6
};

enum {
    PHOrbObstacleUnmatchable = 1, PHOrbObstacleBoardEffect = 2,
    PHOrbObstacleOrbEffect = 4, PHOrbObstacleSpecialOrbs = 8,
    PHOrbObstacleSpinner = 16, PHOrbObstacleLocked = 32, PHOrbObstacleWeakened = 64,
    PHOrbObstacleJammer = 128, PHOrbObstaclePoison = 256,
    PHOrbObstacleMortalPoison = 512, PHOrbObstacleBomb = 1024,
    PHOrbObstacleCloud = 2048, PHOrbObstacleTape = 4096, PHOrbObstacleBlind = 8192,
    PHOrbObstacleSuperBlind = 16384, PHOrbObstacleNail = 32768,
    PHOrbObstacleThorn = 65536, PHOrbObstacleFixedStart = 131072,
    PHOrbObstacleCombo = 262144, PHOrbObstacleMatchRule = 524288
};

enum {
    PHOrbPlayerAwokenBind = 1, PHOrbPlayerMemberBind = 2, PHOrbPlayerSkillSeal = 4
};

typedef struct {
    uint32_t selection; // Raw native skill choice; UINT32_MAX means automatic base/assist selection.
    uint16_t number, charge, assistCatalogID, skillLevel[2], skillUses[2];
    uint8_t state, skillStage[2], cachedSkillStage[2];
    bool active, bound;
} PHOrbSkillMember;

typedef enum { PHOrbSkillAutomatic, PHOrbSkillBody, PHOrbSkillAssist } PHOrbSkillSource;
typedef struct { PHOrbSkillSource source; uint32_t skill; } PHOrbSkillRequest;

// Populated only by the explicit automation reader. Ordinary previews do not
// call skill getters (some native getters lazily mutate cooldown caches).
typedef struct {
    PHOrbSkillMember members[6];
    uint32_t hp, maximumHP;
    uint16_t skillUses;
    uint8_t enemyEffects, playerBypass, activeEnemies, playerEffects;
    bool valid, window, busy, enemiesKnown, boardKnown, hpKnown, statusKnown, awakeningRestricted;
} PHOrbAutomationState;

typedef enum {
    PHOrbSpinnerInactive, PHOrbSpinnerReady, PHOrbSpinnerBadCount,
    PHOrbSpinnerBadPeriod, PHOrbSpinnerBadColors, PHOrbSpinnerBadRows,
    PHOrbSpinnerCountMismatch, PHOrbSpinnerBadPhase
} PHOrbSpinnerReadStatus;

typedef struct {
    PHBattleIdentity identity;
    PHOrbBoard board;
    PHInputState input;
    uintptr_t inputManager, dragged;
    uint32_t phase, wave, moves;
    uint16_t remainingFrames;
    uint16_t leaderNumbers[2]; // Current visible leader/helper, encyclopedia IDs.
    uint32_t obstacles;
    uint16_t spinnerTurns, spinnerPeriodFrames, unmatchableTurns, tapeRowTurns, tapeColumnTurns, cloudTurns;
    uint32_t spinnerRawCount, spinnerRawPeriod, spinnerRawColors;
    uint8_t spinnerRawRows[6], spinnerStoredCells;
    PHOrbSpinnerReadStatus spinnerReadStatus;
    uint8_t fps, minErase, freeMovement;
    bool present, valid, settled, ordinary, multiplayerDisabled, leadersValid, unmatchableKnown;
    PHOrbAutomationState automation;
    PHOrbThornState thorns;
} PHOrbSnapshot;

typedef struct {
    uint8_t count, cells[PHOrbMaxSteps + 1]; // Includes the starting cell.
    uint8_t combos, erased;
    uint8_t goalHits[PHOrbMaxGoals], goalCount, matchedGoals;
    uint8_t poisonErased, unmatchedBombs;
    uint8_t thornContacts, heartFours;
    uint32_t thornDamage;
    uint32_t evaluated, elapsedMS;
    bool deadlineReached, goalsMet, hazardsSafe;
} PHOrbRoute;

bool PHOrbDimensionsSupported(unsigned width, unsigned height);
bool PHOrbBoardValid(const PHOrbBoard *board);
bool PHOrbSpinnerSupported(const PHOrbBoard *board);
unsigned PHOrbSpinnerVariants(const PHOrbBoard *board);
uint8_t PHOrbSpinnerType(const PHOrbBoard *board, unsigned cell, unsigned phase);
uint64_t PHOrbBlockedCells(const PHOrbBoard *board);
bool PHOrbCanStartAt(const PHOrbBoard *board, unsigned cell);
uint32_t PHOrbThornDamage(unsigned percent, uint32_t maximumHP);
bool PHOrbCanEnterThorn(const PHOrbSnapshot *state, unsigned cell);
void PHOrbColorCountRange(const PHOrbBoard *board, unsigned color, unsigned *minimum, unsigned *maximum);
bool PHOrbSameBoard(const PHOrbBoard *a, const PHOrbBoard *b);
// Spinner colors may advance only by a shared phase. Tokens, gameplay flags,
// enhancement, geometry and all restrictions must still agree. Fixed-start
// dimming is visual and is cleared by native start acceptance.
bool PHOrbSamePlanBoard(const PHOrbBoard *a, const PHOrbBoard *b);
bool PHOrbSameTurn(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbSameLeaders(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbGoalsValid(const PHOrbGoalSet *goals);
bool PHOrbAdjacent(unsigned a, unsigned b, unsigned width, unsigned height);
bool PHOrbReplay(const PHOrbBoard *board, const PHOrbRoute *route, unsigned through, PHOrbBoard *result);
bool PHOrbCanPreview(const PHOrbSnapshot *state);
bool PHOrbSameAutomation(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbSamePlayerState(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
uint32_t PHOrbBoardObstacles(const PHOrbSnapshot *state);

#ifdef __cplusplus
}
#endif
