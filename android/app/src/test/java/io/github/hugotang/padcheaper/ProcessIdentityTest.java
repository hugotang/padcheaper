package io.github.hugotang.padcheaper;

import java.io.IOException;
import org.junit.Test;
import static org.junit.Assert.*;

public class ProcessIdentityTest {
    private static String stat(String comm, String ticks) {
        // state (field 3), fields 4..21, then field 22. Values are deliberately distinct.
        StringBuilder value = new StringBuilder("2448 (" + comm + ") S");
        for (int field = 4; field <= 21; ++field) value.append(' ').append(field);
        return value.append(' ').append(ticks).append(" 23000 24\n").toString();
    }

    @Test public void commSpacesAndParenthesesDoNotShiftStartTime() throws IOException {
        assertEquals("1234567", ProcessIdentity.startTicks(stat("gungho.pad:tab1", "1234567")));
        assertEquals("7654321", ProcessIdentity.startTicks(stat("worker (a) ) render", "7654321")));
    }

    @Test public void missingInvalidOrOverflowStartTimeIsRejected() {
        for (String input : new String[] {null, "", "2448 (pad) S 1 2", stat("pad", "-1"),
                stat("pad", "0"), stat("pad", "text"), stat("pad", "9223372036854775808")}) {
            try { ProcessIdentity.startTicks(input); fail("invalid starttime accepted"); }
            catch (IOException expected) { /* cannot attribute this report to a current process */ }
        }
    }
}
