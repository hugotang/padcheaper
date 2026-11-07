// Included by orb_shape_guidance_tests.cpp to inspect the private final-route
// helper directly. No fixture asks beam search to rediscover a particular path.
static PHOrbBoard CycleRecordedBoard() {
    PHOrbBoard board{}; board.width = 6; board.height = 5; board.minErase = 3;
    const uint8_t types[] = {4,5,1,2,3,4, 0,0,4,5,5,4, 3,4,5,3,3,3,
                             5,5,5,3,3,3, 4,4,3,2,3,5};
    std::copy(std::begin(types),std::end(types),board.types);
    board.spinnerMask = UINT64_C(1) << 21; board.spinnerColors = 63;
    for (unsigned i = 0; i < 30; ++i) {
        board.tokens[i] = i + 1; board.x[i] = int16_t(i % 6); board.y[i] = int16_t(i / 6);
    }
    for (unsigned i : {4u,11u,19u,24u}) board.enhancement[i] = 1031127695u;
    board.enhancement[17] = 3204448256u;
    return board;
}

static bool CycleNode(const PHOrbBoard &board, const PHOrbSolveOptions &options,
    const Context &context, const std::vector<uint8_t> &path, Node &node) {
    PHOrbRoute route{}; route.count = uint8_t(path.size()); route.diagonal = options.diagonal;
    std::copy(path.begin(),path.end(),route.cells);
    PHOrbBoard replay{};
    if (!PHOrbReplay(&board,&route,route.count - 1,&replay)) return false;
    node = {}; node.length = route.count;
    std::copy(path.begin(),path.end(),node.path.begin());
    node.cells = BoardCells(replay); node.score = Evaluate(node.cells,context);
    return true;
}

static bool SameCycleTokensIndependently(const std::vector<uint8_t> &first, const Node &second, unsigned size) {
    std::array<unsigned,PHOrbMaxCells> a{}, b{};
    for (unsigned i = 0; i < size; ++i) a[i] = b[i] = i;
    for (unsigned i = 1; i < first.size(); ++i) std::swap(a[first[i - 1]],a[first[i]]);
    for (unsigned i = 1; i < second.length; ++i) std::swap(b[second.path[i - 1]],b[second.path[i]]);
    return a == b;
}

