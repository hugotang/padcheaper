#include "PHOrbEightModel.h"
#include <stdio.h>
#include <string.h>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

// Cup-like fixed team: slot 2 converts water->fire, slot 3 converts wood->dark,
// slot 4 rewrites the board (Ney), slot 5 generates orbs, slot 1 is a
// heart->fire converter that is not charged.
static PHOrbSnapshot Fixture(void) {
    PHOrbSnapshot s;
    memset(&s, 0, sizeof(s));
    for (unsigned i = 0; i < 6; ++i) {
        PHOrbEightMember *m = &s.eight.members[i];
        m->skillKnown = true; m->cooldown = 4;
        for (unsigned c = 0; c < 6; ++c) m->convert[c] = (uint8_t)c;
        s.automation.members[i].active = true; s.automation.members[i].charge = 4;
    }
    s.eight.members[0].convert[5] = 0; s.automation.members[0].charge = 0;
    s.eight.members[1].convert[1] = 0;
    s.eight.members[2].convert[2] = 4;
    s.eight.members[3].boardColors = 0x11;
    s.eight.members[4].generate[0] = 6;
    return s;
}

int main(void) {
    PHOrbSnapshot s = Fixture();
    uint8_t map[6];
    bool ready = false;
    const uint8_t pair[] = {2, 3}, single[] = {2}, ney[] = {4}, generator[] = {5}, uncharged[] = {1};

    Check(PHOrbEightComposeConversion(&s, pair, 0, 0, 0, map, &ready) && ready &&
          map[0] == 0 && map[1] == 1 && map[5] == 5, "an empty sequence is the identity and ready");
    Check(PHOrbEightComposeConversion(&s, pair, 2, 0, 0, map, &ready) && ready &&
          map[1] == 0 && map[2] == 4 && map[0] == 0 && map[3] == 3, "conversions compose in slot order");

    // Composition follows the already-converted colour: water->fire, then fire->light.
    PHOrbSnapshot chained = s;
    chained.eight.members[2].convert[2] = 2; chained.eight.members[2].convert[0] = 3;
    Check(PHOrbEightComposeConversion(&chained, pair, 2, 0, 0, map, NULL) && map[1] == 3 && map[0] == 3,
          "a later conversion applies to orbs produced by an earlier one");

    s.automation.members[2].charge = 3;
    Check(!PHOrbEightComposeConversion(&s, pair, 2, 0, 0, map, &ready), "an uncharged slot is unavailable now");
    Check(PHOrbEightComposeConversion(&s, pair, 2, 0, 1, map, &ready) && !ready,
          "one charge turn short is available next turn and reported as not ready now");
    s.automation.members[2].charge = 2;
    Check(!PHOrbEightComposeConversion(&s, pair, 2, 0, 1, map, &ready), "two charge turns short is not available next turn");
    s = Fixture();

    Check(!PHOrbEightComposeConversion(&s, pair, 2, 1u << 2, 0, map, NULL), "a slot attempted this turn is never recast");
    Check(!PHOrbEightComposeConversion(&s, ney, 1, 0, 0, map, NULL), "a whole-board rewrite has no fixed conversion");
    Check(!PHOrbEightComposeConversion(&s, generator, 1, 0, 0, map, NULL), "orb generation is not a pure conversion");
    Check(!PHOrbEightComposeConversion(&s, uncharged, 1, 0, 1, map, NULL), "a slot four turns short is rejected");
    PHOrbSnapshot blocked = s;
    blocked.automation.members[1].bound = true;
    Check(!PHOrbEightComposeConversion(&blocked, single, 1, 0, 0, map, NULL), "a bound member cannot cast");
    blocked = s; blocked.automation.members[1].state = 1;
    Check(!PHOrbEightComposeConversion(&blocked, single, 1, 0, 0, map, NULL), "a member in a native state cannot cast");
    blocked = s; blocked.automation.members[1].active = false;
    Check(!PHOrbEightComposeConversion(&blocked, single, 1, 0, 0, map, NULL), "an inactive member cannot cast");
    blocked = s; blocked.eight.members[1].skillKnown = false;
    Check(!PHOrbEightComposeConversion(&blocked, single, 1, 0, 0, map, NULL), "an unknown skill cannot be planned");
    blocked = s; blocked.eight.members[1].convert[1] = 6;
    Check(!PHOrbEightComposeConversion(&blocked, single, 1, 0, 0, map, NULL), "conversion to a special orb is rejected");
    const uint8_t outOfRange[] = {7}, zero[] = {0};
    Check(!PHOrbEightComposeConversion(&s, outOfRange, 1, 0, 0, map, NULL) &&
          !PHOrbEightComposeConversion(&s, zero, 1, 0, 0, map, NULL) &&
          !PHOrbEightComposeConversion(&s, pair, 7, 0, 0, map, NULL), "slot and length bounds are enforced");

    // Board: fire 5, water 9, wood 6, light 4, dark 2, heart 4 (30 orbs).
    const unsigned counts[6] = {5, 9, 6, 4, 2, 4};
    const unsigned fireDark = (1u << 0) | (1u << 4);
    Check(PHOrbEightComposeConversion(&s, pair, 2, 0, 0, map, NULL), "fixture pair composes");
    unsigned converted[6];
    PHOrbEightConvertedCounts(counts, map, converted);
    Check(converted[0] == 14 && converted[4] == 8 && converted[1] == 0 && converted[2] == 0 &&
          converted[0] + converted[1] + converted[2] + converted[3] + converted[4] + converted[5] == 30,
          "converted counts move every orb and preserve the total");
    Check(PHOrbEightAttackOrbs(counts, map, fireDark) == 22, "attack orbs count converted fire and dark");
    uint8_t identity[6] = {0, 1, 2, 3, 4, 5};
    Check(PHOrbEightAttackOrbs(counts, identity, fireDark) == 7, "the unconverted board supplies its own attack orbs");

    // After the pair, fire 14 and dark 8. Erasing three non-attack orbs keeps
    // fire 14 >= 12 and >= 6; erasing three original water orbs costs fire.
    Check(PHOrbEightCanRetainAfterThree(counts, map, fireDark, 0, 12, 6), "a non-attack group keeps a twelve-orb fire link");
    const unsigned onlyWaterSpare[6] = {3, 9, 0, 0, 0, 0};
    Check(!PHOrbEightCanRetainAfterThree(onlyWaterSpare, map, fireDark, 0, 12, 6),
          "every removable group converts to fire and leaves fewer than twelve");
    Check(!PHOrbEightCanRetainAfterThree(counts, map, fireDark, 0, 12, 15), "the primary minimum is enforced after removal");
    Check(PHOrbEightCanRetainAfterThree(counts, map, fireDark, 0, 8, 6) &&
          !PHOrbEightCanRetainAfterThree(counts, map, 1u << 3, 0, 8, 6),
          "only configured attack colours can satisfy the link length");
    const unsigned noTriple[6] = {2, 2, 2, 2, 2, 2};
    Check(!PHOrbEightCanRetainAfterThree(noTriple, identity, fireDark, 0, 3, 0), "a three-orb group must exist on the board");
    Check(!PHOrbEightCanRetainAfterThree(counts, map, fireDark, 6, 12, 6), "an invalid primary colour is rejected");

    if (failures) { fprintf(stderr, "8P model tests: %u failure(s)\n", failures); return 1; }
    puts("8P model tests: passed (conversion composition, charge horizon, eligibility, retained inventory)");
    return 0;
}
