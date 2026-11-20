// Private repair geometry checks. These do not claim that bounded search can
// solve the recorded B5 corpus; the separate acceptance probe measures that.
static void SixFourRepairTests() {
    PHOrbBoard board{}; board.width=7; board.height=6; board.minErase=3;
    for (unsigned y=0;y<6;++y) for (unsigned x=0;x<7;++x) {
        const unsigned i=y*7+x;
        board.types[i]=uint8_t(x<4 ? y : (y+x-3)%6); board.tokens[i]=i+1;
        board.enhancement[i]=i%2 ? UINT32_C(1031127695) : UINT32_C(3204448256);
    }
    board.spinnerMask=UINT64_C(1)<<41; board.spinnerColors=63;
    board.tapeMask=UINT64_C(1)<<6; board.flags[6]=PHOrbFlagTape; board.fixedStart=1;
    const uint8_t target[]={0,1,8,9,16,17,24};
    // Undo a known legal gesture from an independently constructed six-four
    // layout. The broken candidate takes the other corner at the beginning;
    // every later swap is identical, so tail-only repair cannot replace it.
    for (unsigned i=unsigned(sizeof(target))-1;i>0;--i) std::swap(board.types[target[i]],board.types[target[i-1]]);
    auto options=PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.steps=6; options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true,0,true};
    options.goals.count=6;
    for (unsigned c=0;c<6;++c) options.goals.items[c]={PHOrbGoalFour,uint8_t(1u<<c),1,0};
    Check(SixFourRepairEligible(board,options),"strict six-four ordinary geometry enters bounded middle repair");
    for (unsigned scenario=0;scenario<6;++scenario) {
        auto b=board; auto o=options;
        if (scenario==0) o.fixedBoard.mode=PHOrbFixedBoardMode::SixColorStall;
        if (scenario==1) o.fixedBoard.requireSixColorFours=false;
        if (scenario==2) o.fishCell=41;
        if (scenario==3) b.flags[10]=PHOrbFlagThorn;
        if (scenario==4) b.types[10]=PHOrbPoison;
        if (scenario==5) o.activity.firstWaveOnly=true;
        Check(!SixFourRepairEligible(b,o),"repair never expands stall, legacy quality, fishing, thorn, special or activity policies");
    }
    const auto context=MakeContext(board,options.goals,true,false,false,{},-1,0,options.fixedBoard);
    // Cached hints update only changed colours. Check each pass against a
    // fresh guide over legal walks, including shared spinners and tape.
    SixFourGuide guide(context), fresh(context);
    SixFourGuideNode guided;
    guided.cells=BoardCells(board);
    for (unsigned i=0;i<42;++i) if (!(context.dynamicSpinners&(Bits(1)<<i)))
        guided.colors[board.types[i]]|=Bits(1)<<i;
    for (unsigned pass=0;pass<4;++pass) {
        guide.pass(pass);
        for (unsigned c=0;c<6;++c) guide.color(guided,c);
        unsigned current=0;
        for (unsigned step=0;step<128;++step) {
            Bits neighbors=MoveNeighbours(Bits(1)<<current,context)&~context.blocked;
            unsigned choice=(step*17+pass)%Count(neighbors);
            while (choice--) neighbors&=neighbors-1;
            const unsigned next=unsigned(__builtin_ctzll(neighbors));
            const unsigned a=guided.cells[current],b=guided.cells[next];
            std::swap(guided.cells[current],guided.cells[next]);
            if (a!=b) {
                const Bits swapped=(Bits(1)<<current)|(Bits(1)<<next);
                guided.colors[a]^=swapped; guided.colors[b]^=swapped;
                guide.color(guided,a); guide.color(guided,b);
            }
            auto checked=guided; fresh.pass(pass);
            for (unsigned c=0;c<6;++c) fresh.color(checked,c);
            Check(checked.values==guided.values && checked.layouts==guided.layouts && checked.mask==guided.mask,
                "incremental colour hints and cache reuse match a fresh full update");
            current=next;
        }
    }
    PHOrbRoute witness{}; witness.count=sizeof(target); std::copy(std::begin(target),std::end(target),witness.cells);
    PHOrbEvaluation exact{}; uint32_t damage=0; uint8_t contacts=0;
    Check(PHOrbEvaluateRoute(board,witness,options,exact,damage,contacts) && exact.goalsMet &&
        exact.firstWaveFourColors==63 && !damage && !contacts,"the inverse fixture has an actual six-phase six-four witness");
    PHOrbRoute bad=witness; bad.cells[1]=7;
    PHOrbBoard replay{};
    Check(PHOrbReplay(&board,&bad,bad.count-1,&replay),"the incomplete seed is a legal continuous gesture");
    Node seed; seed.cells=BoardCells(replay); seed.length=bad.count;
    std::copy_n(bad.cells,bad.count,seed.path.begin()); seed.score=Evaluate(seed.cells,context);
    Check(!seed.score.result.goalsMet,"changing the middle approach destroys at least one required four");
    std::atomic_bool cancel{false}; bool foundWitness=false; unsigned candidates=0;
    for (bool diagonal : {false,true}) {
        auto o=options; o.diagonal=diagonal; o.milliseconds=800;
        PHOrbRoute route{};
        const auto status=PHOrbSolve(board,o,cancel,route);
        Check(status==PHOrbSolveStatus::Solved && route.sixFourGuidePasses && route.sixFourGuidePasses<=48 &&
            route.sixFourGuideCandidates && route.count<=o.steps+1 && route.cells[0]==0 &&
            PHOrbEvaluateRoute(board,route,o,exact,damage,contacts) && exact.goalsMet && exact.firstWaveFourColors==63,
            "cached guide finds a constructed inverse route with fixed start, tape, spinner and complete replay");
        cancel=true; route={};
        Check(PHOrbSolve(board,o,cancel,route)==PHOrbSolveStatus::Cancelled && !route.count,
            "cancelled strict guide publishes no gesture");
        cancel=false;
    }
    VisitSixFourPatches(seed,options,context,cancel,Clock::now()+std::chrono::seconds(2),[&](Node &candidate) {
        ++candidates;
        PHOrbRoute route{}; route.count=candidate.length;
        std::copy_n(candidate.path.begin(),candidate.length,route.cells);
        PHOrbBoard checked{};
        Check(route.count<=options.steps+1 && route.cells[0]==bad.cells[0] &&
            PHOrbReplay(&board,&route,route.count-1,&checked) && BoardCells(checked)==candidate.cells,
            "every splice replays its original suffix exactly, stays bounded and respects fixed-start/tape/spinners");
        if (route.count==sizeof(target) && std::equal(std::begin(target),std::end(target),route.cells)) {
            foundWitness=true;
            Check(PHOrbEvaluateRoute(board,route,options,exact,damage,contacts) && exact.goalsMet &&
                exact.hazardsSafe && exact.firstWaveFourColors==63,"a middle rewrite restores all six fours in every shared phase");
        }
        return !foundWitness;
    });
    Check(candidates && foundWitness,"full-route repair can replace an early corner with the known complete witness");
    unsigned forbidden=0; cancel=true;
    auto visit=[&](Node &) { ++forbidden; return true; };
    VisitSixFourPatches(seed,options,context,cancel,Clock::now()+std::chrono::seconds(2),visit);
    cancel=false;
    VisitSixFourPatches(seed,options,context,cancel,Clock::now()-std::chrono::seconds(1),visit);
    Check(!forbidden,"cancellation and elapsed deadlines publish no new repair candidates");
    SixFourSeeds seeds(true); seeds.offer(seed,context);
    Node best=seed; EvaluationCache cache(true);
    unsigned evaluated=0,depth=0,passes=0,repairPasses=0,repairCandidates=0;
    RepairSixFours(options,context,seeds,cancel,Clock::now()+std::chrono::seconds(2),best,cache,
        evaluated,depth,passes,repairPasses,repairCandidates);
    Check(best.score.result.goalsMet && best.score.result.firstWaveFourColors==63 &&
        repairPasses>0 && repairPasses<=2 && passes==repairPasses && evaluated==repairCandidates &&
        repairCandidates>0 && depth<=options.steps,"repair keeps a fully validated winner and reports its own bounded search work");
}
