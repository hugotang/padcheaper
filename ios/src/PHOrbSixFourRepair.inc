// Included inside PHOrbSolver.cpp's anonymous namespace. Repair a complete
// candidate gesture, including its middle, instead of freezing its approach
// and only extending the tail. This is exploration, never a release contract.
bool SixFourRepairEligible(const PHOrbBoard &board, const PHOrbSolveOptions &options) {
    if (!options.fixedBoard.requireSixColorFours || options.fixedBoard.mode != PHOrbFixedBoardMode::FourAttack ||
        options.fixedBoard.firstColors != 63 || !options.fixedBoard.requireHeartFour ||
        !options.recoveryPasses || options.fishCell >= 0 ||
        options.fixedBoard.unmatchableRecoveryColors || board.unmatchableColors ||
        options.activity.firstWaveOnly || options.activity.preparation || options.activity.colors ||
        options.activity.comboPriority || (board.minErase && board.minErase != 3) || board.minMatch > 3 ||
        options.goals.count != PHOrbBasicTypes) return false;
    unsigned colors = 0;
    for (unsigned g = 0; g < options.goals.count; ++g) {
        const auto &goal = options.goals.items[g];
        if (goal.kind != PHOrbGoalFour || goal.count != 1 || !goal.colors ||
            (goal.colors & (goal.colors - 1)) || (colors & goal.colors)) return false;
        colors |= goal.colors;
    }
    if (colors != 63) return false;
    for (unsigned i = 0; i < unsigned(board.width) * board.height; ++i)
        if (board.types[i] >= PHOrbBasicTypes || (board.flags[i] &
            (PHOrbFlagThorn | PHOrbFlagNail | PHOrbFlagCombo))) return false;
    return true;
}

// Keep different missing-colour sets. A single best partial route can hide
// another layout whose missing four is much easier to repair in the middle.
class SixFourSeeds {
    std::vector<Node> _nodes;
    Bits _present = 0;
public:
    explicit SixFourSeeds(bool enabled) : _nodes(enabled ? 64 : 0) {}
    void offer(const Node &node, const Context &context) {
        const unsigned mask = node.score.result.firstWaveFourColors;
        if (_nodes.empty() || !mask || mask >= _nodes.size() || node.length < 2 || node.fished ||
            node.thornDamage || !node.score.result.hazardsSafe) return;
        const Bits bit = Bits(1) << mask;
        if (!(_present & bit) || Better(node,_nodes[mask],false,context.allowPoisonWithHeartFour,
                                       context.activity,true,context.fixedBoard)) {
            _nodes[mask] = node; _present |= bit;
        }
    }
    std::vector<Node> candidates(const Context &context, unsigned limit) const {
        std::vector<Node> result;
        for (unsigned mask = 1; mask < _nodes.size(); ++mask)
            if (_present & (Bits(1) << mask)) result.push_back(_nodes[mask]);
        std::sort(result.begin(),result.end(),[&](const Node &a, const Node &b) {
            return Better(a,b,false,context.allowPoisonWithHeartFour,context.activity,true,context.fixedBoard);
        });
        if (result.size() > limit) result.resize(limit);
        return result;
    }
};

