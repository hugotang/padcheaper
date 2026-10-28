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
    unsigned nativeCalls = 0, writeCalls = 0;
    unsigned becomeMultiplayerAtRebuild = 0, callsAtMultiplayer = 0, writesAtMultiplayer = 0;
    unsigned switches = 0, failSwitch = 0, changedBank = 1;
    unsigned failRebuild = 0;
    bool changeOtherViewOnFailedRebuild = false;
    bool auditOnRefresh = false;
    unsigned advances = 0, memberAdvances = 0, failAdvanceTick = 0, closeWindowAtTick = 0;
    uint16_t chargeCaps[6] = {6,6,6,6,6,6};
    bool awakeningsDisabled = false, chargingDisabled = false, invalidAwakeningCount = false;
    uint32_t newTeamHPCap = 0;
    uint32_t newTeamMaximumHP = 0, entryRuleMaximumHP = 0, intermediateHPCap = 0, displayedHP = 100;
    unsigned hpInitializations = 0, failEntryHP = 0;
    bool invalidHPMaximum = false;
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
            TeamSize, SoloRules, SharedHelper, SharedLeader, SwitchTeam, MaximumHP, AdjustHP};
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
            displayedHP = decode(impl() + PHBattleHPOffset, 0x766cfc6du);
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
    void receivePeerTeam(uint32_t serial, unsigned view, uint8_t mask = 63) {
        // Model 0x10001771C's per-bank snapshot restore: overwrite encoded
        // script/form/charge fields, then rebuild from the native source.
        // Copy-only loadView never reproduced this guest failure.
        unsigned player = get<uint32_t>(impl() + PHBattlePlayerTeamOffset);
        put<uint32_t>(impl() + PHBattleTurnSerialOffset, serial);
        Set32(impl() + PHBattlePhaseOffset, 2);
        for (unsigned slot = 0; slot < 6; ++slot) if (mask & (1u << slot)) {
            uintptr_t m = bank(player,slot);
            SetByte(m + PHBattleScriptSlotOffset, 255);
            Set16(m + PHBattleFormOffset, slot == 2 ? 450 : 0);
            Set16(m + PHBattleChargeOffset, slot);
            put<uint32_t>(m + PHBattleChargeShadowOffset, slot ^ 0x68fb65ceu);
            Rebuild(game(), m, UINT32_MAX);
        }
        loadView(view); shadows();
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
    static uint64_t Set16(uintptr_t address, uint64_t value) { ++fixture->nativeCalls; encode(address, (uint16_t)value); return value; }
    static uint64_t Set32(uintptr_t address, uint64_t value) { ++fixture->nativeCalls; encode(address, (uint32_t)value, 0x766cfc6du); return value; }
    static uint64_t SetByte(uintptr_t address, uint64_t value) { ++fixture->nativeCalls; encode(address, (uint8_t)value); return value; }
    static uintptr_t Rebuild(uintptr_t game, uintptr_t m, uint64_t argument) { ++fixture->nativeCalls;
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
        if (f.becomeMultiplayerAtRebuild == f.rebuilds) {
            put<uint32_t>(f.header() + PHBattleMultiplayerRVA, 1);
            f.callsAtMultiplayer = f.nativeCalls; f.writesAtMultiplayer = f.writeCalls;
        }
        if (f.failRebuild == f.rebuilds) {
            if (f.changeOtherViewOnFailedRebuild) put<uint16_t>(f.member(4) + 0x510,98);
            return 0;
        }
        return m;
    }
    static uint64_t Refresh(uintptr_t game, uint64_t value) { ++fixture->nativeCalls;
        Fixture &f = *fixture; ++f.refreshes;
        Check(game == f.game() && value == 0, "refresh uses the normal team ABI");
        if (f.auditOnRefresh) put<uint32_t>(f.header() + PHSaveRVA + PHBattleAuditOffset, 0x10000);
        if (f.newTeamHPCap) {
            for (unsigned i = 0; i < 6; ++i) if ((int8_t)decode(f.member(i) + PHBattleScriptSlotOffset) > 0) {
                Set32(f.impl() + PHBattleHPOffset,
                    std::min(f.newTeamHPCap,decode(f.impl() + PHBattleHPOffset,0x766cfc6du))); break;
            }
        }
        if (f.intermediateHPCap) {
            unsigned bound = 0;
            for (unsigned slot = 0; slot < 6; ++slot) bound += (int8_t)decode(f.member(slot) + PHBattleScriptSlotOffset) > 0;
            if (bound && bound < 6) Set32(f.impl() + PHBattleHPOffset,
                std::min(f.intermediateHPCap,decode(f.impl() + PHBattleHPOffset,0x766cfc6du)));
        }
        if (f.bankedTeams) {
            f.syncBanks();
            f.loadView(get<uint32_t>(f.impl() + PHBattleCurrentTeamOffset));
            if (f.changeOtherBankOnRefresh) put<uint16_t>(f.bank(f.changedBank,2) + 0x510, 98);
        }
        return 0;
    }
    static uint64_t MaximumHP(uintptr_t controller) { ++fixture->nativeCalls;
        Fixture &f = *fixture;
        Check(controller == f.impl() + PHBattleHPControllerOffset, "maximum HP uses the native HP controller ABI");
        bool replaced = false;
        for (unsigned slot = 0; slot < 6; ++slot)
            replaced |= (int8_t)decode(f.member(slot) + PHBattleScriptSlotOffset) > 0;
        if (replaced && f.invalidHPMaximum) return UINT32_MAX;
        // Native maximum can differ from raw team HP because of dungeon
        // rules. The transaction must use this result, not a local HP sum.
        if (f.entryRuleMaximumHP) return f.entryRuleMaximumHP;
        if (replaced && f.newTeamHPCap) return f.newTeamHPCap;
        if (replaced && f.newTeamMaximumHP) return f.newTeamMaximumHP;
        return 100 + (f.threePlayer() ? 50 * get<uint32_t>(f.impl() + PHBattleCurrentTeamOffset) : 0);
    }
    static uint64_t AdjustHP(uintptr_t controller, int32_t amount, uint8_t suppressAnimation) { ++fixture->nativeCalls;
        Fixture &f = *fixture;
        Check(controller == f.impl() + PHBattleHPControllerOffset && suppressAnimation == 1,
            "entry HP uses the native controller and suppresses the refill animation");
        uint32_t phase = decode(f.impl() + PHBattlePhaseOffset,0x766cfc6du);
        bool entry = amount > 0 && (phase == 0 || phase == 46);
        if (entry) {
            ++f.hpInitializations;
            if (f.failEntryHP == 1) return 0;
        }
        int64_t next = decode(f.impl() + PHBattleHPOffset, 0x766cfc6du) + (int64_t)amount;
        uint32_t hp = (uint32_t)std::min<int64_t>(MaximumHP(controller), std::max<int64_t>(0,next));
        Set32(f.impl() + PHBattleHPOffset,hp);
        put<uint32_t>(f.impl() + PHBattleHPShadowOffset,hp ^ 0x8085a6beu);
        f.displayedHP = hp;
        if (entry) switch (f.failEntryHP) {
            case 2: put<uint32_t>(f.header() + PHSaveRVA + PHBattleAuditOffset,0x10000); break;
            case 3: encode(f.bank(f.changedBank) + PHBattleTeamHPOffset,17,0x766cfc6du); break;
            case 4: put<uint32_t>(f.impl() + PHBattleHPShadowOffset,0); break;
            case 5: Set32(f.impl() + PHBattlePhaseOffset,2); break;
        }
        return 0;
    }
    static uint64_t Digest(uintptr_t game) { ++fixture->nativeCalls;
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
    static uint64_t MaxCharge(uintptr_t m, uint64_t value) { ++fixture->nativeCalls;
        Check(value == 0, "charge cap uses the native skill/assist ABI");
        unsigned slot = (unsigned)((m - fixture->member(0)) / PHBattleMemberStride);
        Check(slot < 6, "charge cap uses a current physical member");
        return slot < 6 ? fixture->chargeCaps[slot] : 0;
    }
    static int32_t TeamAwakening(uintptr_t game, uint64_t id, uint8_t *mask, uint64_t a, uint64_t b) { ++fixture->nativeCalls;
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
    static int32_t TeamLatent(uintptr_t game, uint64_t id, uint8_t *mask) { ++fixture->nativeCalls;
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
    static uint64_t CurrentBadge(uintptr_t game) { ++fixture->nativeCalls;
        Check(game == fixture->game(), "entry reads the badge of the active team"); return 12;
    }
    static int32_t BadgeBoost(int32_t badge) { ++fixture->nativeCalls;
        Check(badge == 12, "native badge boost receives the current badge"); return fixture->badgeSkillBoost;
    }
    static int32_t MemberHasSkill(uintptr_t m) { ++fixture->nativeCalls; return get<uint8_t>(m) & 1u; }
    static uint64_t TeamSize() { ++fixture->nativeCalls; return fixture->nativeTeamSize; }
    static uint64_t SoloRules(uint64_t selector) { ++fixture->nativeCalls; Check(selector == 0, "bank adapter reads ordinary solo rules"); return fixture->soloRule; }
    static uint64_t SharedHelper(uintptr_t d) { ++fixture->nativeCalls; Check(d == (uintptr_t)fixture->dungeon.data(), "helper rule receives active dungeon"); return fixture->sharedHelperRule; }
    static uint64_t SharedLeader(uintptr_t d) { ++fixture->nativeCalls; Check(d == (uintptr_t)fixture->dungeon.data(), "leader rule receives active dungeon"); return fixture->sharedLeaderRule; }
    static void SwitchTeam(uintptr_t game, uint32_t team, int temporary) { ++fixture->nativeCalls;
        Fixture &f = *fixture;
        Check(game == f.game() && temporary == 0 && team < get<uint32_t>(f.impl() + PHBattleTeamCountOffset),
            "entry uses the native permanent-view loader ABI without changing turn ownership");
        if (++f.switches == f.failSwitch) throw std::runtime_error("injected team loader failure");
        f.loadView(team);
    }
    static uint64_t AdvanceMember(uintptr_t m, int amount) { ++fixture->nativeCalls;
        ++fixture->memberAdvances;
        uint16_t old = (uint16_t)decode(m + PHBattleChargeOffset), cap = (uint16_t)MaxCharge(m,0);
        uint16_t next = (uint16_t)std::min<unsigned>(old + (unsigned)amount,cap);
        Set16(m + PHBattleChargeOffset,next); put<uint32_t>(m + PHBattleChargeShadowOffset,next ^ 0x68fb65ceu);
        return next != old;
    }
    static uint64_t AdvanceTeam(uintptr_t game, int slot, int animate) { ++fixture->nativeCalls;
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
    static float Radius() { ++fixture->nativeCalls; return 1; }
    static uintptr_t Allocate(uint64_t bytes, uint64_t category, uint64_t tag) { ++fixture->nativeCalls;
        Check(bytes == 16 && category == 4 && tag == 44, "empty program uses the native allocator category/tag");
        uintptr_t memory = (uintptr_t)std::calloc(1, (size_t)bytes);
        fixture->allocations[memory] = (size_t)bytes; ++fixture->allocated;
        return memory;
    }
    static void Release(uintptr_t memory, int category, int tag) { ++fixture->nativeCalls;
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
    if (fixture) ++fixture->writeCalls;
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

#include "battle_multiplayer_tests.inc"

int main() {
    @autoreleasepool {
        TestMultiplayerDisabled();
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
        for (unsigned rule = 0; rule < 4; ++rule) {
            Fixture f;
            f.enter(); f.newTeamMaximumHP = 900;
            if (rule == 1) f.newTeamHPCap = 60;
            if (rule == 2) f.entryRuleMaximumHP = 321;
            if (rule == 3) f.entryRuleMaximumHP = 75;
            uint32_t expectedHP = rule == 0 ? 900 : rule == 1 ? 60 : rule == 2 ? 321 : 75;
            id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout team[6]; for (unsigned slot = 0; slot < 6; ++slot) team[slot] = Loadout(slot);
            PHBattleMember output[6] = {}; uint16_t boost = 0;
            Check([engine applyEntryLoadouts:team mask:(rule & 1 ? 1 : 63) state:&before members:output
                initialBoost:&boost reason:nullptr] == PHBattleMutationApplied,
                "new entry initializes current HP for complete and partial teams using the native effective maximum");
            PHBattleState after = f.state(engine);
            Check(f.hpInitializations == 1 && after.hpMatches && after.entryChargesValid && after.digestMatches,
                "one HP initialization follows the complete profile and SB batch with valid mirrors");
            Check(after.hp == expectedHP && f.displayedHP == expectedHP,
                "solo entry updates gameplay HP and its display for larger, smaller and rule-limited teams");
        }
        for (unsigned failure : {0u, 1u, 2u, 4u, 5u}) {
            Fixture f; f.enter(); f.newTeamMaximumHP = 900;
            f.failEntryHP = failure; f.invalidHPMaximum = failure == 0;
            id<PHBattleEngine> engine = PHCreateBattleEngine(); PHBattleState before = f.state(engine);
            PHBattleLoadout team[6]; for (unsigned slot = 0; slot < 6; ++slot) team[slot] = Loadout(slot);
            PHBattleMember output[6] = {}; uint16_t boost = 77;
            PHBattleMutationResult result = [engine applyEntryLoadouts:team mask:63 state:&before members:output
                initialBoost:&boost reason:nullptr];
            Check(result == (failure < 3 ? PHBattleMutationRolledBack : PHBattleMutationUncertain) && boost == 77,
                "invalid maximum, failed HP update, new audit flags, corrupted HP and a closed entry window never report success");
            PHBattleState after = f.state(engine);
            if (result == PHBattleMutationRolledBack) {
                Check(after.hp == before.hp && f.displayedHP == before.hp && after.hpMatches && after.entryChargesValid &&
                    SameEntryMembers(before,after,YES) && f.allocated == f.released,
                    "a recoverable HP initialization failure restores all six profiles, charges, HP and display");
                Check(after.auditFlags == (failure == 2 ? 0x10000u : 0), "HP rollback preserves native audit evidence");
            } else {
                unsigned rebuilds = f.rebuilds;
                Check([engine applyLoadout:team state:&after slot:0 member:output reason:nullptr] == PHBattleMutationRejected &&
                    f.rebuilds == rebuilds, "unprovable HP rollback stops further native writes");
            }
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
