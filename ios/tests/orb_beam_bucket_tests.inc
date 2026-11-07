// Exercise the real selection path with evaluator-produced colour scores.
// A completed colour clause leaves weakestColor at the no-missing sentinel,
// even when an independent Combo clause keeps the whole request incomplete.
static PHOrbBoard BeamBucketBoard(bool spinners) {
    PHOrbBoard board{}; board.width = 7; board.height = 6;
    for (unsigned i = 0; i < 42; ++i) {
        board.types[i] = uint8_t(i / board.width); board.tokens[i] = i + 1;
    }
    if (spinners) {
        board.spinnerColors = 63;
        for (unsigned row = 0; row < 4; ++row)
            board.spinnerMask |= UINT64_C(1) << (row * board.width + 6);
    }
    return board;
}

static bool SameBeamNode(const Node &a, const Node &b) {
    return a.cells == b.cells && a.thorns == b.thorns && a.path == b.path &&
        a.thornDamage == b.thornDamage && a.thornContacts == b.thornContacts &&
        a.length == b.length && a.fished == b.fished && SameScore(a.score,b.score);
}

static size_t BeamNodeIndex(const std::vector<Node> &nodes, const Node &candidate) {
    const auto found = std::find_if(nodes.begin(),nodes.end(),[&](const Node &node) {
        return SameBeamNode(node,candidate);
    });
    return size_t(found - nodes.begin());
}

static std::vector<Node> BeamBucketCandidates(const PHOrbBoard &board, const Score &score) {
    std::vector<Node> nodes;
    // Straight walks through equal-colour fixed cells preserve the evaluated
    // board. Different starts/end regions provide real layout diversity, while
    // longer walks also provide distinct candidates with identical layout keys.
    for (unsigned row = 0; row < board.height; ++row) for (unsigned length = 2; length <= 6; ++length) {
        for (unsigned left = 0; left + length <= 6; ++left) for (bool reverse : {false,true}) {
            PHOrbRoute route{}; route.count = uint8_t(length);
            for (unsigned i = 0; i < length; ++i)
                route.cells[i] = uint8_t(row * board.width + left + (reverse ? length - 1 - i : i));
            PHOrbBoard replay{};
            const bool valid = PHOrbReplay(&board,&route,length - 1,&replay);
            Check(valid && BoardCells(replay) == BoardCells(board),
                "beam bucket fixtures are legal fixed-cell walks that preserve all evaluated colours");
            if (!valid) continue;
            Node node; node.cells = BoardCells(replay); node.score = score; node.length = route.count;
            std::copy(route.cells,route.cells + route.count,node.path.begin());
            nodes.push_back(node);
        }
    }
    return nodes;
}

