// Exercise the production decoder, fingerprints, provider and transactions.
// Only mapped memory, bundle identity and native game calls are test doubles.
#define PH_BATTLE_NATIVE_TESTS 1
#include "../src/PHBattleNative.mm"
#include <cstdlib>
#include <map>
#include <vector>
#include <cstdio>

static unsigned failures;
static void Check(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

struct Fixture;
static Fixture *fixture;

struct Fixture {
    std::vector<uint8_t> image, implementation, catalog, input, dungeon, skills;
    std::map<uintptr_t, size_t> allocations;
    uint8_t originals[6][0x5f0] = {};
    unsigned rebuilds = 0, refreshes = 0, allocated = 0, released = 0;
    unsigned failRebuild = 0;
    bool auditOnRefresh = false;
    uintptr_t failWriteAddress = 0;
    size_t failWriteLength = 0, partialWrite = 0;
    unsigned failWriteOccurrence = 1;
    // The native second protection key shares the player's save field.
    static constexpr uint32_t Key1 = 0x12345678, Key2 = 42;

    Fixture() : image(0x3000000), implementation(0xc4000), catalog(15000 * PHBattleCatalogStride),
                input(PHInputDataSize), dungeon(128), skills(104) {
        fixture = this;
        PHTestNativeCalls = {Set16, Rebuild, Refresh, Digest, MaxCharge, Radius, SetByte, Allocate, Release};
        for (size_t i = 0; i < PHBattleTextGuardCount; ++i)
            std::memcpy(image.data() + PHBattleTextGuards[i].rva, PHBattleTextGuards[i].bytes, 64);
        put<uint8_t>(header() + PHSaveRVA, 1); put<uint8_t>(header() + PHSaveRVA + 1, 1);
        put<uint32_t>(header() + PHSaveRVA + PHPlayerIDOffset, 42);
        put<uintptr_t>(header() + PHSceneTableRVA + 24, game());
        put<uintptr_t>(game() + 0x70, impl()); put<uintptr_t>(header() + PHBattleImplRVA, impl());
        put<uintptr_t>(impl() + PHBattleDungeonPointerOffset, (uintptr_t)dungeon.data());
        put<uint16_t>(header() + PHSaveRVA + PHBattleDungeonOffset, 3);
        put<uint16_t>(header() + PHSaveRVA + PHBattleDungeonOffset + 2, 4);
        put<uint8_t>(header() + PHBattleEntryHashRVA, 1);
        put<uint16_t>(header() + PHBattleModeRVA, 12);
        put<uint32_t>(header() + PHSaveRVA + PHBattleCatalogCountOffset, 15000);
        put<uintptr_t>(header() + PHSaveRVA + PHBattleCatalogPointerOffset, (uintptr_t)catalog.data());
        put<uintptr_t>(header() + PHSaveRVA + PHBattleSkillPointerOffset, (uintptr_t)skills.data());
        put<uint16_t>((uintptr_t)skills.data() + 52 + 14, 5);
        put<uintptr_t>(header() + PHBattleInputRVA, (uintptr_t)input.data());
        put<uint32_t>(header() + PHBattleKey1RVA, Key1); put<uint32_t>(header() + PHBattleKey2RVA, Key2);
        put<uint8_t>(header() + PHBattleScaleRVA, 64);
        encode(impl() + PHBattlePhaseOffset, 2, 0x766cfc6du);
        encode(impl() + PHBattleHPOffset, 100, 0x766cfc6du);
        encode(impl() + PHBattleEnemyCountOffset, 0, 0x766cfc6du);
        for (unsigned i = 0; i < PHBattleEventCount; ++i)
            put<uint32_t>(header() + PHBattleScriptRVA + PHBattleScriptEventsOffset + i * 8, UINT32_MAX);
        for (unsigned id = 1; id < 15000; ++id) {
            uintptr_t card = (uintptr_t)catalog.data() + id * PHBattleCatalogStride;
            put<uint32_t>(card, id); put<uint16_t>(card + 20, 99);
            put<uint32_t>(card + 84, 1); put<uint16_t>(card + 826, 3); put<uint16_t>(card + 832, 1);
            for (unsigned a = 0; a < 9; ++a) put<uint16_t>(card + 748 + a * 2, 1);
        }
        for (unsigned slot = 0; slot < 6; ++slot) {
            uintptr_t m = member(slot);
            put<uint8_t>(m, 1); put<uint16_t>(m + PHBattleBaseIDOffset, (uint16_t)(100 + slot));
            put<uint32_t>(m + PHBattleCatalogIDOffset, 100 + slot);
            put<uintptr_t>(m + PHBattleCardParamOffset, (uintptr_t)catalog.data() + (100 + slot) * PHBattleCatalogStride);
            encode(m + PHBattleSourceSlotOffset, slot); encode(m + PHBattleScriptSlotOffset, 255);
            for (uintptr_t offset : {PHBattleFormOffset, PHBattleTempIDOffset, PHBattleTempTurnsOffset}) encode(m + offset, 0);
            encode(m + PHBattleChargeOffset, 4);
            put<uint16_t>(m + 0x510, 99); put<uint16_t>(m + 0x512, 1);
            put<uint8_t>(m + 63, 20);
            std::memcpy(originals[slot], (void *)m, sizeof(originals[slot]));
        }
        shadows();
    }
    ~Fixture() { for (auto &allocation : allocations) std::free((void *)allocation.first); fixture = nullptr; }
    uintptr_t header() const { return (uintptr_t)image.data(); }
    uintptr_t game() const { return header() + PHGameMainRVA; }
    uintptr_t impl() const { return (uintptr_t)implementation.data(); }
    uintptr_t member(unsigned slot) const { return impl() + PHBattleMemberOffset + slot * PHBattleMemberStride; }
    uintptr_t parameter(unsigned slot) const { return header() + PHBattleScriptRVA + PHBattleParameterOffset + slot * PHBattleProfileSize; }
    template <typename T> static void put(uintptr_t address, T value) { std::memcpy((void *)address, &value, sizeof(value)); }
    template <typename T> static T get(uintptr_t address) { T value; std::memcpy(&value, (void *)address, sizeof(value)); return value; }
    static void encode(uintptr_t address, uint32_t value, uint32_t salt = 0x6a875a8bu) {
        uint32_t raw[4] = {((value << 5) | (value >> 27)) ^ salt, 0, value ^ Key1 ^ Key2, value};
        std::memcpy((void *)address, raw, sizeof(raw));
    }
    static uint32_t decode(uintptr_t address) {
        uint32_t raw[4], value = UINT32_MAX;
        std::memcpy(raw, (void *)address, sizeof(raw));
        Check(PHBattleDecodeProtected(raw, Key1, Key2, 0x6a875a8bu, &value), "native double sees unanimous protected data");
        return value;
    }
    bool mapped(uintptr_t address, size_t length) const {
        if (!length || address > UINTPTR_MAX - length) return false;
        auto contains = [&](uintptr_t base, size_t bytes) { return address >= base && address - base <= bytes && length <= bytes - (address - base); };
        for (const auto *region : {&image, &implementation, &catalog, &input, &dungeon, &skills})
            if (contains((uintptr_t)region->data(), region->size())) return true;
        for (const auto &allocation : allocations) if (contains(allocation.first, allocation.second)) return true;
        return false;
    }
    PHBattleFrame frame() const { return {header(), 42, 1, true, true, {}}; }
    PHBattleState state(id<PHBattleEngine> engine) const {
        PHBattleState value = {};
        NSString *reason = nil;
        BOOL read = [engine readState:&value frame:frame() reason:&reason];
        if (!read) std::fprintf(stderr, "Native fixture read rejected: %s\n", reason.UTF8String ?: "no reason");
        Check(read, "production reader accepts the fixture");
        return value;
    }
    void shadows() {
        put<uint32_t>(impl() + PHBattleHPShadowOffset, 100 ^ 0x8085a6beu);
        put<uint32_t>(impl() + PHBattleDigestShadowOffset, (uint32_t)Digest(game()) ^ 0x689d8b5cu);
    }
    static uint64_t Set16(uintptr_t address, uint64_t value) { encode(address, (uint16_t)value); return value; }
    static uint64_t SetByte(uintptr_t address, uint64_t value) { encode(address, (uint8_t)value); return value; }
    static uintptr_t Rebuild(uintptr_t game, uintptr_t m, uint64_t argument) {
        Fixture &f = *fixture; ++f.rebuilds;
        Check(game == f.game() && argument == UINT32_MAX, "provider uses the normal rebuild ABI");
        unsigned source = decode(m + PHBattleSourceSlotOffset);
        Check(source < 6, "native rebuild source stays in the six saved slots");
        int script = (int8_t)(uint8_t)decode(m + PHBattleScriptSlotOffset);
        uint16_t form = (uint16_t)decode(m + PHBattleFormOffset), base = 0, assist = 0;
        if (script > 0) {
            Check(get<uintptr_t>(f.header() + PHBattleScriptRVA + 8) != 0, "parameter member retains a real published script buffer");
            uintptr_t p = f.parameter((unsigned)script - 1);
            base = get<uint16_t>(p + 4); assist = get<uint16_t>(p + 14);
            std::memcpy((void *)(m + 0x510), (void *)(p + 6), 4);
            std::memcpy((void *)(m + 0x4f0), (void *)(p + 24), 20);
            std::memcpy((void *)(m + 0x59e), (void *)(p + 14), 8);
        } else {
            uintptr_t saved = (uintptr_t)f.originals[source];
            base = get<uint16_t>(saved + PHBattleBaseIDOffset); assist = get<uint16_t>(saved + PHBattleAssistIDOffset);
            std::memcpy((void *)(m + 0x510), (void *)(saved + 0x510), 4);
            std::memcpy((void *)(m + 0x4f0), (void *)(saved + 0x4f0), 20);
            std::memcpy((void *)(m + 0x59e), (void *)(saved + 0x59e), 8);
        }
        uint32_t catalogID = form ? form : base;
        put<uint16_t>(m + PHBattleBaseIDOffset, base); put<uint16_t>(m + PHBattleAssistIDOffset, assist);
        put<uint32_t>(m + PHBattleCatalogIDOffset, catalogID);
        put<uintptr_t>(m + PHBattleCardParamOffset, (uintptr_t)f.catalog.data() + catalogID * PHBattleCatalogStride);
        return f.failRebuild == f.rebuilds ? 0 : m;
    }
    static uint64_t Refresh(uintptr_t game, uint64_t value) {
        Fixture &f = *fixture; ++f.refreshes;
        Check(game == f.game() && value == 0, "refresh uses the normal team ABI");
        if (f.auditOnRefresh) put<uint32_t>(f.header() + PHSaveRVA + PHBattleAuditOffset, 0x10000);
        return 0;
    }
    static uint64_t Digest(uintptr_t game) {
        Check(game == fixture->game(), "digest uses the active game object");
        uint32_t hash = 2166136261u;
        for (unsigned slot = 0; slot < 6; ++slot) {
            uintptr_t m = fixture->member(slot);
            for (uintptr_t offset : {PHBattleBaseIDOffset, PHBattleCatalogIDOffset, (uintptr_t)0x510})
                hash = (hash ^ get<uint16_t>(m + offset)) * 16777619u;
        }
        return hash & 0x7fffffffu;
    }
    static uint64_t MaxCharge(uintptr_t member, uint64_t value) { (void)member; (void)value; return 6; }
    static float Radius() { return 1; }
    static uintptr_t Allocate(uint64_t bytes, uint64_t category, uint64_t tag) {
        Check(bytes == 16 && category == 4 && tag == 44, "empty program uses the native allocator category/tag");
        uintptr_t memory = (uintptr_t)std::calloc(1, (size_t)bytes);
        fixture->allocations[memory] = (size_t)bytes; ++fixture->allocated;
        return memory;
    }
    static void Release(uintptr_t memory, int category, int tag) {
        Check(category == 4 && tag == 44 && fixture->allocations.count(memory), "program release matches an owned allocation");
        Check(get<uintptr_t>(fixture->header() + PHBattleScriptRVA + 8) != memory, "an attached native program is never freed");
        fixture->allocations.erase(memory); std::free((void *)memory); ++fixture->released;
    }
};

BOOL PHMemoryHasProtection(uintptr_t address, size_t length, vm_prot_t required, vm_prot_t forbidden) {
    return fixture && fixture->mapped(address, length) && !(required & VM_PROT_EXECUTE) &&
           !(forbidden & (VM_PROT_READ | VM_PROT_WRITE));
}
BOOL PHReadMemory(uintptr_t address, void *output, size_t length) {
    if (!fixture || !output || !fixture->mapped(address, length)) return NO;
    std::memcpy(output, (void *)address, length); return YES;
}
BOOL PHWriteData(uintptr_t address, const void *input, size_t length) {
    if (!fixture || !input || !fixture->mapped(address, length)) return NO;
    if (fixture->failWriteAddress == address && (!fixture->failWriteLength || fixture->failWriteLength == length) &&
        --fixture->failWriteOccurrence == 0) {
        fixture->failWriteAddress = 0;
        std::memcpy((void *)address, input, std::min(length, fixture->partialWrite));
        return NO;
    }
    std::memcpy((void *)address, input, length); return YES;
}
BOOL PHValidateGuest(const mach_header_64 *header, NSString **reason) {
    (void)reason; return fixture && (uintptr_t)header == fixture->header();
}
BOOL PHAddressIsGuestCode(uintptr_t address, const mach_header_64 *header) {
    return fixture && (uintptr_t)header == fixture->header() && address >= fixture->header() + 0x4000 &&
           address < fixture->header() + 0x1110000;
}

static PHBattleLoadout Loadout(unsigned slot = 0) {
    PHBattleLoadout value = {};
    value.bodyNumber = (uint16_t)(200 + slot); value.assistNumber = (uint16_t)(300 + slot);
    value.awakenings = value.assistAwakenings = value.superAwakening = -1;
    value.plus[0] = value.plus[1] = value.plus[2] = 297;
    value.assistLevel = 99; value.assistSkillLevel = 2; value.assistPlus = value.parameters = true;
    return value;
}

int main() {
    @autoreleasepool {
        {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleMember originals[6], applied[6];
            for (unsigned i = 0; i < 6; ++i) {
                PHBattleState before = f.state(engine); originals[i] = before.members[i];
                PHBattleLoadout loadout = Loadout(i);
                Check([engine applyLoadout:&loadout state:&before slot:i member:&applied[i] reason:nullptr] == PHBattleMutationApplied,
                      "production provider applies each of six body/weapon profiles");
                Check(applied[i].level == 120 && applied[i].skillLevel == 5 && applied[i].providerToken &&
                      applied[i].assistID == 300 + i && applied[i].assist[4] == 2,
                      "native output retains body maximum and separately chosen weapon skill level");
                uint16_t plus[3]; PHBattleDecodePlus(applied[i].extra, plus);
                Check(plus[0] == 297 && plus[1] == 297 && plus[2] == 297, "full native profile carries +891");
            }
            Check(f.allocated == 1 && f.released == 0, "six leases share one valid event-free program");
            uint8_t swapped[0x5f0]; std::memcpy(swapped, (void *)f.member(0), sizeof(swapped));
            std::memcpy((void *)f.member(0), (void *)f.member(5), sizeof(swapped));
            std::memcpy((void *)f.member(5), swapped, sizeof(swapped));
            Fixture::Set16(f.member(5) + PHBattleFormOffset, 250);
            Fixture::Rebuild(f.game(), f.member(5), UINT32_MAX);
            Fixture::Set16(f.member(5) + PHBattleChargeOffset, 9); f.shadows();
            PHBattleState before = f.state(engine);
            Check(before.members[5].providerToken == applied[0].providerToken, "ownership follows leader swap and native transformation");
            Check([engine restoreLoadout:&originals[0] state:&before slot:5 reason:nullptr] == PHBattleMutationApplied,
                  "production restore finds the transformed source at its new position");
            before = f.state(engine);
            Check(before.members[5].catalogID == 100 && before.members[5].charge == 6 && before.members[5].level == 99,
                  "restore preserves the current capped charge and original training");
            for (unsigned i = 1; i < 6; ++i) {
                unsigned position = i == 5 ? 0 : i;
                before = f.state(engine);
                Check([engine restoreLoadout:&originals[i] state:&before slot:position reason:nullptr] == PHBattleMutationApplied,
                      "all remaining production leases restore independently");
            }
            Check(f.released == 1 && !Fixture::get<uintptr_t>(f.header() + PHBattleScriptRVA + 8),
                  "the last restore unpublishes before freeing the owned native program");
        }
        for (size_t partial : {(size_t)0, (size_t)10}) {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            f.failWriteAddress = f.parameter(0); f.failWriteLength = PHBattleProfileSize; f.partialWrite = partial;
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRolledBack,
                  "failed or partial parameter publication cleans up an unbound profile");
            PHBattleProfile zero = {}, restored = {};
            PHReadMemory(f.parameter(0), &restored, sizeof(restored));
            Check(f.rebuilds == 0 && f.allocated == f.released && !std::memcmp(&zero, &restored, sizeof(zero)),
                  "an untouched member is not stranded in a fault state by a parameter write failure");
            before = f.state(engine);
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationApplied,
                  "a verified cleanup permits a later retry");
        }
        {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            f.failWriteAddress = f.header() + PHBattleScriptRVA + 8;
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRejected &&
                  f.allocated == f.released && f.rebuilds == 0, "an unpublished program failure frees only its detached allocation");
        }
        {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            f.failRebuild = 1; PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRolledBack,
                  "a partial native rebuild restores the original source and releases the profile");
            before = f.state(engine);
            Check(before.members[0].catalogID == 100 && before.members[0].scriptSlot == -1 && f.released == 1,
                  "native rollback returns the complete original member");
        }
        {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            f.auditOnRefresh = true; PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRolledBack,
                  "a new native audit flag rejects and rolls back the profile");
            before = f.state(engine);
            Check(before.auditFlags == 0x10000 && PHBattleStateGate(&before) == PHBattleGateAudit,
                  "rollback preserves the game's audit flags");
        }
        {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationApplied,
                  "release-failure fixture starts with a valid owned member");
            PHBattleMember original = before.members[0]; before = f.state(engine);
            f.failWriteAddress = f.header() + PHBattleScriptRVA + 8;
            Check([engine restoreLoadout:&original state:&before slot:0 reason:nullptr] == PHBattleMutationUncertain && f.released == 0,
                  "an unconfirmed detach never frees an attached allocation");
            before = f.state(engine);
            Check([engine applyLoadout:&loadout state:&before slot:1 member:&output reason:nullptr] == PHBattleMutationRejected,
                  "an uncertain native cleanup latches off future mutations");
        }
        for (uint32_t multi : {1u, 4u}) {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine();
            Fixture::put<uint32_t>(f.header() + PHBattleMultiplayerRVA, multi);
            PHBattleFrame frame = f.frame(); frame.policy.configured = true;
            frame.policy.modes.allowAll = frame.policy.sources.allowAll = frame.policy.multiplayer.allowAll = true;
            PHBattleState before = {}; Check([engine readState:&before frame:frame reason:nullptr], "banked selector can be diagnosed");
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check(PHBattleStateGate(&before) == PHBattleGateLayout &&
                  [engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRejected && !f.rebuilds,
                  "allow-all filters cannot send the six-member transaction into banked native calls");
        }
        {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            Fixture::put<uint16_t>(f.header() + PHSaveRVA + PHBattleSourceModeOffset, 3);
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRejected && !f.allocated,
                  "a selector changed after the snapshot cannot enter a native transaction");
        }
        std::printf("Native battle provider tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
