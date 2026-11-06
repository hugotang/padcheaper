#include "BoardLayout.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace padcheaper {
namespace {
constexpr uint64_t GameDimensionsOffset = 0x70;
constexpr uint64_t GameImplementationOffset = 0x88;
constexpr uint64_t PoolOffset = 0x98958;
constexpr uint64_t BlockStride = 0xac;
constexpr size_t PoolCount = 256;
constexpr uint64_t TableOffset = 0xa3558;
constexpr uint64_t TableRowStride = 16;
constexpr uint64_t GuestPointerBytes = 8;
static_assert(PoolOffset + PoolCount * BlockStride == TableOffset,
              "The statically observed block pool ends at the pointer table");
static_assert(sizeof(size_t) <= sizeof(uint64_t), "Guest range checks cover host sizes");

bool Add(uint64_t base, uint64_t size, uint64_t &end) {
    if (size > std::numeric_limits<uint64_t>::max() - base) return false;
    end = base + size;
    return true;
}

Pad236BoardDiagnostic Diagnostic(Pad236BoardDiagnosticCode code, Pad236BoardField field,
                                 uint64_t address = 0, uint64_t size = 0,
                                 int segment = -1, int row = -1, int column = -1) {
    return {code, field, address, size, segment, row, column, {}};
}

Pad236BoardDecodeResult Reject(const Pad236BoardDiagnostic &diagnostic) {
    Pad236BoardDecodeResult result;
    result.diagnostics.push_back(diagnostic);
    return result;
}

struct OwnedRange {
    uint64_t start = 0, end = 0;
    const Pad236OwnedGuestSegment *segment = nullptr;
    int originalIndex = -1;
};

enum class ReadStatus { Complete, Overflow, MissingRange };

class OwnedReader {
public:
    bool Initialize(const std::vector<Pad236OwnedGuestSegment> &segments,
                    Pad236BoardDiagnostic &diagnostic) {
        if (segments.empty()) {
            diagnostic = Diagnostic(Pad236BoardDiagnosticCode::EmptySnapshot,
                                    Pad236BoardField::Snapshot);
            return false;
        }
        if (segments.size() > Pad236MaxSnapshotSegments) {
            diagnostic = Diagnostic(Pad236BoardDiagnosticCode::TooManySegments,
                                    Pad236BoardField::Snapshot);
            return false;
        }
        size_t totalBytes = 0;
        ranges_.reserve(segments.size());
        for (size_t i = 0; i < segments.size(); ++i) {
            const auto &segment = segments[i];
            const auto fail = [&](Pad236BoardDiagnosticCode code) {
                diagnostic = Diagnostic(code, Pad236BoardField::Snapshot,
                                        segment.guestAddress, segment.bytes.size(), int(i));
                return false;
            };
            if (segment.bytes.empty()) return fail(Pad236BoardDiagnosticCode::EmptySegment);
            uint64_t end = 0;
            if (!Add(segment.guestAddress, uint64_t(segment.bytes.size()), end))
                return fail(Pad236BoardDiagnosticCode::SegmentAddressOverflow);
            if (segment.bytes.size() > Pad236MaxSnapshotBytes - totalBytes)
                return fail(Pad236BoardDiagnosticCode::SnapshotTooLarge);
            totalBytes += segment.bytes.size();
            ranges_.push_back({segment.guestAddress, end, &segment, int(i)});
        }
        std::sort(ranges_.begin(), ranges_.end(), [](const OwnedRange &a, const OwnedRange &b) {
            return a.start < b.start;
        });
        for (size_t i = 1; i < ranges_.size(); ++i) {
            if (ranges_[i].start < ranges_[i - 1].end) {
                diagnostic = Diagnostic(Pad236BoardDiagnosticCode::OverlappingSegments,
                                        Pad236BoardField::Snapshot, ranges_[i].start,
                                        ranges_[i].end - ranges_[i].start,
                                        ranges_[i].originalIndex);
                return false;
            }
        }
        return true;
    }