static void CheckBeamSelection(const std::vector<Node> &candidates, const Context &context,
                               unsigned limit, unsigned depth, bool saveDiscarded) {
    const unsigned focus = PHOrbMaxGoals;
    auto ranked = candidates;
    std::sort(ranked.begin(),ranked.end(),[&](const Node &a, const Node &b) {
        return BetterForSearch(a,b,focus,false,context.allowPoisonWithHeartFour,context);
    });
    const size_t keep = std::min(size_t(limit),candidates.size());
    const bool bucketPath = keep >= 4 && keep < candidates.size();
    const size_t elite = bucketPath ? keep - keep / 4 : keep;
    std::unordered_set<uint64_t> eliteLayouts, allLayouts;
    for (size_t i = 0; i < ranked.size(); ++i) {
        const auto key = LayoutKey(ranked[i],context); allLayouts.insert(key);
        if (i < elite) eliteLayouts.insert(key);
    }
    Check(!bucketPath || allLayouts.size() >= eliteLayouts.size() + keep - elite,
        "the sentinel bucket fixture offers enough distinct layouts for every reserved selection");
    if (bucketPath) {
        Check(ranked[elite].score.weakestColor == PHOrbBasicTypes && !ranked[elite].score.missingColors,
            "a real no-missing-colour sentinel reaches the bucket-building loop beyond the elite prefix");
    }

    std::vector<Node> previous, previousDiscarded;
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        auto next = candidates;
        if (repeat == 1) std::reverse(next.begin(),next.end());
        if (repeat == 2) std::rotate(next.begin(),next.begin() + next.size() / 3,next.end());
        // Prefill the output with the prior selection to exercise clearing;
        // the per-pass discarded collection starts empty, as at real call sites.
        auto beam = previous; std::vector<Node> discarded;
        SelectBeam(next,beam,limit,depth,context,focus,false,saveDiscarded ? &discarded : nullptr);
        Check(beam.size() == keep,"sentinel selection fills the requested bounded beam");
        std::vector<bool> seen(candidates.size());
        std::unordered_set<uint64_t> selectedLayouts;
        for (const auto &node : beam) {
            const size_t index = BeamNodeIndex(candidates,node);
            Check(index < candidates.size(),"selection preserves an input node's route, board and complete score");
            if (index < candidates.size()) {
                Check(!seen[index],"selection never duplicates an input candidate"); seen[index] = true;
            }
            selectedLayouts.insert(LayoutKey(node,context));
        }
        for (size_t i = 0; i < elite; ++i)
            Check(BeamNodeIndex(beam,ranked[i]) < beam.size(),"selection retains every normally ranked elite candidate");
        Check(!bucketPath || selectedLayouts.size() >= eliteLayouts.size() + keep - elite,
            "the no-missing-colour bucket still fills each reserved slot with a new layout");
        Check(discarded.size() <= std::min(size_t(128),keep),"discarded alternatives retain their existing cap");
        Check(!saveDiscarded || keep == candidates.size() || !discarded.empty(),
            "an optional discarded pool receives real unselected sentinel candidates");
        std::unordered_set<uint64_t> discardedLayouts;
        for (const auto &node : discarded) {
            const size_t index = BeamNodeIndex(candidates,node);
            Check(index < candidates.size(),"discarded alternatives preserve complete input candidates");
            if (index < candidates.size()) {
                Check(!seen[index],"discarded alternatives are disjoint from the beam and each other"); seen[index] = true;
            }
            Check(discardedLayouts.insert(LayoutKey(node,context)).second,
                "discarded alternatives retain distinct layout keys");
        }
        if (repeat) {
            Check(beam.size() == previous.size() &&
                std::equal(beam.begin(),beam.end(),previous.begin(),SameBeamNode),
                "reordered inputs and repeated calls preserve the deterministic selected order");
            Check(discarded.size() == previousDiscarded.size() &&
                std::equal(discarded.begin(),discarded.end(),previousDiscarded.begin(),SameBeamNode),
                "repeated calls preserve deterministic discarded alternatives without stale links");
        }
        previous = beam; previousDiscarded = discarded;
    }
}

static void BeamBucketTests() {
    for (bool spinners : {false,true}) for (unsigned request = 0; request < 3; ++request) {
        const auto board = BeamBucketBoard(spinners);
        auto goals = request ? ColorHeartGoals() : Goal(PHOrbGoalColors,63,6);
        if (request == 2) goals.items[goals.count++] = {PHOrbGoalCombo,0,7,0};
        Check(PHOrbBoardValid(&board) && PHOrbGoalsValid(&goals),
            "complete-colour and Combo-incomplete bucket fixtures use valid boards and goals");
        auto context = MakeContext(board,goals); context.colorGuidance = true;
        Check(context.colorRecovery && !context.shapeGoals && context.variants == (spinners ? 6u : 1u),
            "bucket fixtures use ordinary colour recovery and the real shared four-spinner phase context");
        const auto score = Evaluate(BoardCells(board),context);
        Check(score.result.hazardsSafe && score.result.combos == 6 && score.matchedColors == 63 &&
            score.missingColors == 0 && score.weakestColor == PHOrbBasicTypes,
            "real evaluation leaves the no-missing sentinel after matching all six colours in every phase");
        Check(score.result.goalsMet == (request != 2) &&
            score.result.matchedGoals == goals.count - (request == 2 ? 1u : 0u),
            "the sentinel occurs both for fully met requests and when only the Combo goal remains unmet");
        if (request == 2)
            Check(score.result.goalHits[2] == 6 && score.missing == (1u << 2),
                "the unmet Combo clause does not invent a missing colour");
        const auto candidates = BeamBucketCandidates(board,score);
        Check(candidates.size() > 32,"enough sentinel candidates survive beyond the largest tested elite prefix");
        // 3 and full-size use the existing partial-sort branch; the others
        // exercise linked buckets with and without an output alternatives pool.
        for (unsigned limit : {3u,4u,8u,32u,unsigned(candidates.size())})
            for (unsigned depth : {1u,4u})
                CheckBeamSelection(candidates,context,limit,depth,(limit + depth) % 2 != 0);
    }
}
