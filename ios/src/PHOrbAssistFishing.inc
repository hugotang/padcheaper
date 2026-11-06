// Bounded offline multi-take recovery. No Session or native-input entry point.
namespace {
using AssistCounts = std::array<uint8_t, PHOrbBasicTypes>;
bool AssistFishingInputValid(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, unsigned limit) {
    if (!TimedFishingBoardValid(board, options.fishCell) || !PHOrbGoalsValid(&options.goals) ||
        !TimedFishingActivitySupported(options.activity) || limit > PHOrbAssistFishingMaxExchanges ||
        !recovery.valid || !recovery.turns || (recovery.dungeonFlags & 8u) ||
        (board.minErase && board.minErase != 3) || board.minMatch > 5) return false;
    for (unsigned c = 0; c < 5; ++c)
        if (recovery.perColor[c] && (!recovery.providers[c] || (recovery.providers[c] & ~63u))) return false;
    return true;
}
unsigned AssistExistingL(const PHOrbGoalSet &goals, unsigned color) {
    for (unsigned g = 0; g < goals.count; ++g)
        if (goals.items[g].kind == PHOrbGoalL && goals.items[g].colors == (1u << color)) return goals.items[g].count;
    return 0;
}
bool AssistGoalsEqual(const PHOrbGoalSet &a, const PHOrbGoalSet &b) {
    if (a.count != b.count || a.comboFirstWave != b.comboFirstWave || a.bonusFirstWave != b.bonusFirstWave) return false;
    for (unsigned g = 0; g < a.count; ++g)
        if (a.items[g].kind != b.items[g].kind || a.items[g].colors != b.items[g].colors ||
            a.items[g].count != b.items[g].count || a.items[g].length != b.items[g].length) return false;
    return true;
}
bool AssistBuildGoals(const PHOrbBoard &board, const PHOrbGoalSet &base,
    const PHOrbAssistRecoveryState &recovery, const uint8_t counts[5], PHOrbAssistFishingPlan &plan, bool &goalLimit) {
    plan = {}; plan.goals = base;
    for (unsigned c = 0; c < 5; ++c) {
        const unsigned existing = AssistExistingL(base,c), count = counts[c];
        const bool eligible = recovery.perColor[c] && !(board.unmatchableColors & (1u << c));
        if (count < existing || count > unsigned(board.width) * board.height / 5 || (count > existing && !eligible)) return false;
        plan.lCounts[c] = uint8_t(count);
        if (eligible && count) {
            plan.predictedTurns += count * recovery.perColor[c]; plan.providers |= recovery.providers[c];
        }
        if (count <= existing) continue;
        if (existing) {
            for (unsigned g = 0; g < plan.goals.count; ++g)
                if (plan.goals.items[g].kind == PHOrbGoalL && plan.goals.items[g].colors == (1u << c))
                    plan.goals.items[g].count = uint8_t(count);
        } else {
            if (plan.goals.count == PHOrbMaxGoals) { goalLimit = true; return false; }
            plan.goals.items[plan.goals.count++] = {PHOrbGoalL,uint8_t(1u << c),uint8_t(count),0};
        }
    }
    if (plan.predictedTurns < recovery.turns) return false;
    unsigned required = 0;
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) required += PHOrbRequiredShapeOrbs(&plan.goals,c);
    return required <= unsigned(board.width) * board.height - 1 && PHOrbGoalsValid(&plan.goals);
}
uint64_t AssistInventoryKey(const Inventory &fixed) {
    uint64_t key = 0;
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) key |= uint64_t(fixed[c]) << (6 * c);
    return key;
}
uint32_t AssistAllocationKey(const PHOrbAssistFishingPlan &plan) {
    uint32_t key = 0;
    for (unsigned c = 0; c < 5; ++c) key |= uint32_t(plan.lCounts[c]) << (4 * c);
    return key;
}
bool AssistInventoryPasses(const Inventory &fixed, const PHOrbBoard &board, const Context &context, unsigned &upper) {
    upper = PHOrbMaxCells;
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) if (board.spinnerColors & (1u << c)) {
        auto phase = fixed; ++phase[c]; unsigned phaseUpper = 0;
        if (!TimedInventoryPasses(phase,context,phaseUpper)) return false;
        upper = std::min(upper,phaseUpper);
    }
    return true;
}
PHOrbAssistFishingInventory AssistInventoryPlans(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, unsigned limit, const std::atomic_bool *cancel = nullptr,
    Clock::time_point deadline = Clock::time_point::max()) {
    PHOrbAssistFishingInventory out; out.valid = out.enumerationComplete = true;
    const auto interrupted = [&]() {
        out.deadlineReached |= Clock::now() >= deadline;
        if ((cancel && cancel->load(std::memory_order_relaxed)) || out.deadlineReached) {
            out.enumerationComplete = false; return true;
        }
        return false;
    };
    Inventory original{};
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (i != unsigned(options.fishCell)) ++original[board.types[i]];
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) out.fixed[c] = uint8_t(original[c]);
    unsigned existing[5]{}, capacity[5]{}; uint8_t counts[5]{};
    for (unsigned c = 0; c < 5; ++c) {
        existing[c] = AssistExistingL(options.goals,c);
        const unsigned reserved = PHOrbRequiredShapeOrbs(&options.goals,c) - 5 * existing[c];
        const bool eligible = recovery.perColor[c] && !(board.unmatchableColors & (1u << c));
        const unsigned possible = original[c] + ((board.spinnerColors & (1u << c)) ? limit : 0);
        capacity[c] = eligible && possible >= reserved ? std::min((possible - reserved) / 5,
            (unsigned(board.width) * board.height - 1) / 5) : existing[c];
    }
    struct Allocation { PHOrbAssistFishingPlan plan; unsigned shortage = 0, groups = 0; };
    std::vector<Allocation> allocations;
    const auto allocate = [&](auto &&self, unsigned color, unsigned groups) -> void {
        if (interrupted() || groups * 5 > unsigned(board.width) * board.height - 1) return;
        if (color < 5) {
            for (unsigned n = existing[color]; n <= capacity[color]; ++n) {
                counts[color] = uint8_t(n); self(self,color + 1,groups + n);
                if (!out.enumerationComplete) return;
            }
            return;
        }
        uint32_t amount = 0;
        for (unsigned c = 0; c < 5; ++c)
            if (!(board.unmatchableColors & (1u << c))) amount += counts[c] * recovery.perColor[c];
        if (amount < recovery.turns) return;
        for (unsigned c = 0; c < 5; ++c)
            if (counts[c] > existing[c] && amount - recovery.perColor[c] >= recovery.turns) return;
        Allocation a; a.groups = groups;
        if (!AssistBuildGoals(board,options.goals,recovery,counts,a.plan,out.goalLimit)) return;
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
            const unsigned required = PHOrbRequiredShapeOrbs(&a.plan.goals,c);
            if (required > original[c]) {
                if (!(board.spinnerColors & (1u << c))) return;
                a.shortage += required - original[c];
            }
        }
        if (a.shortage <= limit) allocations.push_back(a);
    };
    allocate(allocate,0,0);
    std::stable_sort(allocations.begin(),allocations.end(),[](const auto &a, const auto &b) {
        if (a.shortage != b.shortage) return a.shortage < b.shortage;
        if (a.groups != b.groups) return a.groups < b.groups;
        return a.plan.predictedTurns < b.plan.predictedTurns;
    });
    std::vector<PHOrbAssistFishingPlan> feasible;
    for (size_t a = 0; a < allocations.size(); ++a) {
        if (interrupted()) break;
        const auto &allocation = allocations[a];
        auto trial = options; trial.goals = allocation.plan.goals;
        const auto context = TimedFishingContext(board,trial,false);
        unsigned required[PHOrbBasicTypes]{};
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) required[c] = PHOrbRequiredShapeOrbs(&trial.goals,c);
        // Divide the remaining state allowance among L choices. Breadth-first
        // expansion tries fewer transfers before spending it on longer plans.
        const unsigned remainingStates = PHOrbAssistFishingMaxInventoryStates - out.visitedStates;
        if (!remainingStates) { out.stateLimitReached = true; out.enumerationComplete = false; break; }
        const unsigned allowance = std::max(1u,unsigned(remainingStates / (allocations.size() - a)));
        struct State { Inventory fixed; unsigned depth; };
        std::vector<State> pending{{original,0}};
        std::unordered_set<uint64_t> visited{AssistInventoryKey(original)};
        for (size_t cursor = 0; cursor < pending.size() && !interrupted(); ++cursor) {
            const auto state = pending[cursor]; const auto &fixed = state.fixed; const unsigned depth = state.depth;
            ++out.visitedStates;
            int deficit = -1; unsigned missing = 0;
            for (unsigned c = 0; c < PHOrbBasicTypes; ++c) if (fixed[c] < required[c]) {
                missing += required[c] - fixed[c];
                if (deficit < 0) deficit = int(c);
            }
            if (depth + missing > limit) continue;
            unsigned comboUpper = 0;
            if (deficit < 0 && AssistInventoryPasses(fixed,board,context,comboUpper)) {
                auto plan = allocation.plan; plan.exchangeCount = uint8_t(depth); plan.comboUpper = comboUpper;
                for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
                    plan.take[c] = uint8_t(fixed[c] > original[c] ? fixed[c] - original[c] : 0);
                    plan.leave[c] = uint8_t(fixed[c] < original[c] ? original[c] - fixed[c] : 0);
                }
                feasible.push_back(plan); continue;
            }
            if (depth == limit) continue;
            for (unsigned take = 0; take < PHOrbBasicTypes; ++take) {
                if ((deficit >= 0 && take != unsigned(deficit)) || !(board.spinnerColors & (1u << take)) ||
                    fixed[take] < original[take]) continue;
                for (unsigned leave = 0; leave < PHOrbBasicTypes; ++leave) {
                    if (take == leave || !(board.spinnerColors & (1u << leave)) ||
                        fixed[leave] <= required[leave] || fixed[leave] > original[leave]) continue;
                    auto next = fixed; --next[leave]; ++next[take];
                    const auto key = AssistInventoryKey(next);
                    if (visited.find(key) != visited.end()) continue;
                    if (pending.size() >= allowance) {
                        out.stateLimitReached = true; out.enumerationComplete = false; continue;
                    }
                    visited.insert(key); pending.push_back({next,depth + 1});
                }
            }
        }
    }
    std::array<std::vector<unsigned>,PHOrbBasicTypes> distances;
    const unsigned fish = unsigned(options.fishCell), width = board.width;
    for (unsigned cell = 0; cell < board.width * board.height; ++cell)
        if (cell != fish && !(board.tapeMask & (Bits(1) << cell))) {
            const unsigned dx = unsigned(std::abs(int(cell % width) - int(fish % width)));
            const unsigned dy = unsigned(std::abs(int(cell / width) - int(fish / width)));
            distances[board.types[cell]].push_back(options.diagonal ? std::max(dx,dy) : dx + dy);
        }
    for (auto &color : distances) std::sort(color.begin(),color.end());
    const auto supplyCost = [&](const auto &plan) {
        unsigned cost = 0;
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) for (unsigned i = 0; i < plan.leave[c]; ++i)
            cost += i < distances[c].size() ? distances[c][i] : unsigned(PHOrbMaxCells);
        return cost;
    };
    std::stable_sort(feasible.begin(),feasible.end(),[&](const auto &a, const auto &b) {
        if (a.exchangeCount != b.exchangeCount) return a.exchangeCount < b.exchangeCount;
        // All hard goals already passed the inventory bound. Prefer nearby
        // distinct donors before optional Combo so taped supplies cannot crowd
        // out an easy five-take plan. Distance remains ranking, not reachability.
        if (supplyCost(a) != supplyCost(b)) return supplyCost(a) < supplyCost(b);
        if (a.comboUpper != b.comboUpper) return a.comboUpper > b.comboUpper;
        return a.predictedTurns < b.predictedTurns;
    });
    out.feasiblePlans = unsigned(feasible.size());
    if (!feasible.empty()) out.minimumExchanges = feasible.front().exchangeCount;
    std::unordered_set<uint32_t> chosenAllocations; std::vector<bool> selected(feasible.size());
    // One plan per L allocation before alternate sacrifices consume the cap.
    for (size_t p = 0; p < feasible.size() && out.count < PHOrbAssistFishingMaxPlans; ++p)
        if (chosenAllocations.insert(AssistAllocationKey(feasible[p])).second) {
            out.plans[out.count++] = feasible[p]; selected[p] = true;
        }
    for (size_t p = 0; p < feasible.size() && out.count < PHOrbAssistFishingMaxPlans; ++p)
        if (!selected[p]) out.plans[out.count++] = feasible[p];
    out.selectionTruncated = out.count < out.feasiblePlans;
    interrupted();
    return out;
}
bool AssistTimingValid(const PHOrbTimedFishingTiming &timing) {
    return timing.remainingFrames && timing.framesPerMove && timing.settleFramesPerWait &&
        timing.inputAndCaptureFrames && timing.spinnerPeriodFrames;
}
PHOrbTimedFishingReservation AssistReserve(const PHOrbBoard &board, unsigned moves, unsigned exchanges,
    const PHOrbTimedFishingTiming &timing) {
    if (!AssistTimingValid(timing) || exchanges > PHOrbAssistFishingMaxExchanges) return {};
    const uint64_t move = uint64_t(moves) * timing.framesPerMove;
    const uint64_t wait = uint64_t(exchanges) * PHOrbSpinnerVariants(&board) * timing.spinnerPeriodFrames;
    const uint64_t settle = uint64_t(exchanges + 1) * timing.settleFramesPerWait;
    const uint64_t required = move + wait + settle + timing.inputAndCaptureFrames;
    if (required > UINT32_MAX) return {};
    return {true,required < timing.remainingFrames,uint32_t(move),uint32_t(wait),0,uint32_t(settle),uint32_t(required)};
}
bool AssistPlanInventoryValid(const PHOrbBoard &board, const PHOrbAssistFishingRoute &route) {
    unsigned takes = 0, leaves = 0; unsigned fixed[PHOrbBasicTypes]{};
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (i != route.fishCell) ++fixed[board.types[i]];
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
        const unsigned take = route.plan.take[c], leave = route.plan.leave[c];
        if ((take && leave) || leave > fixed[c] || ((take || leave) && !(board.spinnerColors & (1u << c)))) return false;
        takes += take; leaves += leave;
    }
    return takes == route.exchangeCount && leaves == takes && route.plan.exchangeCount == takes;
}
struct AssistFishingNode {
    Node drag;
    AssistCounts takeLeft{}, leaveLeft{};
    uint8_t entries = 0;
    PHOrbTimedFishingStep exchanges[PHOrbAssistFishingMaxExchanges]{};
};
uint64_t AssistFishingHash(const AssistFishingNode &node, unsigned size) {
    uint64_t key = Hash(node.drag,size);
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
        key ^= unsigned(node.takeLeft[c]) | (unsigned(node.leaveLeft[c]) << 8); key *= UINT64_C(1099511628211);
    }
    return (key ^ node.entries) * UINT64_C(1099511628211);
}
void SelectAssistFishingBeam(std::vector<AssistFishingNode> &next, std::vector<AssistFishingNode> &beam,
    unsigned limit, unsigned fish, const PHOrbSolveOptions &options) {
    const size_t keep = std::min(size_t(limit),next.size());
    std::stable_sort(next.begin(),next.end(),[&](const auto &a, const auto &b) {
        return Better(a.drag,b.drag,false,options.allowPoisonWithHeartFour,{});
    });
    if (keep < 4 || keep == next.size()) { next.resize(keep); beam.swap(next); return; }
    beam.clear(); const size_t elite = keep / 2;
    for (size_t p = 0; p < elite; ++p) beam.push_back(next[p]);
    std::array<std::vector<size_t>,2 * (PHOrbAssistFishingMaxExchanges + 1)> lanes;
    for (size_t p = elite; p < next.size(); ++p) {
        const auto &node = next[p];
        lanes[2 * node.entries + (node.drag.path[node.drag.length - 1] == fish)].push_back(p);
    }
    for (size_t row = 0; beam.size() < keep; ++row) {
        bool added = false;
        for (const auto &lane : lanes) if (row < lane.size() && beam.size() < keep) {
            beam.push_back(next[lane[row]]); added = true;
        }
        if (!added) break;
    }
}
PHOrbAssistFishingRoute AssistRoute(const AssistFishingNode &node, const PHOrbAssistFishingPlan &plan,
    unsigned fish, bool diagonal) {
    PHOrbAssistFishingRoute route;
    route.count = node.drag.length; route.fishCell = uint8_t(fish); route.diagonal = diagonal;
    route.exchangeCount = node.entries; route.plan = plan;
    std::copy(node.drag.path.begin(),node.drag.path.begin() + route.count,route.cells);
    std::copy(node.exchanges,node.exchanges + route.exchangeCount,route.exchanges);
    return route;
}
}

