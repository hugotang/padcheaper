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
    PHOrbGoalColumn, PHOrbGoalFour, PHOrbGoalExact
} PHOrbGoalKind;

// Each goal is a conjunction. Colors is a six-bit fire/water/wood/light/dark/
// heart mask. Count means combos, distinct colors, or separate matching groups.
// Length is used by Match (at least this many orbs) and Exact (exactly this
// many actually removed orbs in a first-wave group, without a shape constraint).
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
    uint8_t mortalPoisonErased; // Subset of poisonErased; maxima are independent across spinner phases.
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
    uint16_t sourceCatalogIDs[2]; // Native source body/assist; choice eligibility uses these before transformation.
    // Current usable form is number. During a validated native temporary
    // transformation, identityNumber/identityAssist retain the underlying
    // loadout for plan continuity only; never use them to activate a skill.
    uint16_t identityNumber, identityAssist, temporaryID, temporaryTurns;
    uint8_t state, skillStage[2], cachedSkillStage[2];
    uint16_t assistState; // Native protected +472: low byte 172 retains the assist, type 250 clears it to zero.
    bool active, bound, temporaryKnown;
} PHOrbSkillMember;

typedef enum { PHOrbSkillAutomatic, PHOrbSkillBody, PHOrbSkillAssist } PHOrbSkillSource;
typedef struct { PHOrbSkillSource source; uint32_t skill; } PHOrbSkillRequest;

enum {
    PHOrbSkillWindowScene = 1u << 0, PHOrbSkillWindowHP = 1u << 1,
    PHOrbSkillWindowMemberAnimation = 1u << 2, PHOrbSkillWindowSeal = 1u << 3,
    PHOrbSkillWindowSpecial = 1u << 4, PHOrbSkillWindowMovement = 1u << 5,
    PHOrbSkillWindowEnemyTurn = 1u << 6, PHOrbSkillWindowInput = 1u << 7,
    PHOrbSkillWindowSpecialMode = 1u << 8, PHOrbSkillWindowLimit = 1u << 9,
    PHOrbSkillWindowTimedAnimation = 1u << 10, PHOrbSkillWindowTutorial = 1u << 11,
    PHOrbSkillWindowDungeon = 1u << 12, PHOrbSkillWindowEnemyParts = 1u << 13,
    PHOrbSkillWindowEnemyAction = 1u << 14, PHOrbSkillWindowNoEnemy = 1u << 15,
    PHOrbSkillWindowResize = 1u << 16,
    PHOrbSkillWindowPersistent = PHOrbSkillWindowSeal | PHOrbSkillWindowLimit |
        PHOrbSkillWindowTutorial | PHOrbSkillWindowDungeon | PHOrbSkillWindowEnemyParts
};

// Populated only by the explicit automation reader. Ordinary previews do not
// call skill getters (some native getters lazily mutate cooldown caches).
typedef struct {
    PHOrbSkillMember members[6];
    uint32_t hp, maximumHP;
    uint16_t skillUses;
    uint8_t enemyEffects, playerBypass, activeEnemies, playerEffects;
    bool valid, window, busy, enemiesKnown, boardKnown, hpKnown, statusKnown, awakeningRestricted;
    uint32_t windowBlocks; // Read-only reasons for the existing native window gates.
    bool windowBlocksKnown;
} PHOrbAutomationState;

typedef enum {
    PHOrbSpinnerInactive, PHOrbSpinnerReady, PHOrbSpinnerBadCount,
    PHOrbSpinnerBadPeriod, PHOrbSpinnerBadColors, PHOrbSpinnerBadRows,
    PHOrbSpinnerCountMismatch, PHOrbSpinnerBadPhase
} PHOrbSpinnerReadStatus;

typedef enum {
    PHOrbMultiplayerNone, PHOrbMultiplayerThree, PHOrbMultiplayerFour,
    PHOrbMultiplayerEight, PHOrbMultiplayerUnsupported
} PHOrbMultiplayerKind;

// Read-only native operation context. Multiplayer mode values are selectors,
// not player counts. No skill/team automation may use this input exception.
typedef struct {
    PHOrbMultiplayerKind kind;
    uint32_t turnSerial, teamCount, localSeat, viewTeam, localStatus;
    uint32_t networkFlags, syncState, syncStep, connectionState;
    uint32_t disconnectSeat, disconnectStatus;
    uint8_t countdownReady, finished, resolvePending, clockFlags;
    bool known, clockKnown, disconnectKnown;
    uint64_t clockStart;
    double remainingSeconds;
} PHOrbMultiplayerState;

// Current native view, independently checked against the local team descriptor.
// Multiplayer IDs cannot be supplied by saved teams or a previous turn.
typedef struct {
    uintptr_t teamBank, parameters[2], extraRows;
    uint32_t catalogIDs[2], skillIDs[2], parameterIDs[2];
    uint32_t extraSelection, extraRecord, extraSkill;
    uint16_t sourceCatalogIDs[2], formIDs[2], temporaryIDs[2], temporaryTurns[2];
    uint16_t extraCount;
    uint8_t sourceSlots[2], activeFlags[2], extraFlags[2];
    bool known, extraHelper; // Four-player extra skill has no helper leader skill.
} PHOrbLeaderContext;

