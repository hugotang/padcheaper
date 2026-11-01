// Optional diagnostics, deliberately outside every input/skill/policy gate.
// No native getter, selector, RNG, action, or cache initializer is invoked.
// Included inside PHOrbNative's anonymous namespace.
namespace EA = PHEnemyActions;

struct EnemyActionTables {
    uintptr_t catalog = 0, actions = 0;
    uint32_t catalogCount = 0, catalogRecords = 0, actionCount = 0;
    uint32_t metadata[4]{}; // Native format/version/count/checksum, not validated content provenance.
};
struct EnemyActionEntry {
    uintptr_t parameter = 0; // Private double-read identity, never exported.
    uint32_t catalogID = 0, catalogIndex = 0, aiFlags = 0, aiCounter = 0, actionCount = 0, compound = 0;
    uint32_t attackAddition = 0, overrideState = 0;
    uint64_t hp = 0, maximumHP = 0, attack = 0;
    int32_t countdown = 0, selected = 0, pending = 0, special = 0;
    float enrage = 0;
    uint16_t flags = 0, level = 0, aiBudget = 0;
    int16_t enrageTurns = 0, overrideAction = 0;
    uint8_t active[2]{};
    int8_t actionState = 0, attribute = 0;
    uint8_t definitionHeader[16]{}; // Native parameter+220..235, unmodified.
    uint8_t rows[EA::Rows * 8]{};
    bool memoryKnown = false, identityKnown = false, stateKnown = false, rowsKnown = false;
};
struct EnemyActionCapture {
    EnemyActionTables tables{};
    EnemyActionEntry enemies[EA::MaxEnemies]{};
    EA::Graph graph{};
    uint32_t enemyCount = 0;
    unsigned readBytes = 0, readCalls = 0;
    bool readBudgetExceeded = false;
};
static_assert(std::is_trivially_copyable<EnemyActionCapture>::value, "double-read captures must remain plain values");
static_assert(EA::RecordBytes == 72 && sizeof(EA::Record) == 72, "native action stride");

