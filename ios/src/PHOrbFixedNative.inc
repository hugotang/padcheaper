// Fresh protected evidence for a bound single-player plan. No diagnostic cache.
#include "PHOrbFixedNativeGuards.h"
bool CheckFixedCode(uintptr_t header) {
    for (const auto &g : PHOrbFixedTextGuards) {
        uint8_t bytes[64];
        if (!PHAddressIsGuestCode(header+g.rva,reinterpret_cast<const mach_header_64 *>(header)) ||
            !PHReadMemory(header+g.rva,bytes,sizeof(bytes)) || std::memcmp(bytes,g.bytes,sizeof(bytes))) return false;
    }
    return CheckEnemyActionCode(header) && CheckEnemyPartsCode(header);
}
bool ReadFixedFields(PHOrbSnapshot &state) {
    auto &out = state.fixedPlan; std::memset(&out,0,sizeof(out));
    if (!state.valid || !state.automation.valid || !state.present || state.multiplayerDisabled ||
        state.multiplayer.kind != PHOrbMultiplayerNone || state.identity.sourceMode ||
        !PHBattleIsSinglePlayer(state.identity.entryMode,state.identity.multiplayer)) return false;
    const uintptr_t h=state.identity.header, p=state.identity.impl;
    EnemyActionCapture catalog{}; auto &table=catalog.tables;
    const uintptr_t save=h+PHSaveRVA;
    if (!PHReadPointer(save+PHBattleCatalogPointerOffset,&table.catalog) || !table.catalog || (table.catalog&7u) ||
        !PHReadU32(save+PHBattleCatalogCountOffset,&table.catalogCount) || !table.catalogCount || table.catalogCount>65536 ||
        !PHReadU32(save+PHOrbLeaderCatalogRecordsOffset,&table.catalogRecords) || table.catalogRecords<table.catalogCount ||
        table.catalogRecords>0x20000 || table.catalog>UINTPTR_MAX-size_t(table.catalogRecords)*PHBattleCatalogStride) return false;
    uint32_t k1=0,k2=0,count=0; uint16_t target=0,selection=0;
    uint8_t movement=0,immediate=0,popup=0; uintptr_t overlayA=0,overlayB=0;
    if (!PHReadU32(h+PHBattleKey1RVA,&k1) || !PHReadU32(h+PHBattleKey2RVA,&k2) ||
        !Decode(p+PHOrbEnemyCountOffset,k1,k2,0x766cfc6du,count) || count>8 ||
        !PHReadU16(p+753336,&target) || int16_t(target)<-1 || int16_t(target)>=int(count) ||
        !PHReadU32(p+601584,&out.targetInputMode) || !PHReadU16(p+739544,&out.forcedTargetTurns) ||
        !PHReadU16(p+603008,&selection) || !PHReadU8(p+600479,&out.targetDebounce) ||
        !PHReadU8(p+734608,&movement) || !PHReadU8(p+601627,&immediate) || !PHReadU8(p+600488,&popup) ||
        !PHReadPointer(h+0x2ab8e60,&overlayA) || !PHReadPointer(h+0x2abd058,&overlayB)) return false;
    out.target=int8_t(int16_t(target)); out.targetKnown=true; out.memberSelection=int16_t(selection);
    out.count=uint8_t(count); unsigned living=0;
    for (unsigned i=0;i<count;++i) {
        auto &e=out.enemies[i]; const uintptr_t a=p+PHOrbEnemyOffset+i*PHOrbEnemyStride;
        uint8_t life[624]; uint32_t mode=0,turns=0;
        if (!PHReadMemory(a,life,sizeof(life)) || !PHReadU32(a+580,&e.catalogID) || e.catalogID>INT32_MAX ||
            !Decode(a+2480,k1,k2,0x6a875a8bu,mode) || !Decode(a+2496,k1,k2,0x6a875a8bu,turns)) return false;
        e.flags=Load<uint16_t>(life+532); e.active=life[1]; e.identityKnown=true;
        e.targetBlockMode=int16_t(mode); e.targetBlockTurns=uint16_t(turns);
        if (e.flags&0x100u) {
            if (!ReadEnemyPart(p,count,i,k1,k2,e.part)) return false;
            e.hp=e.part.hp<=INT64_MAX ? e.part.hp : 0;
            if (e.hp) out.partsMask|=1u<<i;
        } else {
            EnemyActionEntry entry{}; entry.catalogID=e.catalogID;
            if (!PHReadPointer(a+PHOrbEnemyActionsParameterOffset,&entry.parameter)) return false;
            std::array<uint8_t,840> record{};
            if (!EnemyActionParameter(catalog,entry,record)) return false;
            e.parameter=entry.parameter;
            if (!EnemyActionWide(life,64,k1,k2,e.hp) || !EnemyActionWide(life,592,k1,k2,e.maximumHP) ||
                !e.maximumHP || e.hp>e.maximumHP) return false;
        }
        e.hpKnown=true;
        if (e.hp) { out.aliveMask|=1u<<i; ++living; }
        e.targetX=Load<float>(life+360); e.targetY=Load<float>(life+364);
        e.targetWidth=Load<int16_t>(life+324); e.targetHeight=Load<int16_t>(life+326);
        e.targetMaximumY=Load<int16_t>(life+322);
        if (!std::isfinite(e.targetX) || !std::isfinite(e.targetY)) return false;
        e.targetable=e.hp && (e.active&1u || (e.flags&0x100u)) &&
            !(int16_t(turns)>0 && int16_t(mode)>0) && e.targetWidth>0 && e.targetHeight>0 &&
            e.targetWidth<4096 && e.targetHeight<4096 && e.targetX>=0 && e.targetX<4096 &&
            e.targetY>=0 && e.targetY<=e.targetMaximumY && e.targetY<4096;
    }
    out.targetWindowKnown=true;
    out.targetAllowed=living>1 && state.phase==2 && state.settled && !state.boardTransition &&
        !state.dragged && PHInputIsQuiet(&state.input) && state.ordinary && !state.automation.busy &&
        !out.targetInputMode && !PositiveDuration(out.forcedTargetTurns) && !out.targetDebounce &&
        out.memberSelection<=999 && !(movement&1u) && !(immediate&1u) && !(popup&1u) && !(overlayA && overlayB);
    out.valid=true; return true;
}
