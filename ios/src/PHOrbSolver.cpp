// Bounded iOS beam-search adaptation of the approach in pazusoba/core.
// See ios/third_party/pazusoba/LICENSE and NOTICE.md for provenance.
#include "PHOrbSolver.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <functional>
#include <iterator>
#include <unordered_set>
#include <vector>

namespace {
using Cells = std::array<uint8_t, PHOrbMaxCells>;
using Bits = uint64_t;
using Clock = std::chrono::steady_clock;
constexpr uint8_t Empty = 255;
unsigned Count(Bits bits) { return unsigned(__builtin_popcountll(bits)); }
struct Pattern { Bits cells, border; uint16_t stableColors = 63; };
struct Context {
    unsigned width, height;
    PHOrbGoalSet goals;
    std::array<std::vector<Pattern>, PHOrbMaxGoals> patterns;
    unsigned minErase = 3, minMatch = 0, variants = 1;
    uint16_t unmatchable = 0;
    Bits dynamicSpinners = 0;
    std::array<Cells, PHOrbTypes> spinnerStates{};
    unsigned shapeGoals = 0;
    bool guidance = true, stableHints = true, allowPoisonWithHeartFour = false;
    bool diagonal = false;
    Bits blocked = 0, boardMask = 0, leftEdge = 0, rightEdge = 0;
    std::array<Bits, 2> runStarts{};
    // Shortest legal-cell distances, shared by all candidates. These guide
    // orb supply only; they are not a claim about a continuous drag route.
    std::array<std::array<Bits, PHOrbMaxCells>, PHOrbMaxCells> distanceMasks{};
    std::array<unsigned, PHOrbMaxCells> distanceDepth{};
};
struct Score {
    PHOrbEvaluation result{};
    unsigned progress = 0, potential = 0, jointPenalty = 0;
    std::array<unsigned, PHOrbMaxGoals> goalProgress{};
    std::array<uint16_t, PHOrbMaxGoals> layouts{};
    uint8_t missing = 0, focus = 0;
};
struct Node {
    Cells cells{};
    Cells thorns{};
    std::array<uint8_t, PHOrbMaxSteps + 1> path{};
    uint32_t thornDamage = 0;
    uint8_t thornContacts = 0;
    uint8_t length = 1;
    Score score;
};

Bits Neighbours(Bits cells, const Context &context) {
    return ((cells << context.width) | (cells >> context.width) |
        ((cells & ~context.rightEdge) << 1) | ((cells & ~context.leftEdge) >> 1)) & context.boardMask;
}

Bits MoveNeighbours(Bits cells, const Context &context) {
    Bits result = Neighbours(cells, context);
    if (context.diagonal) {
        Bits horizontal = ((cells & ~context.rightEdge) << 1) | ((cells & ~context.leftEdge) >> 1);
        result |= ((horizontal << context.width) | (horizontal >> context.width)) & context.boardMask;
    }
    return result;
}

Bits MarkedRuns(Bits color, const Context &context) {
    Bits marked = 0;
    for (unsigned axis = 0; axis < 2; ++axis) {
        const unsigned stride = axis ? context.width : 1;
        Bits starts = color & context.runStarts[axis];
        for (unsigned k = 1; k < context.minErase; ++k) starts &= color >> (k * stride);
        for (unsigned k = 0; k < context.minErase; ++k) marked |= starts << (k * stride);
    }
    return marked;
}

void AddPattern(std::vector<Pattern> &patterns, Bits cells, unsigned w, unsigned h) {
    Bits border = 0;
    for (unsigned i = 0; i < w * h; ++i) if (cells & (Bits(1) << i)) {
        for (int next : {int(i) - int(w), int(i) + int(w), int(i) - 1, int(i) + 1})
            if (next >= 0 && PHOrbAdjacent(i, unsigned(next), w, h)) border |= Bits(1) << next;
    }
    patterns.push_back({cells, border & ~cells});
}

Context MakeContext(const PHOrbBoard &board, const PHOrbGoalSet &goals, bool guidance = true,
                    bool allowPoisonWithHeartFour = false, bool diagonal = false) {
    const unsigned w = board.width, h = board.height;
    Context context{w, h, goals, {}};
    context.allowPoisonWithHeartFour = allowPoisonWithHeartFour;
    context.diagonal = diagonal;
    context.minErase = board.minErase ? board.minErase : 3;
    context.minMatch = board.minMatch;
    context.unmatchable = board.unmatchableColors;
    context.variants = PHOrbSpinnerVariants(&board);
    context.guidance = guidance;
    context.blocked = PHOrbBlockedCells(&board);
    context.boardMask = (Bits(1) << (w * h)) - 1;
    for (unsigned i = 0; i < w * h; ++i) {
        const Bits bit = Bits(1) << i;
        if (i % w == 0) context.leftEdge |= bit;
        if (i % w + 1 == w) context.rightEdge |= bit;
        if (i % w + context.minErase <= w) context.runStarts[0] |= bit;
        if (i / w + context.minErase <= h) context.runStarts[1] |= bit;
    }
    for (unsigned i = 0; i < w * h; ++i) if ((board.spinnerMask & (Bits(1) << i)) && !(board.flags[i] & PHOrbFlagLocked)) {
        context.dynamicSpinners |= Bits(1) << i;
        for (unsigned phase = 0; phase < context.variants; ++phase)
            context.spinnerStates[phase][i] = PHOrbSpinnerType(&board, i, phase);
    }
    for (unsigned g = 0; g < goals.count; ++g) {
        auto &out = context.patterns[g];
        const auto kind = goals.items[g].kind;
        if (kind == PHOrbGoalCombo || kind == PHOrbGoalMatch) continue;
        context.shapeGoals += kind >= PHOrbGoalL;
        if (kind == PHOrbGoalColors || kind == PHOrbGoalFour || kind == PHOrbGoalRow || kind == PHOrbGoalColumn) {
            for (unsigned axis = 0; axis < 2; ++axis) {
                if ((kind == PHOrbGoalRow && axis) || (kind == PHOrbGoalColumn && !axis)) continue;
                unsigned length = kind == PHOrbGoalColors ? context.minErase : kind == PHOrbGoalFour ? 4 : axis ? h : w;
                unsigned dx = axis ? 0 : 1, dy = axis ? 1 : 0;
                for (unsigned y = 0; y + dy * (length - 1) < h; ++y)
                    for (unsigned x = 0; x + dx * (length - 1) < w; ++x) {
                        Bits cells = 0;
                        for (unsigned i = 0; i < length; ++i) cells |= Bits(1) << ((y + dy * i) * w + x + dx * i);
                        AddPattern(out, cells, w, h);
                    }
            }
        } else {
            // 3x3 row-major masks: all four L/T rotations, cross, and square.
            std::vector<unsigned> masks;
            if (kind == PHOrbGoalL) masks = {0x1c9, 0x1e4, 0x04f, 0x127};
            else if (kind == PHOrbGoalT) masks = {0x097, 0x1d2, 0x079, 0x13c};
            else if (kind == PHOrbGoalCross) masks = {0x0ba};
            else if (kind == PHOrbGoalSquare) masks = {0x1ff};
            for (unsigned y = 0; y + 2 < h; ++y) for (unsigned x = 0; x + 2 < w; ++x)
                for (unsigned mask : masks) {
                    Bits cells = 0;
                    for (unsigned i = 0; i < 9; ++i) if (mask & (1u << i)) cells |= Bits(1) << ((y + i / 3) * w + x + i % 3);
                    AddPattern(out, cells, w, h);
                }
        }
    }
    // An exact template must be removable under the native run/group rules.
    // Its validity is invariant across candidates and spinner phases.
    for (unsigned g = 0; g < goals.count; ++g) {
        const auto kind = goals.items[g].kind;
        if (kind < PHOrbGoalL || kind == PHOrbGoalRow || kind == PHOrbGoalColumn) continue;
        auto &patterns = context.patterns[g];
        patterns.erase(std::remove_if(patterns.begin(), patterns.end(), [&](const Pattern &pattern) {
            return Count(pattern.cells) < context.minMatch || MarkedRuns(pattern.cells, context) != pattern.cells;
        }), patterns.end());
    }
    // Guide toward one layout that can survive every shared phase. The old
    // per-phase minima could keep chasing different, incompatible layouts.
    // These masks affect hints only: exact matches still examine all phases.
    // Locked/single-color spinners are stable; a border spinner whose cycle
    // excludes the target color cannot spoil an exact shape's border.
    if (context.variants > 1) for (unsigned g = 0; g < goals.count; ++g) {
        const auto kind = goals.items[g].kind;
        if (kind < PHOrbGoalL) continue;
        for (auto &pattern : context.patterns[g]) {
            if (pattern.cells & context.dynamicSpinners) pattern.stableColors = 0;
            else if (kind != PHOrbGoalFour && kind != PHOrbGoalRow && kind != PHOrbGoalColumn &&
                     (pattern.border & context.dynamicSpinners))
                pattern.stableColors &= ~board.spinnerColors;
        }
    }
    if (context.guidance && context.shapeGoals) for (unsigned i = 0; i < w * h; ++i) {
        Bits frontier = Bits(1) << i, seen = frontier;
        if (frontier & context.blocked) continue;
        for (unsigned distance = 1; distance < w * h; ++distance) {
            frontier = MoveNeighbours(frontier, context) & ~seen & ~context.blocked;
            if (!frontier) break;
            context.distanceMasks[i][distance] = frontier;
            context.distanceDepth[i] = distance;
            seen |= frontier;
        }
    }
    return context;
}

unsigned Needed(const PHOrbGoal &goal, unsigned w, unsigned h) {
    switch (goal.kind) {
        case PHOrbGoalSquare: return 9;
        case PHOrbGoalL: case PHOrbGoalT: case PHOrbGoalCross: return 5;
        case PHOrbGoalRow: return w;
        case PHOrbGoalColumn: return h;
        case PHOrbGoalFour: return 4;
        case PHOrbGoalMatch: return goal.length;
        default: return 3;
    }
}

bool EnoughOrbs(const Cells &cells, const Context &context) {
    for (unsigned phase = 0; phase < context.variants; ++phase) {
        unsigned inventory[PHOrbTypes]{};
        for (unsigned i = 0; i < context.width * context.height; ++i)
            ++inventory[context.dynamicSpinners & (Bits(1) << i) ? context.spinnerStates[phase][i] : cells[i]];
        for (unsigned g = 0; g < context.goals.count; ++g) {
            const auto &goal = context.goals.items[g];
            unsigned possible = 0, needed = std::max({Needed(goal, context.width, context.height), context.minErase, context.minMatch});
            for (unsigned c = 0; c < PHOrbTypes; ++c)
                if (!(context.unmatchable & (1u << c)) && (goal.kind == PHOrbGoalCombo || (goal.colors & (1u << c))))
                    possible += goal.kind == PHOrbGoalColors ? unsigned(inventory[c] >= needed) : inventory[c] / needed;
            if (possible < goal.count) return false;
        }
    }
    return true; // Necessary inventory check, not a proof that a route exists.
}

bool ShapeMatches(Bits group, Bits color, const PHOrbGoal &goal, const std::vector<Pattern> &patterns) {
    for (const auto &pattern : patterns) {
        if (goal.kind == PHOrbGoalRow || goal.kind == PHOrbGoalColumn) {
            if ((group & pattern.cells) == pattern.cells) return true;
            continue;
        }
        // Native four-orb effects use the actual removed group. An unmarked
        // neighbour cannot turn four removed orbs into five. The other exact
        // shapes retain their existing conservative neighbour restriction.
        if (group == pattern.cells &&
            (goal.kind == PHOrbGoalFour || !(color & pattern.border))) return true;
    }
    return false;
}

// Colour goals need only a run. Shape guidance below additionally accounts
// for repairs and competing layouts. Neither heuristic authorizes execution.
unsigned NearColor(Bits color, const std::vector<Pattern> &patterns) {
    unsigned best = 0;
    for (const auto &pattern : patterns) {
        unsigned size = Count(pattern.cells), filled = Count(color & pattern.cells);
        best = std::max(best, 3072 * filled * filled / (size * size));
    }
    return best;
}

constexpr unsigned HintLimit = 6;
struct ShapeHint {
    Bits cells = 0, border = 0;
    unsigned value = 0;
    uint16_t tag = 0;
    uint8_t color = 0;
    bool exact = false;
};
struct ShapeHints { std::array<ShapeHint, HintLimit> items{}; unsigned count = 0; };

void OfferHint(ShapeHints &hints, const ShapeHint &hint) {
    unsigned at = 0;
    while (at < hints.count && (hints.items[at].value > hint.value ||
        (hints.items[at].value == hint.value && hints.items[at].tag < hint.tag))) ++at;
    if (at == HintLimit) return;
    hints.count = std::min(HintLimit, hints.count + 1);
    for (unsigned i = hints.count - 1; i > at; --i) hints.items[i] = hints.items[i - 1];
    hints.items[at] = hint;
}

ShapeHints FindShapeHints(const std::array<Bits, PHOrbTypes> &colors, Bits used,
                         unsigned hits, unsigned g, const Context &context) {
    ShapeHints hints;
    const auto &goal = context.goals.items[g];
    const bool met = hits >= goal.count;
    if (met && goal.count != 1) return hints;
    const bool exact = goal.kind != PHOrbGoalRow && goal.kind != PHOrbGoalColumn;
    for (unsigned c = 0; c < PHOrbBasicTypes; ++c) {
        if (!(goal.colors & (1u << c)) || (context.unmatchable & (1u << c))) continue;
        const Bits color = colors[c];
        for (unsigned p = 0; p < context.patterns[g].size(); ++p) {
            const auto &pattern = context.patterns[g][p];
            if (context.stableHints && !(pattern.stableColors & (1u << c))) continue;
            ShapeHint hint{pattern.cells, pattern.border, 4096, uint16_t(1 + p * PHOrbBasicTypes + c), uint8_t(c), exact};
            if (met) {
                if ((pattern.cells & used & color) == pattern.cells) OfferHint(hints, hint);
                continue;
            }
            // Additional groups must not overlap or join already counted
            // groups of this goal. Other goals can reuse a real match.
            if ((pattern.cells & used) || (pattern.border & used & color)) continue;
            const unsigned size = Count(pattern.cells);
            Bits holes = pattern.cells & ~color;
            if (holes & context.blocked) continue;
            const unsigned missing = Count(holes);
            // No repair can score above the cost of filling its holes alone.
            // Discard that upper bound before computing runs and distances.
            if (hints.count == HintLimit && 3072 * size / (size + 2 * missing) < hints.items.back().value) continue;
            // For four, only a neighbouring *marked* orb would merge into
            // the target after filling. Other exact shapes retain their
            // existing conservative no-same-colour-neighbour contract.
            const Bits interfering = !exact ? 0 : goal.kind == PHOrbGoalFour ?
                MarkedRuns(color | pattern.cells, context) & pattern.border : color & pattern.border;
            unsigned cost = 2 * (missing + Count(interfering));
            hint.value = 3072 * size / (size + cost);
            if (hints.count == HintLimit && hint.value < hints.items.back().value) continue;
            const Bits supply = color & ~pattern.cells & ~used & ~context.blocked;
            if (Count(supply) < missing) continue;
            bool reachable = true;
            while (holes) {
                const unsigned cell = unsigned(__builtin_ctzll(holes)); holes &= holes - 1;
                unsigned distance = 1;
                while (distance <= context.distanceDepth[cell] && !(context.distanceMasks[cell][distance] & supply)) ++distance;
                if (distance > context.distanceDepth[cell]) { reachable = false; break; }
                cost += distance - 1;
            }
            if (!reachable) continue;
            // Supply distances are optimistic: different holes may use the
            // same nearest orb. Legal swaps and exact evaluation decide hits.
            hint.value = 3072 * size / (size + cost);
            OfferHint(hints, hint);
        }
    }
    return hints;
}

unsigned ShapeNear(const ShapeHints &hints, unsigned remaining) {
    unsigned near = 0, count = 0;
    std::array<unsigned, HintLimit> selected{};
    for (unsigned i = 0; i < hints.count && count < remaining; ++i) {
        const auto &candidate = hints.items[i];
        bool compatible = true;
        for (unsigned k = 0; k < count; ++k) {
            const auto &other = hints.items[selected[k]];
            if ((candidate.cells & other.cells) || (candidate.color == other.color && (candidate.border & other.cells)))
                compatible = false;
        }
        if (compatible) { selected[count++] = i; near += candidate.value; }
    }
    return near;
}

unsigned LayoutConflict(const ShapeHint &a, const ShapeHint &b) {
    if (a.color != b.color) return Count(a.cells & b.cells);
    // Equal-colour goals may reuse an identical/subset match, e.g. a four
    // and a 5x4 full column. Touching exact templates would merge instead.
    return Count((a.exact ? b.cells & a.border : 0) | (b.exact ? a.cells & b.border : 0));
}

void JointGuidance(Score &score, const std::array<ShapeHints, PHOrbMaxGoals> &hints, const Context &context) {
    unsigned penalty = 0, pairs = 0, largest = 0;
    for (unsigned g = 0; g < context.goals.count; ++g) for (unsigned h = g + 1; h < context.goals.count; ++h) {
        if (context.goals.items[g].count != 1 || context.goals.items[h].count != 1 || !hints[g].count || !hints[h].count) continue;
        const unsigned independent = hints[g].items[0].value + hints[h].items[0].value;
        unsigned joint = 0, ga = 0, hb = 0;
        for (unsigned a = 0; a < hints[g].count; ++a) for (unsigned b = 0; b < hints[h].count; ++b) {
            const unsigned sum = hints[g].items[a].value + hints[h].items[b].value;
            const unsigned value = sum - std::min(sum, 768 * LayoutConflict(hints[g].items[a], hints[h].items[b]));
            if (value > joint) { joint = value; ga = a; hb = b; }
        }
        const unsigned loss = independent - joint;
        penalty += loss; ++pairs;
        if (pairs == 1 || loss > largest) {
            largest = loss;
            score.layouts[g] = hints[g].items[ga].tag; score.layouts[h] = hints[h].items[hb].tag;
        }
    }
    score.jointPenalty = pairs ? penalty / pairs : 0;
}

void FinishProgress(Score &score, const Context &context) {
    unsigned total = 0, weakest = 4096;
    score.missing = 0; score.focus = 0;
    for (unsigned g = 0; g < context.goals.count; ++g) {
        total += score.goalProgress[g];
        if (score.goalProgress[g] < weakest) { weakest = score.goalProgress[g]; score.focus = uint8_t(g); }
        if (score.result.goalHits[g] < context.goals.items[g].count) score.missing |= uint8_t(1u << g);
    }
    // A weak remaining condition matters more than surplus combos. Pairwise
    // layout penalties are guidance, never feasibility proofs or goal hits.
    if (context.shapeGoals > 1) total += weakest;
    score.progress = total - std::min(total, score.jointPenalty);
}

// All matches in a wave are marked together. Only existing orbs fall; no
// random skyfall or leader-added combos. Exact shapes require an entire
// first-wave group, so a 3x4 blob is never reported as a 3x3 square.
bool ReleaseHazardsSafe(const PHOrbEvaluation &result, bool allowPoisonWithHeartFour, bool requireHeart = true) {
    return !result.unmatchedBombs && !result.mortalPoisonErased &&
        (!result.poisonErased || (allowPoisonWithHeartFour && (!requireHeart || result.heartFours)));
}

Score EvaluateStatic(Cells cells, const Context &context) {
    Score score;
    auto &result = score.result;
    const unsigned width = context.width, height = context.height, size = width * height;
    const Cells initial = cells;
    std::array<Bits, PHOrbTypes> colors{};
    std::array<Bits, PHOrbMaxGoals> used{};
    std::array<ShapeHints, PHOrbMaxGoals> hints{};
    unsigned matchedColors = 0;
    for (unsigned i = 0; i < size; ++i) {
        colors[cells[i]] |= Bits(1) << i;
        if (i % width + 1 < width && cells[i] == cells[i + 1]) ++score.potential;
        if (i + width < size && cells[i] == cells[i + width]) ++score.potential;
        if (i % width + 2 < width && cells[i] == cells[i + 2]) ++score.potential;
        if (i + 2 * width < size && cells[i] == cells[i + 2 * width]) ++score.potential;
    }
    auto waveColors = colors;
    for (unsigned wave = 0; wave < size / 3; ++wave) {
        Bits marked = 0;
        for (unsigned c = 0; c < PHOrbTypes; ++c) {
            if (context.unmatchable & (1u << c)) continue;
            Bits matches = MarkedRuns(waveColors[c], context);
            if (context.minMatch > context.minErase) {
                Bits pending = matches; matches = 0;
                while (pending) {
                    Bits group = pending & (~pending + 1), frontier = group;
                    while (frontier) { frontier = Neighbours(frontier, context) & pending & ~group; group |= frontier; }
                    pending &= ~group;
                    if (Count(group) >= context.minMatch) matches |= group;
                }
            }
            marked |= matches;
        }
        if (!wave) result.unmatchedBombs = uint8_t(Count(waveColors[PHOrbBomb] & ~marked));
        if (!marked) break;
        Bits pending = marked;
        while (pending) {
            const unsigned start = unsigned(__builtin_ctzll(pending)), color = cells[start];
            Bits group = Bits(1) << start, frontier = group;
            while (frontier) { frontier = Neighbours(frontier, context) & pending & waveColors[color] & ~group; group |= frontier; }
            pending &= ~group;
            const unsigned count = Count(group);
            ++result.combos; result.erased += uint8_t(count); matchedColors |= 1u << color;
            if (color == PHOrbPoison || color == PHOrbMortalPoison) result.poisonErased += uint8_t(count);
            if (color == PHOrbMortalPoison) result.mortalPoisonErased += uint8_t(count);
            if (!wave && color == 5 && count == 4) {
                bool horizontal = true, vertical = true; Bits rest = group & ~(Bits(1) << start);
                while (rest) {
                    unsigned cell = unsigned(__builtin_ctzll(rest)); rest &= rest - 1;
                    horizontal &= cell / width == start / width; vertical &= cell % width == start % width;
                }
                result.heartFours += horizontal || vertical;
            }
            for (unsigned g = 0; g < context.goals.count; ++g) {
                const auto &goal = context.goals.items[g];
                if (!(goal.colors & (1u << color))) continue;
                bool hit = goal.kind == PHOrbGoalMatch ? count >= goal.length :
                    !wave && goal.kind >= PHOrbGoalL && ShapeMatches(group, colors[color], goal, context.patterns[g]);
                if (hit) { ++result.goalHits[g]; if (!wave) used[g] |= group; }
            }
        }
        Bits erased = marked;
        while (erased) { cells[unsigned(__builtin_ctzll(erased))] = Empty; erased &= erased - 1; }
        waveColors.fill(0);
        for (unsigned column = 0; column < width; ++column) {
            int target = int(height) - 1;
            for (int row = int(height) - 1; row >= 0; --row) {
                uint8_t value = cells[unsigned(row) * width + column];
                if (value == Empty) continue;
                unsigned cell = unsigned(target--) * width + column;
                cells[cell] = value; waveColors[value] |= Bits(1) << cell;
            }
            while (target >= 0) cells[unsigned(target--) * width + column] = Empty;
        }
    }
    for (unsigned g = 0; g < context.goals.count; ++g) {
        const auto &goal = context.goals.items[g];
        if (goal.kind == PHOrbGoalCombo) result.goalHits[g] = result.combos;
        if (goal.kind == PHOrbGoalColors) result.goalHits[g] = uint8_t(Count(matchedColors & goal.colors));
        if (result.goalHits[g] >= goal.count) { ++result.matchedGoals; score.goalProgress[g] = 4096; }
        // Public replay validation needs actual hits only. Avoid template
        // repair/distance work on the session's main-thread verification.
        if (!context.guidance) continue;
        if (goal.kind >= PHOrbGoalL) {
            hints[g] = FindShapeHints(colors, used[g], result.goalHits[g], g, context);
            if (hints[g].count) score.layouts[g] = hints[g].items[0].tag;
        }
        if (result.goalHits[g] >= goal.count) continue;
        unsigned near = 0;
        if (goal.kind == PHOrbGoalColors) {
            std::array<unsigned, 6> partial{};
            for (unsigned c = 0; c < 6; ++c) if ((goal.colors & (1u << c)) &&
                !(context.unmatchable & (1u << c)) && !(matchedColors & (1u << c)))
                partial[c] = NearColor(colors[c], context.patterns[g]);
            std::sort(partial.begin(), partial.end(), std::greater<unsigned>());
            for (unsigned i = 0; i < unsigned(goal.count - result.goalHits[g]); ++i) near += partial[i];
        } else if (goal.kind >= PHOrbGoalL) {
            near = ShapeNear(hints[g], unsigned(goal.count - result.goalHits[g]));
        } else if (goal.kind == PHOrbGoalMatch) {
            // Largest connected area is only a heuristic; the actual result
            // above still requires marked triples and the requested length.
            Bits seen = 0;
            for (unsigned start = 0; start < size; ++start) {
                if ((seen & (Bits(1) << start)) || !(goal.colors & (1u << initial[start])) ||
                    (context.unmatchable & (1u << initial[start])) || (used[g] & (Bits(1) << start))) continue;
                std::array<unsigned, PHOrbMaxCells> queue{};
                unsigned head = 0, tail = 1; queue[0] = start; seen |= Bits(1) << start;
                while (head < tail) {
                    unsigned index = queue[head++];
                    for (int next : {int(index) - int(width), int(index) + int(width), int(index) - 1, int(index) + 1}) {
                        if (next < 0 || !PHOrbAdjacent(index, unsigned(next), width, height) ||
                            (seen & (Bits(1) << next)) || (used[g] & (Bits(1) << next)) || initial[next] != initial[start]) continue;
                        seen |= Bits(1) << next; queue[tail++] = unsigned(next);
                    }
                }
                near = std::max(near, 3072 * std::min(tail, unsigned(goal.length)) / goal.length);
            }
        }
        score.goalProgress[g] = (result.goalHits[g] * 4096 + near) / goal.count;
    }
    if (context.guidance && context.shapeGoals > 1) JointGuidance(score, hints, context);
    FinishProgress(score, context);
    result.goalsMet = result.matchedGoals == context.goals.count;
    result.hazardsSafe = ReleaseHazardsSafe(result, context.allowPoisonWithHeartFour);
    return score;
}

Score Evaluate(Cells cells, const Context &context) {
    if (context.variants == 1) return EvaluateStatic(cells, context);
    Score worst;
    // The native spinner updater advances every settled, unlocked spinner by
    // one color on the same countdown. Routes never disturb these cells.
    // Preserve their relative phases, including multiple/custom-color cells.
    for (unsigned phase = 0; phase < context.variants; ++phase) {
        for (unsigned i = 0; i < context.width * context.height; ++i)
            if (context.dynamicSpinners & (Bits(1) << i)) cells[i] = context.spinnerStates[phase][i];
        Score variant = EvaluateStatic(cells, context);
        if (!phase) { worst = variant; continue; }
        auto &a = worst.result;
        const auto &b = variant.result;
        a.combos = std::min(a.combos, b.combos); a.erased = std::min(a.erased, b.erased);
        a.poisonErased = std::max(a.poisonErased, b.poisonErased);
        a.mortalPoisonErased = std::max(a.mortalPoisonErased, b.mortalPoisonErased);
        a.unmatchedBombs = std::max(a.unmatchedBombs, b.unmatchedBombs);
        a.heartFours = std::min(a.heartFours, b.heartFours);
        a.hazardsSafe &= b.hazardsSafe;
        for (unsigned g = 0; g < context.goals.count; ++g) {
            a.goalHits[g] = std::min(a.goalHits[g], b.goalHits[g]);
            if (variant.goalProgress[g] < worst.goalProgress[g]) {
                worst.goalProgress[g] = variant.goalProgress[g]; worst.layouts[g] = variant.layouts[g];
            }
        }
        worst.jointPenalty = std::max(worst.jointPenalty, variant.jointPenalty);
        worst.potential = std::min(worst.potential, variant.potential);
    }
    worst.result.matchedGoals = 0;
    for (unsigned g = 0; g < context.goals.count; ++g)
        worst.result.matchedGoals += worst.result.goalHits[g] >= context.goals.items[g].count;
    worst.result.goalsMet = worst.result.matchedGoals == context.goals.count;
    // If any phase clears poison, the team policy requires a heart four in
    // every phase, including those that would not clear poison themselves.
    worst.result.hazardsSafe = ReleaseHazardsSafe(worst.result, context.allowPoisonWithHeartFour);
    FinishProgress(worst, context);
    return worst;
}

bool Better(const Node &a, const Node &b, bool thornComboPriority, bool allowPoisonWithHeartFour) {
    const auto &ar = a.score.result, &br = b.score.result;
    if (ar.hazardsSafe != br.hazardsSafe) return ar.hazardsSafe;
    if (ar.unmatchedBombs != br.unmatchedBombs) return ar.unmatchedBombs < br.unmatchedBombs;
    if (allowPoisonWithHeartFour && ar.mortalPoisonErased != br.mortalPoisonErased)
        return ar.mortalPoisonErased < br.mortalPoisonErased;
    if (!allowPoisonWithHeartFour && ar.poisonErased != br.poisonErased) return ar.poisonErased < br.poisonErased;
    if (ar.goalsMet != br.goalsMet) return ar.goalsMet;
    if (!ar.goalsMet && a.score.progress != b.score.progress) return a.score.progress > b.score.progress;
    if (!thornComboPriority && a.thornDamage != b.thornDamage) return a.thornDamage < b.thornDamage;
    if (ar.combos != br.combos) return ar.combos > br.combos;
    if (thornComboPriority && a.thornDamage != b.thornDamage) return a.thornDamage < b.thornDamage;
    if (allowPoisonWithHeartFour && ar.poisonErased != br.poisonErased) return ar.poisonErased < br.poisonErased;
    if (ar.erased != br.erased) return ar.erased > br.erased;
    if (a.length != b.length) return a.length < b.length;
    if (a.score.potential != b.score.potential) return a.score.potential > b.score.potential;
    return std::lexicographical_compare(a.path.begin(), a.path.begin() + a.length, b.path.begin(), b.path.begin() + b.length);
}

uint64_t Hash(const Node &node, unsigned size) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned i = 0; i < size; ++i) {
        hash ^= node.cells[i] | (unsigned(node.thorns[i]) << 8); hash *= 1099511628211ULL;
    }
    hash ^= node.thornDamage; hash *= 1099511628211ULL;
    hash ^= node.path[node.length - 1]; hash *= 1099511628211ULL;
    // The forbidden immediate reverse depends on the preceding position.
    hash ^= node.length > 1 ? node.path[node.length - 2] : 255;
    return hash * 1099511628211ULL;
}

