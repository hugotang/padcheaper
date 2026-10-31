#import <Foundation/Foundation.h>
#include <vector>
#include <cstdio>
#define PH_MUSIC_TESTS 1
#include "../src/PHMusic.mm"

static std::vector<uint8_t> guest(0x3000000), implBytes(0xc4000), dataBytes(32768);
static const uintptr_t Header = reinterpret_cast<uintptr_t>(guest.data()), Impl = reinterpret_cast<uintptr_t>(implBytes.data()),
    Data = reinterpret_cast<uintptr_t>(dataBytes.data());
static unsigned failures, writes, sets, refreshes, checks;
static bool validCode = true, failWrite, ignoreSet, changeTableDuringRead, reorderTableDuringRead, mutableNames;
static void Check(bool ok,const char *why) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); ++failures; } }
static bool Range(uintptr_t address,size_t length) {
    for (auto *v : {&guest,&implBytes,&dataBytes}) {
        auto base = reinterpret_cast<uintptr_t>(v->data());
        if (address >= base && address-base <= v->size() && length <= v->size()-(address-base)) return true;
    }
    return false;
}
BOOL PHReadMemory(uintptr_t address,void *out,size_t length) {
    if (!Range(address,length)) return NO; std::memcpy(out,reinterpret_cast<void *>(address),length);
    if (changeTableDuringRead && address==Data+2304) {
        changeTableDuringRead=false; const uintptr_t replaced=Data+2560;
        std::memcpy(reinterpret_cast<void *>(Data+2048+16),&replaced,sizeof(replaced));
    }
    if (reorderTableDuringRead && address==Data+2304) {
        reorderTableDuringRead=false;
        uintptr_t names[2];
        std::memcpy(&names[0],reinterpret_cast<void *>(Data+2304+8),sizeof(uintptr_t));
        std::memcpy(&names[1],reinterpret_cast<void *>(Data+2304+24),sizeof(uintptr_t));
        std::memcpy(reinterpret_cast<void *>(Data+2304+8),&names[1],sizeof(uintptr_t));
        std::memcpy(reinterpret_cast<void *>(Data+2304+24),&names[0],sizeof(uintptr_t));
    }
    return YES;
}
BOOL PHWriteData(uintptr_t address,const void *input,size_t length) {
    ++writes;
    if (failWrite || address != Impl+PHMusicOverrideOffset || length != 2) return NO;
    std::memcpy(reinterpret_cast<void *>(address),input,length); return YES;
}
BOOL PHMemoryHasProtection(uintptr_t address,size_t length,vm_prot_t required,vm_prot_t forbidden) {
    (void)required; return Range(address,length) && !(mutableNames && (forbidden & VM_PROT_WRITE));
}
BOOL PHValidateGuest(const mach_header_64 *header,NSString **reason) { (void)header; (void)reason; ++checks; return validCode; }
template<class T> static void Put(uintptr_t address,T value) { std::memcpy(reinterpret_cast<void *>(address),&value,sizeof(value)); }
static void Protected(uintptr_t address,uint32_t value,uint32_t salt=0x766cfc6du) {
    uint32_t raw[]={((value<<5)|(value>>27))^salt,0,value^17u^42u,value};
    std::memcpy(reinterpret_cast<void *>(address),raw,sizeof(raw));
}
static int16_t Current(void) { int16_t value; PHReadMemory(Impl+PHMusicOverrideOffset,&value,2); return value; }
static void Set(uintptr_t game,int track,int when) {
    Check(game==Header+PHGameMainRVA&&when==-1,"native setter uses the current game and immediate audio path"); ++sets;
    if (!ignoreSet) { Put<int16_t>(Impl+PHMusicOverrideOffset,track); Put<int16_t>(Impl+PHMusicTargetOffset,track); }
}
static uint64_t Refresh(uintptr_t game) {
    Check(game==Header+PHGameMainRVA,"restore refresh uses the same live game"); ++refreshes;
    Put<int16_t>(Impl+PHMusicTargetOffset,0); return 0;
}
static PHBattleFrame Frame(void) { PHBattleFrame frame{}; frame.header=Header; frame.player=42; frame.epoch=1; frame.ready=frame.enabled=true; return frame; }
static void Seed(void) {
    std::fill(guest.begin(),guest.end(),0); std::fill(implBytes.begin(),implBytes.end(),0); std::fill(dataBytes.begin(),dataBytes.end(),0);
    writes=sets=refreshes=checks=0; validCode=true;
    failWrite=ignoreSet=changeTableDuringRead=reorderTableDuringRead=mutableNames=false; TestCalls={Set,Refresh};
    const uintptr_t save=Header+PHSaveRVA;
    Put<uint16_t>(save,0x0101); Put<uint32_t>(save+PHPlayerIDOffset,42);
    Put<uintptr_t>(Header+PHSceneTableRVA+24,Header+PHGameMainRVA);
    Put<uintptr_t>(Header+PHGameMainRVA+0x70,Impl); Put<uintptr_t>(Header+PHBattleImplRVA,Impl);
    Put<uint16_t>(save+PHBattleDungeonOffset,7); Put<uint16_t>(save+PHBattleDungeonOffset+2,2);
    Put<uint8_t>(Header+PHBattleEntryHashRVA,1); Put<uint16_t>(Header+PHBattleModeRVA,1);
    Put<uint32_t>(Header+PHBattleKey1RVA,17); Put<uint32_t>(Header+PHBattleKey2RVA,42);
    Protected(Impl+PHBattlePhaseOffset,2); Protected(Impl+PHBattleWaveOffset,0);
    Protected(Impl+PHMusicEffectOffset,UINT16_MAX,0x6a875a8bu);
    Put<uint32_t>(Impl+PHMusicBossWaveOffset,4); Put<int16_t>(Impl+PHMusicOverrideOffset,-1); Put<int16_t>(Impl+PHMusicTargetOffset,0);
    Put<uintptr_t>(Impl+PHBattleDungeonPointerOffset,Data); Put<uintptr_t>(Impl+PHBattleDungeonPointerOffset-8,Data+256);
    Put<uint16_t>(Data+18,2); Put<uint16_t>(Data+256+30,7);
    std::memset(reinterpret_cast<void *>(save+PHMusicClearBucketsOffset),0xff,512);
    Put<uint16_t>(save+PHMaxDecksOffset,2); Put<uint16_t>(save+PHMusicSelectedDeckOffset,1);
    Put<uint16_t>(save+PHPrimaryDecksOffset+96+24,2); Put<uint16_t>(save+PHPrimaryDecksOffset+96+26,3);
    Put<uint32_t>(save+PHEntryCountOffset,1); Put<uintptr_t>(save+PHEntryPointerOffset,Data+512);
    Put<uint16_t>(Data+512,10001); Put<uint32_t>(Data+512+28,0); Put<uint32_t>(Data+512+36,32);
    Put<uintptr_t>(save+PHMusicStringObjectOffset,Data+768); Put<uintptr_t>(Data+768+16,Data+1024);
    std::strcpy(reinterpret_cast<char *>(Data+1024),"track-normal"); std::strcpy(reinterpret_cast<char *>(Data+1056),"track-boss");
    Put<uintptr_t>(Header+PHMusicAudioRVA,Data+2048); Put<uintptr_t>(Data+2048+16,Data+2304); Put<uint16_t>(Data+2048+24,3);
    Put<uintptr_t>(Data+2304+8,Data+1100); Put<uintptr_t>(Data+2304+24,Data+1024); Put<uintptr_t>(Data+2304+40,Data+1056);
    std::strcpy(reinterpret_cast<char *>(Data+1100),"default");
}
int main(void) { @autoreleasepool {
    Seed(); PHMusic *music=[PHMusic new]; PHBattleFrame frame=Frame();
    const auto saveBefore=guest;
    [music onFrame:frame time:1];
    Check(sets==1&&Current()==1&&[[music snapshot][@"active"] boolValue],"uncleared floor selects the current deck normal track");
    [music onFrame:frame time:2]; Check(sets==1,"same selection does not restart the music each frame");
    Protected(Impl+PHBattleWaveOffset,4); [music onFrame:frame time:3];
    Check(sets==2&&Current()==2,"boss wave selects the configured boss version");
    frame.enabled=false; [music onFrame:frame time:4];
    Check(Current()==-1&&refreshes==1&&writes==1,"disable restores only our temporary music override");
    Check(guest==saveBefore,"music never changes account, clear flags, rewards, audit or request state");
    Seed(); music=[PHMusic new]; frame=Frame();
    std::strcpy(reinterpret_cast<char *>(Data+1024),"BGM_972");
    std::strcpy(reinterpret_cast<char *>(Data+1152),"bgm_972.caf"); Put<uintptr_t>(Data+2304+24,Data+1152);
    std::strcpy(reinterpret_cast<char *>(Data+1056),"bgm_973");
    std::strcpy(reinterpret_cast<char *>(Data+1216),"BGM_973.CAF"); Put<uintptr_t>(Data+2304+40,Data+1216);
    [music onFrame:frame time:1];
    Check(sets==1&&Current()==1&&[[music snapshot][@"selection"][@"registered"] isEqual:@"bgm_972.caf"],
        "native case-insensitive prefix resolves descriptor name without CAF suffix");
    Protected(Impl+PHBattleWaveOffset,4); [music onFrame:frame time:2];
    Check(sets==2&&Current()==2,"boss track follows the same native name matching");
    Seed(); music=[PHMusic new]; frame=Frame(); mutableNames=true;
    std::strcpy(reinterpret_cast<char *>(Data+1024),"BGM_12");
    std::strcpy(reinterpret_cast<char *>(Data+1152),"bgm_123.caf"); Put<uintptr_t>(Data+2304+24,Data+1152);
    std::strcpy(reinterpret_cast<char *>(Data+1216),"bgm_12.caf"); Put<uintptr_t>(Data+2304+40,Data+1216);
    [music onFrame:frame time:1];
    Check(sets==1&&Current()==1,"ambiguous prefix uses native first registry entry, not a preferred exact match");
    std::strcpy(reinterpret_cast<char *>(Data+1100),"BGM_12-first.caf");
    [music onFrame:frame time:2];
    Check(sets==2&&Current()==0,"mutable earlier resource names are rescanned before a cached later match");
    Seed(); music=[PHMusic new]; frame=Frame();
    std::strcpy(reinterpret_cast<char *>(Data+1100),"bgm_999.caf");
    std::strcpy(reinterpret_cast<char *>(Data+1024),"BGM_972");
    std::strcpy(reinterpret_cast<char *>(Data+1152),"bgm_972.caf"); Put<uintptr_t>(Data+2304+24,Data+1152);
    [music onFrame:frame time:1];
    const uintptr_t firstName=Data+1152, unrelated=Data+1100;
    Put<uintptr_t>(Data+2304+8,firstName); Put<uintptr_t>(Data+2304+24,unrelated);
    [music onFrame:frame time:2];
    Check(sets==2&&Current()==0,"registry reordering invalidates cache and selects the new native first index");
    Seed(); music=[PHMusic new]; frame=Frame(); changeTableDuringRead=true;
    [music onFrame:frame time:1];
    Check(!sets&&!writes&&[[music snapshot][@"message"] containsString:@"資源表已改變"],
        "a registry replacement during lookup cannot select an index from the former table");
    Seed(); music=[PHMusic new]; frame=Frame(); reorderTableDuringRead=true;
    [music onFrame:frame time:1];
    Check(!sets&&!writes&&[[music snapshot][@"message"] containsString:@"資源表已改變"],
        "an in-place registry reorder during lookup cannot select a stale native index");
    [music onFrame:frame time:2];
    Check(sets==1&&Current()==0,"the next stable lookup uses the reordered registry's first match");
    Seed(); music=[PHMusic new]; frame=Frame(); Put<uintptr_t>(Data+2304+8,0);
    [music onFrame:frame time:1]; Check(!sets&&!writes,"unreadable earlier resource cannot be skipped in native first-match order");
    Check(!ResourceMatches(@"",@"bgm_972.caf")&&!ResourceMatches(@"BGM_972",@"bgm_97.caf")&&
        !ResourceMatches(@"BGM_972",@"xbgm_972.caf"),"empty and nonprefix names do not resolve to a registered track");
    std::printf("Music resource matching regressions: case, suffix, first-match order and registry races checked\n");
    Seed(); music=[PHMusic new]; frame=Frame(); [music onFrame:frame time:1];
    Put<int16_t>(Impl+PHMusicOverrideOffset,9); Put<int16_t>(Impl+PHMusicTargetOffset,9);
    [music onFrame:frame time:2]; frame.enabled=false; [music onFrame:frame time:3];
    Check(Current()==9&&sets==1&&!writes,"native script takeover is not overwritten or restored");
    frame.enabled=true; Put<int16_t>(Impl+PHMusicOverrideOffset,-1); [music onFrame:frame time:4];
    Check(sets==1,"a released script override does not cause the helper to reclaim this battle");
    Seed(); music=[PHMusic new]; frame=Frame(); Protected(Impl+PHMusicEffectOffset,5,0x6a875a8bu);
    [music onFrame:frame time:1]; Check(!sets&&!writes,"a preexisting dungeon music effect keeps its native track");
    Protected(Impl+PHMusicEffectOffset,UINT16_MAX,0x6a875a8bu); [music onFrame:frame time:2];
    Check(sets==1,"a finished event allows the first selection of the named team track");
    Protected(Impl+PHMusicEffectOffset,6,0x6a875a8bu); [music onFrame:frame time:3];
    Check(Current()==-1&&refreshes==1&&![[music snapshot][@"active"] boolValue],"a new event releases our music override through native refresh");
    Protected(Impl+PHMusicEffectOffset,UINT16_MAX,0x6a875a8bu); [music onFrame:frame time:4];
    Check(sets==1,"event takeover keeps the remainder of this battle on native selection");
    Seed(); music=[PHMusic new]; frame=Frame(); Put<uint32_t>(Impl+PHMusicEffectOffset,0);
    [music onFrame:frame time:1]; Check(!sets&&!writes,"inconsistent protected event music cannot be bypassed");
    Seed(); music=[PHMusic new]; frame=Frame(); [music onFrame:frame time:1];
    Put<uint8_t>(Header+PHBattleEntryHashRVA,2); Put<int16_t>(Impl+PHMusicOverrideOffset,-7); Put<int16_t>(Impl+PHMusicTargetOffset,0);
    frame.enabled=false; [music onFrame:frame time:2]; Check(Current()==-7&&!writes,"same allocation reused by a new battle cannot receive the old restore");
    Seed(); music=[PHMusic new]; frame=Frame(); Put<uint32_t>(Header+PHBattleMultiplayerRVA,1); [music onFrame:frame time:1];
    Put<uint32_t>(Header+PHBattleMultiplayerRVA,0); [music onFrame:frame time:2]; Check(!sets&&!writes,"multiplayer is latched despite a later selector transition");
    Put<uint16_t>(Header+PHSaveRVA,0); [music onFrame:frame time:3]; Put<uint16_t>(Header+PHSaveRVA,0x0101); [music onFrame:frame time:4];
    Check(sets==1,"observed battle exit permits a later single-player track selection");
    for (uint16_t mode : {uint16_t(7), uint16_t(10), uint16_t(11)}) {
        Seed(); music=[PHMusic new]; frame=Frame(); Put<uint16_t>(Header+PHBattleModeRVA,mode);
        [music onFrame:frame time:1];
        Check(!sets&&!writes,"co-op and competitive categories cannot set music even with a zero selector");
    }
    Seed(); music=[PHMusic new]; frame=Frame();
    Put<uint16_t>(Header+PHSaveRVA+PHMusicClearBucketsOffset+28,0);
    const auto clearRecord=Header+PHSaveRVA+PHMusicClearRecordsOffset;
    Put<uint16_t>(clearRecord,0xffff); Put<uint16_t>(clearRecord+2,7); Put<uint16_t>(clearRecord+4,2); Put<uint8_t>(clearRecord+8,2);
    [music onFrame:frame time:1]; Check(!sets,"already-cleared dungeons retain the native selector");
    Put<uint8_t>(clearRecord+8,0); [music onFrame:frame time:2]; Check(sets==1,"known uncleared record may use the option");
    Seed(); music=[PHMusic new]; frame=Frame(); std::strcpy(reinterpret_cast<char *>(Data+1056),"RND");
    [music onFrame:frame time:1]; Check(!sets,"random music is not approximated or allowed to alter random history");
    Seed(); music=[PHMusic new]; frame=Frame(); Put<uint16_t>(Data+2048+24,1); [music onFrame:frame time:1];
    Check(!sets,"a missing registered resource leaves music alone");
    Put<uint16_t>(Data+2048+24,3); [music onFrame:frame time:2]; Check(sets==1,"deferred audio resource registration is retried");
    Seed(); music=[PHMusic new]; frame=Frame(); Put<uint32_t>(Data+124,1);
    Put<uintptr_t>(Header+PHSaveRVA+PHBattleDungeonRulePoolOffset,Data+4096); Put<uintptr_t>(Data+4096+16,Data+8192);
    std::strcpy(reinterpret_cast<char *>(Data+8193),"fc1:1|fc2:1|fc3:1|fc4:1|fc5:1|fc6:1");
    [music onFrame:frame time:1]; Check(!sets,"native fixed-six-member dungeon exclusion remains");
    Seed(); music=[PHMusic new]; frame=Frame(); Protected(Impl+PHBattlePhaseOffset,3); [music onFrame:frame time:1];
    Check(!sets,"animated combat phases do not receive music writes");
    Protected(Impl+PHBattlePhaseOffset,2); validCode=false; [music onFrame:frame time:2];
    Check(!sets&&[[music snapshot][@"fault"] boolValue],"failed target validation disables native music calls");
    Seed(); music=[PHMusic new]; frame=Frame(); ignoreSet=true; [music onFrame:frame time:1]; [music onFrame:frame time:2];
    Check(sets==1&&[[music snapshot][@"fault"] boolValue],"native setter readback failure stops repeated writes");
    Seed(); music=[PHMusic new]; frame=Frame(); [music onFrame:frame time:1]; failWrite=true; frame.enabled=false;
    [music onFrame:frame time:2]; Check(!refreshes&&[[music snapshot][@"fault"] boolValue],"failed restore never refreshes with an unverified override");
    std::printf("Music playback tests: %u failures\n",failures);
} return failures?1:0; }