    ReadStatus Read(uint64_t address, size_t size, uint8_t *output) const {
        uint64_t end = 0;
        if (!Add(address, uint64_t(size), end)) return ReadStatus::Overflow;
        auto range = std::lower_bound(ranges_.begin(), ranges_.end(), address,
                                     [](const OwnedRange &value, uint64_t target) {
                                         return value.end <= target;
                                     });
        size_t copied = 0;
        while (address < end) {
            if (range == ranges_.end() || range->start > address)
                return ReadStatus::MissingRange;
            const size_t amount = size_t(std::min(end, range->end) - address);
            const size_t offset = size_t(address - range->start);
            std::memcpy(output + copied, range->segment->bytes.data() + offset, amount);
            address += amount;
            copied += amount;
            ++range;
        }
        return ReadStatus::Complete;
    }

private:
    std::vector<OwnedRange> ranges_;
};

uint64_t LittleEndian(const uint8_t *bytes, size_t size) {
    uint64_t value = 0;
    for (size_t i = 0; i < size; ++i) value |= uint64_t(bytes[i]) << (i * 8);
    return value;
}

int16_t SignedHalfword(const uint8_t *bytes) {
    const uint16_t value = uint16_t(LittleEndian(bytes, 2));
    const int32_t signedValue = value < 0x8000 ? int32_t(value) : int32_t(value) - 0x10000;
    return int16_t(signedValue);
}

Pad236BoardDiagnostic ReadDiagnostic(ReadStatus status, Pad236BoardField field,
                                    uint64_t address, uint64_t size,
                                    int row = -1, int column = -1) {
    return Diagnostic(status == ReadStatus::Overflow ? Pad236BoardDiagnosticCode::AddressOverflow
                                                     : Pad236BoardDiagnosticCode::MissingRange,
                      field, address, size, -1, row, column);
}
}

