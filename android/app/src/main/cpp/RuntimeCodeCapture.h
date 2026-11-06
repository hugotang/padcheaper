#pragma once
#include "LibraryInventory.h"
#include <cstddef>
#include <cstdint>

namespace padcheaper {
// PAD Android 23.6.0 / 230600 only. This is the first file-backed RX PT_LOAD,
// including ELF metadata; it is not a full ELF file or a verified unpacked image.
constexpr uint64_t RuntimeCodeBytes = 17295152;
constexpr size_t RuntimeCodeHeaderBytes = 512;

struct RuntimeCodePlan { uint64_t base = 0, size = 0; };

// Pure, fixed-target validation. Output is cleared on rejection. No game reads.
const char *PlanRuntimeCode(const std::vector<Mapping> &mappings, RuntimeCodePlan &output);
bool MatchesRuntimeCodeHeader(const unsigned char *bytes, size_t size);

struct RuntimeCodeResult {
    const char *status = "code-capture-failed";
    uint64_t capturedBytes = 0;
    bool stableRead = false, mappingsStable = false;
};

// The caller owns an empty regular O_RDWR temporary file. This function duplicates
// its descriptor, never closes the caller's descriptor, and clears accepted output
// on any failure. It performs two bounded read-only memory passes, no native calls.
RuntimeCodeResult CaptureRuntimeCode(int outputFd) noexcept;
}
