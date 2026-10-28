// Included inside PHBattleNative's private namespace. This provider leases only
// an event-free native script context. Account-owned card records are never a
// parameter source written by this code.
struct ProviderContext {
    uintptr_t program, pool, runner;
    uint32_t sourceLength;
};
struct ProviderLease {
    bool active, entryBinding;
    uint8_t parameter;
    uint64_t token;
    uint32_t reboundSerial;
    PHBattleMember original;
    PHBattleProfile previous, installed;
};
struct BattleProvider {
    PHBattleIdentity identity;
    ProviderContext context;
    bool allocatedProgram;
    uint8_t entryTeamCount, entryPlayerTeam;
    uintptr_t entryBanks[PHBattleTeamCapacity];
    ProviderLease leases[6];
};

static uint16_t Profile16(const PHBattleProfile &p, size_t offset) {
    return static_cast<uint16_t>(p.bytes[offset] | (p.bytes[offset + 1] << 8));
}

static BOOL ReadCardInfo(uint16_t id, const PHBattleState *state, PHBattleCardInfo *info) {
    if (!CatalogValid(id, state)) return NO;
    uintptr_t record = state->catalog + id * PHBattleCatalogStride;
    uint16_t baseLevel = 0, limit = 0, flags = 0, superCount = 0;
    uint32_t skill = 0;
    *info = {};
    info->catalogID = id;
    if (!PHReadU16(record + 20, &baseLevel) || !baseLevel || baseLevel > 120 ||
        !PHReadU16(record + 832, &limit) || !PHReadU16(record + 826, &flags) || !(flags & 2u) ||
        !PHReadU32(record + 84, &skill) || skill > 500000 || !PHReadU16(record + 782, &superCount) ||
        superCount > sizeof(info->supers) || !PHReadU16(record + 788, &info->syncAwakening)) return NO;
    info->maxLevel = static_cast<int16_t>(limit) > 0 ? 120 : baseLevel;
    info->maxSkillLevel = 1;
    info->canAssist = (flags & 1u) != 0;
    info->latentSlots = (flags & 0x20u) ? 8 : 6;
    if (skill) {
        uintptr_t table = 0;
        if (!PHReadPointer(state->identity.header + PHSaveRVA + PHBattleSkillPointerOffset, &table) || !table ||
            table > UINTPTR_MAX - static_cast<uintptr_t>(skill) * 52u - 52u ||
            !PHReadU16(table + skill * 52u + 14, &info->maxSkillLevel) ||
            !info->maxSkillLevel || info->maxSkillLevel > 999) return NO;
    }
    for (unsigned i = 0; i < 10; ++i) {
        uint16_t raw = 0;
        if (!PHReadU16(record + 748 + i * 2, &raw)) return NO;
        int awakening = static_cast<int16_t>(raw);
        if (awakening >= 116 && awakening <= 120) awakening -= 94;
        if (awakening < 1) break;
        info->awakeningCount++;
    }
    info->superCount = static_cast<uint8_t>(superCount);
    if (superCount) {
        uintptr_t object = 0, pool = 0;
        uint32_t offset = 0;
        if (!PHReadPointer(state->identity.header + PHSaveRVA + PHBattleCardPoolOffset, &object) || !object ||
            object > UINTPTR_MAX - 24 || !PHReadPointer(object + 16, &pool) || !pool ||
            !PHReadU32(record + 784, &offset) || pool > UINTPTR_MAX - offset - superCount ||
            !PHReadMemory(pool + offset, info->supers, superCount)) return NO;
        for (unsigned i = 0; i < superCount; ++i) if (!info->supers[i]) return NO;
    }
    return YES;
}

static BOOL PrepareProfile(const PHBattleLoadout *loadout, const PHBattleState *state,
                           PHBattleProfile *profile, NSString **reason) {
    PHBattleCardInfo body = {}, assist = {};
    uint16_t bodyID = 0, assistID = 0;
    if (!loadout || !state || !PHBattleCatalogIDForNumber(loadout->bodyNumber, &bodyID) ||
        !ReadCardInfo(bodyID, state, &body)) {
        if (reason) *reason = @"本體編號或遊戲內角色資料尚未就緒";
        return NO;
    }
    if (loadout->assistNumber && (!PHBattleCatalogIDForNumber(loadout->assistNumber, &assistID) ||
        assistID > INT16_MAX || !ReadCardInfo(assistID, state, &assist) || !assist.canAssist)) {
        if (reason) *reason = @"武裝編號無效，或該角色不能作為繼承武裝";
        return NO;
    }
    if (!PHBattleBuildProfile(loadout, &body, loadout->assistNumber ? &assist : nullptr, profile)) {
        if (reason) *reason = @"等級、技能等級、加值、覺醒或潛覺超出該角色的可用範圍";
        return NO;
    }
    return YES;
}

