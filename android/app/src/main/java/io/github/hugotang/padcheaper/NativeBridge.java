package io.github.hugotang.padcheaper;

final class NativeBridge {
    // Inventory only: this API has no write, touch, skill, timer or network operation.
    static native String inspectLibrary();
    // Writes only the fixed read-only code image to this caller-owned temporary fd.
    static native String captureRuntimeCode(int outputFd);
    private NativeBridge() {}
}
