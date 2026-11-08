#pragma once
#include "PHOrbModel.h"
#include <cstddef>

// One phase for preview, one-shot and Auto. Never keep a second string phase:
// the stable names below are only the public UI/diagnostic representation.
enum class PHOrbSessionPhase {
    Idle, Reading, Solving, Preview, Arming, Running, Releasing, Complete,
    Stopped, Blocked, Fault, AutoWaiting, TurnWaiting, OpeningArming,
    OpeningWaiting, SkillArming, SkillWaiting, EightSkillArming, EightSkillWaiting
};

static inline const char *PHOrbSessionPhaseName(PHOrbSessionPhase phase) {
    switch (phase) {
        case PHOrbSessionPhase::Idle: return "idle";
        case PHOrbSessionPhase::Reading: return "reading";
        case PHOrbSessionPhase::Solving: return "solving";
        case PHOrbSessionPhase::Preview: return "preview";
        case PHOrbSessionPhase::Arming: return "arming";
        case PHOrbSessionPhase::Running: return "running";
        case PHOrbSessionPhase::Releasing: return "releasing";
        case PHOrbSessionPhase::Complete: return "complete";
        case PHOrbSessionPhase::Stopped: return "stopped";
        case PHOrbSessionPhase::Blocked: return "blocked";
        case PHOrbSessionPhase::Fault: return "fault";
        case PHOrbSessionPhase::AutoWaiting: return "autoWaiting";
        case PHOrbSessionPhase::TurnWaiting: return "turnWaiting";
        case PHOrbSessionPhase::OpeningArming: return "openingArming";
        case PHOrbSessionPhase::OpeningWaiting: return "openingWaiting";
        case PHOrbSessionPhase::SkillArming: return "skillArming";
        case PHOrbSessionPhase::SkillWaiting: return "skillWaiting";
        case PHOrbSessionPhase::EightSkillArming: return "eightSkillArming";
        case PHOrbSessionPhase::EightSkillWaiting: return "eightSkillWaiting";
    }
    return "fault";
}

static inline bool PHOrbSessionExecuting(PHOrbSessionPhase phase) {
    return phase == PHOrbSessionPhase::Arming || phase == PHOrbSessionPhase::Running ||
        phase == PHOrbSessionPhase::Releasing;
}

// A cancelled release is cleanup, not a fresh intent. Repeated lifecycle
// notifications must preserve its original stop reason while it finishes.
static inline bool PHOrbSessionPendingIntent(PHOrbSessionPhase phase, bool readQueued, bool releaseComplete) {
    return readQueued || phase == PHOrbSessionPhase::Solving || phase == PHOrbSessionPhase::Preview ||
        phase == PHOrbSessionPhase::Arming || phase == PHOrbSessionPhase::Running ||
        (phase == PHOrbSessionPhase::Releasing && releaseComplete);
}

// Session bookkeeping only; this never arms an engine or grants native input.
// Stop revokes the run mode but retains facts used by diagnostics and 8P resume.
struct PHOrbAutoRunState {
    PHOrbSnapshot origin{};
    size_t turn = 0, moves = 0, skills = 0;
    double startedAt = 0, phaseAt = 0;
    bool bound = false, sawResolve = false;

    bool active() const { return mode != Mode::Stopped; }
    bool eight() const { return mode == Mode::Eight; }

    void resetProgress() { turn = 1; moves = skills = 0; sawResolve = false; }
    void beginSolo(double now) {
        *this = {};
        resetProgress(); mode = Mode::Solo; startedAt = phaseAt = now;
    }
    // The caller resets progress only for a new match, before native arming.
    // A same-match resume preserves counts and observed resolution evidence.
    void beginEight(const PHOrbSnapshot &state, double now) {
        mode = Mode::Eight; bind(state); startedAt = phaseAt = now;
    }
    void bind(const PHOrbSnapshot &state) { origin = state; bound = true; }
    void stop() { mode = Mode::Stopped; }

private:
    enum class Mode { Stopped, Solo, Eight };
    Mode mode = Mode::Stopped;
};

// This latch outlives an Auto run and configuration reloads. Both a countdown
// attempt and a manual start consume the match, including failed arming.
// Keeping it outside PHOrbAutoRunState prevents Stop from rearming a match.
//
// Edge-triggered (2026-11-08): only a match whose native countdown was seen
// while the setting was on may start by itself. The countdown has no match
// clock yet and no readable board (device log: known 8P seat, countdownReady
// 0, clockStart 0, board-not-ready), so arming uses every observation, read or
// not. A running match first seen without that countdown (setting enabled or
// app attached mid-match), or one whose first board was already moved, is
// consumed without starting.
class PHOrbEightAutoStartState {
public:
    void observe(const PHOrbSnapshot &state, bool enabled) {
        const PHOrbMultiplayerState &m = state.multiplayer;
        if (!enabled || (state.present && m.kind != PHOrbMultiplayerEight)) { armed = false; return; }
        if (m.known && m.kind == PHOrbMultiplayerEight && m.teamCount == 8 && m.localSeat < 8 &&
            m.countdownReady != 1 && !m.finished) {
            armed = true; armedSeat = m.localSeat;
        }
    }
    bool claimCountdown(const PHOrbSnapshot &state) {
        if (!PHOrbEightMatchReady(&state) || PHOrbSameEightMatch(&consumedMatch, &state)) return false;
        if (!armed || armedSeat != state.multiplayer.localSeat || state.moves) { consume(state); return false; }
        if (!PHOrbCanPreview(&state)) return false; // Keep the arm until the first board settles.
        consume(state); return true;
    }
    void consume(const PHOrbSnapshot &state) { consumedMatch = state; armed = false; }
    bool isArmed() const { return armed; }

private:
    PHOrbSnapshot consumedMatch{};
    bool armed = false;
    uint32_t armedSeat = 0;
};
