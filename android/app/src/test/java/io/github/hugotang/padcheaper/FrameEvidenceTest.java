package io.github.hugotang.padcheaper;

import org.junit.Test;
import static org.junit.Assert.*;

public class FrameEvidenceTest {
    private final Object activity = new Object();

    @Test public void backgroundFramesCannotBecomeForegroundEvidence() {
        FrameEvidence value = new FrameEvidence();
        value.afterFrame(value.beginFrame(activity), 7, 100);
        assertEquals(0, value.snapshot().count);
        value.resume(activity); value.afterFrame(value.beginFrame(activity), 7, 200);
        value.pause(activity); value.afterFrame(value.beginFrame(activity), 8, 300);
        FrameEvidence.Snapshot paused = value.snapshot();
        assertFalse(paused.foreground); assertEquals(1, paused.count); assertEquals(7, paused.threadID);
        assertEquals(200, paused.lastFrameNanos); assertFalse(paused.threadChanged);
        value.resume(activity);
        assertEquals(2, value.snapshot().generation); assertEquals(-1, value.snapshot().threadID);
        assertEquals(0, value.snapshot().lastFrameNanos);
    }

    @Test public void duplicateResumeDoesNotEraseThreadChange() {
        FrameEvidence value = new FrameEvidence();
        value.resume(activity); value.afterFrame(value.beginFrame(activity), 7, 100);
        value.afterFrame(value.beginFrame(activity), 8, 200); value.resume(activity);
        assertEquals(1, value.snapshot().generation); assertEquals(2, value.snapshot().count);
        assertEquals(7, value.snapshot().threadID); assertTrue(value.snapshot().threadChanged);
        value.pause(activity); value.pause(activity); value.resume(activity); value.afterFrame(value.beginFrame(activity), 9, 300);
        assertEquals(2, value.snapshot().generation); assertEquals(9, value.snapshot().threadID);
        assertFalse(value.snapshot().threadChanged);
    }

    @Test public void frameFromPreviousForegroundCannotPublishIntoNewGeneration() {
        FrameEvidence value = new FrameEvidence(); value.resume(activity);
        long old = value.beginFrame(activity);
        value.pause(activity); value.resume(activity); value.afterFrame(old, 7, 100);
        assertEquals(0, value.snapshot().count); assertEquals(-1, value.snapshot().threadID);
        value.afterFrame(value.beginFrame(activity), 8, 200);
        assertEquals(1, value.snapshot().count); assertEquals(8, value.snapshot().threadID);
    }

    @Test public void oldActivityCannotPauseOrPublishIntoReplacement() {
        FrameEvidence value = new FrameEvidence(); value.resume(activity);
        long old = value.beginFrame(activity);
        Object replacement = new Object(); value.resume(replacement);
        value.pause(activity); // A late onDestroy from the previous instance.
        assertTrue(value.snapshot().foreground);
        assertEquals(-1, value.beginFrame(activity));
        value.afterFrame(old, 7, 100);
        assertEquals(0, value.snapshot().count);
        value.afterFrame(value.beginFrame(replacement), 8, 200);
        assertEquals(1, value.snapshot().count); assertEquals(8, value.snapshot().threadID);
        assertEquals(2, value.snapshot().generation);
    }

    @Test public void staleFramesAndFreshResumeCannotReportAnActiveStream() {
        FrameEvidence value = new FrameEvidence();
        assertEquals("background", value.snapshot().status(0));
        value.resume(activity);
        assertEquals("no-frames", value.snapshot().status(0));
        assertEquals(-1, value.snapshot().ageMillis(0));
        value.afterFrame(value.beginFrame(activity), 7, 10_000_000L);
        value.afterFrame(value.beginFrame(activity), 7, 30_000_000L);
        FrameEvidence.Snapshot live = value.snapshot();
        assertEquals("observed", live.status(40_000_000L));
        assertEquals(10, live.ageMillis(40_000_000L));
        assertEquals(50.0, live.averageFps(), 0.001);
        assertEquals("stale", live.status(2_030_000_001L));
        assertEquals("clock-discontinuity", live.status(20_000_000L));
        value.pause(activity); value.resume(activity);
        FrameEvidence.Snapshot resumed = value.snapshot();
        assertEquals(2, resumed.count); assertEquals(0, resumed.generationCount);
        assertEquals("no-frames", resumed.status(3_000_000_000L));
        assertEquals(0.0, resumed.averageFps(), 0.001);
    }

    @Test public void threadChangesDoNotProduceMisleadingFrequency() {
        FrameEvidence value = new FrameEvidence(); value.resume(activity);
        long generation = value.beginFrame(activity);
        value.afterFrame(generation, 7, 100);
        value.afterFrame(generation, 8, 200);
        assertEquals("thread-changed", value.snapshot().status(200));
        assertEquals(0.0, value.snapshot().averageFps(), 0.001);
        value.pause(activity);
        assertEquals("background", value.snapshot().status(200));
    }
}
