#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PHBattleProfileSize = 44, PHBattleParameterSlots = 10, PHBattleEventCount = 157 };

// Encyclopedia numbers at the UI boundary. Zero levels mean the card's maximum;
// -1 awakenings / super awakening mean full / the first available option.
typedef struct {
    uint16_t bodyNumber, assistNumber, level, assistLevel, skillLevel, assistSkillLevel;
    uint16_t plus[3];
    int16_t awakenings, assistAwakenings, superAwakening;
    bool assistPlus, parameters;
    uint8_t latentCount, latents[8];
} PHBattleLoadout;

typedef struct {
    uint16_t catalogID, maxLevel, maxSkillLevel, syncAwakening;
    uint8_t awakeningCount, superCount, supers[32], latentSlots;
    bool canAssist;
} PHBattleCardInfo;

typedef struct { uint8_t bytes[PHBattleProfileSize]; } PHBattleProfile;

bool PHBattleEncodePlus(const uint16_t plus[3], uint8_t bytes[3]);
void PHBattleDecodePlus(const uint8_t bytes[3], uint16_t plus[3]);
unsigned PHBattleLatentCost(unsigned latent);
bool PHBattleEncodeLatents(const uint8_t *latents, size_t count, unsigned capacity, uint64_t *encoded);
bool PHBattleBuildProfile(const PHBattleLoadout *loadout, const PHBattleCardInfo *body,
                         const PHBattleCardInfo *assist, PHBattleProfile *profile);

#ifdef __cplusplus
}
#endif