bool CheckEnemyActionCode(uintptr_t header) {
    for (const auto &guard : PHOrbEnemyActionsTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return true;
}

bool EnemyActionRead(EnemyActionCapture &capture, uintptr_t address, void *out, size_t bytes) {
    if (bytes > EA::MaxReadBytes - capture.readBytes || capture.readCalls == EA::MaxReads) {
        capture.readBudgetExceeded = true; return false;
    }
    capture.readBytes += unsigned(bytes); ++capture.readCalls;
    return PHMemoryHasProtection(address, bytes, VM_PROT_READ, VM_PROT_EXECUTE) && PHReadMemory(address, out, bytes);
}

bool ReadEnemyActionTables(uintptr_t header, EnemyActionTables &out) {
    const uintptr_t save = header + PHSaveRVA;
    return PHReadPointer(save + PHBattleCatalogPointerOffset, &out.catalog) && out.catalog && !(out.catalog & 7u) &&
        PHReadU32(save + PHBattleCatalogCountOffset, &out.catalogCount) && out.catalogCount && out.catalogCount <= 65536 &&
        PHReadU32(save + PHOrbLeaderCatalogRecordsOffset, &out.catalogRecords) &&
        out.catalogRecords >= out.catalogCount && out.catalogRecords <= 0x20000 &&
        out.catalog <= UINTPTR_MAX - size_t(out.catalogRecords) * PHBattleCatalogStride &&
        PHReadMemory(header + PHOrbEnemyActionsMetadataRVA, out.metadata, sizeof(out.metadata)) &&
        PHReadU32(header + PHOrbEnemyActionsCountRVA, &out.actionCount) && out.actionCount == out.metadata[2] &&
        out.actionCount && out.actionCount <= 0x20000 &&
        PHReadPointer(header + PHOrbEnemyActionsPointerRVA, &out.actions) && out.actions && !(out.actions & 3u) &&
        out.actions <= UINTPTR_MAX - size_t(out.actionCount) * EA::RecordBytes;
}

bool EnemyActionRecord(void *context, uint32_t id, EA::Record &record) {
    auto &capture = *static_cast<EnemyActionCapture *>(context);
    const auto &tables = capture.tables;
    return id < tables.actionCount && EnemyActionRead(capture, tables.actions + size_t(id) * EA::RecordBytes,
        record.bytes.data(), record.bytes.size());
}

bool EnemyActionParameter(EnemyActionCapture &capture, EnemyActionEntry &enemy,
    std::array<uint8_t, 840> &bytes) {
    const auto &tables = capture.tables;
    const uint32_t id = enemy.catalogID;
    if (!id || id > INT32_MAX || id == 10000000 || (id >= tables.catalogCount && id <= 100000)) return false;
    uint32_t index = id < tables.catalogCount ? id : id % 100000u;
    uint32_t visited[128]{};
    for (unsigned hop = 0; hop < 128; ++hop) {
        if (!index || index >= tables.catalogRecords) return false;
        for (unsigned i = 0; i < hop; ++i) if (visited[i] == index) return false;
        visited[hop] = index;
        const uintptr_t address = tables.catalog + size_t(index) * PHBattleCatalogStride;
        if (!EnemyActionRead(capture, address, bytes.data(), bytes.size())) return false;
        if (Load<uint32_t>(bytes.data()) == id) {
            if (address != enemy.parameter) return false;
            enemy.catalogIndex = index;
            return true;
        }
        if (id < tables.catalogCount) return false;
        index = Load<uint32_t>(bytes.data() + PHOrbLeaderCatalogNextOffset);
    }
    return false;
}

bool EnemyActionProtected(const uint8_t *bytes, unsigned offset, uint32_t key1, uint32_t key2,
    uint32_t salt, uint32_t &out) {
    uint32_t raw[4]; std::memcpy(raw, bytes + offset, sizeof(raw));
    return PHBattleDecodeProtected(raw, key1, key2, salt, &out);
}
bool EnemyActionWide(const uint8_t *bytes, unsigned offset, uint32_t key1, uint32_t key2, uint64_t &out) {
    uint32_t low = 0, high = 0;
    if (!EnemyActionProtected(bytes, offset, key1, key2, 0x766cfc6du, low) ||
        !EnemyActionProtected(bytes, offset + 16, key1, key2, 0x766cfc6du, high) || (high & 0x80000000u)) return false;
    out = (uint64_t(high) << 32) | low;
    return true;
}
bool EnemyActionShort(const uint8_t *bytes, unsigned offset, uint32_t key1, uint32_t key2, int16_t &out) {
    uint32_t raw = 0;
    if (!EnemyActionProtected(bytes, offset, key1, key2, 0x6a875a8bu, raw)) return false;
    out = int16_t(raw);
    return raw == uint16_t(out) || raw == uint32_t(int32_t(out));
}

bool CollectEnemyActions(const PHOrbSnapshot &state, uint32_t key1, uint32_t key2, EnemyActionCapture &out) {
    // All members are trivially copyable. Zero padding before the bounded
    // comparison; keep guest pointers only inside this temporary capture.
    std::memset(&out, 0, sizeof(out));
    if (!ReadEnemyActionTables(state.identity.header, out.tables) ||
        !Decode(state.identity.impl + PHOrbEnemyCountOffset, key1, key2, 0x766cfc6du, out.enemyCount) ||
        out.enemyCount > EA::MaxEnemies) return false;
    for (unsigned slot = 0; slot < out.enemyCount; ++slot) {
        auto &enemy = out.enemies[slot];
        const uintptr_t address = state.identity.impl + PHOrbEnemyOffset + slot * PHOrbEnemyStride;
        std::array<uint8_t, 2984> memory{};
        if (!EnemyActionRead(out, address, memory.data(), memory.size())) continue;
        const uint8_t *p = memory.data();
        enemy.memoryKnown = true;
        enemy.flags = Load<uint16_t>(p + 532);
        enemy.active[0] = p[0]; enemy.active[1] = p[1]; enemy.actionState = int8_t(p[23]);
        if (enemy.flags & 0x100u) continue; // Parts need their own state model.
        enemy.parameter = Load<uintptr_t>(p + PHOrbEnemyActionsParameterOffset);
        enemy.catalogID = Load<uint32_t>(p + PHOrbEnemyActionsCatalogIDOffset);
        std::array<uint8_t, 840> parameter{};
        enemy.identityKnown = EnemyActionParameter(out, enemy, parameter);
        if (enemy.identityKnown) {
            std::memcpy(enemy.definitionHeader, parameter.data() + 220, sizeof(enemy.definitionHeader));
            std::memcpy(enemy.rows, parameter.data() + PHOrbEnemyActionsRowsOffset, sizeof(enemy.rows));
            enemy.rowsKnown = true;
        }
        enemy.attribute = int8_t(p[575]);
        if (enemy.attribute < 0 && enemy.identityKnown) enemy.attribute = int8_t(parameter[12]);
        enemy.level = Load<uint16_t>(p + 312);
        enemy.selected = Load<int32_t>(p + 1680); enemy.pending = Load<int32_t>(p + 2008);
        enemy.special = Load<int32_t>(p + 284); enemy.compound = Load<uint32_t>(p + 2012);
        enemy.aiFlags = Load<uint32_t>(p + 1720); enemy.aiCounter = Load<uint32_t>(p + 1724);
        enemy.actionCount = Load<uint32_t>(p + 1728); enemy.aiBudget = Load<uint16_t>(p + 2004);
        uint32_t countdown = 0, enrage = 0;
        enemy.stateKnown = EnemyActionWide(p, 64, key1, key2, enemy.hp) &&
            EnemyActionWide(p, 592, key1, key2, enemy.maximumHP) &&
            EnemyActionWide(p, 624, key1, key2, enemy.attack) &&
            EnemyActionProtected(p, 296, key1, key2, 0x766cfc6du, countdown) &&
            EnemyActionProtected(p, 2032, key1, key2, 0x6a875a8bu, enemy.attackAddition) &&
            EnemyActionProtected(p, 2128, key1, key2, 0x6a875a8bu, enrage) &&
            EnemyActionShort(p, 2144, key1, key2, enemy.enrageTurns) &&
            EnemyActionProtected(p, 2336, key1, key2, 0x6a875a8bu, enemy.overrideState) &&
            EnemyActionShort(p, 2352, key1, key2, enemy.overrideAction);
        enemy.countdown = int32_t(countdown);
        std::memcpy(&enemy.enrage, &enrage, sizeof(enrage));
        enemy.stateKnown &= enemy.maximumHP > 0 && enemy.hp <= enemy.maximumHP &&
            std::isfinite(enemy.enrage) && enemy.enrage >= 0 && enemy.attribute >= 0 && enemy.attribute <= 5;
        if (enemy.rowsKnown) for (unsigned row = 0; row < EA::Rows; ++row) {
            const int32_t id = Load<int32_t>(enemy.rows + row * 8);
            if (id) EA::Add(out.graph, id, out.tables.actionCount, EnemyActionRecord, &out);
        }
        // These IDs describe cached state, not a promise about the next turn.
        // Keep positive references even when they are absent from the row list.
        if (enemy.stateKnown) {
            for (int32_t id : {enemy.selected, enemy.pending, enemy.special})
                if (id > 0) EA::Add(out.graph, id, out.tables.actionCount, EnemyActionRecord, &out);
            if (enemy.compound >> 9) EA::Add(out.graph, int32_t(enemy.compound >> 9), out.tables.actionCount, EnemyActionRecord, &out);
            if (enemy.overrideState) EA::Add(out.graph, enemy.overrideAction, out.tables.actionCount, EnemyActionRecord, &out);
        }
    }
    return true;
}

bool EnemyActionsMatchObservation(const PHOrbBattleObservation &observation, const EnemyActionCapture &capture) {
    if (observation.status != PHOrbObservationReady) return true;
    if (observation.enemyCount != capture.enemyCount) return false;
    for (unsigned i = 0; i < capture.enemyCount; ++i) {
        const auto &earlier = observation.enemies[i]; const auto &now = capture.enemies[i];
        if (earlier.hpKnown && (!now.stateKnown || earlier.hp != now.hp)) return false;
        if (now.memoryKnown && earlier.parts != ((now.flags & 0x100u) != 0)) return false;
    }
    return true;
}

NSArray *EnemyActionIssues(uint32_t issues) {
    NSMutableArray *list = [NSMutableArray array];
    const char *names[]{"invalid-action-id", "unreadable-record", "record-budget", "depth-budget", "compound-cycle",
        "nested-compound", "unknown-opcode", "unterminated-compound"};
    for (unsigned i = 0; i < 8; ++i) if (issues & (1u << i)) [list addObject:[NSString stringWithUTF8String:names[i]]];
    return list;
}
NSString *EnemyActionHex(const void *bytes, size_t size) {
    const uint8_t *p = static_cast<const uint8_t *>(bytes);
    NSMutableData *data = [NSMutableData dataWithLength:size * 2];
    char *text = static_cast<char *>(data.mutableBytes);
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < size; ++i) { text[2 * i] = digits[p[i] >> 4]; text[2 * i + 1] = digits[p[i] & 15u]; }
    return [[NSString alloc] initWithBytes:text length:size * 2 encoding:NSASCIIStringEncoding];
}
NSString *EnemyActionDigest(const void *bytes, size_t size) {
    uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(bytes, CC_LONG(size), digest);
    return EnemyActionHex(digest, sizeof(digest));
}
NSMutableDictionary *EnemyActionStatus(NSString *status) {
    return [@{@"status":status, @"diagnosticOnly":@YES, @"parserVersion":@(EA::ParserVersion),
        @"target":@"jp.gungho.pad-ios-23.6.0-106878", @"enemyForecastKnown":@NO,
        @"rawCaptureComplete":@NO, @"definitionCaptureComplete":@NO, @"damageBoundKnown":@NO, @"survivalKnown":@NO,
        @"structureSupported":@NO, @"actionReachabilityKnown":@NO,
        @"spawnIdentityKnown":@NO, @"nextFloorKnown":@NO,
        @"unsupported":@[@"reachable-next-actions", @"post-player-attack-hp", @"spawn-generation",
            @"countdown-timing", @"effective-shield-lifetime", @"healing-order", @"future-skill-deadlines"]} mutableCopy];
}

