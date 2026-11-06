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
        return run(context, frames, false, -1, -1);
    }

    String run(Context context, FrameEvidence frames, boolean runtimeCode,
               long requestedGeneration, long requestedThread) {
        if (!TargetGate.ownsProcess(ownerPid, Process.myPid())) return "此程序不屬於接入主程序。";
        JSONObject report = new JSONObject();
        String stage = "report", message;
        RuntimeCodeCapture output = null;
        long started = SystemClock.elapsedRealtime();
        try {
            report.put("schema", runtimeCode ? 1 : 3).put("moduleVersion", BuildConfig.VERSION_NAME)
                .put("moduleSourceFingerprint", BuildConfig.SOURCE_FINGERPRINT)
                .put("gamePackage", BuildConfig.GAME_PACKAGE)
                .put("pid", ownerPid).put("capturedAtMillis", System.currentTimeMillis())
                .put("capturedElapsedMillis", SystemClock.elapsedRealtime())
                .put("captureStatus", "failed").put("packageIdentityMatched", false)
                .put("libraryArtifactMatched", false).put("frameDispatchPrepared", true)
                .put("runtimeLayoutVerified", false).put("boardReadEnabled", false)
                .put("actionsEnabled", false).put("deviceVerified", false);
            if (runtimeCode) {
                report.put("captureKind", "runtime-code").put("captureStartGeneration", requestedGeneration)
                    .put("captureStartThread", requestedThread);
                requireCodeWindow(frames, requestedGeneration, requestedThread, started);
            }
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
            report.put("library", library);
            if (runtimeCode) {
                if (!report.optBoolean("processIdentityAvailable") ||
                    !"arm64-image-observed".equals(library.optString("status")))
                    throw new IllegalStateException("程序或映像身分尚未確認");
                stage = "runtime-code";
                checkpoint(context, report, frames, stage);
                requireCodeWindow(frames, requestedGeneration, requestedThread, started);
                output = new RuntimeCodeCapture(new File(context.getFilesDir(), "padcheaper"), ownerPid);
                report.put("runtimeCode", output.capture());
                requireCodeWindow(frames, requestedGeneration, requestedThread, started);
                ProcessIdentity after = ProcessIdentity.read();
                if (!after.startTicks.equals(report.getString("processStartTicks")) ||
                    !after.bootId.equals(report.getString("bootId")))
                    throw new IllegalStateException("擷取期間程序身分改變");
                report.put("codeSha256", output.sha256());
                requireCodeWindow(frames, requestedGeneration, requestedThread, started);
                report.put("codeFile", output.publish());
                report.put("captureFinishedElapsedMillis", SystemClock.elapsedRealtime());
                message = "程式研究資料擷取完成，約 16.5 MiB。\n請用電腦端工具匯出核對；此結果不代表盤面已能讀取。";
            } else {
                message = "擷取完成。程序：" + nativeAbi + "；遊戲：arm64。\n原生映像：" +
                    library.optString("status", "unknown");
            }
            report.put("captureStatus", "complete");
        } catch (Throwable error) {
            message = "擷取未完成（" + stage + "）：" + error.getClass().getSimpleName() + " · " + error.getMessage();
            try {
                report.put("captureStatus", "failed").put("failureStage", stage)
                    .put("errorType", error.getClass().getSimpleName()).put("errorMessage", error.getMessage());
            } catch (JSONException ignored) { /* the UI still carries the original failure */ }
            Log.e("PADCheaper", "probe capture failed at " + stage, error);
        }
        try {
            FrameEvidence.Snapshot terminal = frames.snapshot();
            long terminalNanos = System.nanoTime();
            if (runtimeCode && "complete".equals(report.optString("captureStatus"))) {
                try {
                    requireCodeWindow(terminal, terminalNanos, requestedGeneration, requestedThread, started);
                    report.put("captureFinishedElapsedMillis", SystemClock.elapsedRealtime());
                }
                catch (Exception error) {
                    report.put("captureStatus", "failed").put("failureStage", "publish-window")
                        .put("errorMessage", error.getMessage());
                    message = "擷取未完成：發布前遊戲前景或影格已改變。";
                }
            }
            if (!"complete".equals(report.optString("captureStatus"))) {
                report.remove("codeFile"); report.remove("codeSha256");
            }
            appendState(report, terminal, terminalNanos);
            persist(context, report);
            if (output != null && "complete".equals(report.optString("captureStatus"))) output.retain();
            message += "\n本次紀錄已保存至遊戲私有目錄 padcheaper。";
        } catch (Throwable error) {
            message += "\n本次紀錄保存失敗：" + error.getClass().getSimpleName() + " · " + error.getMessage();
            try {
                report.put("captureStatus", "failed").put("failureStage", "report-persistence")
                    .put("errorMessage", error.getMessage());
                report.remove("codeFile"); report.remove("codeSha256");
            } catch (JSONException ignored) { /* preserve the original persistence error */ }
            Log.e("PADCheaper", "probe report persistence failed; an older file may remain", error);
        }
        if (output != null) {
            try { output.close(); }
            catch (Exception error) { message += "\n" + error.getMessage(); Log.e("PADCheaper", "code capture cleanup failed", error); }
        }
        Log.i("PADCheaper", report.toString());
        return message;
    }

    private void requireCodeWindow(FrameEvidence frames, long generation, long thread, long started) {
        requireCodeWindow(frames.snapshot(), System.nanoTime(), generation, thread, started);
    }

    private void requireCodeWindow(FrameEvidence.Snapshot state, long now, long generation, long thread, long started) {
        long elapsed = SystemClock.elapsedRealtime() - started;
        if (!TargetGate.ownsProcess(ownerPid, Process.myPid()) || elapsed < 0 || elapsed > 30_000 ||
            !state.acceptsCapture(generation, thread, now))
            throw new IllegalStateException("擷取期間前景／影格改變或已逾時，請返回遊戲再試");
    }

    private void appendState(JSONObject report, FrameEvidence frames) throws JSONException {
        appendState(report, frames.snapshot(), System.nanoTime());
    }

    private void appendState(JSONObject report, FrameEvidence.Snapshot state, long now) throws JSONException {
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
        String prefix = "runtime-code".equals(report.optString("captureKind")) ? "code-" : "probe-";
        AtomicFile file = new AtomicFile(new File(directory, prefix + ownerPid + ".json"));
        FileOutputStream stream = null;
        byte[] encoded = report.toString(2).getBytes(StandardCharsets.UTF_8);
        try {
            stream = file.startWrite();
            stream.write(encoded);
            stream.getFD().sync();
            file.finishWrite(stream); stream = null;
            // AtomicFile can log rename/close failures without throwing. Confirm
            // the exact published checkpoint before retaining a binary image.
            if (!Arrays.equals(encoded, file.readFully())) throw new IllegalStateException("診斷紀錄回讀不符");
        } finally { if (stream != null) file.failWrite(stream); }
    }
}
