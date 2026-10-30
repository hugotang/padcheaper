#import "PHMusic.h"
#import "PHMemory.h"
#import "PHTarget.h"
#include "PHTargetGenerated.h"
#include "PHBattleGenerated.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace {
struct MusicState {
    PHBattleIdentity identity{};
    uintptr_t dungeon = 0, parent = 0;
    uint32_t phase = 0, wave = 0;
    int16_t overrideTrack = -1, target = -1, effectTrack = -1;
    bool present = false, boss = false;
};
struct MusicCalls {
    void (*set)(uintptr_t, int, int);
    uint64_t (*refresh)(uintptr_t);
};
struct MusicTrackCache {
    uintptr_t audio = 0, table = 0;
    std::vector<uintptr_t> records;
    NSString *requested = nil;
    uint16_t track = UINT16_MAX;
};
#ifdef PH_MUSIC_TESTS
static MusicCalls TestCalls;
#endif
MusicCalls Calls(uintptr_t header) {
#ifdef PH_MUSIC_TESTS
    (void)header; return TestCalls;
#else
    return {reinterpret_cast<void (*)(uintptr_t,int,int)>(header + PHMusicSetRVA),
        reinterpret_cast<uint64_t (*)(uintptr_t)>(header + PHMusicRefreshRVA)};
#endif
}
bool Decode(uintptr_t address, uint32_t key1, uint32_t key2, uint32_t &value) {
    uint32_t raw[4];
    return PHReadMemory(address, raw, sizeof(raw)) && PHBattleDecodeProtected(raw, key1, key2, 0x766cfc6du, &value);
}
bool ReadState(PHBattleFrame frame, MusicState &state) {
    state = {};
    if (!frame.ready || !frame.header || frame.header > UINTPTR_MAX - 0x3000000 || !NSThread.isMainThread) return false;
    const uintptr_t save = frame.header + PHSaveRVA, game = frame.header + PHGameMainRVA;
    uint8_t scene = 0, requested = 0; uint32_t player = 0;
    if (!PHReadU8(save, &scene) || !PHReadU8(save + 1, &requested) ||
        !PHReadU32(save + PHPlayerIDOffset, &player) || !player || player != frame.player) return false;
    if (scene != 1) return true;
    uintptr_t object = 0, impl = 0, active = 0;
    if (requested != scene || !PHReadPointer(frame.header + PHSceneTableRVA + 24, &object) || object != game ||
        !PHReadPointer(game + 0x70, &impl) || !impl || (impl & 7u) || impl > UINTPTR_MAX - 0xc4000 ||
        !PHReadPointer(frame.header + PHBattleImplRVA, &active) || active != impl ||
        !PHMemoryHasProtection(impl + 0x92000, 0x2d000, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) return false;
    state.identity = {frame.header, game, impl, player, frame.epoch, 0, 0, {0}, 0, 0, 0};
    uint16_t mode = 0; uint32_t key1 = 0, key2 = 0, bossWave = 0, effect = 0, effectRaw[4];
    if (!PHReadU16(save + PHBattleDungeonOffset, &state.identity.dungeon) || !state.identity.dungeon ||
        !PHReadU16(save + PHBattleDungeonOffset + 2, &state.identity.floor) || !state.identity.floor ||
        !PHReadMemory(frame.header + PHBattleEntryHashRVA, state.identity.entryHash, sizeof(state.identity.entryHash)) ||
        !PHReadU16(frame.header + PHBattleModeRVA, &mode) ||
        !PHReadU16(save + PHBattleSourceModeOffset, &state.identity.sourceMode) ||
        !PHReadU32(frame.header + PHBattleMultiplayerRVA, &state.identity.multiplayer)) return false;
    state.identity.entryMode = int16_t(mode);
    for (uint8_t b : state.identity.entryHash) state.present |= b != 0;
    if (!state.present) return false;
    if (!PHBattleIsSinglePlayer(state.identity.entryMode, state.identity.multiplayer)) return true;
    if (!PHReadPointer(impl + PHBattleDungeonPointerOffset, &state.dungeon) || !state.dungeon ||
        !PHReadPointer(impl + PHBattleDungeonPointerOffset - 8, &state.parent) || !state.parent ||
        !PHMemoryHasProtection(state.dungeon, 128, VM_PROT_READ, 0) ||
        !PHMemoryHasProtection(state.parent, 36, VM_PROT_READ, 0) ||
        !PHReadU32(frame.header + PHBattleKey1RVA, &key1) || !PHReadU32(frame.header + PHBattleKey2RVA, &key2) ||
        !Decode(impl + PHBattlePhaseOffset, key1, key2, state.phase) ||
        !Decode(impl + PHBattleWaveOffset, key1, key2, state.wave) ||
        !PHReadMemory(impl + PHMusicEffectOffset, effectRaw, sizeof(effectRaw)) ||
        !PHBattleDecodeProtected(effectRaw, key1, key2, 0x6a875a8bu, &effect) || effect > UINT16_MAX ||
        !PHReadU32(impl + PHMusicBossWaveOffset, &bossWave) || bossWave > 10000 || state.wave > 10000 ||
        !PHReadMemory(impl + PHMusicOverrideOffset, &state.overrideTrack, sizeof(state.overrideTrack)) ||
        !PHReadMemory(impl + PHMusicTargetOffset, &state.target, sizeof(state.target))) return false;
    state.boss = bossWave <= state.wave;
    state.effectTrack = int16_t(uint16_t(effect));
    return true;
}
NSString *ReadString(uintptr_t address, size_t limit) {
    if (!address || limit > 8192 || address > UINTPTR_MAX - limit) return nil;
    std::array<char,8192> bytes{}; size_t length = 0;
    while (length < limit) {
        size_t count = std::min(size_t(64), limit - length);
        if (!PHReadMemory(address + length, bytes.data() + length, count)) {
            count = 1; if (!PHReadMemory(address + length, bytes.data() + length, count)) return nil;
        }
        const char *end = static_cast<const char *>(std::memchr(bytes.data() + length, 0, count));
        if (end) return [[NSString alloc] initWithBytes:bytes.data() length:size_t(end - bytes.data()) encoding:NSUTF8StringEncoding];
        length += count;
    }
    return nil;
}
bool Cleared(uintptr_t save, uint16_t dungeon, uint16_t floor, bool &cleared) {
    cleared = false; uint16_t next = 0;
    if (!PHReadU16(save + PHMusicClearBucketsOffset + 2u * uint8_t(unsigned(dungeon) * floor), &next)) return false;
    std::array<bool,20480> seen{};
    for (unsigned n = 0; next != 0xffff; ++n) {
        if (next >= seen.size() || n >= seen.size() || seen[next]) return false;
        seen[next] = true;
        uint8_t bytes[12];
        if (!PHReadMemory(save + PHMusicClearRecordsOffset + 12u * next, bytes, sizeof(bytes))) return false;
        uint16_t ids[3]; std::memcpy(ids, bytes, sizeof(ids));
        if (ids[1] == dungeon && ids[2] == floor) { cleared = (bytes[8] & 2u) != 0; return true; }
        next = ids[0];
    }
    return true;
}
bool CanSelect(const MusicState &state, NSString **reason) {
    const uintptr_t save = state.identity.header + PHSaveRVA;
    uint8_t flags = 0, kind = 0; uint16_t dungeon = 0, floor = 0;
    uint32_t rules = 0; bool cleared = false;
    if (state.identity.sourceMode || state.identity.entryMode == 11) { *reason = @"此關卡沿用原生 BGM"; return false; }
    if (!PHReadU8(save + 0x8fb4, &flags) || !PHReadU8(state.parent + 34, &kind) ||
        !PHReadU16(state.parent + 30, &dungeon) || dungeon != state.identity.dungeon ||
        !PHReadU16(state.dungeon + 18, &floor) || floor != state.identity.floor ||
        !Cleared(save, dungeon, floor, cleared) ||
        !PHReadU32(state.dungeon + PHBattleDungeonRulesOffset, &rules)) {
        *reason = @"等待選曲與通關資料就緒"; return false;
    }
    if (cleared || (flags & 4u) || kind == 9) { *reason = @"此關卡沿用原生 BGM"; return false; }
    if (rules) {
        uintptr_t object = 0, base = 0;
        if (!PHReadPointer(save + PHBattleDungeonRulePoolOffset, &object) || !object || object > UINTPTR_MAX - 24 ||
            !PHReadPointer(object + 16, &base) || !base || base > UINTPTR_MAX - rules) return false;
        NSString *text = ReadString(base + rules, 8192);
        if (!text) return false;
        unsigned fixed = 0;
        for (NSString *part in [text.lowercaseString componentsSeparatedByString:@"|"])
            for (unsigned i = 1; i <= 6; ++i)
                if ([part hasPrefix:[NSString stringWithFormat:@"fc%u:",i]]) fixed |= 1u << (i - 1);
        if (fixed == 63) { *reason = @"固定隊伍關卡沿用原生 BGM"; return false; }
    }
    return true;
}
bool ResourceMatches(NSString *name, NSString *resource) {
    // Native 0x100235674 compares the nonempty requested prefix, folding
    // ASCII case. Registered resources include extensions (bgm_123.caf).
    const unsigned char *wanted = reinterpret_cast<const unsigned char *>(name.UTF8String);
    const unsigned char *candidate = reinterpret_cast<const unsigned char *>(resource.UTF8String);
    if (!wanted || !candidate || !*wanted) return false;
    for (; *wanted; ++wanted, ++candidate) {
        const unsigned a = *wanted >= 'a' && *wanted <= 'z' ? *wanted - 'a' + 'A' : *wanted;
        const unsigned b = *candidate >= 'a' && *candidate <= 'z' ? *candidate - 'a' + 'A' : *candidate;
        if (a != b) return false;
    }
    return true;
}
int Track(const MusicState &state, NSString **reason, NSDictionary **diagnostic, MusicTrackCache &cache) {
    const uintptr_t save = state.identity.header + PHSaveRVA;
    uint16_t selected = 0, count = 0, encoded = 0;
    if (diagnostic) *diagnostic = @{};
    if (!PHReadU16(save + PHMusicSelectedDeckOffset, &selected) ||
        !PHReadU16(save + PHMaxDecksOffset, &count) || !count || count > 32 || selected >= count ||
        !PHReadU16(save + PHPrimaryDecksOffset + 96u * selected + (state.boss ? 26u : 24u), &encoded) || !encoded) {
        *reason = state.boss ? @"隊伍未指定頭目 BGM" : @"隊伍未指定道中 BGM"; return -1;
    }
    NSMutableDictionary *details = [@{@"deck":@(selected + 1), @"encoded":@(encoded), @"boss":@(state.boss),
        @"descriptor":@((encoded >> 1) + 10000), @"match":@"ascii-case-insensitive-prefix", @"track":@(-1)} mutableCopy];
    if (diagnostic) *diagnostic = details;
    *reason = @"隊伍 BGM 資源描述資料未就緒";
    uintptr_t entries = 0, strings = 0, base = 0; uint32_t entryCount = 0;
    if (!PHReadU32(save + PHEntryCountOffset, &entryCount) || !entryCount || entryCount > 2048 ||
        !PHReadPointer(save + PHEntryPointerOffset, &entries) || !entries || entries > UINTPTR_MAX - entryCount * 52u ||
        !PHReadPointer(save + PHMusicStringObjectOffset, &strings) || !strings || strings > UINTPTR_MAX - 24 ||
        !PHReadPointer(strings + 16, &base) || !base) return -1;
    std::vector<uint8_t> descriptors(size_t(entryCount) * 52u);
    if (!PHReadMemory(entries,descriptors.data(),descriptors.size())) return -1;
    NSString *name = nil;
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint16_t identifier = 0; const uint8_t *entry = descriptors.data() + 52u * i;
        std::memcpy(&identifier,entry,sizeof(identifier));
        if (identifier != (encoded >> 1) + 10000) continue;
        uint32_t normal = 0, boss = 0;
        std::memcpy(&normal,entry + 28,sizeof(normal)); std::memcpy(&boss,entry + 36,sizeof(boss));
        if (base > UINTPTR_MAX - std::max(normal,boss)) return -1;
        NSString *bossName = ReadString(base + boss, 1024);
        if (!bossName) return -1;
        if ([bossName.uppercaseString isEqual:@"RND"]) { *reason = @"未通關選曲請指定曲目；隨機 BGM 沿用原生處理"; return -1; }
        name = (encoded & 1u) ? bossName : ReadString(base + normal, 1024); break;
    }
    if (!name.length) { *reason = @"隊伍 BGM 未找到有效的曲目名稱"; return -1; }
    details[@"requested"] = name;
    *reason = @"原生 BGM 資源表未就緒";
    uintptr_t audio = 0, table = 0; uint16_t audioCount = 0;
    if (!PHReadPointer(state.identity.header + PHMusicAudioRVA, &audio) || !audio || audio > UINTPTR_MAX - 80 ||
        !PHReadU16(audio + 24, &audioCount) || audioCount > 32767) return -1;
    details[@"resourceCount"] = @(audioCount);
    if (!audioCount ||
        !PHReadPointer(audio + 16, &table) || !table || table > UINTPTR_MAX - size_t(audioCount) * 16) return -1;
    std::vector<uintptr_t> loaded(size_t(audioCount) * 2);
    if (!PHReadMemory(table,loaded.data(),loaded.size() * sizeof(uintptr_t))) return -1;
    uint16_t matched = UINT16_MAX; NSString *registeredName = nil;
    bool immutable = true;
    if (cache.audio == audio && cache.table == table && cache.records == loaded && cache.track < audioCount &&
        [cache.requested isEqual:name]) {
        registeredName = ReadString(loaded[2u * cache.track + 1],1024);
        if (ResourceMatches(name,registeredName)) matched = cache.track;
    }
    if (matched == UINT16_MAX) for (uint16_t i = 0; i < audioCount; ++i) {
        const uintptr_t address = loaded[2u * i + 1];
        NSString *candidate = ReadString(address, 1024);
        // An unreadable earlier entry could be the native first match. Do
        // not skip it and silently choose a later, ambiguous prefix match.
        if (!candidate) { *reason = @"原生 BGM 資源名稱無法校驗"; return -1; }
        // Cache first-match order only for immutable native names. Mutable
        // registries rescan, and a changed table invalidates the whole cache.
        immutable &= PHMemoryHasProtection(address,[candidate lengthOfBytesUsingEncoding:NSUTF8StringEncoding] + 1,
            VM_PROT_READ,VM_PROT_WRITE);
        if (ResourceMatches(name,candidate)) { matched = i; registeredName = candidate; break; }
    }
    uintptr_t lastAudio = 0, lastTable = 0; uint16_t lastCount = 0;
    std::vector<uintptr_t> lastRecords(loaded.size());
    if (!PHReadPointer(state.identity.header + PHMusicAudioRVA,&lastAudio) || lastAudio != audio ||
        !PHReadPointer(audio + 16,&lastTable) || lastTable != table ||
        !PHReadU16(audio + 24,&lastCount) || lastCount != audioCount ||
        !PHReadMemory(table,lastRecords.data(),lastRecords.size() * sizeof(uintptr_t)) || lastRecords != loaded) {
        *reason = @"查找期間 BGM 資源表已改變，等待下一次更新"; return -1;
    }
    if (matched == UINT16_MAX) { *reason = @"選定 BGM 未在原生資源表中找到，沿用原生音樂"; return -1; }
    if (immutable) { cache = {audio,table,std::move(loaded),name,matched}; }
    else cache = {};
    details[@"registered"] = registeredName; details[@"track"] = @(matched);
    return matched;
}
bool SameMusic(const MusicState &a, const MusicState &b) {
    return PHBattleSameIdentity(&a.identity, &b.identity) && a.dungeon == b.dungeon && a.parent == b.parent &&
        a.wave == b.wave && a.boss == b.boss && a.phase == b.phase && a.overrideTrack == b.overrideTrack &&
        a.target == b.target && a.effectTrack == b.effectTrack;
}
bool SetTrack(PHBattleFrame frame, const MusicState &expected, int16_t track, bool restore) {
    MusicState live;
    if (!PHValidateGuest(reinterpret_cast<const mach_header_64 *>(frame.header), nullptr) ||
        !ReadState(frame, live) || !SameMusic(expected,live) || live.phase != 2 ||
        !PHBattleIsSinglePlayer(live.identity.entryMode, live.identity.multiplayer) ||
        !PHMemoryHasProtection(live.identity.impl + PHMusicOverrideOffset, 20, VM_PROT_READ | VM_PROT_WRITE, VM_PROT_EXECUTE)) return false;
    MusicCalls calls = Calls(frame.header);
    if (restore) {
        if (!PHWriteData(live.identity.impl + PHMusicOverrideOffset, &track, sizeof(track))) return false;
        calls.refresh(live.identity.game);
    } else calls.set(live.identity.game, track, -1);
    MusicState after;
    return ReadState(frame, after) && PHBattleSameIdentity(&live.identity,&after.identity) &&
        after.overrideTrack == track && (restore || after.target == track);
}
}

