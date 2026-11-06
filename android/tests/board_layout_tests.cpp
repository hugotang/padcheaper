#include "BoardLayout.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <utility>

namespace {
using namespace padcheaper;
constexpr uint64_t Game = 0x100002000;
constexpr uint64_t Implementation = 0x200004000;
constexpr uint64_t Pool = Implementation + 0x98958;
constexpr uint64_t Table = Implementation + 0xa3558;
constexpr uint64_t Stride = 0xac;
constexpr uint64_t Maximum = std::numeric_limits<uint64_t>::max();

void Put(std::vector<uint8_t> &bytes, size_t offset, uint64_t value, size_t count) {
    assert(offset <= bytes.size() && count <= bytes.size() - offset);
    for (size_t i = 0; i < count; ++i) bytes[offset + i] = uint8_t(value >> (8 * i));
}

void Put(Pad236OfflineBoardInput &input, uint64_t address, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        bool found = false;
        for (auto &segment : input.segments) {
            if (address + i >= segment.guestAddress &&
                address + i - segment.guestAddress < segment.bytes.size()) {
                segment.bytes[size_t(address + i - segment.guestAddress)] = uint8_t(value >> (8 * i));
                found = true;
                break;
            }
        }
        assert(found);
    }
}

uint16_t Index(size_t logical) { return uint16_t((logical * 37 + 255) % 256); }
uint64_t Block(size_t logical) { return Pool + Index(logical) * Stride; }
uint64_t Slot(uint8_t width, size_t logical) {
    return Table + ((logical / width) * 16 + logical % width) * 8;
}
uint16_t State(size_t logical) {
    constexpr uint16_t states[] = {0, 3, 4, 6, 0xffff, 0x1234};
    return states[logical % 6];
}

Pad236OfflineBoardInput Fixture(uint8_t width = 6, uint8_t height = 5) {
    Pad236OfflineBoardInput input;
    input.gameAddress = Game;
    input.segments.push_back({Game, std::vector<uint8_t>(0x90, 0x5a)});
    input.segments.push_back({Pool, std::vector<uint8_t>(256 * Stride, 0xcd)});
    // Inactive slots are deliberately invalid, so decoding all 256 would fail.
    input.segments.push_back({Table, std::vector<uint8_t>(256 * 8, 0xa5)});
    Put(input, Game + 0x70, width, 1);
    Put(input, Game + 0x71, height, 1);
    Put(input, Game + 0x88, Implementation, 8);
    for (size_t logical = 0; logical < size_t(width) * height; ++logical) {
        Put(input, Slot(width, logical), Block(logical), 8);
        Put(input, Block(logical), uint8_t(0xf0 + logical), 1);
        Put(input, Block(logical) + 0x04, 0x89abcdefU + logical, 4);
        Put(input, Block(logical) + 0x12, State(logical), 2);
        Put(input, Block(logical) + 0x1c, 0xff9c, 2); // -100, no screen conversion.
        Put(input, Block(logical) + 0x1e, 0x8000, 2); // INT16_MIN.
        Put(input, Block(logical) + 0x23, uint8_t(logical ^ 0x3c), 1);
    }
    return input;
}

void SplitAt(Pad236OfflineBoardInput &input, uint64_t address) {
    for (size_t i = 0; i < input.segments.size(); ++i) {
        auto &segment = input.segments[i];
        if (address <= segment.guestAddress || address - segment.guestAddress >= segment.bytes.size())
            continue;
        const auto offset = size_t(address - segment.guestAddress);
        Pad236OwnedGuestSegment tail{address, std::vector<uint8_t>(segment.bytes.begin() + offset,
                                                                 segment.bytes.end())};
        segment.bytes.resize(offset);
        input.segments.push_back(std::move(tail));
        return;
    }
    assert(false);
}

