#include "LibraryInventory.h"
#include "RuntimeCodeCapture.h"
#include <jni.h>
#include <string>

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_hugotang_padcheaper_NativeBridge_inspectLibrary(JNIEnv *env, jclass) {
    try {
        const auto result = padcheaper::InspectGameLibrary();
        const std::string json = "{\"status\":\"" + result.status + "\",\"mappedBytes\":" +
            std::to_string(result.mappedBytes) + ",\"elfMachine\":" + std::to_string(result.machine) +
            ",\"hasExecutableMapping\":" + (result.executableMapping ? "true" : "false") + "}";
        return env->NewStringUTF(json.c_str());
    } catch (...) {
        return env->NewStringUTF("{\"status\":\"native-inspection-failed\"}");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_hugotang_padcheaper_NativeBridge_captureRuntimeCode(JNIEnv *env, jclass, jint outputFd) {
    const auto result = padcheaper::CaptureRuntimeCode(outputFd);
    try {
        const std::string json = "{\"status\":\"" + std::string(result.status) + "\",\"capturedBytes\":" +
            std::to_string(result.capturedBytes) + ",\"fileOffset\":0,\"virtualAddress\":0,\"elfMachine\":183" +
            ",\"stableRead\":" + (result.stableRead ? "true" : "false") +
            ",\"mappingsStable\":" + (result.mappingsStable ? "true" : "false") + "}";
        return env->NewStringUTF(json.c_str());
    } catch (...) {
        return env->NewStringUTF("{\"status\":\"code-result-unavailable\"}");
    }
}