typedef enum {
    PHOrbLeaderReadNotAttempted, PHOrbLeaderReadReady, PHOrbLeaderReadInputContext,
    PHOrbLeaderReadCatalog, PHOrbLeaderReadCatalogCapacity, PHOrbLeaderReadOwner,
    PHOrbLeaderReadSkillTable, PHOrbLeaderReadKeys, PHOrbLeaderReadMember,
    PHOrbLeaderReadSource, PHOrbLeaderReadBase, PHOrbLeaderReadForm,
    PHOrbLeaderReadTemporary, PHOrbLeaderReadTemporaryTurns, PHOrbLeaderReadExtraHelper,
    PHOrbLeaderReadExtraRegistry, PHOrbLeaderReadExtraSelection, PHOrbLeaderReadExtraRow,
    PHOrbLeaderReadExtraSkill, PHOrbLeaderReadMemberState, PHOrbLeaderReadTransform,
    PHOrbLeaderReadParameter, PHOrbLeaderReadSkill, PHOrbLeaderReadDuplicateSource,
    PHOrbLeaderReadPairChanged, PHOrbLeaderReadCatalogChanged, PHOrbLeaderReadOwnerChanged,
    PHOrbLeaderReadSkillTableChanged, PHOrbLeaderReadKeysChanged
} PHOrbLeaderReadStatus;

// Observations from the last attempted check, including failed optional reads.
// Never used for planning/identity. No guest pointers, protected keys or account data.
typedef struct {
    PHOrbLeaderReadStatus status;
    uint32_t catalogCount, catalogRecords, skillCount, catalogIndex;
    uint32_t catalogID, parameterID, skillID, sourceSlot, sourceCatalogID;
    uint32_t formID, temporaryID, temporaryTurns, extraSelection, extraRecord, extraSkill;
    uint16_t extraCount;
    uint8_t pass, visibleSlot, activeFlags, extraFlags, ownerFlags, ownerBusy;
    bool ownerBankMatches, parameterMatches;
} PHOrbLeaderReadDiagnostics;

typedef struct {
    PHBattleIdentity identity;
    PHOrbBoard board;
    PHInputState input;
    uintptr_t inputManager, dragged;
    uint32_t phase, wave, moves;
    uint16_t remainingFrames;
    uint16_t leaderNumbers[2]; // Current visible leader/helper, encyclopedia IDs.
    PHOrbLeaderContext leaderContext;
    PHOrbLeaderReadDiagnostics leaderRead;
    uint32_t obstacles;
    uint16_t spinnerTurns, spinnerPeriodFrames, unmatchableTurns, tapeRowTurns, tapeColumnTurns, cloudTurns;
    uint32_t spinnerRawCount, spinnerRawPeriod, spinnerRawColors;
    uint8_t spinnerRawRows[6], spinnerStoredCells;
    PHOrbSpinnerReadStatus spinnerReadStatus;
    uint8_t fps, minErase, freeMovement;
    // Native resize interpolation is independent of battle phase. Identity
    // remains readable while the orb geometry is unavailable for planning.
    float resizeProgress, resizeTarget;
    bool boardTransition;
    bool present, valid, settled, ordinary, multiplayerDisabled, leadersValid, unmatchableKnown;
    PHOrbAutomationState automation;
    PHOrbThornState thorns;
    PHOrbMultiplayerState multiplayer;
} PHOrbSnapshot;

typedef struct {
    uint8_t count, cells[PHOrbMaxSteps + 1]; // Includes the starting cell.
    uint8_t combos, erased;
    uint8_t goalHits[PHOrbMaxGoals], goalCount, matchedGoals;
    uint8_t poisonErased, unmatchedBombs;
    uint8_t thornContacts, heartFours;
    uint32_t thornDamage;
    uint32_t evaluated, elapsedMS;
    uint8_t searchPasses, backtracks, exploredDepth;
    bool deadlineReached, goalsMet, hazardsSafe;
    uint8_t mortalPoisonErased;
    bool diagonal; // Captured gesture policy; match connectivity remains four-way.
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
PHOrbMultiplayerKind PHOrbMultiplayerMode(int16_t mode, uint32_t multiplayer);
bool PHOrbMultiplayerWindow(const PHOrbSnapshot *state);
bool PHOrbMultiplayerNetworkReady(const PHOrbMultiplayerState *state);
bool PHOrbMultiplayerLocalReady(const PHOrbMultiplayerState *state);
bool PHOrbSameMultiplayerTurn(const PHOrbMultiplayerState *a, const PHOrbMultiplayerState *b);
bool PHOrbInputContextReady(const PHOrbSnapshot *state);
const char *PHOrbLeaderReadStatusName(PHOrbLeaderReadStatus status);
bool PHOrbLeaderContextReady(const PHOrbSnapshot *state);
bool PHOrbSameLeaders(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbGoalsValid(const PHOrbGoalSet *goals);
bool PHOrbAdjacent(unsigned a, unsigned b, unsigned width, unsigned height);
bool PHOrbMoveAdjacent(unsigned a, unsigned b, unsigned width, unsigned height, bool diagonal);
bool PHOrbReplay(const PHOrbBoard *board, const PHOrbRoute *route, unsigned through, PHOrbBoard *result);
bool PHOrbCanPreview(const PHOrbSnapshot *state);
bool PHOrbSameAutomation(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbSameAutomationExceptWindow(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbSamePlayerState(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
uint32_t PHOrbBoardObstacles(const PHOrbSnapshot *state);

#ifdef __cplusplus
}
#endif
