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
    uint8_t bankOriginals[PHBattleTeamCapacity][6][0x5f0] = {};
    bool bankedTeams = false, soloRule = false, sharedHelperRule = false, sharedLeaderRule = false;
    unsigned nativeTeamSize = 5;
    bool changeOtherBankOnRefresh = false;
    unsigned rebuilds = 0, refreshes = 0, allocated = 0, released = 0;
    unsigned switches = 0, failSwitch = 0, changedBank = 1;
    unsigned failRebuild = 0;
    bool auditOnRefresh = false;
    unsigned advances = 0, memberAdvances = 0, failAdvanceTick = 0, closeWindowAtTick = 0;
    uint16_t chargeCaps[6] = {6,6,6,6,6,6};
    bool awakeningsDisabled = false, chargingDisabled = false, invalidAwakeningCount = false;
    uint32_t newTeamHPCap = 0;
    int32_t badgeSkillBoost = 2;
    uintptr_t failWriteAddress = 0;
    size_t failWriteLength = 0, partialWrite = 0;
    unsigned failWriteOccurrence = 1;
    // The native second protection key shares the player's save field.
    static constexpr uint32_t Key1 = 0x12345678, Key2 = 42;

    Fixture() : image(0x3000000), implementation(0xc4000), catalog(15000 * PHBattleCatalogStride),
                input(PHInputDataSize), dungeon(128), skills(104) {
        fixture = this;
        PHTestNativeCalls = {Set16, Rebuild, Refresh, Digest, MaxCharge, Radius, SetByte, Allocate, Release,
            Set32, TeamAwakening, TeamLatent, CurrentBadge, BadgeBoost, AdvanceTeam, AdvanceMember, MemberHasSkill,
            TeamSize, SoloRules, SharedHelper, SharedLeader, SwitchTeam};
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
            put<uint32_t>(m + PHBattleChargeShadowOffset, 4 ^ 0x68fb65ceu);
            put<uint8_t>(m + 0x4f3, 9);
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
    uintptr_t bank(unsigned team, unsigned slot = 0) const {
        return header() + PHBattleTeamStorageRVA + team * PHBattleTeamStorageStride + slot * PHBattleMemberStride;
    }
    std::pair<unsigned, unsigned> sourceForView(unsigned slot) const {
        unsigned current = get<uint32_t>(impl() + PHBattleCurrentTeamOffset);
        unsigned team = slot < nativeTeamSize ? current : current ^ 1u;
        unsigned position = slot < nativeTeamSize ? slot : 0;
        if (get<uint16_t>(header() + PHBattleModeRVA) != 7 && !soloRule) {
            unsigned local = get<uint32_t>(impl() + PHBattleSharedTeamOffset);
            if ((sharedHelperRule || sharedLeaderRule) && slot >= nativeTeamSize) { team = local; position = 5; }
            else if (sharedLeaderRule && slot == 0) { team = local; position = 0; }
        }
        return {team, position};
    }
    void loadView(unsigned team) {
        put<uint32_t>(impl() + PHBattleCurrentTeamOffset, team);
        for (unsigned slot = 0; slot < 6; ++slot) {
            auto source = sourceForView(slot);
            std::memcpy((void *)member(slot), (void *)bank(source.first, source.second), PHBattleMemberStride);
            if (slot >= nativeTeamSize && get<uint16_t>(header() + PHBattleModeRVA) != 7 && !soloRule)
                encode(member(slot) + PHBattleSourceSlotOffset, 5);
        }
        if (threePlayer()) {
            Set32(impl() + PHBattleHPOffset, decode(bank(team) + PHBattleTeamHPOffset, 0x766cfc6du));
            put<uint32_t>(impl() + PHBattleHPShadowOffset, decode(impl() + PHBattleHPOffset, 0x766cfc6du) ^ 0x8085a6beu);
        }
    }
    bool threePlayer() const {
        return get<uint16_t>(header() + PHBattleModeRVA) == 7 && get<uint32_t>(header() + PHBattleMultiplayerRVA) == 1;
    }
    void configureBanks(uint32_t mode, unsigned count = 2, unsigned size = 5, unsigned player = 0) {
        bankedTeams = true; nativeTeamSize = size;
        put<uint32_t>(header() + PHBattleMultiplayerRVA, mode);
        if (size == 6 && !soloRule) put<uint16_t>(header() + PHBattleModeRVA, 7);
        put<uint32_t>(impl() + PHBattleTeamCountOffset, count);
        put<uint32_t>(impl() + PHBattleCurrentTeamOffset, 0);
        put<uint32_t>(impl() + PHBattleSharedTeamOffset, 1);
        put<uint32_t>(impl() + PHBattlePlayerTeamOffset, player);
        put<uint32_t>(impl() + PHBattleTurnSerialOffset, 0);
        if (sharedHelperRule || sharedLeaderRule) {
            uintptr_t object = header() + 0x1600000;
            put<uint32_t>((uintptr_t)dungeon.data() + PHBattleDungeonRulesOffset, 1);
            put<uintptr_t>(header() + PHSaveRVA + PHBattleDungeonRulePoolOffset, object);
            put<uintptr_t>(object + 16, object + 64);
        }
        for (unsigned team = 0; team < count; ++team) {
            uintptr_t descriptor = impl() + PHBattleTeamDescriptorOffset + team * PHBattleTeamDescriptorStride;
            put<uint8_t>(descriptor, 13); put<uintptr_t>(descriptor + PHBattleTeamPointerOffset, bank(team));
            encode(bank(team) + PHBattleTeamHPOffset, 100 + 50 * team, 0x766cfc6du);
            for (unsigned slot = 0; slot < 6; ++slot) {
                uintptr_t m = bank(team,slot);
                std::memcpy((void *)m, originals[slot], PHBattleMemberStride);
                uint16_t id = (uint16_t)(100 + 1000 * team + slot);
                put<uint16_t>(m + PHBattleBaseIDOffset, id); put<uint32_t>(m + PHBattleCatalogIDOffset, id);
                put<uintptr_t>(m + PHBattleCardParamOffset, (uintptr_t)catalog.data() + id * PHBattleCatalogStride);
                bool active = slot < size || ((sharedHelperRule || sharedLeaderRule) && team == 1 && slot == 5);
                put<uint8_t>(m, active ? 1 : 0);
                std::memcpy(bankOriginals[team][slot], (void *)m, PHBattleMemberStride);
            }
        }
        if (sharedLeaderRule) for (unsigned team = 0; team < count; ++team)
            std::memcpy((void *)bank(team), (void *)bank(1), PHBattleMemberStride);
        loadView(0); shadows();
    }
    void syncBanks() {
        if (!bankedTeams) return;
        unsigned current = get<uint32_t>(impl() + PHBattleCurrentTeamOffset);
        for (unsigned slot = 0; slot < 6; ++slot) {
            auto source = sourceForView(slot);
            uintptr_t stored = bank(source.first, source.second);
            uint32_t sourceID = decode(stored + PHBattleSourceSlotOffset);
            std::memcpy((void *)stored, (void *)member(slot), PHBattleMemberStride);
            encode(stored + PHBattleSourceSlotOffset, sourceID);
            if (sharedLeaderRule && slot == 0 && current != source.first)
                std::memcpy((void *)bank(current), (void *)stored, PHBattleMemberStride);
        }
        if (threePlayer()) encode(bank(current) + PHBattleTeamHPOffset, decode(impl() + PHBattleHPOffset, 0x766cfc6du), 0x766cfc6du);
    }
    template <typename T> static void put(uintptr_t address, T value) { std::memcpy((void *)address, &value, sizeof(value)); }
    template <typename T> static T get(uintptr_t address) { T value; std::memcpy(&value, (void *)address, sizeof(value)); return value; }
    static void encode(uintptr_t address, uint32_t value, uint32_t salt = 0x6a875a8bu) {
        uint32_t raw[4] = {((value << 5) | (value >> 27)) ^ salt, 0, value ^ Key1 ^ Key2, value};
        std::memcpy((void *)address, raw, sizeof(raw));
    }
    static uint32_t decode(uintptr_t address, uint32_t salt = 0x6a875a8bu) {
        uint32_t raw[4], value = UINT32_MAX;
        std::memcpy(raw, (void *)address, sizeof(raw));
        Check(PHBattleDecodeProtected(raw, Key1, Key2, salt, &value), "native double sees unanimous protected data");
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
    PHBattleFrame frame() const {
        PHBattleFrame value = {header(), 42, 1, true, true, {}, false};
        if (bankedTeams) {
            value.policy.configured = true;
            value.policy.modes.allowAll = value.policy.sources.allowAll = value.policy.multiplayer.allowAll = true;
        }
        return value;
    }
    void enter() {
        encode(impl() + PHBattlePhaseOffset, threePlayer() ? 46 : 0, 0x766cfc6du);
        for (uintptr_t offset : {PHBattleWaveOffset, PHBattleWaveCopyOffset, PHBattleWaveBackupOffset})
            encode(impl() + offset, 0, 0x766cfc6du);
        put<uint8_t>(impl() + PHBattleIntroOffset, 1);
        put<uint32_t>(header() + PHSaveRVA + PHBattleResumeFlagsOffset, 0);
    }
    void awakening(unsigned card, unsigned index, uint16_t id) {
        put<uint16_t>((uintptr_t)catalog.data() + card * PHBattleCatalogStride + 748 + index * 2, id);
    }
    PHBattleState state(id<PHBattleEngine> engine) const {
        PHBattleState value = {};
        NSString *reason = nil;
        BOOL read = [engine readState:&value frame:frame() reason:&reason];
        if (!read) std::fprintf(stderr, "Native fixture read rejected: %s\n", reason.UTF8String ?: "no reason");
        Check(read, "production reader accepts the fixture");
        return value;
    }
    void shadows() {
        put<uint32_t>(impl() + PHBattleHPShadowOffset, decode(impl() + PHBattleHPOffset, 0x766cfc6du) ^ 0x8085a6beu);
        put<uint32_t>(impl() + PHBattleDigestShadowOffset, (uint32_t)Digest(game()) ^ 0x689d8b5cu);
    }
    static uint64_t Set16(uintptr_t address, uint64_t value) { encode(address, (uint16_t)value); return value; }
    static uint64_t Set32(uintptr_t address, uint64_t value) { encode(address, (uint32_t)value, 0x766cfc6du); return value; }
    static uint64_t SetByte(uintptr_t address, uint64_t value) { encode(address, (uint8_t)value); return value; }
    static uintptr_t Rebuild(uintptr_t game, uintptr_t m, uint64_t argument) {
        Fixture &f = *fixture; ++f.rebuilds;
        Check(game == f.game() && argument == UINT32_MAX, "provider uses the normal rebuild ABI");
        unsigned source = decode(m + PHBattleSourceSlotOffset);
        Check(source < 6, "native rebuild source stays in the six saved slots");
        unsigned sourceTeam = 0;
        if (f.bankedTeams) {
            if (m >= f.member(0) && m < f.member(0) + 6 * PHBattleMemberStride) {
                auto owner = f.sourceForView((unsigned)((m - f.member(0)) / PHBattleMemberStride));
                sourceTeam = owner.first;
                source = decode(f.bank(owner.first, owner.second) + PHBattleSourceSlotOffset);
            } else sourceTeam = (unsigned)((m - f.bank(0)) / PHBattleTeamStorageStride);
        }
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
            uintptr_t saved = f.bankedTeams ? (uintptr_t)f.bankOriginals[sourceTeam][source] : (uintptr_t)f.originals[source];
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
        if (f.newTeamHPCap) {
            for (unsigned i = 0; i < 6; ++i) if ((int8_t)decode(f.member(i) + PHBattleScriptSlotOffset) > 0) {
                Set32(f.impl() + PHBattleHPOffset, f.newTeamHPCap); break;
            }
        }
        if (f.bankedTeams) {
            f.syncBanks();
            f.loadView(get<uint32_t>(f.impl() + PHBattleCurrentTeamOffset));
            if (f.changeOtherBankOnRefresh) put<uint16_t>(f.bank(f.changedBank,2) + 0x510, 98);
        }
        return 0;
    }
    static uint64_t Digest(uintptr_t game) {
        Check(game == fixture->game(), "digest uses the active game object");
        uint32_t hash = 2166136261u;
        unsigned count = fixture->bankedTeams ? get<uint32_t>(fixture->impl() + PHBattleTeamCountOffset) : 1;
        for (unsigned team = 0; team < count; ++team) for (unsigned slot = 0; slot < (fixture->bankedTeams ? fixture->nativeTeamSize : 6); ++slot) {
            uintptr_t m = fixture->bankedTeams ? fixture->bank(team,slot) : fixture->member(slot);
            if (!(get<uint8_t>(m) & 1u)) continue;
            for (uintptr_t offset : {PHBattleBaseIDOffset, PHBattleCatalogIDOffset, (uintptr_t)0x510})
                hash = (hash ^ get<uint16_t>(m + offset)) * 16777619u;
        }
        return hash & 0x7fffffffu;
    }
    static uint64_t MaxCharge(uintptr_t m, uint64_t value) {
        Check(value == 0, "charge cap uses the native skill/assist ABI");
        unsigned slot = (unsigned)((m - fixture->member(0)) / PHBattleMemberStride);
        Check(slot < 6, "charge cap uses a current physical member");
        return slot < 6 ? fixture->chargeCaps[slot] : 0;
    }
    static int32_t TeamAwakening(uintptr_t game, uint64_t id, uint8_t *mask, uint64_t a, uint64_t b) {
        Fixture &f = *fixture;
        Check(game == f.game() && a == 0 && b == 0 && mask, "initial SB uses the constructor's team counter ABI");
        *mask = 0;
        if (f.invalidAwakeningCount) return 1001;
        if (f.awakeningsDisabled) return 0;
        int32_t count = 0;
        for (unsigned i = 0; i < 6; ++i) {
            uintptr_t m = f.member(i);
            if (!(get<uint8_t>(m) & 1u)) continue;
            auto countCard = [&](uint16_t card, unsigned unlocked) {
                if (!card) return;
                for (unsigned j = 0; j < std::min(unlocked,10u); ++j) {
                    if (get<uint16_t>((uintptr_t)f.catalog.data() + card * PHBattleCatalogStride + 748 + j * 2) == id) {
                        ++count; *mask |= (uint8_t)(1u << i);
                    }
                }
            };
            countCard(get<uint16_t>(m + PHBattleBaseIDOffset),get<uint8_t>(m + 0x4f3));
            countCard(get<uint16_t>(m + PHBattleAssistIDOffset),get<uint8_t>(m + 0x5a0) & 0x7fu);
            if (get<uint32_t>(m + 0x500) == id) { ++count; *mask |= (uint8_t)(1u << i); }
        }
        return count;
    }
    static int32_t TeamLatent(uintptr_t game, uint64_t id, uint8_t *mask) {
        Check(game == fixture->game() && mask, "entry latent counter keeps its native ABI");
        *mask = 0; int32_t count = 0;
        for (unsigned i = 0; i < 6; ++i) {
            uintptr_t m = fixture->member(i);
            if (!(get<uint8_t>(m) & 1u)) continue;
            uint64_t packed = get<uint64_t>(m + 0x4f4);
            for (unsigned hole = 0; hole < ((packed >> 4) & 15u) && hole < 8;) {
                unsigned latent = (unsigned)((packed >> (8 + 7 * hole)) & 0x7fu);
                if (latent == id) { ++count; *mask |= (uint8_t)(1u << i); }
                unsigned cost = PHBattleLatentCost(latent);
                hole += cost ? cost : 1;
            }
        }
        return count;
    }
    static uint64_t CurrentBadge(uintptr_t game) {
        Check(game == fixture->game(), "entry reads the badge of the active team"); return 12;
    }
    static int32_t BadgeBoost(int32_t badge) {
        Check(badge == 12, "native badge boost receives the current badge"); return fixture->badgeSkillBoost;
    }
    static int32_t MemberHasSkill(uintptr_t m) { return get<uint8_t>(m) & 1u; }
    static uint64_t TeamSize() { return fixture->nativeTeamSize; }
    static uint64_t SoloRules(uint64_t selector) { Check(selector == 0, "bank adapter reads ordinary solo rules"); return fixture->soloRule; }
    static uint64_t SharedHelper(uintptr_t d) { Check(d == (uintptr_t)fixture->dungeon.data(), "helper rule receives active dungeon"); return fixture->sharedHelperRule; }
    static uint64_t SharedLeader(uintptr_t d) { Check(d == (uintptr_t)fixture->dungeon.data(), "leader rule receives active dungeon"); return fixture->sharedLeaderRule; }
    static void SwitchTeam(uintptr_t game, uint32_t team, int temporary) {
        Fixture &f = *fixture;
        Check(game == f.game() && temporary == 0 && team < get<uint32_t>(f.impl() + PHBattleTeamCountOffset),
            "entry uses the native permanent-view loader ABI without changing turn ownership");
        if (++f.switches == f.failSwitch) throw std::runtime_error("injected team loader failure");
        f.loadView(team);
    }
    static uint64_t AdvanceMember(uintptr_t m, int amount) {
        ++fixture->memberAdvances;
        uint16_t old = (uint16_t)decode(m + PHBattleChargeOffset), cap = (uint16_t)MaxCharge(m,0);
        uint16_t next = (uint16_t)std::min<unsigned>(old + (unsigned)amount,cap);
        Set16(m + PHBattleChargeOffset,next); put<uint32_t>(m + PHBattleChargeShadowOffset,next ^ 0x68fb65ceu);
        return next != old;
    }
    static uint64_t AdvanceTeam(uintptr_t game, int slot, int animate) {
        Fixture &f = *fixture; ++f.advances;
        Check(game == f.game() && slot == -1 && animate == 0, "entry charges the native team without adding a player turn");
        if (!f.chargingDisabled) for (unsigned i = 0; i < 6; ++i) if (MemberHasSkill(f.member(i))) AdvanceMember(f.member(i),1);
        if (f.advances == f.failAdvanceTick) {
            put<uint32_t>(f.header() + PHSaveRVA + PHBattleAuditOffset,0x10000);
            put<uint32_t>(f.member(5) + PHBattleChargeShadowOffset,0);
        }
        if (f.advances == f.closeWindowAtTick) Set32(f.impl() + PHBattlePhaseOffset,2);
        return 0;
    }
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
                  "allow-all filters still reject a missing or uninitialized bank layout before native writes");
        }
        for (uint32_t multi : {1u, 4u}) {
            Fixture f; f.configureBanks(multi); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleState original = f.state(engine);
            Check(PHBattleStateGate(&original) == PHBattleGateReady && original.members[5].sourceTeam == 1 &&
                original.members[5].sourceSlot == 0, "bank adapter identifies the other team's leader as the visible helper");
            PHBattleLoadout team[6]; PHBattleMember installed[6] = {};
            for (unsigned i = 0; i < 6; ++i) {
                team[i] = Loadout(i); PHBattleState before = f.state(engine);
                Check([engine applyLoadout:&team[i] state:&before slot:i member:&installed[i] reason:nullptr] == PHBattleMutationApplied,
                    "multi 1/4 can install independent parameter sources in all six visible slots");
                auto source = f.sourceForView(i);
                Check(Fixture::get<uint16_t>(f.bank(source.first,source.second) + PHBattleBaseIDOffset) == 200+i,
                    "native refresh persists a replaced member into its owning bank");
            }
            f.loadView(1); PHBattleState other = f.state(engine);
            Check(other.storedMembers[0][1].providerToken == installed[1].providerToken &&
                other.members[5].providerToken == installed[0].providerToken,
                "provider ownership survives off-screen teams and leader/helper view changes");
            Check([engine restoreLoadout:&original.members[0] state:&other slot:5 reason:nullptr] == PHBattleMutationApplied &&
                Fixture::get<uint16_t>(f.bank(0) + PHBattleBaseIDOffset) == 100 && f.released == 0,
                "the original leader restores through the other team's helper without releasing off-screen records");
            f.loadView(0);
            for (unsigned i = 1; i < 6; ++i) {
                PHBattleState live = f.state(engine);
                Check([engine restoreLoadout:&original.members[i] state:&live slot:i reason:nullptr] == PHBattleMutationApplied,
                    "remaining banked loadouts restore after switching back to the original team");
            }
            Check(f.allocated == f.released, "banked parameter buffers release only after every owning source restores");
        }
        for (uint32_t multi : {1u, 4u}) for (unsigned scenario = 0; scenario < 4; ++scenario) {
            Fixture f; f.configureBanks(multi); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleState before = f.state(engine); PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            switch (scenario) {
                case 0: f.loadView(1); break;
                case 1: Fixture::put<uint32_t>(f.impl() + PHBattleTeamCountOffset, 9); break;
                case 2: Fixture::put<uintptr_t>(f.impl() + PHBattleTeamDescriptorOffset + PHBattleTeamPointerOffset, f.bank(0)+8); break;
                case 3: Fixture::put<uint8_t>(f.impl() + PHBattleTeamBusyOffset, 8); break;
            }
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRejected &&
                !f.allocated && !f.rebuilds, "changed views, excessive banks, unknown storage pointers and transient team switches cancel before writing");
        }
        for (unsigned rule = 0; rule < 2; ++rule) {
            Fixture f; f.sharedHelperRule = rule == 0; f.sharedLeaderRule = rule == 1; f.configureBanks(1);
            id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            unsigned slot = rule ? 0 : 5;
            Check(PHBattleStateGate(&before) == PHBattleGateReady && before.members[slot].sourceTeam == 1 &&
                before.members[slot].sourceSlot == (rule ? 0 : 5), "shared leader/helper rules select the local owner's native source");
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:slot member:&output reason:nullptr] == PHBattleMutationApplied,
                "shared native sources can be replaced without confusing their mirrored copies");
            PHBattleState after = f.state(engine);
            Check([engine restoreLoadout:&before.members[slot] state:&after slot:slot reason:nullptr] == PHBattleMutationApplied &&
                f.allocated == f.released, "shared native sources and all references restore together");
        }
        {
            Fixture f; f.configureBanks(4,8,6); id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout loadout = Loadout(5); PHBattleMember output = {};
            Check(PHBattleStateGate(&before) == PHBattleGateReady && before.teamCount == 8 && before.members[5].sourceTeam == 0 &&
                [engine applyLoadout:&loadout state:&before slot:5 member:&output reason:nullptr] == PHBattleMutationApplied,
                "six-member native banks support the bounded eight-team layout without borrowing another leader");
        }
        {
            Fixture f; f.configureBanks(1); id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {}; f.changeOtherBankOnRefresh = true;
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationUncertain,
                "an unrelated bank mutation cannot be concealed by refreshing the digest or reported as a full rollback");
        }
        for (unsigned count : {2u, 3u}) for (unsigned player = 0; player < count; ++player) {
            Fixture f; f.configureBanks(1,count,6,player); f.enter();
            id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleLoadout team[6]; PHBattleMember output[6] = {}; uint16_t boost = 77;
            for (unsigned slot = 0; slot < 6; ++slot) { team[slot] = Loadout(slot); f.awakening(200+slot,0,21); }
            PHBattleState before = f.state(engine);
            Check(before.phase == 46 && before.entryWindow && before.turnInfoValid && before.playerTeam == player &&
                before.turnTeam == 0 && PHBattleEntryGate(&before) == PHBattleGateReady,
                "real three-player entry starts in phase 46 and identifies our network seat for two or three participants");
            NSString *reason = nil;
            PHBattleMutationResult result = [engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:&reason];
            if (result != PHBattleMutationApplied) std::fprintf(stderr,"mode7 entry result %d: %s\n",result,reason.UTF8String ?: "");
            Check(result == PHBattleMutationApplied && boost == 8 && f.advances == 8 && f.switches == (player ? 2u : 0u),
                "three-player automatic entry installs our entire bank and native SB in one frame");
            PHBattleState after = f.state(engine);
            Check(after.currentTeam == before.currentTeam && after.hp == before.hp && after.turnSerial == 0 &&
                after.entryChargesValid && after.hpMatches && after.digestMatches,
                "entry restores the original view, HP and turn owner before the game resumes");
            for (unsigned bank = 0; bank < count; ++bank) {
                Check(after.teamHP[bank] == before.teamHP[bank], "entry preserves each participant's HP");
                for (unsigned slot = 0; slot < 6; ++slot) {
                    const PHBattleMember &m = after.storedMembers[bank][slot];
                    Check(m.sourceTeam == bank && m.baseID == (bank == player ? 200 + slot : 100 + 1000 * bank + slot) &&
                        m.charge == (bank == player ? 6 : 4), "only the local network seat receives new members and skill boost");
                    if (bank == player) Check(m.providerToken && m.providerToken == output[slot].providerToken,
                        "entry returns local provider ownership even when its bank is offscreen");
                }
            }
            // A preemptive delay occurs after the transaction. Loading the
            // first local turn must neither reapply the team nor recharge it.
            for (unsigned slot = 0; slot < 6; ++slot) {
                Fixture::encode(f.bank(player,slot) + PHBattleChargeOffset,2);
                Fixture::put<uint32_t>(f.bank(player,slot) + PHBattleChargeShadowOffset,2 ^ 0x68fb65ceu);
            }
            Fixture::put<uint32_t>(f.impl() + PHBattleTurnSerialOffset, player);
            Fixture::Set32(f.impl() + PHBattlePhaseOffset,2); f.loadView(player);
            PHBattleState firstTurn = f.state(engine); boost = 77;
            Check(PHBattleStateGate(&firstTurn) == PHBattleGateReady &&
                [engine applyEntryLoadouts:team mask:63 state:&firstTurn members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected &&
                boost == 77 && f.advances == 8, "the first local turn cannot reset preemptive delays or repeat initial SB");
            for (unsigned slot = 0; slot < 6; ++slot) {
                PHBattleState current = f.state(engine);
                Check(current.members[slot].charge == 2 &&
                    [engine restoreLoadout:&before.storedMembers[player][slot] state:&current slot:slot reason:nullptr] == PHBattleMutationApplied,
                    "our original six members restore on our own turn with delayed charge intact");
            }
            Check(f.allocated == f.released, "local offscreen entry leases release after all six sources restore");
        }
        {
            Fixture f; f.configureBanks(1,3,6,2); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            PHBattleState remote = f.state(engine);
            Check(PHBattleStateGate(&remote) == PHBattleGateTurn &&
                [engine applyLoadout:&loadout state:&remote slot:0 member:&output reason:nullptr] == PHBattleMutationRejected,
                "phase 2 during a remote player's turn cannot replace the visible remote team");
            f.loadView(2); PHBattleState preview = f.state(engine);
            Check(PHBattleStateGate(&preview) == PHBattleGateTurn &&
                [engine changeState:&preview slot:0 form:200 catalog:200 presentation:nullptr reason:nullptr] == PHBattleMutationRejected && !f.rebuilds,
                "a local-team preview does not grant control while another player owns the turn");
            Fixture::put<uint32_t>(f.impl()+PHBattleTurnSerialOffset,2); PHBattleState local = f.state(engine);
            Check(PHBattleStateGate(&local) == PHBattleGateReady, "the modulo turn counter enables the matching local bank");
            Fixture::put<uint32_t>(f.impl()+PHBattleTurnSerialOffset,5);
            Check([engine applyLoadout:&loadout state:&local slot:0 member:&output reason:nullptr] == PHBattleMutationRejected && !f.rebuilds,
                "even a later local turn rejects the previous turn's native transaction snapshot");
        }
        for (unsigned scenario = 0; scenario < 11; ++scenario) {
            Fixture f; f.configureBanks(1,2,6,1); f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleState before = f.state(engine); PHBattleLoadout team[6];
            for (unsigned slot = 0; slot < 6; ++slot) team[slot] = Loadout(slot);
            switch (scenario) {
                case 0: Fixture::put<uint32_t>(f.impl()+PHBattlePlayerTeamOffset,2); break;
                case 1: Fixture::put<uint32_t>(f.impl()+PHBattleTurnSerialOffset,1); break;
                case 2: Fixture::put<uint32_t>(f.impl()+PHBattleTeamCountOffset,1); break;
                case 3: Fixture::put<uint8_t>(f.impl()+PHBattleTeamDescriptorOffset,9); break;
                case 4: Fixture::put<uint32_t>(f.bank(1)+PHBattleTeamHPOffset+12,123); break;
                case 5: Fixture::put<uint32_t>(f.header()+PHSaveRVA+PHBattleResumeFlagsOffset,0x80000000); break;
                case 6: Fixture::Set32(f.impl()+PHBattleWaveOffset,1); break;
                case 7: Fixture::Set32(f.impl()+PHBattlePhaseOffset,2); break;
                case 8: f.image[PHBattleSwitchTeamRVA] ^= 1; break;
                case 9: Fixture::put<uint32_t>(f.bank(0)+PHBattleChargeShadowOffset,0); break;
                case 10: Fixture::put<uint32_t>(f.impl()+PHBattleTurnSerialOffset,UINT32_MAX); break;
            }
            PHBattleMember output[6] = {}; uint16_t boost = 77;
            Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected &&
                !f.switches && !f.rebuilds && !f.allocated && boost == 77,
                "changed ownership, incomplete banks, resumes, later waves/turns and invalid code/charge/HP reject before selecting a team");
        }
        for (unsigned scenario = 0; scenario < 8; ++scenario) {
            Fixture f; f.configureBanks(1,3,6,1); f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleState before = f.state(engine); PHBattleLoadout team[6];
            for (unsigned slot = 0; slot < 6; ++slot) team[slot] = Loadout(slot);
            switch (scenario) {
                case 0: f.failRebuild = 3; f.newTeamHPCap = 60; break;
                case 1: f.failAdvanceTick = 1; f.newTeamHPCap = 60; break;
                case 2: team[5].level = 121; break;
                case 3: f.failSwitch = 1; break;
                case 4: f.failSwitch = 2; break;
                case 5: f.changeOtherBankOnRefresh = true; f.changedBank = 0; break;
                case 6: f.closeWindowAtTick = 1; break;
                case 7: f.invalidAwakeningCount = true; break;
            }
            PHBattleMutationResult wanted = scenario == 2 || scenario == 3 ? PHBattleMutationRejected :
                scenario >= 4 && scenario <= 6 ? PHBattleMutationUncertain : PHBattleMutationRolledBack;
            PHBattleMember output[6] = {}; uint16_t boost = 77; NSString *reason = nil;
            PHBattleMutationResult result = [engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:&reason];
            if (result != wanted) std::fprintf(stderr,"mode7 rollback scenario %u got %d wanted %d: %s\n",scenario,result,wanted,reason.UTF8String ?: "");
            Check(result == wanted && boost == 77, "mode-7 entry distinguishes complete recovery from an unconfirmed native transaction");
            if (wanted != PHBattleMutationUncertain) {
                PHBattleState after = f.state(engine);
                Check(after.currentTeam == 0 && after.hp == before.hp && SameEntryBanks(before,after) &&
                    SameEntryMembers(before,after,YES) && after.entryChargesValid && f.allocated == f.released,
                    "recoverable mode-7 failure restores all banks, original HP/charges and the original visible team");
                Check(after.auditFlags == (scenario == 1 ? 0x10000u : 0u), "multiplayer recovery preserves native audit flags");
            } else {
                unsigned writes = f.rebuilds, switches = f.switches;
                Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected &&
                    f.rebuilds == writes && f.switches == switches, "uncertain multiplayer entry latches off further native mutations");
            }
        }
        for (uint32_t multi : {1u, 4u}) {
            Fixture f; f.configureBanks(multi); f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleLoadout team[6];
            for (unsigned i = 0; i < 6; ++i) { team[i] = Loadout(i); f.awakening(200+i,0,21); }
            PHBattleState before = f.state(engine); PHBattleMember output[6] = {}; uint16_t boost = 0;
            Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationApplied &&
                boost == 8, "banked entry uses the new visible team's native boost before preemptive processing");
            PHBattleState after = f.state(engine);
            for (unsigned i = 0; i < 6; ++i) Check(after.members[i].charge == 6 && output[i].providerToken,
                "banked initial charge and provider ownership remain valid after native refresh");
            Check(after.storedMembers[1][0].charge == 6 && after.storedMembers[1][1].charge == 4,
                "entry publishes the helper's charge while preserving off-screen members' existing charge");
        }
        {
            Fixture f; f.configureBanks(1); f.enter(); f.failAdvanceTick = 1;
            id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout team[6]; for (unsigned i = 0; i < 6; ++i) team[i] = Loadout(i);
            PHBattleMember output[6] = {}; uint16_t boost = 77;
            Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRolledBack &&
                boost == 77, "failed banked entry restores the current team, helper bank and original initial charge");
            PHBattleState after = f.state(engine);
            Check(after.entryChargesValid && after.storedMembers[1][0].charge == 4 && f.allocated == f.released &&
                Fixture::get<uint32_t>(f.header() + PHSaveRVA + PHBattleAuditOffset) == 0x10000,
                "banked entry recovery verifies all source records without clearing native audit flags");
        }
        {
            Fixture f; id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            Fixture::put<uint16_t>(f.header() + PHSaveRVA + PHBattleSourceModeOffset, 3);
            PHBattleLoadout loadout = Loadout(); PHBattleMember output = {};
            Check([engine applyLoadout:&loadout state:&before slot:0 member:&output reason:nullptr] == PHBattleMutationRejected && !f.allocated,
                  "a selector changed after the snapshot cannot enter a native transaction");
        }
        {
            Fixture f; f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleLoadout team[6];
            const uint16_t caps[6] = {6,12,7,20,25,4}; std::memcpy(f.chargeCaps,caps,sizeof(caps));
            for (unsigned i = 0; i < 6; ++i) { team[i] = Loadout(i); f.awakening(200+i,0,21); }
            f.awakening(200,1,56); f.awakening(200,2,145); f.awakening(200,3,105); f.awakening(300,0,56);
            Fixture::put<uint16_t>((uintptr_t)f.catalog.data() + 200 * PHBattleCatalogStride + 788,56);
            team[0].latents[0] = 47; team[0].latentCount = 1;
            PHBattleState before = f.state(engine); PHBattleMember output[6] = {}; uint16_t boost = 0;
            Check(before.entryWindow && before.entryChargesValid && PHBattleEntryGate(&before) == PHBattleGateReady &&
                  PHBattleStateGate(&before) == PHBattleGatePhase, "fresh entry is eligible only through its separate transaction gate");
            Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationApplied,
                  "all six profiles and initial SB finish synchronously in the fresh entry frame");
            Check(boost == 18 && f.rebuilds == 6 && f.advances == 18,
                  "initial boost uses new body/weapon/sync awakenings, SB minus, latent and native badge counts");
            const uint16_t charges[6] = {6,12,7,18,18,4};
            PHBattleState after = f.state(engine);
            for (unsigned i = 0; i < 6; ++i) Check(output[i].baseID == 200+i && output[i].providerToken &&
                output[i].assistID == 300+i && output[i].charge == charges[i] &&
                Fixture::get<uint32_t>(f.member(i)+PHBattleChargeShadowOffset) == (charges[i] ^ 0x68fb65ceu),
                "each new member starts with freshly computed, capped charge and a matching mirror");
            boost = 77;
            Check([engine applyEntryLoadouts:team mask:63 state:&after members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected &&
                  boost == 77 && f.advances == 18, "the same entry identity can never charge twice");
            Fixture::Set32(f.impl() + PHBattlePhaseOffset,2);
            for (unsigned i = 0; i < 6; ++i) {
                after = f.state(engine);
                Check([engine restoreLoadout:&before.members[i] state:&after slot:i reason:nullptr] == PHBattleMutationApplied,
                      "automatically installed profiles keep normal individual restoration ownership");
            }
            Check(f.allocated == f.released, "all auto-installed native records release after normal restore");
        }
        for (unsigned scenario = 0; scenario < 6; ++scenario) {
            Fixture f; f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleLoadout team[6] = {}; team[0] = Loadout(); f.awakening(200,0,21);
            uint16_t expectedBoost = 3, expectedCharge = 3;
            if (scenario == 1) {
                f.awakening(200,0,105); f.badgeSkillBoost = 0; expectedBoost = expectedCharge = 0;
            } else if (scenario == 2) {
                f.awakeningsDisabled = true; team[0].latents[0] = 47; team[0].latentCount = 1;
                expectedBoost = expectedCharge = 5;
            } else if (scenario == 3) { f.chargingDisabled = true; expectedCharge = 0; }
            else if (scenario == 4) { Fixture::put<uint8_t>((uintptr_t)f.dungeon.data()+33,4); expectedBoost = expectedCharge = 2; }
            else if (scenario == 5) { Fixture::put<uint16_t>(f.impl()+PHBattleAwakeningStateOffset,1); expectedBoost = expectedCharge = 2; }
            PHBattleState before = f.state(engine); PHBattleMember output[6] = {}; uint16_t boost = 77;
            Check([engine applyEntryLoadouts:team mask:1 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationApplied &&
                  boost == expectedBoost, "partial/zero/disabled-awakening teams use the native initial boost rules");
            PHBattleState after = f.state(engine);
            for (unsigned i = 0; i < 6; ++i) Check(after.members[i].charge == expectedCharge &&
                after.members[i].baseID == (i ? 100+i : 200) && after.members[i].providerToken == (i ? 0 : output[0].providerToken),
                "partial replacement recalculates all six charges while preserving unselected profiles and native disabled-skill rules");
        }
        {
            Fixture f; f.enter(); f.chargingDisabled = true;
            Fixture::put<uint32_t>(f.impl()+PHBattleSpecialEntryOffset,525190);
            id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout team[6] = {}; team[0] = Loadout(); PHBattleMember output[6] = {}; uint16_t boost = 0;
            Check([engine applyEntryLoadouts:team mask:1 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationApplied &&
                  boost == 1000 && f.advances == 0 && f.memberAdvances == 6,
                  "the special 1000-charge entry follows the constructor's direct per-member path");
            Check(output[5].charge == 6, "special entry still respects native member charge caps");
        }
        for (unsigned scenario = 0; scenario < 8; ++scenario) {
            Fixture f; f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine();
            switch (scenario) {
                case 0: Fixture::put<uint32_t>(f.header()+PHSaveRVA+PHBattleResumeFlagsOffset,0x80000000); break;
                case 1: Fixture::Set32(f.impl()+PHBattleWaveOffset,1); break;
                case 2: Fixture::Set32(f.impl()+PHBattleWaveCopyOffset,1); break;
                case 3: Fixture::Set32(f.impl()+PHBattleWaveBackupOffset,1); break;
                case 4: Fixture::put<uint8_t>(f.impl()+PHBattleIntroOffset,0); break;
                case 5: Fixture::Set32(f.impl()+PHBattlePhaseOffset,2); break;
                case 6: Fixture::put<uint32_t>(f.member(5)+PHBattleChargeShadowOffset,0); break;
                case 7: Fixture::put<uint32_t>(f.header()+PHSaveRVA+PHBattleAuditOffset,0x10000); break;
            }
            PHBattleState before = f.state(engine); PHBattleLoadout team[6] = {}; team[0] = Loadout();
            PHBattleMember output[6] = {}; uint16_t boost = 77;
            Check([engine applyEntryLoadouts:team mask:1 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected &&
                  !f.allocated && !f.rebuilds && !f.advances && boost == 77,
                  "resumed/later-wave/non-intro/corrupt-charge entries reject before any profile or charge writes");
        }
        {
            Fixture f; f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout team[6]; for (unsigned i = 0; i < 6; ++i) team[i] = Loadout(i);
            team[5].bodyNumber = 65435; PHBattleMember output[6] = {}; uint16_t boost = 77;
            Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected &&
                  !f.allocated && !f.rebuilds, "an invalid final profile rejects the entire entry team before its first write");
        }
        for (unsigned scenario = 0; scenario < 4; ++scenario) {
            Fixture f; f.enter(); f.newTeamHPCap = 50; id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleLoadout team[6]; for (unsigned i = 0; i < 6; ++i) team[i] = Loadout(i);
            if (scenario == 0) f.failRebuild = 4;
            else if (scenario == 1) {
                f.failWriteAddress = f.member(4)+PHBattleChargeShadowOffset;
                f.failWriteLength = 4; f.failWriteOccurrence = 2; f.partialWrite = 1;
            } else if (scenario == 2) f.failAdvanceTick = 2;
            else f.invalidAwakeningCount = true;
            PHBattleState before = f.state(engine); PHBattleMember output[6] = {}; uint16_t boost = 77;
            Check([engine applyEntryLoadouts:team mask:63 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRolledBack &&
                  boost == 77, "later-member, charge/mirror, audit and native-count failures roll back the complete entry transaction");
            PHBattleState restored = f.state(engine);
            for (unsigned i = 0; i < 6; ++i) Check(restored.members[i].baseID == 100+i && restored.members[i].catalogID == 100+i &&
                restored.members[i].scriptSlot == -1 && !restored.members[i].providerToken && restored.members[i].charge == 4 &&
                restored.members[i].level == 99 && restored.members[i].assistID == 0,
                "recoverable entry failure restores every original profile and charge, including previously completed members");
            Check(restored.entryChargesValid && restored.hp == 100 && restored.hpMatches && restored.digestMatches && f.allocated == f.released,
                  "entry rollback restores initial HP, protected charge mirrors and all owned native parameter records");
            Check(restored.auditFlags == (scenario == 2 ? 0x10000u : 0u), "entry rollback never clears a native audit flag");
        }
        {
            Fixture f; f.enter(); f.closeWindowAtTick = 1; id<PHBattleEngine> engine = PHCreateBattleEngine();
            PHBattleLoadout team[6] = {}; team[0] = Loadout(); PHBattleState before = f.state(engine);
            PHBattleMember output[6] = {}; uint16_t boost = 0;
            Check([engine applyEntryLoadouts:team mask:1 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationUncertain,
                  "a lost entry window refuses an unprovable rollback and faults the engine");
            before = f.state(engine); unsigned rebuilds = f.rebuilds;
            Check([engine applyLoadout:&team[0] state:&before slot:1 member:&output[1] reason:nullptr] == PHBattleMutationRejected && f.rebuilds == rebuilds,
                  "unconfirmed entry rollback prevents all subsequent native writes");
        }
        {
            Fixture f; f.enter(); id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            const PHBattleTextGuard &guard = PHBattleTextGuards[PHBattleTextGuardCount-1];
            Fixture::put<uint8_t>(f.header()+guard.rva,(uint8_t)(guard.bytes[0]^1u));
            PHBattleLoadout team[6] = {}; team[0] = Loadout(); PHBattleMember output[6] = {}; uint16_t boost = 0;
            Check([engine applyEntryLoadouts:team mask:1 state:&before members:output initialBoost:&boost reason:nullptr] == PHBattleMutationRejected && !f.allocated,
                  "new entry charge helpers are fingerprinted again before mutation");
        }
        std::printf("Native battle provider tests: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
