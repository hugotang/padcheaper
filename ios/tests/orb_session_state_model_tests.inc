#include "PHOrbSessionState.h"
#include <utility>

static void SessionStateModelTests() {
    PHOrbAutoRunState run;
    Check(!run.active() && !run.eight() && !run.bound, "new session has no Auto intent or bound battle");
    auto match = MultiplayerModelState(8, 2);
    run.beginSolo(10);
    Check(run.active() && !run.eight() && !run.bound && run.turn == 1 && !run.moves && !run.skills &&
        run.startedAt == 10 && run.phaseAt == 10, "solo start creates a fresh run awaiting battle binding");
    run.bind(match); run.turn = 4; run.moves = 3; run.skills = 2; run.sawResolve = true;
    run.stop(); run.stop();
    Check(!run.active() && !run.eight() && run.bound && run.turn == 4 && run.moves == 3 && run.skills == 2 &&
        run.sawResolve && PHOrbSameEightMatch(&run.origin, &match),
        "repeated Stop revokes activity without erasing progress or diagnostic origin");
    run.beginEight(match, 20);
    Check(run.active() && run.eight() && run.bound && run.turn == 4 && run.moves == 3 && run.skills == 2 &&
        run.sawResolve && run.startedAt == 20 && run.phaseAt == 20,
        "same-match resume preserves progress while starting a fresh wait deadline");
    run.stop(); ++match.multiplayer.clockStart; run.resetProgress(); run.beginEight(match, 30);
    Check(run.active() && run.eight() && run.turn == 1 && !run.moves && !run.skills && !run.sawResolve,
        "new-match initialization clears progress before arming");
    run.stop(); run.beginSolo(40);
    Check(run.active() && !run.eight() && !run.bound && !run.origin.valid && run.turn == 1 &&
        !run.sawResolve && run.startedAt == 40 && run.phaseAt == 40,
        "switching to solo cannot inherit an eight-player binding or wait clock");

    // Countdown auto-start is edge-triggered: only a countdown observed while
    // the setting is on arms the next ready board of that seat.
    const auto ready = MultiplayerModelState(8, 2);
    auto waiting = ready; waiting.multiplayer.countdownReady = 0; waiting.multiplayer.clockStart = 0;
    waiting.multiplayer.clockKnown = false; waiting.valid = waiting.settled = false; // Board unread during countdown.
    for (unsigned blocked = 0; blocked < 8; ++blocked) {
        PHOrbEightAutoStartState latch; latch.observe(waiting, true);
        auto state = ready;
        if (blocked == 0) state.multiplayer.countdownReady = 0;
        if (blocked == 1) state.multiplayer.viewTeam = 1;
        if (blocked == 2) state.multiplayer.finished = 1;
        if (blocked == 3) state.multiplayer.clockKnown = false;
        if (blocked == 4) state.valid = false;
        if (blocked == 5) state = MultiplayerModelState(3, 2);
        if (blocked == 6) state = MultiplayerModelState(4, 2);
        if (blocked == 7) state.multiplayer.remainingSeconds = 0;
        Check(latch.isArmed() && !latch.claimCountdown(state) && latch.claimCountdown(ready),
            "ineligible observations neither start, consume nor disarm a countdown-armed match");
    }
    {
        PHOrbEightAutoStartState latch;
        Check(!latch.claimCountdown(ready), "a running match first seen without its countdown never starts");
        latch.observe(waiting, true);
        Check(!latch.claimCountdown(ready), "enabling the setting mid-match cannot start that match later");
        auto next = ready; ++next.multiplayer.clockStart; auto unsettled = next; unsettled.settled = false;
        latch.observe(waiting, true);
        Check(!latch.claimCountdown(unsettled) && latch.isArmed() && latch.claimCountdown(next),
            "an unsettled first board keeps the arm until it can be previewed");
        latch.observe(waiting, true); ++next.multiplayer.clockStart; auto moved = next; moved.moves = 1;
        Check(!latch.claimCountdown(moved) && !latch.claimCountdown(next),
            "a first board already moved by hand consumes the match without starting");
        latch.observe(waiting, false); ++next.multiplayer.clockStart;
        Check(!latch.isArmed() && !latch.claimCountdown(next), "a countdown seen with the setting off does not arm");
        latch.observe(waiting, true); latch.observe(waiting, false);
        Check(!latch.isArmed(), "turning the setting off during the countdown disarms it");
        auto solo = ready; solo.multiplayer = {}; solo.identity.multiplayer = 0;
        latch.observe(waiting, true); latch.observe(solo, true);
        Check(!latch.isArmed(), "a present non-8P battle disarms an old countdown");
        auto other = waiting; other.multiplayer.localSeat = other.multiplayer.viewTeam = 3;
        latch.observe(other, true); ++next.multiplayer.clockStart;
        Check(!latch.claimCountdown(next), "a countdown observed for another seat does not arm this seat");
    }
    PHOrbEightAutoStartState countdown;
    countdown.observe(waiting, true);
    Check(countdown.claimCountdown(ready), "first confirmed local countdown is claimable");
    auto later = ready; ++later.wave; ++later.multiplayer.turnSerial;
    countdown.observe(waiting, true);
    Check(!countdown.claimCountdown(ready) && !countdown.claimCountdown(later),
        "Stop, another turn and another floor cannot rearm the same match");
    run.stop(); run.beginSolo(50); run.stop();
    Check(!countdown.claimCountdown(ready), "run resets do not clear the independently owned countdown latch");
    ++later.multiplayer.clockStart;
    countdown.observe(waiting, true);
    Check(countdown.claimCountdown(later), "a distinct next match with an observed countdown permits one new attempt");
    ++later.multiplayer.clockStart;
    Check(!countdown.claimCountdown(later), "a next match without an observed countdown does not start");
    PHOrbEightAutoStartState manual;
    manual.observe(waiting, true); manual.consume(ready);
    Check(!manual.isArmed() && !manual.claimCountdown(ready), "manual start also consumes the match before native arming can fail");

    using Phase = PHOrbSessionPhase;
    for (Phase phase : {Phase::Idle, Phase::Stopped, Phase::Fault, Phase::Blocked, Phase::Complete})
        Check(!PHOrbSessionExecuting(phase) && !PHOrbSessionPendingIntent(phase, false, false),
            "inactive lifecycle callbacks preserve an earlier terminal result");
    for (Phase phase : {Phase::Solving, Phase::Preview, Phase::Arming, Phase::Running})
        Check(PHOrbSessionPendingIntent(phase, false, false), "lifecycle interruption cancels pending work");
    Check(PHOrbSessionExecuting(Phase::Releasing) && !PHOrbSessionPendingIntent(Phase::Releasing, false, false) &&
        PHOrbSessionPendingIntent(Phase::Releasing, false, true) &&
        PHOrbSessionPendingIntent(Phase::Idle, true, false),
        "release cleanup remains executable without inventing a new cancelled intent");

    // These spellings are the existing snapshot/UI contract, independent of
    // the internal enum. Changing them would break saved diagnostic consumers.
    const std::pair<Phase, const char *> names[] = {
        {Phase::Idle,"idle"}, {Phase::Reading,"reading"}, {Phase::Solving,"solving"}, {Phase::Preview,"preview"},
        {Phase::Arming,"arming"}, {Phase::Running,"running"}, {Phase::Releasing,"releasing"}, {Phase::Complete,"complete"},
        {Phase::Stopped,"stopped"}, {Phase::Blocked,"blocked"}, {Phase::Fault,"fault"}, {Phase::AutoWaiting,"autoWaiting"},
        {Phase::TurnWaiting,"turnWaiting"}, {Phase::OpeningArming,"openingArming"}, {Phase::OpeningWaiting,"openingWaiting"},
        {Phase::SkillArming,"skillArming"}, {Phase::SkillWaiting,"skillWaiting"},
        {Phase::EightSkillArming,"eightSkillArming"}, {Phase::EightSkillWaiting,"eightSkillWaiting"}
    };
    for (const auto &entry : names)
        Check(!std::strcmp(PHOrbSessionPhaseName(entry.first), entry.second), "phase export keeps its original stable string");
}