static BOOL ReadIdleContext(uintptr_t header, ProviderContext *context) {
    uintptr_t address = header + PHBattleScriptRVA;
    if (!PHMemoryHasProtection(address, 18120, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE) ||
        !PHReadU32(address, &context->sourceLength) || !PHReadPointer(address + 8, &context->program) ||
        !PHReadPointer(address + 16, &context->pool) ||
        !PHReadPointer(address + PHBattleScriptRunnerOffset, &context->runner) || context->runner) return NO;
    uint8_t events[PHBattleEventCount * 8];
    if (!PHReadMemory(address + PHBattleScriptEventsOffset, events, sizeof(events))) return NO;
    for (unsigned i = 0; i < PHBattleEventCount; ++i) {
        uint32_t offset = 0;
        std::memcpy(&offset, events + i * 8, sizeof(offset));
        if (offset != UINT32_MAX) return NO;
    }
    return YES;
}

static BOOL ContextMatches(const BattleProvider *provider, uintptr_t header) {
    ProviderContext live = {};
    if (!provider->context.program || !ReadIdleContext(header, &live)) return NO;
    BOOL matches = live.program == provider->context.program && live.pool == provider->context.pool &&
                   live.sourceLength == provider->context.sourceLength;
    if (matches && provider->allocatedProgram) {
        uint8_t program[16] = {}, empty[16] = {};
        matches = PHReadMemory(live.program, program, sizeof(program)) && !std::memcmp(program, empty, sizeof(program));
    }
    return matches;
}

static uintptr_t ParameterAddress(uintptr_t header, unsigned index) {
    return header + PHBattleScriptRVA + PHBattleParameterOffset + index * PHBattleProfileSize;
}

static BOOL ProfileOwned(const BattleProvider *provider, const ProviderLease &lease, uintptr_t header) {
    PHBattleProfile live = {};
    return lease.active && ContextMatches(provider, header) &&
        PHReadMemory(ParameterAddress(header, lease.parameter), &live, sizeof(live)) &&
        !std::memcmp(&live, &lease.installed, sizeof(live));
}

static BOOL EntryProviderMatches(const BattleProvider *provider, const PHBattleState *state) {
    return PHBattleIsThreePlayer(state) && PHBattleTeamLayoutValid(state) &&
        provider->entryTeamCount == state->teamCount && provider->entryPlayerTeam == state->playerTeam &&
        !std::memcmp(provider->entryBanks, state->teamBanks, sizeof(provider->entryBanks));
}

static BOOL SameUnboundEntrySource(const PHBattleMember &original, const PHBattleMember &member) {
    // Both zero and -1 mean no script override in 0x1005A12F0. A peer may
    // serialize either; neither is permission to replace a positive binding.
    return PHBattleSameOrigin(&original, &member) && original.scriptSlot <= 0 && member.scriptSlot <= 0 &&
        original.baseID == member.baseID && original.assistID == member.assistID;
}

