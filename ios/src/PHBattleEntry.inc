// Included after the parameter provider, inside the native engine namespace.
// Only a new dungeon's wave-zero intro or three-player entry sync may replace
// initial SB. Phase 2 must never use this charge-reset transaction.
static BOOL SameEntryMembers(const PHBattleState &a, const PHBattleState &b, BOOL charges) {
    for (unsigned i = 0; i < 6; ++i) {
        const PHBattleMember &old = a.members[i], &live = b.members[i];
        if (old.active != live.active) return NO;
        if (!old.active) continue;
        if (!PHBattleSameSource(&old, &live) || old.providerToken != live.providerToken || old.rebindToken != live.rebindToken ||
            old.formID != live.formID || old.catalogID != live.catalogID ||
            old.previousID != live.previousID || old.radius != live.radius || old.flags != live.flags ||
            old.level != live.level || old.skillLevel != live.skillLevel ||
            std::memcmp(old.extra, live.extra, sizeof(old.extra)) ||
            std::memcmp(old.assist, live.assist, sizeof(old.assist)) || (charges && old.charge != live.charge)) return NO;
    }
    return YES;
}

static uint16_t InitialTeamBoost(const PHBattleState &state, BOOL *specialEntry) {
    uint32_t special = 0;
    if (!PHReadU32(state.identity.impl + PHBattleSpecialEntryOffset, &special))
        throw std::runtime_error("Entry rule unreadable");
    *specialEntry = special == 525190;
    if (*specialEntry) return 1000;
    NativeCalls native = Calls(state.identity.header);
    uint8_t mask = 0;
    auto awakenings = [&](uint64_t id) {
        int32_t count = native.teamAwakening(state.identity.game, id, &mask, 0, 0);
        if (count < 0 || count > 1000) throw std::runtime_error("Invalid entry awakening count");
        return count;
    };
    // Follow the constructor's native counters: SB, SB+, SB prism, SB minus,
    // the SB latent and the active badge. Native counters include weapons,
    // supers/sync awakenings and the dungeon's restrictions.
    int64_t total = awakenings(21) + 2LL * awakenings(56) + 2LL * awakenings(145) - awakenings(105);
    uintptr_t dungeon = 0;
    uint8_t dungeonFlags = 0;
    uint16_t awakeningState = 0;
    if (!PHReadPointer(state.identity.impl + PHBattleDungeonPointerOffset, &dungeon) || !dungeon ||
        !PHReadU8(dungeon + 33, &dungeonFlags) ||
        !PHReadU16(state.identity.impl + PHBattleAwakeningStateOffset, &awakeningState))
        throw std::runtime_error("Entry awakening rules unreadable");
    if ((dungeonFlags & 4u) || static_cast<int16_t>(awakeningState << 6) > 0) total = 0;
    int32_t latents = native.teamLatent(state.identity.game, 47, &mask);
    int32_t badge = native.badgeBoost(static_cast<int32_t>(native.currentBadge(state.identity.game)));
    if (latents < 0 || latents > 1000 || badge < 0 || badge > 1000)
        throw std::runtime_error("Invalid entry latent/badge count");
    total += 3LL * latents + badge;
    if (total > 1000) throw std::runtime_error("Invalid initial skill boost");
    return static_cast<uint16_t>(std::max<int64_t>(0, total));
}