uint64_t LayoutKey(const Node &node, const Context &context) {
    uint64_t key = 14695981039346656037ULL;
    for (unsigned g = 0; g < context.goals.count; ++g) { key ^= node.score.layouts[g]; key *= 1099511628211ULL; }
    key ^= node.path[0]; key *= 1099511628211ULL;
    // Distinct current regions can approach the same target from different
    // sides without reserving every near-identical endpoint as its own lane.
    const unsigned current = node.path[node.length - 1];
    key ^= (current % context.width) * 2 / context.width + 2 * ((current / context.width) * 2 / context.height);
    key *= 1099511628211ULL;
    return key ^ node.score.missing ^ (unsigned(node.score.focus) << PHOrbMaxGoals);
}

bool BetterForSearch(const Node &a, const Node &b, unsigned focus, bool thornComboPriority, bool allowPoisonWithHeartFour) {
    if (focus < PHOrbMaxGoals && a.score.result.hazardsSafe == b.score.result.hazardsSafe &&
        !a.score.result.goalsMet && !b.score.result.goalsMet &&
        (allowPoisonWithHeartFour || a.score.result.poisonErased == b.score.result.poisonErased) &&
        (!allowPoisonWithHeartFour || a.score.result.mortalPoisonErased == b.score.result.mortalPoisonErased) &&
        a.score.result.unmatchedBombs == b.score.result.unmatchedBombs) {
        const unsigned av = a.score.progress + 3 * a.score.goalProgress[focus];
        const unsigned bv = b.score.progress + 3 * b.score.goalProgress[focus];
        if (av != bv) return av > bv;
    }
    return Better(a, b, thornComboPriority, allowPoisonWithHeartFour);
}

