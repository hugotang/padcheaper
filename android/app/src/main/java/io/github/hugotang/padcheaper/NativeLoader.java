package io.github.hugotang.padcheaper;

import android.content.Context;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

final class NativeLoader {
    static String abiFromElf(byte[] header) throws IOException {
        if (header == null || header.length < 20 || header[0] != 0x7f || header[1] != 'E' || header[2] != 'L' ||
            header[3] != 'F' || header[4] != 2 || header[5] != 1) throw new IOException("unsupported process ELF");
        int machine = (header[18] & 255) | ((header[19] & 255) << 8);
        if (machine == 62) return "x86_64";
        if (machine == 183) return "arm64-v8a";
        throw new IOException("unsupported process CPU");
    }

    static String processAbi() throws IOException {
        byte[] header = new byte[20];
        try (InputStream stream = new FileInputStream("/proc/self/exe")) {
            int offset = 0;
            while (offset < header.length) {
                int read = stream.read(header, offset, header.length - offset);
                if (read <= 0) throw new IOException("process ELF unavailable");
                offset += read;
            }
        }
        return abiFromElf(header);
    }

    static final class Payload {
        final String abi, sha256;
        Payload(String abi, String sha256) { this.abi = abi; this.sha256 = sha256; }
    }

    static synchronized Payload load(Context context, String moduleApk) throws IOException {
        String abi = processAbi();
        String entryName = "lib/" + abi + "/libpadcheaper_runtime.so";
        File directory = new File(context.getCodeCacheDir(), "padcheaper/" + BuildConfig.VERSION_NAME + "/" + abi);
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("cannot create native cache");
        try (ZipFile zip = new ZipFile(moduleApk)) {
            ZipEntry entry = zip.getEntry(entryName);
            if (entry == null || entry.getSize() <= 0 || entry.getSize() > TargetGate.MAX_FILE_BYTES)
                throw new IOException("native payload missing for " + abi);
            String expected;
            try (InputStream stream = zip.getInputStream(entry)) { expected = TargetGate.digest(stream); }
            // Content-addressed: never overwrite a library that a prior load may have mapped.
            File library = new File(directory, "libpadcheaper_" + expected + ".so");
            if (!library.isFile()) {
                File temporary = File.createTempFile("payload-", ".tmp", directory);
                try {
                    try (InputStream input = zip.getInputStream(entry);
                         FileOutputStream output = new FileOutputStream(temporary)) {
                        // No new writable descriptor is opened after marking the payload read-only.
                        if (!temporary.setReadOnly()) throw new IOException("cannot protect native payload");
                        byte[] buffer = new byte[65536]; long total = 0;
                        for (int read; (read = input.read(buffer)) != -1;) {
                            total += read;
                            if (total > TargetGate.MAX_FILE_BYTES) throw new IOException("payload too large");
                            output.write(buffer, 0, read);
                        }
                        output.getFD().sync();
                    }
                    if (!temporary.renameTo(library)) throw new IOException("cannot publish native payload");
                } finally { if (temporary.exists()) temporary.delete(); }
            }
            try (InputStream stream = new FileInputStream(library)) {
                if (!expected.equals(TargetGate.digest(stream))) throw new IOException("native payload hash differs");
            }
            byte[] header = new byte[20];
            try (InputStream stream = new FileInputStream(library)) {
                int offset = 0;
                while (offset < header.length) {
                    int read = stream.read(header, offset, header.length - offset);
                    if (read <= 0) throw new IOException("native payload ELF truncated");
                    offset += read;
                }
            }
            if (!abi.equals(abiFromElf(header))) throw new IOException("native payload CPU differs from process");
            System.load(library.getAbsolutePath());
            return new Payload(abi, expected);
        }
    }
    private NativeLoader() {}
}