PHOrbAssistFishingInventory PHOrbPlanAssistFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, unsigned maxExchanges) noexcept {
    if (!AssistFishingInputValid(board,options,recovery,maxExchanges)) return {};
    try { return AssistInventoryPlans(board,options,recovery,maxExchanges); }
    catch (...) { return {}; }
}

bool PHOrbReplayAssistFishing(const PHOrbBoard &board, const PHOrbAssistFishingRoute &route,
    unsigned through, PHOrbBoard &result) noexcept {
    const auto initial = board; result = {};
    if (!TimedFishingBoardValid(initial,route.fishCell) || !PHOrbGoalsValid(&route.plan.goals) ||
        route.count < 2 || route.count > PHOrbMaxSteps + 1 || through >= route.count ||
        route.exchangeCount > PHOrbAssistFishingMaxExchanges || !PHOrbCanStartAt(&initial,route.cells[0]) ||
        route.cells[route.count - 1] == route.fishCell || !AssistPlanInventoryValid(initial,route)) return false;
    auto replay = initial, prefix = initial;
    unsigned visits = 0; AssistCounts takes{}, leaves{};
    for (unsigned step = 1; step < route.count; ++step) {
        const unsigned a = route.cells[step - 1], b = route.cells[step];
        if (!PHOrbMoveAdjacent(a,b,initial.width,initial.height,route.diagonal) || (initial.tapeMask & (Bits(1) << b))) return false;
        if (b == route.fishCell) {
            if (visits == route.exchangeCount || !(initial.spinnerColors & (1u << replay.types[a]))) return false;
            const auto &exchange = route.exchanges[visits++];
            if (exchange.entry != step || exchange.exit != step + 1 || exchange.exit >= route.count ||
                exchange.take >= PHOrbBasicTypes || exchange.leave >= PHOrbBasicTypes ||
                exchange.take == exchange.leave || !(initial.spinnerColors & (1u << exchange.take)) ||
                !(initial.spinnerColors & (1u << exchange.leave))) return false;
            replay.types[b] = exchange.take; ++takes[exchange.take];
        } else if (a == route.fishCell) {
            if (!visits || replay.types[b] != route.exchanges[visits - 1].leave) return false;
            ++leaves[replay.types[b]];
        }
#define PH_ASSIST_FISH_SWAP(field) std::swap(replay.field[a],replay.field[b])
        PH_ASSIST_FISH_SWAP(types); PH_ASSIST_FISH_SWAP(flags); PH_ASSIST_FISH_SWAP(enhancement);
        PH_ASSIST_FISH_SWAP(tokens); PH_ASSIST_FISH_SWAP(blindTurns); PH_ASSIST_FISH_SWAP(thornDamage);
#undef PH_ASSIST_FISH_SWAP
        replay.flags[a] &= ~PHOrbFlagBlindTouch; replay.flags[b] &= ~PHOrbFlagBlindTouch;
        if (step == through) prefix = replay;
    }
    if (visits != route.exchangeCount) return false;
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c)
        if (takes[c] != route.plan.take[c] || leaves[c] != route.plan.leave[c]) return false;
    result = prefix; return true;
}

