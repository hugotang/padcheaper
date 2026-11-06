#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace padcheaper {
// Fixed PAD Android 23.6.0 / versionCode 230600 static evidence only.
// These are guest addresses and owned bytes, never host pointers or live readers.
struct Pad236OwnedGuestSegment {
    uint64_t guestAddress = 0;
    std::vector<uint8_t> bytes;
};

struct Pad236OfflineBoardInput {
    // Supplied explicitly by the caller. The uncaptured singleton slot is not read.
    uint64_t gameAddress = 0;
    std::vector<Pad236OwnedGuestSegment> segments;
};

constexpr size_t Pad236MaxSnapshotSegments = 4096;
constexpr size_t Pad236MaxSnapshotBytes = 16 * 1024 * 1024;
constexpr size_t Pad236BlockPrefixBytes = 0x24;

enum class Pad236BoardDecodeStatus { InvalidSnapshot, EvidencePartial };
enum class Pad236BoardDiagnosticCode {
    EmptySnapshot,
    TooManySegments,
    EmptySegment,
    SegmentAddressOverflow,
    SnapshotTooLarge,
    OverlappingSegments,
    NullGameAddress,
    AddressOverflow,
    MissingRange,
    UnsupportedDimensions,
    NullImplementationAddress,
    NullBlockAddress,
    BlockOutsidePool,
    BlockMisaligned,
    DuplicateBlockAddress,
    RuntimeContextUnverified
};
enum class Pad236BoardField {
    Snapshot,
    GameAddress,
    Dimensions,
    ImplementationPointer,
    Pool,
    PointerTable,
    BlockPointer,
    BlockPrefix
};

struct Pad236BoardDiagnostic {
    Pad236BoardDiagnosticCode code = Pad236BoardDiagnosticCode::EmptySnapshot;
    Pad236BoardField field = Pad236BoardField::Snapshot;
    // Source range involved in the failure; pointer validation identifies its slot.
    uint64_t guestAddress = 0;
    uint64_t requiredBytes = 0;
    // -1 means that this diagnostic does not identify a segment or cell.
    int segmentIndex = -1;
    int nativeRow = -1;
    int nativeColumn = -1;
    // A rejected decoded block pointer, retaining all 64 bits separately from its slot.
    std::optional<uint64_t> rawPointerValue;
};

struct Pad236RawBlock {
    uint64_t guestAddress = 0;
    uint16_t poolIndex = 0;
    uint8_t nativeRow = 0, nativeColumn = 0;
    std::array<uint8_t, Pad236BlockPrefixBytes> rawPrefix{};
    // Preserve all byte/state values. There is no ordinary-color or settled gate.
    uint8_t rawColorByte = 0;
    uint16_t rawState = 0;
    // These extra fields are deliberately named by offset, not inferred semantics.
    uint32_t rawWord04 = 0;
    int16_t rawSignedHalfword1c = 0, rawSignedHalfword1e = 0;
};

struct Pad236RawBoard {
    uint64_t gameAddress = 0, implementationAddress = 0;
    uint64_t poolAddress = 0, pointerTableAddress = 0;
    uint8_t width = 0, height = 0;
    // Native y-major order using table[y * 16 + x]; no screen-row conversion.
    std::vector<Pad236RawBlock> cells;
};

struct Pad236BoardDecodeResult {
    Pad236BoardDecodeStatus status = Pad236BoardDecodeStatus::InvalidSnapshot;
    std::vector<Pad236BoardDiagnostic> diagnostics;
    // Always absent after any structural error, including a failure in the last cell.
    std::optional<Pad236RawBoard> board;
};

// Pure offline decoding. No process memory, callbacks, device access, native calls,
// tag stripping, solver conversion or memory writes. Input must not be concurrently
// modified. Adjacent owned segments may cover one field; gaps and overlaps reject.
// Success is always EvidencePartial: supplied bytes do not establish their target
// provenance, capture coherence, phase, settled state or a local operation window.
Pad236BoardDecodeResult DecodePad236OfflineRawBoard(const Pad236OfflineBoardInput &input);
}
