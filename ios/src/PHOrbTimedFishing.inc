// Separate offline-only model. Reuse exact shape evaluation and ordinary
// hint functions without changing PHOrbSolve/PHOrbRoute execution semantics.
namespace {
bool TimedFishingBoardValid(const PHOrbBoard &board, int fish) {
    if (!PHOrbBoardValid(&board) || fish < 0 || unsigned(fish) >= unsigned(board.width) * board.height ||
        board.spinnerMask != (Bits(1) << fish) || (board.spinnerColors & ~63u) ||
        PHOrbSpinnerVariants(&board) < 2 || (board.tapeMask & board.spinnerMask)) return false;
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (board.types[i] >= PHOrbBasicTypes || (board.flags[i] & (PHOrbFlagLocked | PHOrbFlagThorn))) return false;
    return true;
}

bool TimedFishingActivitySupported(const PHOrbActivityObjective &a) {
    if (a.firstWaveOnly || a.comboPriority || a.colors || a.linkLength != 12 ||
        a.comboWeight || a.rowWeight || a.linkWeight || a.stepCost || a.linkRewardLimit ||
        a.attackLinkMultiplier != 1 || a.attackLinkComboBonus || a.preparation ||
        a.primaryColor || a.primaryMinimum != 3) return false;
    for (unsigned color = 0; color < PHOrbBasicTypes; ++color)
        if (a.attackWeights[color] || a.attackRowPercent[color] || a.conversion[color] != color) return false;
    return true;
}

// Build shape templates for a conditional fixed color at the target. The
// original board keeps its spinner metadata; only this local scoring view has
// its cycle removed. In particular a valid exact shape may contain that cell.
Context TimedFishingContext(const PHOrbBoard &board, const PHOrbSolveOptions &options, bool guidance) {
    auto view = board; view.spinnerMask = 0; view.spinnerColors = 0;
    return MakeContext(view, options.goals, guidance, options.allowPoisonWithHeartFour,
        options.diagonal, {}, options.fishCell);
}

unsigned TimedComboCapacity(const Inventory &inventory, const Context &context) {
    unsigned capacity = 0;
    const unsigned minimum = std::max(context.minErase, context.minMatch);
    for (unsigned color = 0; color < PHOrbTypes; ++color) {
        if (context.unmatchable & (1u << color)) continue;
        // Reserve distinct single-color exact shape groups. A generic match,
        // exact length, row or color goal can overlap them and is not reserved.
        unsigned groups = 0, used = 0;
        if (color < PHOrbBasicTypes) for (unsigned g = 0; g < context.goals.count; ++g) {
            const auto &goal = context.goals.items[g];
            if (goal.colors == (1u << color) && (goal.kind == PHOrbGoalL || goal.kind == PHOrbGoalT ||
                goal.kind == PHOrbGoalCross || goal.kind == PHOrbGoalSquare || goal.kind == PHOrbGoalFour)) {
                used += Needed(goal, context.width, context.height) * goal.count;
                groups += goal.count;
            }
        }
        if (used > inventory[color]) return 0;
        capacity += groups + (inventory[color] - used) / minimum;
    }
    return capacity;
}

bool TimedInventoryPasses(const Inventory &inventory, const Context &context, unsigned &comboUpper) {
    if (CheckInventory(inventory, context, 0).kind != PHOrbInventoryKind::None) return false;
    comboUpper = TimedComboCapacity(inventory, context);
    for (unsigned g = 0; g < context.goals.count; ++g)
        if (context.goals.items[g].kind == PHOrbGoalCombo && context.goals.items[g].count > comboUpper) return false;
    return true;
}

std::array<unsigned, PHOrbBasicTypes> TimedSupplyDistances(const PHOrbBoard &board, const PHOrbSolveOptions &options) {
    const unsigned fish = unsigned(options.fishCell), w = board.width;
    std::array<unsigned, PHOrbBasicTypes> distances;
    for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
        unsigned nearest = PHOrbMaxCells;
        for (unsigned i = 0; i < w * board.height; ++i)
            if (i != fish && board.types[i] == color && !(board.tapeMask & (Bits(1) << i))) {
                const unsigned dx = unsigned(std::abs(int(i % w) - int(fish % w)));
                const unsigned dy = unsigned(std::abs(int(i / w) - int(fish / w)));
                nearest = std::min(nearest, options.diagonal ? std::max(dx,dy) : dx + dy);
            }
        distances[color] = nearest;
    }
    return distances; // Ranking only: does not reject blocked or future supplies.
}