static void TagProviderMembers(BattleProvider *provider, PHBattleState *state) {
    if (!provider->context.program) return;
    // The game's native scene cleanup owns any still-attached native allocation.
    // Never free a cached pointer after the battle/context identity changes.
    if (!state->present || !PHBattleSameIdentity(&provider->identity, &state->identity)) { *provider = {}; return; }
    if (!ContextMatches(provider, state->identity.header)) return;
    BOOL entryContext = EntryProviderMatches(provider, state);
    for (const ProviderLease &lease : provider->leases) {
        if (!ProfileOwned(provider, lease, state->identity.header)) continue;
        auto tag = [&](PHBattleMember *members) {
            int matches = 0, position = -1;
            for (unsigned i = 0; i < 6; ++i) {
                PHBattleMember &m = members[i];
                if (PHBattleSameOrigin(&m, &lease.original) && m.scriptSlot == lease.parameter + 1 &&
                    m.baseID == Profile16(lease.installed, 4) && m.assistID == Profile16(lease.installed, 14)) {
                    ++matches; position = i;
                }
            }
            if (matches == 1) members[position].providerToken = lease.token;
        };
        tag(state->members);
        for (unsigned team = 0; team < state->teamCount; ++team) tag(state->storedMembers[team]);
        // 0x10001771C restores each bank's script byte from a peer's turn
        // snapshot. A peer without our local parameter provider sends the
        // original binding. Retain intent only for the same local source and
        // original base/assist, with our unchanged record wholly unreferenced.
        // New native scripts, changed sources and taken-over contexts win.
        if (!entryContext || !lease.entryBinding || state->turnSerial < lease.reboundSerial ||
            ParameterReferenced(state, lease.parameter + 1)) continue;
        auto pending = [&](PHBattleMember *members) {
            int matches = 0, position = -1;
            for (unsigned i = 0; i < 6; ++i) {
                const PHBattleMember &m = members[i];
                if (m.active && m.readable && m.sourceTeam == state->playerTeam &&
                    SameUnboundEntrySource(lease.original, m) && !m.providerToken) { ++matches; position = i; }
            }
            if (matches == 1) members[position].rebindToken = lease.token;
        };
        pending(state->members);
        pending(state->storedMembers[state->playerTeam]);
    }
}

static void RememberEntryBindings(BattleProvider *provider, const PHBattleState *state,
                                  uint8_t mask, const PHBattleMember *members) {
    if (!PHBattleIsThreePlayer(state)) return;
    provider->entryTeamCount = state->teamCount;
    provider->entryPlayerTeam = state->playerTeam;
    std::memcpy(provider->entryBanks, state->teamBanks, sizeof(provider->entryBanks));
    for (unsigned i = 0; i < 6; ++i) if (mask & (1u << i))
        for (ProviderLease &lease : provider->leases) if (lease.active && lease.token == members[i].providerToken) {
            lease.entryBinding = true;
            lease.reboundSerial = state->turnSerial;
        }
}

static int FreeParameter(const PHBattleState *state) {
    PHBattleProfile empty = {};
    for (unsigned index = 0; index < PHBattleParameterSlots; ++index) {
        BOOL inUse = ParameterReferenced(state, static_cast<int>(index + 1));
        PHBattleProfile live = {};
        if (!inUse && PHReadMemory(ParameterAddress(state->identity.header, index), &live, sizeof(live)) &&
            !std::memcmp(&live, &empty, sizeof(live))) return static_cast<int>(index);
    }
    return -1;
}

static BOOL ProviderAvailable(const BattleProvider *provider, const PHBattleState *state, NSString **reason) {
    ProviderContext context = {};
    BOOL available = state && ReadIdleContext(state->identity.header, &context) &&
        (!provider->context.program || (PHBattleSameIdentity(&provider->identity, &state->identity) &&
                                       ContextMatches(provider, state->identity.header))) && FreeParameter(state) >= 0;
    // An absent script must be completely unloaded before publishing a program.
    if (available && !context.program && (context.pool || context.sourceLength)) available = NO;
    if (available && !context.program) {
        uint32_t key1 = 0, key2 = 0, enemies = 0;
        available = PHReadU32(state->identity.header + PHBattleKey1RVA, &key1) &&
            PHReadU32(state->identity.header + PHBattleKey2RVA, &key2) &&
            DecodeAt(state->identity.impl + PHBattleEnemyCountOffset, key1, key2, 0x766cfc6du, &enemies) && enemies <= 8;
        for (unsigned i = 0; available && i < enemies; ++i) {
            uint16_t flags = 0;
            available = PHReadU16(state->identity.impl + PHBattleEnemyFlagsOffset + i * PHBattleEnemyStride, &flags) && !(flags & 0x40u);
        }
    }
    if (!available && reason) *reason = @"此關卡的編隊腳本正在使用參數，暫不能套用武裝與完整培養設定";
    return available;
}

