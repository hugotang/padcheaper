// Independent graph and scoring regressions for thorn-aware colour guidance.
// Distances constrain hints, never exact phase evaluation or route admission.
static void ThornColorGuidanceTests() {
    auto board = ColorSpinnerGuidanceBoard();
    const uint8_t types[] = {0,5,0,3,5,4, 3,4,2,4,2,3, 1,2,4,2,1,1,
        2,1,5,2,3,4, 0,5,3,2,2,5};
    std::copy(std::begin(types),std::end(types),board.types);
    board.spinnerMask = (UINT64_C(1) << 7) | (UINT64_C(1) << 10) |
        (UINT64_C(1) << 19) | (UINT64_C(1) << 22);
    board.spinnerColors = 63;
    Bits thorns = 0;
    for (unsigned i : {1u,5u,11u,13u,15u,17u,20u,23u}) {
        board.flags[i] = PHOrbFlagThorn; board.thornDamage[i] = 4;
        thorns |= UINT64_C(1) << i;
    }
    auto context = MakeContext(board,ColorHeartGoals(),true,false,true); context.colorGuidance = true;
    const auto stationary = MakeColorMovement(context,thorns);
    const auto heldWater = MakeColorMovement(context,thorns & ~(UINT64_C(1) << 17));
    const auto heldWood = MakeColorMovement(context,thorns & ~(UINT64_C(1) << 15));
    Check((stationary.blocked & thorns) == thorns && !(heldWater.blocked & (UINT64_C(1) << 17)) &&
        (heldWater.blocked & (thorns & ~(UINT64_C(1) << 17))) == (thorns & ~(UINT64_C(1) << 17)),
        "only the initially held thorn becomes movable in its zero-contact recovery graph");
    for (const auto *movement : {&stationary,&heldWater,&heldWood}) {
        for (unsigned from = 0; from < 30; ++from) {
            std::array<unsigned,PHOrbMaxCells> distance{}; distance.fill(PHOrbMaxCells);
            std::vector<unsigned> queue;
            if (!(movement->blocked & (UINT64_C(1) << from))) { distance[from] = 0; queue.push_back(from); }
            for (size_t head = 0; head < queue.size(); ++head) {
                const unsigned at = queue[head];
                for (unsigned to = 0; to < 30; ++to) {
                    const int dx = std::abs(int(at % 6) - int(to % 6));
                    const int dy = std::abs(int(at / 6) - int(to / 6));
                    if ((!dx && !dy) || dx > 1 || dy > 1 ||
                        (movement->blocked & (UINT64_C(1) << to)) || distance[to] != PHOrbMaxCells) continue;
                    distance[to] = distance[at] + 1; queue.push_back(to);
                }
            }
            for (unsigned depth = 1; depth < 30; ++depth) {
                Bits expected = 0;
                for (unsigned to = 0; to < 30; ++to) if (distance[to] == depth) expected |= UINT64_C(1) << to;
                Check(movement->distanceMasks[from][depth] == expected,
                    "thorn-aware hint distances agree with independent explicit adjacency traversal");
            }
        }
    }
    // The thorn water at17 is movable only when held. Starting at15 instead
    // can free a hole beside stationary water17; fixed colour is not discarded.
    const Bits water = (UINT64_C(1) << 12) | (UINT64_C(1) << 16) | (UINT64_C(1) << 17);
    const std::vector<Pattern> topRun{{(UINT64_C(1) << 0) | (UINT64_C(1) << 1) | (UINT64_C(1) << 2),0}};
    const std::vector<Pattern> middleRun{{(UINT64_C(1) << 15) | (UINT64_C(1) << 16) | (UINT64_C(1) << 17),0}};
    Check(!RepairColor(water,topRun,1,context,&heldWater).value,
        "a hole occupied by a different stationary thorn cannot be repaired by a colour hint");
    Check(!RepairColor(water,middleRun,1,context,&stationary).value &&
        RepairColor(water,middleRun,1,context,&heldWood).value > 0,
        "holding wood15 frees its hole while stationary matching water17 can remain part of a valid template");
    const std::vector<Pattern> openRun{{(UINT64_C(1) << 24) | (UINT64_C(1) << 25) | (UINT64_C(1) << 26),0}};
    Check(!RepairColor(water,openRun,1,context,&stationary).value &&
        RepairColor(water,openRun,1,context,&heldWater).value > 0,
        "a three-water repair cannot borrow stationary water17 unless that token is initially held");

    const auto cells = BoardCells(board); const auto exact = Evaluate(cells,context);
    std::array<Bits,PHOrbTypes> colors{};
    for (unsigned i = 0; i < 30; ++i) colors[cells[i]] |= UINT64_C(1) << i;
    EvaluationCache cache(true); cache.nextPass();
    for (const auto *movement : {&stationary,&heldWater,&heldWood}) {
        auto score = cache.score(cells,context);
        ColorGuidance(score,colors,context,movement);
        Check(SameEvaluation(score.result,exact.result) && score.progress == exact.progress &&
            score.goalProgress == exact.goalProgress && score.matchedColors == exact.matchedColors,
            "per-start movement hints leave all exact results, canonical progress and phase aggregates unchanged");
        Check(SameScore(cache.score(cells,context),exact),
            "a route-specific thorn hint cannot contaminate the shared colour-layout cache");
    }
}