NSDictionary *EnemyActionTrace(const EnemyActionCapture &capture) {
    NSMutableDictionary *out = EnemyActionStatus(@"ready");
    NSMutableArray *enemies = [NSMutableArray array], *definitions = [NSMutableArray array];
    bool complete = !(capture.graph.issues & EA::CaptureIssues) && !capture.readBudgetExceeded;
    bool supported = !capture.graph.issues;
    for (unsigned slot = 0; slot < capture.enemyCount; ++slot) {
        const auto &enemy = capture.enemies[slot];
        const bool parts = enemy.memoryKnown && (enemy.flags & 0x100u);
        complete &= enemy.rowsKnown && enemy.stateKnown && !parts;
        NSMutableDictionary *entry = [@{@"slot":@(slot + 1), @"memoryKnown":@(enemy.memoryKnown),
            @"identityKnown":@(enemy.identityKnown), @"stateKnown":@(enemy.stateKnown), @"rowsKnown":@(enemy.rowsKnown),
            @"parts":enemy.memoryKnown ? @(parts) : NSNull.null} mutableCopy];
        if (enemy.memoryKnown) {
            entry[@"nativeFlags"] = @(enemy.flags);
            entry[@"activeFlagsRaw"] = @[@(enemy.active[0]), @(enemy.active[1])];
            entry[@"actionStateRaw"] = @(enemy.actionState);
        }
        if (enemy.identityKnown) [entry addEntriesFromDictionary:@{@"catalogID":@(enemy.catalogID), @"catalogIndex":@(enemy.catalogIndex)}];
        if (enemy.stateKnown) [entry addEntriesFromDictionary:@{@"hp":@(enemy.hp), @"maximumHP":@(enemy.maximumHP),
            @"baseAttack":@(enemy.attack), @"attackAttribute":@(enemy.attribute), @"countdownRaw":@(enemy.countdown),
            @"level":@(enemy.level), @"aiFlags":@(enemy.aiFlags), @"aiCounter":@(enemy.aiCounter),
            @"actionCount":@(enemy.actionCount), @"aiBudget":@(enemy.aiBudget),
            @"selectedActionCached":@(enemy.selected), @"pendingActionCached":@(enemy.pending),
            @"specialActionCached":@(enemy.special), @"compoundStateRaw":@(enemy.compound),
            @"compoundRootCached":@(enemy.compound >> 9), @"compoundChildIndexCached":@(enemy.compound & 15u),
            @"compoundContinuingCached":@((enemy.compound & 0x100u) != 0),
            @"enrageMultiplierCurrent":@(enemy.enrage), @"enrageTurnsRaw":@(enemy.enrageTurns),
            @"attackAdditionRaw":@(enemy.attackAddition), @"overrideStateRaw":@(enemy.overrideState),
            @"overrideActionCached":@(enemy.overrideAction)}];
        if (enemy.rowsKnown) {
            uint8_t definition[16 + EA::Rows * 8];
            std::memcpy(definition, enemy.definitionHeader, 16); std::memcpy(definition + 16, enemy.rows, sizeof(enemy.rows));
            entry[@"definitionSHA256"] = EnemyActionDigest(definition, sizeof(definition));
            entry[@"definitionHeaderHex"] = EnemyActionHex(enemy.definitionHeader, sizeof(enemy.definitionHeader));
            entry[@"rowsRawHex"] = EnemyActionHex(enemy.rows, sizeof(enemy.rows));
            entry[@"selectorFlags"] = @(Load<uint16_t>(enemy.definitionHeader + 4));
            entry[@"rowsTotal"] = @(EA::Rows);
            NSMutableArray *rows = [NSMutableArray array];
            for (unsigned row = 0; row < EA::Rows; ++row) {
                const uint8_t *p = enemy.rows + row * 8;
                const int32_t id = Load<int32_t>(p);
                if (!id) continue; // Empty rows still contribute to definitionSHA256.
                const auto *node = capture.graph.find(id);
                NSMutableDictionary *record = [@{@"row":@(row + 1), @"actionID":@(id),
                    @"operandsRaw":@[@(p[4]), @(p[5]), @(p[6]), @(p[7])],
                    @"definitionKnown":@(node && node->known)} mutableCopy];
                if (node && node->known) {
                    const auto control = EA::DescribeControl(node->record.opcode(), p[5]);
                    if (control.name) {
                        record[@"control"] = [NSString stringWithUTF8String:control.name];
                        if (control.jumps) {
                            const bool valid = control.target >= 0 && control.target < int(EA::Rows);
                            record[@"jumpTargetValid"] = @(valid);
                            record[@"jumpTargetRow"] = valid ? @(control.target + 1) : NSNull.null;
                            supported &= valid;
                        }
                    }
                }
                [rows addObject:record];
            }
            entry[@"rows"] = rows;
        }
        [enemies addObject:entry];
    }
    for (unsigned index = 0; index < capture.graph.count; ++index) {
        const auto &node = capture.graph.nodes[index];
        NSMutableDictionary *entry = [@{@"actionID":@(node.id), @"known":@(node.known),
            @"issues":EnemyActionIssues(node.issues)} mutableCopy];
        if (node.known) {
            const auto &record = node.record;
            NSMutableArray *parameters = [NSMutableArray array], *children = [NSMutableArray array];
            for (unsigned i = 0; i < 8; ++i) [parameters addObject:@(record.parameter(i))];
            if (record.opcode() == 83) for (unsigned i = 0; i < 8 && record.parameter(i) > 0; ++i) [children addObject:@(record.parameter(i))];
            [entry addEntriesFromDictionary:@{@"opcode":@(record.opcode()),
                @"kind":[NSString stringWithUTF8String:EA::Kind(record.opcode())], @"parameters":parameters,
                @"recordIDRaw":@(record.word(0)),
                @"rawHex":EnemyActionHex(record.bytes.data(), record.bytes.size()),
                @"sha256":EnemyActionDigest(record.bytes.data(), record.bytes.size()),
                @"weightsRaw":@[@(record.word(48)), @(record.word(52))], @"hpThresholdRaw":@(record.word(56)),
                @"heuristicRaw":@(record.word(60)), @"aiCostRaw":@(record.word(64)),
                @"attachedAttackPercentRaw":@(record.word(68)),
                @"attachedAttackPrecedesEffect":record.opcode() == 88 || record.opcode() == 141 ? @(record.word(68) > 0) : NSNull.null}];
            if (record.opcode() == 83) entry[@"childrenInOrder"] = children;
        }
        [definitions addObject:entry];
    }
    out[@"rawCaptureComplete"] = @(complete);
    out[@"definitionCaptureComplete"] = @(complete && supported);
    out[@"structureSupported"] = @(complete && supported);
    out[@"actionTableCount"] = @(capture.tables.actionCount); out[@"enemyCount"] = @(capture.enemyCount);
    out[@"actionTableMetadataRaw"] = @[@(capture.tables.metadata[0]), @(capture.tables.metadata[1]),
        @(capture.tables.metadata[2]), @(capture.tables.metadata[3])];
    out[@"nativeTableChecksumVerified"] = @NO;
    out[@"recordLimit"] = @(EA::MaxRecords); out[@"depthLimit"] = @(EA::MaxDepth);
    out[@"readBytes"] = @(capture.readBytes); out[@"readCalls"] = @(capture.readCalls);
    out[@"readByteLimit"] = @(EA::MaxReadBytes); out[@"readCallLimit"] = @(EA::MaxReads);
    out[@"readBudgetExceeded"] = @(capture.readBudgetExceeded);
    out[@"issues"] = EnemyActionIssues(capture.graph.issues);
    out[@"enemies"] = enemies; out[@"definitions"] = definitions;
    return out;
}

