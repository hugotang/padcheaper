static void SixFourTransportTests() {
    // Exhaust the small board's four-orb states. The public first-wave
    // evaluator supplies the targets; forward legal swaps supply Bellman's
    // equation independently of the table's reverse search and ranking.
    for (bool diagonal:{false,true}) {
        PHOrbBoard board{}; board.width=5; board.height=4; board.minErase=3;
        const unsigned cells[]={0,1,2,3,5,6,7,8,10,11,12};
        Bits available=0; for (unsigned cell:cells) available|=Bits(1)<<cell;
        board.spinnerMask=Bits(1)<<13; board.spinnerColors=63;
        board.tapeMask=((Bits(1)<<20)-1)&~(available|board.spinnerMask);
        for (unsigned i=0;i<20;++i) {
            board.types[i]=1; board.tokens[i]=i+1;
            if (board.tapeMask&(Bits(1)<<i)) board.flags[i]=PHOrbFlagTape;
        }
        auto options=PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
        options.diagonal=diagonal; options.goals.count=1; options.goals.items[0]={PHOrbGoalFour,1,1,0};
        const auto context=MakeContext(board,options.goals,true,false,diagonal);
        SixFourTransport table(context,0);
        std::atomic_bool cancel{true};
        Check(!table.build(true,cancel,Clock::now()+std::chrono::seconds(2)),"transport setup honors cancellation before allocating a search table");
        cancel=false;
        Check(!table.build(false,cancel,Clock::now()-std::chrono::seconds(1)),"expired transport setup publishes no partial table");
        Check(table.build(false,cancel,Clock::now()+std::chrono::seconds(2)) &&
            table.build(true,cancel,Clock::now()+std::chrono::seconds(2)),"both held-orb transport abstractions finish on a small board");
        unsigned states=0;
        for (unsigned subset=0;subset<(1u<<11);++subset) {
            if (Count(subset)!=4) continue;
            Bits colors=0;
            for (unsigned i=0;i<11;++i) if (subset&(1u<<i)) colors|=Bits(1)<<cells[i];
            for (unsigned i=0;i<20;++i) board.types[i]=(colors&(Bits(1)<<i)) ? 0 : 1;
            PHOrbEvaluation evaluation{};
            Check(PHOrbEvaluateWithOptions(board,options,evaluation),"public evaluator accepts the independent transport target board");
            const bool target=(evaluation.firstWaveFourColors&1)!=0;
            for (unsigned current:cells) {
                const bool held=(colors&(Bits(1)<<current))!=0;
                const unsigned distance=table.cost(colors,current,held);
                unsigned nearest=255;
                for (unsigned next:cells) {
                    if (!PHOrbMoveAdjacent(current,next,5,4,diagonal)) continue;
                    Bits child=colors;
                    if (bool(colors&(Bits(1)<<current))!=bool(colors&(Bits(1)<<next)))
                        child^=(Bits(1)<<current)|(Bits(1)<<next);
                    nearest=std::min(nearest,table.cost(child,next,held));
                }
                Check((distance==0)==target && (target || distance==std::min(255u,nearest+1)),
                    "every transport state has exactly the public targets and the shortest legal continuous-drag distance");
                ++states;
            }
        }
        Check(states==3630,"all four-orb placements and legal endpoints were covered in each movement mode");
    }
}
