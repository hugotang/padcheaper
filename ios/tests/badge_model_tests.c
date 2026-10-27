#include "PHBadgeModel.h"
#include <stdio.h>

static unsigned failures;
static void Check(bool condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
static bool Has(const uint32_t mask[8], unsigned id) { return (mask[id / 32] >> (id % 32)) & 1u; }

int main(void) {
    PHBadgeEntry entries[256] = {0};
    uint32_t mask[8];
    for (unsigned id = 1; id < 256; ++id) entries[id] = (PHBadgeEntry){true, 1, 0};
    Check(PHBadgeBuildMask(entries, 255, mask) == 255 && !Has(mask, 0) && Has(mask, 255), "default excluded and ID 255 uses the final bitmap bit");
    entries[3].kind = 0;
    entries[4].readable = false;
    entries[5].prerequisite = 4;
    entries[6].prerequisite = 7;
    entries[7].prerequisite = 6;
    entries[8].prerequisite = 256;
    entries[9].prerequisite = 10;
    Check(PHBadgeBuildMask(entries, 255, mask) == 249, "invalid, cyclic, missing and placeholder records are excluded");
    for (unsigned id = 3; id <= 8; ++id) Check(!Has(mask, id), "invalid dependency cannot enable a badge");
    Check(Has(mask, 9) && Has(mask, 10), "valid prerequisite chain remains enabled");
    Check(PHBadgeBuildMask(entries, 256, mask) == 0, "bitmap bounds cannot be exceeded");
    Check(PHBadgeBuildMask(NULL, 1, mask) == 0, "missing table is refused");
    for (unsigned index = 0; index < 32; ++index) {
        Check(PHBadgeEncode(PHBadgeEncode(255, index), index) == 255, "encoding round-trips by bank-local index");
    }
    Check(PHBadgeEncode(7, 0) == (7 ^ 0x61) && PHBadgeEncode(7, 1) == (7 ^ 0x70), "bank-local encoding matches native serializer");
    printf("Badge model tests: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