PHOrbTimedFishingInventory TimedInventoryPlans(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    unsigned maxExchanges, const std::atomic_bool *cancel = nullptr,
    Clock::time_point deadline = Clock::time_point::max(), bool *timedOut = nullptr) {
    PHOrbTimedFishingInventory result; result.valid = true;
    const auto context = TimedFishingContext(board, options, false);
    Inventory fixed{};
    for (unsigned i = 0; i < board.width * board.height; ++i)
        if (i != unsigned(options.fishCell)) ++fixed[board.types[i]];
    std::vector<PHOrbTimedFishingInventoryPlan> feasible;
    const auto distances = TimedSupplyDistances(board,options);
    PHOrbTimedFishingInventoryPlan plan;
    const auto enumerate = [&](auto &&self, unsigned count) -> void {
        if ((timedOut && *timedOut) || (cancel && cancel->load(std::memory_order_relaxed))) return;
        if (timedOut && Clock::now() >= deadline) { *timedOut = true; return; }
        plan.count = uint8_t(count);
        for (unsigned color = 0; color < PHOrbBasicTypes; ++color) if (board.spinnerColors & (1u << color)) {
            auto released = fixed; ++released[color];
            unsigned capacity = 0;
            if (TimedInventoryPasses(released, context, capacity)) {
                plan.releaseColor = uint8_t(color); plan.comboUpper = capacity;
                feasible.push_back(plan);
            }
        }
        if (count == maxExchanges) return;
        for (unsigned leave = 0; leave < PHOrbBasicTypes; ++leave) if (fixed[leave] && (board.spinnerColors & (1u << leave))) {
            --fixed[leave];
            for (unsigned take = 0; take < PHOrbBasicTypes; ++take) if (board.spinnerColors & (1u << take)) {
                ++fixed[take]; plan.exchanges[count] = {uint8_t(take), uint8_t(leave)};
                self(self, count + 1);
                --fixed[take]; plan.exchanges[count] = {};
            }
            ++fixed[leave];
        }
    };
    enumerate(enumerate, 0);
    if ((timedOut && *timedOut) || (cancel && cancel->load(std::memory_order_relaxed))) return {};
    result.feasiblePlans = unsigned(feasible.size());
    const auto supplyCost = [&](const PHOrbTimedFishingInventoryPlan &item) {
        unsigned cost = 0;
        for (unsigned e = 0; e < item.count; ++e) cost += distances[item.exchanges[e].leave];
        return cost;
    };
    std::stable_sort(feasible.begin(), feasible.end(), [&](const auto &a, const auto &b) {
        if (a.count != b.count) return a.count < b.count;
        if (a.comboUpper != b.comboUpper) return a.comboUpper > b.comboUpper;
        return supplyCost(a) < supplyCost(b);
    });
    if (timedOut && Clock::now() >= deadline) { *timedOut = true; return {}; }
    if (cancel && cancel->load(std::memory_order_relaxed)) return {};
    if (feasible.empty()) return result;
    result.minimumExchanges = feasible.front().count;
    // Reserve one best inventory candidate per release color before filling
    // the remaining quota, so the cheapest color cannot consume every plan.
    std::vector<bool> selected(feasible.size()); unsigned seenColors = 0;
    for (size_t i = 0; i < feasible.size() && result.count < PHOrbTimedFishingMaxPlans; ++i)
        if (!(seenColors & (1u << feasible[i].releaseColor))) {
            result.plans[result.count++] = feasible[i]; selected[i] = true;
            seenColors |= 1u << feasible[i].releaseColor;
        }
    for (size_t i = 0; i < feasible.size() && result.count < PHOrbTimedFishingMaxPlans; ++i)
        if (!selected[i]) result.plans[result.count++] = feasible[i];
    return result;
}

Score TimedFishingScore(Cells cells, const Context &context, unsigned fish, unsigned releaseColor) {
    cells[fish] = uint8_t(releaseColor);
    return EvaluateStatic(cells, context);
}

