// Independent geometric lower bound: construct rotated stems/crossbars and
// horizontal/vertical fours using coordinates, not production pattern masks.
static unsigned TFourSeparatorOracle(unsigned w, unsigned h) {
    unsigned minimum = w * h + 1;
    for (unsigned rotation = 0; rotation < 4; ++rotation)
        for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x) {
            std::set<unsigned> t, border;
            for (auto local : {std::pair<unsigned,unsigned>{0,0}, {1,0}, {2,0}, {1,1}, {1,2}}) {
                unsigned px = local.first, py = local.second;
                for (unsigned r = 0; r < rotation; ++r) { unsigned old = px; px = 2 - py; py = old; }
                t.insert((y + py) * w + x + px);
            }
            for (unsigned i : t) for (unsigned j = 0; j < w * h; ++j)
                if (!t.count(j) && ((i / w == j / w && (i + 1 == j || j + 1 == i)) || i + w == j || j + w == i))
                    border.insert(j);
            for (unsigned axis = 0; axis < 2; ++axis)
                for (unsigned fy = 0; fy < h; ++fy) for (unsigned fx = 0; fx < w; ++fx) {
                    if (fx + (axis ? 0 : 3) >= w || fy + (axis ? 3 : 0) >= h) continue;
                    auto required = border; bool disjoint = true;
                    for (unsigned k = 0; k < 4; ++k) {
                        const unsigned cell = (fy + (axis ? k : 0)) * w + fx + (axis ? 0 : k);
                        disjoint &= !t.count(cell); required.insert(cell);
                    }
                    if (disjoint) minimum = std::min(minimum, unsigned(required.size()));
                }
        }
    return minimum;
}

static void ShapeSpaceTests() {
    auto board = ShapeBoard(5,4,{});
    const uint8_t cells[] = {5,3,3,3,3, 5,3,3,3,3, 3,5,5,5,3, 3,5,3,5,5};
    std::copy(std::begin(cells),std::end(cells),board.types);
    board.spinnerMask = UINT64_C(1) << 19; board.spinnerColors = 63;
    board.flags[14] = PHOrbFlagThorn; board.thornDamage[14] = 4;
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.goals = LightTHeartFourGoals(); options.goals.count = 2;
    options.diagonal = options.thornComboPriority = options.allowPoisonWithHeartFour = true;
    options.thorns = {465209,697814,0,true};
    PHOrbRoute route{}; std::atomic_bool cancel{false};
    Check(TFourSeparatorOracle(5,4) == 8,
        "a 5x4 exact T and distinct-color four require at least eight non-T cells even with arbitrary placement");
    Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::IncompatibleGoals &&
        !route.count && !route.evaluated && !route.evaluationCalls && !route.searchPasses,
        "recorded 12-light 5x4 board has only seven separators in its light spinner phase and skips futile beam search");
    // The lower bound must not reject a legal boundary case. This witness
    // uses eight fixed hearts; the bottom-right spinner cannot touch its T.
    auto witness = ShapeBoard(5,4,{});
    std::fill(witness.types,witness.types + 20,3);
    for (unsigned cell : {1u,2u,3u,4u,8u,11u,12u,15u}) witness.types[cell] = 5;
    witness.types[19] = 0; witness.spinnerMask = UINT64_C(1) << 19; witness.spinnerColors = 63;
    TapeExcept(witness,{6,7}); witness.fixedStart = 7; options.steps = 1; options.thorns = {};
    Check(PHOrbSolve(witness,options,cancel,route) == PHOrbSolveStatus::Solved && route.goalsMet,
        "eight-separator T/four witness survives every spinner phase and remains executable under the lower bound");
    PHOrbBoard replay{};
    Check(PHOrbReplay(&witness,&route,route.count - 1,&replay), "separator boundary witness returns a legal swap");
    for (unsigned phase = 0; phase < 6; ++phase) {
        auto variant = replay; variant.types[19] = uint8_t(phase);
        Check(ConservativeTOracle(variant,3) == 1 && FourOracle(variant,5) >= 1,
            "independent all-phase shape checks validate the minimum-separator witness");
    }
    options.goals.items[1] = {PHOrbGoalMatch,32,1,4}; options.steps = 1;
    Check(PHOrbSolve(board,options,cancel,route) != PHOrbSolveStatus::IncompatibleGoals,
        "the exact-four separator bound does not reject a different at-least-four goal");
}