Pad236BoardDecodeResult DecodePad236OfflineRawBoard(const Pad236OfflineBoardInput &input) {
    OwnedReader reader;
    Pad236BoardDiagnostic diagnostic;
    if (!reader.Initialize(input.segments, diagnostic)) return Reject(diagnostic);
    if (!input.gameAddress)
        return Reject(Diagnostic(Pad236BoardDiagnosticCode::NullGameAddress,
                                 Pad236BoardField::GameAddress));

    uint64_t dimensionsAddress = 0, implementationPointerAddress = 0;
    if (!Add(input.gameAddress, GameDimensionsOffset, dimensionsAddress))
        return Reject(Diagnostic(Pad236BoardDiagnosticCode::AddressOverflow,
                                 Pad236BoardField::Dimensions, input.gameAddress,
                                 GameDimensionsOffset));
    uint8_t dimensions[2]{};
    auto read = reader.Read(dimensionsAddress, sizeof(dimensions), dimensions);
    if (read != ReadStatus::Complete)
        return Reject(ReadDiagnostic(read, Pad236BoardField::Dimensions,
                                     dimensionsAddress, sizeof(dimensions)));
    const uint8_t width = dimensions[0], height = dimensions[1];
    if (!((width == 5 && height == 4) || (width == 6 && height == 5) ||
          (width == 7 && height == 6)))
        return Reject(Diagnostic(Pad236BoardDiagnosticCode::UnsupportedDimensions,
                                 Pad236BoardField::Dimensions, dimensionsAddress, 2));
    if (!Add(input.gameAddress, GameImplementationOffset, implementationPointerAddress))
        return Reject(Diagnostic(Pad236BoardDiagnosticCode::AddressOverflow,
                                 Pad236BoardField::ImplementationPointer, input.gameAddress,
                                 GameImplementationOffset));
    std::array<uint8_t, GuestPointerBytes> pointer{};
    read = reader.Read(implementationPointerAddress, pointer.size(), pointer.data());
    if (read != ReadStatus::Complete)
        return Reject(ReadDiagnostic(read, Pad236BoardField::ImplementationPointer,
                                     implementationPointerAddress, pointer.size()));
    const uint64_t implementation = LittleEndian(pointer.data(), pointer.size());
    if (!implementation)
        return Reject(Diagnostic(Pad236BoardDiagnosticCode::NullImplementationAddress,
                                 Pad236BoardField::ImplementationPointer,
                                 implementationPointerAddress, pointer.size()));

    uint64_t pool = 0, poolEnd = 0, table = 0, tableEnd = 0;
    if (!Add(implementation, PoolOffset, pool) || !Add(pool, PoolCount * BlockStride, poolEnd))
        return Reject(Diagnostic(Pad236BoardDiagnosticCode::AddressOverflow,
                                 Pad236BoardField::Pool, implementation,
                                 PoolOffset + PoolCount * BlockStride));
    if (!Add(implementation, TableOffset, table) ||
        !Add(table, PoolCount * GuestPointerBytes, tableEnd))
        return Reject(Diagnostic(Pad236BoardDiagnosticCode::AddressOverflow,
                                 Pad236BoardField::PointerTable, implementation,
                                 TableOffset + PoolCount * GuestPointerBytes));

    Pad236RawBoard board;
    board.gameAddress = input.gameAddress;
    board.implementationAddress = implementation;
    board.poolAddress = pool;
    board.pointerTableAddress = table;
    board.width = width;
    board.height = height;
    board.cells.reserve(size_t(width) * height);
    std::array<bool, PoolCount> used{};
    for (uint8_t row = 0; row < height; ++row) {
        for (uint8_t column = 0; column < width; ++column) {
            // Whitelisted dimensions bound both products, and tableEnd was checked.
            const uint64_t slot = table + (uint64_t(row) * TableRowStride + column) * GuestPointerBytes;
            read = reader.Read(slot, pointer.size(), pointer.data());
            if (read != ReadStatus::Complete)
                return Reject(ReadDiagnostic(read, Pad236BoardField::BlockPointer,
                                             slot, pointer.size(), row, column));
            const uint64_t blockAddress = LittleEndian(pointer.data(), pointer.size());
            const auto blockFail = [&](Pad236BoardDiagnosticCode code) {
                auto failure = Diagnostic(code, Pad236BoardField::BlockPointer,
                                          slot, GuestPointerBytes, -1, row, column);
                failure.rawPointerValue = blockAddress;
                return Reject(failure);
            };
            if (!blockAddress) return blockFail(Pad236BoardDiagnosticCode::NullBlockAddress);
            if (blockAddress < pool || blockAddress >= poolEnd)
                return blockFail(Pad236BoardDiagnosticCode::BlockOutsidePool);
            const uint64_t poolOffset = blockAddress - pool;
            if (poolOffset % BlockStride) return blockFail(Pad236BoardDiagnosticCode::BlockMisaligned);
            const size_t poolIndex = size_t(poolOffset / BlockStride);
            if (used[poolIndex]) return blockFail(Pad236BoardDiagnosticCode::DuplicateBlockAddress);
            used[poolIndex] = true;

            Pad236RawBlock block;
            block.guestAddress = blockAddress;
            block.poolIndex = uint16_t(poolIndex);
            block.nativeRow = row;
            block.nativeColumn = column;
            read = reader.Read(blockAddress, block.rawPrefix.size(), block.rawPrefix.data());
            if (read != ReadStatus::Complete)
                return Reject(ReadDiagnostic(read, Pad236BoardField::BlockPrefix,
                                             blockAddress, block.rawPrefix.size(), row, column));
            block.rawColorByte = block.rawPrefix[0];
            block.rawState = uint16_t(LittleEndian(block.rawPrefix.data() + 0x12, 2));
            block.rawWord04 = uint32_t(LittleEndian(block.rawPrefix.data() + 0x04, 4));
            block.rawSignedHalfword1c = SignedHalfword(block.rawPrefix.data() + 0x1c);
            block.rawSignedHalfword1e = SignedHalfword(block.rawPrefix.data() + 0x1e);
            board.cells.push_back(block);
        }
    }
    Pad236BoardDecodeResult result;
    result.status = Pad236BoardDecodeStatus::EvidencePartial;
    result.diagnostics.push_back(Diagnostic(Pad236BoardDiagnosticCode::RuntimeContextUnverified,
                                            Pad236BoardField::Snapshot));
    result.board = std::move(board);
    return result;
}
}