NSDictionary *ReadEnemyActions(const PHOrbSnapshot &state) {
    if (state.multiplayerDisabled || state.multiplayer.kind != PHOrbMultiplayerNone ||
        !PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer)) return EnemyActionStatus(@"multiplayer");
    if (!state.valid || !state.present || state.identity.sourceMode != 0) return EnemyActionStatus(@"unavailable");
    if (state.phase != 2 || !state.settled || state.boardTransition || state.dragged || !PHInputIsQuiet(&state.input))
        return EnemyActionStatus(@"transition");
    PHOrbSnapshot first = state, second = state;
    first.automation = {}; first.automation.window = true;
    second.automation = {}; second.automation.window = true;
    const uintptr_t header = state.identity.header, impl = state.identity.impl;
    uint32_t key1 = 0, key2 = 0, lastKey1 = 0, lastKey2 = 0, phase = 0, wave = 0, moves = 0;
    uintptr_t dragged = 0;
    if (!CheckEnemyActionCode(header) || !ConfirmOrbEntryIdentity(state.identity) ||
        !ReadAutomationFields(first) || first.automation.busy ||
        !PHReadU32(header + PHBattleKey1RVA, &key1) || !PHReadU32(header + PHBattleKey2RVA, &key2)) return EnemyActionStatus(@"unavailable");
    if ((state.automation.valid && !PHOrbSameAutomationExceptWindow(&state, &first)) ||
        (state.observation.status == PHOrbObservationReady && state.observation.hpKnown &&
         (!first.automation.hpKnown || state.observation.hp != first.automation.hp ||
          state.observation.maximumHP != first.automation.maximumHP))) return EnemyActionStatus(@"changed");
    EnemyActionCapture capture{}, confirmation{};
    EnemyActionTables finalTables{};
    if (!CollectEnemyActions(first, key1, key2, capture)) return EnemyActionStatus(@"unavailable");
    if (!EnemyActionsMatchObservation(state.observation, capture)) return EnemyActionStatus(@"changed");
    if (!ReadAutomationFields(second) || second.automation.busy || !CollectEnemyActions(second, key1, key2, confirmation) ||
        std::memcmp(&capture, &confirmation, sizeof(capture)) || !PHOrbSameAutomation(&first, &second) ||
        !ReadEnemyActionTables(header, finalTables) || finalTables.catalog != capture.tables.catalog ||
        finalTables.actions != capture.tables.actions || finalTables.catalogCount != capture.tables.catalogCount ||
        finalTables.catalogRecords != capture.tables.catalogRecords || finalTables.actionCount != capture.tables.actionCount ||
        std::memcmp(finalTables.metadata, capture.tables.metadata, sizeof(finalTables.metadata)) ||
        !ConfirmOrbEntryIdentity(state.identity) ||
        !PHReadU32(header + PHBattleKey1RVA, &lastKey1) || key1 != lastKey1 ||
        !PHReadU32(header + PHBattleKey2RVA, &lastKey2) || key2 != lastKey2 ||
        !Decode(impl + PHBattlePhaseOffset, key1, key2, 0x766cfc6du, phase) || phase != state.phase ||
        !Decode(impl + PHBattleWaveOffset, key1, key2, 0x766cfc6du, wave) || wave != state.wave ||
        !PHReadU32(impl + PHOrbMoveSerialOffset, &moves) || moves != state.moves ||
        !PHReadPointer(impl + PHBattleDragPointerOffset, &dragged) || dragged) return EnemyActionStatus(@"changed");
    PHInputState input{};
    if (!PHReadGameInput(header, &input) || !PHInputIsQuiet(&input)) return EnemyActionStatus(@"changed");
    PHBattleFrame frame{};
    frame.header = header; frame.player = state.identity.player; frame.epoch = state.identity.epoch;
    frame.ready = frame.enabled = true;
    PHOrbSnapshot final{}; bool entryConfirmed = false;
    if (!Read(final, frame, nullptr, PHOrbMultiplayerNone, false, &state.identity, entryConfirmed) || !entryConfirmed ||
        !final.valid || !final.settled || final.boardTransition || final.dragged || !PHInputIsQuiet(&final.input) ||
        !PHOrbSameTurn(&state, &final) || state.moves != final.moves || final.phase != state.phase ||
        !PHOrbSameLeaders(&state, &final) || !PHOrbSamePlanBoard(&state.board, &final.board)) return EnemyActionStatus(@"changed");
    return EnemyActionTrace(capture);
}
