#include "PHBattlePreset.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
static void check(bool value, const char *message) { if (!value) { fprintf(stderr,"FAIL: %s\n",message); ++failures; } }
int main(void) {
    const uint16_t plus[3] = {297,297,297};
    uint8_t bytes[3] = {0}; uint16_t decoded[3] = {0};
    const uint8_t full[3] = {0xc6,0xc6,0x63};
    check(PHBattleEncodePlus(plus,bytes) && memcmp(bytes,full,3)==0,"+891 uses native excess-plus encoding, not three truncated 297 bytes");
    PHBattleDecodePlus(full,decoded);
    check(decoded[0]==297 && decoded[1]==297 && decoded[2]==297,"native plus decoding gives all three +297");
    uint16_t partial[3]={99,15,0};
    check(PHBattleEncodePlus(partial,bytes) && bytes[0]==99 && bytes[1]==15 && bytes[2]==0,"ordinary independent plus values remain distinct");
    partial[0]=298; check(!PHBattleEncodePlus(partial,bytes),"plus overflow rejected");
    partial[0]=297; check(!PHBattleEncodePlus(partial,bytes),"unrepresentable asymmetric extended plus rejected");
    uint64_t encoded=0;
    uint8_t latents[]={43,11,11,11,11,11,11};
    check(PHBattleEncodeLatents(latents,7,8,&encoded) && (encoded & 255)==0x8f && ((encoded>>8)&127)==43 &&
          ((encoded>>15)&127)==43 && ((encoded>>22)&127)==11,"two-slot latent repeats in the native seven-bit stream");
    check(!PHBattleEncodeLatents(latents,7,6,&encoded),"latent capacity is checked before writing");
    uint8_t six[]={13,11,11};
    check(PHBattleEncodeLatents(six,3,8,&encoded) && !PHBattleEncodeLatents(six,3,6,&encoded),"six-slot latent cost is counted");
    check(PHBattleLatentCost(52)==2 && PHBattleLatentCost(53)==0,"native reviewed latent range is bounded");
    PHBattleCardInfo body={.catalogID=14338,.maxLevel=120,.maxSkillLevel=5,.awakeningCount=9,
        .superCount=4,.supers={111,153,70,142},.latentSlots=8};
    PHBattleCardInfo assist={.catalogID=14232,.maxLevel=99,.maxSkillLevel=6,.awakeningCount=8,.latentSlots=6,.canAssist=true};
    PHBattleLoadout loadout={.bodyNumber=14238,.assistNumber=14132,.assistSkillLevel=1,.plus={297,297,297},
        .awakenings=-1,.assistAwakenings=-1,.superAwakening=153,.assistPlus=true,.parameters=true};
    PHBattleProfile profile;
    check(PHBattleBuildProfile(&loadout,&body,&assist,&profile),"reference body/weapon pair is accepted");
    check(profile.bytes[4]==0x02 && profile.bytes[5]==0x38 && profile.bytes[6]==120 && profile.bytes[8]==5,
          "display No.14238 is encoded as native 14338 with max level and body skill");
    check(profile.bytes[14]==0x98 && profile.bytes[15]==0x37 && profile.bytes[16]==0x88 && profile.bytes[17]==99 &&
          profile.bytes[18]==1,"weapon has separate ID, full awakenings, +297, level and SLv.1");
    check(!memcmp(profile.bytes+24,full,3) && profile.bytes[27]==9 && profile.bytes[40]==153,"body awakens and chosen super are independent");
    loadout.superAwakening=123;
    check(!PHBattleBuildProfile(&loadout,&body,&assist,&profile),"super must be offered by the target body");
    loadout.superAwakening=153; loadout.level=99;
    check(!PHBattleBuildProfile(&loadout,&body,&assist,&profile),"ordinary super requires level 100");
    loadout.level=0;loadout.assistSkillLevel=7;
    check(!PHBattleBuildProfile(&loadout,&body,&assist,&profile),"weapon skill cap is checked independently");
    loadout.assistSkillLevel=1;assist.canAssist=false;
    check(!PHBattleBuildProfile(&loadout,&body,&assist,&profile),"non-inheritable weapons are rejected");
    assist.canAssist=true;loadout.assistNumber=0;body.maxLevel=99;body.superCount=0;body.syncAwakening=56;loadout.superAwakening=-1;
    check(PHBattleBuildProfile(&loadout,&body,NULL,&profile) && profile.bytes[6]==99 && profile.bytes[40]==56,
          "a non-limit-break body keeps its native cap and may use its own sync awakening");
    printf("Battle preset tests: %s\n",failures?"FAILED":"passed");
    return failures?1:0;
}
