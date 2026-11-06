// Research-only inspection of production search hints; not an independent match oracle.
// Reuses the implementation in this translation unit. Link model objects, not Solver.o.
#include "../src/PHOrbSolver.cpp"
#include <cstdio>
#include <random>
#include <string>

static PHOrbGoalSet Goals(unsigned kind,unsigned color=1,unsigned count=1,unsigned length=0) {
    PHOrbGoalSet g{}; g.count=1; g.items[0]={uint8_t(kind),uint8_t(color),uint8_t(count),uint8_t(length)}; return g;
}
static PHOrbBoard Board(unsigned seed,unsigned red=10) {
    PHOrbBoard b{}; b.width=6;b.height=5;
    for(unsigned i=0;i<30;++i) { b.types[i]=i<red?0:uint8_t(1+(i-red)%5); b.tokens[i]=i+1; }
    std::mt19937 gen(seed);std::shuffle(b.types,b.types+30,gen);return b;
}
static Cells CellsOf(const PHOrbBoard &b) { Cells c{};std::copy(b.types,b.types+30,c.begin());return c; }
static std::array<Bits,PHOrbTypes> Colors(const PHOrbBoard &b) {std::array<Bits,PHOrbTypes> c{};for(unsigned i=0;i<30;++i)c[b.types[i]]|=Bits(1)<<i;return c;}
static bool Compatible(const ShapeHint&a,const ShapeHint&b) {return !(a.cells&b.cells)&&!(a.color==b.color&&(a.border&b.cells));}
// Reuse each production single-pattern scorer, without the top-six truncation.
static std::vector<ShapeHint> AllHints(const PHOrbBoard&b,const Context&ctx) {
    std::vector<ShapeHint> out; const auto colors=Colors(b);
    for(unsigned p=0;p<ctx.patterns[0].size();++p) {
        Context one=ctx;one.patterns[0]={ctx.patterns[0][p]};
        auto hs=FindShapeHints(colors,0,0,0,one);
        for(unsigned i=0;i<hs.count;++i) {auto h=hs.items[i];h.tag=uint16_t(1+p*6+h.color);out.push_back(h);}
    }
    std::sort(out.begin(),out.end(),[](const ShapeHint&a,const ShapeHint&b){return a.value>b.value||(a.value==b.value&&a.tag<b.tag);});return out;
}
static unsigned PairBest(const std::vector<ShapeHint>&hs) {
    unsigned best=0;for(unsigned a=0;a<hs.size();++a){best=std::max(best,hs[a].value);for(unsigned b=a+1;b<hs.size();++b)if(Compatible(hs[a],hs[b]))best=std::max(best,hs[a].value+hs[b].value);}return best;
}
static void Digits(const PHOrbBoard&b){for(unsigned i=0;i<30;++i)std::printf("%u",b.types[i]);}
static void Dump(const char*label,const PHOrbBoard&b,const PHOrbGoalSet&g) {
    auto ctx=MakeContext(b,g);auto s=EvaluateStatic(CellsOf(b),ctx);auto hs=FindShapeHints(Colors(b),0,0,0,ctx);auto all=AllHints(b,ctx);
    std::vector<ShapeHint> six(hs.items.begin(),hs.items.begin()+hs.count);
    std::printf("{\"type\":\"%s\",\"board\":\"",label);Digits(b);std::printf("\",\"kind\":%u,\"hits\":%u,\"progress\":%u,\"greedy\":%u,\"best6\":%u,\"bestAll\":%u,\"hints\":[",g.items[0].kind,s.result.goalHits[0],s.goalProgress[0],ShapeNear(hs,2),PairBest(six),PairBest(all));
    for(unsigned i=0;i<hs.count;++i)std::printf("%s{\"value\":%u,\"cells\":%llu}",i?",":"",hs.items[i].value,(unsigned long long)hs.items[i].cells);
    std::printf("]}\n");
}
int main() {
    for(unsigned kind:{unsigned(PHOrbGoalL),unsigned(PHOrbGoalT),unsigned(PHOrbGoalCross),unsigned(PHOrbGoalFour),unsigned(PHOrbGoalRow),unsigned(PHOrbGoalColumn)}) {
        const unsigned need=kind==PHOrbGoalSquare?18:kind==PHOrbGoalFour?8:kind==PHOrbGoalRow?12:10;
        unsigned considered=0,truncated=0,greedyLoss=0;bool dumpedCut=false,dumpedGreedy=false;
        for(unsigned seed=1;seed<=300;++seed){auto b=Board(seed,need);auto g=Goals(kind,1,2);auto ctx=MakeContext(b,g);auto score=EvaluateStatic(CellsOf(b),ctx);if(score.result.goalHits[0])continue;
            auto hs=FindShapeHints(Colors(b),0,0,0,ctx);auto all=AllHints(b,ctx);std::vector<ShapeHint> six(hs.items.begin(),hs.items.begin()+hs.count);++considered;
            auto a=ShapeNear(hs,2),b6=PairBest(six),ba=PairBest(all);truncated+=b6<ba;greedyLoss+=a<b6;
            if(!dumpedCut&&b6<ba){Dump("top6-loss",b,g);dumpedCut=true;}if(!dumpedGreedy&&a<b6){Dump("greedy-loss",b,g);dumpedGreedy=true;}
        }
        std::printf("{\"type\":\"summary\",\"kind\":%u,\"count\":2,\"samples\":%u,\"top6Loss\":%u,\"greedyLoss\":%u}\n",kind,considered,truncated,greedyLoss);
    }
    // Only the nominated component changes; both boards have six red orbs.
    // A 2x2 square is connected but has no run. A line is already removable.
    for(unsigned variant=0;variant<3;++variant){auto b=Board(1,0);for(unsigned i=0;i<30;++i)b.types[i]=uint8_t(1+(i%6+2*(i/6))%5);
        std::vector<unsigned> red=variant==0?std::vector<unsigned>{0,1,6,7,20,29}:variant==1?std::vector<unsigned>{0,1,2,6,20,29}:std::vector<unsigned>{0,1,6,7,12,13};for(auto i:red)b.types[i]=0;
        auto g=Goals(PHOrbGoalMatch,1,1,6);auto s=EvaluateStatic(CellsOf(b),MakeContext(b,g));std::printf("{\"type\":\"connected-gradient\",\"variant\":%u,\"board\":\"",variant);Digits(b);std::printf("\",\"hits\":%u,\"progress\":%u,\"combos\":%u}\n",s.result.goalHits[0],s.goalProgress[0],s.result.combos);
    }
}
