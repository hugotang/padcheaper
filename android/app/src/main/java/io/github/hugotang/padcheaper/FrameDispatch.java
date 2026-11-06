package io.github.hugotang.padcheaper;

import android.opengl.GLSurfaceView;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import javax.microedition.khronos.opengles.GL10;

/** Callers pinned to the hashed APK; never infer or modify a native entry. */
final class FrameDispatch {
    final Method renderer, vulkanLoop;

    private FrameDispatch(Method renderer, Method vulkanLoop) {
        this.renderer = renderer; this.vulkanLoop = vulkanLoop;
    }

    static FrameDispatch resolve(ClassLoader loader) throws ReflectiveOperationException {
        Class<?> renderer = Class.forName(BuildConfig.GAME_RENDERER_CLASS, false, loader);
        Class<?> loop = Class.forName(BuildConfig.GAME_VULKAN_LOOP_CLASS, false, loader);
        if (!GLSurfaceView.Renderer.class.isAssignableFrom(renderer) || !Thread.class.isAssignableFrom(loop))
            throw new NoSuchMethodException("frame caller class differs");
        return new FrameDispatch(javaVoidMethod(renderer, BuildConfig.GAME_RENDERER_FRAME, GL10.class),
            javaVoidMethod(loop, BuildConfig.GAME_VULKAN_LOOP));
    }

    private static Method javaVoidMethod(Class<?> type, String name, Class<?>... parameters)
            throws NoSuchMethodException {
        Method method = type.getDeclaredMethod(name, parameters);
        int flags = method.getModifiers();
        if (method.getReturnType() != void.class || !Modifier.isPublic(flags) || Modifier.isStatic(flags) ||
            Modifier.isNative(flags) || Modifier.isAbstract(flags)) throw new NoSuchMethodException("frame caller signature differs");
        return method;
    }
}
