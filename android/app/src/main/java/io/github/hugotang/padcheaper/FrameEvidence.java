package io.github.hugotang.padcheaper;

import java.lang.ref.WeakReference;

/** Session-local observations. No game addresses, authorization or background work. */
final class FrameEvidence {
    private WeakReference<Object> owner = new WeakReference<>(null);
    private boolean foreground;
    private long generation, count, generationCount, threadID = -1, firstFrameNanos, lastFrameNanos;
    private static final long STALE_NANOS = 2_000_000_000L;
    private boolean threadChanged;

    synchronized void resume(Object next) {
        if (next == null) return;
        if (!foreground || owner.get() != next) {
            ++generation;
            threadID = -1;
            generationCount = 0;
            firstFrameNanos = 0;
            lastFrameNanos = 0;
            threadChanged = false;
        }
        owner = new WeakReference<>(next);
        foreground = true;
    }

    synchronized void pause(Object previous) {
        if (previous != null && owner.get() == previous) foreground = false;
    }

    synchronized long beginFrame(Object source) {
        return foreground && source != null && owner.get() == source ? generation : -1;
    }

    synchronized void afterFrame(long capturedGeneration, long thread, long now) {
        if (!foreground || capturedGeneration != generation) return;
        if (threadID == -1) threadID = thread;
        else if (threadID != thread) threadChanged = true;
        ++count;
        if (++generationCount == 1) firstFrameNanos = now;
        lastFrameNanos = now;
    }

    synchronized Snapshot snapshot() {
        return new Snapshot(foreground, generation, count, generationCount, threadID,
            firstFrameNanos, lastFrameNanos, threadChanged);
    }

    static final class Snapshot {
        final boolean foreground, threadChanged;
        final long generation, count, generationCount, threadID, firstFrameNanos, lastFrameNanos;
        Snapshot(boolean foreground, long generation, long count, long generationCount, long threadID,
                 long firstFrameNanos, long lastFrameNanos, boolean threadChanged) {
            this.foreground = foreground; this.generation = generation; this.count = count;
            this.generationCount = generationCount; this.threadID = threadID; this.firstFrameNanos = firstFrameNanos;
            this.lastFrameNanos = lastFrameNanos; this.threadChanged = threadChanged;
        }

        long ageMillis(long now) {
            return generationCount == 0 || now - lastFrameNanos < 0 ? -1 : (now - lastFrameNanos) / 1_000_000L;
        }

        String status(long now) {
            if (!foreground) return "background";
            if (generationCount == 0) return "no-frames";
            if (threadChanged) return "thread-changed";
            long age = now - lastFrameNanos;
            if (age < 0) return "clock-discontinuity";
            return age > STALE_NANOS ? "stale" : "observed";
        }

        double averageFps() {
            long duration = lastFrameNanos - firstFrameNanos;
            return generationCount < 2 || threadChanged || duration <= 0 ? 0 :
                (generationCount - 1) * 1_000_000_000.0 / duration;
        }
    }
}