static BOOL RestoreEntryTeam(BattleProvider *provider, uint64_t *nextToken, uint8_t applied,
                            const PHBattleState &original, const uint32_t chargeShadows[6]) {
    PHBattleFrame frame = {original.identity.header, original.identity.player, original.identity.epoch, true, true, original.policy, false};
    try {
        @try {
            for (int i = 5; i >= 0; --i) {
                if (!(applied & (1u << i))) continue;
                PHBattleState current = {};
                if (!ReadLive(&current, frame, nullptr)) return NO;
                TagProviderMembers(provider, &current);
                if (MutateProfile(provider, nextToken, &current, i, nullptr, &original.members[i], nullptr, nullptr, YES) !=
                    PHBattleMutationApplied) return NO;
            }
            PHBattleState current = {};
            if (!ReadLive(&current, frame, nullptr)) return NO;
            TagProviderMembers(provider, &current);
            if (!current.entryWindow || !CanReadNative(&current) ||
                !PHBattleSameIdentity(&original.identity, &current.identity) || !SameEntryMembers(original, current, NO)) return NO;
            NativeCalls native = Calls(frame.header);
            for (unsigned i = 0; i < 6; ++i) {
                if (!original.members[i].active) continue;
                uintptr_t member = original.identity.impl + PHBattleMemberOffset + i * PHBattleMemberStride;
                native.set16(member + PHBattleChargeOffset, original.members[i].charge);
                RequireWrite(member + PHBattleChargeShadowOffset, &chargeShadows[i], sizeof(chargeShadows[i]));
            }
            // Restore the known initial HP after any intermediate team cap.
            // Never clear an audit flag or restore native resource pointers.
            native.set32(original.identity.impl + PHBattleHPOffset, original.hp);
            uint32_t hpShadow = original.hp ^ 0x8085a6beu;
            RequireWrite(original.identity.impl + PHBattleHPShadowOffset, &hpShadow, sizeof(hpShadow));
            // Refresh the HP display without issuing another entry refill.
            native.adjustHP(original.identity.impl + PHBattleHPControllerOffset, 0, 1);
            // Publish both charge and restored HP into their owning banks.
            if (!PHBattleUsesPrimaryTeam(original.multiplayer)) native.refreshTeam(original.identity.game, 0);
            uint32_t digestShadow = static_cast<uint32_t>(native.digest(original.identity.game)) ^ 0x689d8b5cu;
            RequireWrite(original.identity.impl + PHBattleDigestShadowOffset, &digestShadow, sizeof(digestShadow));
            PHBattleState restored = {};
            if (!ReadLive(&restored, frame, nullptr)) return NO;
            TagProviderMembers(provider, &restored);
            return restored.entryWindow && CanReadNative(&restored) && restored.hpMatches && restored.digestMatches &&
                restored.entryChargesValid && restored.hp == original.hp &&
                PHBattleSameIdentity(&original.identity, &restored.identity) && SameEntryMembers(original, restored, YES) &&
                StoredTeamsUnchanged(&original, &restored);
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    return NO;
}

static PHBattleMutationResult ApplyEntryTeam(BattleProvider *provider, uint64_t *nextToken,
    const PHBattleLoadout *loadouts, uint8_t mask, const PHBattleState *expected,
    PHBattleMember *members, uint16_t *boost, NSString **reason) {
    if (!loadouts || !mask || (mask & ~0x3fu)) return PHBattleMutationRejected;
    PHBattleFrame frame = {expected->identity.header, expected->identity.player, expected->identity.epoch, true, true, expected->policy, false};
    PHBattleState before = {};
    if (!CheckCode(frame.header, reason) || !ReadLive(&before, frame, reason)) return PHBattleMutationRejected;
    TagProviderMembers(provider, &before);
    if (PHBattleEntryGate(&before) != PHBattleGateReady || !before.entryChargesValid ||
        (PHBattleIsThreePlayer(&before) && before.currentTeam != before.playerTeam) ||
        !PHBattleSameIdentity(&expected->identity, &before.identity) || !PHBattleSameTeamView(expected, &before) ||
        expected->catalog != before.catalog ||
        expected->catalogCount != before.catalogCount || !SameEntryMembers(*expected, before, YES)) {
        if (reason) *reason = @"新入場動畫、隊伍或技能充能校驗已變更；本次未自動套用";
        return PHBattleMutationRejected;
    }
    uint32_t chargeShadows[6] = {};
    unsigned required = 0, available = 0;
    PHBattleProfile empty = {};
    for (unsigned index = 0; index < PHBattleParameterSlots; ++index) {
        BOOL referenced = ParameterReferenced(&before, static_cast<int>(index + 1));
        PHBattleProfile record = {};
        available += !referenced && PHReadMemory(ParameterAddress(frame.header, index), &record, sizeof(record)) &&
            !std::memcmp(&record, &empty, sizeof(record));
    }
    for (unsigned i = 0; i < 6; ++i) {
        const PHBattleMember &m = before.members[i];
        if (m.active && !PHReadU32(before.identity.impl + PHBattleMemberOffset + i * PHBattleMemberStride +
                                  PHBattleChargeShadowOffset, &chargeShadows[i])) return PHBattleMutationRejected;
        if (!(mask & (1u << i))) continue;
        ++required;
        PHBattleProfile profile = {};
        if (!loadouts[i].parameters || !m.active || !m.readable || m.sourceSlot < 0 || m.sourceSlot > 5 ||
            m.scriptSlot > 0 || m.providerToken || m.temporaryID || m.temporaryTurns ||
            !PrepareProfile(&loadouts[i], &before, &profile, reason) || !ProviderAvailable(provider, &before, reason)) {
            if (reason && !*reason) *reason = [NSString stringWithFormat:@"位置 %u 無法在入場時套用", i + 1];
            return PHBattleMutationRejected;
        }
        for (unsigned j = 0; j < 6; ++j) if (i != j && PHBattleSameSource(&m, &before.members[j])) {
            if (reason) *reason = @"入場隊員來源不唯一，已取消自動套用";
            return PHBattleMutationRejected;
        }
    }
    if (available < required) {
        if (reason) *reason = @"空閒編隊參數不足，本次未自動套用";
        return PHBattleMutationRejected;
    }
    uint8_t applied = 0;
    PHBattleMutationResult failed = PHBattleMutationRejected;
    NSString *failure = nil;
    try {
        @try {
            PHBattleState current = before;
            for (unsigned i = 0; i < 6; ++i) {
                if (!(mask & (1u << i))) continue;
                PHBattleMember member = {};
                failed = MutateProfile(provider, nextToken, &current, i, &loadouts[i], nullptr, &member, &failure, YES);
                if (failed != PHBattleMutationApplied) {
                    if (failed == PHBattleMutationUncertain) {
                        if (reason) *reason = failure;
                        return failed;
                    }
                    throw std::runtime_error("Entry member transaction failed");
                }
                applied |= static_cast<uint8_t>(1u << i);
                if (!ReadLive(&current, frame, &failure)) throw std::runtime_error("Entry team unreadable");
                TagProviderMembers(provider, &current);
            }
            if (PHBattleEntryGate(&current) != PHBattleGateReady || !current.entryChargesValid)
                throw std::runtime_error("Entry window changed");
            BOOL specialEntry = NO;
            uint16_t initial = InitialTeamBoost(current, &specialEntry);
            NativeCalls native = Calls(frame.header);
            for (unsigned i = 0; i < 6; ++i) {
                if (!current.members[i].active) continue;
                uintptr_t member = current.identity.impl + PHBattleMemberOffset + i * PHBattleMemberStride;
                native.set16(member + PHBattleChargeOffset, 0);
                uint32_t shadow = 0x68fb65ceu;
                RequireWrite(member + PHBattleChargeShadowOffset, &shadow, sizeof(shadow));
            }
            // Use the same native charging loop as dungeon entry. It handles
            // disabled skills, per-member caps and each charge mirror itself.
            if (specialEntry) {
                // This entry rule calls the single-member helper directly in
                // the constructor, rather than the ordinary team tick loop.
                for (unsigned i = 0; i < 6; ++i) {
                    if (!current.members[i].active) continue;
                    uintptr_t member = current.identity.impl + PHBattleMemberOffset + i * PHBattleMemberStride;
                    if (native.memberHasSkill(member)) native.advanceMember(member, 1000);
                }
            } else for (unsigned tick = 0; tick < initial; ++tick) native.advanceTeam(current.identity.game, -1, 0);
            if (!PHBattleUsesPrimaryTeam(current.multiplayer)) native.refreshTeam(current.identity.game, 0);
            PHBattleState after = {};
            uint32_t audit = UINT32_MAX;
            if (!ReadLive(&after, frame, &failure)) throw std::runtime_error("Entry charge unreadable");
            TagProviderMembers(provider, &after);
            if (PHBattleEntryGate(&after) != PHBattleGateReady || !after.entryChargesValid || after.hp != current.hp ||
                !PHBattleSameIdentity(&before.identity, &after.identity) || !SameEntryMembers(current, after, NO) ||
                !StoredTeamsUnchanged(&current, &after, nullptr, NO) ||
                !PHReadU32(frame.header + PHSaveRVA + PHBattleAuditOffset, &audit) || (audit & ~before.auditFlags))
                throw std::runtime_error("Initial skill boost check failed");
            for (unsigned i = 0; i < 6; ++i) {
                if (!after.members[i].active) continue;
                uint64_t maximum = native.maxCharge(after.identity.impl + PHBattleMemberOffset + i * PHBattleMemberStride, 0);
                if (maximum > UINT16_MAX || after.members[i].charge > maximum)
                    throw std::runtime_error("Initial skill charge exceeds native cap");
            }
            // Normal refresh already rebuilt max HP, recovery, awakenings,
            // leader effects and move time. It only clamps existing HP. A new
            // dungeon must also initialize current HP from the new team's
            // effective maximum (including fixed/percentage dungeon rules).
            // Do this once, after the whole batch, and only for the selected
            // local team. refreshTeam(game, 1) would refill other banks too.
            PHBattleState charged = after;
            uintptr_t hpController = after.identity.impl + PHBattleHPControllerOffset;
            uint64_t initialHP = native.maximumHP(hpController);
            if (!initialHP || initialHP > INT32_MAX) throw std::runtime_error("Invalid initial HP maximum");
            native.adjustHP(hpController, static_cast<int32_t>(initialHP), 1);
            if (!PHBattleUsesPrimaryTeam(after.multiplayer)) native.refreshTeam(after.identity.game, 0);
            if (!ReadLive(&after, frame, &failure)) throw std::runtime_error("Initial HP unreadable");
            TagProviderMembers(provider, &after);
            if (PHBattleEntryGate(&after) != PHBattleGateReady || !after.entryChargesValid || after.hp != initialHP ||
                native.maximumHP(hpController) != initialHP || !PHBattleSameIdentity(&charged.identity, &after.identity) ||
                !SameEntryMembers(charged, after, YES) || !StoredTeamsUnchanged(&charged, &after, nullptr, YES, YES) ||
                (PHBattleIsThreePlayer(&after) && after.teamHP[after.currentTeam] != initialHP) ||
                !PHReadU32(frame.header + PHSaveRVA + PHBattleAuditOffset, &audit) || (audit & ~before.auditFlags))
                throw std::runtime_error("Initial HP check failed");
            std::memcpy(members, after.members, sizeof(after.members));
            *boost = initial;
            return PHBattleMutationApplied;
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    if (!applied) {
        if (reason) *reason = failure ?: @"入場設定未完成，隊伍未被套用";
        return failed == PHBattleMutationRolledBack ? failed : PHBattleMutationRejected;
    }
    BOOL restored = RestoreEntryTeam(provider, nextToken, applied, before, chargeShadows);
    if (reason) *reason = restored ? @"入場自動套用未通過校驗；整隊、原有充能及 HP 已回復" :
        @"入場隊伍未能確認完整還原，已停止後續寫入；請複製診斷";
    return restored ? PHBattleMutationRolledBack : PHBattleMutationUncertain;
}

static BOOL SameEntryBanks(const PHBattleState &a, const PHBattleState &b, int exceptTeam = -1) {
    if (!PHBattleSameTeamBanks(&a, &b)) return NO;
    for (unsigned team = 0; team < a.teamCount; ++team) {
        if (static_cast<int>(team) == exceptTeam) continue;
        if (a.teamHP[team] != b.teamHP[team]) return NO;
        for (unsigned slot = 0; slot < 6; ++slot) {
            const PHBattleMember &old = a.storedMembers[team][slot], &live = b.storedMembers[team][slot];
            if (!SameMemberParameters(old, live) || (old.active &&
                (old.charge != live.charge || old.providerToken != live.providerToken || old.rebindToken != live.rebindToken))) return NO;
        }
    }
    return YES;
}

static BOOL EntryViewMatchesBank(const PHBattleState &state) {
    if (!PHBattleIsThreePlayer(&state) || !PHBattleTeamLayoutValid(&state) ||
        state.hp != state.teamHP[state.currentTeam]) return NO;
    for (unsigned slot = 0; slot < 6; ++slot) {
        const PHBattleMember &view = state.members[slot], &stored = state.storedMembers[state.currentTeam][slot];
        if (!SameMemberParameters(view, stored) || (view.active &&
            (view.charge != stored.charge || view.providerToken != stored.providerToken))) return NO;
    }
    return YES;
}

static BOOL SameEntryContext(const PHBattleState &a, const PHBattleState &b) {
    return b.entryWindow && CanReadNative(&b) && b.hpMatches && b.digestMatches && b.entryChargesValid &&
        a.phase == b.phase && PHBattleSameIdentity(&a.identity, &b.identity) && PHBattleSameTeamBanks(&a, &b) &&
        a.catalog == b.catalog && a.catalogCount == b.catalogCount;
}

// Native entry initializes every bank and then waits in phase 46. Select only
// the local network seat using the same native loader used by refreshTeam;
// finish the entire transaction and restore the original view in this frame.
// Neither the turn counter nor a remote team's members/HP/charge may change.
static PHBattleMutationResult ApplyLocalEntryTeam(BattleProvider *provider, uint64_t *nextToken,
    const PHBattleLoadout *loadouts, uint8_t mask, const PHBattleState *expected,
    PHBattleMember *members, uint16_t *boost, NSString **reason) {
    if (!PHBattleIsThreePlayer(expected))
        return ApplyEntryTeam(provider, nextToken, loadouts, mask, expected, members, boost, reason);
    if (!loadouts || !mask || (mask & ~0x3fu)) return PHBattleMutationRejected;
    PHBattleFrame frame = {expected->identity.header, expected->identity.player, expected->identity.epoch, true, true, expected->policy, false};
    PHBattleState before = {};
    if (!CheckCode(frame.header, reason) || !ReadLive(&before, frame, reason)) return PHBattleMutationRejected;
    TagProviderMembers(provider, &before);
    if (PHBattleEntryGate(&before) != PHBattleGateReady || !SameEntryContext(*expected, before) ||
        !PHBattleSameTeamView(expected, &before) || !SameEntryMembers(*expected, before, YES) ||
        !SameEntryBanks(*expected, before) || !EntryViewMatchesBank(before)) {
        if (reason) *reason = @"多人入場、席位或隊伍同步狀態已變更；本次未自動套用";
        return PHBattleMutationRejected;
    }
    if (before.currentTeam == before.playerTeam)
        return ApplyEntryTeam(provider, nextToken, loadouts, mask, &before, members, boost, reason);

    PHBattleMutationResult result = PHBattleMutationRejected;
    PHBattleMember installed[6] = {};
    uint16_t initial = 0;
    BOOL viewRestored = NO;
    try {
        @try {
            Calls(frame.header).switchTeam(before.identity.game, before.playerTeam, 0);
            PHBattleState selected = {};
            if (!ReadLive(&selected, frame, reason)) throw std::runtime_error("Local entry view unreadable");
            TagProviderMembers(provider, &selected);
            if (!SameEntryContext(before, selected) || selected.currentTeam != before.playerTeam ||
                selected.auditFlags != before.auditFlags || !SameEntryBanks(before, selected) || !EntryViewMatchesBank(selected))
                throw std::runtime_error("Local entry view changed another team");
            result = ApplyEntryTeam(provider, nextToken, loadouts, mask, &selected, installed, &initial, reason);
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    try {
        @try {
            PHBattleState current = {};
            if (!ReadLive(&current, frame, nullptr) || !SameEntryContext(before, current))
                throw std::runtime_error("Entry context lost before restoring view");
            Calls(frame.header).switchTeam(before.identity.game, before.currentTeam, 0);
            PHBattleState after = {};
            if (!ReadLive(&after, frame, nullptr)) throw std::runtime_error("Restored entry view unreadable");
            TagProviderMembers(provider, &after);
            viewRestored = SameEntryContext(before, after) && PHBattleSameTeamView(&before, &after) &&
                EntryViewMatchesBank(after) && after.hp == before.hp && SameEntryMembers(before, after, YES) &&
                SameEntryBanks(before, after, result == PHBattleMutationApplied || result == PHBattleMutationUncertain ? before.playerTeam : -1);
            if (viewRestored && result == PHBattleMutationApplied) {
                viewRestored = after.auditFlags == before.auditFlags;
                for (unsigned slot = 0; slot < 6; ++slot) {
                    const PHBattleMember &stored = after.storedMembers[before.playerTeam][slot];
                    viewRestored &= SameMemberParameters(installed[slot], stored) &&
                        installed[slot].charge == stored.charge && installed[slot].providerToken == stored.providerToken;
                }
            }
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    if (!viewRestored) {
        if (reason) *reason = @"多人入場未能確認還原原隊伍畫面，已停止後續寫入；請複製診斷";
        return PHBattleMutationUncertain;
    }
    if (result == PHBattleMutationApplied) {
        std::memcpy(members, installed, sizeof(installed));
        *boost = initial;
    } else if (reason && !*reason) *reason = @"本機隊伍未通過入場檢查；已回復原隊伍畫面";
    return result;
}
