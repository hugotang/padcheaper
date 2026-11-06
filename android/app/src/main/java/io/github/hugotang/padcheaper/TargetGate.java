package io.github.hugotang.padcheaper;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** Artifact identity only. Matching an APK never validates a native memory layout. */
final class TargetGate {
    static final long MAX_FILE_BYTES = 64L * 1024 * 1024;

    static boolean matchesProcess(String name, boolean systemServer) {
        return !systemServer && BuildConfig.GAME_PACKAGE.equals(name);
    }

    static boolean ownsProcess(int ownerPid, int currentPid) { return ownerPid > 0 && ownerPid == currentPid; }

    static boolean matchesVersion(String name, long code) {
        return BuildConfig.GAME_VERSION.equals(name) && code == BuildConfig.GAME_VERSION_CODE;
    }

    static String digest(InputStream stream) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[65536];
            long total = 0;
            for (int count; (count = stream.read(buffer)) != -1;) {
                total += count;
                if (total > MAX_FILE_BYTES) throw new IOException("artifact exceeds bounded size");
                digest.update(buffer, 0, count);
            }
            StringBuilder result = new StringBuilder(64);
            for (byte value : digest.digest()) result.append(String.format(Locale.ROOT, "%02x", value & 255));
            return result.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IOException("SHA-256 unavailable", impossible);
        }
    }

    static boolean matchesApk(String path) throws IOException {
        if (path == null) return false;
        try (InputStream stream = new FileInputStream(path)) {
            return BuildConfig.GAME_APK_SHA256.equals(digest(stream));
        }
    }

    static boolean matchesLibrary(String nativeDirectory, String[] apks) throws IOException {
        // The captured game is arm64 even when ART itself runs as x86_64.
        if (nativeDirectory != null) {
            File extracted = new File(nativeDirectory, "libpad.so");
            if (extracted.isFile()) {
                try (InputStream stream = new FileInputStream(extracted)) {
                    return BuildConfig.GAME_LIBRARY_SHA256.equals(digest(stream));
                }
            }
        }
        if (apks != null) for (String path : apks) {
            try (ZipFile zip = new ZipFile(path)) {
                ZipEntry entry = zip.getEntry("lib/arm64-v8a/libpad.so");
                if (entry == null) continue;
                if (entry.getSize() <= 0 || entry.getSize() > MAX_FILE_BYTES) return false;
                try (InputStream stream = zip.getInputStream(entry)) {
                    return BuildConfig.GAME_LIBRARY_SHA256.equals(digest(stream));
                }
            }
        }
        return false;
    }

    private TargetGate() {}
}
