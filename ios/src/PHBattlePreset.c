#include "PHBattlePreset.h"
#include "PHBattleModel.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
static void put32(uint8_t *p, uint32_t n) { for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8 * i)); }

void PHBattleDecodePlus(const uint8_t bytes[3], uint16_t plus[3]) {
    unsigned extra = 0;
    for (unsigned i = 0; i < 3; ++i) if (bytes[i] > 99) extra += bytes[i] - 99;
    for (unsigned i = 0; i < 3; ++i) plus[i] = (uint16_t)((bytes[i] > 99 ? 99 : bytes[i]) + extra);
}

bool PHBattleEncodePlus(const uint16_t plus[3], uint8_t bytes[3]) {
    if (!plus || !bytes) return false;
    unsigned maximum = 0;
    for (unsigned i = 0; i < 3; ++i) {
        if (plus[i] > 297) return false;
        if (plus[i] > maximum) maximum = plus[i];
    }
    unsigned extra = maximum > 99 ? maximum - 99 : 0, remaining = extra;
    uint8_t result[3];
    for (unsigned i = 0; i < 3; ++i) {
        if (plus[i] < extra) return false;
        unsigned base = plus[i] - extra;
        if (base > 99) return false;
        unsigned add = base == 99 ? (remaining > 99 ? 99 : remaining) : 0;
        result[i] = (uint8_t)(base + add);
        remaining -= add;
    }
    if (remaining) return false;
    memcpy(bytes, result, sizeof(result));
    return true;
}

unsigned PHBattleLatentCost(unsigned latent) {
    if (!latent || latent > 52) return 0;
    if (latent < 12) return 1;
    if (latent <= 51 && ((UINT64_C(0x3c7e00000e000) >> latent) & 1u)) return 6;
    return latent == 51 ? 4 : 2;
}

bool PHBattleEncodeLatents(const uint8_t *latents, size_t count, unsigned capacity, uint64_t *encoded) {
    if (!encoded || count > 8 || (count && !latents) || (capacity != 6 && capacity != 8)) return false;
    uint64_t result = (capacity << 4) | 15u;
    unsigned used = 0;
    for (size_t i = 0; i < count; ++i) {
        unsigned cost = PHBattleLatentCost(latents[i]);
        if (!cost || used + cost > capacity) return false;
        for (unsigned j = 0; j < cost; ++j) result |= (uint64_t)latents[i] << (8 + 7 * used++);
    }
    *encoded = result;
    return true;
}

static bool selection(int16_t requested, uint8_t maximum, uint8_t *out) {
    if (requested < -1 || requested > maximum) return false;
    *out = requested == -1 ? maximum : (uint8_t)requested;
    return true;
}

bool PHBattleBuildProfile(const PHBattleLoadout *loadout, const PHBattleCardInfo *body,
                         const PHBattleCardInfo *assist, PHBattleProfile *profile) {
    if (!loadout || !body || !profile || !loadout->parameters) return false;
    uint16_t bodyID, assistID = 0;
    if (!PHBattleCatalogIDForNumber(loadout->bodyNumber, &bodyID) || bodyID != body->catalogID ||
        !body->maxLevel || body->maxLevel > 120 || !body->maxSkillLevel ||
        body->awakeningCount > 10 || body->superCount > 32) return false;
    uint16_t level = loadout->level ? loadout->level : body->maxLevel;
    uint16_t skill = loadout->skillLevel ? loadout->skillLevel : body->maxSkillLevel;
    if (level > body->maxLevel || skill > body->maxSkillLevel) return false;
    PHBattleProfile result = {{0}};
    put16(result.bytes + 4, bodyID);
    put16(result.bytes + 6, level);
    put16(result.bytes + 8, skill);
    if (!PHBattleEncodePlus(loadout->plus, result.bytes + 24) ||
        !selection(loadout->awakenings, body->awakeningCount, result.bytes + 27)) return false;
    uint16_t super = loadout->superAwakening == -1 ?
        (body->superCount ? body->supers[0] : body->syncAwakening) : (uint16_t)loadout->superAwakening;
    bool validSuper = super == 0;
    for (unsigned i = 0; i < body->superCount; ++i) validSuper |= body->supers[i] == super;
    bool sync = !body->superCount && body->syncAwakening && body->syncAwakening == super;
    validSuper |= sync;
    if (loadout->superAwakening < -1 || !validSuper) return false;
    if (super && !sync && (level < 100 || loadout->plus[0] < 99 || loadout->plus[1] < 99 || loadout->plus[2] < 99)) return false;
    put32(result.bytes + 40, super);
    uint64_t latent = 0;
    if (!PHBattleEncodeLatents(loadout->latents, loadout->latentCount, body->latentSlots, &latent)) return false;
    for (unsigned i = 0; i < 8; ++i) result.bytes[28 + i] = (uint8_t)(latent >> (8 * i));
    if (loadout->assistNumber) {
        if (!assist || !PHBattleCatalogIDForNumber(loadout->assistNumber, &assistID) || assistID > INT16_MAX ||
            assistID != assist->catalogID || !assist->canAssist || !assist->maxLevel || assist->maxLevel > 120 ||
            !assist->maxSkillLevel || assist->awakeningCount > 10) return false;
        uint16_t assistLevel = loadout->assistLevel ? loadout->assistLevel : assist->maxLevel;
        uint16_t assistSkill = loadout->assistSkillLevel ? loadout->assistSkillLevel : assist->maxSkillLevel;
        if (assistLevel > assist->maxLevel || assistSkill > assist->maxSkillLevel ||
            !selection(loadout->assistAwakenings, assist->awakeningCount, result.bytes + 16)) return false;
        put16(result.bytes + 14, assistID);
        result.bytes[16] |= loadout->assistPlus ? 0x80 : 0;
        result.bytes[17] = (uint8_t)assistLevel;
        put16(result.bytes + 18, assistSkill);
    } else {
        result.bytes[17] = 1;
    }
    *profile = result;
    return true;
}
