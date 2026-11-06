package io.github.hugotang.padcheaper;

final class NativeBridge {
    // Inventory only: this API has no write, touch, skill, timer or network operation.
    static native String inspectLibrary();
    private NativeBridge() {}
}
