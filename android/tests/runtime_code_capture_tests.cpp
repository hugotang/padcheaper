#include "RuntimeCodeCapture.h"
#include <array>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
using namespace padcheaper;
Mapping Map(uint64_t start, uint64_t end, uint64_t offset = 0,
            const char *path = "/test/libpad.so", bool writable = false) {
    Mapping value;
    value.start = start; value.end = end; value.offset = offset;
    value.readable = true; value.writable = writable; value.path = path;
    return value;
}
void Expect(const std::vector<Mapping> &maps, const char *status) {
    RuntimeCodePlan plan{77, 99};
    assert(std::strcmp(PlanRuntimeCode(maps, plan), status) == 0);
    if (std::strcmp(status, "ready")) assert(plan.base == 0 && plan.size == 0);
    else assert(plan.base == 0x40000000 && plan.size == RuntimeCodeBytes);
}

// Independent representation of the pinned ELF metadata, never a game binary.
std::array<unsigned char, RuntimeCodeHeaderBytes> FixtureHeader() {
    std::array<unsigned char, RuntimeCodeHeaderBytes> value{};
    auto write = [&](size_t offset, uint64_t number, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) value[offset + i] = uint8_t(number >> (i * 8));
    };
    const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
    std::memcpy(value.data(), ident, sizeof(ident));
    write(16, 3, 2); write(18, 183, 2); write(20, 1, 4);
    write(32, 64, 8); write(40, 30733560, 8);
    write(52, 64, 2); write(54, 56, 2); write(56, 8, 2);
    write(58, 64, 2); write(60, 29, 2); write(62, 27, 2);
    auto ph = [&](size_t index, uint32_t type, uint32_t flags, uint64_t offset, uint64_t address,
                  uint64_t size, uint64_t memory, uint64_t alignment) {
        const size_t base = 64 + index * 56;
        write(base, type, 4); write(base + 4, flags, 4); write(base + 8, offset, 8);
        write(base + 16, address, 8); write(base + 24, address, 8);
        write(base + 32, size, 8); write(base + 40, memory, 8); write(base + 48, alignment, 8);
    };
    ph(0, 6, 4, 64, 64, 448, 448, 8);
    ph(1, 1, 5, 0, 0, 0x107e730, 0x107e730, 0x4000);
    ph(2, 1, 6, 0x107e730, 0x1082730, 0x7fde0, 0x808d0, 0x4000);
    ph(3, 1, 6, 0x10fe510, 0x1106510, 0x8e118, 0x19ce228, 0x4000);
    ph(4, 2, 6, 0x10dde68, 0x10e1e68, 0x240, 0x240, 8);
    ph(5, 0x6474e550, 4, 0x417f8c, 0x417f8c, 0x2ac94, 0x2ac94, 4);
    ph(6, 0x6474e551, 6, 0, 0, 0, 0, 0);
    ph(7, 4, 4, 0x238, 0x238, 0xbc, 0xbc, 4);
    return value;
}
}

