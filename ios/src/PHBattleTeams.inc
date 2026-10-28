// Native 0x100019BBC loads this view, and 0x10000EFEC stores it before
// refresh switches through the banks. Account card records are read-only.
static BOOL SameMemberParameters(const PHBattleMember &a, const PHBattleMember &b) {
    return a.active == b.active && (!a.active || (PHBattleSameSource(&a, &b) &&
        a.formID == b.formID && a.catalogID == b.catalogID && a.temporaryID == b.temporaryID &&
        a.temporaryTurns == b.temporaryTurns && a.level == b.level && a.skillLevel == b.skillLevel &&
        !std::memcmp(a.extra, b.extra, sizeof(a.extra)) && !std::memcmp(a.assist, b.assist, sizeof(a.assist))));
}

static BOOL ReadTeamBanks(PHBattleState *state, uint32_t key1, uint32_t key2) {
    uintptr_t impl = state->identity.impl, header = state->identity.header;
    uint32_t count = 0, current = 0, shared = 0, player = 0, turn = 0;
    uint8_t busy = 0;
    if (!state->protectedValuesValid || !CheckCode(header, nullptr) ||
        !PHReadU8(impl + PHBattleTeamBusyOffset, &busy) || (busy & 8u) ||
        !PHReadU32(impl + PHBattleTeamCountOffset, &count) || !count || count > PHBattleTeamCapacity ||
        !PHReadU32(impl + PHBattleCurrentTeamOffset, &current) || current >= count) return NO;
    state->teamCount = static_cast<uint8_t>(count);
    state->currentTeam = static_cast<uint8_t>(current);
    if (PHBattleIsThreePlayer(state)) {
        // 0xa3e4c is the local network seat. 0xa3e5c belongs to the shared
        // leader/helper rules and must not be used to decide whose turn it is.
        if (count < 2 || count > 3 ||
            !PHReadU32(impl + PHBattlePlayerTeamOffset, &player) || player >= count ||
            !PHReadU32(impl + PHBattleTurnSerialOffset, &turn) || turn > INT32_MAX) return NO;
        state->playerTeam = static_cast<uint8_t>(player);
        state->turnSerial = turn;
        state->turnTeam = static_cast<uint8_t>(turn % count);
        state->turnInfoValid = true;
    }
    try {
        @try {
            NativeCalls native = Calls(header);
            uint64_t size = native.teamSize();
            if (size != 5 && size != 6) return NO;
            state->teamSize = static_cast<uint8_t>(size);
            BOOL ordinary = state->mode != 7 && !(native.soloRules(0) & 1u);
            BOOL sharedHelper = NO, sharedLeader = NO;
            if (ordinary) {
                uintptr_t dungeon = 0;
                uint32_t rules = 0;
                if (!PHReadPointer(impl + PHBattleDungeonPointerOffset, &dungeon) || !dungeon ||
                    !PHReadU32(dungeon + PHBattleDungeonRulesOffset, &rules)) return NO;
                if (rules) {
                    uintptr_t object = 0, pool = 0;
                    if (!PHReadPointer(header + PHSaveRVA + PHBattleDungeonRulePoolOffset, &object) || !object ||
                        object > UINTPTR_MAX - 24 || !PHReadPointer(object + 16, &pool) || !pool ||
                        pool > UINTPTR_MAX - rules || !PHMemoryHasProtection(pool + rules, 1, VM_PROT_READ, 0)) return NO;
                }
                sharedHelper = (native.sharedHelper(dungeon) & 1u) != 0;
                sharedLeader = (native.sharedLeader(dungeon) & 1u) != 0;
                if ((sharedHelper || sharedLeader) &&
                    (!PHReadU32(impl + PHBattleSharedTeamOffset, &shared) || shared >= count)) return NO;
            }
            for (unsigned team = 0; team < count; ++team) {
                uintptr_t descriptor = impl + PHBattleTeamDescriptorOffset + team * PHBattleTeamDescriptorStride;
                uintptr_t address = 0, expected = header + PHBattleTeamStorageRVA + team * PHBattleTeamStorageStride;
                uint8_t flags = 0;
                if (!PHReadU8(descriptor, &flags) || !(flags & 1u) ||
                    !PHReadPointer(descriptor + PHBattleTeamPointerOffset, &address) || address != expected ||
                    !PHMemoryHasProtection(address, PHBattleTeamStorageStride, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) return NO;
                state->teamBanks[team] = address;
                if (PHBattleIsThreePlayer(state) && (!(flags & 4u) ||
                    !DecodeAt(address + PHBattleTeamHPOffset, key1, key2, 0x766cfc6du, &state->teamHP[team]))) return NO;
                for (unsigned slot = 0; slot < 6; ++slot) {
                    PHBattleMember &m = state->storedMembers[team][slot];
                    BOOL chargeValid = NO;
                    if (!ReadMember(address + slot * PHBattleMemberStride, state, key1, key2, &m, &chargeValid) ||
                        (m.active && (m.sourceSlot < 0 || m.sourceSlot > 5))) return NO;
                    m.sourceTeam = static_cast<uint8_t>(team);
                    state->entryChargesValid &= chargeValid;
                }
            }
            // Shared leaders are mirrored in each team's first stored slot.
            // Give those mirrors the same origin, so updating the owner cannot
            // be mistaken for changing an unrelated player's member.
            if (ordinary && sharedLeader) for (unsigned team = 0; team < count; ++team) {
                PHBattleMember &leader = state->storedMembers[team][0];
                const PHBattleMember &owner = state->storedMembers[shared][0];
                leader.sourceTeam = static_cast<uint8_t>(shared);
                leader.sourceSlot = owner.sourceSlot;
                if (!SameMemberParameters(leader, owner)) return NO;
            }
            for (unsigned slot = 0; slot < 6; ++slot) {
                PHBattleMember &view = state->members[slot];
                if (!view.active) continue;
                unsigned team = slot < size ? current : current ^ 1u;
                unsigned position = slot < size ? slot : 0;
                if (ordinary) {
                    if ((sharedHelper || sharedLeader) && slot >= size) { team = shared; position = 5; }
                    else if (sharedLeader && slot == 0) { team = shared; position = 0; }
                }
                if (team >= count || view.sourceSlot < 0 || view.sourceSlot > 5) return NO;
                const PHBattleMember &stored = state->storedMembers[team][position];
                int8_t nativeSource = ordinary && slot >= size ? 5 : stored.sourceSlot;
                if (!stored.active || view.sourceSlot != nativeSource) return NO;
                view.sourceTeam = static_cast<uint8_t>(team);
                view.sourceSlot = stored.sourceSlot;
                if (!SameMemberParameters(view, stored)) return NO;
            }
            uint32_t lastCount = 0, lastCurrent = 0;
            state->teamBanksValid = PHReadU32(impl + PHBattleTeamCountOffset, &lastCount) && lastCount == count &&
                PHReadU32(impl + PHBattleCurrentTeamOffset, &lastCurrent) && lastCurrent == current;
            if (PHBattleIsThreePlayer(state)) {
                uint32_t lastPlayer = 0, lastTurn = 0;
                state->teamBanksValid &= PHReadU32(impl + PHBattlePlayerTeamOffset, &lastPlayer) && lastPlayer == player &&
                    PHReadU32(impl + PHBattleTurnSerialOffset, &lastTurn) && lastTurn == turn;
            }
            return state->teamBanksValid;
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    return NO;
}

static BOOL ParameterReferenced(const PHBattleState *state, int scriptSlot) {
    for (const PHBattleMember &m : state->members) if (m.active && m.scriptSlot == scriptSlot) return YES;
    for (unsigned team = 0; team < state->teamCount; ++team)
        for (const PHBattleMember &m : state->storedMembers[team])
            if (m.active && m.scriptSlot == scriptSlot) return YES;
    return NO;
}

static BOOL StoredTeamsUnchanged(const PHBattleState *before, const PHBattleState *after,
                                const PHBattleMember *changed = nullptr, BOOL charges = YES) {
    if (!PHBattleSameTeamView(before, after)) return NO;
    if (PHBattleIsThreePlayer(before)) for (unsigned team = 0; team < before->teamCount; ++team)
        if ((!changed || changed->sourceTeam != team) && before->teamHP[team] != after->teamHP[team]) return NO;
    for (unsigned team = 0; team < before->teamCount; ++team) for (unsigned slot = 0; slot < 6; ++slot) {
        const PHBattleMember &stored = before->storedMembers[team][slot];
        if (changed && PHBattleSameOrigin(&stored, changed)) continue;
        const PHBattleMember *expected = &stored;
        for (const PHBattleMember &view : before->members)
            if (PHBattleSameOrigin(&stored, &view)) { expected = &view; break; }
        const PHBattleMember &live = after->storedMembers[team][slot];
        if (!SameMemberParameters(*expected, live) || (charges && expected->active && expected->charge != live.charge)) return NO;
    }
    return YES;
}
