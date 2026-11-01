// Optional post-resolver checkpoints. No game function is invoked and no
// checkpoint is fed back into selector, input, skill or recovery decisions.
namespace EH = PHEnemyDamage;
struct EnemyDamageExtra {
    uint8_t life[76]{}, thresholds[88]{};
    uint32_t phaseWords[4]{}, separateWords[4]{}, overrideWords[12]{};
    uint16_t overrideMask = 0, overrideScale = 0, overrideEnabled = 0;
    EH::Enemy state{};
    bool read = false, overrideKnown = false;
};
struct EnemyDamageCapture {
    EH::Context context{};
    EnemyDamageExtra enemies[EA::MaxEnemies]{};
    uintptr_t dungeon = 0, callback = 0; // Private comparison identities only.
    uint16_t dungeonFlags = 0;
    unsigned activeByteSum = 0, readBytes = 0, readCalls = 0;
    bool activeByteSumKnown = false, readBudgetExceeded = false;
};
static_assert(std::is_trivially_copyable<EnemyDamageCapture>::value, "damage capture is bounded plain data");
static_assert(EH::MaxEnemies == EA::MaxEnemies, "native active-byte aggregate uses the bounded enemy slots");
bool CheckEnemyDamageCode(uintptr_t header) {
    for (const auto &guard : PHOrbEnemyDamageTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}
bool EnemyDamageRead(EnemyDamageCapture &capture, uintptr_t address, void *out, size_t bytes) {
    if (capture.readBytes > EH::MaxReadBytes || bytes > EH::MaxReadBytes - capture.readBytes || capture.readCalls >= EH::MaxReads) {
        capture.readBudgetExceeded = true; return false;
    }
    capture.readBytes += unsigned(bytes); ++capture.readCalls;
    return address <= UINTPTR_MAX - bytes && PHMemoryHasProtection(address, bytes, VM_PROT_READ, VM_PROT_EXECUTE) &&
        PHReadMemory(address, out, bytes);
}
bool EnemyDamageByte(const uint32_t *words, uint32_t key1, uint32_t key2, uint8_t &out) {
    uint32_t value = 0;
    if (!PHBattleDecodeProtected(words, key1, key2, 0x6a875a8bu, &value)) return false;
    // The native getter sign-extends its low byte; accept both canonical stores.
    const uint8_t low = uint8_t(value);
    const int32_t signedValue = low < 128 ? int32_t(low) : int32_t(low) - 256;
    if (value != uint32_t(low) && value != uint32_t(signedValue)) return false;
    out = low; return true;
}
bool CollectEnemyDamage(const PHOrbSnapshot &state, const EnemyActionCapture &actions,
    uint32_t key1, uint32_t key2, EnemyDamageCapture &out) {
    std::memset(&out, 0, sizeof(out));
    if (actions.enemyCount > EA::MaxEnemies) return false;
    const uintptr_t impl = state.identity.impl;
    uintptr_t active = 0;
    auto read = [&](uintptr_t p, void *bytes, size_t n) { return EnemyDamageRead(out, p, bytes, n); };
    auto &c = out.context;
    if (!read(state.identity.header + PHBattleImplRVA, &active, sizeof(active)) || active != impl ||
        !read(impl + PHOrbEnemyDamageImmediateOffset, &c.immediate, sizeof(c.immediate)) ||
        !read(impl + PHOrbEnemyDamageSpecialOffset, &c.specialMarker, sizeof(c.specialMarker)) ||
        !read(impl + PHOrbEnemyDamageCallbackOffset, &out.callback, sizeof(out.callback)) ||
        !read(impl + PHBattleDungeonPointerOffset, &out.dungeon, sizeof(out.dungeon))) return false;
    if (out.dungeon && (out.dungeon > UINTPTR_MAX - 26 || !read(out.dungeon + 24, &out.dungeonFlags, sizeof(out.dungeonFlags)))) return false;
    c.actionsEnabled = out.dungeon && (out.dungeonFlags & 0x80u);
    c.callbackPresent = out.callback != 0;
    out.activeByteSumKnown = true;
    for (unsigned slot = 0; slot < actions.enemyCount; ++slot) {
        const auto &enemy = actions.enemies[slot]; auto &extra = out.enemies[slot];
        out.activeByteSumKnown &= enemy.memoryKnown;
        if (enemy.memoryKnown) out.activeByteSum += enemy.active[1]; // Includes parts and raw values other than 1.
        if (!enemy.memoryKnown || (enemy.flags & 0x100u)) continue;
        const uintptr_t p = impl + PHOrbEnemyOffset + slot * PHOrbEnemyStride;
        if (!read(p + 60, extra.life, sizeof(extra.life)) ||
            !read(p + 2464, extra.phaseWords, sizeof(extra.phaseWords)) ||
            !read(p + 2704, extra.separateWords, sizeof(extra.separateWords)) ||
            !read(p + 2892, extra.thresholds, sizeof(extra.thresholds)) ||
            !read(p + 2288, extra.overrideWords, sizeof(extra.overrideWords))) return false;
        extra.read = true;
        auto &e = extra.state;
        uint64_t hp = 0; uint8_t separate = 0;
        e.known = enemy.stateKnown && EnemyActionWide(extra.life, 4, key1, key2, hp) && hp == enemy.hp &&
            EnemyDamageByte(extra.phaseWords, key1, key2, e.phase) && EnemyDamageByte(extra.separateWords, key1, key2, separate);
        e.hp = int64_t(enemy.hp); e.maximumHP = int64_t(enemy.maximumHP);
        e.snapshotHP = Load<int64_t>(extra.life + 60); e.secondarySnapshotHP = Load<int64_t>(extra.life + 68);
        e.survival = extra.life[0]; e.active = enemy.active[1]; e.flags = enemy.flags;
        e.separateHP = int8_t(separate < 128 ? int(separate) : int(separate) - 256);
        e.countdown = enemy.countdown; e.selected = enemy.selected; e.special = enemy.special;
        e.normalPercent = Load<uint16_t>(extra.thresholds);
        e.superTriggerPercent = Load<uint16_t>(extra.thresholds + 2);
        e.superFloorPercent = Load<uint16_t>(extra.thresholds + 4);
        e.fixedSuperHP = Load<uint64_t>(extra.thresholds + 12);
        e.hpThreshold = Load<int16_t>(extra.thresholds + 20); e.hpInterval = Load<uint16_t>(extra.thresholds + 22);
        e.countThreshold = Load<int16_t>(extra.thresholds + 76); e.countInterval = Load<int16_t>(extra.thresholds + 78);
        uint32_t mask = 0, scale = 0, enabled = 0;
        extra.overrideKnown = PHBattleDecodeProtected(extra.overrideWords, key1, key2, 0x6a875a8bu, &mask) && mask <= UINT16_MAX &&
            PHBattleDecodeProtected(extra.overrideWords + 4, key1, key2, 0x6a875a8bu, &scale) && scale <= UINT16_MAX &&
            PHBattleDecodeProtected(extra.overrideWords + 8, key1, key2, 0x6a875a8bu, &enabled) && enabled <= UINT16_MAX;
        if (extra.overrideKnown) {
            extra.overrideMask = uint16_t(mask); extra.overrideScale = uint16_t(scale); extra.overrideEnabled = uint16_t(enabled);
        }
    }
    c.known = true;
    return true;
}
NSDictionary *EnemyDamageCheckpointTrace(const EH::Checkpoint &point) {
    const auto &e = point.state;
    return @{@"status":[NSString stringWithUTF8String:EH::StatusName(point.status)],
        @"localFieldsKnown":@(point.known), @"events":@(point.events),
        @"hpBeforeSurvival":point.prefixKnown ? @(point.hpBeforeSurvival) : NSNull.null,
        @"phaseBeforeSurvival":point.prefixKnown ? @(point.phaseBeforeSurvival) : NSNull.null,
        @"hp":point.known ? @(e.hp) : NSNull.null, @"survivalFlags":point.known ? @(e.survival) : NSNull.null,
        @"phaseFlags":point.known ? @(e.phase) : NSNull.null, @"nativeFlags":point.known ? @(e.flags) : NSNull.null,
        @"countdown":point.known ? @(e.countdown) : NSNull.null, @"selected":point.known ? @(e.selected) : NSNull.null,
        @"snapshotHP":point.known ? @(e.snapshotHP) : NSNull.null,
        @"secondarySnapshotHP":point.known ? @(e.secondarySnapshotHP) : NSNull.null,
        @"postPlayerStateKnown":@NO, @"nextEntryKnown":@NO, @"survivalKnown":@NO};
}
NSDictionary *EnemyDamageTrace(const EnemyActionEntry &enemy, const EnemyDamageCapture &capture, unsigned slot) {
    const auto &extra = capture.enemies[slot]; const auto &e = extra.state;
    if (!extra.read || !EH::Valid(e)) return @{@"status":@"unavailable", @"postPlayerStateKnown":@NO, @"enemyForecastKnown":@NO};
    const auto normal = EH::NormalThreshold(e), trigger = EH::SuperThreshold(e, false), floor = EH::SuperThreshold(e, true);
    const auto scan = EH::ConditionalPhaseEntry(capture.context, e, capture.activeByteSum, capture.activeByteSumKnown);
    NSMutableArray *scenarios = [NSMutableArray array];
    // These are named probes, NOT an exhaustive set of damage outcomes. Each
    // assumes the supplied captured fields survive the unmodeled resolver.
    const int64_t damages[]{0, e.hp > 0 ? e.hp - 1 : 0, e.hp};
    NSArray *names = @[@"zero-effective-damage", @"leave-one-before-survival", @"lethal-before-survival"];
    for (unsigned i = 0; i < 3; ++i) {
        EH::ResolvedHit hit; hit.known = true; hit.damage = damages[i];
        [scenarios addObject:@{@"name":names[i], @"assumedEffectiveDamage":@(hit.damage),
            @"checkpoint":EnemyDamageCheckpointTrace(EH::AfterResolvedHit(capture.context, e, hit))}];
    }
    NSMutableDictionary *out = [@{@"status":@"conditional", @"version":@(EH::Version),
        @"scope":@"local-post-resolver-field-checkpoints", @"snapshotHPCurrent":@(e.snapshotHP),
        @"secondarySnapshotHPCurrent":@(e.secondarySnapshotHP), @"snapshotEqualsCurrentHP":@(e.snapshotHP == e.hp),
        @"snapshotIsNextAttackHPKnown":@NO, @"survivalFlagsCurrent":@(e.survival), @"phaseFlagsCurrent":@(e.phase),
        @"separateHPModeCurrent":@(e.separateHP), @"normalPercentRaw":@(e.normalPercent),
        @"superTriggerPercentRaw":@(e.superTriggerPercent), @"superFloorPercentRaw":@(e.superFloorPercent),
        @"fixedSuperHPRaw":@(e.fixedSuperHP), @"normalThresholdHP":normal.known ? @(normal.value) : NSNull.null,
        @"superTriggerHP":trigger.known ? @(trigger.value) : NSNull.null, @"superFloorHP":floor.known ? @(floor.value) : NSNull.null,
        @"hpPhaseThresholdRaw":@(e.hpThreshold), @"hpPhaseIntervalRaw":@(e.hpInterval),
        @"countPhaseThresholdRaw":@(e.countThreshold), @"countPhaseIntervalRaw":@(e.countInterval),
        @"overrideAccumulationKnown":@(extra.overrideKnown),
        @"overrideAccumulationEnabledRaw":extra.overrideKnown ? @(extra.overrideEnabled) : NSNull.null,
        @"overrideAccumulationMaskRaw":extra.overrideKnown ? @(extra.overrideMask) : NSNull.null,
        @"overrideAccumulationScaleRaw":extra.overrideKnown ? @(extra.overrideScale) : NSNull.null,
        @"overrideStateCurrent":@(enemy.overrideState), @"overrideStateAfterDamageKnown":@NO,
        @"capturedFieldsAfterResolverAssumed":@YES, @"absorptionSkipAbsentAssumed":@YES,
        @"externalCallbackEffectsAbsentAssumed":@YES, @"scenarioCoverageComplete":@NO, @"probes":scenarios,
        @"ifBatchSnapshotRoutineRuns":EnemyDamageCheckpointTrace(EH::ConditionalBatchSnapshot(e)),
        @"ifCleanupRoutineReachesEnemy":EnemyDamageCheckpointTrace(EH::ConditionalCleanup(capture.context, e)),
        @"ifPhaseScanReachesEnemy":@{@"localFieldsKnown":@(scan.known),
            @"phaseFlags":scan.known ? @(scan.phase) : NSNull.null,
            @"countTriggered":scan.known ? @(scan.countTriggered) : NSNull.null,
            @"targetRequested":scan.known ? @(scan.targetRequested) : NSNull.null,
            @"earlierTargetAbsentAssumed":@YES, @"actualTargetKnown":@NO},
        @"unsupported":@[@"raw-to-effective-damage", @"resolver-side-effects", @"attack-order-and-hit-count",
            @"snapshot-refresh-timing", @"separate-hp", @"external-callback-effects", @"cross-enemy-effects",
            @"actual-phase-target-and-animation-timing"],
        @"postPlayerStateKnown":@NO, @"nextEntryKnown":@NO, @"enemyForecastKnown":@NO,
        @"actionReachabilityKnown":@NO, @"damageBoundKnown":@NO, @"survivalKnown":@NO} mutableCopy];
    if (scan.known && scan.targetRequested) {
        auto target = e; target.phase = scan.phase;
        out[@"ifSelectedTargetAnimationCompletes"] = EnemyDamageCheckpointTrace(EH::ConditionalPhaseAnimation(target));
    }
    return out;
}
NSDictionary *EnemyDamageSummary(const EnemyDamageCapture *capture, NSString *status) {
    NSMutableDictionary *out = [@{@"status":status, @"version":@(EH::Version), @"diagnosticOnly":@YES,
        @"readByteLimit":@(EH::MaxReadBytes), @"readCallLimit":@(EH::MaxReads),
        @"scenarioCoverageComplete":@NO, @"postPlayerStateKnown":@NO, @"nextEntryKnown":@NO,
        @"enemyForecastKnown":@NO, @"actionReachabilityKnown":@NO, @"damageBoundKnown":@NO, @"survivalKnown":@NO} mutableCopy];
    if (capture) [out addEntriesFromDictionary:@{@"readBytes":@(capture->readBytes), @"readCalls":@(capture->readCalls),
        @"immediateRaw":@(capture->context.immediate), @"specialMarkerRaw":@(capture->context.specialMarker),
        @"hpCallbackPresent":@(capture->context.callbackPresent), @"actionsEnabled":@(capture->context.actionsEnabled),
        @"activeByteSumKnown":@(capture->activeByteSumKnown),
        @"activeByteSum":capture->activeByteSumKnown ? @(capture->activeByteSum) : NSNull.null}];
    return out;
}
