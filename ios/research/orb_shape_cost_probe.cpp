// Local-only evaluation microbenchmark. It measures the cost of guidance,
// not route success, device speed, or the benefit of disabling guidance.
#include "PHOrbSolver.cpp"
#include <cstdio>
#include <random>

int main() {
    struct GoalCase { const char *name; uint8_t kind, length; };
    const GoalCase cases[] = {{"T",PHOrbGoalT,0},{"L",PHOrbGoalL,0},{"cross",PHOrbGoalCross,0},
        {"square",PHOrbGoalSquare,0},{"row",PHOrbGoalRow,0},{"column",PHOrbGoalColumn,0},
        {"four",PHOrbGoalFour,0},{"match-five",PHOrbGoalMatch,5},{"exact-five",PHOrbGoalExact,5}};
    PHOrbBoard board{}; board.width = 6; board.height = 5;
    // Shared inventory: twelve light, six heart and three each of four other
    // colors. Every shape has enough orbs, even with one light in a spinner.
    for (unsigned i = 0; i < 30; ++i) {
        board.types[i] = i < 12 ? 3 : i < 18 ? 5 : uint8_t((i - 18) / 3);
        if (i >= 27) board.types[i] = 4;
        board.tokens[i] = i + 1;
    }
    std::vector<Cells> layouts(1024);
    std::mt19937 random(11);
    for (auto &cells : layouts) {
        std::copy(board.types,board.types + 30,cells.begin());
        std::shuffle(cells.begin(),cells.begin() + 30,random);
    }
    uint64_t checksum = 0;
    unsigned invalid = 0;
    for (const auto &item : cases) for (bool spinner : {false,true}) {
        board.spinnerMask = spinner ? Bits(1) << 8 : 0; board.spinnerColors = spinner ? 63 : 0;
        PHOrbGoalSet goals{}; goals.count = 1; goals.items[0] = {item.kind,8,1,item.length};
        const auto exact = MakeContext(board,goals,false), guided = MakeContext(board,goals,true);
        for (const auto &cells : layouts)
            invalid += !SameCycleEvaluation(Evaluate(cells,exact).result,Evaluate(cells,guided).result);
        double times[2][3]{};
        for (unsigned run = 0; run < 3; ++run) for (unsigned policy = 0; policy < 2; ++policy) {
            const auto start = Clock::now();
            for (unsigned round = 0; round < 4; ++round) for (const auto &cells : layouts) {
                const auto value = Evaluate(cells,policy ? guided : exact);
                checksum += value.progress + value.result.combos + value.layouts[0];
            }
            times[policy][run] = double(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()) /
                (4.0 * layouts.size());
        }
        for (auto &values : times) std::sort(std::begin(values),std::end(values));
        std::printf("{\"goal\":\"%s\",\"spinner\":%s,\"exactNS\":%.0f,\"guidedNS\":%.0f}\n",
            item.name,spinner ? "true" : "false",times[0][1],times[1][1]);
    }
    std::fprintf(stderr,"checksum=%llu mismatched-evaluations=%u\n",static_cast<unsigned long long>(checksum),invalid);
    return invalid ? 1 : 0;
}
