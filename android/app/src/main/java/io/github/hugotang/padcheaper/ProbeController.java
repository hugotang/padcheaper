package io.github.hugotang.padcheaper;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Color;
import android.os.Process;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.FrameLayout;
import java.lang.ref.WeakReference;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/** An Activity-owned diagnostics button; no overlay permission or root daemon. */
final class ProbeController {
    private final FrameEvidence frames;
    private final ProbeCapture capture;
    private final int ownerPid;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final AtomicBoolean checking = new AtomicBoolean();
    private WeakReference<Activity> activity = new WeakReference<>(null);
    private Button button;
    private AlertDialog dialog;
    private volatile String lastMessage = "尚未擷取。按「擷取接入狀態」檢查。";

    ProbeController(FrameEvidence frames, String moduleApk, int ownerPid) {
        this.frames = frames; this.capture = new ProbeCapture(moduleApk, ownerPid); this.ownerPid = ownerPid;
    }

    private boolean ownProcess() { return TargetGate.ownsProcess(ownerPid, Process.myPid()); }

    void attach(Activity next) {
        if (!ownProcess()) return;
        if (activity.get() == next && button != null) return;
        removeButton();
        activity = new WeakReference<>(next);
        button = new Button(next);
        button.setText("PC 診斷"); button.setTextColor(Color.WHITE);
        button.setTextSize(12); button.setAlpha(0.85f);
        button.setOnClickListener(view -> show(next));
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,
            ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.END);
        params.topMargin = Math.round(48 * next.getResources().getDisplayMetrics().density);
        next.addContentView(button, params);
        button.setVisibility(frames.snapshot().foreground ? View.VISIBLE : View.GONE);
    }

    void resume(Activity next) { attach(next); if (button != null) button.setVisibility(View.VISIBLE); }
    void pause(Activity previous) {
        if (activity.get() != previous) return;
        dismissDialog();
        if (button != null) button.setVisibility(View.GONE);
    }
    void detach(Activity previous) {
        if (activity.get() != previous) return;
        removeButton(); activity.clear();
    }
    private void removeButton() {
        dismissDialog();
        if (button != null && button.getParent() instanceof ViewGroup) ((ViewGroup) button.getParent()).removeView(button);
        button = null;
    }

    private void dismissDialog() {
        if (dialog != null) { dialog.dismiss(); dialog = null; }
    }

    private void show(Activity current) {
        if (!ownProcess() || activity.get() != current || current.isFinishing() || current.isDestroyed() ||
            !frames.snapshot().foreground) return;
        dismissDialog();
        FrameEvidence.Snapshot state = frames.snapshot();
        String message = "版本 " + BuildConfig.VERSION_NAME + "\n影格狀態：" + frameStatusText(state.status(System.nanoTime())) +
            "\n來源識別：" + BuildConfig.SOURCE_FINGERPRINT.substring(0, 12) +
            "\n累計／本次前景影格：" + state.count + "／" + state.generationCount +
            "\n本次前景平均：" + String.format(Locale.ROOT, "%.1f fps", state.averageFps()) +
            "\n繪製執行緒：" + state.threadID +
            "\n本次前景曾切換繪製執行緒：" + state.threadChanged + "\n\n" + lastMessage +
            "\n\n目前只提供接入診斷，讀盤、轉珠、技能與換隊尚未開放。";
        dialog = new AlertDialog.Builder(current).setTitle("PADCheaper 接入診斷").setMessage(message)
            .setPositiveButton(checking.get() ? "擷取中" : "擷取接入狀態", (dialog, which) -> capture(current, false))
            .setNeutralButton("擷取程式資料", (dialog, which) -> confirmCodeCapture(current))
            .setNegativeButton("關閉", null).create();
        dialog.show();
    }

    private void confirmCodeCapture(Activity current) {
        if (!ownProcess() || activity.get() != current || current.isFinishing() || current.isDestroyed() ||
            !frames.snapshot().foreground || checking.get()) return;
        dismissDialog();
        dialog = new AlertDialog.Builder(current).setTitle("擷取盤面接入研究資料")
            .setMessage("擷取目前版本固定的程式區段，約 16.5 MiB，用於定位盤面讀取。\n\n" +
                "請保持遊戲在前景；資料會保存在遊戲私有目錄，之後由電腦端工具匯出。" +
                "這個動作只收集程式資料，尚不能讀盤或轉珠。")
            .setPositiveButton("開始擷取", (dialog, which) -> capture(current, true))
            .setNegativeButton("取消", null).create();
        dialog.show();
    }

    private static String frameStatusText(String status) {
        switch (status) {
            case "background": return "背景中";
            case "no-frames": return "尚未觀察到影格";
            case "stale": return "影格已超過兩秒未更新";
            case "thread-changed": return "繪製執行緒曾改變，待核對";
            case "clock-discontinuity": return "計時資料不連續";
            case "observed": return "近期有收到影格（操作窗口尚未驗證）";
            default: return "影格狀態未知";
        }
    }

    private void capture(Activity current, boolean runtimeCode) {
        if (!ownProcess() || activity.get() != current || !frames.snapshot().foreground ||
            !checking.compareAndSet(false, true)) return;
        Context context = current.getApplicationContext();
        FrameEvidence.Snapshot requestedState = frames.snapshot();
        WeakReference<Activity> requestedBy = new WeakReference<>(current);
        // Explicit, finite user request. Hashing and disk I/O never run on a game frame.
        Thread worker = new Thread(() -> {
            try {
                if (!ownProcess()) return;
                lastMessage = capture.run(context, frames, runtimeCode, requestedState.generation, requestedState.threadID);
            } catch (Throwable error) {
                lastMessage = "擷取未完成：" + error.getClass().getSimpleName() + " · " + error.getMessage();
                Log.e("PADCheaper", "read-only probe failed", error);
            } finally {
                checking.set(false);
                main.post(() -> {
                    Activity requested = requestedBy.get();
                    if (requested != null) show(requested);
                });
            }
        }, "PADCheaper-inspect");
        try { worker.setDaemon(true); worker.start(); }
        catch (Throwable error) {
            checking.set(false);
            lastMessage = "無法啟動擷取：" + error.getClass().getSimpleName();
            Log.e("PADCheaper", "cannot start probe worker", error);
        }
    }
}
