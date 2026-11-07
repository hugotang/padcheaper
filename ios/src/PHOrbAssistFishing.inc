// Shared bounded multi-take planning. Legacy assist entry points remain
// restricted to their original input contract; native submission is separate.
#include "PHOrbMultiFishing.h"
namespace {
using AssistCounts = std::array<uint8_t, PHOrbBasicTypes>;
bool MultiFishingBoardValid(const PHOrbBoard &board, int fish) {
    if (!PHOrbBoardValid(&board) || fish < 0 || unsigned(fish) >= unsigned(board.width) * board.height ||
        !(board.spinnerMask & (Bits(1) << fish)) || (board.spinnerColors & ~63u) ||
        PHOrbSpinnerVariants(&board) < 2 || (board.tapeMask & (Bits(1) << fish))) return false;
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (board.flags[i] & (PHOrbFlagLocked | PHOrbFlagThorn)) return false;
    return true;
}
bool MultiFishingInputValid(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState *recovery, unsigned limit) {
    if (!MultiFishingBoardValid(board,options.fishCell) || !PHOrbGoalsValid(&options.goals) ||
        !TimedFishingActivitySupported(options.activity) || limit > PHOrbAssistFishingMaxExchanges ||
        (board.minErase && board.minErase != 3) || board.minMatch > 5) return false;
    if (!recovery) return true;
    if (!recovery->valid || !recovery->turns || (recovery->dungeonFlags & 8u)) return false;
    for (unsigned c = 0; c < 5; ++c)
        if (recovery->perColor[c] && (!recovery->providers[c] || (recovery->providers[c] & ~63u))) return false;
    return true;
}

// The selected spinner may acquire any phase after an exchange. Other
// spinners were never touched, so their relative phases stay correlated.
// Enumerate at most 6 independent target colors x 6 shared untouched phases.
// The working context is local to one search/evaluation and restored on exit.
Score MultiFishingScore(const Cells &cells, Context &context, unsigned fish, unsigned table) {
    if (context.dynamicSpinners == (Bits(1) << fish)) return Evaluate(cells,context);
    uint8_t saved[PHOrbTypes]{};
    for (unsigned p = 0; p < context.variants; ++p) saved[p] = context.spinnerStates[p][fish];
    Score worst; bool first = true;
    for (unsigned color = 0; color < PHOrbBasicTypes; ++color) if (table & (1u << color)) {
        for (unsigned p = 0; p < context.variants; ++p) context.spinnerStates[p][fish] = uint8_t(color);
        Score value = Evaluate(cells,context);
        if (first) { worst = value; first = false; continue; }
        MergeTimedFishingEvaluation(worst.result,value.result,context.goals);
        for (unsigned g = 0; g < context.goals.count; ++g)
            if (value.goalProgress[g] < worst.goalProgress[g]) {
                worst.goalProgress[g] = value.goalProgress[g]; worst.layouts[g] = value.layouts[g];
            }
        worst.jointPenalty = std::max(worst.jointPenalty,value.jointPenalty);
        worst.potential = std::min(worst.potential,value.potential);
    }
    for (unsigned p = 0; p < context.variants; ++p) context.spinnerStates[p][fish] = saved[p];
    worst.result.hazardsSafe = ReleaseHazardsSafe(worst.result,context.allowPoisonWithHeartFour);
    FinishProgress(worst,context);
    return worst;
}

unsigned MultiMinimumSpinnerSupply(const PHOrbBoard &board, unsigned fish, unsigned color) {
    unsigned minimum = PHOrbMaxCells;
    for (unsigned p = 0; p < PHOrbSpinnerVariants(&board); ++p) {
        unsigned count = 0;
        for (unsigned i = 0; i < board.width * board.height; ++i)
            if (i != fish && (board.spinnerMask & (Bits(1) << i)))
                count += PHOrbSpinnerType(&board,i,p) == color;
        minimum = std::min(minimum,count);
    }
    return minimum;
}
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
std::array<unsigned,PHOrbAssistFishingMaxPlans> FishingSearchWeights(const PHOrbAssistFishingInventory &inventory) {
    std::array<unsigned,PHOrbAssistFishingMaxPlans> weights{};
    if (inventory.count > weights.size() || inventory.geometrySelectedPlans > inventory.count) return weights;
    std::fill(weights.begin(),weights.begin() + inventory.count,1u);
    const unsigned baselineCount = inventory.count - inventory.geometrySelectedPlans;
    if (baselineCount < 4) return weights;
    const uint32_t allocation = AssistAllocationKey(inventory.plans[0]);
    for (unsigned p = 1; p < baselineCount; ++p)
        if (AssistAllocationKey(inventory.plans[p]) != allocation) return weights;
    // Many donor inventories for the same L allocation must not all stop at
    // the same shallow depth. The best-ranked representative gets half of
    // its existing stage budget; every other baseline retains equal shares
    // of the other half. Cross-allocation and geometry shares stay unchanged.
    weights[0] = baselineCount - 1;
    return weights;
}
bool AssistInventoryPasses(const Inventory &fixed, const PHOrbBoard &board, const Context &context,
    unsigned fish, unsigned &upper) {
    upper = PHOrbMaxCells;
    const unsigned phases = (board.spinnerMask & ~(Bits(1) << fish)) ? PHOrbSpinnerVariants(&board) : 1;
    for (unsigned p = 0; p < phases; ++p)
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) if (board.spinnerColors & (1u << c)) {
            auto phase = fixed; ++phase[c]; unsigned phaseUpper = 0;
            for (unsigned i = 0; i < board.width * board.height; ++i)
                if (i != fish && (board.spinnerMask & (Bits(1) << i))) ++phase[PHOrbSpinnerType(&board,i,p)];
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
        if (!(board.spinnerMask & (Bits(1) << i))) ++original[board.types[i]];
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) out.fixed[c] = uint8_t(original[c]);
    unsigned existing[5]{}, capacity[5]{}; uint8_t counts[5]{};
    for (unsigned c = 0; c < 5; ++c) {
        existing[c] = AssistExistingL(options.goals,c);
        const unsigned reserved = PHOrbRequiredShapeOrbs(&options.goals,c) - 5 * existing[c];
        const bool eligible = recovery.perColor[c] && !(board.unmatchableColors & (1u << c));
        const unsigned possible = original[c] + MultiMinimumSpinnerSupply(board,unsigned(options.fishCell),c) +
            ((board.spinnerColors & (1u << c)) ? limit : 0);
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
            const unsigned shape = PHOrbRequiredShapeOrbs(&a.plan.goals,c);
            const unsigned supplied = MultiMinimumSpinnerSupply(board,unsigned(options.fishCell),c);
            const unsigned required = shape > supplied ? shape - supplied : 0;
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
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
            const unsigned shape = PHOrbRequiredShapeOrbs(&trial.goals,c);
            const unsigned supplied = MultiMinimumSpinnerSupply(board,unsigned(options.fishCell),c);
            required[c] = shape > supplied ? shape - supplied : 0;
        }
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
            if (deficit < 0 && AssistInventoryPasses(fixed,board,context,unsigned(options.fishCell),comboUpper)) {
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
        if (!(board.spinnerMask & (Bits(1) << cell)) && !(board.tapeMask & (Bits(1) << cell)) &&
            board.types[cell] < PHOrbBasicTypes) {
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

// Extend only the already-selected passing inventories. Baseline enumeration
// and its selected order are untouched; geometry consumes leftover slots and
// states, and never licenses a route on inventory alone.
void ExtendFishingGeometry(const PHOrbBoard &board, const PHOrbSolveOptions &options, unsigned limit,
    unsigned extraLimit, PHOrbAssistFishingInventory &out, const std::atomic_bool *cancel = nullptr,
    Clock::time_point deadline = Clock::time_point::max()) {
    out.geometryRequestedExtra = extraLimit;
    if (!extraLimit || !out.valid || !out.count) return;
    const unsigned baselineCount = out.count;
    const unsigned slots = std::min(PHOrbMultiFishingMaxGeometryPlans,PHOrbAssistFishingMaxPlans - baselineCount);
    if (!slots) { out.geometrySlotLimitReached = true; return; }
    const unsigned allowance = std::min(PHOrbMultiFishingMaxGeometryStates,
        PHOrbAssistFishingMaxInventoryStates - out.visitedStates);
    if (!allowance) { out.geometryStateLimitReached = true; return; }
    const auto interrupted = [&]() {
        out.geometryDeadlineReached |= Clock::now() >= deadline;
        return out.geometryDeadlineReached || (cancel && cancel->load(std::memory_order_relaxed));
    };
    Inventory original{};
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (!(board.spinnerMask & (Bits(1) << i))) ++original[board.types[i]];
    const auto fixedFor = [&](const PHOrbAssistFishingPlan &plan) {
        auto fixed = original;
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) fixed[c] = fixed[c] + plan.take[c] - plan.leave[c];
        return fixed;
    };
    const auto keyFor = [&](const PHOrbAssistFishingPlan &plan, const Inventory &fixed) {
        return AssistInventoryKey(fixed) | (uint64_t(AssistAllocationKey(plan)) << 36);
    };
    struct Extension { PHOrbAssistFishingPlan plan; Inventory fixed; };
    std::vector<Extension> pending;
    std::unordered_set<uint64_t> visited;
    // Every seed is inserted before its children so a baseline always wins
    // over an identical extension reached from another seed.
    for (unsigned p = 0; p < baselineCount; ++p) {
        const auto &plan = out.plans[p]; const auto fixed = fixedFor(plan);
        visited.insert(keyFor(plan,fixed)); pending.push_back({plan,fixed});
    }
    std::vector<PHOrbAssistFishingPlan> feasible;
    for (size_t cursor = 0; cursor < pending.size() && !interrupted(); ++cursor) {
        const auto parent = pending[cursor];
        if (parent.plan.geometryExtraExchanges >= extraLimit || parent.plan.exchangeCount >= limit) continue;
        auto trial = options; trial.goals = parent.plan.goals;
        const auto context = TimedFishingContext(board,trial,false);
        unsigned required[PHOrbBasicTypes]{};
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
            const unsigned shape = PHOrbRequiredShapeOrbs(&trial.goals,c);
            const unsigned supply = MultiMinimumSpinnerSupply(board,unsigned(options.fishCell),c);
            required[c] = shape > supply ? shape - supply : 0;
        }
        for (unsigned take = 0; take < PHOrbBasicTypes && !interrupted(); ++take) {
            if (!(board.spinnerColors & (1u << take)) || parent.plan.leave[take]) continue;
            for (unsigned leave = 0; leave < PHOrbBasicTypes && !interrupted(); ++leave) {
                if (take == leave || !(board.spinnerColors & (1u << leave)) || parent.plan.take[leave] ||
                    parent.fixed[leave] <= required[leave]) continue;
                auto child = parent;
                ++child.fixed[take]; --child.fixed[leave];
                ++child.plan.take[take]; ++child.plan.leave[leave];
                ++child.plan.exchangeCount; ++child.plan.geometryExtraExchanges;
                const auto key = keyFor(child.plan,child.fixed);
                if (visited.find(key) != visited.end()) continue;
                if (out.geometryVisitedStates >= allowance) { out.geometryStateLimitReached = true; continue; }
                visited.insert(key); ++out.geometryVisitedStates; ++out.visitedStates;
                unsigned comboUpper = 0;
                if (AssistInventoryPasses(child.fixed,board,context,unsigned(options.fishCell),comboUpper)) {
                    child.plan.comboUpper = comboUpper; feasible.push_back(child.plan);
                }
                // A passing +1 inventory can still be geometrically unusable.
                // A nonpassing intermediate can also recover at +2.
                pending.push_back(std::move(child));
            }
        }
    }
    unsigned usefulColors = 0;
    for (unsigned g = 0; g < options.goals.count; ++g) usefulColors |= options.goals.items[g].colors;
    if (!usefulColors) usefulColors = board.spinnerColors; // Pure Combo has no fixed target color.
    std::array<std::vector<unsigned>,PHOrbBasicTypes> distances;
    const unsigned fish = unsigned(options.fishCell), width = board.width;
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (!(board.spinnerMask & (Bits(1) << i)) && !(board.tapeMask & (Bits(1) << i)) && board.types[i] < PHOrbBasicTypes) {
            const unsigned dx = unsigned(std::abs(int(i % width) - int(fish % width)));
            const unsigned dy = unsigned(std::abs(int(i / width) - int(fish / width)));
            distances[board.types[i]].push_back(options.diagonal ? std::max(dx,dy) : dx + dy);
        }
    for (auto &color : distances) std::sort(color.begin(),color.end());
    const auto rank = [&](const PHOrbAssistFishingPlan &plan) {
        unsigned irrelevant = 0, supplyCost = 0, colors = usefulColors;
        for (unsigned g = 0; g < plan.goals.count; ++g) colors |= plan.goals.items[g].colors;
        for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
            if (!(colors & (1u << c))) irrelevant += plan.take[c];
            for (unsigned n = 0; n < plan.leave[c]; ++n)
                supplyCost += n < distances[c].size() ? distances[c][n] : unsigned(PHOrbMaxCells);
        }
        return std::pair<unsigned,unsigned>{irrelevant,supplyCost};
    };
    std::stable_sort(feasible.begin(),feasible.end(),[&](const auto &a, const auto &b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        if (a.comboUpper != b.comboUpper) return a.comboUpper > b.comboUpper;
        return a.exchangeCount < b.exchangeCount;
    });
    out.geometryFeasiblePlans = unsigned(feasible.size());
    std::array<std::vector<size_t>,PHOrbMultiFishingMaxGeometryExtra> depths;
    for (size_t p = 0; p < feasible.size(); ++p) depths[feasible[p].geometryExtraExchanges - 1].push_back(p);
    // Alternate +1/+2 lanes so a crowd of cheap +1 inventories cannot hide
    // the two-take layout rescue. With just one slot, prefer +2 if available.
    for (size_t row = 0; out.geometrySelectedPlans < slots; ++row) {
        bool added = false;
        for (unsigned lane = 0; lane < extraLimit && out.geometrySelectedPlans < slots; ++lane) {
            const unsigned depth = slots == 1 && extraLimit == 2 ? 1 - lane : lane;
            if (row >= depths[depth].size()) continue;
            out.plans[out.count++] = feasible[depths[depth][row]]; ++out.geometrySelectedPlans; added = true;
        }
        if (!added) break;
    }
    out.geometrySelectionTruncated = out.geometrySelectedPlans < out.geometryFeasiblePlans;
    out.feasiblePlans += out.geometryFeasiblePlans;
    out.selectionTruncated |= out.geometrySelectionTruncated;
    interrupted();
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
        if (!(board.spinnerMask & (Bits(1) << i)) && board.types[i] < PHOrbBasicTypes) ++fixed[board.types[i]];
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

PHOrbMultiFishingInventory PHOrbPlanMultiFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState *recovery, unsigned maxExchanges, unsigned geometryExtraExchanges) noexcept {
    if (!MultiFishingInputValid(board,options,recovery,maxExchanges) ||
        geometryExtraExchanges > PHOrbMultiFishingMaxGeometryExtra) return {};
    try {
        const PHOrbAssistRecoveryState ordinary{};
        auto inventory = AssistInventoryPlans(board,options,recovery ? *recovery : ordinary,maxExchanges);
        ExtendFishingGeometry(board,options,maxExchanges,geometryExtraExchanges,inventory);
        return inventory;
    }
    catch (...) { return {}; }
}

