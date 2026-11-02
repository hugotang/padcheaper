#pragma once
#import "PHOrbEight.h"
#import "PHOrbProfiles.h"
#include <string.h>

static NSDictionary *EightProfileFixture(void) {
    NSData *data = [NSData dataWithContentsOfFile:@"ios/templates/eight-player-thanks-2026/activity-profile.json"];
    NSDictionary *package = data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL] : nil;
    return package[@"profile"];
}
static void EightTeamFixture(PHOrbSnapshot *s, NSDictionary *profile) {
    memset(&s->automation,0,sizeof(s->automation));
    memset(&s->eight,0,sizeof(s->eight));
    memset(&s->eightReceipt,0,sizeof(s->eightReceipt));
    memset(&s->enemyCombo,0,sizeof(s->enemyCombo));
    s->automation.valid = s->automation.boardKnown = s->automation.enemiesKnown = s->automation.window = true;
    s->automation.windowBlocksKnown = true; s->automation.activeEnemies = 1;
    s->enemyCombo.valid = true; s->enemyCombo.enemyCount = s->enemyCombo.aliveMask = 1; s->enemyCombo.hp[0] = 100;
    s->eight.valid = true; s->eight.teamBank = 55; s->eight.catalog = 66; s->eight.skills = 77;
    for (unsigned slot = 0; slot < 6; ++slot) {
        NSDictionary *source = profile[@"activity"][@"members"][slot];
        PHOrbSkillMember *member = &s->automation.members[slot]; PHOrbEightMember *m = &s->eight.members[slot];
        member->active = true; member->number = [source[@"number"] unsignedIntValue]; member->selection = UINT32_MAX;
        member->sourceCatalogIDs[0] = member->number; member->charge = 20;
        m->sourceSlot = slot; m->catalogID = 1000000 + member->number; m->parameter = 100 + slot;
        m->activeSkill = m->rootSkill = 1000 + slot; m->leaderSkill = 900; m->cooldown = [source[@"cooldown"] unsignedIntValue];
        m->skillKnown = ![source[@"effect"] isEqual:@"observe"]; m->leaderKnown = true; m->noSkyfall = true;
        m->boardColors = [source[@"boardColors"] unsignedIntValue]; m->unlock = [source[@"unlock"] boolValue];
        for (unsigned c = 0; c < 6; ++c) { m->convert[c] = [source[@"convert"][c] unsignedIntValue]; m->generate[c] = [source[@"generate"][c] unsignedIntValue]; }
        m->attackColors = m->comboColors = 17; m->attackLength = 6; m->comboLength = 12; m->attackPercent = 1000; m->addedCombos = 1;
    }
}
