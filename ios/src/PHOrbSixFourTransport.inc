// A search hint for an exactly-four colour. Unlike independent donor
// distances, this abstraction includes the held orb and the continuous drag.
// All other colours are interchangeable. Reverse BFS gives the minimum moves
// to a stable four in that relaxed problem; it never authorizes a release.
class SixFourTransport {
    const Context &_context;
    unsigned _size, _choose[PHOrbMaxCells+1][5]{};
    std::array<Bits,PHOrbMaxCells> _moves{};
    std::vector<Bits> _targets;
    std::array<std::vector<uint8_t>,2> _distance;
    unsigned rank(Bits bits) const {
        unsigned result=0, k=1;
        for (;bits;bits&=bits-1) result+=_choose[__builtin_ctzll(bits)][k++];
        return result;
    }
public:
    SixFourTransport(const Context &context, unsigned color) : _context(context),
        _size(context.width*context.height) {
        for (unsigned n=0;n<=_size;++n) {
            _choose[n][0]=1;
            for (unsigned k=1;k<=4;++k) _choose[n][k]=n ? _choose[n-1][k-1]+_choose[n-1][k] : 0;
        }
        for (unsigned i=0;i<_size;++i) _moves[i]=MoveNeighbours(Bits(1)<<i,context)&~context.blocked;
        Bits possible=0;
        for (unsigned phase=0;phase<context.variants;++phase)
            for (Bits rest=context.dynamicSpinners;rest;rest&=rest-1) {
                const unsigned i=unsigned(__builtin_ctzll(rest));
                if (context.spinnerStates[phase][i]==color) possible|=Bits(1)<<i;
            }
        for (const auto &pattern:context.patterns[0])
            if (!(pattern.cells&context.blocked) &&
                !(MarkedRuns(pattern.cells|possible,context)&pattern.border)) _targets.push_back(pattern.cells);
    }
    bool build(bool held, const std::atomic_bool &cancel, Clock::time_point deadline) {
        auto &distance=_distance[unsigned(held)];
        if (!distance.empty()) return true;
        const auto interrupted=[&]() { return cancel.load(std::memory_order_relaxed) || Clock::now()>=deadline; };
        if (_targets.empty() || interrupted()) return false;
        const unsigned count=held ? 3 : 4;
        // <= 42*C(42,4) bytes. Queue and unranking data are temporary and
        // released after setup; the two distance tables live for this solve.
        std::vector<uint8_t> pending(size_t(_choose[_size][count])*_size,255);
        std::vector<Bits> masks(_choose[_size][count]);
        std::vector<uint32_t> queue; queue.reserve(pending.size());
        unsigned generated=0;
        for (Bits subset=(Bits(1)<<count)-1;subset<(Bits(1)<<_size);) {
            if (!(generated++&1023u) && interrupted()) return false;
            masks[rank(subset)]=subset;
            const Bits low=subset&(~subset+1), next=subset+low;
            subset=next|(((next^subset)>>2)/low);
        }
        const auto offer=[&](Bits bits, unsigned current, unsigned depth) {
            const unsigned index=rank(bits)*_size+current;
            if (pending[index]!=255) return;
            pending[index]=uint8_t(depth); queue.push_back(index);
        };
        for (Bits pattern:_targets) for (unsigned current=0;current<_size;++current)
            if (!(_context.blocked&(Bits(1)<<current)) && bool(pattern&(Bits(1)<<current))==held)
                offer(pattern&~(held ? Bits(1)<<current : 0),current,0);
        for (size_t at=0;at<queue.size();++at) {
            if (!(at&1023u) && interrupted()) return false;
            const unsigned index=queue[at], current=index%_size, depth=pending[index]+1;
            // Longer abstract walks cannot help an at-most-48-step gesture.
            if (depth>PHOrbMaxSteps) continue;
            const Bits mask=masks[index/_size];
            for (Bits rest=_moves[current];rest;rest&=rest-1) {
                const unsigned cell=unsigned(__builtin_ctzll(rest));
                const Bits child=(mask&(Bits(1)<<cell)) ? mask^((Bits(1)<<current)|(Bits(1)<<cell)) : mask;
                offer(child,cell,depth);
            }
        }
        if (interrupted()) return false;
        distance.swap(pending); return true;
    }
    unsigned cost(Bits color, unsigned current, bool held) const {
        const auto &distance=_distance[unsigned(held)];
        return distance.empty() ? 255 : distance[rank(color&~(held ? Bits(1)<<current : 0))*_size+current];
    }
};