void MergeTimedFishingEvaluation(PHOrbEvaluation &worst, const PHOrbEvaluation &value, const PHOrbGoalSet &goals) {
    worst.combos = std::min(worst.combos,value.combos);
    worst.erased = std::min(worst.erased,value.erased);
    worst.firstWaveCombos = std::min(worst.firstWaveCombos,value.firstWaveCombos);
    worst.heartFours = std::min(worst.heartFours,value.heartFours);
    worst.heartMatches = std::min(worst.heartMatches,value.heartMatches);
    worst.poisonErased = std::max(worst.poisonErased,value.poisonErased);
    worst.mortalPoisonErased = std::max(worst.mortalPoisonErased,value.mortalPoisonErased);
    worst.unmatchedBombs = std::max(worst.unmatchedBombs,value.unmatchedBombs);
    worst.hazardsSafe &= value.hazardsSafe;
    worst.matchedGoals = 0;
    for (unsigned g = 0; g < goals.count; ++g) {
        worst.goalHits[g] = std::min(worst.goalHits[g],value.goalHits[g]);
        worst.matchedGoals += worst.goalHits[g] >= goals.items[g].count;
    }
    worst.goalsMet = worst.matchedGoals == goals.count;
}

struct TimedFishingNode {
    Node drag;
    uint8_t entries = 0;
};
uint64_t TimedFishingHash(const TimedFishingNode &node, unsigned size) {
    return Hash(node.drag, size) ^ (UINT64_C(0x9e3779b97f4a7c15) * node.entries);
}

// Keep exchange-progress lanes so attractive partial shapes cannot erase all
// paths still bringing a sacrifice next to the spinner. Half remains selected
// solely by the objective. This is bounded diversity, not exhaustive search.
void SelectTimedFishingBeam(std::vector<TimedFishingNode> &next, std::vector<TimedFishingNode> &beam,
    unsigned limit, unsigned fish, const PHOrbSolveOptions &options) {
    const size_t keep = std::min(size_t(limit), next.size());
    std::stable_sort(next.begin(), next.end(), [&](const auto &a, const auto &b) {
        return Better(a.drag, b.drag, false, options.allowPoisonWithHeartFour, {});
    });
    if (keep < 4 || keep == next.size()) { next.resize(keep); beam.swap(next); return; }
    beam.clear(); const size_t elite = keep / 2;
    for (size_t i = 0; i < elite; ++i) beam.push_back(next[i]);
    std::array<std::vector<size_t>, 2 * (PHOrbTimedFishingMaxExchanges + 1)> lanes;
    std::vector<bool> selected(next.size());
    for (size_t i = elite; i < next.size(); ++i) {
        const auto &node = next[i];
        const unsigned lane = 2 * node.entries + (node.drag.path[node.drag.length - 1] == fish);
        lanes[lane].push_back(i);
    }
    for (size_t row = 0; beam.size() < keep; ++row) {
        bool added = false;
        for (const auto &lane : lanes) if (row < lane.size() && beam.size() < keep) {
            const size_t i = lane[row]; beam.push_back(next[i]); selected[i] = true; added = true;
        }
        if (!added) break;
    }
    for (size_t i = elite; i < next.size() && beam.size() < keep; ++i)
        if (!selected[i]) beam.push_back(next[i]);
}

PHOrbTimedFishingRoute TimedRoute(const TimedFishingNode &node, const PHOrbTimedFishingInventoryPlan &plan,
                                unsigned fish, bool diagonal) {
    PHOrbTimedFishingRoute route;
    route.count = node.drag.length; route.fishCell = uint8_t(fish); route.diagonal = diagonal;
    route.releaseColors = uint8_t(1u << plan.releaseColor); route.evaluation = node.drag.score.result;
    std::copy(node.drag.path.begin(), node.drag.path.begin() + route.count, route.cells);
    for (unsigned step = 1; step < route.count; ++step) if (route.cells[step] == fish) {
        const auto &change = plan.exchanges[route.exchangeCount];
        route.exchanges[route.exchangeCount++] = {uint8_t(step), uint8_t(step + 1), change.take, change.leave};
    }
    return route;
}
}

PHOrbTimedFishingInventory PHOrbInspectTimedFishing(const PHOrbBoard &board,
    const PHOrbSolveOptions &options, unsigned maxExchanges) noexcept {
    if (!TimedFishingBoardValid(board, options.fishCell) || !PHOrbGoalsValid(&options.goals) ||
        maxExchanges > PHOrbTimedFishingMaxExchanges || !TimedFishingActivitySupported(options.activity)) return {};
    try { return TimedInventoryPlans(board, options, maxExchanges); }
    catch (...) { return {}; }
}

