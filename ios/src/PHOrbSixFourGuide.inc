#include "PHOrbSixFourTransport.inc"
// Search-only guidance for six first-removal fours. A swap changes at most
// two colour masks, so cache each colour's template work independently. This
// is never a release evaluator: every proposed winner goes through Evaluate
// with all original shared phases, and then the ordinary route replay.
struct SixFourGuideNode {
    Cells cells{};
    std::array<Bits,PHOrbBasicTypes> colors{};
    std::array<uint16_t,PHOrbBasicTypes> values{}, layouts{};
    std::array<uint8_t,PHOrbMaxSteps+1> path{};
    unsigned score = 0;
    uint64_t key = 0;
    uint8_t length = 1, mask = 0;
};

class SixFourGuide {
    struct Entry { Bits key = UINT64_MAX; uint16_t value = 0, layout = 0; bool hit = false; };
    const Context &_context;
    const std::vector<Pattern> &_patterns;
    std::array<Bits,PHOrbBasicTypes> _possible{};
    std::vector<Entry> _cache;
    unsigned _distanceWeight = 40, _holeWeight = 150, _borderWeight = 150, _hitBonus = 500, _focus = PHOrbBasicTypes;
public:
    explicit SixFourGuide(const Context &context) : _context(context), _patterns(context.patterns[0]),
        _cache(PHOrbBasicTypes * 4096) {
        for (unsigned phase=0;phase<context.variants;++phase)
            for (Bits rest=context.dynamicSpinners;rest;rest&=rest-1) {
                const unsigned cell=unsigned(__builtin_ctzll(rest));
                _possible[context.spinnerStates[phase][cell]] |= Bits(1)<<cell;
            }
    }
    void pass(unsigned index) {
        const unsigned round=index/2;
        _holeWeight=index&1 ? 0 : 150;
        _distanceWeight=index&1 ? 8 : 40;
        _hitBonus=index&1 ? 0 : 500;
        _borderWeight=round>6 ? 100+50*(round%3) : 150;
        _focus=round ? (round-1)%PHOrbBasicTypes : unsigned(PHOrbBasicTypes);
        for (auto &entry:_cache) entry.key=UINT64_MAX;
    }
    void color(SixFourGuideNode &node, unsigned color) {
        const Bits orbs=node.colors[color];
        auto &entry=_cache[color*4096 + size_t(((orbs^(orbs>>19))*UINT64_C(0x9e3779b97f4a7c15))>>52)];
        if (entry.key!=orbs) {
            unsigned best=0, layout=0; bool hit=false;
            for (unsigned p=0;p<_patterns.size();++p) {
                const auto &pattern=_patterns[p];
                // Stable templates guide exploration. Different per-phase
                // alternatives remain possible in the general fallback.
                if (pattern.cells&_context.dynamicSpinners) continue;
                Bits holes=pattern.cells&~orbs;
                if (holes&_context.blocked) continue;
                const unsigned missing=Count(holes);
                if (!missing && !(MarkedRuns(orbs|_possible[color],_context)&pattern.border)) {
                    hit=true; best=1000; layout=p; break;
                }
                constexpr unsigned filledScore[5]={950,700,420,160,0};
                unsigned value=_holeWeight ? 950-_holeWeight*missing : filledScore[missing];
                if (value<=best) continue;
                unsigned cost=_borderWeight*Count(MarkedRuns(orbs|pattern.cells|_possible[color],_context)&pattern.border);
                Bits supply=orbs&~pattern.cells&~_context.blocked;
                while (holes && cost<value-best) {
                    unsigned cell=unsigned(__builtin_ctzll(holes));
                    if (_holeWeight) {
                        // Assign different donors, hardest hole first. This
                        // is a heuristic, not a feasibility or route bound.
                        unsigned farthest=0;
                        for (Bits todo=holes;todo;todo&=todo-1) {
                            const unsigned target=unsigned(__builtin_ctzll(todo));
                            unsigned distance=1;
                            while (distance<=_context.distanceDepth[target] &&
                                !(supply&_context.distanceMasks[target][distance])) ++distance;
                            if (distance>farthest) { farthest=distance; cell=target; }
                        }
                    }
                    holes&=~(Bits(1)<<cell);
                    unsigned distance=1;
                    while (distance<=_context.distanceDepth[cell] &&
                        !(supply&_context.distanceMasks[cell][distance])) ++distance;
                    cost+=_distanceWeight*(distance-1);
                    if (_holeWeight && distance<=_context.distanceDepth[cell]) {
                        const Bits nearest=supply&_context.distanceMasks[cell][distance];
                        supply&=~(nearest&(~nearest+1));
                    }
                }
                value-=std::min(value,cost);
                if (value>best) { best=value; layout=p; }
            }
            entry={orbs,uint16_t(best),uint16_t(layout),hit};
        }
        node.values[color]=entry.value; node.layouts[color]=entry.layout;
        node.mask=uint8_t((node.mask&~(1u<<color)) | (entry.hit ? 1u<<color : 0));
    }
    void total(SixFourGuideNode &node) const {
        node.score=0; node.key=UINT64_C(14695981039346656037);
        for (unsigned c=0;c<PHOrbBasicTypes;++c) {
            node.score+=node.values[c]*(c==_focus ? 2u : 1u);
            node.key^=node.colors[c]; node.key*=UINT64_C(1099511628211);
        }
        node.key^=node.path[node.length-1]; node.key*=UINT64_C(1099511628211);
        node.score+=_hitBonus*Count(node.mask);
    }
};

