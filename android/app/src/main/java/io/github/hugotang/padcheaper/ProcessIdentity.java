package io.github.hugotang.padcheaper;

import java.io.ByteArrayOutputStream;
import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Read only boot/process identity; PID alone can refer to a previous game launch. */
final class ProcessIdentity {
    final String startTicks, bootId;

    private ProcessIdentity(String startTicks, String bootId) { this.startTicks = startTicks; this.bootId = bootId; }

    static ProcessIdentity read() throws IOException {
        String boot = readSmallFile("/proc/sys/kernel/random/boot_id").trim().toLowerCase(Locale.ROOT);
        if (!boot.matches("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))
            throw new IOException("invalid boot identity");
        return new ProcessIdentity(startTicks(readSmallFile("/proc/self/stat")), boot);
    }

    static String startTicks(String stat) throws IOException {
        if (stat == null) throw new IOException("process identity unavailable");
        int end = stat.lastIndexOf(')'); // comm may contain spaces and ')' characters.
        if (end < 0 || end + 2 >= stat.length() || stat.charAt(end + 1) != ' ')
            throw new IOException("invalid process stat");
        String[] fields = stat.substring(end + 2).trim().split("\\s+");
        // Tail starts at field 3 (state); starttime is Linux proc field 22.
        if (fields.length < 20 || !fields[19].matches("[0-9]+")) throw new IOException("missing process starttime");
        try {
            long ticks = Long.parseLong(fields[19]);
            if (ticks <= 0) throw new NumberFormatException();
            return Long.toString(ticks);
        } catch (NumberFormatException error) { throw new IOException("invalid process starttime", error); }
    }

    private static String readSmallFile(String path) throws IOException {
        try (FileInputStream input = new FileInputStream(path); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[1024];
            for (int count; (count = input.read(buffer)) != -1;) {
                if (output.size() + count > 8192) throw new IOException("process identity too large");
                output.write(buffer, 0, count);
            }
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}
