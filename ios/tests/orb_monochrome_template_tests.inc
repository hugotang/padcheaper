// Written after the 0.5.41 monochrome report. Not executed in this source-only
// batch. Static layouts below are hand-derived counterexamples, never seeded
// search routes or evidence that the reported five/six-take board is solved.
#include "PHOrbMonochromeTemplate.h"

static PHOrbBoard MonochromeReportedBoard(unsigned fish = 6) {
    auto board = Board(6,5,1);
    std::fill(board.types,board.types + 30,uint8_t(5));
    std::fill(board.enhancement,board.enhancement + 30,uint32_t(0));
    for (unsigned i : {1u,2u,5u,6u,10u,11u,13u,14u,23u,25u,28u,29u}) board.enhancement[i] = 1031127695;
    board.spinnerMask = UINT64_C(1) << fish; board.spinnerColors = 63;
    board.types[fish] = 4; board.minErase = 3;
    return board;
}
static PHOrbSolveOptions MonochromeReportedOptions(unsigned fish = 6) {
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.diagonal = true; options.fishCell = int(fish);
    options.allowPoisonWithHeartFour = true; options.thornComboPriority = true;
    options.goals = Goals(PHOrbGoalT,8);
    options.goals.items[options.goals.count++] = {PHOrbGoalFour,32,1,0};
    return options;
}
static bool MonochromeGroupIsT(const std::set<unsigned> &group,unsigned width) {
    if (group.size() != 5) return false;
    unsigned minX = width, minY = PHOrbMaxCells, maxX = 0, maxY = 0;
    for (unsigned i : group) {
        minX = std::min(minX,i % width); maxX = std::max(maxX,i % width);
        minY = std::min(minY,i / width); maxY = std::max(maxY,i / width);
    }
    if (maxX - minX != 2 || maxY - minY != 2) return false;
    // Independent coordinate descriptions, not production pattern masks.
    for (unsigned side = 0; side < 4; ++side) {
        std::set<unsigned> expected;
        for (unsigned offset = 0; offset < 3; ++offset) {
            if (side < 2) expected.insert((minY + (side ? 2 : 0)) * width + minX + offset);
            else expected.insert((minY + offset) * width + minX + (side == 3 ? 2 : 0));
        }
        if (side < 2) for (unsigned offset = 1; offset <= 2; ++offset)
            expected.insert((minY + (side ? 2 - offset : offset)) * width + minX + 1);
        else for (unsigned offset = 1; offset <= 2; ++offset)
            expected.insert((minY + 1) * width + minX + (side == 3 ? 2 - offset : offset));
        if (group == expected) return true;
    }
    return false;
}
static bool MonochromeGroupIsFour(const std::set<unsigned> &group,unsigned width) {
    if (group.size() != 4) return false;
    const unsigned first = *group.begin(); bool row = true, column = true;
    for (unsigned cell : group) { row &= cell / width == first / width; column &= cell % width == first % width; }
    return row || column;
}
static std::pair<unsigned,unsigned> MonochromeFirstHits(const PHOrbBoard &board,unsigned target = 3,unsigned donor = 5) {
    unsigned t = 0, four = 0;
    // Existing independent run scans and marked-only flood fill. The source
    // board is a concrete release color here, not a production phase oracle.
    for (const auto &group : AssistFishingFirstGroups(board)) {
        const unsigned color = board.types[*group.begin()];
        t += color == target && MonochromeGroupIsT(group,board.width);
        four += color == donor && MonochromeGroupIsFour(group,board.width);
    }
    return {t,four};
}
static PHOrbBoard MonochromeStaticLayout(const std::vector<unsigned> &target) {
    auto board = MonochromeReportedBoard(); board.spinnerMask = board.spinnerColors = 0;
    std::fill(board.types,board.types + 30,uint8_t(5));
    for (unsigned cell : target) board.types[cell] = 3;
    return board;
}
static void MonochromeBoundaryTests() {
    // Top-row 0..3 is a visible heart four, but row 1 has marked hearts at
    // 6..9, joining it to the larger heart removal. Only the light T is exact.
    auto connected = MonochromeStaticLayout({4,10,15,16,17});
    const auto connectedHits = MonochromeFirstHits(connected);
    Check(connectedHits.first == 1 && connectedHits.second == 0,
        "a local four-heart run joined to other marked hearts is not an exact four");
    PHOrbEvaluation result{}; auto options = MonochromeReportedOptions();
    Check(PHOrbEvaluate(connected,options.goals,result) && result.goalHits[0] == connectedHits.first &&
        result.goalHits[1] == connectedHits.second && !result.goalsMet,
        "the full two-clause evaluator agrees with marked-group geometry on the monochrome T counterexample");

    auto unmarked = connected; unmarked.types[11] = 3;
    Check(MonochromeFirstHits(unmarked).first == 1 && PHOrbEvaluate(unmarked,options.goals,result) && result.goalHits[0] == 1,
        "an unmarked sixth target beside the T does not enlarge its five-orb removal");
    auto merged = connected; merged.types[14] = 3;
    Check(MonochromeFirstHits(merged).first == 0 && PHOrbEvaluate(merged,options.goals,result) && !result.goalHits[0],
        "a sixth target extending the T bar is marked and invalidates the exact T");

    // 7 and 9 remain heart-colored beside top-row 0..3, but both have only
    // two-cell vertical runs and isolated horizontal runs. They are unmarked.
    auto border = MonochromeStaticLayout({4,6,8,10,13,15});
    const auto borderGroups = AssistFishingFirstGroups(border);
    Check(std::find(borderGroups.begin(),borderGroups.end(),std::set<unsigned>{0,1,2,3}) != borderGroups.end() &&
        border.types[7] == 5 && border.types[9] == 5 && MonochromeFirstHits(border).second == 1,
        "an exact heart four may have unmarked heart neighbors; a full nonheart perimeter is not necessary");
    Check(PHOrbEvaluate(border,options.goals,result) && !result.goalHits[0] && result.goalHits[1] == 1 && !result.goalsMet,
        "six separators forming an exact heart four do not by themselves supply a target T");

    // Four extracted targets could use the spinner as the fifth T cell only
    // in the light release phase. No native route or phase permission follows.
    auto conditional = MonochromeStaticLayout({10,15,16,17});
    for (unsigned release = 0; release < 6; ++release) {
        auto concrete = conditional; concrete.types[4] = uint8_t(release);
        Check((MonochromeFirstHits(concrete).first != 0) == (release == 3),
            "four fixed targets plus a spinner T cell are conditional on target-color release");
    }
    conditional.types[4] = 3; conditional.spinnerMask = UINT64_C(1) << 4; conditional.spinnerColors = 63;
    Check(PHOrbEvaluate(conditional,options.goals,result) && !result.goalHits[0] && !result.goalsMet,
        "a target-phase-only T never becomes an all-spinner-phase template result");
}
static void MonochromeProjectionBoundTests() {
    // Independent exhaustive static-layout oracle, to execute only in the
    // next authorized test run. No drag route, template helper or production
    // evaluator creates these layouts. The spinner's donor release phase is
    // enough to refute an all-phase guarantee for every source position.
    const unsigned w = 6, h = 5;
    for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x) {
        for (unsigned side = 0; side < 4; ++side) {
            std::vector<unsigned> t;
            for (unsigned k = 0; k < 3; ++k)
                t.push_back(side < 2 ? (y + (side ? 2 : 0)) * w + x + k :
                    (y + k) * w + x + (side == 3 ? 2 : 0));
            for (unsigned k = 1; k <= 2; ++k)
                t.push_back(side < 2 ? (y + (side ? 2-k : k)) * w + x + 1 :
                    (y + 1) * w + x + (side == 3 ? 2-k : k));
            const auto original = MonochromeStaticLayout(t);
            const auto initial = MonochromeFirstHits(original);
            Check(initial.first == 1 && initial.second == 0,
                "a T's three-column/three-row projection cannot isolate an exact donor four");
            for (unsigned cell = 0; cell < w*h; ++cell) {
                if (std::find(t.begin(),t.end(),cell) != t.end()) continue;
                for (unsigned color = 0; color < 6; ++color) {
                    if (color == 5) continue;
                    auto separated = original; separated.types[cell] = uint8_t(color);
                    const auto hits = MonochromeFirstHits(separated);
                    Check(!hits.first || !hits.second,
                        "no T plus one separator of any basic color supplies both exact first-wave groups");
                }
            }
        }
    }
}
static void MonochromeTemplateTests() {
    MonochromeBoundaryTests();
    const auto board = MonochromeReportedBoard(); const auto options = MonochromeReportedOptions();
    const auto eligible = [](const PHOrbBoard &b,const PHOrbSolveOptions &o) {
        return !std::strcmp(PHOrbMonochromeEligibilityReason(b,o),"eligible");
    };
    Check(eligible(board,options),"reported 29-heart board retains legal enhancement and original thorn/poison policies");
    auto changed = board; changed.spinnerMask |= UINT64_C(1) << 9;
    Check(!eligible(changed,options),"monochrome templates do not open a second spinner");
    changed = board; changed.types[0] = 0;
    Check(!eligible(changed,options),"ordinary multicolor boards retain the general planner");
    for (unsigned field = 0; field < 7; ++field) {
        changed = board;
        if (field == 0) changed.flags[0] = PHOrbFlagLocked;
        if (field == 1) { changed.flags[0] = PHOrbFlagThorn; changed.thornDamage[0] = 4; }
        if (field == 2) { changed.flags[0] = PHOrbFlagBlind; changed.blindTurns[0] = 1; }
        if (field == 3) { changed.tapeMask = 1; changed.flags[0] = PHOrbFlagTape; }
        if (field == 4) changed.cloudMask = 1;
        if (field == 5) changed.fixedStart = 1;
        if (field == 6) changed.unmatchableColors = 8;
        Check(!eligible(changed,options),"unreviewed locks, thorns, blind, tape, cloud, fixed start and unmatchable rules stay ineligible");
    }
    auto altered = options; altered.goals.items[0].colors = 32;
    Check(!eligible(board,altered),"target-T color must differ from the monochrome donor-four color");
    altered = options; altered.goals.items[altered.goals.count++] = {PHOrbGoalCombo,0,3,0};
    Check(!eligible(board,altered),"template planner cannot silently discard an additional goal");
    altered = options; altered.goals.items[0].count = 2;
    Check(!eligible(board,altered),"two target Ts are outside this bounded template family");
    altered = options; altered.activity.firstWaveOnly = true;
    Check(!eligible(board,altered),"activity policies cannot inherit ordinary monochrome template scoring");
    changed = board; changed.minErase = 4;
    Check(!eligible(changed,options),"nonstandard minimum runs cannot reuse three-run templates");
    changed = board; changed.spinnerColors = 55;
    Check(!eligible(changed,options),"a source table without the target color cannot supply a light T");

    const auto before = board;
    const auto details = PHOrbInspectMonochromeTemplate(board,options);
    Check(details.eligible && details.minimumExchanges == 7 &&
        !std::strcmp(details.reason,"exact-four-separator-bound") && !std::memcmp(&before,&board,sizeof(board)),
        "the 29-heart report exposes a seven-exchange necessary lower bound without mutating its snapshot");
    altered = options; std::swap(altered.goals.items[0],altered.goals.items[1]);
    Check(PHOrbInspectMonochromeTemplate(board,altered).minimumExchanges == 7,
        "the exact geometry bound follows goal semantics rather than saved goal order");
    for (unsigned fish = 0; fish < 30; ++fish) for (unsigned target = 0; target < 6; ++target) {
        if (target == 5) continue;
        auto fixture = MonochromeReportedBoard(fish); auto policy = MonochromeReportedOptions(fish);
        policy.goals.items[0].colors = uint8_t(1u << target);
        const auto bound = PHOrbInspectMonochromeTemplate(fixture,policy);
        Check(bound.eligible && bound.minimumExchanges == 7,
            "the necessary projection bound is independent of source cell and extracted target color");
    }
    for (unsigned donor = 0; donor < 6; ++donor) for (unsigned target = 0; target < 6; ++target) {
        if (donor == target) continue;
        auto fixture = board; std::fill(fixture.types,fixture.types + 30,uint8_t(donor)); fixture.types[6] = uint8_t(target);
        auto policy = options; policy.goals.items[0].colors = uint8_t(1u << target);
        policy.goals.items[1].colors = uint8_t(1u << donor);
        const auto bound = PHOrbInspectMonochromeTemplate(fixture,policy);
        Check(bound.eligible && bound.minimumExchanges == 7,
            "projection reasoning remains valid for every distinct basic donor/target color pair");
    }
    changed = board; changed.types[0] = 0;
    const auto unrelated = PHOrbInspectMonochromeTemplate(changed,options);
    Check(!unrelated.eligible && unrelated.minimumExchanges == 0,
        "a mixed fixed board does not inherit the monochrome impossibility bound");
    PHOrbTimedFishingTiming timing{};
    timing.remainingFrames = 390; timing.framesPerMove = 2;
    timing.spinnerPeriodFrames = 15; timing.settleFramesPerWait = 6; timing.inputAndCaptureFrames = 12;
    PHOrbMultiFishingResult out{}; std::atomic_bool cancel{false};
    const auto status = PHOrbSolveMultiFishing(board,options,nullptr,timing,PHOrbMultiFishingMaxExchanges,
        cancel,out,PHOrbMultiFishingMaxGeometryExtra);
    Check(status == PHOrbSolveStatus::NoRoute && out.exactGeometryBound && out.minimumExchangesLowerBound == 7 &&
        !out.route.count && !out.evaluated && !out.plansSearched && !out.evaluation.goalsMet &&
        !out.reservation.valid && !out.reservation.fits,
        "ordinary multi-fishing reports the proven six-take limit before futile geometry search or timing claims");
    cancel = true;
    out = {};
    Check(PHOrbSolveMultiFishing(board,options,nullptr,timing,PHOrbMultiFishingMaxExchanges,cancel,out,
        PHOrbMultiFishingMaxGeometryExtra) == PHOrbSolveStatus::Cancelled && !out.route.count &&
        !out.exactGeometryBound && !out.minimumExchangesLowerBound,
        "pre-cancellation retains priority over the newly added exact geometry diagnostic");
    MonochromeProjectionBoundTests();
}
