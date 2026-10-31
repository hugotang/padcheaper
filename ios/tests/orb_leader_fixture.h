// Value-only multiplayer leader fixture, shared by model/planning/session cases.
// No native getter, memory reader, solver or network behavior is reproduced.
#include <string.h>

static void SetLeaderContextFixture(PHOrbSnapshot *s, uint16_t leader, uint32_t leaderSkill,
                                   uint16_t helper, uint32_t helperSkill) {
    s->leadersValid = true;
    s->leaderNumbers[0] = leader; s->leaderNumbers[1] = helper;
    PHOrbLeaderContext c;
    memset(&c, 0, sizeof(c)); c.known = true; c.teamBank = 8000 + s->multiplayer.localSeat * 100;
    for (unsigned i = 0; i < 2; ++i) {
        uint16_t id = 0; PHBattleCatalogIDForNumber(s->leaderNumbers[i], &id);
        c.catalogIDs[i] = c.sourceCatalogIDs[i] = c.parameterIDs[i] = id;
        c.parameters[i] = 9000 + i * 100; c.sourceSlots[i] = i ? 5 : 0; c.activeFlags[i] = 1;
        c.skillIDs[i] = i ? helperSkill : leaderSkill;
    }
    s->leaderContext = c;
}
static void SetExtraLeaderFixture(PHOrbSnapshot *s) {
    PHOrbLeaderContext *c = &s->leaderContext;
    s->leaderNumbers[1] = 0; c->catalogIDs[1] = 10000000; c->sourceCatalogIDs[1] = (uint16_t)10000000u;
    c->activeFlags[1] = 0; c->extraFlags[1] = 2; c->extraHelper = true;
    c->extraRows = 12000; c->extraCount = 1; c->extraSelection = c->extraRecord = 73;
    c->parameterIDs[1] = 10000000; c->parameters[1] = c->extraRows + 8;
    c->skillIDs[1] = 0; c->extraSkill = 51;
}
