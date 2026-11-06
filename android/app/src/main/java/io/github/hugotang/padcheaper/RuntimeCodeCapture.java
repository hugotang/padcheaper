package io.github.hugotang.padcheaper;

import android.os.ParcelFileDescriptor;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.UUID;
import org.json.JSONObject;

/** Output handling only. The native helper selects a fixed code segment, never arbitrary addresses. */
final class RuntimeCodeCapture implements AutoCloseable {
    static final long CODE_BYTES = 17_295_152L;
    private final File temporary, published;
    private boolean retained;

    RuntimeCodeCapture(File directory, int ownerPid) throws IOException {
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IOException("無法建立程式擷取目錄");
        File[] existing = directory.listFiles();
        if (existing == null || existing.length > 128) throw new IOException("無法核對擷取空間");
        long used = 0;
        for (File file : existing) {
            if (file.getName().startsWith("code-") &&
                (file.getName().endsWith(".bin") || file.getName().endsWith(".tmp"))) {
                long length = file.length();
                if (!file.isFile() || length < 0 || length > CODE_BYTES * 3 - used)
                    throw new IOException("擷取空間已滿，請先匯出並清理舊程式擷取檔");
                used += length;
            }
        }
        // At most three complete images, including this request; no automatic deletion of evidence.
        if (used > CODE_BYTES * 2) throw new IOException("擷取空間已滿，請先匯出並清理舊程式擷取檔");
        String name = "code-" + ownerPid + "-" + UUID.randomUUID();
        published = new File(directory, name + ".bin");
        temporary = new File(directory, name + ".tmp");
        if (published.exists() || !temporary.createNewFile()) throw new IOException("無法建立新的程式擷取檔");
    }

    JSONObject capture() throws Exception {
        JSONObject result;
        try (ParcelFileDescriptor descriptor = ParcelFileDescriptor.open(temporary, ParcelFileDescriptor.MODE_READ_WRITE)) {
            result = new JSONObject(NativeBridge.captureRuntimeCode(descriptor.getFd()));
            if (!"code-captured".equals(result.optString("status")) ||
                result.optLong("capturedBytes", -1) != CODE_BYTES ||
                result.optLong("fileOffset", -1) != 0 || result.optLong("virtualAddress", -1) != 0 ||
                result.optInt("elfMachine", 0) != 183 || !result.optBoolean("stableRead") ||
                !result.optBoolean("mappingsStable"))
                throw new IOException("程式擷取未完成：" + result.optString("status", "unknown"));
            descriptor.getFileDescriptor().sync();
        }
        if (temporary.length() != CODE_BYTES) throw new IOException("程式擷取長度不符");
        return result;
    }

    String sha256() throws IOException {
        try (FileInputStream stream = new FileInputStream(temporary)) { return TargetGate.digest(stream); }
    }

    String publish() throws IOException {
        if (published.exists() || !temporary.renameTo(published)) throw new IOException("無法發布程式擷取檔");
        return published.getName();
    }

    void retain() { retained = true; }

    @Override public void close() throws IOException {
        boolean removed = !temporary.exists() || temporary.delete();
        if (!retained && published.exists()) removed &= published.delete();
        if (!removed) throw new IOException("未完成擷取檔清理失敗；檔案不作為有效證據");
    }
}