bool PHOrbReplayMultiFishing(const PHOrbBoard &board, const PHOrbMultiFishingRoute &route,
    unsigned through, PHOrbBoard &result) noexcept {
    const auto initial = board; result = {};
    if (!MultiFishingBoardValid(initial,route.fishCell) || !PHOrbGoalsValid(&route.plan.goals) ||
        route.count < 2 || route.count > PHOrbMaxSteps + 1 || through >= route.count ||
        route.exchangeCount > PHOrbAssistFishingMaxExchanges || !PHOrbCanStartAt(&initial,route.cells[0]) ||
        route.cells[route.count - 1] == route.fishCell || !AssistPlanInventoryValid(initial,route)) return false;
    auto replay = initial, prefix = initial;
    unsigned visits = 0; AssistCounts takes{}, leaves{};
    for (unsigned step = 1; step < route.count; ++step) {
        const unsigned a = route.cells[step - 1], b = route.cells[step];
        if (!PHOrbMoveAdjacent(a,b,initial.width,initial.height,route.diagonal) ||
            ((initial.tapeMask | (initial.spinnerMask & ~(Bits(1) << route.fishCell))) & (Bits(1) << b))) return false;
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

bool PHOrbEvaluateMultiFishing(const PHOrbBoard &board, const PHOrbMultiFishingRoute &route,
    const PHOrbSolveOptions &options, const PHOrbAssistRecoveryState *recovery, PHOrbEvaluation &result) noexcept {
    result = {};
    if (!MultiFishingInputValid(board,options,recovery,PHOrbAssistFishingMaxExchanges) ||
        options.fishCell != route.fishCell || options.diagonal != route.diagonal) return false;
    PHOrbAssistFishingPlan expected; bool goalLimit = false;
    const PHOrbAssistRecoveryState ordinary{};
    if (!AssistBuildGoals(board,options.goals,recovery ? *recovery : ordinary,route.plan.lCounts,expected,goalLimit) ||
        !AssistGoalsEqual(expected.goals,route.plan.goals) || expected.predictedTurns != route.plan.predictedTurns ||
        expected.providers != route.plan.providers) return false;
    PHOrbBoard replay;
    if (!PHOrbReplayMultiFishing(board,route,route.count - 1,replay)) return false;
    auto finalOptions = options; finalOptions.goals = expected.goals;
    return PHOrbEvaluateMultiFishingBoard(replay,route.fishCell,finalOptions,result);
}

bool PHOrbEvaluateMultiFishingBoard(const PHOrbBoard &board, unsigned fishCell,
    const PHOrbSolveOptions &options, PHOrbEvaluation &result) noexcept {
    result = {};
    if (!MultiFishingBoardValid(board,int(fishCell)) || !PHOrbGoalsValid(&options.goals) ||
        !TimedFishingActivitySupported(options.activity)) return false;
    try {
        auto context = MakeContext(board,options.goals,false,options.allowPoisonWithHeartFour,options.diagonal,{},int(fishCell));
        Cells cells{}; std::copy(board.types,board.types + board.width * board.height,cells.begin());
        result = MultiFishingScore(cells,context,fishCell,board.spinnerColors).result;
        return true;
    } catch (...) { return false; }
}

PHOrbTimedFishingReservation PHOrbReserveMultiFishing(const PHOrbBoard &board,
    const PHOrbMultiFishingRoute &route, const PHOrbTimedFishingTiming &timing) noexcept {
    PHOrbBoard replay;
    if (!PHOrbReplayMultiFishing(board,route,route.count - 1,replay)) return {};
    return AssistReserve(board,route.count - 1,route.exchangeCount,timing);
}

PHOrbSolveStatus PHOrbSolveMultiFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState *recovery, const PHOrbTimedFishingTiming &timing, unsigned maxExchanges,
    const std::atomic_bool &cancel, PHOrbMultiFishingResult &out, unsigned geometryExtraExchanges) noexcept {
    out = {};
    if (!MultiFishingInputValid(board,options,recovery,maxExchanges) || !AssistTimingValid(timing) ||
        !options.steps || options.steps > PHOrbMaxSteps || !options.beam || options.beam > 512 ||
        !options.milliseconds || options.milliseconds > PHOrbMaxSearchMilliseconds ||
        geometryExtraExchanges > PHOrbMultiFishingMaxGeometryExtra) return PHOrbSolveStatus::Invalid;
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
        const PHOrbAssistRecoveryState ordinary{};
        out.inventory = AssistInventoryPlans(board,options,recovery ? *recovery : ordinary,maxExchanges,&cancel,inventoryDeadline);
        ExtendFishingGeometry(board,options,maxExchanges,geometryExtraExchanges,out.inventory,&cancel,inventoryDeadline);
        if (cancelled()) return PHOrbSolveStatus::Cancelled;
        if (!out.inventory.count || Clock::now() >= searchDeadline) {
            out.searchBudgetExhausted = Clock::now() >= searchDeadline; stamp(); return PHOrbSolveStatus::NoRoute;
        }
        const unsigned size = board.width * board.height, fish = unsigned(options.fishCell);
        const unsigned baselinePlans = out.inventory.count - out.inventory.geometrySelectedPlans;
        const auto searchWeights = FishingSearchWeights(out.inventory);
        bool geometryHasTime = false;
        for (unsigned p = baselinePlans; p < out.inventory.count; ++p) {
            const unsigned minimumMoves = 2u * out.inventory.plans[p].exchangeCount;
            geometryHasTime |= minimumMoves <= options.steps &&
                AssistReserve(board,minimumMoves,out.inventory.plans[p].exchangeCount,timing).fits;
        }
        const auto remainingSearch = searchDeadline - Clock::now();
        // A single fixed boundary reserves one third for retained layout
        // alternatives. Baselines run first; finishing early lends the unused
        // time to geometry. No alternatives means the original full budget.
        const auto baselineDeadline = geometryHasTime ? searchDeadline - remainingSearch / 3 : searchDeadline;
        AssistFishingNode best; PHOrbAssistFishingPlan bestPlan; bool found = false, solved = false;
        for (unsigned p = 0; p < out.inventory.count && !solved; ++p) {
            if (cancelled()) return PHOrbSolveStatus::Cancelled;
            const auto now = Clock::now();
            if (now >= searchDeadline) {
                out.searchBudgetExhausted = true;
                for (unsigned skipped = p; skipped < out.inventory.count; ++skipped) {
                    ++out.plansBudgetSkipped;
                    out.geometryPlansBudgetSkipped += out.inventory.plans[skipped].geometryExtraExchanges != 0;
                }
                break;
            }
            const auto &plan = out.inventory.plans[p];
            const bool geometry = plan.geometryExtraExchanges != 0;
            const auto stageDeadline = geometry ? searchDeadline : baselineDeadline;
            if (now >= stageDeadline) { ++out.plansBudgetSkipped; out.geometryPlansBudgetSkipped += geometry; continue; }
            unsigned steps = options.steps;
            while (steps && !AssistReserve(board,steps,plan.exchangeCount,timing).fits) --steps;
            if (steps < std::max(1u,2u * plan.exchangeCount)) {
                ++out.plansTimeRejected; out.geometryPlansTimeRejected += geometry; continue;
            }
            const auto planStart = Clock::now();
            if (planStart >= stageDeadline) { ++out.plansBudgetSkipped; out.geometryPlansBudgetSkipped += geometry; continue; }
            ++out.plansSearched; out.geometryPlansSearched += geometry;
            unsigned remainingWeight = 0;
            for (unsigned next = p; next < (geometry ? out.inventory.count : baselinePlans); ++next)
                remainingWeight += searchWeights[next];
            const auto planDeadline = planStart + (stageDeadline - planStart) * searchWeights[p] / remainingWeight;
            auto trial = options; trial.goals = plan.goals;
            auto context = MakeContext(board,trial.goals,true,trial.allowPoisonWithHeartFour,trial.diagonal,{},trial.fishCell);
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
                    Bits neighbors = MoveNeighbours(Bits(1) << current,context) &
                        ~(board.tapeMask | (board.spinnerMask & ~(Bits(1) << fish)));
                    while (neighbors && !ended && !solved) {
                        const unsigned target = unsigned(__builtin_ctzll(neighbors)); neighbors &= neighbors - 1;
                        const bool entry = target == fish, leaving = current == fish;
                        if (parent.drag.length > 1 && target == parent.drag.path[parent.drag.length - 2] && !entry && !leaving) continue;
                        if (entry && (parent.entries == plan.exchangeCount ||
                            !(board.spinnerColors & (1u << parent.drag.cells[current])))) continue;
                        const unsigned sacrifice = parent.drag.cells[target];
                        if (leaving && (!parent.entries || sacrifice >= PHOrbBasicTypes || !parent.leaveLeft[sacrifice])) continue;
                        for (unsigned take = 0; take < (entry ? unsigned(PHOrbBasicTypes) : 1u); ++take) {
                            if (entry && !parent.takeLeft[take]) continue;
                            if (cancelled()) return PHOrbSolveStatus::Cancelled;
                            const auto candidateTime = Clock::now();
                            if (candidateTime >= planDeadline) {
                                ended = true; ++out.planSlicesExpired;
                                out.geometryPlanSlicesExpired += geometry;
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
                            child.drag.score = MultiFishingScore(child.drag.cells,context,fish,board.spinnerColors);
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
        if (!PHOrbReplayMultiFishing(board,route,route.count - 1,replay) ||
            std::memcmp(replay.types,best.drag.cells.data(),size) ||
            !PHOrbEvaluateMultiFishing(board,route,options,recovery,checked)) { stamp(); return PHOrbSolveStatus::Failed; }
        const auto reservation = PHOrbReserveMultiFishing(board,route,timing);
        if (cancelled()) return PHOrbSolveStatus::Cancelled;
        stamp();
        if (out.deadlineReached || !reservation.valid || !reservation.fits) return PHOrbSolveStatus::NoRoute;
        out.route = route; out.evaluation = checked; out.reservation = reservation;
        if (!checked.hazardsSafe) return PHOrbSolveStatus::Unsafe;
        return checked.goalsMet ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet;
    } catch (...) { out = {}; return PHOrbSolveStatus::Failed; }
}

// Compatibility entry points retain the old offline model's strict board and
// active-recovery preconditions. General native planning uses MultiFishing.
PHOrbAssistFishingInventory PHOrbPlanAssistFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, unsigned maxExchanges) noexcept {
    if (!AssistFishingInputValid(board,options,recovery,maxExchanges)) return {};
    return PHOrbPlanMultiFishing(board,options,&recovery,maxExchanges);
}
bool PHOrbReplayAssistFishing(const PHOrbBoard &board, const PHOrbAssistFishingRoute &route,
    unsigned through, PHOrbBoard &result) noexcept {
    const bool valid = TimedFishingBoardValid(board,route.fishCell);
    if (!valid) { result = {}; return false; }
    return PHOrbReplayMultiFishing(board,route,through,result);
}
bool PHOrbEvaluateAssistFishing(const PHOrbBoard &board, const PHOrbAssistFishingRoute &route,
    const PHOrbSolveOptions &options, const PHOrbAssistRecoveryState &recovery, PHOrbEvaluation &result) noexcept {
    if (!AssistFishingInputValid(board,options,recovery,PHOrbAssistFishingMaxExchanges)) { result = {}; return false; }
    return PHOrbEvaluateMultiFishing(board,route,options,&recovery,result);
}
PHOrbTimedFishingReservation PHOrbReserveAssistFishing(const PHOrbBoard &board,
    const PHOrbAssistFishingRoute &route, const PHOrbTimedFishingTiming &timing) noexcept {
    if (!TimedFishingBoardValid(board,route.fishCell)) return {};
    return PHOrbReserveMultiFishing(board,route,timing);
}
PHOrbSolveStatus PHOrbSolveAssistFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const PHOrbAssistRecoveryState &recovery, const PHOrbTimedFishingTiming &timing, unsigned maxExchanges,
    const std::atomic_bool &cancel, PHOrbAssistFishingResult &result) noexcept {
    if (!AssistFishingInputValid(board,options,recovery,maxExchanges)) { result = {}; return PHOrbSolveStatus::Invalid; }
    return PHOrbSolveMultiFishing(board,options,&recovery,timing,maxExchanges,cancel,result);
}