static void RouteSimplificationTests() {
    const std::vector<uint8_t> original{11,16,22,27,20,19,13,8,9,14,7,2,3,4,11,17,16,11,17,16,11,
        17,23,28,27,20,26,19,24,18,12,7,1,0,6,12,19,25,18,19,26,25,24};
    const std::vector<uint8_t> expected{11,16,22,27,20,19,13,8,9,14,7,2,3,4,11,
        17,23,28,27,20,26,19,24,18,12,7,1,0,6,12,19,25,18,19,26,25,24};
    auto board = CycleRecordedBoard();
    PHOrbSolveOptions options; options.diagonal = true;
    options.goals = Goal(PHOrbGoalT,8); options.goals.items[options.goals.count++] = {PHOrbGoalFour,32,1,0};
    const auto context = MakeContext(board,options.goals,true,false,true);
    Node node;
    Check(CycleNode(board,options,context,original,node) && node.score.result.goalsMet &&
        node.score.result.combos == 6, "recorded cycle fixture preserves the reported six-combo T and heart-four goals");
    const auto saved = node;
    std::atomic_bool cancel{false};
    const auto deadline = []() { return Clock::now() + std::chrono::seconds(10); };
    Check(SimplifyIdentityCycles(board,options,context,cancel,deadline(),node) && node.length == 37 &&
        std::equal(expected.begin(),expected.end(),node.path.begin()),
        "recorded 42-move route removes exactly the six-move identity cycle");
    Check(SameCycleTokensIndependently(original,node,30) && SameScore(saved.score,node.score) &&
        node.path[0] == saved.path[0] && node.path[node.length - 1] == saved.path[saved.length - 1],
        "cleanup preserves synthetic token identity, original scoring, and both gesture endpoints");
    PHOrbRoute beforeRoute{}, afterRoute{};
    beforeRoute.count = saved.length; afterRoute.count = node.length;
    beforeRoute.diagonal = afterRoute.diagonal = true;
    std::copy(saved.path.begin(),saved.path.begin() + saved.length,beforeRoute.cells);
    std::copy(node.path.begin(),node.path.begin() + node.length,afterRoute.cells);
    PHOrbBoard before{}, after{};
    Check(PHOrbReplay(&board,&beforeRoute,beforeRoute.count - 1,&before) &&
        PHOrbReplay(&board,&afterRoute,afterRoute.count - 1,&after) && PHOrbSameBoard(&before,&after),
        "shortened path preserves all per-orb metadata as well as colors");
    for (unsigned phase = 0; phase < 6; ++phase) {
        auto left = before, right = after;
        left.types[21] = right.types[21] = PHOrbSpinnerType(&board,21,phase);
        left.spinnerMask = right.spinnerMask = 0; left.spinnerColors = right.spinnerColors = 0;
        PHOrbEvaluation a{}, b{};
        Check(PHOrbEvaluateWithOptions(left,options,a) && PHOrbEvaluateWithOptions(right,options,b) &&
            SameEvaluation(a,b) && a.goalsMet && a.hazardsSafe,
            "every possible untouched-spinner color retains exact groups and release safety after cleanup");
    }
    const auto expectUnchanged = [&](PHOrbBoard input, PHOrbSolveOptions settings, Node candidate, Clock::time_point until,
                                      const char *message) {
        const auto prior = candidate;
        Check(!SimplifyIdentityCycles(input,settings,context,cancel,until,candidate) &&
            candidate.length == prior.length && candidate.path == prior.path && SameScore(candidate.score,prior.score),message);
    };
    expectUnchanged(board,options,node,deadline(),"already simplified route is unchanged");
    expectUnchanged(board,options,saved,Clock::now(),"expired budget leaves the original accepted path intact");
    cancel = true;
    expectUnchanged(board,options,saved,deadline(),"cancellation never publishes a shortened route"); cancel = false;
    auto fishOptions = options; fishOptions.fishCell = 21;
    expectUnchanged(board,fishOptions,saved,deadline(),"fishing search options cannot enter ordinary cycle cleanup");
    auto fishNode = saved; fishNode.fished = true;
    expectUnchanged(board,options,fishNode,deadline(),"a route that used a spinner is never simplified");
    auto activity = options; activity.activity.firstWaveOnly = true;
    expectUnchanged(board,activity,saved,deadline(),"activity gesture and step scoring remain outside cleanup");
    activity = options; activity.activity.stepCost = 1;
    expectUnchanged(board,activity,saved,deadline(),"even nondefault inactive activity weights exclude cleanup");
    for (unsigned flag : {unsigned(PHOrbFlagBlindTouch),unsigned(PHOrbFlagThorn),unsigned(PHOrbFlagEffectFresh),
                          unsigned(PHOrbFlagLocked),unsigned(PHOrbFlagCombo),unsigned(PHOrbFlagNail)}) {
        auto changed = board; changed.flags[11] = flag;
        expectUnchanged(changed,options,saved,deadline(),"flagged/touch-sensitive orb paths are left untouched");
    }
    auto changed = board; changed.blindTurns[11] = 1;
    expectUnchanged(changed,options,saved,deadline(),"blind-duration state excludes cleanup");
    changed = board; changed.thornDamage[11] = 1;
    expectUnchanged(changed,options,saved,deadline(),"nonzero thorn metadata excludes cleanup even without its flag");
    changed = board; changed.types[0] = PHOrbMortalPoison;
    expectUnchanged(changed,options,saved,deadline(),"nonbasic-color boards retain their original paths in this narrow implementation");
    auto altered = saved; altered.score.result.combos++;
    expectUnchanged(board,options,altered,deadline(),"re-evaluation mismatch cannot publish a shortened path");
    altered = saved; altered.score.result.goalsMet = false;
    expectUnchanged(board,options,altered,deadline(),"unmet explanatory routes are not simplified");
    altered = saved; altered.score.result.hazardsSafe = false;
    expectUnchanged(board,options,altered,deadline(),"unsafe explanatory routes are not simplified");
    auto fixed = board; fixed.fixedStart = original[0] + 1;
    node = saved;
    Check(SimplifyIdentityCycles(fixed,options,context,cancel,deadline(),node) && node.path[0] + 1 == fixed.fixedStart,
        "cleanup retains a legal fixed starting cell");

    PHOrbBoard plain{}; plain.width = 6; plain.height = 5;
    // Deliberately indistinguishable colors and missing external token IDs:
    // the helper must still use synthetic identity, never colors or pointers.
    PHOrbSolveOptions simple; simple.diagonal = true;
    const auto simpleContext = MakeContext(plain,simple.goals,true,false,true);
    const std::vector<uint8_t> rotation{0,1,7,0};
    Check(CycleNode(plain,simple,simpleContext,rotation,node),"three-edge loop is a legal route fixture");
    Check(!SimplifyIdentityCycles(plain,simple,simpleContext,cancel,deadline(),node) && node.length == rotation.size(),
        "returning to the same cell with equal colors still cannot remove a nonidentity token rotation");
    const std::vector<uint8_t> emptyGesture{0,1,7,0,1,7,0};
    Check(CycleNode(plain,simple,simpleContext,emptyGesture,node),"whole-route identity fixture is legal");
    Check(!SimplifyIdentityCycles(plain,simple,simpleContext,cancel,deadline(),node) && node.length == emptyGesture.size(),
        "cleanup never turns a requested drag into an empty gesture");
    const std::vector<uint8_t> twoCycles{0,1,7,0,1,7,0,1,2,8,1,2,8,1,7};
    Check(CycleNode(plain,simple,simpleContext,twoCycles,node),"two separate cycles form a legal route fixture");
    Check(SimplifyIdentityCycles(plain,simple,simpleContext,cancel,deadline(),node) && node.length == 3 &&
        node.path[0] == 0 && node.path[1] == 1 && node.path[2] == 7 && SameCycleTokensIndependently(twoCycles,node,30),
        "bounded stack pass removes multiple identity cycles while preserving the remaining permutation");
}
