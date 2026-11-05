#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// Raw catalog IDs, not encyclopedia numbers. Fixed multiplayer variants can
// exceed UINT16_MAX in currentCatalogID; source and form fields remain words.
// Readers decode facts here. Each consumer retains its own catalog/ownership
// validation and must not use planning identity to authorize a native skill.
typedef struct {
    uint32_t currentCatalogID;
    uint16_t sourceBodyID, sourceAssistID, effectiveAssistID;
    uint16_t persistentFormID, temporaryFormID, temporaryTurns;
} PHBattleMemberIdentity;

enum {
    PHBattleAssistDescriptorSize = 8,
    PHBattleAssistAwakeningsByte = 2, PHBattleAssistLevelByte = 3,
    PHBattleAssistSkillLevelByte = 4, PHBattleAssistVariantByte = 6,
    PHBattleAssistPaddingByte = 7
};

// These bits belong specifically to dungeon+33, not dungeon+32 (skill and
// leader restrictions) or a temporary enemy effect duration.
enum {
    PHBattleDungeonAwakeningsDisabled = 0x04,
    PHBattleDungeonAssistsDisabled = 0x08,
    PHBattleDungeonLatentsDisabled = 0x20
};
typedef struct {
    uintptr_t address;
    uint8_t flags;
    bool known;
} PHBattleDungeonMemberRules;

static inline bool PHBattleSameDungeonMemberRules(const PHBattleDungeonMemberRules *a,
                                                 const PHBattleDungeonMemberRules *b) {
    return a && b && a->known && b->known && a->address == b->address && a->flags == b->flags;
}

static inline bool PHBattleDungeonDisables(const PHBattleDungeonMemberRules *rules, uint8_t mask) {
    return rules && rules->known && (rules->flags & mask) != 0;
}

static inline bool PHBattleEffectiveAssistAvailable(const PHBattleDungeonMemberRules *rules, uint16_t assistID) {
    return rules && rules->known && !PHBattleDungeonDisables(rules, PHBattleDungeonAssistsDisabled) &&
        (int16_t)assistID > 0;
}

static inline uint16_t PHBattleAssistDescriptorID(const uint8_t bytes[PHBattleAssistDescriptorSize]) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline void PHBattleNormalizeAssistDescriptor(uint8_t bytes[PHBattleAssistDescriptorSize]) {
    // Native 0x100531AEC/0x10048127C copy stack padding. The no-assist
    // initializer also leaves the unused skill word untouched.
    bytes[PHBattleAssistPaddingByte] = 0;
    if (!PHBattleAssistDescriptorID(bytes))
        bytes[PHBattleAssistSkillLevelByte] = bytes[PHBattleAssistSkillLevelByte + 1] = 0;
}

static inline bool PHBattleEffectiveAssistMatches(const uint8_t effective[PHBattleAssistDescriptorSize],
    const uint8_t configured[PHBattleAssistDescriptorSize], const PHBattleDungeonMemberRules *rules) {
    if (!effective || !configured || !rules || !rules->known) return false;
    uint8_t actual[PHBattleAssistDescriptorSize], expected[PHBattleAssistDescriptorSize] = {0};
    memcpy(actual, effective, sizeof(actual));
    if (PHBattleDungeonDisables(rules, PHBattleDungeonAssistsDisabled)) expected[PHBattleAssistLevelByte] = 1;
    else memcpy(expected, configured, sizeof(expected));
    PHBattleNormalizeAssistDescriptor(actual);
    PHBattleNormalizeAssistDescriptor(expected);
    return memcmp(actual, expected, sizeof(actual)) == 0;
}

static inline uint32_t PHBattleUnderlyingBodyID(const PHBattleMemberIdentity *member) {
    return member->persistentFormID ? member->persistentFormID : member->sourceBodyID;
}

static inline bool PHBattleSameMemberIdentity(const PHBattleMemberIdentity *a, const PHBattleMemberIdentity *b) {
    return a && b && a->currentCatalogID == b->currentCatalogID && a->sourceBodyID == b->sourceBodyID &&
        a->sourceAssistID == b->sourceAssistID && a->effectiveAssistID == b->effectiveAssistID &&
        a->persistentFormID == b->persistentFormID && a->temporaryFormID == b->temporaryFormID &&
        a->temporaryTurns == b->temporaryTurns;
}

// Return catalog IDs for plan continuity, without guessing whether a form is
// valid in a particular mode's catalog. The caller checks those tables.
static inline bool PHBattlePlanningIdentity(const PHBattleMemberIdentity *member,
                                           uint32_t *bodyID, uint16_t *assistID) {
    if (!member || !bodyID || !assistID || !member->currentCatalogID) return false;
    uint32_t body = member->currentCatalogID;
    uint16_t assist = member->effectiveAssistID;
    if (member->temporaryTurns) {
        if (!member->temporaryFormID || member->currentCatalogID != member->temporaryFormID) return false;
        body = PHBattleUnderlyingBodyID(member);
        assist = member->sourceAssistID;
        if (!body) return false;
    }
    *bodyID = body; *assistID = assist;
    return true;
}

static inline bool PHBattleAssistIsRetained(uint16_t nativeState) {
    // Protected member+472. Type 250 consumes an assist by clearing this.
    return (nativeState & 255u) == 172;
}
