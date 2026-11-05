#pragma once
#include "PHBattleMemberState.h"
#include "PHBattleModel.h"
#include "PHBattleGenerated.h"
#include "PHMemory.h"
#include <cstring>

// Shared read-only adapter. No game getters, policy decisions, catalog
// lookups or writes. The callback addresses are relative to one member.
template<class Read>
static bool PHDecodeBattleMemberIdentity(Read read, uint32_t key1, uint32_t key2,
                                         PHBattleMemberIdentity &output) {
    PHBattleMemberIdentity value{};
    auto word = [&](uintptr_t offset, uint16_t &out) {
        uint32_t raw[4], decoded = 0;
        if (!read(offset, raw, sizeof(raw)) ||
            !PHBattleDecodeProtected(raw, key1, key2, 0x6a875a8bu, &decoded) || decoded > UINT16_MAX) return false;
        out = uint16_t(decoded); return true;
    };
    if (!read(PHBattleCatalogIDOffset, &value.currentCatalogID, sizeof(value.currentCatalogID)) ||
        !read(PHBattleBaseIDOffset, &value.sourceBodyID, sizeof(value.sourceBodyID)) ||
        !read(PHBattleAssistIDOffset, &value.sourceAssistID, sizeof(value.sourceAssistID)) ||
        !read(PHBattleEffectiveAssistOffset, &value.effectiveAssistID, sizeof(value.effectiveAssistID)) ||
        !word(PHBattleFormOffset, value.persistentFormID) ||
        !word(PHBattleTempIDOffset, value.temporaryFormID) ||
        !word(PHBattleTempTurnsOffset, value.temporaryTurns)) return false;
    output = value; return true;
}

static inline bool PHReadBattleMemberIdentity(uintptr_t address, uint32_t key1, uint32_t key2,
                                              PHBattleMemberIdentity &output) {
    return PHDecodeBattleMemberIdentity([&](uintptr_t offset, void *out, size_t length) {
        return address <= UINTPTR_MAX - offset && address + offset <= UINTPTR_MAX - length &&
            PHReadMemory(address + offset, out, length);
    }, key1, key2, output);
}

static inline bool PHReadBattleMemberIdentityBytes(const uint8_t *bytes, size_t size,
    uint32_t key1, uint32_t key2, PHBattleMemberIdentity &output) {
    return PHDecodeBattleMemberIdentity([&](uintptr_t offset, void *out, size_t length) {
        if (!bytes || offset > size || length > size - offset) return false;
        std::memcpy(out, bytes + offset, length); return true;
    }, key1, key2, output);
}

static inline bool PHReadDungeonMemberRules(uintptr_t dungeon, PHBattleDungeonMemberRules &output) {
    PHBattleDungeonMemberRules value{};
    value.address = dungeon;
    // A successfully read null pointer is distinct from an unreadable rule.
    // Native skill selection accepts no rule object; entry replacement
    // requires a non-null object explicitly at its mutation boundary.
    if (dungeon && (dungeon > UINTPTR_MAX - PHBattleDungeonMemberFlagsOffset - 1 ||
        !PHReadU8(dungeon + PHBattleDungeonMemberFlagsOffset, &value.flags))) return false;
    value.known = true; output = value; return true;
}

static inline bool PHReadBattleDungeonMemberRules(uintptr_t impl, PHBattleDungeonMemberRules &output) {
    uintptr_t dungeon = 0;
    return impl <= UINTPTR_MAX - PHBattleDungeonPointerOffset - sizeof(dungeon) &&
        PHReadPointer(impl + PHBattleDungeonPointerOffset, &dungeon) && PHReadDungeonMemberRules(dungeon, output);
}
