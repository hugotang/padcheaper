#pragma once
#include "PHOrbSolver.h"

struct PHOrbMonochromeTemplateDetails {
    bool eligible = false;
    // Proven necessary number of exchanges, never a found solution. Zero
    // means this narrow proof does not apply. Seven need not be sufficient.
    unsigned minimumExchanges = 0;
    const char *reason = "not-inspected";
};

// Returns "eligible" only for a 6x5 board with one spinner, 29 identical
// basic donor orbs, and exactly T(target,1) + Four(donor,1), in either order.
// Target and donor differ and both belong to the basic spinner table. Requires
// ordinary match rules, default activity and no orb/board restrictions. Legal
// token enhancement and ordinary poison/thorn ranking options are preserved.
const char *PHOrbMonochromeEligibilityReason(const PHOrbBoard &board,
    const PHOrbSolveOptions &options) noexcept;

// At the donor release phase, a horizontal exact Four needs non-donor cells
// in at least five columns; a vertical Four needs them in at least five rows.
// A target T spans three rows and columns. With at most one additional orb,
// these shapes cannot coexist. Thus an all-phase solution needs >=7 exchanges
// from this monochrome board. This is a static lower bound, not route search,
// time estimation, permission to exceed the six-exchange cap, or evidence that
// seven exchanges suffice. Conditional-color release is outside this proof.
PHOrbMonochromeTemplateDetails PHOrbInspectMonochromeTemplate(const PHOrbBoard &board,
    const PHOrbSolveOptions &options) noexcept;