void RemoveByte(Pad236OfflineBoardInput &input, uint64_t address) {
    for (size_t i = 0; i < input.segments.size(); ++i) {
        auto &segment = input.segments[i];
        if (address < segment.guestAddress || address - segment.guestAddress >= segment.bytes.size())
            continue;
        const auto offset = size_t(address - segment.guestAddress);
        Pad236OwnedGuestSegment tail{address + 1,
            std::vector<uint8_t>(segment.bytes.begin() + offset + 1, segment.bytes.end())};
        segment.bytes.resize(offset);
        if (segment.bytes.empty()) input.segments.erase(input.segments.begin() + i);
        if (!tail.bytes.empty()) input.segments.push_back(std::move(tail));
        return;
    }
    assert(false);
}

Pad236BoardDecodeResult Partial(const Pad236OfflineBoardInput &input) {
    auto result = DecodePad236OfflineRawBoard(input);
    assert(result.status == Pad236BoardDecodeStatus::EvidencePartial);
    assert(result.board);
    assert(result.diagnostics.size() == 1);
    assert(result.diagnostics[0].code == Pad236BoardDiagnosticCode::RuntimeContextUnverified);
    return result;
}

Pad236BoardDiagnostic Rejected(const Pad236OfflineBoardInput &input, Pad236BoardDiagnosticCode code,
                               Pad236BoardField field) {
    const auto result = DecodePad236OfflineRawBoard(input);
    assert(result.status == Pad236BoardDecodeStatus::InvalidSnapshot);
    assert(!result.board); // No partially accepted cells, even for late failures.
    assert(result.diagnostics.size() == 1);
    assert(result.diagnostics[0].code == code);
    assert(result.diagnostics[0].field == field);
    return result.diagnostics[0];
}

void BoardSizesAndRawFields() {
    for (uint8_t width : {5, 6, 7}) {
        const uint8_t height = uint8_t(width - 1);
        auto input = Fixture(width, height);
        const auto result = Partial(input);
        const auto &board = *result.board;
        assert(board.width == width && board.height == height);
        assert(board.gameAddress == Game && board.implementationAddress == Implementation);
        assert(board.poolAddress == Pool && board.pointerTableAddress == Table);
        assert(board.cells.size() == size_t(width) * height);
        assert(board.cells[0].poolIndex == 255); // Last block is a valid pool member.
        for (size_t logical = 0; logical < board.cells.size(); ++logical) {
            const auto &block = board.cells[logical];
            assert(block.guestAddress == Block(logical) && block.poolIndex == Index(logical));
            assert(block.nativeRow == logical / width && block.nativeColumn == logical % width);
            assert(block.rawColorByte == uint8_t(0xf0 + logical));
            assert(block.rawState == State(logical));
            assert(block.rawWord04 == 0x89abcdefU + logical);
            assert(block.rawSignedHalfword1c == -100 && block.rawSignedHalfword1e == -32768);
            assert(block.rawPrefix[0x23] == uint8_t(logical ^ 0x3c));
        }
        // Unknown values are retained; state changes cannot silently rewrite color.
        Put(input, Block(0), 0xff, 1);
        Put(input, Block(0) + 0x12, 0x8006, 2);
        const auto unknown = Partial(input);
        assert(unknown.board->cells[0].rawColorByte == 0xff);
        assert(unknown.board->cells[0].rawState == 0x8006);
    }
    auto compact = Fixture();
    // A width-packed table must not accidentally satisfy the native 16-wide layout.
    compact.segments[2].bytes.assign(6 * 5 * 8, 0);
    for (size_t logical = 0; logical < 30; ++logical)
        Put(compact.segments[2].bytes, logical * 8, Block(logical), 8);
    Rejected(compact, Pad236BoardDiagnosticCode::MissingRange, Pad236BoardField::BlockPointer);
}