static void EnsureProvider(BattleProvider *provider, const PHBattleState *state, BOOL *touched) {
    if (provider->context.program) return;
    ProviderContext context = {};
    if (!ReadIdleContext(state->identity.header, &context)) throw std::runtime_error("Script context changed");
    provider->identity = state->identity;
    provider->context = context;
    if (context.program) return;
    // Zero is the real compiler's end-of-program opcode (0x100595098).
    // Use the matching native allocator/tag, so native scene cleanup can free
    // this valid, empty bytecode buffer. Do not run the compiler/destructor over
    // the live controller, fabricate a pointer, or create any event entry.
    uintptr_t program = Calls(state->identity.header).allocate(16, 4, 44);
    if (!program) throw std::runtime_error("Native script allocation failed");
    uint8_t contents[16], empty[16] = {};
    if (!PHReadMemory(program, contents, sizeof(contents)) || std::memcmp(contents, empty, sizeof(contents))) {
        Calls(state->identity.header).release(program, 4, 44);
        *provider = {};
        throw std::runtime_error("Empty program initialization failed");
    }
    provider->context.program = program;
    provider->allocatedProgram = true;
    *touched = YES;
    BOOL written = PHWriteData(state->identity.header + PHBattleScriptRVA + 8, &program, sizeof(program));
    uintptr_t live = UINTPTR_MAX;
    BOOL readable = PHReadPointer(state->identity.header + PHBattleScriptRVA + 8, &live);
    if (!written || !readable || live != program || !ContextMatches(provider, state->identity.header)) {
        // Never free a buffer which might still be published in the controller.
        if (readable && !live) {
            Calls(state->identity.header).release(program, 4, 44);
            *provider = {};
            *touched = NO;
        }
        throw std::runtime_error("Empty program publication failed");
    }
}

static BOOL ReleaseIdleProvider(BattleProvider *provider, const PHBattleState *state) {
    uintptr_t header = state->identity.header;
    for (const ProviderLease &other : provider->leases) if (other.active) return YES;
    if (!provider->context.program) { *provider = {}; return YES; }
    if (!PHBattleSameIdentity(&provider->identity, &state->identity) || !ContextMatches(provider, header)) return NO;
    if (provider->allocatedProgram) {
        for (unsigned i = 0; i < PHBattleParameterSlots; ++i) if (ParameterReferenced(state, static_cast<int>(i + 1))) return YES;
        // Native code may have acquired another, not-yet-bound record. Leave a
        // still-attached program to the native scene cleanup in that case.
        PHBattleProfile record = {}, zero = {};
        for (unsigned i = 0; i < PHBattleParameterSlots; ++i) {
            if (!PHReadMemory(ParameterAddress(header, i), &record, sizeof(record))) return NO;
            if (std::memcmp(&record, &zero, sizeof(record))) return YES;
        }
        uintptr_t empty = 0, program = provider->context.program;
        uintptr_t live = UINTPTR_MAX;
        PHWriteData(header + PHBattleScriptRVA + 8, &empty, sizeof(empty));
        if (!PHReadPointer(header + PHBattleScriptRVA + 8, &live) || live) return NO;
        Calls(header).release(program, 4, 44);
    }
    *provider = {};
    return YES;
}

static BOOL ReturnRecord(BattleProvider *provider, ProviderLease *lease, const PHBattleState *state) {
    PHBattleProfile live = {};
    uintptr_t address = ParameterAddress(state->identity.header, lease->parameter);
    PHWriteData(address, &lease->previous, sizeof(lease->previous));
    if (!PHReadMemory(address, &live, sizeof(live)) || std::memcmp(&live, &lease->previous, sizeof(live))) return NO;
    lease->active = false;
    return ReleaseIdleProvider(provider, state);
}

static BOOL ReleaseProfile(BattleProvider *provider, ProviderLease *lease, const PHBattleState *state) {
    return ProfileOwned(provider, *lease, state->identity.header) && ReturnRecord(provider, lease, state);
}

static BOOL ProfileMatchesMember(const PHBattleProfile &profile, const PHBattleMember &member) {
    return member.level == Profile16(profile, 6) && member.skillLevel == Profile16(profile, 8) &&
        !std::memcmp(member.extra, profile.bytes + 24, sizeof(member.extra)) &&
        !std::memcmp(member.assist, profile.bytes + 14, sizeof(member.assist));
}

