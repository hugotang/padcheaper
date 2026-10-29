static uintptr_t OrbCell(unsigned cell, unsigned width = 6, unsigned height = 5) {
    unsigned native = (height - 1 - cell / width) * 16 + cell % width;
    return Load<uintptr_t>(implBytes.data() + PHOrbTableOffset + native * 8);
}
static void SpinnerFixture(unsigned width, unsigned height, std::initializer_list<unsigned> cells, unsigned colors = 0) {
    Put<uint16_t>(Impl + PHOrbSpinnerDurationOffset, 0x404);
    Protected(Impl + PHOrbSpinnerCountOffset, uint32_t(cells.size()), 0x6a875a8b);
    Protected(Impl + PHOrbSpinnerPeriodOffset, 30, 0x6a875a8b);
    Protected(Impl + PHOrbSpinnerColorsOffset, colors, 0x6a875a8b);
    for (unsigned row = 0; row < 6; ++row) {
        unsigned mask = 0;
        for (unsigned cell : cells) if (height - 1 - cell / width == row) mask |= 1u << (cell % width);
        Protected(Impl + PHOrbSpinnerRowsOffset + 16 * row, mask, 0x6a875a8b);
    }
}
static void OrbEffectNativeTests() {
    PHOrbSnapshot state{}, previous{}; NSString *reason = nil;
    id<PHOrbEngine> engine;
    for (auto dimensions : {std::pair<unsigned, unsigned>{6,5}, {7,6}, {5,4}}) {
        const unsigned w = dimensions.first, h = dimensions.second, last = w * h - 1;
        Reset(w, h); engine = PHCreateOrbEngine(); SpinnerFixture(w, h, {0, last});
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
            state.board.spinnerMask == (1 | (UINT64_C(1) << last)) && state.board.spinnerColors == 63 &&
            state.spinnerTurns == 4 && state.spinnerPeriodFrames == 30 && writes == 0,
            "spinner masks convert native bottom-first rows on every board size without mutation");
        previous = state;
        for (unsigned cell : {0u, last}) Put<uint8_t>(OrbCell(cell, w, h), uint8_t((state.board.types[cell] + 1) % 6));
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbSamePlanBoard(&previous.board, &state.board),
            "common spinner phase advances keep native route preflight valid");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&previous frame:Frame() reason:&reason] && writes == 0,
            "native input independently refuses a spinner start");
        Check([engine stageCell:1 down:YES beginning:YES expected:&previous frame:Frame() reason:&reason] &&
            [engine finishInput:&reason], "fresh native preflight accepts a non-spinner start after an entire cycle advance");
        Reset(w, h); engine = PHCreateOrbEngine();
        Put<uint16_t>(Impl + PHOrbTapeRowDurationOffset, 0x402);
        Put<uint16_t>(Impl + PHOrbTapeColumnDurationOffset, 0x403);
        Protected(Impl + PHOrbTapeRowsOffset, 1, 0x6a875a8b);
        Protected(Impl + PHOrbTapeColumnsOffset, 1u << (w - 1), 0x6a875a8b);
        uint64_t expectedMask = 0;
        for (unsigned i = 0; i <= last; ++i) if (i < w || i % w == w - 1) {
            expectedMask |= UINT64_C(1) << i;
            Put<uint32_t>(OrbCell(i, w, h) + 4, PHOrbFlagTape);
        }
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && state.board.tapeMask == expectedMask,
            "tape row bits are top-first and combine with column bits including the last cell");
        Check(![engine stageCell:last down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "native preflight refuses a taped start without writing input");
        Put<uint32_t>(OrbCell(last, w, h) + 4, 0);
        Check([engine read:&state frame:Frame() reason:&reason] && !PHOrbCanPreview(&state),
            "a tape mask that disagrees with native cell flags is never silently trusted");

        Reset(w, h); engine = PHCreateOrbEngine();
        Put<uint16_t>(Impl + PHOrbCloudDurationOffset, 0x405);
        Protected(Impl + PHOrbCloudXOffset, w - 2, 0x6a875a8b);
        Protected(Impl + PHOrbCloudYOffset, 0, 0x6a875a8b);
        Protected(Impl + PHOrbCloudWidthOffset, 3, 0x6a875a8b);
        Protected(Impl + PHOrbCloudHeightOffset, 2, 0x6a875a8b);
        const uint64_t cloud = (UINT64_C(3) << (last - 1)) | (UINT64_C(3) << (last - 1 - w));
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && state.board.cloudMask == cloud,
            "cloud rectangle clips at the right edge and converts bottom-first Y while preserving real colors");
        Check([engine stageCell:last down:YES beginning:YES expected:&state frame:Frame() reason:&reason] &&
            [engine finishInput:&reason], "clouds are visible annotations and do not forbid native input");

        Reset(w, h); engine = PHCreateOrbEngine();
        Protected(Impl + PHOrbFixedStartOffset, w - 1, 0x6a875a8b);
        Protected(Impl + PHOrbFixedStartYOffset, 0, 0x6a875a8b);
        for (unsigned i = 0; i < last; ++i) Put<uint32_t>(OrbCell(i, w, h) + 4, PHOrbFlagStartDim);
        Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && state.board.fixedStart == last + 1,
            "fixed native bottom-right start becomes a one-based preview cell");
        Check(![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "incorrect fixed start is rejected independently by the native input boundary");
        Check([engine stageCell:last down:YES beginning:YES expected:&state frame:Frame() reason:&reason], "correct fixed start can be staged");
        NativeInputFrame(); Put<uintptr_t>(Impl + PHBattleDragPointerOffset, state.board.tokens[last]);
        for (unsigned i = 0; i < last; ++i) Put<uint32_t>(OrbCell(i, w, h) + 4, 0);
        Check([engine finishInput:&reason] && [engine read:&previous frame:Frame() reason:&reason] &&
            PHOrbSamePlanBoard(&state.board, &previous.board), "native accepted start may clear dimming without changing the plan");
    }
    Reset(); engine = PHCreateOrbEngine();
    Put<uint32_t>(OrbCell(0) + 4, PHOrbFlagBlindTouch | PHOrbFlagEffectFresh);
    Put<uint32_t>(OrbCell(1) + 4, PHOrbFlagBlind | PHOrbFlagBlindFresh); Put<uint8_t>(OrbCell(1) + 1, 0x85);
    Put<uint32_t>(OrbCell(2) + 4, PHOrbFlagLocked | PHOrbFlagNail | PHOrbFlagCombo);
    Put<float>(OrbCell(2) + 8, -0.5f);
    Put<uint32_t>(OrbCell(3) + 4, PHOrbFlagThorn); Put<uint8_t>(OrbCell(3) + 12, 0x84);
    Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) &&
        state.board.types[1] == 1 && state.board.blindTurns[1] == 5 && state.board.thornDamage[3] == 4,
        "normal darkness, timed superdarkness, weak orbs, nails and thorns retain actual native metadata");
    Check(![engine stageCell:3 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && writes == 0,
        "thorn damage is prevented by the native input boundary as well as the solver");
    Put<uint32_t>(OrbCell(0) + 4, PHOrbFlagEffectFresh);
    Check([engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] &&
        [engine finishInput:&reason], "ordinary darkness clearing does not invalidate an otherwise identical guarded start");
    Put<float>(OrbCell(2) + 8, -1.01f); [engine read:&state frame:Frame() reason:&reason];
    Check(!PHOrbCanPreview(&state), "out-of-range negative enhancement remains rejected");
    Reset(); engine = PHCreateOrbEngine();
    Put<uint16_t>(Impl + PHOrbUnmatchableDurationOffset, 0x403);
    Protected(Impl + PHOrbUnmatchableColorsOffset, (1u << PHOrbPoison) | (1u << 5), 0x6a875a8b);
    Put<uint8_t>(Header + PHGameMainRVA + 92, 4);
    Put<uint16_t>(Impl + PHOrbMinMatchOffset, 5); Put<uint16_t>(Impl + PHOrbMinMatchExtraOffset, 6);
    Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && state.board.minErase == 4 &&
        state.board.minMatch == 6 && state.board.unmatchableColors == 160,
        "unmatchable colors and both native match thresholds are read without disabling supported boards");
    for (unsigned fault = 0; fault < 6; ++fault) {
        Reset(); engine = PHCreateOrbEngine(); SpinnerFixture(6, 5, {0});
        if (fault == 0) Protected(Impl + PHOrbSpinnerCountOffset, 2, 0x6a875a8b);
        if (fault == 1) Protected(Impl + PHOrbSpinnerRowsOffset + 16 * 5, 1, 0x6a875a8b);
        if (fault == 2) Protected(Impl + PHOrbSpinnerPeriodOffset, 0, 0x6a875a8b);
        if (fault == 3) Protected(Impl + PHOrbSpinnerColorsOffset, 0x401, 0x6a875a8b);
        if (fault == 4) Put<uint32_t>(Impl + PHOrbSpinnerCountOffset, 1234);
        if (fault == 5) Put<uint16_t>(Impl + PHOrbSpinnerDurationOffset, 0x3ff);
        Check([engine read:&state frame:Frame() reason:&reason] && !PHOrbCanPreview(&state) && !writes,
            "bad spinner count, row mask, period, color, protected data or duration never enables input");
    }
    for (unsigned fault = 0; fault < 11; ++fault) {
        Reset(); engine = PHCreateOrbEngine();
        if (fault < 2) {
            Put<uint16_t>(Impl + PHOrbUnmatchableDurationOffset, 0x403);
            Protected(Impl + PHOrbUnmatchableColorsOffset, fault ? 0x400 : 0, 0x6a875a8b);
        } else if (fault == 2) {
            Put<uint16_t>(Impl + PHOrbTapeRowDurationOffset, 0x402);
            Protected(Impl + PHOrbTapeRowsOffset, 1u << 5, 0x6a875a8b);
        } else if (fault == 3) {
            Put<uint16_t>(Impl + PHOrbTapeColumnDurationOffset, 0x402);
            Protected(Impl + PHOrbTapeColumnsOffset, 1u << 6, 0x6a875a8b);
        } else if (fault == 4 || fault == 5) {
            Put<uint16_t>(Impl + PHOrbCloudDurationOffset, 0x405);
            Protected(Impl + PHOrbCloudXOffset, 0, 0x6a875a8b);
            Protected(Impl + PHOrbCloudYOffset, fault == 4 ? 5 : 0, 0x6a875a8b);
            Protected(Impl + PHOrbCloudWidthOffset, fault == 5 ? 0 : 2, 0x6a875a8b);
            Protected(Impl + PHOrbCloudHeightOffset, 2, 0x6a875a8b);
        } else if (fault == 6) {
            Protected(Impl + PHOrbFixedStartOffset, 254, 0x6a875a8b);
        } else if (fault == 7) {
            Protected(Impl + PHOrbFixedStartOffset, 0, 0x6a875a8b);
            Protected(Impl + PHOrbFixedStartYOffset, 5, 0x6a875a8b);
        } else if (fault == 8) {
            Put<uint16_t>(Impl + PHOrbMinMatchOffset, 31);
        } else if (fault == 9) {
            Put<uint8_t>(Header + PHGameMainRVA + 92, 8);
        } else {
            Put<uint8_t>(OrbCell(0) + 1, 100); Put<uint32_t>(OrbCell(0) + 4, PHOrbFlagBlind);
        }
        Check([engine read:&state frame:Frame() reason:&reason] && !PHOrbCanPreview(&state) &&
            ![engine stageCell:0 down:YES beginning:YES expected:&state frame:Frame() reason:&reason] && !writes,
            "invalid unmatchable, tape, cloud, fixed-start, match-limit or darkness data rejects both preview and native input");
    }
    Reset(); engine = PHCreateOrbEngine(); SpinnerFixture(6, 5, {0}, (1u << 0) | (1u << PHOrbPoison));
    Check([engine read:&state frame:Frame() reason:&reason] && PHOrbCanPreview(&state) && PHOrbSpinnerVariants(&state.board) == 2,
        "custom poison-color spinner with stable zero metadata reaches hazard-aware planning");
    Put<uint8_t>(Header + PHOrbTapeProjectRVA, uint8_t(guest[PHOrbTapeProjectRVA] ^ 1));
    engine = PHCreateOrbEngine();
    Check(![engine read:&state frame:Frame() reason:&reason] && !writes, "new board-effect instruction guards are enforced");
}