bool SearchSixFourGuide(const PHOrbBoard &board, const PHOrbSolveOptions &options, const Context &context,
    const std::atomic_bool &cancel, Clock::time_point deadline, Node &best, bool &found,
    unsigned &evaluated, unsigned &exploredDepth, unsigned &passes, unsigned &guidePasses,
    unsigned &guideCandidates, EvaluationCache &cache) {
    SixFourGuide guide(context);
    const unsigned size=context.width*context.height;
    const auto interrupted=[&]() { return cancel.load(std::memory_order_relaxed) || Clock::now()>=deadline; };
    std::vector<SixFourGuideNode> beam, next;
    beam.reserve(std::max(size,options.beam)); next.reserve(options.beam*(options.diagonal ? 8u:4u));
    VisitedSet visited(options.beam*(options.diagonal ? 8u:4u));
    std::array<unsigned,PHOrbBasicTypes> inventory{};
    for (unsigned i=0;i<size;++i) if (!(context.dynamicSpinners&(Bits(1)<<i))) ++inventory[board.types[i]];
    unsigned rare=0;
    for (unsigned c=1;c<PHOrbBasicTypes;++c) if (inventory[c]<inventory[rare]) rare=c;
    bool transportEligible=!options.diagonal && inventory[rare]==4;
    // A stationary orb on tape/locked spinner needs a different target set.
    // Preserve the ordinary template passes for those boards.
    for (unsigned i=0;i<size;++i)
        if (board.types[i]==rare && (context.blocked&(Bits(1)<<i)) &&
            !(context.dynamicSpinners&(Bits(1)<<i))) transportEligible=false;
    SixFourTransport transport(context,rare);
    for (unsigned pass=0;pass<48 && !interrupted();++pass) {
        // Keep the first ordinary opportunities and the full diagonal policy.
        // Orthogonal four-orb supply tries a complete colour-focus cycle in
        // the small held-colour table before building the larger other-colour
        // table. Later passes alternate the two lanes. Other inventories get
        // two rare-colour passes before resuming
        // ordinary guidance. All setup and exploration share this deadline.
        const bool transportPass=transportEligible && pass>=2;
        const bool rarePass=!options.diagonal && inventory[rare]>4 && (pass==2 || pass==3);
        constexpr unsigned cycle=2*(PHOrbBasicTypes+1);
        const unsigned transportPassIndex=pass>=2 ? pass-2 : 0;
        const unsigned hintPass=transportPass ? transportPassIndex<2*cycle ? transportPassIndex%cycle :
            cycle+(transportPassIndex-2*cycle)/2 : rarePass ? pass-2 :
            !options.diagonal && inventory[rare]>4 && pass>=4 ? pass-2 : pass;
        const bool held=transportPass && (transportPassIndex<cycle ||
            (transportPassIndex>=2*cycle && !(transportPassIndex&1)));
        if (transportPass && !transport.build(held,cancel,deadline)) break;
        ++passes; ++guidePasses; guide.pass(hintPass);
        SixFourGuideNode initial;
        std::copy_n(board.types,size,initial.cells.begin());
        for (unsigned i=0;i<size;++i) if (!(context.dynamicSpinners&(Bits(1)<<i)))
            initial.colors[initial.cells[i]]|=Bits(1)<<i;
        for (unsigned c=0;c<PHOrbBasicTypes;++c) guide.color(initial,c);
        beam.clear();
        for (unsigned i=0;i<size;++i) if (PHOrbCanStartAt(&board,i) &&
            (!transportPass || (board.types[i]==rare)==held)) {
            auto node=initial; node.path[0]=uint8_t(i); guide.total(node); beam.push_back(node);
        }
        SixFourGuideNode partial=initial;
        for (unsigned depth=0;depth<options.steps && !interrupted();++depth) {
            next.clear(); visited.clear();
            for (const auto &parent:beam) {
                if (interrupted()) break;
                const unsigned current=parent.path[depth];
                Bits neighbors=MoveNeighbours(Bits(1)<<current,context)&~context.blocked;
                if (depth) neighbors&=~(Bits(1)<<parent.path[depth-1]);
                while (neighbors) {
                    const unsigned cell=unsigned(__builtin_ctzll(neighbors)); neighbors&=neighbors-1;
                    auto child=parent; child.path[child.length++]=uint8_t(cell);
                    const unsigned a=child.cells[current], b=child.cells[cell];
                    std::swap(child.cells[current],child.cells[cell]);
                    if (a!=b) {
                        const Bits moved=(Bits(1)<<current)|(Bits(1)<<cell);
                        child.colors[a]^=moved; child.colors[b]^=moved;
                        guide.color(child,a); guide.color(child,b);
                    }
                    guide.total(child); ++evaluated; ++guideCandidates;
                    exploredDepth=std::max(exploredDepth,depth+1);
                    if (!visited.insert(child.key)) continue;
                    if (child.score>partial.score) partial=child;
                    if (child.mask==63) {
                        Node candidate; candidate.cells=child.cells; candidate.path=child.path; candidate.length=child.length;
                        candidate.score=Evaluate(candidate.cells,context); ++cache.calls;
                        if (candidate.score.result.goalsMet && candidate.score.result.hazardsSafe) {
                            best=candidate; found=true; return true;
                        }
                    }
                    if (transportPass) {
                        const unsigned cost=transport.cost(child.colors[rare],cell,held);
                        if (cost>options.steps-depth-1) continue;
                        const unsigned penalty=cost*(held ? 60u : 20u)+(held ? 0u : child.values[rare]);
                        child.score-=std::min(child.score,penalty);
                    } else if (rarePass) child.score+=4*child.values[rare];
                    next.push_back(child);
                }
            }
            if (interrupted() || next.empty()) break;
            const auto order=[](const auto &a,const auto &b) { return a.score!=b.score ? a.score>b.score : a.key<b.key; };
            std::sort(next.begin(),next.end(),order);
            const size_t keep=std::min(next.size(),size_t(options.beam));
            beam.clear(); std::unordered_set<uint64_t> layouts;
            std::array<unsigned,PHOrbMaxCells> endpoints{};
            std::vector<bool> selected(next.size());
            for (size_t i=0;i<next.size() && beam.size()<keep;++i) {
                if (transportPass || rarePass) {
                    if (endpoints[next[i].path[depth+1]]++<std::max(1u,options.beam/32)) {
                        beam.push_back(next[i]); selected[i]=true;
                    }
                    continue;
                }
                uint64_t key=next[i].path[0];
                for (auto tag:next[i].layouts) { key^=tag; key*=UINT64_C(1099511628211); }
                key^=next[i].path[depth+1]/context.width;
                const bool distinct=layouts.insert(key).second;
                if (beam.size()<keep/2 || distinct) { beam.push_back(next[i]); selected[i]=true; }
            }
            for (size_t i=0;i<next.size() && beam.size()<keep;++i)
                if (!selected[i]) beam.push_back(next[i]);
        }
        if (partial.length>1 && !interrupted()) {
            Node candidate; candidate.cells=partial.cells; candidate.path=partial.path; candidate.length=partial.length;
            candidate.score=Evaluate(candidate.cells,context); ++cache.calls;
            if (!found || Better(candidate,best,false,context.allowPoisonWithHeartFour,context.activity,true,context.fixedBoard)) {
                best=candidate; found=true;
            }
        }
    }
    return false;
}