bool PHOrbReplayTimedFishing(const PHOrbBoard &board, const PHOrbTimedFishingRoute &route,
    unsigned through, PHOrbBoard &result) noexcept {
    const auto initial = board; // Also permits replay into the caller's input.
    result = {};
    if (!TimedFishingBoardValid(initial, route.fishCell) || route.count < 2 || route.count > PHOrbMaxSteps + 1 ||
        through >= route.count || route.exchangeCount > PHOrbTimedFishingMaxExchanges || !route.releaseColors ||
        (route.releaseColors & ~initial.spinnerColors) || !PHOrbCanStartAt(&initial,route.cells[0]) ||
        route.cells[route.count - 1] == route.fishCell) return false;
    unsigned visits = 0;
    for (unsigned step = 1; step < route.count; ++step) {
        const unsigned a = route.cells[step - 1], b = route.cells[step];
        if (!PHOrbMoveAdjacent(a,b,initial.width,initial.height,route.diagonal) || (initial.tapeMask & (Bits(1) << b))) return false;
        if (b == route.fishCell) {
            if (visits >= route.exchangeCount) return false;
            const auto &exchange = route.exchanges[visits++];
            if (exchange.entry != step || exchange.exit != step + 1 || exchange.exit >= route.count ||
                exchange.take >= PHOrbBasicTypes || exchange.leave >= PHOrbBasicTypes ||
                !(initial.spinnerColors & (1u << exchange.take)) || !(initial.spinnerColors & (1u << exchange.leave))) return false;
        }
    }
    if (visits != route.exchangeCount) return false;
    auto replay = initial, prefix = initial; visits = 0;
    for (unsigned step = 1; step < route.count; ++step) {
        const unsigned a = route.cells[step - 1], b = route.cells[step];
        if (b == route.fishCell) {
            if (!(initial.spinnerColors & (1u << replay.types[a]))) return false;
            replay.types[b] = route.exchanges[visits++].take;
        } else if (a == route.fishCell && (!visits || replay.types[b] != route.exchanges[visits - 1].leave)) return false;
#define PH_TIMED_SWAP(field) std::swap(replay.field[a], replay.field[b])
        PH_TIMED_SWAP(types); PH_TIMED_SWAP(flags); PH_TIMED_SWAP(enhancement);
        PH_TIMED_SWAP(tokens); PH_TIMED_SWAP(blindTurns); PH_TIMED_SWAP(thornDamage);
#undef PH_TIMED_SWAP
        replay.flags[a] &= ~PHOrbFlagBlindTouch; replay.flags[b] &= ~PHOrbFlagBlindTouch;
        if (step == through) prefix = replay;
    }
    result = prefix; return true;
}

bool PHOrbEvaluateTimedFishing(const PHOrbBoard &board, const PHOrbTimedFishingRoute &route,
    const PHOrbSolveOptions &options, PHOrbEvaluation &result) noexcept {
    result = {};
    PHOrbBoard replay;
    if (options.fishCell != route.fishCell || options.diagonal != route.diagonal || !PHOrbGoalsValid(&options.goals) ||
        !TimedFishingActivitySupported(options.activity) || !PHOrbReplayTimedFishing(board,route,route.count - 1,replay)) return false;
    try {
        auto context = TimedFishingContext(board,options,false);
        context.variants = 0; context.dynamicSpinners = Bits(1) << route.fishCell;
        for (unsigned color = 0; color < PHOrbBasicTypes; ++color) if (route.releaseColors & (1u << color))
            context.spinnerStates[context.variants++][route.fishCell] = uint8_t(color);
        Cells cells{}; std::copy(replay.types,replay.types + board.width * board.height,cells.begin());
        cells[route.fishCell] = context.spinnerStates[0][route.fishCell];
        result = Evaluate(cells,context).result; return true;
    } catch (...) { return false; }
}

PHOrbTimedFishingReservation PHOrbReserveTimedFishing(const PHOrbBoard &board,
    const PHOrbTimedFishingRoute &route, const PHOrbTimedFishingTiming &timing) noexcept {
    PHOrbBoard replay;
    if (!timing.remainingFrames || !timing.framesPerMove || !timing.settleFramesPerWait ||
        !timing.inputAndCaptureFrames || !timing.spinnerPeriodFrames ||
        !PHOrbReplayTimedFishing(board,route,route.count - 1,replay)) return {};
    const uint64_t cycle = uint64_t(PHOrbSpinnerVariants(&board)) * timing.spinnerPeriodFrames;
    const uint64_t move = uint64_t(route.count - 1) * timing.framesPerMove;
    const uint64_t takeWait = route.exchangeCount * cycle;
    const uint64_t releaseWait = route.releaseColors == board.spinnerColors ? 0 : cycle;
    const uint64_t settle = uint64_t(route.exchangeCount + 1) * timing.settleFramesPerWait;
    const uint64_t required = move + takeWait + releaseWait + settle + timing.inputAndCaptureFrames;
    if (required > UINT32_MAX) return {};
    return {true, required < timing.remainingFrames, uint32_t(move), uint32_t(takeWait),
        uint32_t(releaseWait), uint32_t(settle), uint32_t(required)};
}