void FragmentCoverage() {
    auto input = Fixture();
    SplitAt(input, Game + 0x71);
    SplitAt(input, Game + 0x8b); // Implementation pointer crosses a fragment boundary.
    SplitAt(input, Slot(6, 0) + 7); // An eight-byte table pointer also crosses one.
    SplitAt(input, Block(0) + 0x13); // Split within rawState's two bytes.
    SplitAt(input, Block(0) + 0x23);
    std::reverse(input.segments.begin(), input.segments.end());
    Partial(input); // Unsorted adjacent fragments are accepted without modifying input.

    const std::array<std::pair<uint64_t, Pad236BoardField>, 4> gaps{{
        {Game + 0x71, Pad236BoardField::Dimensions},
        {Game + 0x8f, Pad236BoardField::ImplementationPointer},
        {Slot(6, 0) + 7, Pad236BoardField::BlockPointer},
        {Block(0) + 0x23, Pad236BoardField::BlockPrefix}
    }};
    for (const auto &gap : gaps) {
        auto missing = Fixture();
        RemoveByte(missing, gap.first);
        const auto diagnostic = Rejected(missing, Pad236BoardDiagnosticCode::MissingRange, gap.second);
        if (gap.second == Pad236BoardField::BlockPointer) assert(diagnostic.requiredBytes == 8);
        if (gap.second == Pad236BoardField::BlockPrefix) assert(diagnostic.requiredBytes == 0x24);
    }
    // There is no requirement to capture game bytes, inactive slots or unused pool
    // entries that are not part of an actually consumed field or active block prefix.
    auto sparse = Fixture();
    const auto source = sparse;
    sparse.segments.clear();
    sparse.segments.push_back({Game + 0x70, {6, 5}});
    std::vector<uint8_t> implementation(8);
    Put(implementation, 0, Implementation, 8);
    sparse.segments.push_back({Game + 0x88, implementation});
    for (size_t logical = 0; logical < 30; ++logical) {
        std::vector<uint8_t> pointer(8);
        Put(pointer, 0, Block(logical), 8);
        sparse.segments.push_back({Slot(6, logical), pointer});
        const auto offset = size_t(Block(logical) - Pool);
        const auto &pool = source.segments[1].bytes;
        sparse.segments.push_back({Block(logical),
            std::vector<uint8_t>(pool.begin() + offset, pool.begin() + offset + 0x24)});
    }
    Partial(sparse);
    RemoveByte(sparse, Block(29) + 0x23);
    const auto late = Rejected(sparse, Pad236BoardDiagnosticCode::MissingRange,
                               Pad236BoardField::BlockPrefix);
    assert(late.nativeRow == 4 && late.nativeColumn == 5 && late.guestAddress == Block(29));
}

void InvalidDimensionsAndPointers() {
    for (const auto &dims : std::array<std::pair<uint8_t, uint8_t>, 7>{{
            {0, 0}, {0xff, 5}, {6, 0xff}, {8, 7}, {6, 4}, {5, 6}, {16, 16}}}) {
        auto input = Fixture();
        Put(input, Game + 0x70, dims.first, 1);
        Put(input, Game + 0x71, dims.second, 1);
        Rejected(input, Pad236BoardDiagnosticCode::UnsupportedDimensions, Pad236BoardField::Dimensions);
    }
    auto input = Fixture();
    input.gameAddress = 0;
    Rejected(input, Pad236BoardDiagnosticCode::NullGameAddress, Pad236BoardField::GameAddress);
    input = Fixture();
    Put(input, Game + 0x88, 0, 8);
    Rejected(input, Pad236BoardDiagnosticCode::NullImplementationAddress,
             Pad236BoardField::ImplementationPointer);
    for (const auto &entry : std::array<std::pair<uint64_t, Pad236BoardDiagnosticCode>, 6>{{
            {0, Pad236BoardDiagnosticCode::NullBlockAddress},
            {Pool - 1, Pad236BoardDiagnosticCode::BlockOutsidePool},
            {Table, Pad236BoardDiagnosticCode::BlockOutsidePool},
            {Pool + 1, Pad236BoardDiagnosticCode::BlockMisaligned},
            {Pool + Stride + 1, Pad236BoardDiagnosticCode::BlockMisaligned},
            {Block(0) | (uint64_t(0xab) << 56), Pad236BoardDiagnosticCode::BlockOutsidePool}}}) {
        input = Fixture();
        Put(input, Slot(6, 0), entry.first, 8);
        const auto diagnostic = Rejected(input, entry.second, Pad236BoardField::BlockPointer);
        assert(diagnostic.nativeRow == 0 && diagnostic.nativeColumn == 0);
        assert(diagnostic.guestAddress == Slot(6, 0) && diagnostic.requiredBytes == 8);
        assert(diagnostic.rawPointerValue && *diagnostic.rawPointerValue == entry.first);
    }
    input = Fixture();
    Put(input, Slot(6, 29), Block(0), 8);
    const auto duplicate = Rejected(input, Pad236BoardDiagnosticCode::DuplicateBlockAddress,
                                    Pad236BoardField::BlockPointer);
    assert(duplicate.nativeRow == 4 && duplicate.nativeColumn == 5);
    input = Fixture();
    Put(input, Game + 0x88, Implementation | (uint64_t(0xab) << 56), 8);
    Rejected(input, Pad236BoardDiagnosticCode::MissingRange, Pad236BoardField::BlockPointer);
    input = Fixture();
    input.gameAddress |= uint64_t(0xab) << 56;
    Rejected(input, Pad236BoardDiagnosticCode::MissingRange, Pad236BoardField::Dimensions);
}

