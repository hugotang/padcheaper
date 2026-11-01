// One bounded floor per call. This is a file-export source, never AI or input
// evidence. The complete loaded spawn table is checked on both sides of it.
constexpr unsigned DungeonDumpMaxWaves = 100, DungeonDumpWaveBytes = 264, DungeonDumpSpawnBytes = 32;
struct DungeonDumpCapture {
    int16_t waveCount = 0;
    uint8_t waves[DungeonDumpMaxWaves * DungeonDumpWaveBytes]{};
    uint8_t catalogPrefixes[8][220]{};
    EnemyActionCapture actions{};
};
bool CheckDungeonDumpCode(uintptr_t header) {
    for (const auto &guard : PHOrbDungeonDumpTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header + guard.rva, reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header + guard.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, guard.bytes, sizeof(bytes))) return false;
    }
    return CheckEnemyActionCode(header);
}
NSDictionary *DungeonDumpStatus(NSString *status) {
    return @{@"status":status, @"allFloorsCaptured":@NO, @"allDefinitionsCaptured":@NO,
        @"enemyForecastKnown":@NO, @"survivalKnown":@NO, @"diagnosticOnly":@YES};
}
bool CollectDungeonDump(const PHOrbSnapshot &state, unsigned requested, DungeonDumpCapture &out) {
    std::memset(&out,0,sizeof(out));
    uint16_t count = 0;
    const uintptr_t header = state.identity.header;
    if (!PHReadU16(header + PHOrbDungeonDumpCountAddress, &count) || int16_t(count) < 1 || count > DungeonDumpMaxWaves ||
        state.wave >= count || requested >= count ||
        !PHMemoryHasProtection(header + PHOrbDungeonDumpWavesAddress, count * DungeonDumpWaveBytes, VM_PROT_READ, VM_PROT_EXECUTE) ||
        !PHReadMemory(header + PHOrbDungeonDumpWavesAddress, out.waves, count * DungeonDumpWaveBytes) ||
        !ReadEnemyActionTables(header,out.actions.tables)) return false;
    out.waveCount = int16_t(count);
    // A populated global table alone might belong to an earlier entry. The
    // current native enemies retain their exact spawn-record pointer and wave.
    const int currentCount = Load<int16_t>(out.waves + state.wave * DungeonDumpWaveBytes + 2);
    uint32_t key1 = 0, key2 = 0, liveCount = 0;
    if (currentCount < 1 || currentCount > 8 ||
        !PHReadU32(header + PHBattleKey1RVA,&key1) || !PHReadU32(header + PHBattleKey2RVA,&key2) ||
        !Decode(state.identity.impl + PHOrbEnemyCountOffset,key1,key2,0x766cfc6du,liveCount) ||
        liveCount < unsigned(currentCount) || liveCount > 8) return false;
    for (unsigned slot = 0; slot < unsigned(currentCount); ++slot) {
        const uintptr_t enemy = state.identity.impl + PHOrbEnemyOffset + slot * PHOrbEnemyStride;
        uintptr_t spawn = 0; uint16_t wave = 0, flags = 0;
        if (!PHReadPointer(enemy + PHOrbDungeonDumpSpawnPointerOffset,&spawn) ||
            spawn != header + PHOrbDungeonDumpWavesAddress + state.wave * DungeonDumpWaveBytes + 8 + slot * DungeonDumpSpawnBytes ||
            !PHReadU16(enemy + PHOrbDungeonDumpSpawnWaveOffset,&wave) || wave != state.wave ||
            !PHReadU16(enemy + 532,&flags) || (flags & 0x100u)) return false;
    }
    const uint8_t *wave = out.waves + requested * DungeonDumpWaveBytes;
    const int enemies = Load<int16_t>(wave + 2);
    if (enemies < 0 || enemies > 8) return true; // Export raw invalid count as incomplete, never index beyond it.
    auto &capture = out.actions;
    capture.enemyCount = unsigned(enemies);
    for (unsigned slot = 0; slot < capture.enemyCount; ++slot) {
        auto &entry = capture.enemies[slot];
        const uint8_t *spawn = wave + 8 + slot * DungeonDumpSpawnBytes;
        entry.catalogID = Load<uint32_t>(spawn + 4); entry.level = Load<uint16_t>(spawn + 8);
        std::array<uint8_t,840> parameter{};
        entry.identityKnown = EnemyActionParameter(capture,entry,parameter,false);
        if (!entry.identityKnown) continue;
        std::memcpy(out.catalogPrefixes[slot],parameter.data(),220);
        std::memcpy(entry.definitionHeader,parameter.data() + 220,sizeof(entry.definitionHeader));
        std::memcpy(entry.rows,parameter.data() + PHOrbEnemyActionsRowsOffset,sizeof(entry.rows));
        entry.rowsKnown = true;
        for (unsigned row = 0; row < EA::Rows; ++row) {
            const int32_t id = Load<int32_t>(entry.rows + 8 * row);
            if (id) EA::Add(capture.graph,id,capture.tables.actionCount,EnemyActionRecord,&capture);
        }
    }
    return true;
}
NSDictionary *DungeonDumpTrace(const DungeonDumpCapture &capture, unsigned requested) {
    NSMutableArray *floors = [NSMutableArray array], *catalogs = [NSMutableArray array], *definitions = [NSMutableArray array];
    for (unsigned index = 0; index < unsigned(capture.waveCount); ++index) {
        const uint8_t *wave = capture.waves + index * DungeonDumpWaveBytes;
        const int count = Load<int16_t>(wave + 2);
        NSMutableArray *spawns = [NSMutableArray array];
        for (unsigned slot = 0; count >= 0 && count <= 8 && slot < unsigned(count); ++slot) {
            const uint8_t *spawn = wave + 8 + slot * DungeonDumpSpawnBytes;
            [spawns addObject:@{@"slot":@(slot + 1), @"catalogID":@(Load<uint32_t>(spawn + 4)),
                @"level":@(Load<uint16_t>(spawn + 8)), @"rawHex":EnemyActionHex(spawn,DungeonDumpSpawnBytes)}];
        }
        [floors addObject:@{@"wave":@(index), @"floorNumber":@(index + 1), @"enemyCountRaw":@(count),
            @"layoutKnown":@(count >= 0 && count <= 8), @"spawns":spawns,
            @"rawHex":EnemyActionHex(wave,DungeonDumpWaveBytes)}];
    }
    const auto &actions = capture.actions;
    const int count = Load<int16_t>(capture.waves + requested * DungeonDumpWaveBytes + 2);
    bool complete = count >= 0 && count <= 8 && !actions.readBudgetExceeded && !(actions.graph.issues & EA::CaptureIssues);
    for (unsigned slot = 0; slot < actions.enemyCount; ++slot) {
        const auto &enemy = actions.enemies[slot];
        complete &= enemy.rowsKnown;
        NSMutableDictionary *entry = [@{@"slot":@(slot + 1), @"catalogID":@(enemy.catalogID),
            @"level":@(enemy.level), @"definitionKnown":@(enemy.rowsKnown)} mutableCopy];
        if (enemy.rowsKnown) {
            uint8_t raw[16 + EA::Rows * 8];
            std::memcpy(raw,enemy.definitionHeader,16); std::memcpy(raw + 16,enemy.rows,sizeof(enemy.rows));
            entry[@"catalogIndex"] = @(enemy.catalogIndex);
            entry[@"catalogPrefixRawHex"] = EnemyActionHex(capture.catalogPrefixes[slot],220);
            entry[@"definitionHeaderHex"] = EnemyActionHex(enemy.definitionHeader,16);
            entry[@"rowsRawHex"] = EnemyActionHex(enemy.rows,sizeof(enemy.rows));
            entry[@"definitionSHA256"] = EnemyActionDigest(raw,sizeof(raw));
            NSMutableArray *rows = [NSMutableArray array];
            for (unsigned row = 0; row < EA::Rows; ++row) {
                const uint8_t *p = enemy.rows + 8 * row;
                const int32_t id = Load<int32_t>(p);
                if (id) [rows addObject:@{@"row":@(row + 1), @"actionID":@(id),
                    @"operandsRaw":@[@(p[4]),@(p[5]),@(p[6]),@(p[7])]}];
            }
            entry[@"rows"] = rows;
        }
        [catalogs addObject:entry];
    }
    for (unsigned index = 0; index < actions.graph.count; ++index)
        [definitions addObject:EnemyActionDefinitionTrace(actions.graph.nodes[index])];
    return @{@"status":@"ready", @"diagnosticOnly":@YES, @"scope":@"loaded-entry-spawn-table",
        @"waveCount":@(capture.waveCount), @"waveIndex":@(requested), @"floors":floors,
        @"spawnTableSHA256":EnemyActionDigest(capture.waves,capture.waveCount * DungeonDumpWaveBytes),
        @"actionTableMetadataRaw":@[@(actions.tables.metadata[0]),@(actions.tables.metadata[1]),
            @(actions.tables.metadata[2]),@(actions.tables.metadata[3])],
        @"floorDefinitions":@{@"wave":@(requested), @"catalogs":catalogs, @"definitions":definitions,
            @"rawCaptureComplete":@(complete), @"issues":EnemyActionIssues(actions.graph.issues),
            @"readBudgetExceeded":@(actions.readBudgetExceeded), @"actionReadBytesPerPass":@(actions.readBytes),
            @"actionReadCallsPerPass":@(actions.readCalls), @"spawnTableBytesPerPass":@(capture.waveCount * DungeonDumpWaveBytes)},
        @"randomAlternativesKnown":@NO, @"preemptiveSelectionKnown":@NO, @"enemyForecastKnown":@NO, @"survivalKnown":@NO};
}
NSDictionary *ReadDungeonDump(const PHOrbSnapshot &state, unsigned requested) {
    if (state.multiplayerDisabled || state.multiplayer.kind != PHOrbMultiplayerNone ||
        !PHBattleIsSinglePlayer(state.identity.entryMode,state.identity.multiplayer)) return DungeonDumpStatus(@"multiplayer");
    if (!state.valid || !state.present || state.identity.sourceMode || !state.identity.header ||
        !CheckDungeonDumpCode(state.identity.header)) return DungeonDumpStatus(@"unavailable");
    if (state.phase != 2 || !state.settled || state.boardTransition || state.dragged || !PHInputIsQuiet(&state.input))
        return DungeonDumpStatus(@"transition");
    PHOrbSnapshot first = state, second = state;
    first.automation = {}; first.automation.window = true;
    second.automation = {}; second.automation.window = true;
    auto capture = std::make_unique<DungeonDumpCapture>();
    auto confirmation = std::make_unique<DungeonDumpCapture>();
    if (!ConfirmOrbEntryIdentity(state.identity) || !ReadAutomationFields(first) || first.automation.busy ||
        !CollectDungeonDump(first,requested,*capture)) return DungeonDumpStatus(@"unavailable");
    if (!ReadAutomationFields(second) || second.automation.busy || !PHOrbSameAutomation(&first,&second) ||
        !CollectDungeonDump(second,requested,*confirmation) ||
        std::memcmp(capture.get(),confirmation.get(),sizeof(*capture)) || !ConfirmOrbEntryIdentity(state.identity))
        return DungeonDumpStatus(@"changed");
    PHBattleFrame frame{}; frame.header = state.identity.header; frame.player = state.identity.player;
    frame.epoch = state.identity.epoch; frame.ready = frame.enabled = true;
    PHOrbSnapshot final{}; bool confirmed = false;
    if (!Read(final,frame,nullptr,PHOrbMultiplayerNone,false,&state.identity,confirmed) || !confirmed ||
        !final.valid || !final.settled || final.boardTransition || final.dragged || !PHInputIsQuiet(&final.input) ||
        !PHOrbSameTurn(&state,&final) || state.moves != final.moves || final.phase != state.phase ||
        !PHOrbSameLeaders(&state,&final) || !PHOrbSamePlanBoard(&state.board,&final.board)) return DungeonDumpStatus(@"changed");
    return DungeonDumpTrace(*capture,requested);
}