static PHBattleMutationResult MutateProfile(BattleProvider *provider, uint64_t *nextToken,
    const PHBattleState *expected, NSUInteger slot, const PHBattleLoadout *loadout,
    const PHBattleMember *original, PHBattleMember *output, NSString **reason, BOOL entry = NO) {
    PHBattleFrame frame = {expected->identity.header, expected->identity.player, expected->identity.epoch, true, true, expected->policy, false};
    PHBattleState before = {};
    if (!CheckCode(frame.header, reason) || !ReadLive(&before, frame, reason)) return PHBattleMutationRejected;
    TagProviderMembers(provider, &before);
    const PHBattleMember &old = before.members[slot];
    // Immediate entry rollback may follow a new audit flag from our failed
    // operation. Restore owned sources while keeping that flag in the game.
    BOOL phaseAllowed = entry && original ? before.entryWindow && CanReadNative(&before) &&
        before.hpMatches && before.digestKnown && before.digestMatches :
        (entry ? PHBattleEntryGate(&before) : PHBattleStateGate(&before)) == PHBattleGateReady;
    if (!phaseAllowed || (entry && PHBattleIsThreePlayer(&before) && before.currentTeam != before.playerTeam) ||
        !PHBattleSameIdentity(&expected->identity, &before.identity) || !PHBattleSameTeamView(expected, &before) ||
        expected->catalog != before.catalog || expected->catalogCount != before.catalogCount ||
        !PHBattleSameSource(&expected->members[slot], &old) || expected->members[slot].formID != old.formID ||
        expected->members[slot].catalogID != old.catalogID || expected->members[slot].providerToken != old.providerToken ||
        old.sourceSlot < 0 || old.sourceSlot > 5 || old.temporaryID || old.temporaryTurns ||
        !PHMemoryHasProtection(frame.header + PHBattlePRNGRVA, 4, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) {
        if (reason) *reason = @"執行前的戰鬥或隊員已變更，已取消操作";
        return PHBattleMutationRejected;
    }
    ProviderLease *lease = nullptr;
    PHBattleProfile profile = {};
    int parameter = -1;
    if (loadout) {
        if (old.scriptSlot > 0 || old.providerToken || old.rebindToken || !PrepareProfile(loadout, &before, &profile, reason) ||
            !ProviderAvailable(provider, &before, reason)) return PHBattleMutationRejected;
        for (ProviderLease &candidate : provider->leases) if (!candidate.active) { lease = &candidate; break; }
        parameter = FreeParameter(&before);
        if (!lease || parameter < 0) return PHBattleMutationRejected;
    } else {
        uint64_t token = old.providerToken ? old.providerToken : old.rebindToken;
        for (ProviderLease &candidate : provider->leases) if (candidate.active && candidate.token == token) { lease = &candidate; break; }
        if (!lease || !original || !PHBattleSameSource(&lease->original, original) || original->formID != lease->original.formID ||
            original->catalogID != lease->original.catalogID || !ProfileOwned(provider, *lease, frame.header)) return PHBattleMutationRejected;
        if (old.rebindToken && !old.providerToken) {
            // Sync has already restored the native source. Cancel its retained
            // entry intent by releasing only the unreferenced owned record.
            // Preserve the game's current level, form, HP and skill charge.
            if (entry || expected->members[slot].rebindToken != old.rebindToken ||
                ParameterReferenced(&before, lease->parameter + 1)) return PHBattleMutationRejected;
            BOOL released = ReleaseProfile(provider, lease, &before);
            if (!released && reason) *reason = @"同步後的編隊參數未能確認釋放，已停止後續寫入";
            return released ? PHBattleMutationApplied : PHBattleMutationUncertain;
        }
    }
    uintptr_t address = before.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
    BOOL changed = NO, touched = NO, bound = NO;
    uint16_t charge = 0;
    PHBattleState after = {};
    PHBattleMember wanted = loadout ? old : *original;
    NSString *failure = nil;
    try {
        @try {
            if (loadout) {
                EnsureProvider(provider, &before, &touched);
                // EnsureProvider does not move the inline lease array.
                *lease = {};
                lease->parameter = static_cast<uint8_t>(parameter);
                lease->original = old;
                lease->installed = profile;
                lease->token = ++*nextToken;
                if (!PHReadMemory(ParameterAddress(frame.header, parameter), &lease->previous, sizeof(lease->previous)))
                    throw std::runtime_error("Parameter snapshot failed");
                touched = YES;
                lease->active = true;
                RequireWrite(ParameterAddress(frame.header, parameter), &profile, sizeof(profile));
                if (!ProfileOwned(provider, *lease, frame.header)) throw std::runtime_error("Parameter publication failed");
                wanted.scriptSlot = static_cast<int8_t>(parameter + 1);
                wanted.baseID = Profile16(profile, 4);
                wanted.assistID = Profile16(profile, 14);
                wanted.formID = 0;
                wanted.catalogID = wanted.baseID;
            }
            touched = YES;
            bound = YES;
            Calls(frame.header).setByte(address + PHBattleScriptSlotOffset, static_cast<uint8_t>(wanted.scriptSlot));
            BOOL success = Perform(&before, slot, wanted.formID, wanted.catalogID, loadout ? nullptr : original,
                old.charge, YES, &changed, &charge, &failure, &wanted, entry);
            uint32_t audit = UINT32_MAX;
            success = success && ReadLive(&after, frame, &failure) &&
                PHReadU32(frame.header + PHSaveRVA + PHBattleAuditOffset, &audit) && !(audit & ~before.auditFlags) &&
                (loadout ? ProfileMatchesMember(profile, after.members[slot]) :
                 after.members[slot].level == original->level && after.members[slot].skillLevel == original->skillLevel &&
                 !std::memcmp(after.members[slot].extra, original->extra, sizeof(original->extra)) &&
                 !std::memcmp(after.members[slot].assist, original->assist, sizeof(original->assist)));
            if (success) {
                if (loadout) {
                    TagProviderMembers(provider, &after);
                    if (after.members[slot].providerToken != lease->token) throw std::runtime_error("Provider ownership lost");
                    *output = after.members[slot];
                } else if (!ReleaseProfile(provider, lease, &after)) {
                    if (reason) *reason = @"隊員已重建，但參數釋放未能確認；停止後續寫入";
                    return PHBattleMutationUncertain;
                }
                return PHBattleMutationApplied;
            }
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    if (!touched) {
        if (reason) *reason = @"完整隊伍設定在寫入前被取消";
        return PHBattleMutationRejected;
    }
    BOOL restored = NO;
    try {
        @try {
            PHBattleState current = {};
            // No native member has been bound yet. A failed/partial record
            // write is ours to return while the idle context and source match;
            // it must not strand an otherwise untouched team in a fault state.
            if (loadout && !bound && ReadLive(&current, frame, nullptr) &&
                PHBattleSameIdentity(&before.identity, &current.identity) &&
                PHBattleSameSource(&old, &current.members[slot]) && ContextMatches(provider, frame.header)) {
                BOOL referenced = ParameterReferenced(&current, parameter + 1);
                restored = !referenced && (lease->active ? ReturnRecord(provider, lease, &current) :
                                                           ReleaseIdleProvider(provider, &current));
            }
            if (bound && ReadLive(&current, frame, nullptr, &before, slot) && CanReadNative(&current) &&
                PHBattleSameIdentity(&before.identity, &current.identity) &&
                PHBattleSameTeamView(&before, &current) && PHBattleSameOrigin(&current.members[slot], &old) &&
                ProfileOwned(provider, *lease, frame.header)) {
                Calls(frame.header).setByte(address + PHBattleScriptSlotOffset, static_cast<uint8_t>(old.scriptSlot));
                restored = Perform(&current, slot, old.formID, old.catalogID, &old, old.charge, NO,
                    &changed, &charge, nullptr, &old, entry) && ReadLive(&after, frame, nullptr) &&
                    StoredTeamsUnchanged(&before, &after);
                if (restored && loadout) restored = ReleaseProfile(provider, lease, &after);
                if (restored && !loadout) {
                    TagProviderMembers(provider, &after);
                    restored = after.members[slot].providerToken == lease->token;
                }
            }
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { restored = NO; }
    if (reason) *reason = restored ? @"設定未通過原生校驗；本次操作已回復，已套用的位置仍可還原" :
        @"完整隊伍操作未能確認還原，已停止後續寫入；請複製診斷";
    return restored ? PHBattleMutationRolledBack : PHBattleMutationUncertain;
}
