// Shared static geometry bound. No ordinary/beam solver or native call.
#include "PHOrbMonochromeTemplate.h"

const char *PHOrbMonochromeEligibilityReason(const PHOrbBoard &board,
    const PHOrbSolveOptions &options) noexcept {
    if (!PHOrbBoardValid(&board) || !PHOrbGoalsValid(&options.goals)) return "invalid-input";
    if (board.width != 6 || board.height != 5) return "board-size";
    if (options.fishCell < 0 || options.fishCell >= 30 ||
        board.spinnerMask != (Bits(1) << unsigned(options.fishCell)) ||
        (board.spinnerColors & ~63u) || PHOrbSpinnerVariants(&board) < 2) return "spinner";
    if (board.tapeMask || board.cloudMask || board.fixedStart || board.unmatchableColors)
        return "board-restriction";
    if ((board.minErase && board.minErase != 3) || board.minMatch > 3) return "match-rule";
    if (!TimedFishingActivitySupported(options.activity)) return "activity";
    if (options.goals.count != 2 || options.goals.comboFirstWave || options.goals.bonusFirstWave)
        return "goals";
    const unsigned first = options.fishCell == 0 ? 1 : 0, donor = board.types[first];
    if (donor >= PHOrbBasicTypes || !(board.spinnerColors & (1u << donor))) return "donor";
    for (unsigned i = 0; i < 30; ++i) {
        if (board.flags[i] || board.blindTurns[i] || board.thornDamage[i]) return "orb-effect";
        if (i != unsigned(options.fishCell) && board.types[i] != donor) return "not-monochrome";
    }
    unsigned t = 0, four = 0;
    for (unsigned g = 0; g < options.goals.count; ++g) {
        const auto &goal = options.goals.items[g];
        if (goal.count != 1 || goal.length || Count(goal.colors) != 1) return "goals";
        if (goal.kind == PHOrbGoalFour && goal.colors == (1u << donor)) ++four;
        else if (goal.kind == PHOrbGoalT && goal.colors != (1u << donor) &&
            (goal.colors & board.spinnerColors)) ++t;
        else return "goals";
    }
    return t == 1 && four == 1 ? "eligible" : "goals";
}

PHOrbMonochromeTemplateDetails PHOrbInspectMonochromeTemplate(const PHOrbBoard &board,
    const PHOrbSolveOptions &options) noexcept {
    PHOrbMonochromeTemplateDetails details;
    details.reason = PHOrbMonochromeEligibilityReason(board,options);
    if (std::strcmp(details.reason,"eligible")) return details;
    details.eligible = true;
    // Donor belongs to the spinner table, so every all-phase release must
    // also work when that cell is donor. Each exchange can add at most one
    // non-donor fixed orb. Five of them must be the exact target T.
    //
    // A horizontal exact donor Four occupies four columns. Every one needs
    // a non-donor somewhere, otherwise its entire five-cell column is marked
    // and joins the Four. At least one horizontal endpoint also lies inside
    // this six-column board and must be non-donor to avoid a five-orb run.
    // That endpoint is in a fifth distinct column. For a vertical Four the
    // same argument gives five rows (the board is five rows tall).
    //
    // The T spans three columns and rows. One extra non-donor can cover only
    // a fourth. Hence <=6 exchanges cannot satisfy both shapes in that phase,
    // regardless of route, colors of the separator, timing or search budget.
    details.minimumExchanges = 7;
    details.reason = "exact-four-separator-bound";
    return details;
}