// Replace a segment with a legal walk between the SAME endpoints, then replay
// the original suffix over the changed colours. No assumption that the suffix
// preserves matches: the visitor evaluates the entire resulting board again.
// Inserted loops and shorter replacements allow repairs even near 48 steps.
template <typename Visitor>
void VisitSixFourPatches(const Node &seed, const PHOrbSolveOptions &options, const Context &context,
    const std::atomic_bool &cancel, Clock::time_point deadline, Visitor &&visit) {
    if (seed.length < 2 || seed.length > options.steps + 1 || seed.fished || seed.thornDamage) return;
    struct Edit { unsigned removed, added; };
    constexpr Edit edits[] = {{2,2},{4,4},{6,6},{0,4},{2,4},{4,2},{4,6},{6,4},{6,8},{8,6}};
    const unsigned size = context.width * context.height;
    const auto interrupted = [&]() { return cancel.load(std::memory_order_relaxed) || Clock::now() >= deadline; };
    // Start in the middle and alternate towards both ends; a short deadline
    // must not always be spent on the same early prefix.
    for (unsigned offset = 0; offset < seed.length && !interrupted(); ++offset) {
        const unsigned begin = unsigned(int(seed.length / 2) +
            (offset & 1 ? -int((offset + 1) / 2) : int(offset / 2)));
        Cells prefix = seed.cells;
        for (unsigned i = seed.length - 1; i > begin; --i)
            std::swap(prefix[seed.path[i]],prefix[seed.path[i - 1]]);
        for (const auto &edit : edits) {
            if (interrupted()) return;
            const unsigned end = begin + edit.removed;
            if (end >= seed.length || seed.length - edit.removed + edit.added > options.steps + 1) continue;
            const unsigned target = seed.path[end];
            std::array<uint8_t,9> patch{}; patch[0] = seed.path[begin];
            Cells cells = prefix;
            unsigned visited = 0;
            bool stop = false;
            const auto walk = [&](auto &&self, unsigned depth) -> void {
                // Bound each window as well as the shared wall deadline, so
                // a branching diagonal window cannot consume every seed.
                if (stop || visited >= 1024 || interrupted()) return;
                ++visited;
                const unsigned current = patch[depth], remaining = edit.added - depth;
                const unsigned dx = unsigned(std::abs(int(current % context.width) - int(target % context.width)));
                const unsigned dy = unsigned(std::abs(int(current / context.width) - int(target / context.width)));
                if ((options.diagonal ? std::max(dx,dy) : dx + dy) > remaining ||
                    (!options.diagonal && ((dx + dy) & 1) != (remaining & 1))) return;
                if (!remaining) {
                    if (current != target || (edit.removed == edit.added &&
                        std::equal(patch.begin(),patch.begin() + edit.added + 1,seed.path.begin() + begin))) return;
                    Node candidate = seed;
                    candidate.length = uint8_t(seed.length - edit.removed + edit.added);
                    std::copy_n(patch.begin() + 1,edit.added,candidate.path.begin() + begin + 1);
                    std::copy(seed.path.begin() + end + 1,seed.path.begin() + seed.length,
                              candidate.path.begin() + begin + edit.added + 1);
                    candidate.cells = cells;
                    for (unsigned i = end + 1; i < seed.length; ++i)
                        std::swap(candidate.cells[seed.path[i - 1]],candidate.cells[seed.path[i]]);
                    // An identity edit offers no new layout or future endpoint.
                    // Keep shorter paths, which leave room for the next round.
                    if (candidate.length >= seed.length &&
                        std::equal(candidate.cells.begin(),candidate.cells.begin() + size,seed.cells.begin())) return;
                    stop = !visit(candidate);
                    return;
                }
                const int width = int(context.width);
                const int neighbors[] = {int(current)-width,int(current)+width,int(current)-1,int(current)+1,
                    int(current)-width-1,int(current)-width+1,int(current)+width-1,int(current)+width+1};
                for (unsigned d = 0; d < (options.diagonal ? 8u : 4u) && !stop && visited < 1024; ++d) {
                    const int next = neighbors[d];
                    if (next < 0 || !PHOrbMoveAdjacent(current,unsigned(next),context.width,context.height,options.diagonal) ||
                        (context.blocked & (Bits(1) << unsigned(next))) ||
                        (depth && unsigned(next) == patch[depth - 1])) continue;
                    patch[depth + 1] = uint8_t(next);
                    std::swap(cells[current],cells[unsigned(next)]);
                    self(self,depth + 1);
                    std::swap(cells[current],cells[unsigned(next)]);
                }
            };
            walk(walk,0);
            if (stop) return;
        }
    }
}

void RepairSixFours(const PHOrbSolveOptions &options, const Context &context, SixFourSeeds &seeds,
    const std::atomic_bool &cancel, Clock::time_point deadline, Node &best, EvaluationCache &cache,
    unsigned &evaluated, unsigned &exploredDepth, unsigned &passes, unsigned &repairPasses,
    unsigned &repairCandidates) {
    for (unsigned round = 0; round < 2 && !best.score.result.goalsMet; ++round) {
        if (cancel.load(std::memory_order_relaxed) || Clock::now() >= deadline) return;
        auto candidates = seeds.candidates(context,std::min(8u,options.beam));
        if (candidates.empty()) return;
        ++passes; ++repairPasses; cache.nextPass();
        for (unsigned i = 0; i < candidates.size(); ++i) {
            const auto now = Clock::now();
            if (cancel.load(std::memory_order_relaxed) || now >= deadline) return;
            // Share remaining time among every retained seed and both rounds.
            const auto slice = now + (deadline - now) / ((candidates.size() - i) * (2 - round));
            VisitSixFourPatches(candidates[i],options,context,cancel,slice,[&](Node &candidate) {
                candidate.score = cache.score(candidate.cells,context);
                ++evaluated; ++repairCandidates;
                exploredDepth = std::max(exploredDepth,unsigned(candidate.length - 1));
                if (Better(candidate,best,false,context.allowPoisonWithHeartFour,context.activity,true,context.fixedBoard))
                    best = candidate;
                seeds.offer(candidate,context);
                return !best.score.result.goalsMet;
            });
            if (best.score.result.goalsMet) return;
        }
    }
}