int main() {
    using namespace padcheaper;
    const uint64_t base = 0x40000000, end = base + RuntimeCodeBytes;
    Expect({}, "not-loaded");
    Expect({Map(base, end)}, "ready"); // Houdini r--p, not executable.
    Expect({Map(base + 0x1000, end, 0x1000), Map(base, base + 0x1000)}, "ready");
    Expect({Map(base, end, 0, "/test/libpad.so", true)}, "code-mapping-not-readonly-library");
    auto unreadable = Map(base, end); unreadable.readable = false;
    Expect({unreadable}, "code-mapping-not-readonly-library");
    Expect({Map(base, base + 0x1000), Map(base + 0x2000, end, 0x2000)}, "code-mapping-gap-or-overlap");
    Expect({Map(base, base + 0x2000), Map(base + 0x1000, end, 0x1000)}, "code-mapping-gap-or-overlap");
    Expect({Map(base, base + 0x1000), Map(base + 0x1000, end, 0x2000)}, "code-file-offset-mismatch");
    Expect({Map(base, end - 1)}, "code-mapping-incomplete");
    Expect({Map(base, base + 0x1000), Map(base + 0x1000, end, 0x1000, "[anon:other]")}, "code-mapping-not-readonly-library");
    Expect({Map(base, end), Map(end + 0x1000, end + 0x2000, 0, "/another/libpad.so")}, "ambiguous-library");
    Expect({Map(base, end), Map(end + 0x1000, end + 0x2000)}, "code-base-unavailable");
    Expect({Map(UINT64_MAX - 100, UINT64_MAX)}, "code-base-unavailable");
    auto header = FixtureHeader();
    assert(MatchesRuntimeCodeHeader(header.data(), header.size()));
    assert(!MatchesRuntimeCodeHeader(nullptr, header.size()));
    assert(!MatchesRuntimeCodeHeader(header.data(), header.size() - 1));
    header[120 + 4] ^= 2; // PT_LOAD flags changed even though ELF machine is unchanged.
    assert(!MatchesRuntimeCodeHeader(header.data(), header.size()));
    header = FixtureHeader();

    // A synthetic named mapping exercises both full copy passes and exact output.
    // No game's actual library, process or data is used by this executable.
    char directory[] = "padcheaper-code-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/libpad.so";
    const std::string outPath = std::string(directory) + "/capture.tmp";
    int source = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    int output = open(outPath.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    assert(source >= 0 && output >= 0);
    assert(ftruncate(source, off_t(RuntimeCodeBytes)) == 0);
    assert(pwrite(source, header.data(), header.size(), 0) == ssize_t(header.size()));
    const unsigned char tail = 0xa5;
    assert(pwrite(source, &tail, 1, off_t(RuntimeCodeBytes - 1)) == 1);
    void *mapping = mmap(nullptr, RuntimeCodeBytes, PROT_READ, MAP_PRIVATE, source, 0);
    assert(mapping != MAP_FAILED);
    const auto captured = CaptureRuntimeCode(output);
    assert(std::strcmp(captured.status, "code-captured") == 0);
    assert(captured.capturedBytes == RuntimeCodeBytes && captured.stableRead && captured.mappingsStable);
    struct stat info{};
    assert(fstat(output, &info) == 0 && info.st_size == off_t(RuntimeCodeBytes));
    std::array<unsigned char, RuntimeCodeHeaderBytes> actual{};
    assert(pread(output, actual.data(), actual.size(), 0) == ssize_t(actual.size()) && actual == header);
    unsigned char observed = 0;
    assert(pread(output, &observed, 1, off_t(RuntimeCodeBytes - 1)) == 1 && observed == tail);
    // Nonempty outputs are rejected without truncating the caller's existing file.
    assert(std::strcmp(CaptureRuntimeCode(output).status, "output-not-empty-owned-regular-file") == 0);
    assert(fstat(output, &info) == 0 && info.st_size == off_t(RuntimeCodeBytes));
    assert(ftruncate(output, 0) == 0);
    header[18] = 62;
    assert(pwrite(source, header.data(), header.size(), 0) == ssize_t(header.size()));
    assert(std::strcmp(CaptureRuntimeCode(output).status, "code-header-mismatch") == 0);
    assert(fstat(output, &info) == 0 && info.st_size == 0);
    assert(munmap(mapping, RuntimeCodeBytes) == 0);
    assert(std::strcmp(CaptureRuntimeCode(output).status, "not-loaded") == 0);
    assert(std::strcmp(CaptureRuntimeCode(-1).status, "output-descriptor-unavailable") == 0);
    int readonly = open(outPath.c_str(), O_RDONLY | O_CLOEXEC);
    assert(readonly >= 0);
    assert(std::strcmp(CaptureRuntimeCode(readonly).status, "output-not-empty-owned-regular-file") == 0);
    assert(close(readonly) == 0 && close(source) == 0 && close(output) == 0);
    assert(unlink(path.c_str()) == 0 && unlink(outPath.c_str()) == 0 && rmdir(directory) == 0);
}