@implementation PHMusic {
    MusicState _last;
    PHBattleFrame _frame;
    BOOL _known, _owned, _suspended, _fault, _multiplayer;
    int16_t _original, _track;
    NSTimeInterval _lastTime;
    NSUInteger _changes;
    NSString *_message;
    NSDictionary *_selection;
    MusicTrackCache _cache;
}
- (instancetype)init { if ((self = [super init])) _message = @"關閉"; return self; }
- (void)onFrame:(PHBattleFrame)frame time:(NSTimeInterval)time {
    NSCAssert(NSThread.isMainThread, @"Music changes require a verified game frame");
    const bool changed = _frame.enabled != frame.enabled || _frame.epoch != frame.epoch;
    _frame = frame;
    if (!frame.ready) { _message = @"等待遊戲校驗與登入"; return; }
    if (!changed && time - _lastTime < 0.25) return;
    _lastTime = time;
    MusicState state;
    if (!ReadState(frame,state)) { _message = @"等待選曲資料就緒"; return; }
    if (!state.present) {
        _known = _owned = _suspended = _fault = _multiplayer = NO;
        _selection = @{}; _cache = {};
        _message = frame.enabled ? @"等待單人未通關關卡" : @"關閉"; return;
    }
    if (!_known || !PHBattleSameIdentity(&_last.identity,&state.identity)) {
        _owned = _suspended = _fault = NO;
        _selection = @{};
        // A selector transition within the same battle cannot re-enable writes.
        if (!_known || _last.identity.epoch != state.identity.epoch || _last.identity.impl != state.identity.impl ||
            std::memcmp(_last.identity.entryHash,state.identity.entryHash,32)) _multiplayer = NO;
    }
    _known = YES; _last = state;
    _multiplayer |= !PHBattleIsSinglePlayer(state.identity.entryMode,state.identity.multiplayer);
    if (_multiplayer) { _owned = NO; _message = @"多人關卡不修改 BGM"; return; }
    if (_fault) { _message = @"選曲未能確認完成，本場停止修改"; return; }
    if (_owned && (state.overrideTrack != _track || state.target != _track)) {
        _owned = NO; _suspended = YES;
    }
    if (_suspended || (!_owned && state.overrideTrack >= 0)) {
        _message = @"遊戲劇情已指定 BGM，保留原生選曲"; return;
    }
    if (state.phase != 2) { _message = @"等待操作階段更新 BGM"; return; }
    NSString *reason = @"等待曲目資料就緒";
    const bool effect = state.effectTrack >= 0;
    NSDictionary *selection = nil;
    int wanted = frame.enabled && !effect && CanSelect(state,&reason) ? Track(state,&reason,&selection,_cache) : -1;
    if (selection) _selection = selection;
    if (effect) reason = @"關卡事件已指定 BGM，保留原生選曲";
    if (!frame.enabled) reason = @"關閉";
    if (wanted < 0 && !_owned) { _message = reason; return; }
    if (wanted == _track && _owned) { _message = state.boss ? @"未通關：使用隊伍頭目 BGM" : @"未通關：使用隊伍道中 BGM"; return; }
    const BOOL restore = wanted < 0;
    if (!_owned) _original = state.overrideTrack;
    try {
        @try {
            if (!SetTrack(frame,state,restore ? _original : int16_t(wanted),restore)) { _fault = YES; return; }
            _owned = !restore; _track = int16_t(wanted); ++_changes;
            if (restore && effect) _suspended = YES;
            _message = restore ? reason : state.boss ? @"未通關：使用隊伍頭目 BGM" : @"未通關：使用隊伍道中 BGM";
        } @catch (NSException *exception) { (void)exception; _fault = YES; }
    } catch (...) { _fault = YES; }
}
- (NSDictionary *)snapshot {
    return @{@"enabled":@(_frame.enabled), @"active":@(_owned && !_fault), @"fault":@(_fault),
        @"changes":@(_changes), @"message":_message ?: @"", @"selection":_selection ?: @{}, @"deviceVerified":@NO};
}
@end
