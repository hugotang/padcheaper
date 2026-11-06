package io.github.hugotang.padcheaper;

import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.os.Process;
import android.os.SystemClock;
import android.util.AtomicFile;
import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import org.json.JSONException;
import org.json.JSONObject;

/** One explicit, finite capture. Also saves failed stages so an old success is not left behind. */
final class ProbeCapture {
    private final String moduleApk;
    private final int ownerPid;
    private boolean nativeLoaded;
    private String nativeAbi = "unknown";
    private String nativeSha256 = "unknown";

    ProbeCapture(String moduleApk, int ownerPid) { this.moduleApk = moduleApk; this.ownerPid = ownerPid; }

    String run(Context context, FrameEvidence frames) {
        if (!TargetGate.ownsProcess(ownerPid, Process.myPid())) return "此程序不屬於接入主程序。";
        JSONObject report = new JSONObject();
        String stage = "report", message;
        try {
            report.put("schema", 3).put("moduleVersion", BuildConfig.VERSION_NAME)
                .put("moduleSourceFingerprint", BuildConfig.SOURCE_FINGERPRINT)
                .put("gamePackage", BuildConfig.GAME_PACKAGE)
                .put("pid", ownerPid).put("capturedAtMillis", System.currentTimeMillis())
                .put("capturedElapsedMillis", SystemClock.elapsedRealtime())
                .put("captureStatus", "failed").put("packageIdentityMatched", false)
                .put("libraryArtifactMatched", false).put("frameDispatchPrepared", true)
                .put("runtimeLayoutVerified", false).put("boardReadEnabled", false)
                .put("actionsEnabled", false).put("deviceVerified", false);
            try {
                ProcessIdentity identity = ProcessIdentity.read();
                report.put("processStartTicks", identity.startTicks).put("bootId", identity.bootId)
                    .put("processIdentityAvailable", true);
            } catch (Exception error) {
                // Keep collecting entry diagnostics even if Android restricts a proc file.
                report.put("processIdentityAvailable", false).put("processIdentityError", error.getMessage());
            }
            stage = "package";
            checkpoint(context, report, frames, stage);
            PackageInfo game = context.getPackageManager().getPackageInfo(BuildConfig.GAME_PACKAGE, 0);
            report.put("gameVersion", game.versionName).put("gameVersionCode", game.getLongVersionCode());
            if (!TargetGate.matchesVersion(game.versionName, game.getLongVersionCode()))
                throw new IllegalStateException("遊戲版本不符");
            ApplicationInfo info = game.applicationInfo;
            if (info == null || !TargetGate.matchesApk(info.sourceDir))
                throw new IllegalStateException("遊戲 APK 已改變，請完整重開並核對版本");
            report.put("packageIdentityMatched", true);
            stage = "library-artifact";
            checkpoint(context, report, frames, stage);
            String[] splits = info.splitSourceDirs == null ? new String[0] : info.splitSourceDirs;
            String[] apks = Arrays.copyOf(splits, splits.length + 1); apks[splits.length] = info.sourceDir;
            if (!TargetGate.matchesLibrary(info.nativeLibraryDir, apks))
                throw new IllegalStateException("遊戲原生程式與已核對版本不同");
            report.put("libraryArtifactMatched", true);
            stage = "payload-load";
            checkpoint(context, report, frames, stage);
            if (!nativeLoaded) {
                NativeLoader.Payload payload = NativeLoader.load(context, moduleApk);
                nativeAbi = payload.abi; nativeSha256 = payload.sha256; nativeLoaded = true;
            }
            stage = "library-mapping";
            checkpoint(context, report, frames, stage);
            JSONObject library = new JSONObject(NativeBridge.inspectLibrary());
            report.put("library", library).put("captureStatus", "complete");
            message = "擷取完成。程序：" + nativeAbi + "；遊戲：arm64。\n原生映像：" +
                library.optString("status", "unknown");
        } catch (Throwable error) {
            message = "擷取未完成（" + stage + "）：" + error.getClass().getSimpleName() + " · " + error.getMessage();
            try {
                report.put("captureStatus", "failed").put("failureStage", stage)
                    .put("errorType", error.getClass().getSimpleName()).put("errorMessage", error.getMessage());
            } catch (JSONException ignored) { /* the UI still carries the original failure */ }
            Log.e("PADCheaper", "probe capture failed at " + stage, error);
        }
        try {
            appendState(report, frames);
            persist(context, report);
            message += "\n本次紀錄已保存至遊戲私有目錄 padcheaper。";
        } catch (Throwable error) {
            message += "\n本次紀錄保存失敗：" + error.getClass().getSimpleName() + " · " + error.getMessage();
            Log.e("PADCheaper", "probe report persistence failed; an older file may remain", error);
        }
        Log.i("PADCheaper", report.toString());
        return message;
    }

    private void appendState(JSONObject report, FrameEvidence frames) throws JSONException {
        FrameEvidence.Snapshot state = frames.snapshot();
        long now = System.nanoTime();
        report.put("nativePayloadAbi", nativeAbi).put("nativePayloadLoaded", nativeLoaded)
                .put("nativePayloadSha256", nativeSha256)
                .put("frames", state.count).put("foregroundFrames", state.generationCount)
                .put("frameThread", state.threadID).put("foreground", state.foreground)
                .put("foregroundGeneration", state.generation).put("threadChanged", state.threadChanged)
                .put("lastFrameNanos", state.lastFrameNanos).put("frameAgeMillis", state.ageMillis(now))
                .put("frameStatus", state.status(now)).put("averageForegroundFps", state.averageFps());
    }

    private void checkpoint(Context context, JSONObject report, FrameEvidence frames, String stage) throws Exception {
        // A native abort/exit cannot be caught by Java. Save the attempted stage first,
        // replacing any earlier success even when the process never returns here.
        report.put("captureStatus", "in-progress").put("progressStage", stage);
        appendState(report, frames);
        persist(context, report);
        Log.i("PADCheaper", "probe stage: " + stage);
    }

    private void persist(Context context, JSONObject report) throws Exception {
        File directory = new File(context.getFilesDir(), "padcheaper");
        if (!directory.isDirectory() && !directory.mkdirs()) throw new IllegalStateException("無法建立診斷目錄");
        AtomicFile file = new AtomicFile(new File(directory, "probe-" + ownerPid + ".json"));
        FileOutputStream stream = null;
        try {
            stream = file.startWrite();
            stream.write(report.toString(2).getBytes(StandardCharsets.UTF_8));
            file.finishWrite(stream); stream = null;
        } finally { if (stream != null) file.failWrite(stream); }
    }
}
