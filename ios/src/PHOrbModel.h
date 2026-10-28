#pragma once
#include "PHBattleModel.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { PHOrbMaxCells = 42, PHOrbMaxSteps = 48, PHOrbInputBytes = 2672 };

// Top row first. Coordinates belong to cells; tokens and orb data move.
typedef struct {
    uint8_t width, height, types[PHOrbMaxCells];
    uint32_t flags[PHOrbMaxCells], enhancement[PHOrbMaxCells];
    uintptr_t tokens[PHOrbMaxCells];
    int16_t x[PHOrbMaxCells], y[PHOrbMaxCells];
} PHOrbBoard;

typedef struct {
    PHBattleIdentity identity;
    PHOrbBoard board;
    PHInputState input;
    uintptr_t inputManager, dragged;
    uint32_t phase, wave, moves;
    uint16_t remainingFrames;
    uint8_t fps, minErase, freeMovement;
    bool present, valid, settled, ordinary, multiplayerDisabled;
} PHOrbSnapshot;

typedef struct {
    uint8_t count, cells[PHOrbMaxSteps + 1]; // Includes the starting cell.
    uint8_t combos, erased;
    uint32_t evaluated, elapsedMS;
    bool deadlineReached;
} PHOrbRoute;

bool PHOrbBoardValid(const PHOrbBoard *board);
bool PHOrbSameBoard(const PHOrbBoard *a, const PHOrbBoard *b);
bool PHOrbSameTurn(const PHOrbSnapshot *a, const PHOrbSnapshot *b);
bool PHOrbAdjacent(unsigned a, unsigned b, unsigned width, unsigned height);
bool PHOrbReplay(const PHOrbBoard *board, const PHOrbRoute *route, unsigned through, PHOrbBoard *result);
bool PHOrbCanPreview(const PHOrbSnapshot *state);

#ifdef __cplusplus
}
#endif
