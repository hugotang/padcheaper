#include "LibraryInventory.h"
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    using namespace padcheaper;
    Mapping output{}; output.start = 77;
    assert(!ParseMapping("invalid", output) && output.start == 77);
    assert(!ParseMapping("2000-1000 r--p 0000 08:01 1 /lib/arm64/libpad.so", output));
    assert(!ParseMapping("1000-2000 rwxz 0000 08:01 1 /lib/arm64/libpad.so", output));
    assert(ParseMapping("400020800000-40002187f000 r--p 00000000 08:23 6684737 /lib/arm64/libpad.so", output));
    assert(output.readable && !output.executable && output.offset == 0);
    assert(IsGameLibrary(output.path));
    assert(!IsGameLibrary("/lib/libpad.so.backup"));
    assert(!IsGameLibrary("/lib/libpad.so (deleted)"));
    assert(!IsGameLibrary("/lib/libpadhelper.so"));
    const std::vector<Mapping> mappings{output};
    assert(ReadableRange(mappings, output.start, 20));
    assert(!ReadableRange(mappings, output.end - 1, 20));
    assert(!ReadableRange(mappings, UINT64_MAX - 1, 20));
    assert(!ReadableRange(mappings, output.start, 0));
    unsigned char header[20]{};
    std::memcpy(header, "\x7f" "ELF", 4); header[4] = 2; header[5] = 1; header[18] = 183;
    assert(ElfMachine(header, sizeof(header)) == 183);
    assert(!ElfMachine(header, 19));
    header[5] = 2; assert(!ElfMachine(header, sizeof(header)));

    // Exercise the complete maps + safe self-copy path, including a real mapping.
    // Run from a writable test directory; never map a game's actual library here.
    char directory[] = "padcheaper-inventory-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/libpad.so";
    int fd = open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    assert(fd >= 0);
    const size_t pageSize = size_t(sysconf(_SC_PAGESIZE));
    assert(ftruncate(fd, off_t(pageSize)) == 0);
    header[5] = 1;
    assert(write(fd, header, sizeof(header)) == sizeof(header));
    void *mapped = mmap(nullptr, pageSize, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(mapped != MAP_FAILED);
    const auto inventory = InspectGameLibrary();
    assert(inventory.status == "arm64-image-observed" && inventory.machine == 183);
    assert(inventory.mappedBytes == pageSize && !inventory.executableMapping);
    assert(munmap(mapped, pageSize) == 0);
    assert(close(fd) == 0);
    assert(unlink(path.c_str()) == 0 && rmdir(directory) == 0);
    assert(InspectGameLibrary().status == "not-loaded");
}
