// Conservative lower bounds from conjunctive goals. The caller constrains
// match/color goals to the first removal whenever this credit is used.
static unsigned ComboGoalLength(const PHOrbGoal *g, unsigned w, unsigned h) {
    switch (g->kind) {
        case PHOrbGoalMatch: case PHOrbGoalExact: return g->length;
        case PHOrbGoalFour: return 4;
        case PHOrbGoalL: case PHOrbGoalCross: case PHOrbGoalT: return 5;
        case PHOrbGoalSquare: return 9;
        case PHOrbGoalRow: return w;
        case PHOrbGoalColumn: return h;
        default: return 0;
    }
}
static unsigned ComboGoalGroups(const PHOrbGoalSet *goals, unsigned colors, unsigned length,
                                 unsigned kind, unsigned w, unsigned h) {
    unsigned result = 0;
    for (unsigned i = 0; i < goals->count; ++i) {
        const PHOrbGoal *g = &goals->items[i];
        if (!g->colors || (g->colors & ~colors)) continue;
        if (kind != UINT8_MAX && g->kind != kind) continue;
        if (ComboGoalLength(g,w,h) < length) continue;
        if (g->count > result) result = g->count; // Overlapping goals cannot be added.
    }
    return result;
}
static unsigned ComboGoalColors(const PHOrbGoalSet *goals, unsigned colors, unsigned w, unsigned h) {
    unsigned fixed = 0, lower = 0;
    for (unsigned c = 0; c < 6; ++c)
        if ((colors & (1u << c)) && ComboGoalGroups(goals,1u << c,3,UINT8_MAX,w,h)) fixed |= 1u << c;
    for (unsigned i = 0; i < goals->count; ++i) {
        const PHOrbGoal *g = &goals->items[i];
        if (g->kind != PHOrbGoalColors) continue;
        const unsigned outside = (unsigned)__builtin_popcount((unsigned)g->colors & ~colors);
        if (g->count > outside && g->count - outside > lower) lower = g->count - outside;
    }
    const unsigned distinct = (unsigned)__builtin_popcount(fixed);
    return distinct > lower ? distinct : lower;
}
static bool ComboAwakeningGoal(const PHOrbComboBonusState *s, const PHOrbGoalSet *g,
                               unsigned awakening, unsigned w, unsigned h) {
    const unsigned ids[] = {27,48,60,78,126};
    const unsigned kinds[] = {PHOrbGoalFour,PHOrbGoalSquare,PHOrbGoalL,PHOrbGoalCross,PHOrbGoalT};
    for (unsigned i = 0; i < 5; ++i) if (awakening == ids[i])
        return ComboGoalGroups(g,s->awakeningColors[i],3,kinds[i],w,h) > 0;
    if (awakening >= 149 && awakening <= 153)
        return (s->columnColors & (1u << (awakening - 149))) &&
            ComboGoalGroups(g,1u << (awakening - 149),3,PHOrbGoalColumn,w,h);
    return false; // Other awakenings need their own native activation proof.
}
unsigned PHOrbGuaranteedComboBonuses(const PHOrbComboBonusState *s, const PHOrbGoalSet *goals,
                                     unsigned w, unsigned h, unsigned perSource[3]) {
    if (perSource) memset(perSource,0,3 * sizeof(*perSource));
    if (!s || !s->valid || s->count > PHOrbMaxComboEffects || !PHOrbGoalsValid(goals) ||
        !PHOrbDimensionsSupported(w,h)) return 0;
    unsigned sums[3] = {0};
    static const unsigned awakeningIDs[] = {27,48,60,78,126,22,23,24,25,26,79,80,81,45,50,73,74,75,76,77,20,82,133,134,135};
    for (unsigned i = 0; i < s->count; ++i) {
        const PHOrbComboEffect *e = &s->effects[i]; bool active = false;
        if (e->source > 2 || !e->combos || e->combos > 20) return 0;
        switch (e->kind) {
            case PHOrbComboAllLinks:
                active = e->condition && e->condition <= 63 && e->minimum >= 3;
                for (unsigned c = 0; active && c < 6; ++c) if (e->condition & (1u << c))
                    active = ComboGoalGroups(goals,1u << c,e->minimum,UINT8_MAX,w,h) > 0;
                break;
            case PHOrbComboLink:
                active = e->condition && e->condition <= 63 && e->minimum >= 3 &&
                    ComboGoalGroups(goals,e->condition,e->minimum,UINT8_MAX,w,h); break;
            case PHOrbComboColors:
                active = e->condition && e->condition <= 63 && e->minimum &&
                    ComboGoalColors(goals,e->condition & (s->attackColors | 32u),w,h) >= e->minimum; break;
            case PHOrbComboGroups: {
                unsigned groups = 0, wanted[6] = {0};
                bool supported = true;
                for (unsigned k = 0; k < 5 && (int8_t)e->colors[k] >= 0; ++k) {
                    if (e->colors[k] >= 6) { supported = false; break; }
                    ++wanted[e->colors[k]];
                }
                for (unsigned c = 0; c < 6; ++c) if ((s->attackColors | 32u) & (1u << c)) {
                    const unsigned n = ComboGoalGroups(goals,1u << c,3,UINT8_MAX,w,h);
                    groups += n < wanted[c] ? n : wanted[c];
                }
                active = supported && e->minimum && groups >= e->minimum; break;
            }
            case PHOrbComboHeartCross: active = ComboGoalGroups(goals,32,5,PHOrbGoalCross,w,h); break;
            case PHOrbComboL: active = e->condition && e->condition <= 63 &&
                ComboGoalGroups(goals,e->condition,5,PHOrbGoalL,w,h); break;
            case PHOrbComboAwakenings:
            case PHOrbComboColumns: {
                const unsigned count = e->kind == PHOrbComboAwakenings ? 25 : 7;
                active = e->condition && !(e->condition >> count);
                for (unsigned b = 0; active && b < count; ++b) if (e->condition & (1u << b))
                    active = ComboAwakeningGoal(s,goals,e->kind == PHOrbComboAwakenings ? awakeningIDs[b] :
                        b < 5 ? 149 + b : b == 5 ? 44 : 59,w,h);
                break;
            }
            case PHOrbComboAwakening: active = ComboAwakeningGoal(s,goals,e->condition,w,h); break;
            default: break;
        }
        if (active) sums[e->source] += e->combos;
    }
    const unsigned total = sums[0] + sums[1] + sums[2];
    if (total > 100) return 0; // Do not model native byte-queue overflow.
    if (perSource) memcpy(perSource,sums,sizeof(sums));
    return total;
}