PHOrbSolveStatus PHOrbSolveTimedFishing(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    unsigned maxExchanges, const std::atomic_bool &cancel, PHOrbTimedFishingRoute &route) noexcept {
    route = {};
    if (!TimedFishingBoardValid(board, options.fishCell) || !PHOrbGoalsValid(&options.goals) ||
        maxExchanges > PHOrbTimedFishingMaxExchanges || !TimedFishingActivitySupported(options.activity) ||
        !options.steps || options.steps > PHOrbMaxSteps || !options.beam || options.beam > 512 ||
        !options.milliseconds || options.milliseconds > PHOrbMaxSearchMilliseconds) return PHOrbSolveStatus::Invalid;
    if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
    try {
        const auto started = Clock::now(), deadline = started + std::chrono::milliseconds(options.milliseconds);
        const auto validationReserve = std::chrono::milliseconds(std::min(25u,std::max(1u,options.milliseconds / 20u)));
        const auto searchDeadline = deadline - validationReserve;
        const auto stamp = [&]() {
            const auto now = Clock::now();
            route.elapsedMS = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count());
            route.deadlineReached = now >= deadline;
        };
        bool preflightTimedOut = false;
        const auto inventory = TimedInventoryPlans(board, options, maxExchanges, &cancel, searchDeadline, &preflightTimedOut);
        if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
        if (preflightTimedOut || Clock::now() >= searchDeadline) {
            route.searchBudgetExhausted = true; stamp();
            return PHOrbSolveStatus::NoRoute; // Incomplete inventory work cannot prove a shortage.
        }
        if (!inventory.count) { stamp(); return PHOrbSolveStatus::InsufficientOrbs; }
        const auto context = TimedFishingContext(board, options, true);
        const unsigned size = board.width * board.height, fish = unsigned(options.fishCell);
        TimedFishingNode best; bool found = false, timedOut = false;
        PHOrbTimedFishingInventoryPlan bestPlan;
        unsigned evaluated = 0, depthReached = 0, plansSearched = 0, slicesExpired = 0;
        for (unsigned p = 0; p < inventory.count; ++p) {
            const auto now = Clock::now();
            if (now >= searchDeadline) { timedOut = true; break; }
            if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
            const auto &plan = inventory.plans[p]; ++plansSearched;
            const auto planDeadline = now + (searchDeadline - now) / (inventory.count - p);
            std::vector<TimedFishingNode> beam, next;
            beam.reserve(std::max(options.beam,size)); next.reserve(options.beam * (options.diagonal ? 8 : 4));
            for (unsigned cell = 0; cell < size; ++cell) if (PHOrbCanStartAt(&board,cell) &&
                (!plan.count || (board.spinnerColors & (1u << board.types[cell])))) {
                TimedFishingNode node; std::copy(board.types,board.types + size,node.drag.cells.begin());
                node.drag.path[0] = uint8_t(cell); beam.push_back(node);
            }
            VisitedSet visited(size_t(std::max(options.beam,size)) * (options.diagonal ? 8 : 4));
            bool ended = false;
            for (unsigned depth = 0; depth < options.steps && !beam.empty() && !ended; ++depth) {
                next.clear(); visited.clear();
                for (const auto &parent : beam) {
                    const unsigned current = parent.drag.path[parent.drag.length - 1];
                    Bits neighbors = MoveNeighbours(Bits(1) << current,context) & ~board.tapeMask;
                    while (neighbors) {
                        if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
                        const auto candidateTime = Clock::now();
                        if (candidateTime >= planDeadline) {
                            ended = true; ++slicesExpired; timedOut |= candidateTime >= searchDeadline; break;
                        }
                        const unsigned target = unsigned(__builtin_ctzll(neighbors)); neighbors &= neighbors - 1;
                        const bool entry = target == fish, exit = current == fish;
                        if (parent.drag.length > 1 && target == parent.drag.path[parent.drag.length - 2] && !entry && !exit) continue;
                        if (entry && parent.entries >= plan.count) continue;
                        if (exit && (!parent.entries || parent.drag.cells[target] != plan.exchanges[parent.entries - 1].leave)) continue;
                        TimedFishingNode child = parent;
                        if (entry) {
                            child.drag.cells[fish] = plan.exchanges[child.entries++].take;
                            child.drag.fished = true;
                        }
                        child.drag.path[child.drag.length++] = uint8_t(target);
                        std::swap(child.drag.cells[current],child.drag.cells[target]);
                        if (!visited.insert(TimedFishingHash(child,size))) continue;
                        child.drag.score = TimedFishingScore(child.drag.cells,context,fish,plan.releaseColor);
                        ++evaluated; depthReached = std::max(depthReached,depth + 1);
                        const bool complete = child.entries == plan.count && target != fish;
                        // An attractive layout before the prescribed exchanges
                        // is guidance only; it must not masquerade as Solved.
                        child.drag.score.result.goalsMet &= complete;
                        if (complete && (!found || Better(child.drag,best.drag,false,options.allowPoisonWithHeartFour,{},true))) {
                            best = child; bestPlan = plan; found = true;
                        }
                        next.push_back(std::move(child));
                    }
                    if (ended) break;
                }
                if (!next.empty() && !ended && depth + 1 < options.steps)
                    SelectTimedFishingBeam(next,beam,options.beam,fish,options);
                else break;
            }
        }
        if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
        route.evaluated = evaluated; route.exploredDepth = uint8_t(depthReached); route.plansSearched = uint8_t(plansSearched);
        route.planSlicesExpired = uint8_t(slicesExpired);
        route.searchBudgetExhausted = timedOut || Clock::now() >= searchDeadline; stamp();
        if (!found) return PHOrbSolveStatus::NoRoute;
        const auto stats = route;
        route = TimedRoute(best,bestPlan,fish,options.diagonal);
        route.evaluated = stats.evaluated; route.exploredDepth = stats.exploredDepth; route.plansSearched = stats.plansSearched;
        route.planSlicesExpired = stats.planSlicesExpired;
        route.searchBudgetExhausted = stats.searchBudgetExhausted;
        const auto validationUnavailable = [&]() {
            if (Clock::now() < deadline) return false;
            route = stats; stamp(); return true; // No unchecked layout escapes as a route.
        };
        if (validationUnavailable()) return PHOrbSolveStatus::NoRoute;
        PHOrbBoard replay;
        if (!PHOrbReplayTimedFishing(board,route,route.count - 1,replay) ||
            std::memcmp(replay.types,best.drag.cells.data(),size)) { route = {}; return PHOrbSolveStatus::Failed; }
        const auto checking = TimedFishingContext(board,options,false);
        if (validationUnavailable()) return PHOrbSolveStatus::NoRoute;
        route.evaluation = TimedFishingScore(best.drag.cells,checking,fish,bestPlan.releaseColor).result;
        route.evaluatedReleaseColors = route.releaseColors;
        // Expand only to colors actually checked within the same deadline.
        // The failure candidate keeps its planned color and unmet goal counts.
        if (route.evaluation.goalsMet && route.evaluation.hazardsSafe)
            for (unsigned color = 0; color < PHOrbBasicTypes; ++color) {
                if (cancel.load(std::memory_order_relaxed)) { route = {}; return PHOrbSolveStatus::Cancelled; }
                const uint8_t bit = uint8_t(1u << color);
                if (!(board.spinnerColors & bit) || (route.evaluatedReleaseColors & bit)) continue;
                if (Clock::now() >= deadline) break;
                const auto checked = TimedFishingScore(best.drag.cells,checking,fish,color).result;
                route.evaluatedReleaseColors |= bit;
                if (checked.goalsMet && checked.hazardsSafe) {
                    route.releaseColors |= bit;
                    MergeTimedFishingEvaluation(route.evaluation,checked,options.goals);
                }
            }
        if (cancel.load(std::memory_order_relaxed)) { route = {}; return PHOrbSolveStatus::Cancelled; }
        stamp();
        if (!route.evaluation.hazardsSafe) return PHOrbSolveStatus::Unsafe;
        return route.evaluation.goalsMet ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet;
    } catch (...) { route = {}; return PHOrbSolveStatus::Failed; }
}