bool PHOrbEvaluateAssistFishing(const PHOrbBoard &board, const PHOrbAssistFishingRoute &route,
    const PHOrbSolveOptions &options, const PHOrbAssistRecoveryState &recovery, PHOrbEvaluation &result) noexcept {
    result = {};
    if (!AssistFishingInputValid(board,options,recovery,PHOrbAssistFishingMaxExchanges) ||
        options.fishCell != route.fishCell || options.diagonal != route.diagonal) return false;
    PHOrbAssistFishingPlan expected; bool goalLimit = false;
    if (!AssistBuildGoals(board,options.goals,recovery,route.plan.lCounts,expected,goalLimit) ||
        !AssistGoalsEqual(expected.goals,route.plan.goals) || expected.predictedTurns != route.plan.predictedTurns ||
        expected.providers != route.plan.providers) return false;
    PHOrbBoard replay;
    if (!PHOrbReplayAssistFishing(board,route,route.count - 1,replay)) return false;
    auto trial = options; trial.goals = expected.goals; trial.fishCell = -1;
    return PHOrbEvaluateWithOptions(replay,trial,result);
}

PHOrbTimedFishingReservation PHOrbReserveAssistFishing(const PHOrbBoard &board,
    const PHOrbAssistFishingRoute &route, const PHOrbTimedFishingTiming &timing) noexcept {
    PHOrbBoard replay;
    if (!PHOrbReplayAssistFishing(board,route,route.count - 1,replay)) return {};
    return AssistReserve(board,route.count - 1,route.exchangeCount,timing);
}

