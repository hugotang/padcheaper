// Included after the entry transaction. Multiplayer snapshots carry script
// bindings, but not this process's leased profiles. Reattach an unchanged owned
// profile only on the local player's operation turn. This is never an SB pass.
static ProviderLease *RebindLease(BattleProvider *provider, const PHBattleMember &member) {
    for (ProviderLease &lease : provider->leases)
        if (lease.active && lease.entryBinding && lease.token == member.rebindToken &&
            SameUnboundEntrySource(lease.original, member)) return &lease;
    return nullptr;
}

static BOOL SameRebindContext(const PHBattleState &before, const PHBattleState &current) {
    return PHBattleIsThreePlayer(&current) && current.phase == 2 && CanReadNative(&current) &&
        current.currentTeam == current.playerTeam && current.turnTeam == current.playerTeam &&
        PHBattleSameIdentity(&before.identity, &current.identity) && PHBattleSameTeamView(&before, &current) &&
        before.catalog == current.catalog && before.catalogCount == current.catalogCount;
}

static BOOL RestoreReboundTeam(BattleProvider *provider, const PHBattleState &original, uint8_t touched,
                               const PHBattleState &lastBefore, unsigned lastSlot) {
    PHBattleFrame frame = {original.identity.header, original.identity.player, original.identity.epoch,
                          true, true, original.policy, false};
    try {
        @try {
            PHBattleState current = {};
            // Only the final in-flight member may be ahead of its stored bank.
            if (!ReadLive(&current, frame, nullptr, &lastBefore, lastSlot)) return NO;
            TagProviderMembers(provider, &current);
            for (int slot = 5; slot >= 0; --slot) {
                if (!(touched & (1u << slot))) continue;
                const PHBattleMember &old = original.members[slot];
                ProviderLease *lease = RebindLease(provider, old);
                if (!SameRebindContext(original, current) || !lease ||
                    !ProfileOwned(provider, *lease, frame.header) ||
                    !PHBattleSameOrigin(&old, &current.members[slot])) return NO;
                uintptr_t address = original.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
                Calls(frame.header).setByte(address + PHBattleScriptSlotOffset, static_cast<uint8_t>(old.scriptSlot));
                BOOL changed = NO;
                uint16_t charge = 0;
                if (!Perform(&current, slot, old.formID, old.catalogID, &old, old.charge, NO,
                             &changed, &charge, nullptr, &old, YES) || !ReadLive(&current, frame, nullptr)) return NO;
                TagProviderMembers(provider, &current);
            }
            NativeCalls native = Calls(frame.header);
            // Undo an intermediate HP cap as well as all changed bindings.
            // The profile records stay leased; no stale resource is copied.
            native.set32(original.identity.impl + PHBattleHPOffset, original.hp);
            uint32_t hpShadow = original.hp ^ 0x8085a6beu;
            RequireWrite(original.identity.impl + PHBattleHPShadowOffset, &hpShadow, sizeof(hpShadow));
            native.adjustHP(original.identity.impl + PHBattleHPControllerOffset, 0, 1);
            native.refreshTeam(original.identity.game, 0);
            uint32_t digestShadow = static_cast<uint32_t>(native.digest(original.identity.game)) ^ 0x689d8b5cu;
            RequireWrite(original.identity.impl + PHBattleDigestShadowOffset, &digestShadow, sizeof(digestShadow));
            PHBattleState restored = {};
            if (!ReadLive(&restored, frame, nullptr)) return NO;
            TagProviderMembers(provider, &restored);
            return SameRebindContext(original, restored) && restored.entryChargesValid &&
                restored.hpMatches && restored.digestMatches && restored.hp == original.hp &&
                SameEntryMembers(original, restored, YES) && SameEntryBanks(original, restored);
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    return NO;
}

static PHBattleMutationResult RebindEntryTeam(BattleProvider *provider, uint8_t mask,
    const PHBattleState *expected, PHBattleMember *members, NSString **reason) {
    if (!mask || (mask & ~0x3fu)) return PHBattleMutationRejected;
    PHBattleFrame frame = {expected->identity.header, expected->identity.player, expected->identity.epoch,
                          true, true, expected->policy, false};
    PHBattleState before = {};
    if (!CheckCode(frame.header, reason) || !ReadLive(&before, frame, reason)) return PHBattleMutationRejected;
    TagProviderMembers(provider, &before);
    if (PHBattleStateGate(&before) != PHBattleGateReady || !before.entryChargesValid ||
        !SameRebindContext(*expected, before) || !EntryProviderMatches(provider, &before) ||
        !SameEntryMembers(*expected, before, YES) || !SameEntryBanks(*expected, before) || !EntryViewMatchesBank(before)) {
        if (reason) *reason = @"同步後的隊伍、回合或充能已變更，暫不補回入場設定";
        return PHBattleMutationRejected;
    }
    for (unsigned slot = 0; slot < 6; ++slot) if (mask & (1u << slot)) {
        const PHBattleMember &m = before.members[slot];
        ProviderLease *lease = RebindLease(provider, m);
        if (!lease || m.providerToken || m.temporaryID || m.temporaryTurns ||
            before.turnSerial <= lease->reboundSerial || !ProfileOwned(provider, *lease, frame.header) ||
            ParameterReferenced(&before, lease->parameter + 1)) {
            if (reason) *reason = @"原生編隊已接管、暫時變身中，或本回合已嘗試補回設定";
            return PHBattleMutationRejected;
        }
    }
    uint8_t touched = 0;
    unsigned lastSlot = 6;
    PHBattleState lastBefore = before;
    NSString *failure = nil;
    try {
        @try {
            PHBattleState current = before;
            for (unsigned slot = 0; slot < 6; ++slot) {
                if (!(mask & (1u << slot))) continue;
                const PHBattleMember old = current.members[slot];
                ProviderLease *lease = RebindLease(provider, old);
                if (!lease || !ProfileOwned(provider, *lease, frame.header) ||
                    PHBattleStateGate(&current) != PHBattleGateReady) throw std::runtime_error("Rebind source changed");
                PHBattleMember wanted = old;
                wanted.scriptSlot = static_cast<int8_t>(lease->parameter + 1);
                wanted.baseID = Profile16(lease->installed, 4);
                wanted.assistID = Profile16(lease->installed, 14);
                // Retain synchronized transformations and consumed/delayed
                // charge. Only the new skill's native cap may reduce charge.
                wanted.catalogID = old.formID ? old.formID : wanted.baseID;
                lastBefore = current; lastSlot = slot;
                lease->reboundSerial = before.turnSerial;
                touched |= static_cast<uint8_t>(1u << slot);
                uintptr_t address = before.identity.impl + PHBattleMemberOffset + slot * PHBattleMemberStride;
                Calls(frame.header).setByte(address + PHBattleScriptSlotOffset, static_cast<uint8_t>(wanted.scriptSlot));
                BOOL changed = NO;
                uint16_t charge = 0;
                if (!Perform(&current, slot, wanted.formID, wanted.catalogID, &old, old.charge, YES,
                             &changed, &charge, &failure, &wanted, YES) || !ReadLive(&current, frame, &failure))
                    throw std::runtime_error("Rebind native rebuild failed");
                TagProviderMembers(provider, &current);
                if (!SameRebindContext(before, current) || !current.entryChargesValid ||
                    !current.hpMatches || !current.digestMatches || current.auditFlags != before.auditFlags ||
                    current.members[slot].providerToken != lease->token ||
                    !ProfileMatchesMember(lease->installed, current.members[slot]))
                    throw std::runtime_error("Rebind ownership check failed");
            }
            // Per-member refresh can temporarily cap HP before all six
            // profiles are rebuilt. Preserve the synchronized HP against the
            // final team's effective cap, not those intermediate caps. No
            // game frame or damage processing runs inside this transaction.
            PHBattleState rebuilt = current;
            NativeCalls native = Calls(frame.header);
            uintptr_t controller = current.identity.impl + PHBattleHPControllerOffset;
            uint64_t maximum = native.maximumHP(controller);
            if (!maximum || maximum > INT32_MAX || before.hp > INT32_MAX || current.hp > INT32_MAX)
                throw std::runtime_error("Invalid rebind HP bound");
            uint32_t retainedHP = static_cast<uint32_t>(std::min<uint64_t>(before.hp, maximum));
            if (current.hp != retainedHP) {
                native.adjustHP(controller, static_cast<int32_t>(retainedHP) - static_cast<int32_t>(current.hp), 1);
                native.refreshTeam(current.identity.game, 0);
            }
            if (!ReadLive(&current, frame, &failure)) throw std::runtime_error("Rebind HP unreadable");
            TagProviderMembers(provider, &current);
            if (!SameRebindContext(before, current) || !current.hpMatches || !current.digestMatches ||
                !current.entryChargesValid || current.auditFlags != before.auditFlags || current.hp != retainedHP ||
                native.maximumHP(controller) != maximum ||
                !SameEntryMembers(rebuilt, current, YES) || !StoredTeamsUnchanged(&rebuilt, &current, nullptr, YES, YES) ||
                !SameEntryBanks(before, current, before.playerTeam) || !EntryViewMatchesBank(current))
                throw std::runtime_error("Rebind changed another team");
            std::memcpy(members, current.members, sizeof(current.members));
            return PHBattleMutationApplied;
        } @catch (NSException *exception) { (void)exception; }
    } catch (...) { }
    if (!touched) {
        if (reason) *reason = failure ?: @"同步後的入場設定在寫入前取消";
        return PHBattleMutationRejected;
    }
    BOOL restored = RestoreReboundTeam(provider, before, touched, lastBefore, lastSlot);
    if (reason) *reason = restored ? @"補回設定未通過校驗，已回復同步後的整隊、HP 與充能；自動補回已停止" :
        @"同步後的隊伍未能確認完整回復，已停止後續寫入；請複製診斷";
    return restored ? PHBattleMutationRolledBack : PHBattleMutationUncertain;
}