void SelectBeam(std::vector<Node> &next, std::vector<Node> &beam, unsigned limit, unsigned depth,
                const Context &context, unsigned focus, bool thornComboPriority, std::vector<Node> *discarded = nullptr) {
    const size_t keep = std::min(size_t(limit), next.size());
    const auto order = [focus, thornComboPriority, &context](const Node &a, const Node &b) {
        return BetterForSearch(a, b, focus, thornComboPriority, context.allowPoisonWithHeartFour);
    };
    if (!context.shapeGoals || keep < 4 || keep == next.size()) {
        std::partial_sort(next.begin(), next.begin() + keep, next.end(), order);
        next.resize(keep); beam.swap(next); return;
    }
    std::sort(next.begin(), next.end(), order);
    beam.clear();
    const size_t elite = keep - keep / 4, none = next.size();
    std::vector<bool> selected(next.size());
    std::unordered_set<uint64_t> layouts;
    layouts.reserve(keep);
    auto take = [&](size_t index) { beam.push_back(next[index]); selected[index] = true; };
    for (size_t i = 0; i < elite; ++i) { take(i); layouts.insert(LayoutKey(next[i], context)); }
    // Keep three quarters by the normal objective order. The remainder is
    // shared round-robin by missing-goal set and weakest goal, with distinct
    // layouts and starts. Linked indices avoid one allocation per bucket.
    constexpr unsigned Buckets = (1u << PHOrbMaxGoals) * PHOrbMaxGoals;
    std::array<size_t, Buckets> head, tail;
    head.fill(none); tail.fill(none);
    std::vector<size_t> link(next.size(), none);
    std::vector<unsigned> active;
    for (size_t i = elite; i < next.size(); ++i) {
        const unsigned bucket = next[i].score.missing | (unsigned(next[i].score.focus) << PHOrbMaxGoals);
        if (head[bucket] == none) { head[bucket] = i; active.push_back(bucket); }
        else link[tail[bucket]] = i;
        tail[bucket] = i;
    }
    bool added = true;
    while (beam.size() < keep && added) {
        added = false;
        for (size_t k = 0; k < active.size() && beam.size() < keep; ++k) {
            auto &cursor = head[active[(k + depth) % active.size()]];
            while (cursor != none) {
                const size_t i = cursor; cursor = link[i];
                if (layouts.insert(LayoutKey(next[i], context)).second) { take(i); added = true; break; }
            }
        }
    }
    // A small pool with identical layouts still fills its bounded quota.
    for (size_t i = elite; i < next.size() && beam.size() < keep; ++i) if (!selected[i]) take(i);
    if (discarded) {
        const size_t cap = std::min(size_t(128), keep);
        std::unordered_set<uint64_t> alternatives;
        for (size_t i = 0; i < next.size() && discarded->size() < cap; ++i)
            if (!selected[i] && alternatives.insert(LayoutKey(next[i], context)).second) discarded->push_back(next[i]);
    }
    // Interleave reserved candidates so a deadline halfway through the next
    // layer can still explore them instead of expanding all elites first.
    next.clear();
    size_t normal = 0, diverse = elite;
    while (normal < elite || diverse < keep) {
        for (unsigned k = 0; k < 3 && normal < elite; ++k) next.push_back(beam[normal++]);
        if (diverse < keep) next.push_back(beam[diverse++]);
    }
    beam.swap(next);
}
}

