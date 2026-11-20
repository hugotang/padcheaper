// Sanitized B5 failure corpus; source/limits: coral-b5-20261120.json.
// Geometry only: this is not a native damage, timing or kill oracle.
#pragma once
#include "PHOrbSolver.h"
#include <algorithm>
#include <iterator>

struct CoralB5Case {
    const char *version;
    uint8_t types[42], path[49], pathCount, spinner, firstFours, firstCombos;
    uint32_t enhancement[42];
    bool diagonal;
};
static constexpr CoralB5Case CoralB5Cases[] = {
    {"0.5.50-orbs",
     {2,0,3,4,1,1,2,4,5,4,2,4,2,3,5,1,5,0,4,3,4,2,4,3,2,3,2,0,0,0,2,5,1,2,2,2,1,1,5,3,0,3},
     {24,16,10,18,25,17,23,16,8,14,22,30,29,37,31,30,38,31,23,16,24,25,31,24,25,26,33,27,26,25,24,31,39,33,41,34,26,25,24,16,22,15,16,8,7,15,14}, 47, 32, 3, 7,
     {0,1031127695,1031127695,1031127695,1031127695,1031127695,3204448256,1031127695,3204448256,3204448256,0,0,0,0,1031127695,1031127695,1031127695,1031127695,0,1031127695,1031127695,0,0,1031127695,0,1031127695,1031127695,1031127695,1031127695,1031127695,1031127695,0,1031127695,1031127695,0,0,1031127695,0,0,1031127695,1031127695,0}, true},
    {"0.5.52-orbs",
     {5,1,4,5,5,2,0,3,0,4,2,2,5,4,2,5,1,5,0,2,1,3,2,4,1,1,3,0,2,4,5,3,0,1,3,0,0,1,1,5,3,1},
     {39,33,26,32,31,25,18,10,4,12,18,24,30,29,21,22,15,16,23,30,31,32,40,39,38,37,36,35,28,21,14,22,15,7}, 34, 8, 4, 8,
     {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, true},
    {"0.5.54-orbs",
     {0,3,4,2,0,3,2,1,4,5,5,0,5,4,1,1,1,4,4,2,0,1,1,0,4,4,3,5,3,0,0,0,1,1,1,2,1,5,0,2,5,3},
     {11,12,19,13,5,12,4,3,11,17,9,16,24,32,40,41,33,27,26,34,40,39,31,37,29,36,30,22,23,17,25,24,23,30,24,25,26,20,27,34,26,25,24,23,29,28,21,15,9}, 49, 8, 5, 7,
     {1031127695,0,1031127695,0,0,0,1031127695,0,1031127695,0,0,1031127695,0,0,0,1031127695,1031127695,0,0,3204448256,0,0,0,0,3204448256,0,0,0,1031127695,1031127695,1031127695,3204448256,1031127695,0,0,1031127695,1031127695,1031127695,1031127695,1031127695,1031127695,0}, true},
    {"0.5.58-orbs",
     {1,5,4,5,4,3,5,2,1,5,1,4,3,3,3,3,3,1,0,5,5,4,4,3,2,0,1,1,0,1,0,4,3,1,2,4,5,0,2,1,2,0},
     {2,9,10,17,16,23,30,31,24,25,18,19,12,11,18,19,12,5,4,11,12,19,26,33,40,39,32,31,38,37,36,29,22,21,28,29,22,21,14,7,0}, 41, 15, 4, 6,
     {0,1031127695,1031127695,1031127695,0,1031127695,0,0,1031127695,1031127695,1031127695,1031127695,1031127695,1031127695,1031127695,0,1031127695,1031127695,0,1031127695,0,1031127695,0,1031127695,1031127695,0,0,1031127695,0,0,0,0,1031127695,1031127695,0,0,1031127695,0,1031127695,0,0,1031127695}, false},
};
static inline PHOrbBoard CoralB5Board(const CoralB5Case &fixture) {
    PHOrbBoard board{}; board.width=7; board.height=6; board.minErase=3;
    board.spinnerMask=UINT64_C(1)<<fixture.spinner; board.spinnerColors=63;
    std::copy(std::begin(fixture.types),std::end(fixture.types),board.types);
    std::copy(std::begin(fixture.enhancement),std::end(fixture.enhancement),board.enhancement);
    for (unsigned i=0;i<42;++i) { board.tokens[i]=i+1; board.x[i]=int16_t((i%7)*40+20); board.y[i]=int16_t((i/7)*40+20); }
    return board;
}
static inline PHOrbSolveOptions CoralB5Options(const CoralB5Case &fixture) {
    auto options=PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough); options.diagonal=fixture.diagonal;
    options.fixedBoard={PHOrbFixedBoardMode::FourAttack,63,true,0,true};
    options.goals.count=6;
    for (unsigned c=0;c<6;++c) options.goals.items[c]={PHOrbGoalFour,uint8_t(1u<<c),1,0};
    return options;
}
