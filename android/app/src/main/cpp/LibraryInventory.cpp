#include "LibraryInventory.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/uio.h>
#include <unistd.h>

namespace padcheaper {
bool ParseMapping(const std::string &line, Mapping &output) {
    unsigned long long start = 0, end = 0, offset = 0, inode = 0;
    unsigned major = 0, minor = 0; int consumed = 0;
    char permissions[5] = {};
    if (std::sscanf(line.c_str(), "%llx-%llx %4s %llx %x:%x %llu %n",
        &start, &end, permissions, &offset, &major, &minor, &inode, &consumed) != 7 ||
        start >= end || std::strlen(permissions) != 4 || consumed <= 0 ||
        (permissions[0] != 'r' && permissions[0] != '-') ||
        (permissions[1] != 'w' && permissions[1] != '-') ||
        (permissions[2] != 'x' && permissions[2] != '-') ||
        (permissions[3] != 'p' && permissions[3] != 's')) return false;
    Mapping value{};
    value.start = start; value.end = end; value.offset = offset;
    value.readable = permissions[0] == 'r'; value.writable = permissions[1] == 'w';
    value.executable = permissions[2] == 'x'; value.path = line.substr(size_t(consumed));
    output = value;
    return true;
}

bool IsGameLibrary(const std::string &path) {
    const size_t slash = path.find_last_of('/');
    return slash != std::string::npos && path.substr(slash + 1) == "libpad.so";
}

bool ReadableRange(const std::vector<Mapping> &mappings, uint64_t address, uint64_t size) {
    if (!size || size > UINT64_MAX - address) return false;
    for (const auto &mapping : mappings)
        if (mapping.readable && mapping.start <= address && address + size <= mapping.end) return true;
    return false;
}

uint16_t ElfMachine(const unsigned char *bytes, size_t size) {
    if (!bytes || size < 20 || std::memcmp(bytes, "\x7f" "ELF", 4) || bytes[4] != 2 || bytes[5] != 1) return 0;
    return uint16_t(bytes[18]) | uint16_t(bytes[19] << 8);
}

LibraryInventory InspectGameLibrary() {
    LibraryInventory result;
    std::ifstream stream("/proc/self/maps");
    if (!stream) { result.status = "maps-unavailable"; return result; }
    std::vector<Mapping> mappings;
    std::string line, selectedPath; uint64_t header = 0; unsigned headers = 0, count = 0;
    while (std::getline(stream, line)) {
        if (++count > 32768 || line.size() > 8192) { result.status = "maps-limit"; return result; }
        Mapping mapping;
        if (!ParseMapping(line, mapping) || !IsGameLibrary(mapping.path)) continue;
        if (!selectedPath.empty() && selectedPath != mapping.path) { result.status = "ambiguous-library"; return result; }
        selectedPath = mapping.path;
        if (!mapping.offset) { header = mapping.start; ++headers; }
        if (mapping.end - mapping.start > UINT64_MAX - result.mappedBytes) { result.status = "maps-overflow"; return result; }
        result.mappedBytes += mapping.end - mapping.start;
        result.executableMapping |= mapping.executable;
        mappings.push_back(mapping);
    }
    if (stream.bad()) { result.status = "maps-read-failed"; return result; }
    if (mappings.empty()) return result;
    // Only the ELF header is read. Never publish addresses or inspect heap/account data.
    if (headers != 1 || !ReadableRange(mappings, header, 20) || header > UINTPTR_MAX) {
        result.status = "header-unavailable"; return result;
    }
    unsigned char bytes[20]{};
    // Read exactly this process's mapped header through the bounded OS copy API.
    // Opening /proc/self/mem terminated the tested game process without an error;
    // no direct pointer dereference or permission-changing fallback is attempted.
    iovec local{bytes, sizeof(bytes)};
    iovec remote{reinterpret_cast<void *>(uintptr_t(header)), sizeof(bytes)};
    const ssize_t read = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
    if (read != sizeof(bytes)) { result.status = "header-read-failed"; return result; }
    result.machine = ElfMachine(bytes, sizeof(bytes));
    result.status = result.machine == 183 ? "arm64-image-observed" : "unexpected-library-architecture";
    // Mapped ELF identity alone grants no board reads or native function calls.
    return result;
}
}
