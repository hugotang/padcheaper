static void EightComboPriorityTests() {
    auto options = PHOrbOptionsForQuality(PHOrbSearchQuality::Thorough);
    options.activity.firstWaveOnly = options.activity.comboPriority = true; options.activity.colors = 17;
    options.activity.comboWeight = 1600; options.activity.stepCost = 35;
    options.goals = Goals(PHOrbGoalMatch,17,1,3); options.goals.comboFirstWave = true;
    std::atomic_bool cancel{false};
    // Generated boards only. Baseline minima came from 0.5.21's bounded beam;
    // the independent run/flood-fill oracle checks every selected route.
    const unsigned seeds[]{1,2,3,6,7,10,17,20,22,24};
    const unsigned minimum[]{8,6,7,7,7,8,8,7,8,8};
    for (unsigned n = 0; n < sizeof(seeds)/sizeof(*seeds); ++n) {
        const auto board = Board(6,5,seeds[n]); PHOrbRoute route{}; PHOrbBoard replay{};
        const auto status = PHOrbSolve(board,options,cancel,route);
        Check(status == PHOrbSolveStatus::Solved && route.count <= options.steps + 1 &&
            PHOrbReplay(&board,&route,route.count-1,&replay),"B1 priority stays within the original path budget");
        const auto actual = Oracle(replay,true);
        Check(actual.first == route.combos && actual.second == route.erased && actual.first >= minimum[n],
            "B1 generated-board routes meet the baseline and independently replay as first-removal combos");
    }
    options.steps = 4; options.milliseconds = 1000; options.backtrackLimit = 0;
    options.activity.comboWeight = 1; options.activity.stepCost = 10000;
    auto board = Board(6,5,6); PHOrbRoute priority{}, shortPath{};
    Check(PHOrbSolve(board,options,cancel,priority) == PHOrbSolveStatus::Solved,"Combo-first search finds a bounded candidate");
    options.activity.comboPriority = false;
    Check(PHOrbSolve(board,options,cancel,shortPath) == PHOrbSolveStatus::Solved &&
        priority.combos > shortPath.combos,"large step penalty cannot buy fewer B1 Combo in priority mode");
}
static void EightPreparationSolverTests() {
    auto board = Board(6,5,41);
    // Generated fixture with 2 fire, 5 water, 6 wood, 9 light, 7 dark, 1 heart.
    const uint8_t cells[30]{0,1,2,3,4,3, 1,2,3,4,0,1, 2,3,4,2,3,4, 3,4,1,3,2,3, 4,5,4,2,3,1};
    std::copy(std::begin(cells),std::end(cells),board.types);
    PHOrbSolveOptions options;
    options.steps = 12; options.beam = 512; options.milliseconds = 150;
    options.goals = Goals(PHOrbGoalCombo,0,1,0); options.goals.comboFirstWave = true;
    options.activity.firstWaveOnly = options.activity.preparation = true;
    options.activity.colors = 17; options.activity.primaryMinimum = 6; options.activity.stepCost = 35;
    options.activity.attackWeights[0] = 300; options.activity.attackWeights[4] = 100;
    const uint8_t map[6]{0,0,4,0,4,4}; std::copy(std::begin(map),std::end(map),options.activity.conversion);
    std::atomic_bool cancel{false}; PHOrbRoute route{}; PHOrbBoard replay{}; PHOrbEvaluation value{};
    uint32_t damage = 0; uint8_t contacts = 0;
    Check(PHOrbSolve(board,options,cancel,route) == PHOrbSolveStatus::Solved && route.count <= 13 &&
        PHOrbReplay(&board,&route,route.count-1,&replay) &&
        PHOrbEvaluateRoute(board,route,options,value,damage,contacts),"bounded preparation finds and independently replays a route");
    const auto first = Oracle(replay,true), all = Oracle(replay,false);
    Check(first.first == 1 && first.second == 3 && all == first && value.goalsMet && value.preparationReady &&
        value.retainedAfterConversion[0] >= 6 &&
        (value.retainedAfterConversion[0] >= 12 || value.retainedAfterConversion[4] >= 12),
        "one real three-orb group retains enough converted orbs without hypothetical drops or cascades");
    auto changed = options;
    for (unsigned c = 0; c < 6; ++c) changed.activity.conversion[c] = uint8_t(c);
    Check(PHOrbEvaluateWithOptions(replay,changed,value) && !value.goalsMet,
        "a different next-turn conversion cannot inherit the original reserve guarantee");
    auto invalid = board; invalid.flags[0] |= PHOrbFlagLocked;
    Check(PHOrbSolve(invalid,options,cancel,route) == PHOrbSolveStatus::Invalid &&
        !PHOrbEvaluateWithOptions(invalid,options,value),"locked preparation is rejected consistently in search and release");
    for (unsigned i = 0; i < 30; ++i) invalid.types[i] = 0;
    invalid.flags[0] = 0;
    Check(PHOrbEvaluateWithOptions(invalid,options,value) && !value.preparationReady,
        "one giant combo is not a resource-preserving three-orb preparation");

    options.activity.preparation = false; options.activity.comboWeight = 0;
    options.activity.attackWeights[0] = options.activity.attackWeights[4] = 0;
    options.activity.linkWeight = 2500; options.activity.linkRewardLimit = 1;
    const uint8_t both[30]{0,0,0,0,0,0, 0,0,0,0,0,0, 1,2,1,2,1,2, 4,4,4,4,4,4, 4,4,4,4,4,4};
    std::copy(std::begin(both),std::end(both),board.types);
    Check(PHOrbEvaluateWithOptions(board,options,value) && value.linkGroups[0] == 1 && value.linkGroups[4] == 1 &&
        value.activityValue == 2500,"two attack-color links keep both geometry facts but share the configured activation reward");
    options.activity.linkRewardLimit = 0;
    Check(PHOrbEvaluateWithOptions(board,options,value) && value.activityValue == 5000,
        "legacy profiles retain their explicit per-group heuristic");
}
static void EightSolverTests() {
    EightPreparationSolverTests();
    EightComboPriorityTests();
    auto s = MultiplayerModelState(8,7), other = s;
    for (uint32_t phase : {28u,29u,30u,31u,67u,68u}) {
        other = s; other.phase = phase;
        Check(PHOrbBattleEnded(&other) && !PHOrbEightMatchReady(&other) && !PHOrbCanPreview(&other),
            "native clear/result phases close Auto before the global finished flag or deadline");
        other.valid = false;
        Check(!PHOrbBattleEnded(&other),"an unverified phase cannot be published as a confirmed result");
        other.valid = true; other.present = false;
        Check(!PHOrbBattleEnded(&other),"an absent scene is not a confirmed Dungeon Clear");
    }
    for (uint32_t phase : {0u,2u,3u,6u,18u,19u,20u,22u,26u,27u,48u,58u,65u,69u,UINT32_MAX}) {
        other = s; other.phase = phase;
        Check(!PHOrbBattleEnded(&other),"normal floor/skill transitions and unknown phases are not inferred as clear");
    }
    other = s;
    other.wave = 1; ++other.multiplayer.turnSerial; other.multiplayer.resolvePending = 1; other.multiplayer.networkFlags = 3;
    Check(PHOrbEightMatchReady(&other) && PHOrbSameEightMatch(&s,&other) && !PHOrbSameTurn(&s,&other) &&
        !PHOrbMultiplayerWindow(&other),"8P match identity survives local resolve while its gesture window closes");
    other = s; ++other.multiplayer.clockStart;
    Check(!PHOrbSameEightMatch(&s,&other),"a reused scene with a new match clock requires another explicit Auto click");
    other = s; other.identity.entryHash[0]++;
    Check(!PHOrbSameEightMatch(&s,&other),"new entry nonce cannot inherit 8P Auto");
    other = MultiplayerModelState(4,3);
    Check(!PHOrbEightMatchReady(&other),"8P match helper never authorizes 4P");
    auto beforeSkill = s;
    beforeSkill.eight.valid = beforeSkill.automation.valid = true;
    beforeSkill.eight.members[1].skillKnown = true;
    beforeSkill.eight.members[1].activeSkill = 1001;
    beforeSkill.eight.members[1].catalogID = 1001726;
    beforeSkill.automation.skillUses = 2;
    auto afterSkill = beforeSkill;
    afterSkill.automation.skillUses = 3;
    afterSkill.eightReceipt = {1001,1001726,1,0,true};
    Check(PHOrbEightSkillAcknowledged(&beforeSkill,&afterSkill,1) &&
        !afterSkill.automation.members[1].skillUses[0],
        "ordinary 8P skills acknowledge with native root/owner/slot and total while limited uses remain zero");
    for (unsigned bad = 0; bad < 12; ++bad) {
        auto changed = afterSkill;
        if (bad == 0) changed.eightReceipt.valid = false;
        if (bad == 1) changed.eightReceipt.slot = 5;
        if (bad == 2) ++changed.eightReceipt.skill;
        if (bad == 3) ++changed.eightReceipt.catalogID;
        if (bad == 4) changed.eightReceipt.source = 1;
        if (bad == 5) --changed.automation.skillUses;
        if (bad == 6) ++changed.automation.skillUses;
        if (bad == 7) ++changed.multiplayer.clockStart;
        if (bad == 8) changed.automation.valid = false;
        if (bad == 9) changed.eight.valid = false;
        if (bad == 10) changed.eight.members[1].skillSignature = 42;
        if (bad == 11) changed.identity.entryHash[0]++;
        Check(!PHOrbEightSkillAcknowledged(&beforeSkill,&changed,1),
            "receipt needs exact owner, body, slot, skill, one new use and the original match/team");
    }
    beforeSkill.eight.members[5] = beforeSkill.eight.members[1];
    afterSkill.eight = beforeSkill.eight;
    Check(!PHOrbEightSkillAcknowledged(&beforeSkill,&afterSkill,5),"duplicate fixed cards cannot share a visible-slot receipt");
    beforeSkill.automation.skillUses = afterSkill.automation.skillUses = 255;
    Check(!PHOrbEightSkillAcknowledged(&beforeSkill,&afterSkill,1),"saturated use total cannot prove a new cast");
    PHOrbSolveOptions options;
    options.activity.firstWaveOnly = true; options.activity.colors = 17;
    options.activity.comboWeight = 400; options.activity.rowWeight = 1500; options.activity.linkWeight = 2500;
    options.goals.count = 2; options.goals.items[0] = {PHOrbGoalMatch,17,1,12};
    options.goals.items[1] = {PHOrbGoalCombo,0,4,0}; options.goals.comboFirstWave = true;
    auto board = Board(6,5,4);
    const uint8_t rows[30]{0,0,0,0,0,0, 0,0,0,0,0,0, 1,1,1,4,4,4, 2,2,2,5,5,5, 3,5,1,2,3,0};
    std::copy(std::begin(rows),std::end(rows),board.types);
    PHOrbEvaluation result{};
    Check(PHOrbEvaluateWithOptions(board,options,result) && result.goalsMet && result.firstWaveCombos == 5 &&
        result.rowGroups[0] == 1 && result.linkGroups[0] == 1 && result.largestGroups[0] == 12 && result.activityValue == 6000,
        "one connected twelve-group containing two rows counts once per shape, separate from combat bonuses");
    board.types[5] = board.types[11] = 3;
    Check(PHOrbEvaluateWithOptions(board,options,result) && !result.goalsMet && !result.linkGroups[0],
        "less than twelve removed in one group does not satisfy super-link goal");
    board = ShapeBoard(6,5,{0,1,2,6,7,8,12,13,14,18,19,20});
    options.goals = Goals(PHOrbGoalMatch,1,1,12);
    Check(PHOrbEvaluateWithOptions(board,options,result) && result.goalsMet && result.linkGroups[0] == 1 && !result.rowGroups[0],
        "twelve connected removed orbs need no full horizontal row");
    board = ShapeBoard(6,5,{0,1,2,3,4,5,18,19,20,21,22,23});
    Check(PHOrbEvaluateWithOptions(board,options,result) && !result.goalsMet && !result.linkGroups[0] && result.rowGroups[0] == 2,
        "two separated six-orb rows do not form a twelve-link");
    bool foundCascade = false;
    for (unsigned seed = 1; seed <= 100 && !foundCascade; ++seed) {
        board = Board(6,5,seed); PHOrbEvaluation ordinary{}, first{};
        PHOrbGoalSet noGoals{};
        if (!PHOrbEvaluate(board,noGoals,ordinary) || ordinary.combos == ordinary.firstWaveCombos) continue;
        options.goals = noGoals;
        Check(PHOrbEvaluateWithOptions(board,options,first) && first.combos == ordinary.firstWaveCombos &&
            first.combos < ordinary.combos,"8P objective never credits deterministic cascades as guaranteed native no-skyfall combos");
        foundCascade = true;
    }
    Check(foundCascade,"test exercised a board with deterministic cascade ambiguity");
    board = Board(6,5,72); options.goals = {}; options.activity.stepCost = 35;
    options.steps = 8; options.beam = 32; options.milliseconds = 1000;
    std::atomic_bool cancel{false}; PHOrbRoute route{};
    const auto status = PHOrbSolve(board,options,cancel,route);
    uint32_t damage = 0; uint8_t contacts = 0;
    Check(status == PHOrbSolveStatus::Solved && PHOrbEvaluateRoute(board,route,options,result,damage,contacts) &&
        result.combos == route.combos && result.goalsMet == route.goalsMet,
        "activity beam search and release replay use the same evaluation and goal policy");
}