void InvalidSegmentsAndAddressArithmetic() {
    Pad236OfflineBoardInput input;
    Rejected(input, Pad236BoardDiagnosticCode::EmptySnapshot, Pad236BoardField::Snapshot);
    input = Fixture();
    input.segments.push_back({0x800, {}});
    Rejected(input, Pad236BoardDiagnosticCode::EmptySegment, Pad236BoardField::Snapshot);
    input = Fixture();
    input.segments.push_back({Maximum, {1}});
    Rejected(input, Pad236BoardDiagnosticCode::SegmentAddressOverflow, Pad236BoardField::Snapshot);
    input = Fixture();
    input.segments.push_back({Game + 0x70, {6, 5}}); // Same data still overlaps.
    Rejected(input, Pad236BoardDiagnosticCode::OverlappingSegments, Pad236BoardField::Snapshot);
    input = Fixture();
    input.segments.push_back({0x400000000, std::vector<uint8_t>(Pad236MaxSnapshotBytes, 0)});
    Rejected(input, Pad236BoardDiagnosticCode::SnapshotTooLarge, Pad236BoardField::Snapshot);
    input = Fixture();
    input.segments.resize(Pad236MaxSnapshotSegments + 1);
    Rejected(input, Pad236BoardDiagnosticCode::TooManySegments, Pad236BoardField::Snapshot);

    input = Fixture();
    input.gameAddress = Maximum - 0x60;
    Rejected(input, Pad236BoardDiagnosticCode::AddressOverflow, Pad236BoardField::Dimensions);
    input = Fixture();
    input.gameAddress = Maximum - 0x70; // Offset fits, complete dimension range does not.
    Rejected(input, Pad236BoardDiagnosticCode::AddressOverflow, Pad236BoardField::Dimensions);
    input = Fixture();
    input.gameAddress = Maximum - 0x80;
    input.segments.push_back({input.gameAddress + 0x70, {6, 5}});
    Rejected(input, Pad236BoardDiagnosticCode::AddressOverflow,
             Pad236BoardField::ImplementationPointer);
    input = Fixture();
    input.gameAddress = Maximum - 0x8b; // Pointer start fits but all eight bytes do not.
    input.segments.push_back({input.gameAddress + 0x70, {6, 5}});
    Rejected(input, Pad236BoardDiagnosticCode::AddressOverflow,
             Pad236BoardField::ImplementationPointer);
    input = Fixture();
    Put(input, Game + 0x88, Maximum - 0x98957, 8); // Pool start overflows.
    Rejected(input, Pad236BoardDiagnosticCode::AddressOverflow, Pad236BoardField::Pool);
    input = Fixture();
    Put(input, Game + 0x88, Maximum - 0xa3557, 8); // Pool end overflows.
    Rejected(input, Pad236BoardDiagnosticCode::AddressOverflow, Pad236BoardField::Pool);
    input = Fixture();
    Put(input, Game + 0x88, Maximum - 0xa3558 - 100, 8); // Full table extent overflows.
    Rejected(input, Pad236BoardDiagnosticCode::AddressOverflow, Pad236BoardField::PointerTable);
}
}

int main() {
    BoardSizesAndRawFields();
    FragmentCoverage();
    InvalidDimensionsAndPointers();
    InvalidSegmentsAndAddressArithmetic();
}
