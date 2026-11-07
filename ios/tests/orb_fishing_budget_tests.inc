// Private scheduling contract: fixed shares, no wall-clock expectations and
// no requirement that a particular machine rediscovers a route in a deadline.
static void FishingBudgetTests() {
    PHOrbAssistFishingInventory inventory{};
    inventory.count = 12;
    for (unsigned p = 0; p < inventory.count; ++p) {
        inventory.plans[p].lCounts[3] = 1;
        inventory.plans[p].exchangeCount = 2;
    }
    auto weights = FishingSearchWeights(inventory);
    unsigned total = 0;
    for (unsigned p = 0; p < inventory.count; ++p) total += weights[p];
    Check(weights[0] == 11 && total == 22,
        "twelve donor alternatives for one L allocation reserve half the stage for their first representative");
    for (unsigned p = 1; p < inventory.count; ++p)
        Check(weights[p] == 1,"every other baseline retains a positive equal share instead of being discarded");
    auto remaining = std::chrono::milliseconds(220);
    for (unsigned p = 0; p < inventory.count; ++p) {
        const auto share = remaining * weights[p] / total;
        Check(share.count() == (p ? 10 : 110),
            "consumed fixed shares leave exactly half for the eleven remaining baseline plans");
        remaining -= share; total -= weights[p];
    }
    Check(remaining.count() == 0 && total == 0,"weighted baseline scheduling preserves the original total stage budget");

    inventory.plans[11].lCounts[3] = 0; inventory.plans[11].lCounts[0] = 1;
    weights = FishingSearchWeights(inventory);
    Check(std::all_of(weights.begin(),weights.end(),[](unsigned weight) { return weight == 1; }),
        "a different retained L allocation preserves the original equal time distribution");
    inventory.plans[11] = inventory.plans[0];
    inventory.geometrySelectedPlans = 4;
    for (unsigned p = 8; p < inventory.count; ++p) {
        inventory.plans[p].geometryExtraExchanges = 1;
        inventory.plans[p].lCounts[0] = 1; // Geometry allocation cannot alter the baseline stage.
    }
    weights = FishingSearchWeights(inventory);
    unsigned baselineWeight = 0, geometryWeight = 0;
    for (unsigned p = 0; p < 8; ++p) baselineWeight += weights[p];
    for (unsigned p = 8; p < inventory.count; ++p) geometryWeight += weights[p];
    Check(weights[0] == 7 && baselineWeight == 14 && geometryWeight == 4,
        "baseline weighting excludes geometry plans and leaves their separate reserved stage equally divided");
    for (unsigned count : {0u,1u,2u,3u}) {
        inventory.count = count; inventory.geometrySelectedPlans = 0;
        weights = FishingSearchWeights(inventory);
        for (unsigned p = 0; p < weights.size(); ++p)
            Check(weights[p] == unsigned(p < count),"fewer than four baseline plans retain original equal shares");
    }
}
