#include "RuntimeCodeCapture.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

namespace padcheaper {
namespace {
// Exact first 512 bytes (ELF header + eight program headers), from the local
// artifact SHA-256 fec9fbc55d3a9b7945817420475a8c03fa608ecc4605535e373ca2d0fc738a8a.
// Metadata SHA-256: 4d1fef531ae14a6d2c12b368b02ced94cab841d365a17554ceb94c09e430f55e.
// PT_LOAD at PHDR index 1: flags=RX, offset=vaddr=0, filesz=memsz=0x107e730.
constexpr unsigned char ExpectedHeader[RuntimeCodeHeaderBytes] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0xb7, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0xf4, 0xd4, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x38, 0x00, 0x08, 0x00, 0x40, 0x00, 0x1d, 0x00, 0x1b, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0xe7, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x30, 0xe7, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x30, 0xe7, 0x07, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x30, 0x27, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00, 0x30, 0x27, 0x08, 0x01, 0x00, 0x00, 0x00, 0x00,
    0xe0, 0xfd, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00,
    0x10, 0xe5, 0x0f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x65, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x65, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x18, 0xe1, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x28, 0xe2, 0x9c, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x68, 0xde, 0x0d, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x68, 0x1e, 0x0e, 0x01, 0x00, 0x00, 0x00, 0x00, 0x68, 0x1e, 0x0e, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0xe5, 0x74, 0x64, 0x04, 0x00, 0x00, 0x00,
    0x8c, 0x7f, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8c, 0x7f, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x8c, 0x7f, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x94, 0xac, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x94, 0xac, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x51, 0xe5, 0x74, 0x64, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x38, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x38, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xbc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr size_t ChunkBytes = 64 * 1024;
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
bool Expired(Deadline deadline) { return Clock::now() >= deadline; }

struct MappingSnapshot {
    RuntimeCodePlan plan;
    // Exact lines preserve file device/inode, permissions, offsets and all mappings
    // of the selected library. Unrelated heap/thread mappings need not stay fixed.
    std::vector<std::string> libraryLines;
};

const char *ReadSnapshot(MappingSnapshot &snapshot, Deadline deadline) {
    std::ifstream stream("/proc/self/maps");
    if (!stream) return "maps-unavailable";
    std::vector<Mapping> mappings;
    std::string line;
    unsigned count = 0, selectedMajor = 0, selectedMinor = 0;
    unsigned long long selectedInode = 0;
    while (std::getline(stream, line)) {
        if (Expired(deadline)) return "capture-time-limit";
        if (++count > 32768 || line.size() > 8192) return "maps-limit";
        Mapping mapping;
        if (!ParseMapping(line, mapping)) return "maps-invalid";
        if (IsGameLibrary(mapping.path)) {
            unsigned long long start = 0, end = 0, offset = 0, inode = 0;
            unsigned major = 0, minor = 0;
            char permissions[5]{};
            if (std::sscanf(line.c_str(), "%llx-%llx %4s %llx %x:%x %llu",
                &start, &end, permissions, &offset, &major, &minor, &inode) != 7 || !inode)
                return "library-file-identity-unavailable";
            if (!snapshot.libraryLines.empty() &&
                (inode != selectedInode || major != selectedMajor || minor != selectedMinor))
                return "library-file-identity-ambiguous";
            selectedInode = inode; selectedMajor = major; selectedMinor = minor;
            snapshot.libraryLines.push_back(line);
        }
        mappings.push_back(std::move(mapping));
    }
    if (stream.bad()) return "maps-read-failed";
    return PlanRuntimeCode(mappings, snapshot.plan);
}

bool CopyMemory(uint64_t address, void *buffer, size_t size) {
    if (!size || size > ChunkBytes || address > UINTPTR_MAX || size - 1 > UINTPTR_MAX - address) return false;
    iovec local{buffer, size};
    iovec remote{reinterpret_cast<void *>(uintptr_t(address)), size};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == ssize_t(size);
}

bool HeaderMatches(uint64_t base) {
    std::array<unsigned char, RuntimeCodeHeaderBytes> header{};
    return CopyMemory(base, header.data(), header.size()) && MatchesRuntimeCodeHeader(header.data(), header.size());
}

bool SameSnapshot(const MappingSnapshot &first, const MappingSnapshot &next) {
    return first.plan.base == next.plan.base && first.plan.size == next.plan.size &&
        first.libraryLines == next.libraryLines;
}

struct Output {
    int fd = -1;
    bool accepted = false, complete = false;
    ~Output() {
        if (fd >= 0) {
            if (accepted && !complete) (void) ftruncate(fd, 0);
            close(fd);
        }
    }
    RuntimeCodeResult fail(const char *status) {
        if (accepted && ftruncate(fd, 0) != 0) return {"output-cleanup-failed", 0, false, false};
        return {status, 0, false, false};
    }
};
}

bool MatchesRuntimeCodeHeader(const unsigned char *bytes, size_t size) {
    return bytes && size == sizeof(ExpectedHeader) && std::memcmp(bytes, ExpectedHeader, size) == 0;
}

const char *PlanRuntimeCode(const std::vector<Mapping> &mappings, RuntimeCodePlan &output) {
    output = {};
    if (mappings.size() > 32768) return "maps-limit";
    std::string path;
    uint64_t base = 0;
    unsigned headers = 0;
    for (const auto &mapping : mappings) if (IsGameLibrary(mapping.path)) {
        if (!path.empty() && path != mapping.path) return "ambiguous-library";
        path = mapping.path;
        if (mapping.offset == 0) { base = mapping.start; ++headers; }
    }
    if (path.empty()) return "not-loaded";
    if (headers != 1 || !base || base > UINTPTR_MAX || RuntimeCodeBytes > UINTPTR_MAX - base)
        return "code-base-unavailable";
    const uint64_t end = base + RuntimeCodeBytes;
    std::vector<const Mapping *> covered;
    for (const auto &mapping : mappings) {
        if (mapping.start >= mapping.end) return "maps-invalid";
        if (mapping.end > base && mapping.start < end) covered.push_back(&mapping);
    }
    std::sort(covered.begin(), covered.end(), [](const Mapping *a, const Mapping *b) { return a->start < b->start; });
    uint64_t cursor = base;
    for (const Mapping *mapping : covered) {
        if (mapping->start != cursor) return "code-mapping-gap-or-overlap";
        if (mapping->path != path || !mapping->readable || mapping->writable)
            return "code-mapping-not-readonly-library";
        if (mapping->offset != mapping->start - base) return "code-file-offset-mismatch";
        cursor = std::min(end, mapping->end);
    }
    if (cursor != end) return "code-mapping-incomplete";
    output = {base, RuntimeCodeBytes};
    return "ready";
}

RuntimeCodeResult CaptureRuntimeCode(int outputFd) noexcept {
    Output output;
    try {
        const Deadline deadline = Clock::now() + std::chrono::seconds(10);
        if (outputFd < 0 || (output.fd = fcntl(outputFd, F_DUPFD_CLOEXEC, 0)) < 0)
            return output.fail("output-descriptor-unavailable");
        struct stat info{};
        const int flags = fcntl(output.fd, F_GETFL);
        if (flags < 0 || (flags & O_ACCMODE) != O_RDWR || (flags & O_APPEND) ||
            fstat(output.fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size != 0 ||
            info.st_uid != geteuid() || info.st_nlink != 1)
            return output.fail("output-not-empty-owned-regular-file");
        output.accepted = true;
        MappingSnapshot before;
        const char *status = ReadSnapshot(before, deadline);
        if (std::strcmp(status, "ready")) return output.fail(status);
        if (!HeaderMatches(before.plan.base)) return output.fail("code-header-mismatch");
        std::array<unsigned char, ChunkBytes> bytes{}, previous{};
        for (uint64_t offset = 0; offset < before.plan.size;) {
            if (Expired(deadline)) return output.fail("capture-time-limit");
            const size_t size = size_t(std::min<uint64_t>(bytes.size(), before.plan.size - offset));
            if (!CopyMemory(before.plan.base + offset, bytes.data(), size)) return output.fail("code-read-failed");
            if (pwrite(output.fd, bytes.data(), size, off_t(offset)) != ssize_t(size)) return output.fail("code-write-failed");
            offset += size;
        }
        MappingSnapshot between;
        status = ReadSnapshot(between, deadline);
        if (std::strcmp(status, "ready") || !SameSnapshot(before, between)) return output.fail("code-mappings-changed");
        // Re-copy every byte and compare to the first pass. This detects observed
        // changes; it does not claim a transactional snapshot or unpacked code.
        for (uint64_t offset = 0; offset < before.plan.size;) {
            if (Expired(deadline)) return output.fail("capture-time-limit");
            const size_t size = size_t(std::min<uint64_t>(bytes.size(), before.plan.size - offset));
            if (!CopyMemory(before.plan.base + offset, bytes.data(), size)) return output.fail("code-reread-failed");
            if (pread(output.fd, previous.data(), size, off_t(offset)) != ssize_t(size)) return output.fail("code-file-reread-failed");
            if (std::memcmp(bytes.data(), previous.data(), size)) return output.fail("code-content-changed");
            offset += size;
        }
        MappingSnapshot after;
        status = ReadSnapshot(after, deadline);
        if (std::strcmp(status, "ready") || !SameSnapshot(before, after)) return output.fail("code-mappings-changed");
        if (!HeaderMatches(before.plan.base)) return output.fail("code-header-changed");
        if (fstat(output.fd, &info) != 0 || info.st_size != off_t(before.plan.size)) return output.fail("code-output-size-changed");
        if (Expired(deadline)) return output.fail("capture-time-limit");
        output.complete = true;
        return {"code-captured", before.plan.size, true, true};
    } catch (...) { return output.fail("code-capture-failed"); }
}
}
