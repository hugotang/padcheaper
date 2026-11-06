package io.github.hugotang.padcheaper;

import android.app.Activity;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;
import io.github.libxposed.api.XposedModule;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.List;

/** LSPosed is the Java entry; the native payload contains no framework dependency. */
public final class PadModule extends XposedModule {
    private static final String TAG = "PADCheaper";
    private final FrameEvidence frames = new FrameEvidence();
    private final List<HookHandle> handles = new ArrayList<>();
    private boolean selected, installed;
    private int ownerPid;
    private volatile ProbeController controller;

    @Override public void onModuleLoaded(ModuleLoadedParam param) {
        ownerPid = Process.myPid();
        selected = TargetGate.matchesProcess(param.getProcessName(), param.isSystemServer());
        if (selected) log(Log.INFO, TAG, "probe entry; framework=" + getFrameworkName() + " " +
            getFrameworkVersion() + "; API=" + getApiVersion());
    }

    private boolean ownProcess() { return TargetGate.ownsProcess(ownerPid, Process.myPid()); }

    private void observe(Runnable callback) {
        if (!ownProcess()) return;
        try { callback.run(); }
        catch (Throwable error) { log(Log.ERROR, TAG, "probe observer failed", error); }
    }

    @Override public void onPackageReady(PackageReadyParam param) {
        if (!selected || !ownProcess() || installed || !param.isFirstPackage() ||
            !BuildConfig.GAME_PACKAGE.equals(param.getPackageName())) return;
        try {
            if (!TargetGate.matchesApk(param.getApplicationInfo().sourceDir)) {
                log(Log.WARN, TAG, "APK identity differs; no hooks installed");
                return;
            }
            Class<?> activityClass = Class.forName(BuildConfig.GAME_CLASS, false, param.getClassLoader());
            if (!Activity.class.isAssignableFrom(activityClass)) throw new IllegalStateException("target is not an Activity");
            Method frame = activityClass.getDeclaredMethod(BuildConfig.GAME_FRAME);
            Method create = activityClass.getDeclaredMethod("onCreate", Bundle.class);
            Method resume = activityClass.getDeclaredMethod("onResume");
            Method pause = activityClass.getDeclaredMethod("onPause");
            Method destroy = activityClass.getDeclaredMethod("onDestroy");
            FrameDispatch dispatch = FrameDispatch.resolve(param.getClassLoader());
            // Hook the Java wrapper, keeping the game JNI and native bridge in charge.
            if (frame.getReturnType() != void.class || Modifier.isStatic(frame.getModifiers()) ||
                Modifier.isNative(frame.getModifiers())) throw new IllegalStateException("frame signature differs");
            handles.add(hook(create).setExceptionMode(ExceptionMode.PROTECTIVE).intercept(chain -> {
                Object result = chain.proceed();
                observe(() -> {
                    Activity activity = (Activity) chain.getThisObject();
                    if (controller == null) controller = new ProbeController(frames, getModuleApplicationInfo().sourceDir, ownerPid);
                    controller.attach(activity);
                });
                return result;
            }));
            handles.add(hook(resume).setExceptionMode(ExceptionMode.PROTECTIVE).intercept(chain -> {
                Object result = chain.proceed();
                observe(() -> {
                    Activity activity = (Activity) chain.getThisObject();
                    frames.resume(activity);
                    ProbeController current = controller;
                    if (current != null) current.resume(activity);
                });
                return result;
            }));
            handles.add(hook(pause).setExceptionMode(ExceptionMode.PROTECTIVE).intercept(chain -> {
                observe(() -> {
                    Activity activity = (Activity) chain.getThisObject();
                    frames.pause(activity);
                    ProbeController current = controller;
                    if (current != null) current.pause(activity);
                });
                return chain.proceed();
            }));
            handles.add(hook(frame).setExceptionMode(ExceptionMode.PROTECTIVE).intercept(chain -> {
                long generation = ownProcess() ? frames.beginFrame(chain.getThisObject()) : -1;
                Object result = chain.proceed(); // Exactly once, including any original exception.
                if (ownProcess()) frames.afterFrame(generation, Thread.currentThread().getId(), System.nanoTime());
                return result;
            }));
            handles.add(hook(destroy).setExceptionMode(ExceptionMode.PROTECTIVE).intercept(chain -> {
                observe(() -> {
                    Activity activity = (Activity) chain.getThisObject();
                    frames.pause(activity);
                    ProbeController current = controller;
                    if (current != null) current.detach(activity);
                });
                return chain.proceed();
            }));
            // A tiny wrapper can have been inlined into either rendering path by ART.
            // Only these APK-verified Java callers are affected; no global deoptimization.
            if (!deoptimize(dispatch.renderer) || !deoptimize(dispatch.vulkanLoop))
                throw new IllegalStateException("verified frame caller deoptimization failed");
            installed = true;
            log(Log.INFO, TAG, "Java frame/lifecycle observers installed; native layout and actions remain disabled");
        } catch (Throwable error) {
            for (HookHandle handle : handles) {
                try { handle.unhook(); } catch (Throwable ignored) { /* retain original failure */ }
            }
            handles.clear();
            log(Log.ERROR, TAG, "entry installation failed", error);
        }
    }

    @Override public boolean onHotReloading(HotReloadingParam param) {
        // Native library unloading and process/tab ownership are not yet verified.
        return false;
    }
}
