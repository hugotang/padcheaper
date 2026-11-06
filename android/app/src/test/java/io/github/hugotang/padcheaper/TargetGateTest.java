package io.github.hugotang.padcheaper;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import org.junit.Test;
import static org.junit.Assert.*;

public class TargetGateTest {
    @Test public void secondaryProcessesAndDifferentBuildsAreExcluded() {
        assertTrue(TargetGate.matchesProcess("jp.gungho.pad", false));
        assertFalse(TargetGate.matchesProcess("jp.gungho.pad:tab1", false));
        assertFalse(TargetGate.matchesProcess("jp.gungho.pad", true));
        assertFalse(TargetGate.matchesProcess(null, false));
        assertFalse(TargetGate.matchesProcess("jp.gungho.pad.extra", false));
        assertTrue(TargetGate.ownsProcess(2448, 2448));
        assertFalse(TargetGate.ownsProcess(2448, 2577)); // fork inherited the entry object
        assertFalse(TargetGate.ownsProcess(0, 0));
        assertTrue(TargetGate.matchesVersion("23.6.0", 230600));
        assertFalse(TargetGate.matchesVersion("23.6.0", 230601));
        assertFalse(TargetGate.matchesVersion("23.6.1", 230600));
    }

    @Test public void digestMatchesKnownSha256() throws IOException {
        assertEquals("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            TargetGate.digest(new ByteArrayInputStream("abc".getBytes(StandardCharsets.UTF_8))));
    }

    @Test public void processElfControlsPayloadAbi() throws IOException {
        byte[] header = new byte[20];
        header[0] = 0x7f; header[1] = 'E'; header[2] = 'L'; header[3] = 'F'; header[4] = 2; header[5] = 1;
        header[18] = 62; assertEquals("x86_64", NativeLoader.abiFromElf(header));
        header[18] = (byte) 183; assertEquals("arm64-v8a", NativeLoader.abiFromElf(header));
        header[4] = 1;
        try { NativeLoader.abiFromElf(header); fail("32-bit process must be rejected"); }
        catch (IOException expected) { /* no cross-architecture call fallback */ }
    }

    @Test public void invalidElfCannotSelectAPayload() {
        for (byte[] invalid : new byte[][] {null, new byte[0], new byte[19], new byte[20]}) {
            try { NativeLoader.abiFromElf(invalid); fail("invalid header accepted"); }
            catch (IOException expected) { /* short and corrupt headers are explicit failures */ }
        }
        byte[] header = new byte[20];
        header[0] = 0x7f; header[1] = 'E'; header[2] = 'L'; header[3] = 'F'; header[4] = 2; header[5] = 1;
        try { NativeLoader.abiFromElf(header); fail("unknown CPU accepted"); }
        catch (IOException expected) { /* no inferred ABI */ }
        header[18] = 62; header[5] = 2;
        try { NativeLoader.abiFromElf(header); fail("big-endian CPU accepted"); }
        catch (IOException expected) { /* only the supported little-endian CPUs */ }
    }
}