PHOrbSolveStatus PHOrbSolveAssistFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, const PHOrbTimedFishingTiming &timing, unsigned maxExchanges,
    const std::atomic_bool &cancel, PHOrbAssistFishingResult &out) noexcept {
    out = {};
    if (!AssistFishingInputValid(board,options,recovery,maxExchanges) || !AssistTimingValid(timing) ||
        !options.steps || options.steps > PHOrbMaxSteps || !options.beam || options.beam > 512 ||
        !options.milliseconds || options.milliseconds > PHOrbMaxSearchMilliseconds) return PHOrbSolveStatus::Invalid;
    if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
    try {
        const auto started = Clock::now(), deadline = started + std::chrono::milliseconds(options.milliseconds);
        const auto reserve = std::chrono::milliseconds(std::min(25u,std::max(1u,options.milliseconds / 20)));
        const auto searchDeadline = deadline - reserve;
        const auto stamp = [&]() {
            const auto now = Clock::now();
            out.elapsedMS = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count());
            out.deadlineReached = now >= deadline;
        };
        const auto cancelled = [&]() {
            if (!cancel.load(std::memory_order_relaxed)) return false;
            out.route = {}; out.evaluation = {}; out.reservation = {}; stamp(); return true;
        };
        const auto inventoryDeadline = std::min(searchDeadline,
            started + std::chrono::milliseconds(std::min(100u,std::max(1u,options.milliseconds / 4))));
        out.inventory = AssistInventoryPlans(board,options,recovery,maxExchanges,&cancel,inventoryDeadline);
        if (cancelled()) return PHOrbSolveStatus::Cancelled;
        if (!out.inventory.count || Clock::now() >= searchDeadline) {
            out.searchBudgetExhausted = Clock::now() >= searchDeadline; stamp(); return PHOrbSolveStatus::NoRoute;
        }
        const unsigned size = board.width * board.height, fish = unsigned(options.fishCell);
        AssistFishingNode best; PHOrbAssistFishingPlan bestPlan; bool found = false, solved = false;
        for (unsigned p = 0; p < out.inventory.count && !solved; ++p) {
            if (cancelled()) return PHOrbSolveStatus::Cancelled;
            const auto now = Clock::now();
            if (now >= searchDeadline) { out.searchBudgetExhausted = true; break; }
            const auto &plan = out.inventory.plans[p];
            unsigned steps = options.steps;
            while (steps && !AssistReserve(board,steps,plan.exchangeCount,timing).fits) --steps;
            if (steps < std::max(1u,2u * plan.exchangeCount)) { ++out.plansTimeRejected; continue; }
            ++out.plansSearched;
            const auto planDeadline = now + (searchDeadline - now) / (out.inventory.count - p);
            auto trial = options; trial.goals = plan.goals;
            const auto context = MakeContext(board,trial.goals,true,trial.allowPoisonWithHeartFour,trial.diagonal,{},trial.fishCell);
            std::vector<AssistFishingNode> beam, next;
            beam.reserve(std::max(options.beam,size)); next.reserve(options.beam * (options.diagonal ? 8 : 4) * PHOrbBasicTypes);
            for (unsigned cell = 0; cell < size; ++cell) if (PHOrbCanStartAt(&board,cell) &&
                (!plan.exchangeCount || (board.spinnerColors & (1u << board.types[cell])))) {
                AssistFishingNode node; std::copy(board.types,board.types + size,node.drag.cells.begin());
                std::copy(plan.take,plan.take + PHOrbBasicTypes,node.takeLeft.begin());
                std::copy(plan.leave,plan.leave + PHOrbBasicTypes,node.leaveLeft.begin());
                node.drag.path[0] = uint8_t(cell); beam.push_back(node);
            }
            VisitedSet visited(size_t(std::max(options.beam,size)) * (options.diagonal ? 8 : 4) * PHOrbBasicTypes);
            bool ended = false;
            for (unsigned depth = 0; depth < steps && !beam.empty() && !ended && !solved; ++depth) {
                next.clear(); visited.clear();
                for (const auto &parent : beam) {
                    const unsigned current = parent.drag.path[parent.drag.length - 1];
                    Bits neighbors = MoveNeighbours(Bits(1) << current,context) & ~board.tapeMask;
                    while (neighbors && !ended && !solved) {
                        const unsigned target = unsigned(__builtin_ctzll(neighbors)); neighbors &= neighbors - 1;
                        const bool entry = target == fish, leaving = current == fish;
                        if (parent.drag.length > 1 && target == parent.drag.path[parent.drag.length - 2] && !entry && !leaving) continue;
                        if (entry && parent.entries == plan.exchangeCount) continue;
                        const unsigned sacrifice = parent.drag.cells[target];
                        if (leaving && (!parent.entries || !parent.leaveLeft[sacrifice])) continue;
                        for (unsigned take = 0; take < (entry ? unsigned(PHOrbBasicTypes) : 1u); ++take) {
                            if (entry && !parent.takeLeft[take]) continue;
                            if (cancelled()) return PHOrbSolveStatus::Cancelled;
                            const auto candidateTime = Clock::now();
                            if (candidateTime >= planDeadline) {
                                ended = true; ++out.planSlicesExpired;
                                out.searchBudgetExhausted |= candidateTime >= searchDeadline; break;
                            }
                            AssistFishingNode child = parent;
                            if (entry) {
                                auto &exchange = child.exchanges[child.entries++];
                                exchange.entry = child.drag.length; exchange.exit = child.drag.length + 1; exchange.take = uint8_t(take);
                                --child.takeLeft[take]; child.drag.cells[fish] = uint8_t(take); child.drag.fished = true;
                            }
                            if (leaving) {
                                child.exchanges[child.entries - 1].leave = uint8_t(sacrifice); --child.leaveLeft[sacrifice];
                            }
                            child.drag.path[child.drag.length++] = uint8_t(target);
                            std::swap(child.drag.cells[current],child.drag.cells[target]);
                            const unsigned stillNeeded = 2u * (plan.exchangeCount - child.entries) + unsigned(entry);
                            if (depth + 1 + stillNeeded > steps || !visited.insert(AssistFishingHash(child,size))) continue;
                            child.drag.score = Evaluate(child.drag.cells,context);
                            ++out.evaluated; out.exploredDepth = uint8_t(std::max(unsigned(out.exploredDepth),depth + 1));
                            const bool complete = child.entries == plan.exchangeCount && !entry;
                            child.drag.score.result.goalsMet &= complete;
                            if (complete && (!found || Better(child.drag,best.drag,false,options.allowPoisonWithHeartFour,{}))) {
                                best = child; bestPlan = plan; found = true;
                                solved = child.drag.score.result.goalsMet && child.drag.score.result.hazardsSafe;
                            }
                            next.push_back(std::move(child));
                            if (solved) break;
                        }
                    }
                    if (ended || solved) break;
                }
                if (!next.empty() && !ended && !solved && depth + 1 < steps)
                    SelectAssistFishingBeam(next,beam,options.beam,fish,options);
                else break;
            }
        }
        if (cancelled()) return PHOrbSolveStatus::Cancelled;
        out.searchBudgetExhausted |= Clock::now() >= searchDeadline;
        if (!found || Clock::now() >= deadline) { stamp(); return PHOrbSolveStatus::NoRoute; }
        const auto route = AssistRoute(best,bestPlan,fish,options.diagonal);
        PHOrbBoard replay; PHOrbEvaluation checked;
        if (!PHOrbReplayAssistFishing(board,route,route.count - 1,replay) ||
            std::memcmp(replay.types,best.drag.cells.data(),size) ||
            !PHOrbEvaluateAssistFishing(board,route,options,recovery,checked)) { stamp(); return PHOrbSolveStatus::Failed; }
        const auto reservation = PHOrbReserveAssistFishing(board,route,timing);
        if (cancelled()) return PHOrbSolveStatus::Cancelled;
        stamp();
        if (out.deadlineReached || !reservation.valid || !reservation.fits) return PHOrbSolveStatus::NoRoute;
        out.route = route; out.evaluation = checked; out.reservation = reservation;
        if (!checked.hazardsSafe) return PHOrbSolveStatus::Unsafe;
        return checked.goalsMet ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet;
    } catch (...) { out = {}; return PHOrbSolveStatus::Failed; }
}