PHOrbSolveOptions PHOrbOptionsForQuality(PHOrbSearchQuality quality) noexcept {
    switch (quality) {
        case PHOrbSearchQuality::Fast: return {24, 128, 120, {}};
        case PHOrbSearchQuality::Thorough: return {48, 512, 800, {}, {}, 1, 2};
        case PHOrbSearchQuality::Balanced: return {};
    }
    return {};
}

bool PHOrbEvaluate(const PHOrbBoard &board, const PHOrbGoalSet &goals, PHOrbEvaluation &result,
                   bool allowPoisonWithHeartFour) noexcept {
    result = {};
    if (!PHOrbBoardValid(&board) || !PHOrbGoalsValid(&goals)) return false;
    try {
        Cells cells{};
        std::copy(board.types, board.types + board.width * board.height, cells.begin());
        result = Evaluate(cells, MakeContext(board, goals, false, allowPoisonWithHeartFour)).result;
        return true;
    } catch (...) { return false; }
}

bool PHOrbEvaluate(const PHOrbBoard &board, const PHOrbGoalSet &goals, PHOrbEvaluation &result) noexcept {
    return PHOrbEvaluate(board, goals, result, false);
}

bool PHOrbEvaluateRoute(const PHOrbBoard &board, const PHOrbRoute &route, const PHOrbSolveOptions &options,
                        PHOrbEvaluation &result, uint32_t &thornDamage, uint8_t &thornContacts) noexcept {
    result = {}; thornDamage = 0; thornContacts = 0;
    PHOrbBoard replay{};
    if (!route.count || route.diagonal != options.diagonal || !PHOrbReplay(&board, &route, route.count - 1, &replay) ||
        !PHOrbEvaluate(replay, options.goals, result, options.allowPoisonWithHeartFour)) return false;
    Cells thorns{};
    for (unsigned i = 0; i < board.width * board.height; ++i)
        thorns[i] = board.flags[i] & PHOrbFlagThorn ? board.thornDamage[i] : 0;
    uint64_t damage = 0;
    for (unsigned i = 1; i < route.count; ++i) {
        const unsigned a = route.cells[i - 1], b = route.cells[i];
        if (thorns[b]) { ++thornContacts; damage += PHOrbThornDamage(thorns[b], options.thorns.maximumHP); }
        std::swap(thorns[a], thorns[b]);
    }
    thornDamage = uint32_t(std::min(damage, uint64_t(UINT32_MAX)));
    if (thornContacts) {
        const auto &t = options.thorns;
        result.hazardsSafe &= t.known && t.hp > 0 && t.hp <= t.maximumHP && t.maximumHP <= INT32_MAX &&
            uint64_t(t.pendingDamage) + damage < t.hp && result.heartFours > 0;
    }
    return true;
}

