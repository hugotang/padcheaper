static void DiagonalNativeTests() {
    for (auto dimensions : {std::pair<unsigned,unsigned>{6,5}, {7,6}, {5,4}}) {
        Reset(dimensions.first, dimensions.second);
        id<PHOrbEngine> engine = PHCreateOrbEngine(); PHOrbSnapshot state{}; NSString *reason = nil;
        Check([engine read:&state frame:Frame() reason:&reason], "diagonal native board is readable");
        const unsigned target = dimensions.first + 1;
        const auto real = realInput;
        Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason],
            "diagonal gesture begins at a guarded cell center");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset,state.board.tokens[0]);
        Check([engine finishInput:&reason] && [engine read:&state frame:Frame() reason:&reason],
            "real input bank is restored before diagonal movement");
        Check([engine stageCell:target down:YES beginning:NO expected:&state frame:Frame() reason:&reason] &&
            Load<float>(reinterpret_cast<uint8_t *>(GlobalInput()) + 4) == state.board.x[target] &&
            Load<float>(reinterpret_cast<uint8_t *>(GlobalInput()) + 8) == state.board.y[target],
            "diagonal movement stages the exact neighboring native center in one held frame");
        NativeInputFrame(); Check([engine finishInput:&reason], "diagonal held frame restores the input lease");
        const unsigned before = writes;
        Check(![engine stageCell:target + 1 down:YES beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "native boundary rejects a non-neighbor even with free movement enabled");
        Put<uint8_t>(state.identity.game + 93,0);
        Check(![engine stageCell:target down:YES beginning:NO expected:&state frame:Frame() reason:&reason] && writes == before,
            "changed native free-movement mode cannot execute an old diagonal preview");
        Put<uint8_t>(state.identity.game + 93,1);
        Check([engine stageCell:0 down:NO beginning:NO expected:&state frame:Frame() reason:&reason] &&
            [engine finishInput:&reason] && realInput == real,
            "rejected diagonal target can release the held token while the real input bytes stay intact");
    }
    // This fixture checks staging/leases only; actual native diagonal token swaps need device verification.
}