PHOrbSolveStatus PHOrbSolve(const PHOrbBoard &board, PHOrbSolveOptions options,
                           const std::atomic_bool &cancel, PHOrbRoute &route) noexcept {
    route = {};
    if (!PHOrbBoardValid(&board) || !PHOrbGoalsValid(&options.goals) || !options.steps || options.steps > PHOrbMaxSteps ||
        !options.beam || options.beam > 512 || !options.milliseconds || options.milliseconds > 1000 ||
        options.recoveryPasses > 1 || options.backtrackLimit > 2) return PHOrbSolveStatus::Invalid;
    if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
    try {
        const auto start = Clock::now(), deadline = start + std::chrono::milliseconds(options.milliseconds);
        const unsigned size = board.width * board.height;
        Cells initial{};
        std::copy(board.types, board.types + size, initial.begin());
        Cells initialThorns{};
        for (unsigned i = 0; i < size; ++i) initialThorns[i] = board.flags[i] & PHOrbFlagThorn ? board.thornDamage[i] : 0;
        const auto &t = options.thorns;
        const bool budgetKnown = t.known && t.hp > 0 && t.hp <= t.maximumHP && t.maximumHP <= INT32_MAX && t.pendingDamage < t.hp;
        const uint32_t thornBudget = budgetKnown ? t.hp - t.pendingDamage - 1 : 0;
        std::array<uint32_t, 128> damageTable{};
        for (unsigned p = 1; p < damageTable.size(); ++p) damageTable[p] = PHOrbThornDamage(p, t.maximumHP);
        const Context context = MakeContext(board, options.goals, true, options.allowPoisonWithHeartFour, options.diagonal);
        const Bits blocked = PHOrbBlockedCells(&board);
        // Shape guidance and shared spinner phases cost more than combo-only
        // evaluation. Check the clock between each such candidate.
        const unsigned deadlineMask = context.variants > 1 || context.shapeGoals ? 0u : options.goals.count ? 7u : 31u;
        if (!EnoughOrbs(initial, context)) return PHOrbSolveStatus::InsufficientOrbs;
        std::vector<Node> starts, beam, next;
        starts.reserve(size); beam.reserve(std::max(size, options.beam)); next.reserve(options.beam * (options.diagonal ? 8 : 4));
        for (unsigned i = 0; i < size; ++i) {
            if (!PHOrbCanStartAt(&board, i)) continue;
            Node node; node.cells = initial; node.thorns = initialThorns; node.path[0] = uint8_t(i); starts.push_back(node);
        }
        struct Frontier { unsigned depth; std::vector<Node> nodes; };
        std::vector<Frontier> saved;
        Node best;
        bool found = false, timedOut = false, hadContactCandidates = false;
        unsigned evaluated = 0, passes = 0, backtracks = 0, exploredDepth = 0;
        std::unordered_set<uint64_t> visited;
        visited.reserve(options.beam * (options.diagonal ? 8 : 4));
        const auto met = [&]() { return found && best.score.result.goalsMet && best.score.result.hazardsSafe; };
        const bool recover = context.shapeGoals && options.recoveryPasses;
        const bool retain = context.shapeGoals && options.backtrackLimit && options.steps >= 12;
        const auto primaryDeadline = recover ? start + (deadline - start) * 2 / 3 : deadline;
        auto search = [&](const Context &scoring, unsigned firstDepth, unsigned focus,
                          Clock::time_point passDeadline, bool saveFrontiers, bool noContacts = false) {
            ++passes;
            bool passEnded = false;
            for (unsigned depth = firstDepth; depth < options.steps && !passEnded && !timedOut; ++depth) {
                next.clear(); visited.clear();
                for (const Node &parent : beam) {
                    unsigned current = parent.path[parent.length - 1];
                    const int width = int(board.width);
                    const int neighbors[] = {int(current) - width, int(current) + width, int(current) - 1, int(current) + 1,
                        int(current) - width - 1, int(current) - width + 1, int(current) + width - 1, int(current) + width + 1};
                    for (unsigned direction = 0; direction < (options.diagonal ? 8u : 4u); ++direction) {
                        const int cell = neighbors[direction];
                        if (cancel.load(std::memory_order_relaxed)) return;
                        if ((evaluated & deadlineMask) == 0) {
                            const auto now = Clock::now();
                            if (now >= deadline) { timedOut = true; break; }
                            // A successful main search can spend the remaining
                            // budget improving combos. Otherwise reserve time
                            // for a different score or a discarded frontier.
                            if (now >= passDeadline && !met()) { passEnded = true; break; }
                        }
                        if (cell < 0 || !PHOrbMoveAdjacent(current, unsigned(cell), board.width, board.height, options.diagonal) ||
                            (blocked & (Bits(1) << cell)) ||
                            (parent.length > 1 && unsigned(cell) == parent.path[parent.length - 2])) continue;
                        Node child = parent;
                        if (child.thorns[cell]) {
                            const uint32_t damage = damageTable[child.thorns[cell]];
                            if (noContacts || !budgetKnown || uint64_t(child.thornDamage) + damage > thornBudget) continue;
                            child.thornDamage += damage; ++child.thornContacts;
                        }
                        child.path[child.length++] = uint8_t(cell);
                        std::swap(child.cells[current], child.cells[cell]);
                        std::swap(child.thorns[current], child.thorns[cell]);
                        if (!visited.insert(Hash(child, size)).second) continue;
                        child.score = Evaluate(child.cells, scoring);
                        hadContactCandidates |= child.thornContacts != 0;
                        ++evaluated; exploredDepth = std::max(exploredDepth, depth + 1);
                        // A branch can finish its heart four later. Keep the
                        // exact poison/heart and thorn gates on released candidates,
                        // without using it to discard intermediate repairs.
                        const bool heartPending = !child.score.result.heartFours && (child.thornContacts ||
                            (options.allowPoisonWithHeartFour && child.score.result.poisonErased));
                        if (heartPending) child.score.result.hazardsSafe = false;
                        if (!found || Better(child, best, options.thornComboPriority, options.allowPoisonWithHeartFour)) {
                            best = child; found = true;
                        }
                        if (heartPending) child.score.result.hazardsSafe =
                            ReleaseHazardsSafe(child.score.result, options.allowPoisonWithHeartFour, false);
                        next.push_back(child);
                    }
                    if (passEnded || timedOut) break;
                }
                if (next.empty() || passEnded || timedOut || depth + 1 == options.steps) break;
                // Keep at most two small frontiers from early and middle
                // depths. These are genuinely discarded nodes, not copies
                // of the beam that the first pass already expanded.
                const bool checkpoint = saveFrontiers && !met() && saved.size() < options.backtrackLimit &&
                    (depth + 1 == options.steps / 4 || depth + 1 == options.steps / 2);
                if (checkpoint) {
                    Frontier frontier{depth + 1, {}};
                    SelectBeam(next, beam, options.beam, depth, scoring, focus, options.thornComboPriority, &frontier.nodes);
                    if (!frontier.nodes.empty()) saved.push_back(std::move(frontier));
                } else SelectBeam(next, beam, options.beam, depth, scoring, focus, options.thornComboPriority);
            }
        };
        // A spinner/thorn board can make an early easy four crowd out the
        // harder exact shape. Start with that shape's progress while keeping
        // every original goal and release gate. Other passes retain the
        // ordinary objective, so the focused score does not authorize a move.
        unsigned primaryFocus = PHOrbMaxGoals, largestShape = 0;
        if (context.variants > 1 && context.shapeGoals > 1 &&
            std::any_of(initialThorns.begin(), initialThorns.end(), [](uint8_t value) { return value != 0; })) {
            for (unsigned g = 0; g < options.goals.count; ++g) {
                const auto &goal = options.goals.items[g];
                if (goal.kind < PHOrbGoalL || goal.kind == PHOrbGoalFour ||
                    goal.kind == PHOrbGoalRow || goal.kind == PHOrbGoalColumn) continue;
                const unsigned needed = Needed(goal, board.width, board.height) * goal.count;
                if (needed > largestShape) { largestShape = needed; primaryFocus = g; }
            }
        }
        beam = starts;
        search(context, 0, primaryFocus, primaryDeadline, retain);
        if (recover && !met() && !timedOut && !cancel.load(std::memory_order_relaxed)) {
            Context recovery = context;
            // Alternate layouts can collectively satisfy all phases even
            // when no single layout is phase-independent. Never delete those
            // possibilities from exact evaluation or the recovery search.
            recovery.stableHints = false;
            const auto now = Clock::now();
            const auto recoveryDeadline = saved.empty() ? deadline : now + (deadline - now) / 2;
            beam = starts;
            search(recovery, 0, found ? best.score.focus : 0, recoveryDeadline, false);
        }
        while (!saved.empty() && !met() && !timedOut && !cancel.load(std::memory_order_relaxed)) {
            auto frontier = std::move(saved.back()); saved.pop_back();
            const auto now = Clock::now();
            if (now >= deadline) { timedOut = true; break; }
            const auto passDeadline = saved.empty() ? deadline : now + (deadline - now) / 2;
            beam = std::move(frontier.nodes); ++backtracks;
            search(context, frontier.depth, found ? best.score.focus : 0, passDeadline, false);
        }
        // Permitting survivable thorn contacts changes beam competition even
        // if the final best path has no damage: temporary complete shapes can
        // crowd out contact-free paths with better eventual combos. Explore
        // that subspace separately using only the original remaining budget.
        // Keep the selected final ordering and the unchanged survival gates.
        // Skip the duplicate pass if blocked or unaffordable thorns never
        // admitted a contact candidate into any of the preceding beams.
        if (hadContactCandidates && !timedOut && !cancel.load(std::memory_order_relaxed) && Clock::now() < deadline) {
            beam = starts;
            search(context, 0, PHOrbMaxGoals, deadline, false, true);
        }
        if (cancel.load(std::memory_order_relaxed)) return PHOrbSolveStatus::Cancelled;
        if (!found) return PHOrbSolveStatus::NoRoute;
        route.count = best.length;
        route.diagonal = options.diagonal;
        std::copy(best.path.begin(), best.path.begin() + best.length, route.cells);
        route.combos = best.score.result.combos; route.erased = best.score.result.erased;
        route.goalCount = options.goals.count; route.matchedGoals = best.score.result.matchedGoals;
        route.goalsMet = best.score.result.goalsMet;
        route.hazardsSafe = best.score.result.hazardsSafe;
        route.poisonErased = best.score.result.poisonErased; route.unmatchedBombs = best.score.result.unmatchedBombs;
        route.mortalPoisonErased = best.score.result.mortalPoisonErased;
        route.thornDamage = best.thornDamage; route.thornContacts = best.thornContacts;
        route.heartFours = best.score.result.heartFours;
        std::copy(std::begin(best.score.result.goalHits), std::end(best.score.result.goalHits), route.goalHits);
        const auto finished = Clock::now();
        route.evaluated = evaluated; route.deadlineReached = timedOut || finished >= deadline;
        route.elapsedMS = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(finished - start).count());
        route.searchPasses = uint8_t(passes); route.backtracks = uint8_t(backtracks); route.exploredDepth = uint8_t(exploredDepth);
        PHOrbBoard replay{};
        if (!PHOrbReplay(&board, &route, route.count - 1, &replay) || std::memcmp(replay.types, best.cells.data(), size)) {
            route = {}; return PHOrbSolveStatus::Failed;
        }
        if (!route.hazardsSafe) return PHOrbSolveStatus::Unsafe;
        return route.goalsMet ? PHOrbSolveStatus::Solved : PHOrbSolveStatus::GoalsUnmet;
    } catch (...) { route = {}; return PHOrbSolveStatus::Failed; }
}
